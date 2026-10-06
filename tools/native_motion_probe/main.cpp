// Synthetic GPU tests and local capture replay of the native motion pass.
// Synthetic expectations are analytic; replay inputs stay in ignored out/.
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/d3d12/d3d12_provider.h>
#include <rex/ui/d3d12/ssx_native_motion.h>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>
using namespace rex::ui::d3d12;
using Microsoft::WRL::ComPtr;
namespace {
void Require(bool ok, const char *text) {
  if (!ok)
    throw std::runtime_error(text);
}
void Check(HRESULT hr, const char *text) { Require(SUCCEEDED(hr), text); }
template <typename T> T Read(std::istream &file) {
  T value{};
  file.read(reinterpret_cast<char *>(&value), sizeof(value));
  Require(bool(file), "Truncated replay metadata");
  return value;
}
std::vector<char> ReadFile(const std::filesystem::path &path, size_t size) {
  Require(std::filesystem::file_size(path) == size,
          "Replay file size mismatch");
  std::ifstream file(path, std::ios::binary);
  std::vector<char> bytes(size);
  file.read(bytes.data(), size);
  Require(bool(file), "Replay file read");
  return bytes;
}
ComPtr<ID3D12Resource> Buffer(ID3D12Device *device, uint64_t bytes,
                              bool upload) {
  D3D12_RESOURCE_DESC d{};
  d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  d.Width = bytes;
  d.Height = d.DepthOrArraySize = d.MipLevels = d.SampleDesc.Count = 1;
  d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  D3D12_HEAP_PROPERTIES h{};
  h.Type = upload ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_READBACK;
  ComPtr<ID3D12Resource> result;
  Check(device->CreateCommittedResource(&h, D3D12_HEAP_FLAG_NONE, &d,
                                        upload
                                            ? D3D12_RESOURCE_STATE_GENERIC_READ
                                            : D3D12_RESOURCE_STATE_COPY_DEST,
                                        nullptr, IID_PPV_ARGS(&result)),
        "Buffer");
  return result;
}
void Transition(ID3D12GraphicsCommandList *list, ID3D12Resource *resource,
                D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
  D3D12_RESOURCE_BARRIER b{};
  b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  b.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before,
                  after};
  list->ResourceBarrier(1, &b);
}
struct Case {
  const char *name;
  uint32_t width = 64, height = 64;
  float shift = 0, shear = 0, camera = 0;
  bool occluded = false, uv_mismatch = false, index_mismatch = false;
  bool out_of_bounds = false, duplicate = false, reset = false;
  bool nonfused = false, jitter = false;
  bool ExpectedNative() const {
    return !uv_mismatch && !index_mismatch && !out_of_bounds && !duplicate &&
           !reset;
  }
};
struct Harness {
  std::unique_ptr<D3D12Provider> provider = D3D12Provider::Create();
  ComPtr<ID3D12Fence> fence;
  uint64_t submission = 0;
  Harness() {
    Require(bool(provider), "Provider");
    Check(provider->GetDevice()->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                             IID_PPV_ARGS(&fence)),
          "Fence");
  }
  void Finish(ID3D12GraphicsCommandList *list) {
    Check(list->Close(), "Close");
    ID3D12CommandList *lists[] = {list};
    provider->GetDirectQueue()->ExecuteCommandLists(1, lists);
    Check(provider->GetDirectQueue()->Signal(fence.Get(), submission),
          "Signal");
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    Require(event != nullptr, "Event");
    const HRESULT hr = fence->SetEventOnCompletion(submission, event);
    const DWORD status =
        SUCCEEDED(hr) ? WaitForSingleObject(event, 15000) : WAIT_FAILED;
    CloseHandle(event);
    Require(status == WAIT_OBJECT_0, "GPU timeout");
  }
  void Run(const Case &c) {
    SsxCameraMotion camera;
    SsxNativeMotion native;
    const uint32_t w = c.width, h = c.height;
    uint64_t native_pixels = 0, failures = 0;
    double max_error = 0;
    const bool capture_test =
        w == 64 && c.shear > 0 && std::string(c.name) == "deformation";
    const auto capture_dir =
        std::filesystem::current_path() /
        ("capture-readback-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    for (uint32_t step = 0; step < 2; ++step) {
      auto *device = provider->GetDevice();
      ComPtr<ID3D12CommandAllocator> allocator;
      ComPtr<ID3D12GraphicsCommandList> list;
      Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                           IID_PPV_ARGS(&allocator)),
            "Allocator");
      Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                      allocator.Get(), nullptr,
                                      IID_PPV_ARGS(&list)),
            "List");
      auto frame = std::make_shared<SsxFrameInputs>();
      auto &f = frame->metadata;
      f.guest_frame = step + 1;
      f.jitter_enabled = c.jitter;
      if (c.jitter)
        f.jitter_pixels = step ? std::array<float, 2>{-.375f, .125f}
                               : std::array<float, 2>{.25f, -.25f};
      f.captured_mask = 3;
      f.camera_samples = 1;
      f.camera_viewport_valid = true;
      f.scene_width = w;
      f.scene_height = h;
      f.scale_x = f.scale_y = 1;
      f.output_width = w;
      f.output_height = h;
      f.guest_viewport_xy = {w * .5f, h * -.5f, w * .5f + .5f, h * .5f + .5f};
      f.guest_viewport_z = {-1, 1};
      f.world_to_clip_candidate = {
          1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, step ? c.camera : 0, 0, 0, 1};
      if (c.nonfused) {
        // 4097 * (1 + 2^-12) rounds to 4098 before subtracting 4097.5.
        // FMA instead produces .500244140625, rejecting the .5 depth plane.
        f.world_to_clip_candidate[10] = 1.000244140625f;
        f.world_to_clip_candidate[14] = -4097.5f;
      }
      auto &depth = frame->textures[size_t(SsxInput::kResolvedDepth)];
      bool created;
      Require(depth.Prepare(device, DXGI_FORMAT_R32_FLOAT, w, h, created),
              "Depth");
      depth.view_format = DXGI_FORMAT_R32_FLOAT;
      auto desc = depth.resource->GetDesc();
      D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
      UINT64 bytes;
      device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr,
                                    nullptr, &bytes);
      auto upload = Buffer(device, bytes, true);
      void *data;
      D3D12_RANGE empty{0, 0};
      Check(upload->Map(0, &empty, &data), "Depth map");
      for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
          const float z = c.occluded && x < w / 2 ? .75f : .5f;
          std::memcpy(static_cast<uint8_t *>(data) +
                          y * footprint.Footprint.RowPitch + x * 4,
                      &z, 4);
        }
      upload->Unmap(0, nullptr);
      D3D12_TEXTURE_COPY_LOCATION from{}, to{};
      from.pResource = upload.Get();
      from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
      from.PlacedFootprint = footprint;
      to.pResource = depth.resource.Get();
      to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
      Transition(list.Get(), depth.resource.Get(),
                 D3D12_RESOURCE_STATE_COPY_DEST, GuestInputTexture::kReadState);
      auto &geo = frame->geometry;
      Require(geo.Prepare(device), "Geometry");
      geo.count = c.duplicate ? 2 : 1;
      auto &draw = geo.draws[0];
      draw.shader = 0x9897AE6BB3FD8C0Bull;
      draw.index_address = 0x10000;
      draw.vertex_address =
          0x20000 + step * 0x1000; // Vertex allocations may rotate.
      draw.vertex_count = 4;
      draw.index_count = 6;
      draw.vertex_endian = 2;
      draw.index_endian = 1;
      draw.index_offset = 128;
      draw.world_to_clip = f.world_to_clip_candidate;
      draw.viewport = f.guest_viewport_xy;
      geo.draws[1] = draw;
      geo.bytes = 140;
      auto geometry_upload = Buffer(device, geo.bytes, true);
      Check(geometry_upload->Map(0, &empty, &data), "Geometry map");
      std::memset(data, 0, geo.bytes);
      for (uint32_t i = 0; i < 4; ++i) {
        const float y = i < 2 ? -.5f : .5f;
        const float x = (i == 0 || i == 3 ? -.5f : .5f) +
                        (step ? c.shift + c.shear * (y + .5f) : 0);
        const uint32_t p[] = {
            std::byteswap(std::bit_cast<uint32_t>(x)),
            std::byteswap(std::bit_cast<uint32_t>(y)),
            std::byteswap(std::bit_cast<uint32_t>(c.nonfused ? 4097.f : .5f))};
        std::memcpy(static_cast<uint8_t *>(data) + i * 32, p, 12);
        const uint32_t uv[] = {i, i + 4};
        std::memcpy(static_cast<uint8_t *>(data) + i * 32 + 24, uv, 8);
      }
      if (step && c.uv_mismatch)
        static_cast<uint8_t *>(data)[24] ^= 1;
      uint16_t indices[] = {0, 1, 2, 0, 2, 3};
      if (step && c.index_mismatch)
        indices[0] = indices[3] = 1;
      if (step && c.out_of_bounds)
        indices[0] = indices[3] = 9;
      for (auto &v : indices)
        v = std::byteswap(v);
      std::memcpy(static_cast<uint8_t *>(data) + 128, indices, 12);
      geometry_upload->Unmap(0, nullptr);
      Transition(list.Get(), geo.buffer.Get(), GuestInputTexture::kReadState,
                 D3D12_RESOURCE_STATE_COPY_DEST);
      list->CopyBufferRegion(geo.buffer.Get(), 0, geometry_upload.Get(), 0,
                             geo.bytes);
      Transition(list.Get(), geo.buffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                 GuestInputTexture::kReadState);
      if (step && c.reset)
        camera.Reset();
      ++submission;
      const auto motion =
          camera.Record(*provider, list.Get(), frame, submission,
                        fence->GetCompletedValue(), f.guest_frame / 120.0);
      Require(motion.dispatched, "Camera pass");
      if (capture_test && step) {
        Require(native.RequestCapture(capture_dir), "Capture request");
        Require(!native.RequestCapture(capture_dir),
                "Duplicate capture request accepted");
      }
      const auto result = native.Record(*provider, list.Get(), frame, motion,
                                        submission, fence->GetCompletedValue());
      if (step && !c.duplicate && !c.reset)
        Require(result.drawn && result.matched == 1, "Native draw missing");
      const auto repeated_camera =
          camera.Record(*provider, list.Get(), frame, submission,
                        fence->GetCompletedValue(), f.guest_frame / 120.0);
      const auto repeat =
          native.Record(*provider, list.Get(), frame, repeated_camera,
                        submission, fence->GetCompletedValue());
      Require(!repeat.drawn, "Repeated paint redrew geometry");
      if (capture_test && step)
        Require(!std::filesystem::exists(capture_dir),
                "Capture written before fence completion");
      std::array<ComPtr<ID3D12Resource>, 2> readback;
      std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT, 2> fp;
      std::array<UINT64, 2> rb_bytes;
      ID3D12Resource *resources[] = {motion.vectors, motion.validity};
      for (uint32_t i = 0; i < 2; ++i) {
        desc = resources[i]->GetDesc();
        device->GetCopyableFootprints(&desc, 0, 1, 0, &fp[i], nullptr, nullptr,
                                      &rb_bytes[i]);
        readback[i] = Buffer(device, rb_bytes[i], false);
        from = {};
        to = {};
        from.pResource = resources[i];
        from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        to.pResource = readback[i].Get();
        to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        to.PlacedFootprint = fp[i];
        Transition(list.Get(), resources[i], GuestInputTexture::kReadState,
                   D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
        Transition(list.Get(), resources[i], D3D12_RESOURCE_STATE_COPY_SOURCE,
                   GuestInputTexture::kReadState);
      }
      Finish(list.Get());
      if (capture_test && step) {
        // Completion can arrive on a repeated presentation or invalid scene.
        native.Record(*provider, list.Get(), nullptr, {}, submission,
                      fence->GetCompletedValue());
        Require(std::filesystem::is_regular_file(capture_dir / "metadata.json"),
                "Capture completion marker");
        Require(!native.RequestCapture(capture_dir),
                "Existing capture would be overwritten");
        for (const auto *name :
             {"current-geometry.bin", "previous-geometry.bin"})
          Require(std::filesystem::file_size(capture_dir / name) == geo.bytes,
                  "Geometry capture size");
      }
      void *mapped[2];
      for (uint32_t i = 0; i < 2; ++i) {
        D3D12_RANGE range{0, size_t(rb_bytes[i])};
        Check(readback[i]->Map(0, &range, &mapped[i]), "Readback");
        if (capture_test && step) {
          std::ifstream file(capture_dir / (i ? "validity.bin" : "motion.bin"),
                             std::ios::binary);
          const size_t row_bytes = size_t(w) * (i ? 1 : 8);
          std::vector<char> row(row_bytes);
          for (uint32_t y = 0; y < h; ++y) {
            file.read(row.data(), row.size());
            Require(bool(file) &&
                        !std::memcmp(row.data(),
                                     static_cast<uint8_t *>(mapped[i]) +
                                         size_t(y) * fp[i].Footprint.RowPitch,
                                     row_bytes),
                    "Capture content mismatch");
          }
          Require(file.peek() == std::char_traits<char>::eof(),
                  "Capture contains row padding");
        }
      }
      for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
          const uint8_t valid = *(static_cast<uint8_t *>(mapped[1]) +
                                  y * fp[1].Footprint.RowPitch + x);
          const auto *value = reinterpret_cast<const float *>(
              static_cast<uint8_t *>(mapped[0]) + y * fp[0].Footprint.RowPitch +
              x * 8);
          const double world_y =
              (double(y) - f.jitter_pixels[1] - h * .5) / (h * -.5);
          const double deformation =
              step ? c.shift + c.shear * (world_y + .5) : 0;
          const double world_x =
              (double(x) - f.jitter_pixels[0] - w * .5) / (w * .5) -
              (step ? c.camera : 0) - deformation;
          const bool interior = std::abs(world_x) < .5 - 4.0 / w &&
                                std::abs(world_y) < .5 - 4.0 / h;
          const bool blocked = c.occluded && x < w / 2;
          if (step && c.ExpectedNative() && interior && !blocked && valid != 2)
            ++failures;
          if (valid == 2 && (!step || !c.ExpectedNative() || blocked))
            ++failures;
          if (valid == 2)
            ++native_pixels;
          const double expected_x =
              !step || c.reset
                  ? 0
                  : -double(w) * .5 *
                        (c.camera + (valid == 2 ? deformation : 0));
          const double error = std::max(std::abs(value[0] - expected_x),
                                        std::abs(double(value[1])));
          max_error = std::max(max_error, error);
          if (!std::isfinite(error) || error > .003)
            ++failures;
        }
      for (auto &r : readback)
        r->Unmap(0, &empty);
    }
    std::printf("%s %s size=%ux%u native_pixels=%llu max_error_pixels=%.6f "
                "failures=%llu\n",
                failures ? "FAIL" : "PASS", c.name, w, h, native_pixels,
                max_error, failures);
    Require(!failures, "Analytic native motion comparison");
  }
  // Time the same captured draw, alternating order to limit clock/thermal bias.
  // Uploads, CPU work, readbacks and the camera pass are outside the
  // timestamps.
  void Benchmark(const std::array<std::shared_ptr<SsxFrameInputs>, 2> &frames,
                 const SsxCameraMotion::Result &motion,
                 const std::filesystem::path &output) {
    auto *device = provider->GetDevice();
    ComPtr<ID3D12QueryHeap> queries;
    D3D12_QUERY_HEAP_DESC q{};
    q.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    q.Count = 2;
    Check(device->CreateQueryHeap(&q, IID_PPV_ARGS(&queries)),
          "Timestamp heap");
    auto readback = Buffer(device, 16, false);
    UINT64 frequency = 0;
    Check(provider->GetDirectQueue()->GetTimestampFrequency(&frequency),
          "Timestamp frequency");
    Require(frequency != 0, "Timestamp frequency zero");
    std::ofstream csv(output / "native-benchmark.csv");
    Require(bool(csv), "Benchmark CSV");
    csv << "round,mode,sample,native_ms\n";
    SsxNativeMotion native;
    for (uint32_t round = 0; round < 8; ++round) {
      for (uint32_t order = 0; order < 2; ++order) {
        const char *mode = ((round + order) & 1) ? "off" : "every_frame";
        rex::cvar::SetFlagByName("d3d12_ssx_native_statistics", mode);
        for (uint32_t sample = 0; sample < 40; ++sample) {
          ComPtr<ID3D12CommandAllocator> allocator;
          ComPtr<ID3D12GraphicsCommandList> list;
          Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                               IID_PPV_ARGS(&allocator)),
                "Benchmark allocator");
          Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                          allocator.Get(), nullptr,
                                          IID_PPV_ARGS(&list)),
                "Benchmark list");
          ++submission;
          native.Reset();
          auto seed = motion;
          seed.plan.constants.reset = true;
          seed.plan.repeated = false;
          native.Record(*provider, list.Get(), frames[0], seed, submission,
                        fence->GetCompletedValue());
          list->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
          const auto result =
              native.Record(*provider, list.Get(), frames[1], motion,
                            submission, fence->GetCompletedValue());
          Require(result.drawn, "Benchmark native draw missing");
          list->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
          list->ResolveQueryData(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0,
                                 2, readback.Get(), 0);
          Finish(list.Get());
          void *mapped = nullptr;
          D3D12_RANGE range{0, 16}, empty{0, 0};
          Check(readback->Map(0, &range, &mapped), "Timestamp map");
          const auto *ticks = static_cast<const uint64_t *>(mapped);
          Require(ticks[1] >= ticks[0], "Timestamp order");
          const double ms = 1000.0 * double(ticks[1] - ticks[0]) / frequency;
          readback->Unmap(0, &empty);
          // Discard initial warmup after each mode switch.
          if (sample >= 8)
            csv << round << ',' << mode << ',' << sample << ',' << ms << '\n';
        }
      }
    }
    csv.close();
    Require(bool(csv), "Benchmark CSV write");
    std::puts("PASS timestamp benchmark: 256 samples per mode, identical "
              "captured geometry/depth");
  }
  void Replay(const std::filesystem::path &input,
              const std::filesystem::path &output, bool benchmark = false) {
    std::ifstream metadata(input / "replay-metadata.bin", std::ios::binary);
    const auto magic = Read<std::array<char, 8>>(metadata);
    const bool jitter_metadata = !std::memcmp(magic.data(), "SSXNMV2\0", 8);
    Require(jitter_metadata || !std::memcmp(magic.data(), "SSXNMV1\0", 8),
            "Replay metadata version");
    const auto width = Read<uint32_t>(metadata),
               height = Read<uint32_t>(metadata);
    Require(width && height && width <= 4096 && height <= 4096,
            "Replay dimensions");
    SsxCameraMotion camera;
    SsxNativeMotion native;
    std::array<std::shared_ptr<SsxFrameInputs>, 2> frames;
    for (uint32_t step = 0; step < 2; ++step) {
      auto frame = std::make_shared<SsxFrameInputs>();
      frames[step] = frame;
      auto &f = frame->metadata;
      f.guest_frame = Read<uint64_t>(metadata);
      f.scene_width = Read<uint32_t>(metadata);
      f.scene_height = Read<uint32_t>(metadata);
      f.scale_x = Read<uint32_t>(metadata);
      f.scale_y = Read<uint32_t>(metadata);
      f.world_to_clip_candidate = Read<std::array<float, 16>>(metadata);
      f.guest_viewport_xy = Read<std::array<float, 4>>(metadata);
      if (jitter_metadata) {
        f.jitter_enabled = Read<uint32_t>(metadata) != 0;
        f.jitter_pixels = Read<std::array<float, 2>>(metadata);
      }
      f.guest_viewport_z = {-1, 1};
      f.camera_viewport_valid = true;
      f.camera_samples = 1;
      f.captured_mask = 3;
      f.output_width = width;
      f.output_height = height;
      auto &geometry = frame->geometry;
      geometry.bytes = Read<uint32_t>(metadata);
      geometry.count = Read<uint32_t>(metadata);
      Require(geometry.bytes && geometry.bytes <= SsxGeometry::kCapacity &&
                  geometry.count <= SsxGeometry::kMaxDraws,
              "Replay geometry bounds");
      for (uint32_t i = 0; i < geometry.count; ++i) {
        auto &d = geometry.draws[i];
        d.shader = Read<uint64_t>(metadata);
        d.vertex_address = Read<uint32_t>(metadata);
        d.index_address = Read<uint32_t>(metadata);
        d.vertex_offset = Read<uint32_t>(metadata);
        d.index_offset = Read<uint32_t>(metadata);
        d.vertex_count = Read<uint32_t>(metadata);
        d.index_count = Read<uint32_t>(metadata);
        d.vertex_endian = Read<uint32_t>(metadata);
        d.index_endian = Read<uint32_t>(metadata);
        d.world_to_clip = Read<std::array<float, 16>>(metadata);
        d.viewport = Read<std::array<float, 4>>(metadata);
        Require(uint64_t(d.vertex_offset) + uint64_t(d.vertex_count) * 32 <=
                        geometry.bytes &&
                    uint64_t(d.index_offset) + uint64_t(d.index_count) * 2 <=
                        geometry.bytes,
                "Replay draw bounds");
      }
      auto *device = provider->GetDevice();
      ComPtr<ID3D12CommandAllocator> allocator;
      ComPtr<ID3D12GraphicsCommandList> list;
      Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                           IID_PPV_ARGS(&allocator)),
            "Allocator");
      Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                      allocator.Get(), nullptr,
                                      IID_PPV_ARGS(&list)),
            "List");
      std::vector<ComPtr<ID3D12Resource>> uploads;
      for (size_t i = 0; i < 2; ++i) {
        const auto path = input / (i ? "color.bin" : "depth.bin");
        if (!std::filesystem::exists(path))
          continue;
        auto &texture = frame->textures[size_t(i ? SsxInput::kEncodedColor
                                                 : SsxInput::kResolvedDepth)];
        bool created;
        const auto format =
            i ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R32_FLOAT;
        Require(texture.Prepare(device, format, width, height, created),
                "Replay texture");
        texture.view_format = format;
        const auto desc = texture.resource->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
        UINT64 bytes;
        device->GetCopyableFootprints(&desc, 0, 1, 0, &fp, nullptr, nullptr,
                                      &bytes);
        auto upload = Buffer(device, bytes, true);
        void *data;
        D3D12_RANGE empty{0, 0};
        Check(upload->Map(0, &empty, &data), "Texture upload map");
        const size_t row_bytes = size_t(width) * (i ? 8 : 4);
        const auto source = ReadFile(path, row_bytes * height);
        for (uint32_t y = 0; y < height; ++y)
          std::memcpy(static_cast<char *>(data) +
                          size_t(y) * fp.Footprint.RowPitch,
                      source.data() + size_t(y) * row_bytes, row_bytes);
        upload->Unmap(0, nullptr);
        D3D12_TEXTURE_COPY_LOCATION from{}, to{};
        from.pResource = upload.Get();
        from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        from.PlacedFootprint = fp;
        to.pResource = texture.resource.Get();
        to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
        Transition(list.Get(), texture.resource.Get(),
                   D3D12_RESOURCE_STATE_COPY_DEST,
                   GuestInputTexture::kReadState);
        uploads.push_back(upload);
      }
      Require(geometry.Prepare(device), "Replay geometry");
      auto upload = Buffer(device, geometry.bytes, true);
      void *data;
      D3D12_RANGE empty{0, 0};
      Check(upload->Map(0, &empty, &data), "Geometry upload map");
      const auto source = ReadFile(
          input / (step ? "current-geometry.bin" : "previous-geometry.bin"),
          geometry.bytes);
      std::memcpy(data, source.data(), source.size());
      upload->Unmap(0, nullptr);
      Transition(list.Get(), geometry.buffer.Get(),
                 GuestInputTexture::kReadState, D3D12_RESOURCE_STATE_COPY_DEST);
      list->CopyBufferRegion(geometry.buffer.Get(), 0, upload.Get(), 0,
                             geometry.bytes);
      Transition(list.Get(), geometry.buffer.Get(),
                 D3D12_RESOURCE_STATE_COPY_DEST, GuestInputTexture::kReadState);
      ++submission;
      const auto motion =
          camera.Record(*provider, list.Get(), frame, submission,
                        fence->GetCompletedValue(), step / 120.0 + 1);
      Require(motion.dispatched, "Replay camera dispatch");
      if (step)
        Require(native.RequestCapture(output),
                "Replay output directory must be new");
      const auto result = native.Record(*provider, list.Get(), frame, motion,
                                        submission, fence->GetCompletedValue());
      if (step)
        Require(result.drawn, "Replay native draw missing");
      Finish(list.Get());
      if (step) {
        native.Record(*provider, list.Get(), nullptr, {}, submission,
                      fence->GetCompletedValue());
        Require(std::filesystem::is_regular_file(output / "metadata.json"),
                "Replay output incomplete");
        std::printf(
            "PASS replay frame=%llu previous geometry retained, draws=%u\n",
            f.guest_frame, result.matched);
        if (benchmark)
          Benchmark(frames, motion, output);
      }
    }
  }
};
} // namespace
int wmain(int argc, wchar_t **argv) {
  rex::InitLogging("native-motion-probe.log");
  int status = 0;
  try {
    const bool benchmark = argc == 4 && std::wstring(argv[1]) == L"--benchmark";
    rex::cvar::SetFlagByName("d3d12_debug", benchmark ? "false" : "true");
    rex::cvar::SetFlagByName("d3d12_streamline", "false");
    Harness harness;
    ComPtr<ID3D12InfoQueue> messages;
    if (!benchmark) {
      Check(harness.provider->GetDevice()->QueryInterface(
                IID_PPV_ARGS(&messages)),
            "Debug layer");
      messages->ClearStoredMessages();
    }
    if ((argc == 4 || argc == 5) && std::wstring(argv[1]) == L"--replay") {
      if (argc == 5) {
        const std::wstring mode(argv[4]);
        Require(mode == L"off" || mode == L"sampled" || mode == L"every_frame",
                "Statistics mode");
        rex::cvar::SetFlagByName("d3d12_ssx_native_statistics",
                                 std::string(mode.begin(), mode.end()));
      }
      harness.Replay(std::filesystem::path(argv[2]),
                     std::filesystem::path(argv[3]));
    } else if (benchmark) {
      harness.Replay(std::filesystem::path(argv[2]),
                     std::filesystem::path(argv[3]), true);
    } else {
      Require(argc == 1,
              "Usage: ssx_native_motion_probe [--replay input-directory "
              "new-output-directory [off|sampled|every_frame]] or "
              "--benchmark input-directory new-output-directory");
      for (const char *mode : {"off", "sampled", "every_frame"}) {
        rex::cvar::SetFlagByName("d3d12_ssx_native_statistics", mode);
        std::printf("Statistics mode: %s\n", mode);
        for (uint32_t width : {64u, 3360u, 3840u}) {
          const uint32_t height = width == 64     ? 64
                                  : width == 3360 ? 1752
                                                  : 2160;
          harness.Run({.name = "stationary", .width = width, .height = height});
          harness.Run({.name = "rigid-plus-camera",
                       .width = width,
                       .height = height,
                       .shift = .125f,
                       .camera = .125f});
          harness.Run({.name = "deformation",
                       .width = width,
                       .height = height,
                       .shear = .25f});
        }
        harness.Run({.name = "occluded", .shear = .25f, .occluded = true});
        harness.Run({.name = "changed-UV", .shear = .25f, .uv_mismatch = true});
        harness.Run({.name = "changed-topology",
                     .shear = .25f,
                     .index_mismatch = true});
        harness.Run({.name = "out-of-range-index", .out_of_bounds = true});
        harness.Run({.name = "ambiguous-mesh", .duplicate = true});
        harness.Run({.name = "history-reset", .shift = .125f, .reset = true});
        harness.Run({.name = "guest-nonfused-rounding", .nonfused = true});
        harness.Run({.name = "jitter-stationary",
                     .width = 3360,
                     .height = 1752,
                     .jitter = true});
        harness.Run({.name = "jitter-deformation",
                     .width = 3360,
                     .height = 1752,
                     .shear = .25f,
                     .jitter = true});
        harness.Run({.name = "jitter-rigid-camera",
                     .width = 3840,
                     .height = 2160,
                     .shift = .125f,
                     .camera = .125f,
                     .jitter = true});
      }
    }
    unsigned errors = 0;
    for (UINT64 i = 0; messages && i < messages->GetNumStoredMessages(); ++i) {
      SIZE_T bytes = 0;
      messages->GetMessage(i, nullptr, &bytes);
      std::vector<uint8_t> storage(bytes);
      auto *message = reinterpret_cast<D3D12_MESSAGE *>(storage.data());
      Check(messages->GetMessage(i, message, &bytes), "Debug message");
      if (message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) {
        ++errors;
        std::printf("D3D12 ERROR: %s\n", message->pDescription);
      }
    }
    if (messages)
      std::printf("D3D12 validation errors: %u\n", errors);
    Require(!errors, "D3D12 validation");
  } catch (const std::exception &e) {
    std::printf("FAIL: %s\n", e.what());
    status = 1;
  }
  rex::ShutdownLogging();
  return status;
}
