// Real DLSS dispatch on an analytic scene, independent of SSX game assets.
// This checks the API/resource contract, not SSX image quality or frame
// generation.
#include <DirectXMath.h>
#include <DirectXPackedVector.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <rex/ui/d3d12/d3d12_provider.h>
#include <rex/ui/d3d12/ssx_reconstruction.h>
#include <rex/ui/d3d12/d3d12_util.h>
#include <stdexcept>
#include <vector>

using namespace rex::ui::d3d12;
using Microsoft::WRL::ComPtr;
namespace {
void Require(bool value, const char *label) {
  if (!value)
    throw std::runtime_error(label);
}
void Check(HRESULT hr, const char *label) { Require(SUCCEEDED(hr), label); }
void Barrier(ID3D12GraphicsCommandList *list, ID3D12Resource *texture,
             D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
  D3D12_RESOURCE_BARRIER b{};
  b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  b.Transition = {texture, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, from, to};
  list->ResourceBarrier(1, &b);
}
struct Texture {
  ComPtr<ID3D12Resource> gpu, upload, readback;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
  uint32_t row_bytes = 0, height = 0;
};
struct Harness {
  D3D12Provider &provider;
  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> list;
  ComPtr<ID3D12Fence> fence;
  Texture color, depth, motion, output;
  uint64_t submission = 0;
  bool open = false;
  explicit Harness(D3D12Provider &p) : provider(p) {
    auto *d = provider.GetDevice();
    Check(d->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                    IID_PPV_ARGS(&allocator)),
          "SR allocator");
    Check(d->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                               allocator.Get(), nullptr, IID_PPV_ARGS(&list)),
          "SR list");
    Check(list->Close(), "SR initial close");
    Check(d->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)),
          "SR fence");
  }
  ~Harness() {
    // Keep even partially recorded SL work alive through submission and drain.
    try {
      Finish();
    } catch (...) {
      std::puts("FAIL SR cleanup GPU drain");
    }
  }
  void Begin() {
    Check(allocator->Reset(), "SR allocator reset");
    Check(list->Reset(allocator.Get(), nullptr), "SR list reset");
    open = true;
  }
  void Finish() {
    if (open) {
      Check(list->Close(), "SR close");
      ID3D12CommandList *commands[] = {list.Get()};
      provider.GetDirectQueue()->ExecuteCommandLists(1, commands);
      open = false;
    }
    Check(provider.GetDirectQueue()->Signal(fence.Get(), ++submission),
          "SR signal");
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    Require(event != nullptr, "SR event");
    const HRESULT hr = fence->SetEventOnCompletion(submission, event);
    DWORD wait =
        SUCCEEDED(hr) ? WaitForSingleObject(event, 30000) : WAIT_FAILED;
    CloseHandle(event);
    Require(wait == WAIT_OBJECT_0, "SR GPU timeout");
  }
  ComPtr<ID3D12Resource> Buffer(uint64_t size, D3D12_HEAP_TYPE type) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = type;
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width = size;
    d.Height = d.DepthOrArraySize = d.MipLevels = d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> resource;
    Check(provider.GetDevice()->CreateCommittedResource(
              &heap, D3D12_HEAP_FLAG_NONE, &d,
              type == D3D12_HEAP_TYPE_UPLOAD ? D3D12_RESOURCE_STATE_GENERIC_READ
                                             : D3D12_RESOURCE_STATE_COPY_DEST,
              nullptr, IID_PPV_ARGS(&resource)),
          "SR buffer");
    return resource;
  }
  Texture MakeTexture(uint32_t w, uint32_t h, DXGI_FORMAT format,
                      uint32_t pixel_bytes, bool uav = false) {
    Texture t;
    t.row_bytes = w * pixel_bytes;
    t.height = h;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = w;
    d.Height = h;
    d.DepthOrArraySize = d.MipLevels = d.SampleDesc.Count = 1;
    d.Format = format;
    d.Flags = uav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
                  : D3D12_RESOURCE_FLAG_NONE;
    Check(provider.GetDevice()->CreateCommittedResource(
              &heap, D3D12_HEAP_FLAG_NONE, &d,
              uav ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS
                  : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
              nullptr, IID_PPV_ARGS(&t.gpu)),
          "SR texture");
    uint64_t bytes;
    provider.GetDevice()->GetCopyableFootprints(&d, 0, 1, 0, &t.footprint,
                                                nullptr, nullptr, &bytes);
    t.upload = Buffer(bytes, D3D12_HEAP_TYPE_UPLOAD);
    if (uav)
      t.readback = Buffer(bytes, D3D12_HEAP_TYPE_READBACK);
    return t;
  }
  void Upload(Texture &t, const void *source, bool uav = false) {
    void *mapped;
    D3D12_RANGE none{0, 0};
    Check(t.upload->Map(0, &none, &mapped), "SR upload map");
    for (uint32_t y = 0; y < t.height; ++y)
      std::memcpy(
          static_cast<uint8_t *>(mapped) + t.footprint.Footprint.RowPitch * y,
          static_cast<const uint8_t *>(source) + t.row_bytes * y, t.row_bytes);
    t.upload->Unmap(0, nullptr);
    const auto state = uav ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS
                           : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    Barrier(list.Get(), t.gpu.Get(), state, D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION from{}, to{};
    from.pResource = t.upload.Get();
    from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    from.PlacedFootprint = t.footprint;
    to.pResource = t.gpu.Get();
    to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    Barrier(list.Get(), t.gpu.Get(), D3D12_RESOURCE_STATE_COPY_DEST, state);
  }
  void ReadOutput() {
    Barrier(list.Get(), output.gpu.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION from{}, to{};
    from.pResource = output.gpu.Get();
    from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    to.pResource = output.readback.Get();
    to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    to.PlacedFootprint = output.footprint;
    list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    Barrier(list.Get(), output.gpu.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Finish();
  }
  void ValidateOutput(uint64_t frame, bool reset) {
    void *mapped;
    Check(output.readback->Map(0, nullptr, &mapped), "SR readback map");
    float maximum = 0;
    double sum = 0;
    uint64_t invalid = 0, unwritten = 0;
    float center[3]{};
    for (uint32_t y = 0; y < output.height; ++y) {
      const auto *row = reinterpret_cast<const uint16_t *>(
          static_cast<const uint8_t *>(mapped) +
          output.footprint.Footprint.RowPitch * y);
      for (uint32_t x = 0; x < output.row_bytes / 8; ++x) {
        for (unsigned c = 0; c < 3; ++c) {
          const float v =
              DirectX::PackedVector::XMConvertHalfToFloat(row[x * 4 + c]);
          invalid += !std::isfinite(v);
          unwritten += v == -100.0f;
          maximum = std::max(maximum, v);
          sum += v;
          if (x == output.row_bytes / 16 && y == output.height / 2)
            center[c] = v;
        }
      }
    }
    D3D12_RANGE none{0, 0};
    output.readback->Unmap(0, &none);
    const double mean =
        sum / (uint64_t(output.row_bytes / 8) * output.height * 3);
    std::printf("SR_GPU frame=%llu reset=%d output=%ux%u nonfinite=%llu "
                "unwritten=%llu max=%.5f mean=%.5f\n",
                frame, reset, output.row_bytes / 8, output.height, invalid,
                unwritten, maximum, mean);
    Require(!invalid && !unwritten && maximum > 2.0f && maximum < 100.0f &&
                mean > .02 && mean < 10,
            "SR output did not contain plausible finite HDR pixels");
    Require(std::abs(center[0] - 8.f) < .1f &&
                std::abs(center[1] - 2.f) < .1f &&
                std::abs(center[2] - .5f) < .1f,
            "SR changed the interior linear HDR color beyond tolerance");
  }
};

Streamline::SRCamera Camera(uint32_t width, uint32_t height) {
  using namespace DirectX;
  Streamline::SRCamera c;
  c.near_plane = .1f;
  c.far_plane = 1000.f;
  c.vertical_fov = 1.f;
  c.aspect = float(width) / height;
  c.up = {0, 1, 0};
  c.right = {1, 0, 0};
  c.forward = {0, 0, 1};
  const auto projection = XMMatrixPerspectiveFovLH(c.vertical_fov, c.aspect,
                                                   c.near_plane, c.far_plane);
  auto store = [](auto &dest, FXMMATRIX matrix) {
    XMFLOAT4X4 value;
    XMStoreFloat4x4(&value, matrix);
    std::memcpy(dest.data(), &value, sizeof(value));
  };
  store(c.view_to_clip, projection);
  store(c.clip_to_view, XMMatrixInverse(nullptr, projection));
  store(c.clip_to_previous, XMMatrixIdentity());
  store(c.previous_to_clip, XMMatrixIdentity());
  return c;
}
} // namespace

void CheckSREvaluation(D3D12Provider &provider) {
  auto *sr = provider.GetStreamline();
  Require(sr && sr->support().sr,
          "SR evaluation requires supported Streamline");
  Harness gpu(provider);
  uint64_t id = 100;
  unsigned evaluations = 0, rejections = 0;
  struct Scenario {
    Streamline::Quality quality;
    uint32_t width, height, frames;
  };
  const Scenario scenarios[] = {{Streamline::Quality::kQuality, 3840, 2160, 7},
                                {Streamline::Quality::kQuality, 3360, 1752, 2},
                                {Streamline::Quality::kDLAA, 3840, 2160, 2}};
  for (const auto &scenario : scenarios) {
    // Explicit GPU drain/release before mode/output-size changes.
    gpu.Finish();
    Require(sr->ReleaseSR(), "SR release before resize");
    Require(
        sr->ConfigureSsxSR(scenario.quality, scenario.width, scenario.height),
        "SR configure");
    Streamline::OptimalSettings optimal;
    Require(sr->GetOptimalSettings(scenario.quality, scenario.width,
                                   scenario.height, optimal),
            "SR size query");
    const uint32_t w = optimal.width, h = optimal.height;
    std::printf("SR_SCENARIO input=%ux%u output=%ux%u\n", w, h, scenario.width,
                scenario.height);
    gpu.color = gpu.MakeTexture(w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, 8);
    gpu.depth = gpu.MakeTexture(w, h, DXGI_FORMAT_R32_FLOAT, 4);
    gpu.motion = gpu.MakeTexture(w, h, DXGI_FORMAT_R32G32_FLOAT, 8);
    gpu.output = gpu.MakeTexture(scenario.width, scenario.height,
                                 DXGI_FORMAT_R16G16B16A16_FLOAT, 8, true);
    std::vector<uint16_t> color(size_t(w) * h * 4);
    std::vector<float> depth(size_t(w) * h), motion(size_t(w) * h * 2);
    std::vector<uint16_t> sentinel(
        size_t(scenario.width) * scenario.height * 4,
        DirectX::PackedVector::XMConvertFloatToHalf(-100.f));
    for (unsigned f = 0; f < scenario.frames; ++f, ++id) {
      gpu.Begin();
      Streamline::SRFrame frame;
      frame.id = id;
      frame.camera = Camera(w, h);
      const std::array<float, 2> jitter[] = {{0, -1.f / 6},
                                             {-.25f, 1.f / 6},
                                             {.25f, -7.f / 18},
                                             {-.375f, -1.f / 18}};
      frame.jitter_pixels = jitter[f % 4];
      frame.color = gpu.color.gpu.Get();
      frame.depth = gpu.depth.gpu.Get();
      frame.motion = gpu.motion.gpu.Get();
      frame.output = gpu.output.gpu.Get();
      frame.reset = f == 2; // Simulated camera cut.
      if (f == 3) {
        id += 2;
        frame.id = id;
      } // Simulated dropped/render-gap frames.
      auto reject = [&](const Streamline::SRFrame &bad) {
        Require(!sr->EvaluateSR(gpu.list.Get(), bad).recorded,
                "Invalid SR input accepted");
        ++rejections;
      };
      if (f == 0) {
        auto bad = frame;
        bad.depth = nullptr;
        reject(bad);
        bad = frame;
        bad.motion = frame.depth;
        reject(bad); // Format mismatch.
        bad = frame;
        bad.output = frame.color;
        reject(bad); // Input/output alias.
        bad = frame;
        bad.camera.view_to_clip = {};
        reject(bad);
        bad = frame;
        bad.camera.up = {};
        reject(bad);
        bad = frame;
        bad.jitter_pixels[0] = std::numeric_limits<float>::quiet_NaN();
        reject(bad);
      }
      if (f == 4) {
        auto bad = frame;
        --bad.id;
        reject(bad);
      } // Duplicate frame.
      const float move = 8.f * f;
      const float rect_left = w * .4f + move, rect_right = w * .6f + move;
      for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
          // Sample a stationary plane and a laterally moving foreground
          // rectangle. Projection jitter changes raster location but is absent
          // from motion.
          const float px = x + .5f - frame.jitter_pixels[0];
          const float py = y + .5f - frame.jitter_pixels[1];
          const bool object = px >= rect_left && px < rect_right &&
                              py >= h * .3f && py < h * .7f;
          const bool checker = (int(px / 12.f) ^ int(py / 12.f)) & 1;
          const size_t pixel = size_t(y) * w + x;
          const float rgb[] = {object ? 8.f : .15f + .5f * px / w,
                               object ? 2.f : .1f + .3f * py / h,
                               object    ? .5f
                               : checker ? .3f
                                         : .1f,
                               1.f};
          for (unsigned c = 0; c < 4; ++c)
            color[pixel * 4 + c] =
                DirectX::PackedVector::XMConvertFloatToHalf(rgb[c]);
          depth[pixel] = object ? .8f : .99f;
          motion[pixel * 2] = object && f ? -8.f : 0.f;
          motion[pixel * 2 + 1] = 0;
        }
      gpu.Upload(gpu.color, color.data());
      gpu.Upload(gpu.depth, depth.data());
      gpu.Upload(gpu.motion, motion.data());
      gpu.Upload(gpu.output, sentinel.data(), true);
      const auto result = sr->EvaluateSR(gpu.list.Get(), frame);
      // Submit on failure too: an SDK failure may follow partial command
      // recording.
      gpu.ReadOutput();
      Require(result.recorded, "Real DLSS evaluation failed");
      const bool expected_reset = f == 0 || f == 2 || f == 3 || f == 4;
      Require(result.history_reset == expected_reset,
              "SR history reset policy mismatch");
      gpu.ValidateOutput(frame.id, result.history_reset);
      ++evaluations;
      if (f == 1) {
        Require(!sr->ConfigureSsxSR(scenario.quality, scenario.width - 16,
                                    scenario.height),
                "SR accepted output resize without drain/release");
        ++rejections;
      }
    }
  }
  gpu.Finish();
  Require(sr->ReleaseSR(), "SR final release");
  std::printf("PASS real DLSS evaluations=%u rejected_inputs_or_changes=%u; "
              "synthetic scene only, FG off\n",
              evaluations, rejections);
}

void CheckSsxSceneReconstruction(D3D12Provider& provider, const char* color_replay) {
  Harness gpu(provider);
  SsxReconstruction reconstruction;
  std::vector<std::shared_ptr<const SsxFGTemporal>> retained_motion;
  struct Scenario { uint32_t scene_w, scene_h, scale, output_w, output_h; Streamline::Quality quality; };
  const Scenario scenarios[] = {
    {1120,584,3,3360,1752,Streamline::Quality::kDLAA},
    {1120,584,2,3360,1752,Streamline::Quality::kQuality},
    {1280,720,2,3840,2160,Streamline::Quality::kQuality},
  };
  uint64_t id = 200;
  const std::array<float,3> expected{.25f,2.f,3.f};
  for (const auto& scenario : scenarios) {
    if (color_replay && scenario.quality!=Streamline::Quality::kDLAA) continue;
    const uint32_t w = scenario.scene_w*scenario.scale, h = scenario.scene_h*scenario.scale;
    const uint32_t ow = scenario.output_w, oh = scenario.output_h;
    std::vector<uint16_t> values(size_t(w)*h*4);
    std::vector<float> depths(size_t(w)*h,.01f);
    std::vector<uint16_t> sentinel(size_t(ow)*oh*4,DirectX::PackedVector::XMConvertFloatToHalf(-100.f));
    for (uint32_t y=0;y<h;++y) for (uint32_t x=0;x<w;++x) {
      for (unsigned c=0;c<3;++c)
        values[(size_t(y)*w+x)*4+c]=DirectX::PackedVector::XMConvertFloatToHalf(expected[c]);
      values[(size_t(y)*w+x)*4+3]=DirectX::PackedVector::XMConvertFloatToHalf(.125f+.25f*x/w+.125f*y/h);
    }
    if (color_replay) {
      std::ifstream file(color_replay,std::ios::binary|std::ios::ate);
      Require(file && file.tellg()==std::streamoff(values.size()*sizeof(uint16_t)),
              "Color replay requires a tightly packed 3360x1752 RGBA16F capture");
      file.seekg(0); file.read(reinterpret_cast<char*>(values.data()),values.size()*sizeof(uint16_t));
      Require(bool(file),"Read color replay");
      std::puts("COLOR_REPLAY: fixed captured color, synthetic static camera/depth, zero jitter; brightness test only");
    }
    for (unsigned test=0;test<(color_replay ? 32u : 3u);++test,++id) {
      auto frame=std::make_shared<SsxFrameInputs>();
      auto& f=frame->metadata;
      f.guest_frame=id;
      f.scene_width=scenario.scene_w; f.scene_height=scenario.scene_h;
      f.scale_x=f.scale_y=scenario.scale;
      f.output_width=ow; f.output_height=oh; f.captured_mask=3;
      f.camera_samples=1; f.camera_viewport_valid=true;
      f.guest_viewport_xy={f.scene_width*.5f,f.scene_height*-.5f,f.scene_width*.5f,f.scene_height*.5f};
      f.guest_viewport_z={-1,1};
      f.depth_before_lighting=true; f.depth_draw=10; f.color_draw=20;
      f.jitter_enabled=true; f.jittered_draws=1;
      f.jitter_pixels={test==1 ? -.25f : .25f,1.f/6};
      if (color_replay) f.jitter_pixels={0,0};
      const float a=8000.f/(8000.f-.5f), b=-.5f*a;
      f.world_to_clip_candidate={1.5f,0,0,0, 0,8.f/3,0,0, 0,0,-a,-1, -75,-1600.f/3,1000*a+b,1000};
      gpu.color=gpu.MakeTexture(w,h,DXGI_FORMAT_R16G16B16A16_FLOAT,8);
      gpu.depth=gpu.MakeTexture(w,h,DXGI_FORMAT_R32_FLOAT,4);
      gpu.output=gpu.MakeTexture(ow,oh,DXGI_FORMAT_R16G16B16A16_FLOAT,8,true);
      gpu.Begin(); gpu.Upload(gpu.color,values.data()); gpu.Upload(gpu.depth,depths.data());
      gpu.Upload(gpu.output,sentinel.data(),true);
      Barrier(gpu.list.Get(),gpu.color.gpu.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,GuestInputTexture::kReadState);
      Barrier(gpu.list.Get(),gpu.depth.gpu.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,GuestInputTexture::kReadState);
      gpu.Finish();
      auto& color=frame->textures[0];
      color.resource=gpu.color.gpu; color.width=w; color.height=h;
      color.view_format=DXGI_FORMAT_R16G16B16A16_FLOAT; color.host_swizzle=0x688; color.swizzled_signs=0x55;
      auto& depth=frame->textures[1]; depth.resource=gpu.depth.gpu; depth.width=w; depth.height=h;
      depth.view_format=DXGI_FORMAT_R32_FLOAT;
      if (test==0) {
        f.scene_msaa_samples=4;
        const auto rejected=reconstruction.Submit(provider,frame,gpu.output.gpu.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,id/60.0,scenario.quality);
        Require(!rejected.submitted && !rejected.applied,"Multisampled scene entered reconstruction");
        f.scene_msaa_samples=1;
        const auto wrong_mode = scenario.quality==Streamline::Quality::kDLAA ? Streamline::Quality::kQuality : Streamline::Quality::kDLAA;
        const auto dimensions=reconstruction.Submit(provider,frame,gpu.output.gpu.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,id/60.0,wrong_mode);
        Require(!dimensions.submitted && !dimensions.applied,"Incorrect Quality/DLAA dimensions accepted");
      }
      const bool retain = !color_replay && scenario.quality==Streamline::Quality::kDLAA;
      const auto result=reconstruction.Submit(provider,frame,gpu.output.gpu.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,id/60.0,scenario.quality,true,retain);
      Require(result.submitted && result.applied,"SSX decode/reconstruct/re-encode boundary failed");
      Require(bool(result.fg_temporal)==retain,"SSX FG temporal handoff missing or unexpectedly enabled");
      if (retain) {
        const auto& t=*result.fg_temporal;
        Require(t.guest_frame==id && t.reset==result.reset && t.depth_inverted &&
                t.jitter_pixels==f.jitter_pixels && t.motion.width==w && t.motion.height==h,
                "SSX FG temporal metadata mismatch");
        for (const auto& previous:retained_motion)
          Require(previous->motion.resource.Get()!=t.motion.resource.Get(),
                  "SSX FG overwrote a consumer-held motion buffer");
        retained_motion.push_back(result.fg_temporal);
        // Read back actual copied motion. This scene is static despite jitter;
        // including projection jitter in the motion would make this fail.
        auto read=gpu.MakeTexture(w,h,DXGI_FORMAT_R32G32_FLOAT,8,true);
        gpu.Begin();
        Barrier(gpu.list.Get(),t.motion.resource.Get(),GuestInputTexture::kReadState,
                D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION from{},to{};
        from.pResource=t.motion.resource.Get(); from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        to.pResource=read.readback.Get(); to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        to.PlacedFootprint=read.footprint;
        gpu.list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
        Barrier(gpu.list.Get(),t.motion.resource.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,
                GuestInputTexture::kReadState);
        gpu.Finish();
        void* mapped=nullptr;
        Check(read.readback->Map(0,nullptr,&mapped),"SSX FG motion readback");
        double maximum=0;
        for (uint32_t y=0;y<h;++y) {
          const auto* row=reinterpret_cast<const float*>(static_cast<const char*>(mapped)+
                          y*read.footprint.Footprint.RowPitch);
          for (uint32_t x=0;x<w*2;++x) {
            Require(std::isfinite(row[x]),"SSX FG motion is not finite");
            maximum=std::max(maximum,std::abs(double(row[x])));
          }
        }
        D3D12_RANGE none{}; read.readback->Unmap(0,&none);
        Require(maximum<.005,"SSX FG retained static motion includes jitter or stale data");
        std::printf("SSX_FG_RETAIN frame=%llu held=%zu max_static_motion=%.8f\n",id,retained_motion.size(),maximum);
      }
      if (id>=203) {
        Require(result.timed_frame && result.timed_frame<id,"Missing completed reconstruction timing");
        Require(std::isfinite(result.inputs_ms) && result.inputs_ms>=0 &&
                std::isfinite(result.dlss_ms) && result.dlss_ms>0 &&
                std::isfinite(result.encode_ms) && result.encode_ms>=0,"Invalid GPU reconstruction timing");
        std::printf("SSX_TIMING frame=%llu inputs_ms=%.3f dlss_ms=%.3f encode_ms=%.3f\n",
                    result.timed_frame,result.inputs_ms,result.dlss_ms,result.encode_ms);
      }
      Require(result.reset==(test==0),"SSX boundary reset on consecutive static frames");
      gpu.Begin(); gpu.ReadOutput();
      void* data;
      Check(gpu.output.readback->Map(0,nullptr,&data),"SSX reconstruction readback");
      float maximum_error=0,alpha_error=0; uint64_t invalid=0;
      double energy_in=0,energy_out=0;
      std::ofstream replay_output;
      if (color_replay && test==31) replay_output.open("color-replay-output.bin",std::ios::binary);
      for (uint32_t y=0;y<oh;++y) {
        const auto* row=reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(data)+gpu.output.footprint.Footprint.RowPitch*y);
        if (replay_output.is_open()) replay_output.write(reinterpret_cast<const char*>(row),ow*8);
        for (uint32_t x=0;x<ow;++x) for (unsigned c=0;c<4;++c) {
          const float v=DirectX::PackedVector::XMConvertHalfToFloat(row[x*4+c]);
          invalid+=!std::isfinite(v) || v < 0;
          if (color_replay) {
            const float original=DirectX::PackedVector::XMConvertHalfToFloat(values[(size_t(y)*w+x)*4+c]);
            if(c<3) { energy_in+=double(original)*original; energy_out+=double(v)*v; }
            else alpha_error=std::max(alpha_error,std::abs(v-original));
            continue;
          }
          if (c<3) maximum_error=std::max(maximum_error,std::abs(v-expected[c]));
          else {
            const float sx=std::clamp((x+.5f)*w/ow-.5f,0.f,float(w-1));
            const float sy=std::clamp((y+.5f)*h/oh-.5f,0.f,float(h-1));
            alpha_error=std::max(alpha_error,std::abs(v-(.125f+.25f*sx/w+.125f*sy/h)));
          }
        }
      }
      D3D12_RANGE none{0,0}; gpu.output.readback->Unmap(0,&none);
      if (color_replay) {
        if (replay_output.is_open()) Require(bool(replay_output),"Write color replay output");
        std::printf("COLOR_REPLAY frame=%llu mode=DLAA preset=L mean_linear_in=%.6f mean_linear_out=%.6f energy_ratio=%.6f alpha_error=%.6f invalid=%llu\n",
                    id,energy_in/(w*h*3),energy_out/(w*h*3),energy_out/std::max(energy_in,1e-12),alpha_error,invalid);
        Require(!invalid && alpha_error==0,"Color replay has invalid pixels or changed alpha");
        continue;
      }
      std::printf("SSX_BOUNDARY frame=%llu mode=%s preset=L input=%ux%u output=%ux%u reversed_depth=1 scene_samples=1 reset=%d max_encoded_error=%.6f alpha_error=%.6f invalid=%llu\n",
                  f.guest_frame,scenario.quality==Streamline::Quality::kDLAA ? "DLAA" : "Quality",w,h,ow,oh,result.reset,maximum_error,alpha_error,invalid);
      Require(!invalid && maximum_error<.04f && alpha_error<.001f,"SSX encoded color/alpha reconstruction failed");
    }
  }
  if (color_replay)
    std::puts("PASS color replay; inspect brightness ratios and local color-replay-output.bin; no gameplay/motion-quality assertion");
  else
    std::puts("PASS SSX decode/DLAA-or-Quality/encode/copy boundary; 9 frames, MSAA/dimension guards, alpha resampled separately");
}

void CheckSsxQualityScale(D3D12Provider& provider) {
#ifndef SSX_TEST_QUALITY_SCALE
  std::puts("SKIP QUALITY_SCALE: configure SSX_SDK_SOURCE_DIR to test production shaders");
#else
  #include "ssx_edram_rescale_cs.h"
  #include "ssx_quality_extract_cs.h"
  Harness gpu(provider);
  auto* device=provider.GetDevice();
  D3D12_DESCRIPTOR_RANGE ranges[] = {
      {D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0}, {D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,0}};
  D3D12_ROOT_PARAMETER params[3]{};
  params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  params[0].Constants={0,0,6};
  for (uint32_t i=0;i<2;++i) {
    params[i+1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[i+1].DescriptorTable={1,&ranges[i]};
  }
  D3D12_ROOT_SIGNATURE_DESC desc{}; desc.NumParameters=3; desc.pParameters=params;
  ComPtr<ID3D12RootSignature> root;
  root.Attach(util::CreateRootSignature(provider,desc));
  Require(bool(root),"Quality scale root");
  ComPtr<ID3D12PipelineState> rescale, extract;
  rescale.Attach(util::CreateComputePipeline(device,ssx_edram_rescale_cs,sizeof(ssx_edram_rescale_cs),root.Get()));
  extract.Attach(util::CreateComputePipeline(device,ssx_quality_extract_cs,sizeof(ssx_quality_extract_cs),root.Get()));
  Require(rescale && extract,"Quality scale pipelines");
  D3D12_DESCRIPTOR_HEAP_DESC hd{};
  hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; hd.NumDescriptors=4;
  hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  ComPtr<ID3D12DescriptorHeap> heap;
  Check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)),"Quality scale heap");
  const auto step=provider.GetViewDescriptorSize();
  auto cpu=[&](uint32_t i) { auto h=heap->GetCPUDescriptorHandleForHeapStart();h.ptr+=step*i;return h; };
  auto handle=[&](uint32_t i) { auto h=heap->GetGPUDescriptorHandleForHeapStart();h.ptr+=step*i;return h; };
  auto bind=[&](ID3D12PipelineState* p,uint32_t first) {
    auto* h=heap.Get();gpu.list->SetDescriptorHeaps(1,&h);
    gpu.list->SetComputeRootSignature(root.Get());gpu.list->SetPipelineState(p);
    gpu.list->SetComputeRootDescriptorTable(1,handle(first));
    gpu.list->SetComputeRootDescriptorTable(2,handle(first+1));
  };
  auto buffer=[&](uint32_t words) {
    D3D12_RESOURCE_DESC d{};
    util::FillBufferResourceDesc(d,uint64_t(words)*4,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ComPtr<ID3D12Resource> p;
    Check(device->CreateCommittedResource(&util::kHeapPropertiesDefault,D3D12_HEAP_FLAG_NONE,&d,
        D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&p)),"Quality EDRAM buffer");
    return p;
  };
  const uint32_t native_words=1280*4*8, storage_words=1280*9*8;
  // Full uint32 patterns include NaNs when reinterpreted as floats: copies must
  // preserve bits, both words of 64-bit pixels, tile edges and untouched tiles.
  std::vector<uint32_t> original(native_words), sentinel(storage_words,0xCDCDCDCD);
  for (uint32_t i=0;i<native_words;++i) original[i]=(i*2654435761u)^0x7FC00001;
  for (uint32_t bpp : {1u,2u}) for (uint32_t msaa : {0u,1u,2u}) {
    auto low=buffer(native_words), high=buffer(storage_words), back=buffer(native_words);
    auto upload=gpu.Buffer(uint64_t(storage_words)*4,D3D12_HEAP_TYPE_UPLOAD);
    auto native_upload=gpu.Buffer(uint64_t(native_words)*4,D3D12_HEAP_TYPE_UPLOAD);
    auto read=gpu.Buffer(uint64_t(storage_words)*4,D3D12_HEAP_TYPE_READBACK);
    auto read_back=gpu.Buffer(uint64_t(native_words)*4,D3D12_HEAP_TYPE_READBACK);
    void* mapped;
    Check(upload->Map(0,nullptr,&mapped),"Quality map storage");
    std::memcpy(mapped,sentinel.data(),storage_words*4); upload->Unmap(0,nullptr);
    Check(native_upload->Map(0,nullptr,&mapped),"Quality map native");
    std::memcpy(mapped,original.data(),native_words*4); native_upload->Unmap(0,nullptr);
    auto srv=[&](ID3D12Resource* p,uint32_t n,uint32_t i) {
      D3D12_SHADER_RESOURCE_VIEW_DESC s{};s.Format=DXGI_FORMAT_R32_UINT;
      s.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;s.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      s.Buffer.NumElements=n;device->CreateShaderResourceView(p,&s,cpu(i));
    };
    auto uav=[&](ID3D12Resource* p,uint32_t n,uint32_t i) {
      D3D12_UNORDERED_ACCESS_VIEW_DESC u{};u.Format=DXGI_FORMAT_R32_UINT;
      u.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;u.Buffer.NumElements=n;
      device->CreateUnorderedAccessView(p,nullptr,&u,cpu(i));
    };
    srv(low.Get(),native_words,0);uav(high.Get(),storage_words,1);
    srv(high.Get(),storage_words,2);uav(back.Get(),native_words,3);
    gpu.Begin();
    gpu.list->CopyBufferRegion(low.Get(),0,native_upload.Get(),0,native_words*4);
    gpu.list->CopyBufferRegion(back.Get(),0,native_upload.Get(),0,native_words*4);
    gpu.list->CopyBufferRegion(high.Get(),0,upload.Get(),0,storage_words*4);
    Barrier(gpu.list.Get(),low.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Barrier(gpu.list.Get(),high.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    bind(rescale.Get(),0);
    uint32_t k[]={2,3,2,4,bpp,msaa};gpu.list->SetComputeRoot32BitConstants(0,6,k,0);
    gpu.list->Dispatch(45,4,1);
    Barrier(gpu.list.Get(),high.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
    gpu.list->CopyBufferRegion(read.Get(),0,high.Get(),0,storage_words*4);
    Barrier(gpu.list.Get(),high.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Barrier(gpu.list.Get(),back.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    bind(rescale.Get(),2);k[0]=3;k[1]=2;gpu.list->SetComputeRoot32BitConstants(0,6,k,0);
    gpu.list->Dispatch(20,4,1);
    Barrier(gpu.list.Get(),back.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
    gpu.list->CopyBufferRegion(read_back.Get(),0,back.Get(),0,native_words*4);
    gpu.Finish();
    Check(read_back->Map(0,nullptr,&mapped),"Quality roundtrip map");
    Require(std::memcmp(mapped,original.data(),native_words*4)==0,"Quality EDRAM roundtrip lost native bits");
    read_back->Unmap(0,nullptr);
    Check(read->Map(0,nullptr,&mapped),"Quality storage map");
    auto* actual=static_cast<uint32_t*>(mapped);
    for (uint32_t tile=0;tile<8;++tile)
      for (uint32_t y=0;y<48;++y)
        for (uint32_t x=0;x<240/bpp;++x)
          for (uint32_t word=0;word<bpp;++word) {
            const auto index=tile*1280*9+y*240+x*bpp+word;
            const uint32_t xs=msaa>=2 ? 2 : 1, ys=msaa>=1 ? 2 : 1;
            const auto sx=uint32_t(std::floor((double(x/xs)+.5)*2/3))*xs+x%xs;
            const auto sy=uint32_t(std::floor((double(y/ys)+.5)*2/3))*ys+y%ys;
            const auto expected=tile>=2 && tile<6 ? original[tile*1280*4+sy*160+sx*bpp+word] : 0xCDCDCDCD;
            Require(actual[index]==expected,"Quality EDRAM wrong pixel/component/tile");
          }
    read->Unmap(0,nullptr);
  }
  // Exercise the exact texture extraction shader with the two production formats.
  for (auto format : {DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R16G16B16A16_FLOAT}) {
    const uint32_t bytes=format==DXGI_FORMAT_R32_FLOAT ? 4 : 8;
    auto source=gpu.MakeTexture(240,96,format,bytes);
    gpu.output=gpu.MakeTexture(160,64,format,bytes,true);
    std::vector<uint8_t> native(160*64*bytes), replicated(240*96*bytes);
    for (uint32_t y=0;y<64;++y) for(uint32_t x=0;x<160;++x) {
      if(bytes==4) { float f=float(y*160+x)/16384;std::memcpy(native.data()+(y*160+x)*bytes,&f,4); }
      else for(uint32_t c=0;c<4;++c) {
        const auto half=DirectX::PackedVector::XMConvertFloatToHalf(float((x+3*y+c)%123)/64);
        std::memcpy(native.data()+(y*160+x)*bytes+c*2,&half,2);
      }
    }
    for(uint32_t y=0;y<96;++y) for(uint32_t x=0;x<240;++x) {
      const auto sx=uint32_t((double(x)+.5)*2/3),sy=uint32_t((double(y)+.5)*2/3);
      std::memcpy(replicated.data()+(y*240+x)*bytes,native.data()+(sy*160+sx)*bytes,bytes);
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=format;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;
    device->CreateShaderResourceView(source.gpu.Get(),&srv,cpu(0));
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.Format=format;uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
    device->CreateUnorderedAccessView(gpu.output.gpu.Get(),nullptr,&uav,cpu(1));
    gpu.Begin();gpu.Upload(source,replicated.data());bind(extract.Get(),0);
    uint32_t k[]={160,64};gpu.list->SetComputeRoot32BitConstants(0,2,k,0);gpu.list->Dispatch(20,8,1);
    gpu.ReadOutput();void* mapped;
    Check(gpu.output.readback->Map(0,nullptr,&mapped),"Quality extract map");
    for(uint32_t y=0;y<64;++y)
      Require(std::memcmp(static_cast<uint8_t*>(mapped)+y*gpu.output.footprint.Footprint.RowPitch,
          native.data()+y*160*bytes,160*bytes)==0,"Quality extraction changed native sample");
    gpu.output.readback->Unmap(0,nullptr);
  }
  std::puts("PASS QUALITY_SCALE: packed32/64 at 1/2/4 samples bit-exact 2x-3x-2x, untouched tiles, R32 depth/FP16 color native extraction");
#endif
}
