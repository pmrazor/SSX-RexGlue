// Exercises the production compute pass on D3D12. Expected motion is derived
// analytically from synthetic geometry, independently of its matrix inversion.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/d3d12/d3d12_provider.h>
#include <rex/ui/d3d12/ssx_camera_motion.h>
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
void Transition(ID3D12GraphicsCommandList *list, ID3D12Resource *resource,
                D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
  D3D12_RESOURCE_BARRIER b{};
  b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  b.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before,
                  after};
  list->ResourceBarrier(1, &b);
}
ComPtr<ID3D12Resource> Buffer(ID3D12Device *device, uint64_t bytes,
                              bool upload) {
  D3D12_RESOURCE_DESC d{};
  d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  d.Width = bytes;
  d.Height = 1;
  d.DepthOrArraySize = d.MipLevels = d.SampleDesc.Count = 1;
  d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  D3D12_HEAP_PROPERTIES h{};
  h.Type = upload ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_READBACK;
  ComPtr<ID3D12Resource> result;
  Check(device->CreateCommittedResource(&h, D3D12_HEAP_FLAG_NONE, &d,
                                        upload
                                            ? D3D12_RESOURCE_STATE_GENERIC_READ
                                            : D3D12_RESOURCE_STATE_COPY_DEST,
                                        nullptr, IID_PPV_ARGS(&result)),
        "Create buffer");
  return result;
}
SsxFrameMetadata Frame(uint32_t width = 65, uint32_t height = 33,
                       uint32_t scale = 1) {
  SsxFrameMetadata f;
  f.guest_frame = 1;
  f.captured_mask = 3;
  f.camera_samples = 1;
  f.camera_viewport_valid = true;
  f.scene_width = width;
  f.scene_height = height;
  f.scale_x = f.scale_y = scale;
  f.output_width = 3840;
  f.output_height = 2160;
  f.guest_viewport_xy = {width * .5f, height * -.5f, width * .5f + .5f,
                         height * .5f + .5f};
  f.guest_viewport_z = {-1, 1};
  f.world_to_clip_candidate = {1, 0, 0,          0, 0, 2, 0,           0,
                               0, 0, 1.0000625f, 1, 0, 0, -.50003125f, 0};
  return f;
}
using Expected =
    std::function<std::array<double, 2>(uint32_t, uint32_t, double)>;
const Expected zero = [](uint32_t, uint32_t, double) {
  return std::array<double, 2>{0, 0};
};

struct Harness {
  std::unique_ptr<D3D12Provider> provider;
  ComPtr<ID3D12Fence> fence;
  uint64_t submission = 0;
  Harness() {
    provider = D3D12Provider::Create();
    Require(bool(provider), "Create provider");
    Check(provider->GetDevice()->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                             IID_PPV_ARGS(&fence)),
          "Create fence");
  }
  void Finish(ID3D12GraphicsCommandList *list) {
    Check(list->Close(), "Close list");
    ID3D12CommandList *lists[] = {list};
    provider->GetDirectQueue()->ExecuteCommandLists(1, lists);
    Check(provider->GetDirectQueue()->Signal(fence.Get(), submission),
          "Signal");
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    Require(event != nullptr, "Create event");
    const HRESULT hr = fence->SetEventOnCompletion(submission, event);
    const DWORD waited =
        SUCCEEDED(hr) ? WaitForSingleObject(event, 15000) : WAIT_FAILED;
    CloseHandle(event);
    Require(waited == WAIT_OBJECT_0, "GPU timeout");
  }
  void Run(SsxCameraMotion &motion, SsxFrameMetadata f, SsxMotionReset reason,
           const Expected &expected, const char *label,
           bool invalid_depth = false) {
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
    frame->metadata = f;
    auto &depth = frame->textures[size_t(SsxInput::kResolvedDepth)];
    bool created;
    const uint32_t w = f.scene_width * f.scale_x,
                   h = f.scene_height * f.scale_y;
    Require(depth.Prepare(device, DXGI_FORMAT_R32_FLOAT, w, h, created),
            "Depth allocation");
    depth.view_format = DXGI_FORMAT_R32_FLOAT;
    auto desc = depth.resource->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 bytes;
    device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr,
                                  &bytes);
    auto upload = Buffer(device, bytes, true);
    void *data;
    D3D12_RANGE empty{0, 0};
    Check(upload->Map(0, &empty, &data), "Upload map");
    for (uint32_t y = 0; y < h; ++y)
      for (uint32_t x = 0; x < w; ++x) {
        const float z = 5 + x * .01f + y * .02f;
        float d = 1 - (1.0000625f - .50003125f / z);
        if (invalid_depth && x % 4 == 0)
          d = 0;
        if (invalid_depth && x % 4 == 1)
          d = std::numeric_limits<float>::quiet_NaN();
        if (invalid_depth && x % 4 == 2)
          d = 2;
        std::memcpy(static_cast<uint8_t *>(data) +
                        y * footprint.Footprint.RowPitch + x * 4,
                    &d, 4);
      }
    upload->Unmap(0, &empty);
    D3D12_TEXTURE_COPY_LOCATION from{}, to{};
    from.pResource = upload.Get();
    from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    from.PlacedFootprint = footprint;
    to.pResource = depth.resource.Get();
    to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    Transition(list.Get(), depth.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
               GuestInputTexture::kReadState);
    ++submission;
    auto result =
        motion.Record(*provider, list.Get(), frame, submission,
                      fence->GetCompletedValue(), f.guest_frame / 120.0);
    Require(result.dispatched, "Motion not dispatched");
    Require(result.plan.reason == reason, "Wrong history reset reason");
    auto repeated =
        motion.Record(*provider, list.Get(), frame, submission,
                      fence->GetCompletedValue(), f.guest_frame / 120.0);
    Require(repeated.plan.repeated && !repeated.dispatched &&
                repeated.vectors == result.vectors,
            "Repeated paint changed vectors");
    std::array<ComPtr<ID3D12Resource>, 2> readback;
    std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT, 2> footprints;
    std::array<UINT64, 2> sizes;
    ID3D12Resource *resources[] = {result.vectors, result.validity};
    for (size_t i = 0; i < 2; ++i) {
      desc = resources[i]->GetDesc();
      device->GetCopyableFootprints(&desc, 0, 1, 0, &footprints[i], nullptr,
                                    nullptr, &sizes[i]);
      readback[i] = Buffer(device, sizes[i], false);
      from = {};
      from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      from.pResource = resources[i];
      to = {};
      to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
      to.pResource = readback[i].Get();
      to.PlacedFootprint = footprints[i];
      Transition(list.Get(), resources[i], GuestInputTexture::kReadState,
                 D3D12_RESOURCE_STATE_COPY_SOURCE);
      list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
      Transition(list.Get(), resources[i], D3D12_RESOURCE_STATE_COPY_SOURCE,
                 GuestInputTexture::kReadState);
    }
    Finish(list.Get());
    void *mapped[2];
    for (size_t i = 0; i < 2; ++i) {
      D3D12_RANGE range{0, size_t(sizes[i])};
      Check(readback[i]->Map(0, &range, &mapped[i]), "Readback map");
    }
    double max_error = 0;
    uint64_t failures = 0;
    for (uint32_t y = 0; y < h; ++y)
      for (uint32_t x = 0; x < w; ++x) {
        const auto *value = reinterpret_cast<float *>(
            static_cast<uint8_t *>(mapped[0]) +
            y * footprints[0].Footprint.RowPitch + x * 8);
        const uint8_t valid = *(static_cast<uint8_t *>(mapped[1]) +
                                y * footprints[1].Footprint.RowPitch + x);
        const bool expected_valid =
            reason == SsxMotionReset::kNone && (!invalid_depth || x % 4 == 3);
        const auto e = expected_valid ? expected(x, y, 5 + x * .01f + y * .02f)
                                      : std::array<double, 2>{0, 0};
        for (size_t i = 0; i < 2; ++i) {
          const double error = std::abs(value[i] - e[i]);
          max_error = std::max(max_error, error);
          if (!std::isfinite(value[i]) || error > .003)
            ++failures;
        }
        if (valid != expected_valid)
          ++failures;
      }
    for (auto &rb : readback)
      rb->Unmap(0, &empty);
    std::printf("%s %s size=%ux%u pixels=%llu max_error_pixels=%.6f reset=%s\n",
                failures ? "FAIL" : "PASS", label, w, h, uint64_t(w) * h,
                max_error, SsxMotionResetName(reason));
    Require(!failures, "Analytic GPU comparison failed");
  }
};
} // namespace
int main() {
  rex::InitLogging("motion-probe.log");
  int status = 0;
  try {
    rex::cvar::SetFlagByName("d3d12_debug", "true");
    rex::cvar::SetFlagByName("d3d12_streamline", "false");
    Harness h;
    ComPtr<ID3D12InfoQueue> messages;
    Check(h.provider->GetDevice()->QueryInterface(IID_PPV_ARGS(&messages)),
          "D3D12 debug unavailable");
    messages->ClearStoredMessages();
    {
      SsxCameraMotion m;
      auto f = Frame();
      h.Run(m, f, SsxMotionReset::kFirstFrame, zero, "first-reset");
      ++f.guest_frame;
      h.Run(m, f, SsxMotionReset::kNone, zero, "stationary");
      ++f.guest_frame;
      h.Run(m, f, SsxMotionReset::kNone, zero, "invalid-depth", true);
      m.Reset();
      ++f.guest_frame;
      h.Run(m, f, SsxMotionReset::kExplicit, zero, "explicit-reset");
    }
    for (auto size :
         {std::array<uint32_t, 3>{65, 33, 1}, {1120, 584, 3}, {1280, 720, 3}}) {
      SsxCameraMotion m;
      auto f = Frame(size[0], size[1], size[2]);
      h.Run(m, f, SsxMotionReset::kFirstFrame, zero, "translation-start");
      ++f.guest_frame;
      f.world_to_clip_candidate[12] = -.25f;
      f.world_to_clip_candidate[13] = -.4f;
      h.Run(
          m, f, SsxMotionReset::kNone,
          [=](uint32_t, uint32_t, double z) {
            return std::array<double, 2>{.25 * size[0] * size[2] * .5 / z,
                                         -.4 * size[1] * size[2] * .5 / z};
          },
          "translation-xy");
    }
    {
      SsxCameraMotion m;
      auto f = Frame();
      h.Run(m, f, SsxMotionReset::kFirstFrame, zero, "rotation-start");
      ++f.guest_frame;
      const float c = std::cos(.1f), s = std::sin(.1f);
      f.world_to_clip_candidate[0] = c;
      f.world_to_clip_candidate[1] = 2 * s;
      f.world_to_clip_candidate[4] = -s;
      f.world_to_clip_candidate[5] = 2 * c;
      h.Run(
          m, f, SsxMotionReset::kNone,
          [=](uint32_t x, uint32_t y, double) {
            const double nx = (x + .5 - 33) / 32.5, ny = (y + .5 - 17) / -16.5;
            return std::array<double, 2>{(c * nx + s * ny / 2 - nx) * 32.5,
                                         (-2 * s * nx + c * ny - ny) * -16.5};
          },
          "camera-roll");
    }
    {
      SsxCameraMotion m;
      auto f = Frame();
      h.Run(m, f, SsxMotionReset::kFirstFrame, zero, "forward-start");
      ++f.guest_frame;
      f.world_to_clip_candidate[14] -= .2f * f.world_to_clip_candidate[10];
      f.world_to_clip_candidate[15] = -.2f;
      h.Run(
          m, f, SsxMotionReset::kNone,
          [](uint32_t x, uint32_t y, double z) {
            return std::array<double, 2>{(x + .5 - 33) * (z / (z + .2) - 1),
                                         (y + .5 - 17) * (z / (z + .2) - 1)};
          },
          "camera-forward");
      f.scale_x = f.scale_y = 3;
      ++f.guest_frame;
      h.Run(m, f, SsxMotionReset::kResize, zero, "resize-reset");
      f.guest_frame += 9;
      h.Run(m, f, SsxMotionReset::kFrameGap, zero, "gap-reset");
    }
    {
      SsxCameraMotion m;
      auto f = Frame(1120, 584, 3);
      f.jitter_enabled = true; f.jitter_pixels = {.25f, -.25f};
      h.Run(m, f, SsxMotionReset::kFirstFrame, zero, "jitter-reset");
      ++f.guest_frame; f.jitter_pixels = {-.375f, .125f};
      h.Run(m, f, SsxMotionReset::kNone, zero, "jitter-stationary");
      ++f.guest_frame; f.world_to_clip_candidate[12] = -.2f;
      const auto scale = double(f.guest_viewport_xy[0] * f.scale_x);
      h.Run(m, f, SsxMotionReset::kNone,
        [scale](uint32_t, uint32_t, double z) { return std::array<double,2>{.2 * scale / z, 0}; },
        "jitter-camera-pan");
    }
    unsigned errors = 0;
    for (UINT64 i = 0; i < messages->GetNumStoredMessages(); ++i) {
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
    std::printf("D3D12 validation errors: %u\n", errors);
    Require(!errors, "D3D12 validation failed");
  } catch (const std::exception &e) {
    std::printf("FAIL: %s\n", e.what());
    status = 1;
  }
  rex::ShutdownLogging();
  return status;
}
