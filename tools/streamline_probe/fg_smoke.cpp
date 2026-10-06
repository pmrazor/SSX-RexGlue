// Real DLAA + DLSS-G presentation of an analytic moving scene. No game assets.
// This deliberately owns a visible window: occluded Presents do not prove FG.
#include <DirectXMath.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <rex/ui/d3d12/d3d12_provider.h>
#include <set>
#include <stdexcept>
#include <thread>

using namespace rex::ui::d3d12;
using Microsoft::WRL::ComPtr;
namespace {
void Require(bool ok, const char *name) {
  if (!ok)
    throw std::runtime_error(name);
}
void Check(HRESULT hr, const char *name) { Require(SUCCEEDED(hr), name); }
void Transition(ID3D12GraphicsCommandList *list, ID3D12Resource *r,
                D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
  D3D12_RESOURCE_BARRIER b{};
  b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  b.Transition = {r, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, from, to};
  list->ResourceBarrier(1, &b);
}
constexpr auto kRead = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                       D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
constexpr auto kUav = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
constexpr auto kComputeRead = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
constexpr const char *kShader = R"(
cbuffer Parameters : register(b0) {
  uint width, height; float object_x, previous_x;
  float jitter_x, jitter_y; uint post, reset;
};
RWTexture2D<float4> scene : register(u0);
RWTexture2D<float> depth : register(u1);
RWTexture2D<float2> motion : register(u2);
RWTexture2D<float4> hudless : register(u3);
RWTexture2D<float> ui : register(u4);
RWTexture2D<float4> final_color : register(u5);
Texture2D<float4> reconstructed : register(t0);
float3 PQ(float3 nits) {
  float3 y=pow(max(nits,0)/10000,2610.0/16384);
  return pow((3424.0/4096+2413.0/128*y)/(1+2392.0/128*y),2523.0/32);
}
float3 HDR10(float3 c) {
  float3 nits=c*200;
  return PQ(mul(float3x3(.62740390,.32928304,.04331307,
                         .06909729,.91954040,.01136232,
                         .01639144,.08801331,.89559525),nits));
}
[numthreads(8,8,1)] void main(uint3 id : SV_DispatchThreadID) {
  if (id.x >= width || id.y >= height) return;
  if (post == 0) {
    float2 p = float2(id.xy) + .5 - float2(jitter_x, jitter_y);
    bool object = abs(p.x - object_x) < width * .09 && abs(p.y - height * .52) < height * .16;
    float grid = fmod(floor(p.x / 24) + floor(p.y / 24), 2);
    float3 c = object ? float3(.8, .12 + grid * .2, .06) :
                         float3(.04, .16, .3) + grid * .05 + p.y / height * .3;
    scene[id.xy] = float4(c,1);
    depth[id.xy] = object ? .7 : .99;
    motion[id.xy] = float2(object && !reset ? previous_x - object_x : 0, 0);
  } else {
    float3 c = sqrt(saturate(reconstructed.Load(int3(id.xy,0)).rgb));
    float a = id.x > width/20 && id.x < width/3 && id.y > height/20 && id.y < height/9 ? .85 : 0;
    float3 ui_color=float3(.04,.8,.9);
#if HDR10_TEST
    c=HDR10(c); ui_color=HDR10(ui_color);
#endif
    hudless[id.xy] = float4(c,1);
    ui[id.xy] = a;
    final_color[id.xy] = float4(ui_color * a + c * (1-a),1);
  }
})";

struct Harness {
  D3D12Provider &provider;
  Streamline &sl;
  HWND window = nullptr;
  ComPtr<IDXGISwapChain3> chain;
  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> list;
  ComPtr<ID3D12Fence> fence;
  ComPtr<ID3D12DescriptorHeap> heap;
  ComPtr<ID3D12RootSignature> root;
  ComPtr<ID3D12PipelineState> pipeline;
  std::array<ComPtr<ID3D12Resource>, 7> textures;
  uint64_t submission = 0, frame_id = 0;
  uint32_t generated_frames = 1;
  uint32_t width = 3840, height = 2160;
  bool list_open = false, hdr10 = false;
  DXGI_FORMAT ColorFormat() const { return hdr10 ? DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM; }
  void ColorSpace() {
    if (!hdr10) return;
    UINT supported=0;
    Check(chain->CheckColorSpaceSupport(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020,&supported),"HDR10 support query");
    Require(supported & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT,"HDR10 presentation support");
    Check(chain->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020),"HDR10 color space");
    ComPtr<IDXGISwapChain4> chain4;
    Check(chain.As(&chain4),"HDR10 chain4");
    DXGI_HDR_METADATA_HDR10 m{};
    m.RedPrimary[0]=35400;m.RedPrimary[1]=14600;
    m.GreenPrimary[0]=8500;m.GreenPrimary[1]=39850;
    m.BluePrimary[0]=6550;m.BluePrimary[1]=2300;
    m.WhitePoint[0]=15635;m.WhitePoint[1]=16450;
    m.MaxMasteringLuminance=m.MaxContentLightLevel=1000;
    Check(chain4->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10,sizeof(m),&m),"HDR10 metadata");
  }
  std::set<uint64_t> completed_tokens, checked_reports;
  std::ofstream latency_csv{"reflex-reports.csv"};
  void ReadLatencyReports() {
    Streamline::ReflexState state;
    Require(sl.GetReflexState(state), "Reflex driver report query");
    for (const auto &r : state.reports) {
      if (!state.reports_available || !completed_tokens.contains(r.frame_id) ||
          !r.simulation_start_us || !r.simulation_end_us ||
          !r.render_start_us || !r.render_end_us || !r.present_start_us ||
          !r.present_end_us || checked_reports.contains(r.frame_id))
        continue;
      // Driver reports have microsecond timestamps. Adjacent back-to-back
      // markers have been observed to reverse by 1 us on driver 617.14.
      // Preserve raw values in CSV; tolerate 2 us, never millisecond
      // inversions.
      const std::array<uint64_t, 6> times = {
          r.simulation_start_us, r.simulation_end_us, r.render_start_us,
          r.render_end_us,       r.present_start_us,  r.present_end_us};
      for (size_t i = 1; i < times.size(); ++i)
        Require(times[i] + 2 >= times[i - 1],
                "Reflex driver marker order (2 us tolerance)");
      checked_reports.insert(r.frame_id);
      latency_csv << r.frame_id << ',' << r.simulation_start_us << ','
                  << r.simulation_end_us << ',' << r.render_start_us << ','
                  << r.render_end_us << ',' << r.present_start_us << ','
                  << r.present_end_us << ',' << r.driver_start_us << ','
                  << r.driver_end_us << ',' << r.gpu_start_us << ','
                  << r.gpu_end_us << ',' << r.gpu_active_us << '\n';
    }
  }
  explicit Harness(D3D12Provider &p, bool hdr = false) : provider(p), sl(*p.GetStreamline()), hdr10(hdr) {
    latency_csv << "frame_id,simulation_start_us,simulation_end_us,render_"
                   "start_us,render_end_us,"
                   "present_start_us,present_end_us,driver_start_us,driver_end_"
                   "us,gpu_start_us,gpu_end_us,gpu_active_us\n";
    auto *d = p.GetDevice();
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"SSXFGValidation";
    Require(RegisterClassW(&wc) != 0, "FG window class");
    window = CreateWindowExW(
        0, wc.lpszClassName, L"SSX DLAA + Frame Generation validation",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 800, nullptr,
        nullptr, wc.hInstance, nullptr);
    Require(window != nullptr, "FG window");
    ShowWindow(window, SW_SHOWNORMAL);
    // A console test runner may supply STARTF_USESHOWWINDOW=SW_HIDE. Its first
    // ShowWindow override must not silently turn this into an occluded test.
    ShowWindow(window, SW_SHOW);
    Check(d->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                    IID_PPV_ARGS(&allocator)),
          "FG allocator");
    Check(d->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                               allocator.Get(), nullptr, IID_PPV_ARGS(&list)),
          "FG list");
    Check(list->Close(), "FG initial close");
    Check(d->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)),
          "FG fence");
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = 7;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    Check(d->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap)),
          "FG descriptor heap");
    D3D12_DESCRIPTOR_RANGE ranges[] = {
        {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 6, 0, 0, 0},
        {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 6}};
    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants = {0, 0, 8};
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable = {2, ranges};
    D3D12_ROOT_SIGNATURE_DESC rd{};
    rd.NumParameters = 2;
    rd.pParameters = params;
    ComPtr<ID3DBlob> blob, errors, shader;
    Check(D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob,
                                      &errors),
          "FG root serialization");
    Check(d->CreateRootSignature(0, blob->GetBufferPointer(),
                                 blob->GetBufferSize(), IID_PPV_ARGS(&root)),
          "FG root");
    const D3D_SHADER_MACRO defines[]={{"HDR10_TEST",hdr10 ? "1" : "0"},{nullptr,nullptr}};
    Check(D3DCompile(kShader, strlen(kShader), "analytic-fg", defines, nullptr,
                     "main", "cs_5_1", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                     &shader, &errors),
          "FG shader");
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = root.Get();
    pd.CS = {shader->GetBufferPointer(), shader->GetBufferSize()};
    Check(d->CreateComputePipelineState(&pd, IID_PPV_ARGS(&pipeline)),
          "FG pipeline");
  }
  ~Harness() {
    try {
      Drain();
      sl.SetFrameGenerationActive(false);
      chain.Reset();
      sl.ReleaseSR();
    } catch (...) {
      std::puts("FAIL FG cleanup");
    }
    if (window)
      DestroyWindow(window);
    UnregisterClassW(L"SSXFGValidation", GetModuleHandleW(nullptr));
  }
  void Drain() {
    if (list_open) {
      Check(list->Close(), "FG emergency close");
      ID3D12CommandList *lists[] = {list.Get()};
      provider.GetDirectQueue()->ExecuteCommandLists(1, lists);
      list_open = false;
    }
    Check(provider.GetDirectQueue()->Signal(fence.Get(), ++submission),
          "FG signal");
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    Require(event != nullptr, "FG event");
    auto hr = fence->SetEventOnCompletion(submission, event);
    auto result =
        SUCCEEDED(hr) ? WaitForSingleObject(event, 10000) : WAIT_FAILED;
    CloseHandle(event);
    Require(result == WAIT_OBJECT_0, "FG GPU timeout");
  }
  void CreateChain(bool fg) {
    Drain();
    Require(sl.SetFrameGenerationActive(false),
            "FG suspend before chain change");
    chain.Reset();
    Require(sl.SetFrameGenerationLoaded(fg), "FG feature load/unload");
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = width;
    desc.Height = height;
    desc.Format = ColorFormat();
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 3;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    ComPtr<IDXGISwapChain1> first;
    Check(
        provider.GetDXGIFactory()->CreateSwapChainForHwnd(
            provider.GetDirectQueue(), window, &desc, nullptr, nullptr, &first),
        "FG swap chain");
    Check(first.As(&chain), "FG swap chain 3");
    ColorSpace();
    Require(sl.ConfigureReflex(fg ? Streamline::ReflexMode::kOn
                                  : Streamline::ReflexMode::kOff,
                               16667),
            "FG Reflex configuration");
    Require(sl.SetFrameGenerationActive(fg, generated_frames), "FG activation");
  }
  void WaitForFocus() {
    std::puts("FG_WAITING_FOR_FOCUS: activate the SSX DLAA + Frame Generation "
              "validation window");
    const auto deadline = GetTickCount64() + 45000;
    while (GetForegroundWindow() != window) {
      MSG msg{};
      while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
      }
      Require(IsWindow(window) && GetTickCount64() < deadline,
              "FG window was not focused before timeout");
      MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT);
    }
    std::puts("FG_FOCUSED");
  }
  void Resources() {
    Drain();
    Require(sl.ReleaseSR(), "FG release DLAA");
    auto *d = provider.GetDevice();
    const DXGI_FORMAT formats[] = {
        DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R32_FLOAT,
        DXGI_FORMAT_R32G32_FLOAT,       ColorFormat(),
        DXGI_FORMAT_R8_UNORM,           ColorFormat(),
        DXGI_FORMAT_R16G16B16A16_FLOAT};
    auto cpu = heap->GetCPUDescriptorHandleForHeapStart();
    const auto stride = d->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    for (unsigned i = 0; i < textures.size(); ++i) {
      textures[i].Reset();
      D3D12_HEAP_PROPERTIES props{};
      props.Type = D3D12_HEAP_TYPE_DEFAULT;
      D3D12_RESOURCE_DESC desc{};
      desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
      desc.Width = width;
      desc.Height = height;
      desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
      desc.Format = formats[i];
      desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
      Check(d->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc,
                                       kUav, nullptr,
                                       IID_PPV_ARGS(&textures[i])),
            "FG resource");
      if (i < 6)
        d->CreateUnorderedAccessView(textures[i].Get(), nullptr, nullptr, cpu);
      else {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = formats[i];
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        d->CreateShaderResourceView(textures[i].Get(), &srv, cpu);
      }
      cpu.ptr += stride;
    }
    Require(sl.ConfigureSsxSR(Streamline::Quality::kDLAA, width, height),
            "FG DLAA configuration");
  }
  Streamline::SRCamera Camera() {
    using namespace DirectX;
    Streamline::SRCamera c{};
    auto proj =
        XMMatrixPerspectiveFovLH(1.0f, float(width) / height, .1f, 100.0f);
    XMFLOAT4X4 m;
    XMStoreFloat4x4(&m, proj);
    memcpy(c.view_to_clip.data(), &m, sizeof(m));
    XMStoreFloat4x4(&m, XMMatrixInverse(nullptr, proj));
    memcpy(c.clip_to_view.data(), &m, sizeof(m));
    for (int i = 0; i < 4; ++i)
      c.clip_to_previous[i * 5] = c.previous_to_clip[i * 5] = 1;
    c.up = {0, 1, 0};
    c.right = {1, 0, 0};
    c.forward = {0, 0, 1};
    c.near_plane = .1f;
    c.far_plane = 100;
    c.vertical_fov = 1;
    c.aspect = float(width) / height;
    return c;
  }
  uint32_t Draw(bool loaded, bool active, bool reset,
                bool reject_extent = false, bool token_only = false) {
    Require(GetForegroundWindow() == window, "FG validation lost foreground focus");
    std::shared_ptr<Streamline::Frame> token;
    if (token_only) {
      std::thread producer([&] { token = sl.AcquireRenderToken(frame_id + 1); });
      producer.join();
    } else token = sl.BeginFrame();
    Require(bool(token), "FG begin simulation");
    uint32_t token_id = 0;
    Require(sl.GetFrameId(token, token_id), "Reflex stable frame identity");
    uint64_t guest_render_id = ~uint64_t(0);
    Require(!sl.GetFrameRenderId(nullptr, guest_render_id) && !guest_render_id,
            "Missing token cannot inherit a scene identity");
    if (token_only)
      Require(sl.GetFrameRenderId(token, guest_render_id) && guest_render_id == frame_id + 1,
              "FG scene identity comes from ordered render token");
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT)
        throw std::runtime_error("FG window closed during validation");
      if (!token_only && msg.message == sl.LatencyPingMessage())
        Require(sl.MarkFrame(token, Streamline::Marker::kLatencyPing),
                "FG latency ping");
      if (!token_only && msg.message == WM_LBUTTONDOWN)
        Require(sl.MarkFrame(token, Streamline::Marker::kInputFlash),
                "FG input flash");
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    // Actual analytic simulation, between its markers. The foreground
    // translates 4 pixels per frame; background and camera are static. Camera
    // cuts reset it.
    const float x = float(width) * .4f + float(frame_id % 80) * 4;
    const float previous_x = reset || frame_id % 80 == 0 ? x : x - 4;
    if (!token_only) {
    Require(sl.MarkFrame(token, Streamline::Marker::kSimulationEnd),
            "FG simulation end");
    Require(!sl.MarkFrame(token, Streamline::Marker::kPresentStart),
            "FG marker order rejection");
    Require(sl.MarkFrame(token, Streamline::Marker::kRenderSubmitStart),
            "FG render start");
    } else Require(!sl.MarkFrame(token, Streamline::Marker::kSimulationEnd), "render-only token rejects simulation marker");
    Check(allocator->Reset(), "FG allocator reset");
    Check(list->Reset(allocator.Get(), nullptr), "FG list reset");
    list_open = true;
    struct Constants {
      uint32_t w, h;
      float x, old_x, jx, jy;
      uint32_t post, reset;
    } constants{width,
                height,
                x,
                previous_x,
                frame_id % 2 ? -.25f : .25f,
                frame_id % 4 < 2 ? -.25f : .25f,
                0,
                reset || frame_id % 80 == 0};
    auto dispatch = [&] {
      ID3D12DescriptorHeap *heaps[] = {heap.Get()};
      list->SetDescriptorHeaps(1, heaps);
      list->SetComputeRootSignature(root.Get());
      list->SetPipelineState(pipeline.Get());
      list->SetComputeRoot32BitConstants(0, 8, &constants, 0);
      list->SetComputeRootDescriptorTable(
          1, heap->GetGPUDescriptorHandleForHeapStart());
      list->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
    };
    Transition(list.Get(), textures[6].Get(), kUav, kComputeRead);
    dispatch();
    Transition(list.Get(), textures[6].Get(), kComputeRead, kUav);
    for (unsigned i = 0; i < 3; ++i)
      Transition(list.Get(), textures[i].Get(), kUav, kComputeRead);
    Streamline::SRFrame sr{};
    sr.id = ++frame_id;
    sr.timeline = token;
    sr.camera = Camera();
    sr.jitter_pixels = {constants.jx, constants.jy};
    sr.reset = constants.reset;
    sr.color = textures[0].Get();
    sr.depth = textures[1].Get();
    sr.motion = textures[2].Get();
    sr.output = textures[6].Get();
    Require(sl.EvaluateSR(list.Get(), sr).recorded,
            "FG shared-token DLAA evaluation");
    Transition(list.Get(), textures[6].Get(), kUav, kComputeRead);
    for (unsigned i = 0; i < 3; ++i)
      Transition(list.Get(), textures[i].Get(), kComputeRead, kUav);
    constants.post = 1;
    dispatch();
    for (unsigned i = 0; i < 3; ++i)
      Transition(list.Get(), textures[i].Get(), kUav, kComputeRead);
    for (unsigned i = 3; i < 5; ++i)
      Transition(list.Get(), textures[i].Get(), kUav, kRead);
    Transition(list.Get(), textures[5].Get(), kUav,
               D3D12_RESOURCE_STATE_COPY_SOURCE);
    ComPtr<ID3D12Resource> buffer;
    Check(chain->GetBuffer(chain->GetCurrentBackBufferIndex(),
                           IID_PPV_ARGS(&buffer)),
          "FG current backbuffer");
    Transition(list.Get(), buffer.Get(), D3D12_RESOURCE_STATE_PRESENT,
               D3D12_RESOURCE_STATE_COPY_DEST);
    list->CopyResource(buffer.Get(), textures[5].Get());
    Transition(list.Get(), buffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
               D3D12_RESOURCE_STATE_PRESENT);
    if (active) {
      Streamline::FGFrame fg{};
      fg.camera = sr.camera;
      fg.jitter_pixels = sr.jitter_pixels;
      fg.reset = sr.reset;
      fg.depth = sr.depth;
      fg.motion = sr.motion;
      fg.hudless = textures[3].Get();
      fg.hdr10 = hdr10;
      // Match gameplay's HUD-free-only extraction path for HDR. A scalar UI
      // alpha cannot describe SSX's colored multiply HUD after PQ encoding.
      fg.ui_alpha = hdr10 ? nullptr : textures[4].Get();
      if (hdr10 && sr.reset) {
        fg.hdr10 = false;
        Require(!sl.TagFrameGeneration(list.Get(), token, fg, width, height), "reject PQ color declared SDR");
        fg.hdr10 = true;
        // Rejection resets FG tags; this negative check needs shared SR reset.
      }
      if (reject_extent) {
        Require(
            !sl.TagFrameGeneration(list.Get(), token, fg, width - 1, height),
            "FG extent rejection");
        Require(sl.SetFrameGenerationActive(false),
                "FG suspend rejected frame");
      } else
        Require(sl.TagFrameGeneration(list.Get(), token, fg, width, height),
                "FG tag inputs");
    } else if (!token_only)
      Require(sl.ClearFrameGenerationTags(list.Get(), token),
              "FG clear paused/fallback inputs");
    Check(list->Close(), "FG close");
    list_open = false;
    ID3D12CommandList *lists[] = {list.Get()};
    provider.GetDirectQueue()->ExecuteCommandLists(1, lists);
    if (token_only) {
      const auto presented = sl.PresentRenderToken(token, [&] { return chain->Present(0, 0); });
      Require(presented.matched && presented.token_id == token_id &&
                  presented.render_id == frame_id && presented.result == S_OK,
              "ordered render token actual Present");
      Require(!sl.GetFrameRenderId(token, guest_render_id) && !guest_render_id,
              "Retired token cannot repeat a scene decision");
      unsigned calls = 0;
      const auto repeated = sl.PresentRenderToken(token, [&] { ++calls; return S_FALSE; });
      Require(!repeated.matched && calls == 1 && repeated.result == S_FALSE,
              "repeated token must call fallback once without claiming a new frame");
    } else {
    Require(sl.MarkFrame(token, Streamline::Marker::kRenderSubmitEnd),
            "FG render end");
    Require(sl.MarkFrame(token, Streamline::Marker::kPresentStart),
            "FG present start");
    const auto result =
        chain->Present(0, 0); // No RESTART: preserve FG's frame pacing.
    Require(sl.MarkFrame(token, Streamline::Marker::kPresentEnd),
            "FG present end");
    Require(result == S_OK,
            "FG Present must be visible, successful, non-occluded");
    completed_tokens.insert(token_id);
    ReadLatencyReports();
    }
    Streamline::FGState state{};
    if (loaded) {
      Require(sl.GetFrameGenerationState(state), "FG state");
      Require(state.status == 0, "FG state reported an error");
      if (frame_id < 13)
        std::printf("FG_INPUT_FENCE frame=%llu active=%d present=%u fence=%d value=%llu\n",
                    frame_id, active, state.frames_presented, bool(state.inputs_fence),
                    state.inputs_fence_value);
      if (state.inputs_fence && state.inputs_fence_value)
        Check(provider.GetDirectQueue()->Wait(state.inputs_fence.Get(),
                                              state.inputs_fence_value),
              "FG inputs fence wait");
    }
    Drain();
    Check(allocator->Reset(), "FG restore allocator");
    Check(list->Reset(allocator.Get(), nullptr), "FG restore list");
    list_open = true;
    for (unsigned i = 0; i < 3; ++i)
      Transition(list.Get(), textures[i].Get(), kComputeRead, kUav);
    for (unsigned i = 3; i < 5; ++i)
      Transition(list.Get(), textures[i].Get(), kRead, kUav);
    Transition(list.Get(), textures[5].Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
               kUav);
    Transition(list.Get(), textures[6].Get(), kComputeRead, kUav);
    Drain();
    return token_only ? 1 : state.frames_presented;
  }
};
} // namespace

void CheckFrameGeneration(D3D12Provider &provider, uint32_t generated_frames, bool hdr10) {
  auto *sl = provider.GetStreamline();
  Require(sl && sl->support().fg && sl->support().reflex && sl->support().pcl,
          "FG support");
  Harness h(provider, hdr10);
  std::printf("FG_COLOR_SPACE %s\n",hdr10 ? "HDR10_PQ_2020" : "SDR");
  h.generated_frames = generated_frames;
  Require(generated_frames <= sl->support().max_generated_frames, "Requested MFG capability");
  h.WaitForFocus();
  h.CreateChain(false);
  h.Resources();
  Require(!sl->SetFrameGenerationActive(true),
          "FG rejects unloaded / Reflex Off activation");
  for (unsigned i = 0; i < 8; ++i)
    h.Draw(false, false, i == 0);
  h.CreateChain(true);
  Require(!sl->SetFrameGenerationActive(true, 0), "FG rejects zero generated frames");
  Require(!sl->SetFrameGenerationActive(true, sl->support().max_generated_frames + 1),
          "FG rejects unsupported multiplier without changing the active mode");
  Require(!sl->ConfigureReflex(Streamline::ReflexMode::kOff),
          "FG rejects disabling required Reflex");
  // Exclude feature allocation / first-use pipeline compilation from the
  // generated-frame rate assertion. Keep its correctness checks active.
  for (unsigned i = 0; i < 30; ++i) h.Draw(true, true, i == 0);
  uint32_t presented = 0, rendered = 0, reset_frames = 0, suspended = 0;
  for (unsigned i = 0; i < 120; ++i) {
    const bool active = !(i >= 48 && i < 56);
    if (i == 48 || i == 56 || i == 73)
      Require(sl->SetFrameGenerationActive(active, generated_frames), "FG pause/resume");
    const bool reset = i == 0 || i == 32 || i == 56 || i == 73;
    const auto count = h.Draw(true, active, reset, i == 72);
    if (active && i != 72) {
      presented += count;
      ++rendered;
    } else {
      Require(count <= 1, "FG generated while suspended");
      ++suspended;
    }
    reset_frames += reset;
  }
  std::printf("FG_GPU multiplier=%u 3840x2160 rendered=%u presented=%u suspended=%u "
              "explicit_resets=%u status=0\n",
              generated_frames + 1, rendered, presented, suspended, reset_frames);
  Require(presented > rendered * generated_frames + rendered / 2,
          "FG did not generate enough real presented frames");
  h.Drain();
  Require(sl->SetFrameGenerationActive(false), "FG suspend for resize");
  h.width = 2560;
  h.height = 1440;
  Check(h.chain->ResizeBuffers(3, h.width, h.height, h.ColorFormat(),
                               0),
        "FG ResizeBuffers");
  h.ColorSpace();
  h.Resources();
  Require(sl->SetFrameGenerationActive(true, generated_frames), "FG resume after resize");
  h.WaitForFocus();
  for (unsigned i = 0; i < 20; ++i) h.Draw(true, true, i == 0);
  uint32_t resized = 0;
  for (unsigned i = 0; i < 40; ++i)
    resized += h.Draw(true, true, i == 0);
  std::printf("FG_GPU resized=2560x1440 rendered=40 presented=%u status=0\n",
              resized);
  Require(resized > 40 * generated_frames + 20, "FG failed after resize");
  if (generated_frames > 1) {
    h.Drain();
    Require(sl->SetFrameGenerationActive(false), "MFG suspend before 2x fallback");
    Require(sl->SetFrameGenerationActive(true, 1), "MFG switch back to 2x");
    for (unsigned i = 0; i < 20; ++i) h.Draw(true, true, i == 0);
    uint32_t fallback_presented = 0;
    for (unsigned i = 0; i < 40; ++i) fallback_presented += h.Draw(true, true, i == 0);
    std::printf("FG_GPU fallback_multiplier=2 rendered=40 presented=%u status=0\n", fallback_presented);
    Require(fallback_presented > 60 && fallback_presented <= 80, "MFG 2x fallback count");
  }
  h.CreateChain(false);
  h.WaitForFocus();
  for (auto mode : {Streamline::ReflexMode::kOff, Streamline::ReflexMode::kOn,
                    Streamline::ReflexMode::kOnWithBoost}) {
    Require(sl->ConfigureReflex(mode, 16667), "Reflex mode switch without FG");
    for (unsigned i = 0; i < 16; ++i)
      h.Draw(false, false, i == 0);
  }
  h.ReadLatencyReports();
  h.latency_csv.flush();
  std::printf(
      "REFLEX_DRIVER completed_tokens=%zu matched_ordered_reports=%zu\n",
      h.completed_tokens.size(), h.checked_reports.size());
  Require(h.checked_reports.size() >= 16,
          "Reflex driver returned too few complete matching reports");
  std::printf("PASS real DLAA + FG, Reflex markers, %s, pause/resume, "
              "history reset, invalid extent, resize, unloaded fallback\n",
              hdr10 ? "HDR10 HUD-free color" : "HUD alpha");
}

void CheckRenderTokens(D3D12Provider& provider) {
  auto* sl = provider.GetStreamline();
  Require(sl && sl->support().sr, "render token SR support");
  Harness h(provider);
  SetWindowTextW(h.window, L"SSX NVIDIA frame-token validation (Reflex and FG off)");
  h.WaitForFocus();
  h.CreateChain(false);
  h.Resources();
  std::array<std::shared_ptr<Streamline::Frame>, 4> pending;
  for (size_t i = 0; i < pending.size(); ++i) {
    pending[i] = sl->AcquireRenderToken(1000 + i);
    Require(bool(pending[i]), "bounded render token creation");
  }
  unsigned calls = 0;
  const auto replaced = sl->PresentRenderToken(pending[0], [&] { ++calls; return S_OK; });
  Require(!replaced.matched && calls == 1, "replaced slot must not claim a Present");
  for (auto& token : pending) {
    Require(!sl->MarkFrame(token, Streamline::Marker::kSimulationEnd), "no fabricated simulation markers");
    sl->RetireRenderToken(token);
  }
  const auto missing = sl->PresentRenderToken(nullptr, [&] { ++calls; return DXGI_STATUS_OCCLUDED; });
  Require(!missing.matched && calls == 2 && missing.result == DXGI_STATUS_OCCLUDED,
          "missing token preserves actual Present result");
  for (unsigned i = 0; i < 64; ++i) h.Draw(false, false, i == 0, false, true);
  h.Drain();
  h.width = 2560; h.height = 1440;
  Check(h.chain->ResizeBuffers(3, h.width, h.height, DXGI_FORMAT_R8G8B8A8_UNORM, 0), "token resize");
  h.Resources();
  for (unsigned i = 0; i < 16; ++i) h.Draw(false, false, i == 0, false, true);
  std::puts("PASS RENDER_TOKENS 80 shared-token DLAA/actual Present frames, 80 repeat rejections, "
            "bounded drops, missing input, cross-thread allocation and resize; gameplay Reflex/FG off");
}
