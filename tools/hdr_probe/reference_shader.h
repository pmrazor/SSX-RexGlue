#pragma once
#include <rex/ui/d3d12/d3d12_util.h>
#include <fstream>
#include <iterator>

// Optional archived DXBC comparison. The root layout is deliberately identical
// to SsxHDR's: after Record, reuse its exact descriptors/constants to dispatch
// the old shader on the same inputs. Only the shader bytecode differs.
struct ReferenceShader {
  Microsoft::WRL::ComPtr<ID3D12RootSignature> root;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline;
  void Load(const rex::ui::d3d12::D3D12Provider &provider, const char *path) {
    std::ifstream file(path, std::ios::binary);
    std::vector<char> code{std::istreambuf_iterator<char>(file), {}};
    if (code.size() < 32 || code.size() > 1024 * 1024 ||
        std::memcmp(code.data(), "DXBC", 4))
      throw std::runtime_error("reference must be an archived DXBC compute shader");
    D3D12_DESCRIPTOR_RANGE ranges[] = {
        {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 12, 0, 0, 0},
        {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0, 0, 12}};
    D3D12_ROOT_PARAMETER parameters[2]{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[0].Descriptor = {0, 0};
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[1].DescriptorTable = {2, ranges};
    D3D12_STATIC_SAMPLER_DESC samplers[4]{};
    for (uint32_t i = 0; i < 4; ++i) {
      auto &s = samplers[i];
      s.ShaderRegister = i;
      s.Filter = (i & 1) ? D3D12_FILTER_MIN_MAG_MIP_POINT
                         : D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
      s.AddressU = s.AddressV = s.AddressW =
          i < 2 ? D3D12_TEXTURE_ADDRESS_MODE_CLAMP : D3D12_TEXTURE_ADDRESS_MODE_WRAP;
      s.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
      s.MaxLOD = D3D12_FLOAT32_MAX;
    }
    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.NumParameters = 2;
    desc.pParameters = parameters;
    desc.NumStaticSamplers = 4;
    desc.pStaticSamplers = samplers;
    root.Attach(rex::ui::d3d12::util::CreateRootSignature(provider, desc));
    if (root)
      pipeline.Attach(rex::ui::d3d12::util::CreateComputePipeline(
          provider.GetDevice(), code.data(), code.size(), root.Get()));
    if (!pipeline)
      throw std::runtime_error("reference shader pipeline creation failed");
  }
};
