#include <array>
#include <cstdio>
#include <cstring>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/d3d12/d3d12_provider.h>
#include <rex/ui/d3d12/ssx_hdr.h>
#include <rex/ui/d3d12/ssx_fg_color.h>
#include <rex/ui/d3d12/ssx_hdr_layers.h>
#include <rex/ui/d3d12/ssx_scene_color.h>
#include <stdexcept>
#include <vector>
#include "reference_shader.h"
using namespace rex::ui::d3d12;
using Microsoft::WRL::ComPtr;
namespace {
void Require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
void Check(HRESULT hr, const char *message) { Require(SUCCEEDED(hr), message); }
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
  d.Height = d.DepthOrArraySize = d.MipLevels = d.SampleDesc.Count = 1;
  d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = upload ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_READBACK;
  ComPtr<ID3D12Resource> resource;
  Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d,
                                        upload
                                            ? D3D12_RESOURCE_STATE_GENERIC_READ
                                            : D3D12_RESOURCE_STATE_COPY_DEST,
                                        nullptr, IID_PPV_ARGS(&resource)),
        "Buffer");
  return resource;
}
struct Case {
  const char *name;
  float peak = 1000;
  uint32_t width = 64, height = 32;
  bool native = true, exposure = false, linear = false, calibration = false,
       transport = true;
  uint32_t overlay = 0; // BGRA bytes; premultiplied SDR UI in native mode.
  float ui = 200, ev = 0, boost = 1;
  bool shadows = false;
  uint32_t grading = 0;
  bool color = false, point_lut = false;
  uint32_t layers = 0, post_mode = 0;
  float post_strength = 1;
  uint8_t bloom_code = 32;
  bool compressed_post = false;
  bool trace_light = false;
  uint32_t scale = 3;
  bool independent = false, direct_scene = false;
};
struct Harness {
  std::unique_ptr<D3D12Provider> provider = D3D12Provider::Create();
  ComPtr<ID3D12Fence> fence;
  SsxHDR pass;
  SsxHDR plain_pass;
  ReferenceShader reference;
  uint64_t submission = 0;
  uint64_t plain_channel_checks = 0, reference_channel_checks = 0;
  int plain_max_error = 0, reference_max_error = 0;
  uint32_t checks = 0, hudless_checks = 0;
  int max_error = 0;
  bool independent_only = false;
  Harness() {
    Require(bool(provider), "provider");
    Check(provider->GetDevice()->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                             IID_PPV_ARGS(&fence)),
          "fence");
  }
  void Finish(ID3D12GraphicsCommandList *list) {
    Check(list->Close(), "close");
    ID3D12CommandList *lists[] = {list};
    provider->GetDirectQueue()->ExecuteCommandLists(1, lists);
    Check(provider->GetDirectQueue()->Signal(fence.Get(), ++submission),
          "signal");
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    Require(event != nullptr, "event");
    const HRESULT hr = fence->SetEventOnCompletion(submission, event);
    const auto status =
        SUCCEEDED(hr) ? WaitForSingleObject(event, 20000) : WAIT_FAILED;
    CloseHandle(event);
    Require(status == WAIT_OBJECT_0, "GPU timeout");
  }
  void Run(const Case &c) {
    if (independent_only && (!c.independent || c.peak != 1000)) return;
    auto *device = provider->GetDevice();
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         IID_PPV_ARGS(&allocator)),
          "allocator");
    Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                    allocator.Get(), nullptr,
                                    IID_PPV_ARGS(&list)),
          "list");
    const auto index = uint32_t(submission % 3);
    Require(pass.Prepare(*provider, index, c.width, c.height), "HDR prepare");
    std::vector<ComPtr<ID3D12Resource>> uploads;
    auto upload = [&](ID3D12Resource *resource, uint32_t pixel_bytes,
                      auto pixel, D3D12_RESOURCE_STATES state) {
      const auto d = resource->GetDesc();
      D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
      UINT64 bytes = 0;
      device->GetCopyableFootprints(&d, 0, 1, 0, &fp, nullptr, nullptr, &bytes);
      auto buffer = Buffer(device, bytes, true);
      void *mapped = nullptr;
      D3D12_RANGE none{};
      Check(buffer->Map(0, &none, &mapped), "upload map");
      const uint32_t depth = d.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D
                                 ? d.DepthOrArraySize
                                 : 1;
      for (uint32_t z = 0; z < depth; ++z)
        for (uint32_t y = 0; y < d.Height; ++y)
          for (uint32_t x = 0; x < d.Width; ++x)
            pixel(x, y + z * d.Height,
                  static_cast<uint8_t *>(mapped) +
                      (z * d.Height + y) * fp.Footprint.RowPitch +
                      x * pixel_bytes);
      buffer->Unmap(0, nullptr);
      D3D12_TEXTURE_COPY_LOCATION from{}, to{};
      from.pResource = buffer.Get();
      from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
      from.PlacedFootprint = fp;
      to.pResource = resource;
      to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
      Transition(list.Get(), resource, D3D12_RESOURCE_STATE_COPY_DEST, state);
      uploads.push_back(buffer);
    };
    ComPtr<ID3D12Resource> overlay;
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = c.width;
    d.Height = c.height;
    d.DepthOrArraySize = d.MipLevels = d.SampleDesc.Count = 1;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d,
                                          D3D12_RESOURCE_STATE_COPY_DEST,
                                          nullptr, IID_PPV_ARGS(&overlay)),
          "overlay");
    upload(
        overlay.Get(), 4,
        [&](auto, auto, void *target) { std::memcpy(target, &c.overlay, 4); },
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    auto frame = std::make_shared<SsxFrameInputs>();
    frame->metadata.guest_frame = submission + 1;
    frame->metadata.scene_width = 1120;
    frame->metadata.scene_height = 584;
    frame->metadata.scale_x = frame->metadata.scale_y = c.scale;
    frame->metadata.captured_mask = c.exposure ? 5 : 1;
    frame->metadata.exposure_branch = c.exposure;
    frame->metadata.tone_map_constants[0] = 1;
    frame->metadata.tone_map_constants[4] = 8;
    frame->metadata.tone_map_constants[36] = 2;
    frame->metadata.tone_map_constants[8] = .22f;
    frame->metadata.tone_map_constants[12] = .3f;
    frame->metadata.tone_map_constants[16] = .03f;
    frame->metadata.tone_map_constants[20] = .002f;
    frame->metadata.tone_map_constants[24] = .06f;
    frame->metadata.tone_map_constants[28] = 1.0f / 30;
    frame->metadata.tone_map_constants[32] = 1.489361763f;
    auto retained = std::make_shared<SsxFGTemporal>();
    retained->guest_frame = frame->metadata.guest_frame;
    retained->hdr_valid = true;
    frame->fg_temporal = retained;
    bool created;
    Require(retained->hdr_color.Prepare(device, DXGI_FORMAT_R16G16B16A16_FLOAT,
                                        1120 * c.scale, 584 * c.scale, created),
            "scene");
    static constexpr uint16_t half_values[] = {0,      0x3400, 0x3c00, 0x4000,
                                               0x4400, 0x4800, 0x4c00, 0x5080};
    static constexpr double values[] = {0, .25, 1, 2, 4, 8, 16, 36};
    static constexpr uint16_t shadow_half[] = {0,      0x1c00, 0x2400, 0x2c00,
                                               0x3000, 0x3400, 0x3800, 0x3c00};
    static constexpr double shadow_values[] = {
        0, 1. / 256, 1. / 64, 1. / 16, 1. / 8, .25, .5, 1};
    upload(
        retained->hdr_color.resource.Get(), 8,
        [&](uint32_t x, auto, void *target) {
          const auto h = (c.shadows ? shadow_half : half_values)[x / (140 * c.scale)];
          const uint16_t pixel[] = {h, uint16_t(c.color && h ? h - 0x400 : h),
                                    uint16_t(c.color && h ? h - 0x800 : h),
                                    0x3c00};
          std::memcpy(target, pixel, 8);
        },
        GuestInputTexture::kReadState);
    // Decoder validation needs the original scene's metadata, not another copy.
    frame->textures[0] = retained->hdr_color;
    frame->textures[0].view_format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    frame->textures[0].host_swizzle = 0x688;
    frame->textures[0].swizzled_signs = 0x55;
    if (c.exposure) {
      auto &exposure = frame->textures[2];
      Require(exposure.Prepare(device, DXGI_FORMAT_R16_TYPELESS, 3, 1, created),
              "exposure");
      exposure.view_format = DXGI_FORMAT_R16_UNORM;
      exposure.host_swizzle = 0xA00;
      exposure.source_mip = 8;
      exposure.guest_fetch[4] = 8 << 6;
      exposure.guest_fetch[0] = (2 << 10) | (2 << 13);
      exposure.guest_fetch[3] = c.linear ? (1 << 19) | (1 << 21) : 0;
      upload(
          exposure.resource.Get(), 2,
          [&](uint32_t x, auto, void *target) {
            uint16_t v = x == 0 ? 0 : x == 1 ? 32768 : 65535;
            if (c.shadows || c.grading)
              v = 0;
            std::memcpy(target, &v, 2);
          },
          GuestInputTexture::kReadState);
    }
    // Synthetic non-separable RGB volume detects axis/channel mistakes. Its
    // storage is BGR, exactly like the captured guest view; every node is
    // UNORM8.
    auto lut_node = [&](int x, int y, int z, int channel) {
      const double r = std::clamp(x, 0, 31) / 31.,
                   g = std::clamp(y, 0, 31) / 31.,
                   b = std::clamp(z, 0, 31) / 31.;
      double result = channel == 0 ? r : channel == 1 ? g : b;
      if (c.grading >= 2) {
        if (channel == 0)
          result = .8 * r + .12 * g + .08 * b;
        if (channel == 1)
          result = .05 * r + .9 * g + .05 * b;
        if (channel == 2)
          result = .1 * r + .15 * g + .75 * b;
      }
      if (c.grading == 3)
        result = .04 + .96 * result;
      if (c.grading == 4)
        result *= channel == 0 ? .65 : channel == 1 ? .8 : 1;
      if (c.grading == 7)
        // Transition LUT: bright at zero, with authored negative slopes in
        // red/green. A LUT's zero sample is not a display black pedestal.
        result = channel == 0 ? .94 - .5 * result
                              : channel == 1 ? .94 - .25 * result : .94 + .04 * result;
      return std::round(result * 255) / 255;
    };
    if (c.grading) {
      auto &m = frame->metadata;
      m.grading_captured = m.display_gamma_captured = true;
      switch (c.post_mode) {
      case 0:
        m.hdr_post_shader = 0x86455B1D2BA4A035ull;
        break;
      case 1:
        m.hdr_post_shader = 0x418794134AA3A6AFull;
        break;
      case 2:
        m.hdr_post_shader = 0x94262AE20BE5EC7Aull;
        break;
      case 4:
        m.hdr_post_shader = 0x88095FB3FE6C244Dull;
        break;
      case 5:
        m.hdr_post_shader = 0x48CD7B80EE1F1D67ull;
        break;
      default:
        Require(false, "unknown test post variant");
      }
      m.hdr_post_constants = {.03f,
                              .08f,
                              .14f,
                              0,
                              .15f,
                              .65f,
                              0,
                              0,
                              2 * c.post_strength,
                              .5f * c.post_strength,
                              .25f * c.post_strength,
                              1,
                              .25f * c.post_strength,
                              .75f * c.post_strength,
                              3 * c.post_strength,
                              .8f,
                              .1f,
                              .7f,
                              .6f,
                              0,
                              1,
                              1,
                              .5f,
                              .5f,
                              2,
                              0,
                              0,
                              3};
      if (c.post_mode == 2) {
        m.hdr_post_constants[24] = -.5f;
        m.hdr_post_constants[26] = 3;
        m.hdr_post_constants[27] = 2;
      }
      m.color_draw = 100;
      m.grading_draw = 200;
      m.grading_constants = {1, 0, 0, 0, 7, 0, 0, 1, 31.f / 32, 1.f / 64, 0, 0};
      for (uint32_t i = 0; i < 256; ++i) {
        const double code = double(i) / 255;
        const auto g = uint32_t(std::round((c.grading == 5 ? std::pow(code, 1.1)
                                            : c.grading == 6 ? .04 + .96 * code
                                                             : code) *
                                           1023));
        m.fg_gamma_ramp[i] = (g << 20) | (g << 10) | g;
      }
      auto &lut = frame->grading_lut;
      Require(lut.PrepareGradingLut(device, 32, created), "LUT allocation");
      lut.host_swizzle = 0x60A;
      lut.guest_fetch = {0x824802, 0xad44086, 0x7c0f81f, 0x1280c14, 3, 0x400};
      if (c.point_lut) {
        lut.guest_fetch[3] &= ~((3u << 19) | (3u << 21));
        lut.guest_fetch[4] = 0;
      }
      upload(
          lut.resource.Get(), 4,
          [&](uint32_t x, uint32_t yz, void *target) {
            uint8_t pixel[4];
            for (int channel = 0; channel < 3; ++channel)
              pixel[2 - channel] = uint8_t(
                  std::round(lut_node(x, yz % 32, yz / 32, channel) * 255));
            pixel[3] = 255;
            std::memcpy(target, pixel, 4);
          },
          GuestInputTexture::kReadState);
    }
    // Uniform synthetic draw layers: contribution RGB and destination
    // transmission.
    std::array<double, 4> world{0, 0, 0, 1}, hud{0, 0, 0, 1};
    if (c.layers == 2 || c.layers == 9)
      world = {.125, .25, .0625, .5};
    if (c.layers == 3)
      world = {.25, .5, .125, 0};
    if (c.layers == 4)
      world = {.125, .25, .0625, 1};
    // Two additive SDR draws can leave C above one in the affine recording.
    // The guest UNORM world target clamps it before the final post shader.
    if (c.layers == 13 || c.layers == 14)
      world = {2, .5, .25, c.layers == 14 ? 0. : 1.};
    if (c.layers == 6 || c.layers == 9)
      hud = {.25, .125, .0625, .5};
    if (c.layers == 7)
      hud = {.5, .25, .125, 0};
    if (c.layers == 8)
      hud = {.5, .25, .125, 1};
    std::array<double,3> world_t{world[3],world[3],world[3]},hud_t{hud[3],hud[3],hud[3]};
    if(c.layers==10)hud_t={.25,.5,.125};
    if(c.layers==12)world_t={.5,.25,.125};
    const bool glare = c.layers == 5 || c.layers == 9;
    if (c.layers) {
      auto half = [](double v) -> uint16_t {
        if (v == 0)
          return 0;
        if (v == .0625)
          return 0x2c00;
        if (v == .125)
          return 0x3000;
        if (v == .25)
          return 0x3400;
        if (v == .5)
          return 0x3800;
        if (v == 2)
          return 0x4000;
        return 0x3c00;
      };
      for (bool is_hud : {false,true}) for(bool is_transmission:{false,true}) {
        auto& layer=is_transmission ? (is_hud?frame->hdr_hud_transmission:frame->hdr_world_transmission) :
                                     (is_hud?frame->hdr_hud:frame->hdr_world);
        Require(layer.Prepare(device,DXGI_FORMAT_R16G16B16A16_FLOAT,
            (is_hud?1280:1120)*c.scale,(is_hud?720:584)*c.scale,created),"layer allocation");
        upload(layer.resource.Get(),8,[&](auto,auto,void* target){
          uint16_t p[4]={};const auto& v=is_hud?hud:world;const auto& t=is_hud?hud_t:world_t;
          for(int i=0;i<3;++i)p[i]=half(is_transmission?t[i]:v[i]);
          std::memcpy(target,p,8);
        },GuestInputTexture::kReadState);
      }
      auto &bloom = frame->hdr_bloom;
      Require(bloom.Prepare(device, DXGI_FORMAT_R8G8B8A8_TYPELESS, 280*c.scale, 146*c.scale,
                            created),
              "bloom allocation");
      bloom.view_format = DXGI_FORMAT_R8G8B8A8_UNORM;
      bloom.host_swizzle = 0x60A;
      bloom.guest_fetch = {0x82404802, 0xa8a0086, 0x122117,
                           0x1280c14,  3,         0x200};
      upload(
          bloom.resource.Get(), 4,
          [&](auto, auto, void *target) {
            const uint8_t p[4] = {uint8_t(glare ? c.bloom_code / 4 : 0),
                                  uint8_t(glare ? c.bloom_code / 2 : 0),
                                  uint8_t(glare ? c.bloom_code : 0), 255};
            std::memcpy(target, p, 4);
          },
          GuestInputTexture::kReadState);
      frame->metadata.hdr_world_captured = frame->metadata.hdr_hud_captured =
          frame->metadata.hdr_bloom_captured = true;
    }
    std::array<std::array<uint8_t, 4>, 3> post_pixels{
        {{48, 96, 160, 64},
         {128, 32, 64, 160},
         {24, 128, 80, 96}}}; // RGBA, stored BGRA.
    if (c.post_mode) {
      const uint32_t mask = SsxHDRPostTextureMask(c.post_mode);
      for (uint32_t i = 0; i < 3; ++i) {
        if (!(mask & (1u << i)))
          continue;
        auto &t = frame->hdr_post_textures[i];
        if (c.compressed_post && i) {
          // Constant DXT5 blocks: color endpoint 0 and alpha endpoint 0 are
          // selected for every texel. This independently exercises native BC3
          // sampling using the real Tricky fetch contract from the race log.
          Require(t.Prepare(device,DXGI_FORMAT_BC3_UNORM,4,4,created),"BC3 post allocation");
          t.view_format=DXGI_FORMAT_BC3_UNORM;t.host_swizzle=0x688;
          t.guest_fetch={0x8A004802,0x1285C054,0x59E4FF,0x280D10,3,0xA00};
          post_pixels[i]=i==1 ? std::array<uint8_t,4>{255,0,0,160} : std::array<uint8_t,4>{0,255,0,96};
          std::array<uint8_t,16> block{};block[0]=post_pixels[i][3];
          const uint16_t color=i==1 ? 0xF800 : 0x07E0;
          std::memcpy(block.data()+8,&color,2);
          auto desc=t.resource->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes=0;
          device->GetCopyableFootprints(&desc,0,1,0,&fp,nullptr,nullptr,&bytes);
          auto buffer=Buffer(device,bytes,true);void* mapped=nullptr;D3D12_RANGE none{};
          Check(buffer->Map(0,&none,&mapped),"BC3 upload map");std::memcpy(mapped,block.data(),16);buffer->Unmap(0,nullptr);
          D3D12_TEXTURE_COPY_LOCATION from{},to{};
          from.pResource=buffer.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=fp;
          to.pResource=t.resource.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
          list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
          Transition(list.Get(),t.resource.Get(),D3D12_RESOURCE_STATE_COPY_DEST,GuestInputTexture::kReadState);
          uploads.push_back(buffer);
          continue;
        }
        Require(t.Prepare(device, DXGI_FORMAT_R8G8B8A8_TYPELESS, 1, 1, created),
                "post allocation");
        t.view_format = DXGI_FORMAT_R8G8B8A8_UNORM;
        t.host_swizzle = 0x60A;
        t.guest_fetch = frame->hdr_bloom.guest_fetch;
        if (i == 0)
          t.guest_fetch[0] &=
              ~((7u << 10) | (7u << 13)); // Observed wrap sampler.
        upload(
            t.resource.Get(), 4,
            [&](auto, auto, void *dest) {
              const auto &v = post_pixels[i];
              const uint8_t bgra[] = {v[2], v[1], v[0], v[3]};
              std::memcpy(dest, bgra, 4);
            },
            GuestInputTexture::kReadState);
      }
      frame->metadata.hdr_post_textures_captured = mask;
    }
    if (c.trace_light) {
      auto& reference = frame->fg_hudless;
      Require(reference.Prepare(device, DXGI_FORMAT_R8G8B8A8_TYPELESS,1120*c.scale,584*c.scale,created),
              "trace SDR reference allocation");
      reference.host_swizzle = 0x60A;
      frame->metadata.fg_hudless_captured = true;
      upload(reference.resource.Get(),4,[&](auto,auto,void* dest) {
        const uint8_t bgra[] = {32,64,128,255};
        std::memcpy(dest,bgra,4);
      },GuestInputTexture::kReadState);
    }
    SsxHDRConstants k;
    k.width = c.width;
    k.height = c.height;
    k.peak_nits = c.peak;
    k.ui_nits = c.ui;
    k.exposure_ev = c.ev;
    k.highlight_boost = c.boost;
    k.calibration = c.calibration;
    k.hdr_output = c.transport;
    Require(PlanSsxHDR(*frame, k, c.grading != 0, c.layers != 0),
            "native plan");
    retained->hdr_valid = false;
    Require(!PlanSsxHDR(*frame, k, c.grading != 0), "stale HDR accepted");
    retained->hdr_valid = true;
    ++retained->guest_frame;
    Require(!PlanSsxHDR(*frame, k, c.grading != 0),
            "mismatched frame accepted");
    --retained->guest_frame;
    if (c.exposure) {
      frame->textures[2].guest_fetch[0] ^= 1 << 10;
      Require(!PlanSsxHDR(*frame, k, c.grading != 0),
              "unknown exposure addressing accepted");
      frame->textures[2].guest_fetch[0] ^= 1 << 10;
    }
    if (c.grading) {
      auto &m = frame->metadata;
      m.grading_captured = false;
      Require(!PlanSsxHDR(*frame, k), "missing grade accepted");
      m.grading_captured = true;
      m.grading_duplicate = true;
      Require(!PlanSsxHDR(*frame, k), "duplicate grade accepted");
      m.grading_duplicate = false;
      m.fg_gamma_pwl = true;
      Require(!PlanSsxHDR(*frame, k), "unknown gamma accepted");
      m.fg_gamma_pwl = false;
      frame->grading_lut.guest_fetch[3] ^= 1 << 13;
      Require(!PlanSsxHDR(*frame, k), "unknown LUT exponent accepted");
      frame->grading_lut.guest_fetch[3] ^= 1 << 13;
    }
    Require(PlanSsxHDR(*frame, k, c.grading != 0, c.layers != 0),
            "restored plan");
    if (c.layers) {
      frame->metadata.hdr_layers_failed = true;
      Require(!PlanSsxHDR(*frame, k, true, true), "unsupported layer accepted");
      frame->metadata.hdr_layers_failed = false;
      frame->metadata.hdr_bloom_captured = false;
      Require(!PlanSsxHDR(*frame, k, true, true), "missing bloom accepted");
      frame->metadata.hdr_bloom_captured = true;
      frame->hdr_bloom.guest_fetch[3] ^= 1 << 13;
      Require(!PlanSsxHDR(*frame, k, true, true), "biased bloom accepted");
      frame->hdr_bloom.guest_fetch[3] ^= 1 << 13;
      Require(PlanSsxHDR(*frame, k, true, true), "restored layer plan");
      const auto post_shader = frame->metadata.hdr_post_shader;
      frame->metadata.hdr_post_shader = 0;
      Require(!PlanSsxHDR(*frame, k, true, true),
              "unknown post shader accepted");
      frame->metadata.hdr_post_shader = post_shader;
      if (c.post_mode & 1) {
        frame->metadata.hdr_post_textures_captured ^= 1;
        Require(!PlanSsxHDR(*frame, k, true, true),
                "missing post input accepted");
        frame->metadata.hdr_post_textures_captured ^= 1;
        frame->hdr_post_textures[0].guest_fetch[4] |= 1 << 12;
        Require(!PlanSsxHDR(*frame, k, true, true), "post LOD bias accepted");
        frame->hdr_post_textures[0].guest_fetch[4] &= ~(1u << 12);
      }
      if (c.post_mode & 4) {
        const auto radius_end = frame->metadata.hdr_post_constants[17];
        frame->metadata.hdr_post_constants[17] =
            frame->metadata.hdr_post_constants[16];
        Require(!PlanSsxHDR(*frame, k, true, true),
                "degenerate radial effect accepted");
        frame->metadata.hdr_post_constants[17] = radius_end;
      }
      Require(PlanSsxHDR(*frame, k, true, true), "restored post plan");
    }
    if (c.grading) {
      const auto saved = k.grade;
      k.grade[0] += .125f;
      Require(
          !pass.Record(*provider, list.Get(), index, overlay.Get(), frame, k),
          "tampered grade constants accepted");
      k.grade = saved;
    }
    SsxSceneColor decoder;
    ID3D12Resource* direct_scene = nullptr;
    if (c.direct_scene) {
      Require(!c.color, "direct decoder reference uses neutral input stripes");
      frame->fg_temporal.reset();
      Require(!PlanSsxHDR(*frame,k,true,true),"missing retained HDR accepted without decode");
      direct_scene = decoder.Record(*provider,list.Get(),frame,submission+1,submission).color;
      Require(direct_scene && PlanSsxHDR(*frame,k,true,true,nullptr,direct_scene),
              "independent HDR must accept matching decoded color without DLAA/FG");
      Require(!frame->fg_temporal && !frame->frame_token && !frame->metadata.jitter_enabled,
              "independent HDR must not create temporal inputs");
    }
    k.native_scene = c.native;
    ComPtr<ID3D12Resource> hudless;
    if (CanGenerateSsxHDRFrame(k)) {
      auto desc=overlay->GetDesc();desc.Format=DXGI_FORMAT_R10G10B10A2_UNORM;
      desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
      Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,
          GuestInputTexture::kReadState,nullptr,IID_PPV_ARGS(&hudless)),"HDR HUDless target");
      auto bad=k;bad.calibration=1;
      Require(!pass.Record(*provider,list.Get(),index,overlay.Get(),frame,bad,hudless.Get()),"FG rejects calibration");
      bad=k;bad.hdr_output=0;
      Require(!pass.Record(*provider,list.Get(),index,overlay.Get(),frame,bad,hudless.Get()),"FG rejects SDR fallback");
      Require(!pass.Record(*provider,list.Get(),index,overlay.Get(),frame,k,overlay.Get()),"FG rejects SDR color target");
    }
    SsxFGColor pool;
    std::vector<std::shared_ptr<SsxFGColorFrame>> held;
    ComPtr<ID3D12Fence> consumer;
    if (hudless && c.width==3840 && c.height==2160 && !c.direct_scene) {
      frame->metadata.output_width=1280*c.scale;frame->metadata.output_height=720*c.scale;
      Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&consumer)),"HDR FG consumer fence");
      for (int i=0;i<6;++i) {
        auto slot=pool.AcquireHDR(*provider,frame,c.width,c.height,submission+1,submission);
        Require(bool(slot),"HDR pool allocation");
        slot->fg_fence=consumer;slot->fg_fence_value=1;
        held.push_back(slot);
      }
      Require(!pool.AcquireHDR(*provider,frame,c.width,c.height,submission+1,submission),"HDR pool bounded");
      hudless=held[0]->color;
    }
    auto *output =
        pass.Record(*provider, list.Get(), index, overlay.Get(), frame, k, hudless.Get(),
                    c.trace_light, direct_scene);
    Require(output != nullptr, "record");
    auto out_desc = output->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
    UINT64 bytes = 0;
    device->GetCopyableFootprints(&out_desc, 0, 1, 0, &fp, nullptr, nullptr,
                                  &bytes);
    auto readback = Buffer(device, bytes, false);
    D3D12_TEXTURE_COPY_LOCATION from{}, to{};
    from.pResource = output;
    from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    to.pResource = readback.Get();
    to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    to.PlacedFootprint = fp;
    list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    ComPtr<ID3D12Resource> reference_readback, plain_readback;
    if (reference.pipeline) {
      reference_readback = Buffer(device, bytes, false);
      // Record leaves these compute bindings intact. Replay the archived
      // shader before another pass changes them; the old shader ignores u1.
      Transition(list.Get(), overlay.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                 D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
      Transition(list.Get(), output, D3D12_RESOURCE_STATE_COPY_SOURCE,
                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      list->SetPipelineState(reference.pipeline.Get());
      list->Dispatch((c.width + 7) / 8, (c.height + 7) / 8, 1);
      Transition(list.Get(), output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                 D3D12_RESOURCE_STATE_COPY_SOURCE);
      Transition(list.Get(), overlay.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                 D3D12_RESOURCE_STATE_RENDER_TARGET);
      to.pResource = reference_readback.Get();
      list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    }
    if (hudless) {
      Require(plain_pass.Prepare(*provider, index, c.width, c.height), "plain HDR prepare");
      auto *plain = plain_pass.Record(*provider, list.Get(), index, overlay.Get(), frame, k,
                                      nullptr, false, direct_scene);
      Require(plain != nullptr, "plain HDR record");
      plain_readback = Buffer(device, bytes, false);
      from.pResource = plain;
      to.pResource = plain_readback.Get();
      list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    }
    ComPtr<ID3D12Resource> hudless_readback;
    if (hudless) {
      hudless_readback=Buffer(device,bytes,false);
      from.pResource=hudless.Get();to.pResource=hudless_readback.Get();
      Transition(list.Get(),hudless.Get(),GuestInputTexture::kReadState,D3D12_RESOURCE_STATE_COPY_SOURCE);
      list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
      Transition(list.Get(),hudless.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,GuestInputTexture::kReadState);
    }
    Finish(list.Get());
    if (c.trace_light) {
      SsxHDRLightTrace trace;
      Require(pass.ReadCompletedLightTrace(index,trace),"completed light trace missing");
      Require(!pass.ReadCompletedLightTrace(index,trace),"light trace consumed twice");
      Require(!pass.ReadCompletedLightTrace(99,trace),"invalid trace slot accepted");
      Require(trace.guest_frame==frame->metadata.guest_frame &&
              trace.post_shader==frame->metadata.hdr_post_shader,"trace frame identity");
      float native_peak=0;
      for (const auto& sample : trace.samples) {
        for (float v : sample) Require(std::isfinite(v),"nonfinite trace sample");
        Require(sample[23]==1,"SDR reference validity marker missing");
        Require(sample[20]>sample[21] && sample[21]>sample[22],"SDR reference channel order");
        Require(sample[16]>=0 && sample[16]<=c.peak,"HDR trace outside nit range");
        native_peak=std::max(native_peak,sample[0]);
      }
      Require(native_peak>30,"trace did not retain native scene highlights");
    }
    if (consumer) {
      held.clear();
      pool.Collect(submission);
      Require(!pool.AcquireHDR(*provider,frame,c.width,c.height,submission+1,submission),"HDR consumer fence blocks reuse");
      Check(consumer->Signal(1),"complete HDR synthetic consumer");
      pool.Collect(submission);
      Require(frame.use_count()==1,"HDR pool releases producer packet");
      auto reused=pool.AcquireHDR(*provider,frame,c.width,c.height,submission+1,submission);
      Require(bool(reused),"HDR pool reuse after both fences");
      Require(bool(pool.AcquireHDR(*provider,frame,2560,1440,submission+1,submission)),
              "HDR permits proportional output scaling");
      Require(!pool.AcquireHDR(*provider,frame,2560,1080,submission+1,submission),
              "HDR FG rejects a different aspect mapping");
    }
    void *hudless_mapped = nullptr;
    void *mapped = nullptr;
    D3D12_RANGE range{0, SIZE_T(bytes)};
    Check(readback->Map(0, &range, &mapped), "readback");
    if (c.grading == 7 && c.post_mode == 0) {
      uint32_t pixel;
      std::memcpy(&pixel,static_cast<uint8_t*>(mapped)+(c.height/2)*fp.Footprint.RowPitch+
                  (3*c.width/16)*4,4);
      for (uint32_t channel=0;channel<3;++channel) {
        const auto code=(pixel>>(channel*10))&1023;
        if (code <= uint32_t(std::round(SsxPQEncode(20)*1023)))
          std::printf("Authored transition color erased: channel=%u PQ=%u nits=%.3f\n",
                      channel,code,SsxPQDecode(double(code)/1023));
        Require(code>uint32_t(std::round(SsxPQEncode(20)*1023)),
                "live LUT black subtraction erased authored transition colors");
      }
    }
    if (c.layers == 13 || c.layers == 14) {
      // Independent output bound: a black scene plus SDR effects (or an
      // opaque SDR layer over any scene) contains no native HDR highlight.
      // The 200-nit bound must be independent of the selected display peak.
      const auto limit = uint32_t(std::round(SsxPQEncode(200) * 1023)) + 1;
      const auto stripe_count = c.layers == 14 ? 8u : 1u;
      for (uint32_t stripe = 0; stripe < stripe_count; ++stripe) {
        uint32_t pixel;
        const auto x = (2 * stripe + 1) * c.width / 16;
        std::memcpy(&pixel, static_cast<uint8_t *>(mapped) +
                    (c.height / 2) * fp.Footprint.RowPitch + x * 4, 4);
        for (uint32_t channel = 0; channel < 3; ++channel) {
          const auto code = (pixel >> (channel * 10)) & 1023;
          if (code > limit)
            std::printf("SDR world overflow: %s stripe=%u channel=%u code=%u "
                        "limit=%u decoded_nits=%.2f\n", c.name, stripe, channel,
                        code, limit, SsxPQDecode(double(code) / 1023));
          Require(code <= limit, "SDR world overflow became an HDR highlight");
        }
      }
    }
    const auto compare = [&](ID3D12Resource *other, uint64_t &count, int &maximum,
                              const char *label, int tolerance) {
      if (!other) return;
      void *other_data = nullptr;
      Check(other->Map(0, &range, &other_data), "comparison readback");
      uint64_t mismatches = 0;
      for (uint32_t y = 0; y < c.height; ++y) {
        const auto *a = reinterpret_cast<const uint32_t *>(
            static_cast<const uint8_t *>(mapped) + y * fp.Footprint.RowPitch);
        const auto *b = reinterpret_cast<const uint32_t *>(
            static_cast<const uint8_t *>(other_data) + y * fp.Footprint.RowPitch);
        for (uint32_t x = 0; x < c.width; ++x)
          for (uint32_t ch = 0; ch < 3; ++ch) {
            const int error = std::abs(int((a[x] >> (ch * 10)) & 1023) -
                                       int((b[x] >> (ch * 10)) & 1023));
            maximum = std::max(maximum, error);
            mismatches += error > tolerance;
            ++count;
          }
      }
      other->Unmap(0, nullptr);
      if (mismatches)
        std::printf("%s mismatch case=%s channels=%llu maximum=%d\n", label, c.name,
                    static_cast<unsigned long long>(mismatches), maximum);
      Require(mismatches == 0, label);
    };
    compare(plain_readback.Get(), plain_channel_checks, plain_max_error,
            "HUDless output changed final HDR color", 0);
    compare(reference_readback.Get(), reference_channel_checks, reference_max_error,
            "archived shader changed final HDR color", 1);
    if(hudless_readback) Check(hudless_readback->Map(0,&range,&hudless_mapped),"HUDless readback");
    const auto linear = [](double code) {
      return code <= .04045 ? code / 12.92
                            : std::pow((code + .055) / 1.055, 2.4);
    };
    std::array<uint32_t, 8> highlight_codes{};
    for (uint32_t stripe = 0; stripe < 8; ++stripe) {
      const auto x = (2 * stripe + 1) * c.width / 16, y = c.height / 2;
      const double uvx = (x + .5) / c.width, uvy = (y + .5) / c.height;
      double exposure = 1;
      if (c.exposure && !c.shadows && !c.grading) {
        const double pos = uvx * 3 - .5;
        const auto texel = [](int i) {
          return i <= 0 ? 0.0 : i == 1 ? 32768.0 / 65535 : 1.0;
        };
        const double raw = c.linear ? std::lerp(texel(int(std::floor(pos))),
                                                texel(int(std::floor(pos)) + 1),
                                                pos - std::floor(pos))
                                    : texel(int(uvx * 3));
        exposure = std::exp2(-2 * raw);
      }
      const double stored = c.shadows ? shadow_values[stripe] : values[stripe];
      const double exposed = (c.direct_scene ? stored * stored : stored) * exposure * std::exp2(c.ev);
      std::array<double, 3> channel_nits{};
      double hdr_excess = 0;
      auto original_grade = [&](std::array<double, 3> rgb,
                                bool effects = true) {
        std::array<double, 3> light{}, position{}, lookup{}, result{};
        double excess = std::max(exposed / k.curve_efs_white[3] - 1., 0.);
        for (int i = 0; i < 3; ++i) {
          double value = rgb[i];
          if (c.exposure && value != 0) {
            const auto &q = frame->metadata.tone_map_constants;
            value = q[32] * ((value * (q[8] * value + q[16]) + q[20]) /
                                 (value * (q[8] * value + q[12]) + q[24]) -
                             q[28]);
          }
          double code = std::clamp(std::sqrt(std::abs(value)), 0., 1.);
          if (c.layers && effects)
            code = std::clamp(world[i] + code * world_t[i], 0., 1.);
          light[i] = code * code;
          if (c.layers && effects) {
            if (c.post_mode & 1)
              light[i] = std::lerp(light[i], post_pixels[0][i] / 255.,
                                   post_pixels[0][3] / 255.);
            const double b = glare ? (c.bloom_code / (1 << i)) / 255. : 0;
            light[i] += 7 * b * b;
          }
        }
        double pre_peak = 1;
        if (c.layers && effects) {
          pre_peak = std::max(
              {1., std::abs(light[0]), std::abs(light[1]), std::abs(light[2])});
          excess *=
              std::max({world_t[0],world_t[1]*(c.color?.5:1.),world_t[2]*(c.color?.25:1.)}) * ((c.post_mode & 1) ? 1 - post_pixels[0][3] / 255. : 1);
          excess += pre_peak - 1;
        }
        for (int i = 0; i < 3; ++i)
          position[i] =
              std::clamp(std::sqrt(std::abs(light[i])), 0., 1.) * 31;
        for (int channel = 0; channel < 3; ++channel) {
          double value = 0;
          if (c.point_lut)
            value = lut_node(int(std::floor(position[0] + .5)),
                             int(std::floor(position[1] + .5)),
                             int(std::floor(position[2] + .5)), channel);
          else
            for (int z = 0; z < 2; ++z)
              for (int y = 0; y < 2; ++y)
                for (int x = 0; x < 2; ++x) {
                  const int ix = int(std::floor(position[0])),
                            iy = int(std::floor(position[1])),
                            iz = int(std::floor(position[2]));
                  const double fx = position[0] - ix, fy = position[1] - iy,
                               fz = position[2] - iz;
                  value += lut_node(ix + x, iy + y, iz + z, channel) *
                           (x ? fx : 1 - fx) * (y ? fy : 1 - fy) *
                           (z ? fz : 1 - fz);
                }
          lookup[channel] = value * value;
        }
        const auto &p = frame->metadata.hdr_post_constants;
        if (effects && (c.post_mode & 2)) {
          const double radius =
              std::sqrt(std::abs((uvx + p[24]) * (uvx + p[24]) +
                                 (uvy + p[24]) * (uvy + p[24]) + p[25]));
          const double v = std::clamp((radius - p[4]) / (p[5] - p[4]), 0., 1.),
                       a = v * v * (p[26] - v * p[27]);
          for (int i = 0; i < 3; ++i)
            lookup[i] = std::lerp(lookup[i], double(p[i]), a);
          excess *= std::clamp(1 - a, 0., 1.);
        }
        if (effects && (c.post_mode & 4)) {
          const double qx = uvx * p[20] - p[22], qy = uvy * p[21] - p[23];
          const double radius = std::sqrt(std::abs(qx * qx + qy * qy + p[25])),
                       v = std::clamp((radius - p[16]) / (p[17] - p[16]), 0.,
                                      1.);
          const double a = std::clamp(post_pixels[1][3] / 255. * p[11] + p[26],
                                      0., 1.),
                       b = std::clamp(post_pixels[2][3] / 255. * p[15] + p[26],
                                      0., 1.);
          const double radial =
              std::max(v * v * (p[27] - v * p[24]), double(p[18]));
          for (int i = 0; i < 3; ++i)
            lookup[i] = std::lerp(
                std::lerp(lookup[i], post_pixels[2][i] / 255. * p[12 + i], b) *
                    radial,
                post_pixels[1][i] / 255. * p[8 + i], a);
          excess *= (1 - a) * (1 - b) * std::max(radial, 0.);
        }
        for (int i = 0; i < 3; ++i)
          lookup[i] = std::max(
              std::sqrt(std::abs(lookup[i])) * k.grade[0] + k.grade[1], 0.);
        const double post_peak =
            (c.layers && effects)
                ? std::max({1., lookup[0], lookup[1], lookup[2]})
                : 1;
        if (c.layers && effects)
          excess += post_peak * post_peak - 1;
        for (int i = 0; i < 3; ++i) {
          const double gp = std::clamp(lookup[i], 0., 1.) * 255;
          const int low = int(std::floor(gp)), high = std::min(low + 1, 255),
                    shift = 20 - i * 10;
          const auto &ramp = frame->metadata.fg_gamma_ramp;
          result[i] =
              linear(std::lerp(double((ramp[low] >> shift) & 1023),
                               double((ramp[high] >> shift) & 1023), gp - low) /
                     1023);
        }
        if (effects)
          hdr_excess = excess;
        return result;
      };
      if (c.grading) {
        const double white = k.curve_efs_white[3],
                     bounded = std::min(exposed, white);
        auto graded = original_grade({bounded, c.color ? bounded * .5 : bounded,
                                      c.color ? bounded * .25 : bounded});
        std::array<double,3> black{};
        for (int i=0;i<3;++i)
          black[i]=linear(double((frame->metadata.fg_gamma_ramp[0]>>(20-i*10))&1023)/1023);
        for (int i = 0; i < 3; ++i)
          channel_nits[i] =
              std::clamp((graded[i] - black[i]) / (1 - black[i]), 0., 1.) * 200;
        const double base =
            *std::max_element(channel_nits.begin(), channel_nits.end());
        if (hdr_excess > 0 && base > 0) {
          const double excess = hdr_excess, span = c.peak - base;
          double t = excess * excess / (1 + excess) * 200 / span;
          t += (c.boost - 1) * t * t / (1 + t);
          const double mapped = base + span * (-std::expm1(-t));
          for (auto &v : channel_nits)
            v *= mapped / base;
        }
      } else {
        double toe = exposed;
        if (toe < .25) {
          const double t = toe / .25;
          toe *= 1 - (1 - k.shadow_slope) * (1 - t) * (1 - t);
        }
        double nits = toe * 200;
        if (nits > 200) {
          double t = (nits - 200) / (c.peak - 200);
          t += (c.boost - 1) * t * t / (1 + t);
          nits = 200 + (c.peak - 200) * (-std::expm1(-t));
        }
        channel_nits.fill(nits);
      }
      if(hudless_mapped) {
        const auto &v=channel_nits;
        const double rec[]={.62740390*v[0]+.32928304*v[1]+.04331307*v[2],
                            .06909729*v[0]+.91954040*v[1]+.01136232*v[2],
                            .01639144*v[0]+.08801331*v[1]+.89559525*v[2]};
        uint32_t pixel;std::memcpy(&pixel,static_cast<uint8_t*>(hudless_mapped)+y*fp.Footprint.RowPitch+x*4,4);
        for(int i=0;i<3;++i) {
          int expected=int(std::round(SsxPQEncode(rec[i])*1023));
          Require(std::abs(int((pixel>>(i*10))&1023)-expected)<=1,"HDR HUDless color/exposure/post mismatch");
          ++hudless_checks;
        }
      }
      if(c.layers && (hud[0]!=0 || hud[1]!=0 || hud[2]!=0 || hud_t!=std::array<double,3>{1,1,1})) {
        const double peak=*std::max_element(channel_nits.begin(),channel_nits.end());
        for(int i=0;i<3;++i) {
          const double base=channel_nits[i]*std::min(1.,c.ui/std::max(peak,1e-20));
          const int shift=20-i*10;
          const auto& ramp=frame->metadata.fg_gamma_ramp;
          const auto ramp_value=[&](int j){return double((ramp[j]>>shift)&1023)/1023;};
          const double black=linear(ramp_value(0));
          const double reference=base/c.ui*(1-black)+black;
          const double display=reference<=.0031308 ? 12.92*reference : 1.055*std::pow(reference,1/2.4)-.055;
          int upper=0;while(upper<255 && ramp_value(upper)<display)++upper;
          const int lower=std::max(upper-1,0);
          const double frac=std::clamp((display-ramp_value(lower))/std::max(ramp_value(upper)-ramp_value(lower),1e-20),0.,1.);
          const double guest=(lower+frac*(upper-lower))/255;
          const double position=std::clamp(hud[i]+guest*hud_t[i],0.,1.)*255;
          const int lo=int(position),hi=std::min(lo+1,255);
          const double mixed=linear(std::lerp(ramp_value(lo),ramp_value(hi),position-lo));
          channel_nits[i]=std::clamp((mixed-black)/(1-black),0.,1.)*c.ui+(channel_nits[i]-base)*hud_t[i];
        }
        const double result_peak=*std::max_element(channel_nits.begin(),channel_nits.end());
        if(result_peak>c.peak)for(auto& n:channel_nits)n*=c.peak/result_peak;
      }
      const double alpha = (c.overlay >> 24) / 255.0;
      const double code = (c.overlay & 255) / 255.0;
      for (auto &nits : channel_nits)
        nits = c.native ? nits * (1 - alpha) +
                              linear(alpha ? std::min(code / alpha, 1.0) : 0) *
                                  c.ui * alpha
                        : linear(code) * c.ui;
      if (c.calibration) {
        double nits = 0;
        if (std::abs(uvx - .5) < .1 && std::abs(uvy - .35) < .05)
          nits = (uvx < .5 ? .9 : 1) * c.peak;
        if (std::abs(uvx - .5) < .1 && std::abs(uvy - .5) < .025)
          nits = 200;
        if (std::abs(uvx - .5) < .1 && std::abs(uvy - .6) < .025)
          nits = c.ui;
        channel_nits.fill(nits);
      }
      const auto &v = channel_nits;
      std::array<double, 3> rec2020{
          .62740390 * v[0] + .32928304 * v[1] + .04331307 * v[2],
          .06909729 * v[0] + .91954040 * v[1] + .01136232 * v[2],
          .01639144 * v[0] + .08801331 * v[1] + .89559525 * v[2]};
      uint32_t pixel;
      std::memcpy(&pixel,
                  static_cast<uint8_t *>(mapped) + y * fp.Footprint.RowPitch +
                      x * 4,
                  4);
      highlight_codes[stripe] = pixel & 1023;
      for (unsigned channel = 0; channel < 3; ++channel) {
        const double nits = rec2020[channel];
        const int expected =
            int(std::round((c.transport ? SsxPQEncode(nits) : code) * 1023));
        const int error =
            std::abs(int((pixel >> (channel * 10)) & 1023) - expected);
        max_error = std::max(max_error, error);
        ++checks;
        if (error > 1) {
          std::printf("mismatch %s stripe=%u nits=%.4f got=%u expected=%d\n",
                      c.name, stripe, nits, (pixel >> (channel * 10)) & 1023,
                      expected);
          Require(false, "PQ/color mismatch");
        }
      }
    }
    if (c.native && !c.overlay && !c.exposure && !c.calibration &&
        c.transport && c.ev == 0 && !c.grading) {
      Require(highlight_codes[2] < highlight_codes[3] &&
                  highlight_codes[3] < highlight_codes[4] &&
                  highlight_codes[4] < highlight_codes[5] &&
                  highlight_codes[5] < highlight_codes[6],
              "native values above 1 lost highlight separation");
      Require(std::abs(int(highlight_codes[2]) -
                       int(std::round(SsxPQEncode(200) * 1023))) <= 1,
              "changing peak changed diffuse white");
    }
    if (c.native && !c.overlay && !c.calibration && c.transport &&
        c.layers <= 1 && c.grading != 3 && c.grading != 7)
      Require(highlight_codes[0] == 0, "black must be exact 10-bit code zero");
    if ((c.grading == 3 || c.grading == 7) && c.layers <= 1)
      Require(highlight_codes[0] > 0,"authored grade lift/flash was removed");
    if (c.shadows) {
      for (size_t i = 1; i < highlight_codes.size(); ++i)
        Require(highlight_codes[i] > highlight_codes[i - 1],
                "near-black detail crushed");
      if (!c.grading)
        Require(highlight_codes[1] <
                    uint32_t(std::round(SsxPQEncode(200. / 256) * 1023)),
                "guest shadow toe missing");
    }
    if (c.grading && !c.layers && !c.shadows && !c.overlay && !c.calibration) {
      for (size_t i = 3; i < highlight_codes.size(); ++i)
        Require(highlight_codes[i] > highlight_codes[i - 1],
                "graded HDR highlight detail lost");
    }
    if (c.layers == 5 && c.bloom_code > 128)
      Require(highlight_codes[2] < highlight_codes[4] &&
                  highlight_codes[4] < highlight_codes[6],
              "strong bloom clipped distinct native highlights");
    if (c.layers == 13)
      Require(highlight_codes[4] < highlight_codes[5] &&
                  highlight_codes[5] < highlight_codes[6],
              "world clamp erased separate native scene highlight detail");
    // The calibration's peak patch is checked separately at its center.
    if (c.calibration) {
      uint32_t pixel;
      const auto x = uint32_t(c.width * .55), y = uint32_t(c.height * .35);
      std::memcpy(&pixel,
                  static_cast<uint8_t *>(mapped) + y * fp.Footprint.RowPitch +
                      x * 4,
                  4);
      Require(std::abs(int(pixel & 1023) -
                       int(std::round(SsxPQEncode(c.peak) * 1023))) <= 1,
              "peak calibration nits");
    }
    readback->Unmap(0, nullptr);
    if(hudless_readback)hudless_readback->Unmap(0,nullptr);
    std::printf("PASS %s peak=%.0f output=%ux%u\n", c.name, c.peak, c.width,
                c.height);
  }
};
} // namespace
int main(int argc, char **argv) {
  rex::InitLogging("hdr-probe.log");
  int status = 0;
  try {
    rex::cvar::SetFlagByName("d3d12_debug", "true");
    rex::cvar::SetFlagByName("d3d12_streamline", "false");
    Harness h;
    if (argc == 3 && std::strcmp(argv[1], "--reference-shader") == 0)
      h.reference.Load(*h.provider, argv[2]);
    else if (argc == 2 && std::strcmp(argv[1], "--independent-features") == 0)
      h.independent_only = true;
    else if (argc != 1)
      throw std::runtime_error("usage: ssx_hdr_probe [--reference-shader archived.cso | --independent-features]");
    ComPtr<ID3D12InfoQueue> messages;
    Check(h.provider->GetDevice()->QueryInterface(IID_PPV_ARGS(&messages)),
          "debug layer");
    messages->ClearStoredMessages();
    // Fixed 1,000-nit checks only: no DLSS/FG execution or multiplier sweep.
    for (uint32_t scale : {1u, 2u, 3u}) {
      h.Run({.name="independent-retained-scale",.width=3840,.height=2160,
          .exposure=true,.grading=2,.layers=9,.scale=scale,.independent=true});
      h.Run({.name="independent-decoded-scale",.width=1280*scale,.height=720*scale,
          .exposure=true,.grading=2,.layers=9,.scale=scale,.independent=true,.direct_scene=true});
    }
    h.Run({.name="independent-tricky-decode",.width=3840,.height=2160,
        .exposure=true,.grading=7,.layers=9,.post_mode=4,.trace_light=true,
        .independent=true,.direct_scene=true});
    for (float peak : {1000.f,3000.f})
      for (uint32_t mode : {0u,1u,2u,4u,5u})
        h.Run({.name="authored-tricky-grade-transition",.peak=peak,
               .exposure=true,.grading=7,.color=true,.layers=1,.post_mode=mode});
    for (float peak : {1000.f, 3000.f}) {
      h.Run({.name = "sdr-world-overflow-native-highlights",
             .peak = peak, .exposure = true, .grading = 2,
             .color = true, .layers = 13});
      h.Run({.name = "opaque-sdr-world-overflow",
             .peak = peak, .exposure = true, .grading = 2,
             .color = true, .layers = 14});
    }
    h.Run({.name = "native-1000"});
    h.Run({.name = "native-3000", .peak = 3000});
    h.Run({.name = "highlight-boost-1000", .boost = 1.25});
    h.Run({.name = "highlight-boost-3000", .peak = 3000, .boost = 1.25});
    h.Run({.name = "zero-black-shadow-detail-1000",
           .exposure = true,
           .shadows = true});
    h.Run({.name = "zero-black-shadow-detail-3000",
           .peak = 3000,
           .exposure = true,
           .shadows = true});
    h.Run({.name = "spatial-exposure-point", .exposure = true});
    h.Run(
        {.name = "spatial-exposure-linear", .exposure = true, .linear = true});
    h.Run({.name = "opaque-ui", .overlay = 0xffffffff, .ui = 160});
    h.Run({.name = "translucent-ui", .overlay = 0x80808080});
    h.Run({.name = "sdr-frontend", .native = false, .overlay = 0xff808080});
    h.Run({.name = "sdr-output-fallback",
           .native = false,
           .transport = false,
           .overlay = 0xff808080});
    h.Run({.name = "ev-minus-one", .ev = -1});
    h.Run({.name = "calibration-1000", .calibration = true});
    h.Run({.name = "calibration-3000-offscreen",
           .peak = 3000,
           .calibration = true});
    h.Run({.name = "4k-output", .width = 3840, .height = 2160});
    h.Run({.name = "resize-back"});
    h.Run({.name = "original-grade-identity",
           .exposure = true,
           .shadows = true,
           .grading = 1});
    h.Run({.name = "original-grade-color",
           .exposure = true,
           .grading = 2,
           .color = true});
    h.Run({.name = "original-grade-color-3000",
           .peak = 3000,
           .exposure = true,
           .grading = 2,
           .color = true});
    h.Run({.name = "original-grade-boost",
           .exposure = true,
           .boost = 1.25,
           .grading = 2,
           .color = true});
    h.Run({.name = "authored-grade-lift-kept",
           .exposure = true,
           .shadows = true,
           .grading = 3});
    h.Run({.name = "grade-track-change",
           .exposure = true,
           .grading = 4,
           .color = true});
    h.Run({.name = "grade-point-volume",
           .exposure = true,
           .grading = 2,
           .color = true,
           .point_lut = true});
    h.Run({.name = "grade-ui-independent",
           .overlay = 0xffffffff,
           .ui = 160,
           .grading = 2,
           .color = true});
    h.Run({.name = "grade-current-display-gamma",
           .exposure = true,
           .grading = 5,
           .color = true});
    h.Run({.name = "grade-zero-black-3000",
           .peak = 3000,
           .exposure = true,
           .shadows = true,
           .grading = 6});
    const char *names[] = {"",
                           "layers-transparent",
                           "world-alpha",
                           "world-opaque",
                           "world-additive",
                           "original-bloom",
                           "hud-alpha",
                           "hud-opaque",
                           "hud-additive",
                           "all-layers"};
    for (uint32_t layer = 1; layer <= 9; ++layer)
      h.Run({.name = names[layer],
             .exposure = true,
             .grading = 2,
             .color = true,
             .layers = layer});
    h.Run({.name = "all-layers-3000",
           .peak = 3000,
           .exposure = true,
           .ui = 160,
           .grading = 2,
           .color = true,
           .layers = 9});
    h.Run({.name = "layers-zero-black",
           .exposure = true,
           .shadows = true,
           .grading = 6,
           .layers = 1});
    h.Run({.name = "empty-hud-lifted-gamma",
           .exposure = true,
           .shadows = true,
           .grading = 6,
           .layers = 1});
    h.Run({.name = "opaque-hud-lifted-gamma",
           .exposure = true,
           .grading = 6,
           .layers = 7});
    for (uint32_t mode : {1u, 2u, 4u, 5u})
      for (float strength : {1.f, 4.f})
        h.Run({.name = "post-variant",
               .exposure = true,
               .grading = 2,
               .color = true,
               .layers = 9,
               .post_mode = mode,
               .post_strength = strength});
    h.Run({.name = "strong-bloom-1000",
           .exposure = true,
           .grading = 2,
           .color = true,
           .layers = 5,
           .bloom_code = 224});
    h.Run({.name = "strong-bloom-3000",
           .peak = 3000,
           .exposure = true,
           .grading = 2,
           .color = true,
           .layers = 5,
           .bloom_code = 224});
    h.Run({.name = "strong-post-3000",
           .peak = 3000,
           .exposure = true,
           .grading = 2,
           .color = true,
           .layers = 9,
           .post_mode = 5,
           .post_strength = 8,
           .bloom_code = 224});
    h.Run({.name="fg-hdr-4k-pool",.width=3840,.height=2160,.exposure=true,.overlay=0x80404040,.grading=2,.color=true,.layers=9});
    h.Run({.name="tricky-bc3-post",.exposure=true,.grading=2,.color=true,.layers=9,.post_mode=4,.post_strength=2,.compressed_post=true});
    h.Run({.name="tricky-bc3-pre-post",.peak=3000,.exposure=true,.grading=2,.color=true,.layers=9,.post_mode=5,.post_strength=2,.compressed_post=true});
    h.Run({.name="colored-hud-multiply",.exposure=true,.grading=2,.color=true,.layers=10});
    h.Run({.name="white-hud-multiply-identity",.exposure=true,.grading=2,.color=true,.layers=11});
    h.Run({.name="colored-world-multiply",.exposure=true,.grading=2,.color=true,.layers=12});
    h.Run({.name="hud-authored-gamma",.exposure=true,.grading=5,.color=true,.layers=6});
    h.Run({.name="hud-multiply-lifted-gamma",.exposure=true,.grading=6,.color=true,.layers=10});
    for (uint32_t mode : {0u,1u,4u,5u})
      h.Run({.name="hdr-light-trace-parity",.width=3840,.height=2160,
             .exposure=true,.grading=2,.color=true,.layers=9,.post_mode=mode,
             .compressed_post=true,.trace_light=true});
    unsigned errors = 0;
    for (UINT64 i = 0; i < messages->GetNumStoredMessages(); ++i) {
      SIZE_T bytes = 0;
      messages->GetMessage(i, nullptr, &bytes);
      std::vector<uint8_t> storage(bytes);
      auto *m = reinterpret_cast<D3D12_MESSAGE *>(storage.data());
      messages->GetMessage(i, m, &bytes);
      if (m->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) {
        ++errors;
        std::printf("D3D12 ERROR: %s\n", m->pDescription);
      }
    }
    std::printf("HDR HUDless channel checks=%u\n",h.hudless_checks);
    std::printf("HDR with/without HUDless channels=%llu maximum_error=%d; archived shader channels=%llu maximum_error=%d\n",
                static_cast<unsigned long long>(h.plain_channel_checks), h.plain_max_error,
                static_cast<unsigned long long>(h.reference_channel_checks), h.reference_max_error);
    std::printf(
        "HDR channel checks=%u maximum_10bit_error=%d D3D12_errors=%u\n",
        h.checks, h.max_error, errors);
    Require(!errors, "D3D12 validation failed");
  } catch (const std::exception &e) {
    std::printf("FAIL: %s\n", e.what());
    status = 1;
  }
  rex::ShutdownLogging();
  return status;
}
