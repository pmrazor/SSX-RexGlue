// Validates translated, locally supplied SSX microcode on the actual D3D12
// device. No game microcode is embedded or redistributed by this probe.
#include <cstdio>
#include <d3dcompiler.h>
#include <filesystem>
#include <fstream>
#include <rex/cvar.h>
#include <rex/graphics/pipeline/shader/dxbc.h>
#include <rex/logging.h>
#include <rex/ui/d3d12/d3d12_provider.h>
#include <rex/ui/d3d12/ssx_jitter.h>
#include <stdexcept>
using namespace rex::graphics;
using namespace rex::ui::d3d12;
using Microsoft::WRL::ComPtr;
void Check(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
void CheckRejections(const DxbcShader &shader,
                     const std::vector<uint32_t> &words) {
  auto predicated = words, wrong_matrix = words;
  bool changed_predicate = false, changed_matrix = false;
  for (uint32_t i = 0; i < shader.cf_pair_index_bound(); ++i) {
    ucode::ControlFlowInstruction pair[2];
    ucode::UnpackControlFlowInstructions(words.data() + i * 3, pair);
    for (const auto &cf : pair) {
      if (cf.opcode() != ucode::ControlFlowOpcode::kExec &&
          cf.opcode() != ucode::ControlFlowOpcode::kExecEnd)
        continue;
      ParsedExecInstruction exec;
      ParseControlFlowExec(cf.exec, i * 2, exec);
      auto sequence = exec.sequence;
      for (uint32_t n = 0; n < exec.instruction_count; ++n, sequence >>= 2) {
        if (sequence & 1)
          continue;
        auto offset = (exec.instruction_address + n) * 3;
        const auto &raw = *reinterpret_cast<const ucode::AluInstruction *>(
            words.data() + offset);
        ParsedAluInstruction op;
        ParseAluInstruction(raw, xenos::ShaderType::kVertex, op);
        if (op.vector_and_constant_result.storage_target ==
            InstructionStorageTarget::kPosition) {
          // The ISA's ALU predicate-enable bit. The export may now be absent.
          predicated[offset + 1] |= 1u << 28;
          changed_predicate = true;
        }
        for (unsigned src = 1; src <= 3; ++src) {
          if (raw.src_is_temp(src))
            continue;
          const auto r = raw.src_reg(src);
          if (!((r >= 156 && r < 164) || (r >= 192 && r < 196)))
            continue;
          const auto shift = (3 - src) * 8;
          wrong_matrix[offset + 2] =
              (wrong_matrix[offset + 2] & ~(255u << shift)) |
              (((r + 64) & 255u) << shift);
          changed_matrix = true;
        }
      }
    }
  }
  Check(changed_predicate && !AnalyzeSsxVertexShader(predicated).projected,
        "conditional position was accepted");
  Check(changed_matrix && !AnalyzeSsxVertexShader(wrong_matrix).projected,
        "unrecognized matrix family was accepted");
  Check(!AnalyzeSsxVertexShader(std::span(words).first(words.size() / 2))
             .projected,
        "truncated shader was accepted");
}
int main(int argc, char **argv) {
  rex::InitLogging("jitter-shader-probe.log");
  try {
    Check(argc == 2,
          "Usage: ssx_jitter_shader_probe <local shader dump directory>");
    rex::cvar::SetFlagByName("d3d12_debug", "true");
    rex::cvar::SetFlagByName("d3d12_streamline", "false");
    auto provider = D3D12Provider::Create();
    Check(bool(provider), "device");
    auto *device = provider->GetDevice();
    ComPtr<ID3D12InfoQueue> info;
    Check(SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&info))),
          "debug layer");
    info->ClearStoredMessages();
    D3D12_ROOT_PARAMETER params[6]{};
    for (UINT i = 0; i < 4; ++i) {
      params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
      params[i].Descriptor = {i, 0};
    }
    params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[4].Descriptor = {0, 0};
    params[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[5].Descriptor = {0, 0};
    D3D12_ROOT_SIGNATURE_DESC rd{
        6, params, 0, nullptr,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
    ComPtr<ID3DBlob> blob, error;
    Check(SUCCEEDED(D3D12SerializeRootSignature(
              &rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error)),
          "root serialization");
    ComPtr<ID3D12RootSignature> root;
    Check(SUCCEEDED(device->CreateRootSignature(0, blob->GetBufferPointer(),
                                                blob->GetBufferSize(),
                                                IID_PPV_ARGS(&root))),
          "root");
    DxbcShaderTranslator translator(
        rex::ui::GraphicsProvider::GpuVendorID::kNvidia, false, true, false,
        true, 3, 3);
    unsigned passed = 0, clip = 0, structural = 0, extra = 0, scanned = 0;
    for (auto &file : std::filesystem::directory_iterator(argv[1])) {
      auto name = file.path().filename().string();
      if (!name.ends_with(".ucode.bin.vert") || name.size() < 23)
        continue;
      uint64_t hash = std::stoull(name.substr(7, 16), nullptr, 16);
      std::ifstream input(file.path(), std::ios::binary | std::ios::ate);
      size_t bytes = size_t(input.tellg());
      Check(bytes && bytes % 4 == 0, "microcode size");
      input.seekg(0);
      std::vector<uint32_t> words(bytes / 4);
      input.read(reinterpret_cast<char *>(words.data()), bytes);
      DxbcShader shader(rex::graphics::xenos::ShaderType::kVertex, hash,
                        words.data(), words.size(), std::endian::native);
      rex::string::StringBuffer disassembly;
      shader.AnalyzeUcode(disassembly);
      ++scanned;
      const auto &analysis = shader.ssx_analysis();
      if (analysis.projected) {
        CheckRejections(shader, words);
        ++structural;
        if (IsSsxJitterShader(hash)) {
          Check(analysis.clip_interpolators == SsxClipInterpolatorMask(hash),
                "structural clip mask differs from audited mask");
        } else {
          ++extra;
          std::printf("STRUCTURAL_NEW %016llX clip_mask=%X\n", hash,
                      analysis.clip_interpolators);
        }
        // Same microcode with an unrelated identity must get the same answer.
        DxbcShader renamed(rex::graphics::xenos::ShaderType::kVertex,
                           hash ^ 0xD1FF000000000000ull, words.data(),
                           words.size(), std::endian::native);
        Check(renamed.ssx_analysis().projected &&
                  renamed.ssx_analysis().clip_interpolators ==
                      analysis.clip_interpolators,
              "structural recognition depends on shader hash");
      } else if (IsSsxJitterShader(hash)) {
        std::printf("AUDITED_EXCEPTION %016llX\n", hash);
      }
      if (!analysis.projected && !IsSsxJitterShader(hash) &&
          !SsxFixedScreenShader(hash))
        continue;
      DxbcShaderTranslator::Modification modification(
          translator.GetDefaultVertexShaderModification(64));
      modification.vertex.ssx_clip_jitter = true;
      auto *translation = shader.GetOrCreateTranslation(modification.value);
      Check(translator.TranslateAnalyzedShader(*translation),
            "translation failed");
      const auto &code = translation->translated_binary();
      D3D12_GRAPHICS_PIPELINE_STATE_DESC p{};
      p.pRootSignature = root.Get();
      p.VS = {code.data(), code.size()};
      p.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
      p.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
      p.RasterizerState.DepthClipEnable = TRUE;
      p.SampleMask = UINT_MAX;
      p.SampleDesc.Count = 1;
      p.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
      p.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
      ComPtr<ID3D12PipelineState> pipeline;
      HRESULT hr =
          device->CreateGraphicsPipelineState(&p, IID_PPV_ARGS(&pipeline));
      if (FAILED(hr)) {
        std::printf("FAIL %016llX HRESULT=%08X\n", hash, unsigned(hr));
        throw std::runtime_error("GPU rejected translated vertex shader");
      }
      ++passed;
      if (analysis.projected ? analysis.clip_interpolators
                             : SsxClipInterpolatorMask(hash))
        ++clip;
    }
    Check(passed >= 60, "not enough audited local shaders found");
    Check(structural >= 55,
          "structural recognition lost audited projection families");
    std::printf("STRUCTURAL scanned=%u recognized=%u additional=%u "
                "hash_independent=true\n",
                scanned, structural, extra);
    unsigned errors = 0;
    for (UINT64 i = 0; i < info->GetNumStoredMessages(); ++i) {
      SIZE_T bytes = 0;
      info->GetMessage(i, nullptr, &bytes);
      std::vector<uint8_t> storage(bytes);
      auto *m = reinterpret_cast<D3D12_MESSAGE *>(storage.data());
      info->GetMessage(i, m, &bytes);
      if (m->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) {
        ++errors;
        std::puts(m->pDescription);
      }
    }
    std::printf(
        "PASS translated shaders=%u clip_varying_shaders=%u D3D12_errors=%u\n",
        passed, clip, errors);
    Check(!errors, "D3D12 validation errors");
  } catch (const std::exception &e) {
    std::fprintf(stderr, "FAIL %s\n", e.what());
    rex::ShutdownLogging();
    return 1;
  }
  rex::ShutdownLogging();
  return 0;
}
