#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/d3d12/d3d12_provider.h>
#include <rex/ui/d3d12/ssx_scene_color.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
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
  uint32_t width = 65, height = 33;
  uint16_t log_value = 32768;
  bool nonuniform = false, exposure = true;
  bool missing_exposure = false;
  int color_exp = 0, exposure_exp = 0;
  float key = .4f, log_scale = 23.083120346069336f;
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

  void AlignmentRules() {
    SsxFrameInputs packet;
    auto &f = packet.metadata;
    f.scene_width = 1120;
    f.scene_height = 584;
    f.scale_x = f.scale_y = 3;
    f.captured_mask = 3;
    f.camera_samples = 1;
    f.camera_viewport_valid = true;
    f.guest_viewport_xy = {560, -292, 560, 292};
    f.guest_viewport_z = {-1, 1};
    f.depth_before_lighting = true;
    f.depth_draw = 10;
    f.color_draw = 20;
    f.jitter_enabled = true;
    f.jitter_pixels = {-.375f, .125f};
    f.jittered_draws = 500;
    bool created;
    Require(packet.textures[0].Prepare(provider->GetDevice(),
                                       DXGI_FORMAT_R16G16B16A16_FLOAT, 3360,
                                       1752, created),
            "alignment color");
    Require(packet.textures[1].Prepare(provider->GetDevice(),
                                       DXGI_FORMAT_R32_FLOAT, 3360, 1752,
                                       created),
            "alignment depth");
    packet.textures[1].view_format = DXGI_FORMAT_R32_FLOAT;
    auto valid = [&] {
      return CheckSsxTemporalAlignment(packet) ==
             SsxAlignmentStatus::kCompatible;
    };
    Require(valid(), "aligned packet rejected");
    f.color_draw = 9;
    Require(!valid(), "late depth accepted");
    f.color_draw = 20;
    f.depth_before_lighting = false;
    Require(!valid(), "postprocess fallback accepted");
    f.depth_before_lighting = true;
    f.guest_viewport_xy[2] += .5f;
    Require(!valid(), "shifted origin accepted");
    f.guest_viewport_xy[2] -= .5f;
    packet.textures[1].width -= 1;
    Require(!valid(), "unequal extents accepted");
    packet.textures[1].width += 1;
    packet.textures[1].source_mip = 1;
    Require(!valid(), "depth mip accepted");
    packet.textures[1].source_mip = 0;
    packet.textures[1].guest_fetch[3] = 1 << 13;
    Require(!valid(), "scaled depth accepted");
    packet.textures[1].guest_fetch[3] = 0;
    f.jitter_unknown_before_color = 1;
    Require(!valid(), "incomplete jitter accepted");
    f.jitter_unknown_before_color = 0;
    f.jitter_enabled = false;
    Require(!valid(), "jitter mode mismatch accepted");
    f.jitter_pixels = {};
    Require(valid(), "zero-jitter fallback rejected");
    std::puts("PASS alignment compatibility and 8 rejection/fallback checks");
  }
  SsxSceneColor pass;
  void Run(const Case &c) {
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
    auto frame = std::make_shared<SsxFrameInputs>();
    auto &f = frame->metadata;
    f.guest_frame = ++submission;
    f.scene_width = c.width;
    f.scene_height = c.height;
    f.scale_x = f.scale_y = 1;
    f.captured_mask = c.exposure ? 5 : 1;
    f.exposure_branch = c.exposure;
    f.tone_map_constants[0] = c.key;
    f.tone_map_constants[4] = 8;
    f.tone_map_constants[36] = c.log_scale;
    std::vector<ComPtr<ID3D12Resource>> uploads;
    for (unsigned i = 0; i < (c.exposure ? 2u : 1u); ++i) {
      auto &tex = frame->textures[i ? 2 : 0];
      bool created;
      const unsigned width = i ? 3 : c.width, height = i ? 1 : c.height;
      Require(tex.Prepare(device,
                          i ? DXGI_FORMAT_R16_TYPELESS
                            : DXGI_FORMAT_R16G16B16A16_FLOAT,
                          width, height, created),
              "input allocation");
      tex.view_format =
          i ? DXGI_FORMAT_R16_UNORM : DXGI_FORMAT_R16G16B16A16_FLOAT;
      tex.host_swizzle = i ? 0xA00 : 0x688;
      tex.swizzled_signs = i ? 0 : 0x55;
      tex.source_mip = i ? 8 : 0;
      tex.guest_fetch[3] = (uint32_t(i ? c.exposure_exp : c.color_exp) & 63)
                           << 13;
      tex.guest_fetch[4] = 8 << 6;
      auto d = tex.resource->GetDesc();
      D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
      UINT64 bytes;
      device->GetCopyableFootprints(&d, 0, 1, 0, &fp, nullptr, nullptr, &bytes);
      auto upload = Buffer(device, bytes, true);
      void *data;
      D3D12_RANGE empty{0, 0};
      Check(upload->Map(0, &empty, &data), "upload map");
      for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x) {
          auto *target = static_cast<uint8_t *>(data) +
                         y * fp.Footprint.RowPitch + x * (i ? 2 : 8);
          if (i) {
            const uint16_t v =
                uint16_t(c.log_value + (c.nonuniform && x == 2 ? 1 : 0));
            std::memcpy(target, &v, 2);
          } else {
            const uint16_t pixel[4] = {0x3800, 0x4000, 0xc200, 0x3c00};
            std::memcpy(target, pixel, 8);
          }
        }
      upload->Unmap(0, nullptr);
      D3D12_TEXTURE_COPY_LOCATION from{}, to{};
      from.pResource = upload.Get();
      from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
      from.PlacedFootprint = fp;
      to.pResource = tex.resource.Get();
      to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
      Transition(list.Get(), tex.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                 GuestInputTexture::kReadState);
      uploads.push_back(upload);
    }
    SsxColorConstants k;
    Require(PlanSsxSceneColor(*frame, k), "valid plan rejected");
    // Unsupported representations must fail without touching command-list
    // state.
    auto &color = frame->textures[0];
    color.host_swizzle ^= 1;
    Require(!PlanSsxSceneColor(*frame, k), "unknown swizzle accepted");
    color.host_swizzle ^= 1;
    color.swizzled_signs = 0;
    Require(!PlanSsxSceneColor(*frame, k), "unknown signs accepted");
    color.swizzled_signs = 0x55;
    f.duplicate_mask = 1;
    Require(!PlanSsxSceneColor(*frame, k), "ambiguous scene accepted");
    f.duplicate_mask = 0;
    if (c.exposure) {
      f.tone_map_constants[4] = 8.5f;
      Require(PlanSsxSceneColor(*frame, k) && k.exposure_enabled == 2,
              "fractional LOD not isolated from color");
      f.tone_map_constants[4] = 8;
      frame->textures[2].guest_fetch[4] |= 1 << 12;
      Require(PlanSsxSceneColor(*frame, k) && k.exposure_enabled == 2,
              "biased LOD not isolated from color");
      frame->textures[2].guest_fetch[4] &= ~(1 << 12);
      f.tone_map_constants[0] = std::numeric_limits<float>::quiet_NaN();
      Require(PlanSsxSceneColor(*frame, k) && k.exposure_enabled == 2,
              "NaN key not isolated from color");
      f.tone_map_constants[0] = c.key;
    }
    if (c.missing_exposure) {
      uploads.push_back(
          frame->textures[2]
              .resource); // Retain the queued upload's destination to fence.
      frame->textures[2].resource.Reset();
      f.captured_mask &= ~4u;
      f.failed_mask |= 4;
    }
    const auto result = pass.Record(*provider, list.Get(), frame, submission,
                                    fence->GetCompletedValue());
    Require(result.dispatched && result.color && result.exposure,
            "dispatch failed");
    Require(result.reconstruction_color.automatic_exposure &&
                result.reconstruction_color.ExposureTag() == nullptr &&
                result.reconstruction_color.pre_exposure == 1,
            "guest exposure leaked into DLSS contract");
    const auto repeat = pass.Record(*provider, list.Get(), frame, submission,
                                    fence->GetCompletedValue());
    Require(repeat.repeated && !repeat.dispatched &&
                repeat.color == result.color,
            "repeat changed results");
    std::array<ComPtr<ID3D12Resource>, 2> rb;
    std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT, 2> fp;
    ID3D12Resource *resources[2] = {result.color, result.exposure};
    for (unsigned i = 0; i < 2; ++i) {
      auto d = resources[i]->GetDesc();
      UINT64 bytes;
      device->GetCopyableFootprints(&d, 0, 1, 0, &fp[i], nullptr, nullptr,
                                    &bytes);
      rb[i] = Buffer(device, bytes, false);
      D3D12_TEXTURE_COPY_LOCATION from{}, to{};
      from.pResource = resources[i];
      from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      to.pResource = rb[i].Get();
      to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
      to.PlacedFootprint = fp[i];
      Transition(list.Get(), resources[i], GuestInputTexture::kReadState,
                 D3D12_RESOURCE_STATE_COPY_SOURCE);
      list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
      Transition(list.Get(), resources[i], D3D12_RESOURCE_STATE_COPY_SOURCE,
                 GuestInputTexture::kReadState);
    }
    auto diagnostics = Buffer(device, 16, false);
    Transition(list.Get(), result.diagnostics, GuestInputTexture::kReadState,
               D3D12_RESOURCE_STATE_COPY_SOURCE);
    list->CopyBufferRegion(diagnostics.Get(), 0, result.diagnostics, 0, 16);
    Transition(list.Get(), result.diagnostics, D3D12_RESOURCE_STATE_COPY_SOURCE,
               GuestInputTexture::kReadState);
    Finish(list.Get());
    void *mapped;
    D3D12_RANGE range{0, SIZE_T(rb[0]->GetDesc().Width)};
    Check(rb[0]->Map(0, &range, &mapped), "color readback");
    // Powers-of-two values and exponent adjustment have exact FP16 encodings.
    uint16_t expected[4] = {0x3400, 0x4400, 0x4880, 0x3c00};
    for (unsigned i = 0; i < 3; ++i)
      expected[i] = uint16_t(int(expected[i]) + 2 * c.color_exp * 1024);
    for (unsigned y = 0; y < c.height; ++y)
      for (unsigned x = 0; x < c.width; ++x)
        Require(!std::memcmp(static_cast<uint8_t *>(mapped) +
                                 y * fp[0].Footprint.RowPitch + x * 8,
                             expected, 8),
                "linear HDR color mismatch");
    rb[0]->Unmap(0, nullptr);
    range = {0, 4};
    Check(rb[1]->Map(0, &range, &mapped), "exposure readback");
    const float actual = *static_cast<float *>(mapped);
    rb[1]->Unmap(0, nullptr);
    const double target =
        !c.exposure ? 1
        : (c.nonuniform || c.missing_exposure)
            ? 0
            : c.key * std::exp2(-double(c.log_scale) * c.log_value / 65535.0 *
                                std::exp2(c.exposure_exp));
    Require(std::isfinite(actual) &&
                std::abs(actual - target) <=
                    std::max(1e-10, std::abs(target) * 2e-5),
            "exposure mismatch");
    range = {0, 16};
    Check(diagnostics->Map(0, &range, &mapped), "diagnostics readback");
    const auto *v = static_cast<float *>(mapped);
    Require((v[3] != 0) == !(c.nonuniform || c.missing_exposure),
            "scalar validity mismatch");
    if (c.nonuniform)
      Require(v[0] < v[1] && actual == 0, "nonuniform exposure was averaged");
    diagnostics->Unmap(0, nullptr);
    std::printf("PASS %s %ux%u HDR_rgb=%g,%g,%g exposure=%.9g uniform=%d\n",
                c.name, c.width, c.height, .25 * std::exp2(2 * c.color_exp),
                4 * std::exp2(2 * c.color_exp), 9 * std::exp2(2 * c.color_exp),
                actual, !(c.nonuniform || c.missing_exposure));
  }
};
} // namespace
int main() {
  rex::InitLogging("scene-color-probe.log");
  int status = 0;
  try {
    rex::cvar::SetFlagByName("d3d12_debug", "true");
    rex::cvar::SetFlagByName("d3d12_streamline", "false");
    Harness h;
    ComPtr<ID3D12InfoQueue> messages;
    Check(h.provider->GetDevice()->QueryInterface(IID_PPV_ARGS(&messages)),
          "debug layer");
    messages->ClearStoredMessages();
    h.Run({.name = "uniform-3x1"});
    h.Run({.name = "single-unorm-step-is-nonuniform", .nonuniform = true});
    h.Run({.name = "branch-off-no-exposure", .exposure = false});
    h.Run({.name = "signed-negative-color-exp", .color_exp = -1});
    h.Run({.name = "positive-fetch-exponents",
           .color_exp = 1,
           .exposure_exp = 1});
    h.Run({.name = "dark-log", .log_value = 0});
    h.Run({.name = "bright-log", .log_value = 65535});
    h.Run({.name = "actual-3x-scene", .width = 3360, .height = 1752});
    h.Run({.name = "4k-resize", .width = 3840, .height = 2160});
    h.Run({.name = "reuse-after-resize"});
    h.Run({.name = "missing-guest-exposure-auto-contract",
           .missing_exposure = true});
    h.AlignmentRules();
    unsigned errors = 0;
    for (UINT64 i = 0; i < messages->GetNumStoredMessages(); ++i) {
      SIZE_T bytes = 0;
      messages->GetMessage(i, nullptr, &bytes);
      std::vector<uint8_t> storage(bytes);
      auto *m = reinterpret_cast<D3D12_MESSAGE *>(storage.data());
      Check(messages->GetMessage(i, m, &bytes), "debug message");
      if (m->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) {
        ++errors;
        std::printf("D3D12 ERROR: %s\n", m->pDescription);
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
