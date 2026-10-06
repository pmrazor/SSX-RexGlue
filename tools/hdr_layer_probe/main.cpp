// Exercises the actual translated ROV path with locally supplied guest
// microcode. No game shader data is embedded or redistributed.
#include <DirectXPackedVector.h>
#include <cstdio>
#include <cstring>
#include <d3dcompiler.h>
#include <filesystem>
#include <fstream>
#include <rex/cvar.h>
#include <rex/graphics/pipeline/shader/dxbc.h>
#include <rex/logging.h>
#include <rex/ui/d3d12/d3d12_provider.h>
#include <rex/ui/d3d12/ssx_hdr_layers.h>
#include <stdexcept>
using namespace rex::graphics;
using namespace rex::ui::d3d12;
using Microsoft::WRL::ComPtr;
void Require(bool v, const char *m) {
  if (!v)
    throw std::runtime_error(m);
}
void Check(HRESULT hr, const char *m) { Require(SUCCEEDED(hr), m); }
ComPtr<ID3D12Resource> Buffer(ID3D12Device *device, uint64_t size,
                              D3D12_HEAP_TYPE heap,
                              D3D12_RESOURCE_STATES state) {
  D3D12_HEAP_PROPERTIES hp{};
  hp.Type = heap;
  D3D12_RESOURCE_DESC d{};
  d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  d.Width = size;
  d.Height = d.DepthOrArraySize = d.MipLevels = d.SampleDesc.Count = 1;
  d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  if (heap == D3D12_HEAP_TYPE_DEFAULT)
    d.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  ComPtr<ID3D12Resource> r;
  Check(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, state,
                                        nullptr, IID_PPV_ARGS(&r)),
        "buffer");
  return r;
}
void Barrier(ID3D12GraphicsCommandList *l, ID3D12Resource *r,
             D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b) {
  D3D12_RESOURCE_BARRIER v{};
  v.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  v.Transition = {r, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, a, b};
  l->ResourceBarrier(1, &v);
}
int main(int argc, char **argv) {
  rex::InitLogging("hdr-layer-probe.log");
  int status = 0;
  try {
    if (argc == 4 && std::string(argv[1]) == "--dump-cache") {
      // Read-only snapshot of XESH v20201219; locally owned game code stays
      // local.
      std::ifstream cache(argv[2], std::ios::binary);
      uint32_t header[2]{};
      cache.read(reinterpret_cast<char *>(header), 8);
      Require(header[0] == 0x48534558 && header[1] == 0x19122020,
              "XESH version");
      unsigned count = 0;
      while (cache) {
        uint64_t hash;
        uint32_t packed;
        if (!cache.read(reinterpret_cast<char *>(&hash), 8) ||
            !cache.read(reinterpret_cast<char *>(&packed), 4))
          break;
        const uint32_t size = packed & 0x7fffffff;
        Require(size > 0 && size < 65536, "XESH shader size");
        std::vector<uint32_t> words(size);
        if (!cache.read(reinterpret_cast<char *>(words.data()), size * 4))
          break;
        DxbcShader s((packed >> 31) ? xenos::ShaderType::kPixel
                                    : xenos::ShaderType::kVertex,
                     hash, words.data(), words.size(), std::endian::big);
        rex::string::StringBuffer dis;
        s.AnalyzeUcode(dis);
        s.DumpUcode(argv[3]);
        ++count;
      }
      std::printf("Dumped %u local shaders\n", count);
      rex::ShutdownLogging();
      return 0;
    }
    Require(argc == 2,
            "Usage: ssx_hdr_layer_probe <local shader dump directory>");
    rex::cvar::SetFlagByName("d3d12_debug", "true");
    rex::cvar::SetFlagByName("d3d12_streamline", "false");
    auto provider = D3D12Provider::Create();
    Require(bool(provider), "provider");
    auto *device = provider->GetDevice();
    ComPtr<ID3D12InfoQueue> messages;
    Check(device->QueryInterface(IID_PPV_ARGS(&messages)), "debug layer");
    messages->ClearStoredMessages();
    D3D12_DESCRIPTOR_RANGE uav_range{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0, 0,
                                     0};
    D3D12_ROOT_PARAMETER params[5]{};
    for (UINT i = 0; i < 4; ++i) {
      params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
      params[i].Descriptor = {i, 0};
    }
    params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[4].DescriptorTable = {1, &uav_range};
    D3D12_ROOT_SIGNATURE_DESC rd{
        5, params, 0, nullptr,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
    ComPtr<ID3DBlob> serialized, error;
    Check(D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1,
                                      &serialized, &error),
          "root serialization");
    ComPtr<ID3D12RootSignature> root;
    Check(device->CreateRootSignature(0, serialized->GetBufferPointer(),
                                      serialized->GetBufferSize(),
                                      IID_PPV_ARGS(&root)),
          "root");
    const char *vs_source = R"(
      cbuffer Draw : register(b1) { float4 color; };
      struct Output { float4 c:TEXCOORD0; float4 p:SV_Position; };
      Output main(uint id:SV_VertexID) { Output o; o.c=color;
        o.p=float4(id==2?3:-1,id==1?3:-1,.5,1);return o; }
    )";
    ComPtr<ID3DBlob> vs;
    Check(D3DCompile(vs_source, strlen(vs_source), nullptr, nullptr, nullptr,
                     "main", "vs_5_1", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vs,
                     &error),
          "test VS");
    auto path = std::filesystem::path(argv[1]) /
                "shader_1396D901E25CD2D1.ucode.bin.frag";
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    Require(bool(input), "local color shader");
    size_t bytes = size_t(input.tellg());
    Require(bytes && bytes % 4 == 0, "microcode size");
    std::vector<uint32_t> words(bytes / 4);
    input.seekg(0);
    input.read(reinterpret_cast<char *>(words.data()), bytes);
    DxbcShader shader(xenos::ShaderType::kPixel, 0x1396D901E25CD2D1ull,
                      words.data(), words.size(), std::endian::native);
    rex::string::StringBuffer disassembly;
    shader.AnalyzeUcode(disassembly);
    DxbcShaderTranslator translator(
        rex::ui::GraphicsProvider::GpuVendorID::kNvidia, false, true, false,
        true, 3, 3);
    DxbcShaderTranslator::Modification mod(
        translator.GetDefaultPixelShaderModification(64));
    mod.pixel.interpolator_mask = 1;
    mod.pixel.ssx_hdr_layer = true;
    auto *translation = shader.GetOrCreateTranslation(mod.value);
    Require(translator.TranslateAnalyzedShader(*translation),
            "layer shader translation");
    const auto &ps = translation->translated_binary();
    D3D12_GRAPHICS_PIPELINE_STATE_DESC p{};
    p.pRootSignature = root.Get();
    p.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    p.PS = {ps.data(), ps.size()};
    p.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    p.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    p.RasterizerState.DepthClipEnable = TRUE;
    p.SampleMask = UINT_MAX;
    p.SampleDesc.Count = 1;
    p.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    p.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    ComPtr<ID3D12PipelineState> pipeline;
    const HRESULT pipeline_hr =
        device->CreateGraphicsPipelineState(&p, IID_PPV_ARGS(&pipeline));
    if (FAILED(pipeline_hr))
      std::printf("PSO HRESULT=%08X\n", unsigned(pipeline_hr));
    if (SUCCEEDED(pipeline_hr)) {
      const UINT base=65536, plane_words=3840*2160*2, count=base+plane_words+3840*4*2;
      auto edram = Buffer(device, count * 4, D3D12_HEAP_TYPE_DEFAULT,
                          D3D12_RESOURCE_STATE_COPY_DEST);
      auto upload = Buffer(device, count * 4, D3D12_HEAP_TYPE_UPLOAD,
                           D3D12_RESOURCE_STATE_GENERIC_READ);
      auto readback = Buffer(device, count * 4, D3D12_HEAP_TYPE_READBACK,
                             D3D12_RESOURCE_STATE_COPY_DEST);
      auto cb = Buffer(device, 2048, D3D12_HEAP_TYPE_UPLOAD,
                       D3D12_RESOURCE_STATE_GENERIC_READ);
      D3D12_DESCRIPTOR_HEAP_DESC hd{};
      hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
      hd.NumDescriptors = 2;
      hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
      ComPtr<ID3D12DescriptorHeap> heap;
      Check(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap)), "heap");
      auto handle = heap->GetCPUDescriptorHandleForHeapStart();
      D3D12_UNORDERED_ACCESS_VIEW_DESC ud{};
      ud.Format = DXGI_FORMAT_R32_TYPELESS;
      ud.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
      ud.Buffer.NumElements = count;
      ud.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
      device->CreateUnorderedAccessView(edram.Get(), nullptr, &ud, handle);
      handle.ptr += device->GetDescriptorHandleIncrementSize(hd.Type);
      ud.Format = DXGI_FORMAT_R32_UINT;
      ud.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
      device->CreateUnorderedAccessView(edram.Get(), nullptr, &ud, handle);
      ComPtr<ID3D12Fence> fence;
      Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)),
            "fence");
      uint64_t serial = 0;
      unsigned checks = 0;
      struct Case {
        const char *name;
        uint32_t blend;
        int draws;
        bool enabled, discard, scissor;
        bool depth = false, color_mask_disabled = false;
        uint32_t x = 0, y = 0;
        bool white=false;
      };
      const Case cases[] = {
          {"opaque", 0x10001, 1, true, false, false},
          {"alpha", 0x706, 1, true, false, false},
          {"ordered-alpha", 0x706, 3, true, false, false},
          {"additive", 0x101, 2, true, false, false},
          {"source-alpha-additive", 0x106, 2, true, false, false},
          {"destination-alpha", 0xA, 1, true, false, false},
          {"destination-color-multiply", 0x80008, 1, true, false, false},
          {"ordered-color-multiply", 0x80008, 2, true, false, false},
          {"white-multiply-keeps-background", 0x80008, 1, true, false, false,false,false,0,0,true},
          {"source-color", 0x4, 1, true, false, false},
          {"inverse-source-color", 0x5, 1, true, false, false},
          {"destination-source-color", 0x401, 1, true, false, false},
          {"inverse-destination-alpha", 0xB, 1, true, false, false},
          {"disabled", 0x706, 1, false, false, false},
          {"alpha-test-rejected", 0x706, 1, true, true, false},
          {"scissor-rejected", 0x706, 1, true, false, true},
          {"depth-test-rejected", 0x706, 1, true, false, false, true},
          {"effective-color-mask-disabled", 0x10001, 1, true, false, false,
           false, true},
          {"nonzero-pixel-address", 0x706, 1, true, false, false, false, false,
           3, 2},
          {"outside-layer-width", 0x706, 1, true, false, false, false, false, 4,
           2},
          {"outside-layer-height", 0x706, 1, true, false, false, false, false,
           3, 3}};
      for (const auto &c : cases) {
        void *mapped = nullptr;
        D3D12_RANGE none{};
        Check(upload->Map(0, &none, &mapped), "upload map");
        auto *data = static_cast<uint32_t *>(mapped);
        std::fill_n(data, count, 0x80C08040u);
        for (UINT i=base;i<base+plane_words;++i)data[i]=0;
        for (UINT i=base+plane_words;i<count;i+=2){data[i]=0x3c003c00;data[i+1]=0x00003c00;}
        upload->Unmap(0, nullptr);
        DxbcShaderTranslator::SystemConstants k{};
        k.flags = DxbcShaderTranslator::kSysFlag_PrimitivePolygonal;
        if (!c.discard)
          k.flags |= DxbcShaderTranslator::kSysFlag_AlphaPassIfLess |
                     DxbcShaderTranslator::kSysFlag_AlphaPassIfEqual |
                     DxbcShaderTranslator::kSysFlag_AlphaPassIfGreater;
        if (c.depth)
          k.flags |= DxbcShaderTranslator::kSysFlag_ROVDepthStencil;
        k.edram_32bpp_tile_pitch_dwords_scaled = 1280 * 9;
        for (int i = 0; i < 4; ++i) {
          k.color_exp_bias[i] = 1;
          k.edram_rt_keep_mask[i][0] = k.edram_rt_keep_mask[i][1] = UINT_MAX;
        }
        k.edram_rt_keep_mask[0][0] = k.edram_rt_keep_mask[0][1] =
            c.color_mask_disabled ? UINT_MAX : 0;
        k.edram_rt_format_flags[0] = 96;
        k.edram_rt_clamp[0][2] = k.edram_rt_clamp[0][3] = 1;
        k.edram_rt_blend_factors_ops[0] = c.blend;
        k.ssx_hdr_layer[0] = c.enabled ? base : 0;
        k.ssx_hdr_layer[1] = 4;
        k.ssx_hdr_layer[2] = 3;
        k.ssx_hdr_layer[3] = c.blend;
        static_assert(sizeof(k) <= 1024);
        const float color[] = {c.white?1.f:.25f,c.white?1.f:.5f,c.white?1.f:.125f,c.white?0.f:.5f};
        Check(cb->Map(0, &none, &mapped), "CB map");
        std::memcpy(mapped, &k, sizeof(k));
        std::memcpy(static_cast<char *>(mapped) + 1024, color, sizeof(color));
        cb->Unmap(0, nullptr);
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                             IID_PPV_ARGS(&allocator)),
              "allocator");
        Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                        allocator.Get(), pipeline.Get(),
                                        IID_PPV_ARGS(&list)),
              "list");
        if (serial)
          Barrier(list.Get(), edram.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
                  D3D12_RESOURCE_STATE_COPY_DEST);
        list->CopyBufferRegion(edram.Get(), 0, upload.Get(), 0, count * 4);
        Barrier(list.Get(), edram.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        list->SetGraphicsRootSignature(root.Get());
        auto *raw_heap = heap.Get();
        list->SetDescriptorHeaps(1, &raw_heap);
        for (UINT i = 0; i < 4; ++i)
          list->SetGraphicsRootConstantBufferView(
              i, cb->GetGPUVirtualAddress() + (i == 1 ? 1024 : 0));
        list->SetGraphicsRootDescriptorTable(
            4, heap->GetGPUDescriptorHandleForHeapStart());
        D3D12_VIEWPORT viewport{float(c.x), float(c.y), 1, 1, 0, 1};
        list->RSSetViewports(1, &viewport);
        D3D12_RECT scissor{LONG(c.x), LONG(c.y),
                           LONG(c.x + (c.scissor ? 0 : 1)), LONG(c.y + 1)};
        list->RSSetScissorRects(1, &scissor);
        list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        for (int draw = 0; draw < c.draws; ++draw)
          list->DrawInstanced(3, 1, 0, 0);
        Barrier(list.Get(), edram.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->CopyBufferRegion(readback.Get(), 0, edram.Get(), 0, count * 4);
        Check(list->Close(), "close");
        ID3D12CommandList *lists[] = {list.Get()};
        provider->GetDirectQueue()->ExecuteCommandLists(1, lists);
        Check(provider->GetDirectQueue()->Signal(fence.Get(), ++serial),
              "signal");
        HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        Check(fence->SetEventOnCompletion(serial, event), "fence event");
        DWORD wait = WaitForSingleObject(event, 20000);
        CloseHandle(event);
        Require(wait == WAIT_OBJECT_0, "GPU timeout");
        D3D12_RANGE range{0, count * 4};
        Check(readback->Map(0, &range, &mapped), "readback");
        const auto *result = reinterpret_cast<const uint16_t *>(
            static_cast<const uint32_t *>(mapped) + base +
            2 * (c.y * 3840 + c.x));
        const auto* trans_result=reinterpret_cast<const uint16_t*>(
            static_cast<const uint32_t*>(mapped)+base+plane_words+2*(c.y*3840+c.x));
        double contribution[3]={},transmission[3]={1,1,1};
        using F=xenos::BlendFactor;
        auto factor=[&](uint32_t f,int ch) {
          switch(F(f)) {
            case F::kZero:return 0.;case F::kOne:return 1.;
            case F::kSrcColor:return double(color[ch]);case F::kOneMinusSrcColor:return 1.-color[ch];
            case F::kSrcAlpha:return double(color[3]);case F::kOneMinusSrcAlpha:return 1.-color[3];
            case F::kDstAlpha:return 128./255;case F::kOneMinusDstAlpha:return 127./255;
            default:return 0.;
          }
        };
        const bool captured=c.enabled&&!c.discard&&!c.scissor&&!c.depth&&!c.color_mask_disabled&&c.x<4&&c.y<3;
        if(captured)for(int draw=0;draw<c.draws;++draw)for(int ch=0;ch<3;++ch){
          const bool multiply=F(c.blend&31)==F::kDstColor;
          const double add=multiply ? 0 : color[ch]*factor(c.blend&31,ch);
          const double gain=factor((c.blend>>8)&31,ch)+(multiply?color[ch]:0);
          contribution[ch]=add+contribution[ch]*gain;transmission[ch]*=gain;
        }
        for(int ch=0;ch<3;++ch) {
          const double actual_c=DirectX::PackedVector::XMConvertHalfToFloat(result[ch]);
          const double actual_t=DirectX::PackedVector::XMConvertHalfToFloat(trans_result[ch]);
          if(std::abs(actual_c-contribution[ch])>.001 || std::abs(actual_t-transmission[ch])>.001){
            std::printf("FAIL %s ch=%d C=%g/%g T=%g/%g\n",c.name,ch,actual_c,contribution[ch],actual_t,transmission[ch]);
            throw std::runtime_error("ROV affine layer mismatch");
          }
          checks+=2;
          if(captured && !c.x && !c.y) {
            // Independent oracle: compare against the original guest ROV write,
            // not merely another copy of the new blend-factor switch.
            const uint32_t original=static_cast<const uint32_t*>(mapped)[0];
            const double initial=double((0x80C08040u>>(ch*8))&255)/255;
            const int reconstructed=int(std::round(std::clamp(actual_c+actual_t*initial,0.,1.)*255));
            const int guest=(original>>(ch*8))&255;
            if(std::abs(reconstructed-guest)>c.draws+1){
              std::printf("FAIL %s guest=%d affine=%d ch=%d\n",c.name,guest,reconstructed,ch);
              throw std::runtime_error("Affine layer disagrees with original Xbox blending");
            }
          }
        }
        Require(result[4]==0 && result[7]==0 && trans_result[4]==0x3c00 && trans_result[6]==0x3c00,"out-of-coverage write");
        readback->Unmap(0, nullptr);
        std::printf("PASS %s\n", c.name);
      }
      std::printf("ROV channel checks=%u\n", checks);
    }
    unsigned errors = 0;
    for (UINT64 i = 0; i < messages->GetNumStoredMessages(); ++i) {
      SIZE_T bytes = 0;
      messages->GetMessage(i, nullptr, &bytes);
      std::vector<uint8_t> b(bytes);
      auto *m = reinterpret_cast<D3D12_MESSAGE *>(b.data());
      messages->GetMessage(i, m, &bytes);
      if (m->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) {
        ++errors;
        std::puts(m->pDescription);
      }
    }
    std::printf("D3D12_errors=%u\n", errors);
    Require(SUCCEEDED(pipeline_hr) && !errors, "GPU validation");
  } catch (const std::exception &e) {
    std::printf("FAIL: %s\n", e.what());
    status = 1;
  }
  rex::ShutdownLogging();
  return status;
}
