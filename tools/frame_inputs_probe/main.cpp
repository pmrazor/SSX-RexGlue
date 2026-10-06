// No game assets or visible window. Exercises the production snapshot allocator
// and D3D12 copy/state sequence, including source mutation, mip selection and reuse.
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/d3d12/d3d12_provider.h>
#include <rex/ui/d3d12/guest_frame_inputs.h>

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace rex::ui::d3d12;

namespace {
void Require(bool ok, const char* message) {
  if (!ok)
    throw std::runtime_error(message);
}
void Check(HRESULT hr, const char* message) {
  Require(SUCCEEDED(hr), message);
}

void Transition(ID3D12GraphicsCommandList* list, ID3D12Resource* resource,
                D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
  list->ResourceBarrier(1, &barrier);
}

ComPtr<ID3D12Resource> Buffer(ID3D12Device* device, uint64_t bytes, bool upload) {
  D3D12_RESOURCE_DESC desc{};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = bytes;
  desc.Height = desc.DepthOrArraySize = desc.MipLevels = 1;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = upload ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_READBACK;
  ComPtr<ID3D12Resource> result;
  Check(device->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &desc,
            upload ? D3D12_RESOURCE_STATE_GENERIC_READ : D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&result)),
        "Create buffer");
  return result;
}

void RunCase(D3D12Provider& provider, GuestInputTexture& snapshot, DXGI_FORMAT format, UINT width,
             UINT height, UINT mip, UINT bytes_per_pixel, bool expect_reuse) {
  auto* device = provider.GetDevice();
  D3D12_RESOURCE_DESC desc{};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = width;
  desc.Height = height;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = UINT16(mip + 1);
  desc.Format = format;
  desc.SampleDesc.Count = 1;
  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  ComPtr<ID3D12Resource> source;
  Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                        IID_PPV_ARGS(&source)),
        "Create source");
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
  UINT64 bytes;
  device->GetCopyableFootprints(&desc, mip, 1, 0, &footprint, nullptr, nullptr, &bytes);
  auto upload = Buffer(device, bytes, true);
  auto overwrite = Buffer(device, bytes, true);
  auto readback = Buffer(device, bytes, false);
  auto source_readback = Buffer(device, bytes, false);
  const UINT copy_width = footprint.Footprint.Width, copy_height = footprint.Footprint.Height;
  const UINT pitch = footprint.Footprint.RowPitch;
  std::vector<unsigned char> expected(bytes, 0);
  for (UINT y = 0; y < copy_height; ++y)
    for (UINT x = 0; x < copy_width * bytes_per_pixel; ++x)
      expected[y * pitch + x] = static_cast<unsigned char>((y * 17 + x * 7 + mip + 31) & 255);
  void* data;
  D3D12_RANGE empty{0, 0};
  Check(upload->Map(0, &empty, &data), "Map upload");
  std::memcpy(data, expected.data(), bytes);
  upload->Unmap(0, nullptr);
  Check(overwrite->Map(0, &empty, &data), "Map overwrite");
  std::memset(data, 0, bytes);
  overwrite->Unmap(0, nullptr);

  auto* previous = snapshot.resource.Get();
  bool created = false;
  Require(snapshot.Prepare(device, format, copy_width, copy_height, created), "Prepare snapshot");
  Require(created != expect_reuse, "Unexpected allocation/reuse");
  if (expect_reuse)
    Require(snapshot.resource.Get() == previous, "Reuse changed resource");
  ComPtr<ID3D12CommandAllocator> allocator;
  Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)),
        "Create allocator");
  ComPtr<ID3D12GraphicsCommandList> list;
  Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                  IID_PPV_ARGS(&list)),
        "Create list");
  D3D12_TEXTURE_COPY_LOCATION src{}, up{}, dst{}, rb{};
  src.pResource = source.Get();
  src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  src.SubresourceIndex = mip;
  up.pResource = upload.Get();
  up.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  up.PlacedFootprint = footprint;
  dst.pResource = snapshot.resource.Get();
  dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  rb.pResource = readback.Get();
  rb.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  rb.PlacedFootprint = footprint;
  list->CopyTextureRegion(&src, 0, 0, 0, &up, nullptr);
  Transition(list.Get(), source.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
             GuestInputTexture::kReadState);
  Transition(list.Get(), source.Get(), GuestInputTexture::kReadState,
             D3D12_RESOURCE_STATE_COPY_SOURCE);
  auto source_rb = rb;
  source_rb.pResource = source_readback.Get();
  list->CopyTextureRegion(&source_rb, 0, 0, 0, &src, nullptr);
  if (!created)
    Transition(list.Get(), snapshot.resource.Get(), GuestInputTexture::kReadState,
               D3D12_RESOURCE_STATE_COPY_DEST);
  list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
  std::array<D3D12_RESOURCE_BARRIER, 3> restore;
  const auto restore_count =
      snapshot.GetCopyRestoreBarriers(source.Get(), GuestInputTexture::kReadState, mip, restore);
  list->ResourceBarrier(restore_count, restore.data());
  // Mimic the texture cache overwriting its resolve after the input was copied.
  Transition(list.Get(), source.Get(), GuestInputTexture::kReadState,
             D3D12_RESOURCE_STATE_COPY_DEST);
  up.pResource = overwrite.Get();
  list->CopyTextureRegion(&src, 0, 0, 0, &up, nullptr);
  Transition(list.Get(), snapshot.resource.Get(), GuestInputTexture::kReadState,
             D3D12_RESOURCE_STATE_COPY_SOURCE);
  list->CopyTextureRegion(&rb, 0, 0, 0, &dst, nullptr);
  Transition(list.Get(), snapshot.resource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
             GuestInputTexture::kReadState);
  Check(list->Close(), "Close list");
  ID3D12CommandList* lists[] = {list.Get()};
  provider.GetDirectQueue()->ExecuteCommandLists(1, lists);
  ComPtr<ID3D12Fence> fence;
  Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "Create fence");
  Check(provider.GetDirectQueue()->Signal(fence.Get(), 1), "Signal");
  HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  Require(event != nullptr, "Create event");
  HRESULT wait_hr = fence->SetEventOnCompletion(1, event);
  DWORD waited = SUCCEEDED(wait_hr) ? WaitForSingleObject(event, 15000) : WAIT_FAILED;
  CloseHandle(event);
  Require(waited == WAIT_OBJECT_0, "GPU completion timeout");
  D3D12_RANGE range{0, SIZE_T(bytes)};
  Check(readback->Map(0, &range, &data), "Map readback");
  bool equal = true;
  for (UINT y = 0; y < copy_height; ++y)
    equal &= std::memcmp(static_cast<unsigned char*>(data) + y * pitch, expected.data() + y * pitch,
                         copy_width * bytes_per_pixel) == 0;
  if (!equal) {
    void* source_data;
    Check(source_readback->Map(0, &range, &source_data), "Map source readback");
    std::printf("SOURCE before snapshot: %02X,%02X\n", static_cast<unsigned char*>(source_data)[0],
                static_cast<unsigned char*>(source_data)[1]);
    source_readback->Unmap(0, &empty);
    std::printf(
        "MISMATCH format=%u mip=%u footprint=%ux%u pitch=%u bytes=%llu first=%02X,%02X "
        "expected=%02X,%02X\n",
        unsigned(format), mip, copy_width, copy_height, pitch, bytes,
        static_cast<unsigned char*>(data)[0], static_cast<unsigned char*>(data)[1], expected[0],
        expected[1]);
    ComPtr<ID3D12InfoQueue> queue;
    if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&queue)))) {
      for (UINT64 i = 0; i < queue->GetNumStoredMessages(); ++i) {
        SIZE_T length = 0;
        queue->GetMessage(i, nullptr, &length);
        std::vector<unsigned char> storage(length);
        auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
        if (SUCCEEDED(queue->GetMessage(i, message, &length)))
          std::printf("D3D12: %s\n", message->pDescription);
      }
    }
  }
  readback->Unmap(0, &empty);
  Require(equal, "Snapshot content changed or wrong mip/extent copied");
  std::printf(
      "PASS format=%u source=%ux%u mip=%u snapshot=%ux%u reused=%d frozen_after_source_write=1\n",
      unsigned(format), width, height, mip, copy_width, copy_height, expect_reuse);
}
}  // namespace

int main() {
  rex::InitLogging("frame-inputs-probe.log");
  int result = 0;
  try {
    rex::cvar::SetFlagByName("d3d12_debug", "true");
    rex::cvar::SetFlagByName("d3d12_streamline", "false");
    auto provider = D3D12Provider::Create();
    Require(bool(provider), "Create provider");
    ComPtr<ID3D12InfoQueue> messages;
    Check(provider->GetDevice()->QueryInterface(IID_PPV_ARGS(&messages)),
          "Debug layer unavailable");
    messages->ClearStoredMessages();
    GuestInputTexture color, depth, exposure;
    RunCase(*provider, color, DXGI_FORMAT_R16G16B16A16_FLOAT, 3360, 1752, 0, 8, false);
    RunCase(*provider, color, DXGI_FORMAT_R16G16B16A16_FLOAT, 3360, 1752, 0, 8, true);
    RunCase(*provider, color, DXGI_FORMAT_R16G16B16A16_FLOAT, 3840, 2160, 0, 8, false);
    RunCase(*provider, depth, DXGI_FORMAT_R32_FLOAT, 3360, 1752, 0, 4, false);
    RunCase(*provider, exposure, DXGI_FORMAT_R16_TYPELESS, 280, 146, 8, 2, false);
    RunCase(*provider, exposure, DXGI_FORMAT_R16_TYPELESS, 840, 438, 8, 2, false);
    RunCase(*provider, exposure, DXGI_FORMAT_R16_TYPELESS, 840, 438, 8, 2, true);
    auto* old = color.resource.Get();
    bool created;
    Require(!color.Prepare(provider->GetDevice(), DXGI_FORMAT_R8G8B8A8_UNORM, 1, 1, created),
            "Unexpected format accepted");
    Require(!color.Prepare(provider->GetDevice(), DXGI_FORMAT_R32_FLOAT, 0, 1, created),
            "Zero size accepted");
    Require(color.resource.Get() == old, "Failed allocation discarded valid resource");
    unsigned errors = 0;
    for (UINT64 i = 0; i < messages->GetNumStoredMessages(); ++i) {
      SIZE_T bytes = 0;
      messages->GetMessage(i, nullptr, &bytes);
      std::vector<unsigned char> storage(bytes);
      auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
      Check(messages->GetMessage(i, message, &bytes), "Get debug message");
      if (message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) {
        ++errors;
        std::printf("D3D12 ERROR: %s\n", message->pDescription);
      }
    }
    std::printf("D3D12 validation errors: %u\n", errors);
    Require(!errors, "D3D12 validation failed");
  } catch (const std::exception& error) {
    std::printf("FAIL: %s\n", error.what());
    result = 1;
  }
  rex::ShutdownLogging();
  return result;
}
