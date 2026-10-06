// Read-only adapter/output probe. No display settings or HDR modes are changed.
#include <d3d12.h>
#include <dxgi1_6.h>
#include <windows.h>
#include <wrl/client.h>

#include <cstdio>

using Microsoft::WRL::ComPtr;

int main() {
  ComPtr<IDXGIFactory1> factory;
  HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
  if (FAILED(hr)) {
    std::printf("CreateDXGIFactory1 failed: 0x%08lx\n", static_cast<unsigned long>(hr));
    return 1;
  }
  for (UINT a = 0;; ++a) {
    ComPtr<IDXGIAdapter1> adapter;
    hr = factory->EnumAdapters1(a, &adapter);
    if (hr == DXGI_ERROR_NOT_FOUND) break;
    if (FAILED(hr)) return 1;
    DXGI_ADAPTER_DESC1 desc{};
    adapter->GetDesc1(&desc);
    std::printf("ADAPTER index=%u name=%ls vendor=0x%04x vram_mib=%llu\n", a, desc.Description,
                desc.VendorId,
                static_cast<unsigned long long>(desc.DedicatedVideoMemory / (1024 * 1024)));
    ComPtr<ID3D12Device> device;
    if (SUCCEEDED(
            D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
      D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
      if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options,
                                                sizeof(options)))) {
        std::printf("D3D12 rov_supported=%d resource_binding_tier=%u\n", options.ROVsSupported,
                    static_cast<unsigned>(options.ResourceBindingTier));
      }
    }
    for (UINT o = 0;; ++o) {
      ComPtr<IDXGIOutput> output;
      hr = adapter->EnumOutputs(o, &output);
      if (hr == DXGI_ERROR_NOT_FOUND) break;
      if (FAILED(hr)) return 1;
      ComPtr<IDXGIOutput6> output6;
      DXGI_OUTPUT_DESC1 display{};
      if (FAILED(output.As(&output6)) || FAILED(output6->GetDesc1(&display))) continue;
      DISPLAY_DEVICEW monitor{};
      monitor.cb = sizeof(monitor);
      EnumDisplayDevicesW(display.DeviceName, 0, &monitor, 0);
      DEVMODEW mode{};
      mode.dmSize = sizeof(mode);
      const bool has_mode = EnumDisplaySettingsW(display.DeviceName, ENUM_CURRENT_SETTINGS, &mode);
      std::printf("OUTPUT index=%u device=%ls monitor=%ls attached=%d\n", o, display.DeviceName,
                  monitor.DeviceString, display.AttachedToDesktop);
      if (has_mode) {
        std::printf("DESKTOP width=%lu height=%lu refresh_hz=%lu\n", mode.dmPelsWidth,
                    mode.dmPelsHeight, mode.dmDisplayFrequency);
      }
      std::printf("OUTPUT_COLOR bits_per_color=%u dxgi_color_space=%u desktop_pq2020=%s\n",
                  display.BitsPerColor, static_cast<unsigned>(display.ColorSpace),
                  display.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 ? "yes" : "no");
      std::printf("REPORTED_LUMINANCE min=%.4f max=%.1f full_frame=%.1f nits\n",
                  display.MinLuminance, display.MaxLuminance, display.MaxFullFrameLuminance);
    }
  }
  std::puts("Reported luminance is OS/driver metadata, not a measured calibration.");
  return 0;
}
