#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <rex/ui/d3d12/d3d12_provider.h>
#include <rex/ui/d3d12/ssx_fg_color.h>
#include <stdexcept>
#include <vector>
using namespace rex::ui::d3d12;
using Microsoft::WRL::ComPtr;
namespace {
void Need(bool ok, const char *what) {
  if (!ok)
    throw std::runtime_error(what);
}
void Check(HRESULT hr, const char *what) { Need(SUCCEEDED(hr), what); }
ComPtr<ID3D12Resource> Buffer(ID3D12Device *d, uint64_t size, bool upload) {
  D3D12_RESOURCE_DESC r{};
  r.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  r.Width = size;
  r.Height = r.DepthOrArraySize = r.MipLevels = r.SampleDesc.Count = 1;
  r.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  D3D12_HEAP_PROPERTIES h{};
  h.Type = upload ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_READBACK;
  ComPtr<ID3D12Resource> out;
  Check(d->CreateCommittedResource(&h, D3D12_HEAP_FLAG_NONE, &r,
                                   upload ? D3D12_RESOURCE_STATE_GENERIC_READ
                                          : D3D12_RESOURCE_STATE_COPY_DEST,
                                   nullptr, IID_PPV_ARGS(&out)),
        "FG color buffer");
  return out;
}
void Transition(ID3D12GraphicsCommandList *l, ID3D12Resource *r,
                D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b) {
  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition = {r, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, a, b};
  l->ResourceBarrier(1, &barrier);
}
} // namespace
// Optional replay is a local binary packet: width,height,256 LUT words, tightly
// packed BGRA8 source. No copyrighted content is stored in the source tree.
void CheckFGColor(D3D12Provider &provider, const char *replay) {
  auto *d = provider.GetDevice();
  auto frame = std::make_shared<SsxFrameInputs>();
  auto &m = frame->metadata;
  m.scene_width = 1120;
  m.scene_height = 584;
  m.scale_x = m.scale_y = 3;
  m.output_width = 3840;
  m.output_height = 2160;
  m.camera_samples = 1;
  m.depth_before_lighting = m.fg_hudless_captured = true;
  constexpr uint32_t sw = 3360, sh = 1752, ow = 3840, oh = 2160;
  std::vector<uint8_t> pixels(size_t(sw) * sh * 4);
  for (uint32_t y = 0; y < sh; ++y)
    for (uint32_t x = 0; x < sw; ++x) {
      const auto i = (size_t(y) * sw + x) * 4;
      pixels[i] = uint8_t((x / 13 + y / 19) % 256);
      pixels[i + 1] = uint8_t((x / 31 + y / 7) % 256);
      pixels[i + 2] = uint8_t((x / 5 + y / 29) % 256);
      pixels[i + 3] = 255;
    }
  for (uint32_t i = 0; i < 256; ++i)
    m.fg_gamma_ramp[i] =
        (uint32_t(std::lround(std::pow(i / 255.0, 1.1) * 1023)) << 20) |
        (uint32_t(std::lround(std::pow(i / 255.0, .9) * 1023)) << 10) | i * 4;
  if (replay) {
    std::ifstream f(replay, std::ios::binary);
    uint32_t w = 0, h = 0;
    f.read(reinterpret_cast<char *>(&w), 4);
    f.read(reinterpret_cast<char *>(&h), 4);
    Need(w == sw && h == sh, "FG replay extent");
    f.read(reinterpret_cast<char *>(m.fg_gamma_ramp.data()), 256 * 4);
    f.read(reinterpret_cast<char *>(pixels.data()), pixels.size());
    Need(bool(f) && f.peek() == EOF, "FG replay size");
  }
  auto &input = frame->fg_hudless;
  bool created = false;
  Need(input.Prepare(d, DXGI_FORMAT_R8G8B8A8_TYPELESS, sw, sh, created),
       "FG source");
  input.view_format = DXGI_FORMAT_R8G8B8A8_UNORM;
  input.host_swizzle = 0x60A;
  Need(CanBuildSsxFGColor(*frame, ow, oh), "FG valid format");
  Need(!CanBuildSsxFGColor(*frame, 2560, 1440), "FG reject scaling");
  m.fg_gamma_pwl = true;
  Need(!CanBuildSsxFGColor(*frame, ow, oh), "FG reject PWL");
  m.fg_gamma_pwl = false;
  m.fg_hudless_duplicate = true;
  Need(!CanBuildSsxFGColor(*frame, ow, oh), "FG reject duplicate");
  m.fg_hudless_duplicate = false;
  input.host_swizzle = 0;
  Need(!CanBuildSsxFGColor(*frame, ow, oh), "FG reject swizzle");
  input.host_swizzle = 0x60A;
  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> list;
  ComPtr<ID3D12Fence> fence, fg_fence;
  Check(d->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                  IID_PPV_ARGS(&allocator)),
        "allocator");
  Check(d->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(),
                             nullptr, IID_PPV_ARGS(&list)),
        "list");
  Check(d->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)),
        "fence");
  Check(d->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fg_fence)),
        "FG consumption fence");
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
  uint64_t bytes = 0;
  auto desc = input.resource->GetDesc();
  d->GetCopyableFootprints(&desc, 0, 1, 0, &fp, nullptr, nullptr, &bytes);
  auto upload = Buffer(d, bytes, true);
  void *mapped = nullptr;
  D3D12_RANGE none{};
  Check(upload->Map(0, &none, &mapped), "upload map");
  for (uint32_t y = 0; y < sh; ++y)
    std::memcpy(static_cast<uint8_t *>(mapped) + fp.Offset +
                    size_t(y) * fp.Footprint.RowPitch,
                pixels.data() + size_t(y) * sw * 4, sw * 4);
  upload->Unmap(0, nullptr);
  D3D12_TEXTURE_COPY_LOCATION source{}, dest{};
  source.pResource = upload.Get();
  source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  source.PlacedFootprint = fp;
  dest.pResource = input.resource.Get();
  dest.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  list->CopyTextureRegion(&dest, 0, 0, 0, &source, nullptr);
  Transition(list.Get(), input.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
             GuestInputTexture::kReadState);
  SsxFGColor color;
  std::vector<std::shared_ptr<SsxFGColorFrame>> held;
  for (unsigned i = 0; i < 6; ++i) {
    auto out = color.Record(provider, list.Get(), frame, ow, oh, 1, 0);
    Need(bool(out), "FG six color slots");
    out->fg_fence = fg_fence;
    out->fg_fence_value = 1;
    held.push_back(out);
  }
  Need(!color.Record(provider, list.Get(), frame, ow, oh, 1, 0),
       "FG held pool exhaustion");
  desc = held[0]->color->GetDesc();
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT ofp{};
  d->GetCopyableFootprints(&desc, 0, 1, 0, &ofp, nullptr, nullptr, &bytes);
  auto readback = Buffer(d, bytes, false);
  source.pResource = held[0]->color.Get();
  source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  dest.pResource = readback.Get();
  dest.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  dest.PlacedFootprint = ofp;
  Transition(list.Get(), source.pResource, GuestInputTexture::kReadState,
             D3D12_RESOURCE_STATE_COPY_SOURCE);
  list->CopyTextureRegion(&dest, 0, 0, 0, &source, nullptr);
  Transition(list.Get(), source.pResource, D3D12_RESOURCE_STATE_COPY_SOURCE,
             GuestInputTexture::kReadState);
  Check(list->Close(), "close");
  ID3D12CommandList *lists[] = {list.Get()};
  provider.GetDirectQueue()->ExecuteCommandLists(1, lists);
  Check(provider.GetDirectQueue()->Signal(fence.Get(), 1), "signal");
  HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  Need(event != nullptr, "event");
  Check(fence->SetEventOnCompletion(1, event), "event fence");
  auto wait = WaitForSingleObject(event, 15000);
  CloseHandle(event);
  Need(wait == WAIT_OBJECT_0, "FG GPU timeout");
  D3D12_RANGE range{0, size_t(bytes)};
  Check(readback->Map(0, &range, &mapped), "read map");
  unsigned checked = 0, max_error = 0;
  // Independent bilinear + guest 8-bit store + 10-bit gamma LUT + host 8-bit
  // store.
  for (uint32_t y = 3; y < oh; y += 17)
    for (uint32_t x = 5; x < ow; x += 19)
      for (unsigned c = 0; c < 3; ++c) {
        const double sx = (x + .5) * sw / ow - .5, sy = (y + .5) * sh / oh - .5;
        int x0 = int(std::floor(sx)), y0 = int(std::floor(sy));
        double ax = sx - x0, ay = sy - y0;
        // D3D filtering uses eight fractional bits; tolerate one output code
        // for interpolation/UNORM ties, but never fit an exposure or alignment
        // offset.
        ax = std::round(ax * 256) / 256;
        ay = std::round(ay * 256) / 256;
        auto at = [&](int px, int py) {
          return pixels[(size_t(std::clamp(py, 0, int(sh) - 1)) * sw +
                         std::clamp(px, 0, int(sw) - 1)) *
                            4 +
                        2 - c];
        };
        double value =
            (at(x0, y0) * (1 - ax) + at(x0 + 1, y0) * ax) * (1 - ay) +
            (at(x0, y0 + 1) * (1 - ax) + at(x0 + 1, y0 + 1) * ax) * ay;
        auto code = uint32_t(std::floor(value + .5));
        auto expected = uint8_t(std::lround(
            ((m.fg_gamma_ramp[code] >> (20 - c * 10)) & 1023) * 255.0 / 1023));
        auto actual = *(static_cast<uint8_t *>(mapped) + ofp.Offset +
                        size_t(y) * ofp.Footprint.RowPitch + x * 4 + c);
        max_error = std::max(max_error,
                             unsigned(std::abs(int(actual) - int(expected))));
        ++checked;
      }
  if (replay) {
    std::ofstream out("fg-color-output.bin", std::ios::binary);
    for (uint32_t y = 0; y < oh; ++y)
      out.write(static_cast<char *>(mapped) + ofp.Offset +
                    size_t(y) * ofp.Footprint.RowPitch,
                ow * 4);
    Need(bool(out), "FG output write");
  }
  readback->Unmap(0, &none);
  std::printf("FG_COLOR samples=%u max_code_error=%u\n", checked, max_error);
  Need(max_error <= 2, "FG analytic color conversion");
  held.clear();
  color.Collect(1);
  Check(allocator->Reset(), "reset allocator");
  Check(list->Reset(allocator.Get(), nullptr), "reset list");
  Need(!color.Record(provider, list.Get(), frame, ow, oh, 2, 1),
       "FG input fence must block slot reuse");
  Check(fg_fence->Signal(1), "complete synthetic consumer");
  color.Collect(1);
  Need(frame.use_count() == 1,
       "FG completed slots must release producer packets");
  auto reused = color.Record(provider, list.Get(), frame, ow, oh, 2, 1);
  Need(bool(reused), "FG released slot reuse");
  Check(list->Close(), "close reused");
  provider.GetDirectQueue()->ExecuteCommandLists(1, lists);
  Check(provider.GetDirectQueue()->Signal(fence.Get(), 2), "signal reused");
  Check(fence->SetEventOnCompletion(2, nullptr), "drain reused");
  std::puts("PASS FG color mapping, format guards, bounded pool and "
            "independent consumer fence");
}
