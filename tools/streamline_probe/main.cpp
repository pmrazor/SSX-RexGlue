// Exercises the same provider and Streamline backend used by the game, without
// any game assets. The test window stays hidden; this is not a gameplay test.
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/d3d12/d3d12_provider.h>

#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
using rex::ui::d3d12::D3D12Provider;
using rex::ui::d3d12::Streamline;
void CheckSREvaluation(D3D12Provider &provider);
void CheckSsxSceneReconstruction(D3D12Provider &provider, const char* color_replay = nullptr);
void CheckSsxQualityScale(D3D12Provider &provider);
void CheckFrameGeneration(D3D12Provider &provider, uint32_t generated_frames, bool hdr10);
void CheckFGColor(D3D12Provider &provider, const char* replay);
void CheckRenderTokens(D3D12Provider &provider);

namespace {
void Require(HRESULT hr, const char *operation) {
  if (FAILED(hr)) {
    std::printf("FAIL %s HRESULT=0x%08lX\n", operation,
                static_cast<unsigned long>(hr));
    throw std::runtime_error(operation);
  }
}

// Drain each submission before resetting its allocator or resizing. This small
// test trades throughput for unambiguous resource lifetime verification.
class QueueDrain {
public:
  explicit QueueDrain(D3D12Provider &provider)
      : queue_(provider.GetDirectQueue()) {
    Require(provider.GetDevice()->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                              IID_PPV_ARGS(&fence_)),
            "CreateFence");
    event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!event_)
      throw std::runtime_error("CreateEvent");
  }
  ~QueueDrain() {
    if (event_)
      CloseHandle(event_);
  }
  void Wait() {
    Require(queue_->Signal(fence_.Get(), ++value_), "Signal");
    Require(fence_->SetEventOnCompletion(value_, event_),
            "SetEventOnCompletion");
    if (WaitForSingleObject(event_, 10000) != WAIT_OBJECT_0) {
      throw std::runtime_error("GPU completion timeout");
    }
  }

private:
  ID3D12CommandQueue *queue_;
  ComPtr<ID3D12Fence> fence_;
  HANDLE event_ = nullptr;
  uint64_t value_ = 0;
};

void CheckPresentation(D3D12Provider &provider) {
  auto *device = provider.GetDevice();
  const wchar_t *class_name = L"SSXStreamlineProbe";
  WNDCLASSW window_class{};
  window_class.lpfnWndProc = DefWindowProcW;
  window_class.hInstance = GetModuleHandleW(nullptr);
  window_class.lpszClassName = class_name;
  if (!RegisterClassW(&window_class))
    throw std::runtime_error("RegisterClassW");
  HWND window = CreateWindowExW(0, class_name, L"SSX Streamline probe",
                                WS_OVERLAPPEDWINDOW, 0, 0, 640, 480, nullptr,
                                nullptr, window_class.hInstance, nullptr);
  if (!window)
    throw std::runtime_error("CreateWindowExW");
  try {
    DXGI_SWAP_CHAIN_DESC1 description{};
    description.Width = 3840;
    description.Height = 2160;
    // Match ReXGlue's current SDR presenter format.
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount = 3;
    description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    ComPtr<IDXGISwapChain1> chain1;
    Require(provider.GetDXGIFactory()->CreateSwapChainForHwnd(
                provider.GetDirectQueue(), window, &description, nullptr,
                nullptr, &chain1),
            "CreateSwapChainForHwnd");
    ComPtr<IDXGISwapChain3> chain;
    Require(chain1.As(&chain), "IDXGISwapChain3");
    chain1.Reset();
    QueueDrain drain(provider);
    ComPtr<ID3D12CommandAllocator> allocator;
    Require(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                           IID_PPV_ARGS(&allocator)),
            "CreateCommandAllocator");
    ComPtr<ID3D12GraphicsCommandList> list;
    Require(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                      allocator.Get(), nullptr,
                                      IID_PPV_ARGS(&list)),
            "CreateCommandList");
    Require(list->Close(), "Close initial list");
    D3D12_DESCRIPTOR_HEAP_DESC heap_description{};
    heap_description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heap_description.NumDescriptors = 1;
    ComPtr<ID3D12DescriptorHeap> heap;
    Require(
        device->CreateDescriptorHeap(&heap_description, IID_PPV_ARGS(&heap)),
        "CreateDescriptorHeap");
    unsigned occluded = 0;
    for (unsigned frame = 0; frame < 12; ++frame) {
      if (frame == 4 || frame == 8) {
        const UINT width = frame == 4 ? 2560 : 3840;
        const UINT height = frame == 4 ? 1440 : 2160;
        Require(chain->ResizeBuffers(3, width, height, description.Format, 0),
                "ResizeBuffers");
      }
      ComPtr<ID3D12Resource> backbuffer;
      Require(chain->GetBuffer(chain->GetCurrentBackBufferIndex(),
                               IID_PPV_ARGS(&backbuffer)),
              "GetBuffer");
      const auto rtv = heap->GetCPUDescriptorHandleForHeapStart();
      device->CreateRenderTargetView(backbuffer.Get(), nullptr, rtv);
      Require(allocator->Reset(), "Reset allocator");
      Require(list->Reset(allocator.Get(), nullptr), "Reset list");
      D3D12_RESOURCE_BARRIER barrier{};
      barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      barrier.Transition.pResource = backbuffer.Get();
      barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
      barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
      list->ResourceBarrier(1, &barrier);
      const float color[] = {0.05f, 0.1f, 0.2f, 1.0f};
      list->ClearRenderTargetView(rtv, color, 0, nullptr);
      std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
      list->ResourceBarrier(1, &barrier);
      Require(list->Close(), "Close list");
      ID3D12CommandList *lists[] = {list.Get()};
      provider.GetDirectQueue()->ExecuteCommandLists(1, lists);
      const HRESULT presented = chain->Present(0, DXGI_PRESENT_RESTART);
      Require(presented, "Present");
      occluded += presented == DXGI_STATUS_OCCLUDED;
      drain.Wait();
    }
    std::printf("PRESENT_TEST calls=12 resizes=2 occluded=%u (hidden window; "
                "FG disabled)\n",
                occluded);
  } catch (...) {
    DestroyWindow(window);
    UnregisterClassW(class_name, window_class.hInstance);
    throw;
  }
  DestroyWindow(window);
  UnregisterClassW(class_name, window_class.hInstance);
}

int Run(int argc, char **argv) {
  if (argc < 2) {
    std::puts("Usage: ssx_streamline_probe <DLL-directory|--disabled> "
              "[--expect-fallback] [--debug] [--skip-ssx-options] "
              "[--evaluate-smoke] [--fg-smoke|--fg-smoke-3x|--fg-hdr-smoke-3x] "
              "[--color-replay <3360x1752-RGBA16F-file>]");
    return 2;
  }
  bool fallback = std::string(argv[1]) == "--disabled";
  bool disabled = fallback;
  bool debug = false;
  bool configure_ssx = true;
  bool evaluate_smoke = false;
  bool fg_smoke = false, hdr_fg_smoke = false;
  uint32_t generated_frames = 1;
  bool fg_color_smoke = false;
  const char* fg_color_replay = nullptr;
  bool token_smoke = false;
  const char* color_replay = nullptr;
  for (int i = 2; i < argc; ++i) {
    if (std::string(argv[i]) == "--evaluate-smoke")
      evaluate_smoke = true;
    else if (std::string(argv[i]) == "--token-smoke")
      token_smoke = true;
    else if (std::string(argv[i]) == "--fg-color-smoke")
      fg_color_smoke = true;
    else if (std::string(argv[i]) == "--fg-color-replay" && i+1<argc)
      fg_color_replay = argv[++i];
    else if (std::string(argv[i]) == "--fg-hdr-smoke-3x") {
      fg_smoke = hdr_fg_smoke = true;
      generated_frames = 2;
    }
    else if (std::string(argv[i]) == "--fg-smoke")
      fg_smoke = true;
    else if (std::string(argv[i]) == "--fg-smoke-3x") {
      fg_smoke = true;
      generated_frames = 2;
    }
    else if (std::string(argv[i]) == "--color-replay" && i+1<argc)
      color_replay = argv[++i];
    else if (std::string(argv[i]) == "--skip-ssx-options")
      configure_ssx = false;
    else if (std::string(argv[i]) == "--expect-fallback")
      fallback = true;
    else if (std::string(argv[i]) == "--debug") {
      debug = true;
      rex::cvar::SetFlagByName("d3d12_debug", "true");
    } else
      return 2;
  }
  rex::cvar::SetFlagByName("d3d12_streamline", disabled ? "false" : "true");
  rex::cvar::SetFlagByName("d3d12_streamline_path", disabled ? "" : argv[1]);
  auto provider = D3D12Provider::Create();
  if (!provider)
    throw std::runtime_error("Provider creation");
  auto *streamline = provider->GetStreamline();
  if (bool(streamline) == fallback)
    throw std::runtime_error("Unexpected Streamline availability");
  std::printf("STREAMLINE connected=%d expected_fallback=%d\n",
              streamline != nullptr, fallback);
  if (streamline) {
    const auto &support = streamline->support();
    std::printf("SUPPORT SR=%s FG=%s Reflex=%s PCL=%s low_latency=%d "
                "max_generated_frames=%u\n",
                support.sr_result.c_str(), support.fg_result.c_str(),
                support.reflex_result.c_str(), support.pcl_result.c_str(),
                support.low_latency_available, support.max_generated_frames);
    const Streamline::Quality qualities[] = {
        Streamline::Quality::kQuality, Streamline::Quality::kBalanced,
        Streamline::Quality::kPerformance,
        Streamline::Quality::kUltraPerformance, Streamline::Quality::kDLAA};
    const char *names[] = {"quality", "balanced", "performance",
                           "ultra_performance", "dlaa"};
    for (unsigned i = 0; i < std::size(qualities); ++i) {
      Streamline::OptimalSettings settings{};
      if (!streamline->GetOptimalSettings(qualities[i], 3840, 2160, settings)) {
        if (support.sr)
          throw std::runtime_error("Optimal settings query failed");
        break;
      }
      std::printf("DLSS_4K mode=%s input=%ux%u min=%ux%u max=%ux%u\n", names[i],
                  settings.width, settings.height, settings.min_width,
                  settings.min_height, settings.max_width, settings.max_height);
    }
    if (configure_ssx) {
      if (!streamline->ConfigureSsxSR(Streamline::Quality::kQuality, 3840,
                                      2160))
        throw std::runtime_error(
            "SSX HDR-linear automatic exposure configuration");
      std::puts("PASS SSX DLSS options: HDR-linear, automatic exposure, "
                "preExposure=1, no evaluation");
    }
    Streamline::OptimalSettings invalid{};
    if (streamline->GetOptimalSettings(Streamline::Quality::kQuality, 0, 2160,
                                       invalid)) {
      throw std::runtime_error("Zero output size was accepted");
    }
  }
  CheckPresentation(*provider);
  if (evaluate_smoke) {
    CheckSsxQualityScale(*provider);
    CheckSREvaluation(*provider);
    CheckSsxSceneReconstruction(*provider);
    const auto module = GetModuleHandleW(L"nvngx_dlss.dll");
    wchar_t filename[32768]{};
    const auto length = module ? GetModuleFileNameW(module, filename, DWORD(std::size(filename))) : 0;
    if (!length || length >= std::size(filename) ||
        !std::filesystem::equivalent(std::filesystem::path(filename),
                                    std::filesystem::path(argv[1]) / "nvngx_dlss.dll"))
      throw std::runtime_error("DLSS evaluation did not load the verified runtime DLL");
    std::printf("DLSS_LOADED_MODULE %s\n",std::filesystem::path(filename).string().c_str());
  }
  if (color_replay)
    CheckSsxSceneReconstruction(*provider, color_replay);
  if (token_smoke) CheckRenderTokens(*provider);
  if (fg_color_smoke || fg_color_replay) CheckFGColor(*provider, fg_color_replay);
  if (fg_smoke)
    CheckFrameGeneration(*provider, generated_frames, hdr_fg_smoke);
  ComPtr<ID3D12InfoQueue> diagnostics;
  if (SUCCEEDED(
          provider->GetDevice()->QueryInterface(IID_PPV_ARGS(&diagnostics)))) {
    unsigned errors = 0;
    for (UINT64 i = 0;
         i < diagnostics->GetNumStoredMessagesAllowedByRetrievalFilter(); ++i) {
      SIZE_T size = 0;
      Require(diagnostics->GetMessage(i, nullptr, &size), "GetMessage size");
      std::vector<uint8_t> storage(size);
      auto *message = reinterpret_cast<D3D12_MESSAGE *>(storage.data());
      Require(diagnostics->GetMessage(i, message, &size), "GetMessage");
      if (message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) {
        ++errors;
        std::printf("D3D12_ERROR %s\n", message->pDescription);
      }
    }
    std::printf("D3D12_VALIDATION errors=%u\n", errors);
    diagnostics.Reset();
    if (errors)
      throw std::runtime_error("D3D12 validation failed");
  } else if (debug) {
    throw std::runtime_error("D3D12 debug validation is unavailable");
  }
  // Also exercise provider teardown while the logger is still available.
  provider.reset();
  std::printf("PASS SDK connection/presentation/teardown; SR smoke=%d; FG smoke=%d\n",
              evaluate_smoke, fg_smoke);
  return 0;
}
} // namespace

int main(int argc, char **argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  rex::InitLogging("streamline-probe.log");
  int result = 1;
  try {
    result = Run(argc, argv);
  } catch (const std::exception &error) {
    std::printf("FAIL %s\n", error.what());
  }
  rex::ShutdownLogging();
  return result;
}
