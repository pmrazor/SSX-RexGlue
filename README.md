# SSX (2012) — rexglue static recompilation

[![Play Video](https://img.youtube.com/vi/cT2P-EIpZzY/0.jpg)](https://www.youtube.com/watch?v=cT2P-EIpZzY)

A native Windows build of **SSX** (Xbox 360, 2012), made by
statically recompiling the game's PowerPC code to C++ with
[rexglue-sdk](https://github.com/rexglue/rexglue-sdk) v0.10.0. Graphics, audio
and the kernel still go through rexglue's runtime (derived from Xenia).

> setup, hand-written hooks, tools and SDK fixes. You need your own copy of the
> game; the recompiled C++ (`generated/`) is produced locally from your
> `default.xex` and must not be redistributed.

## Status

- Boots, plays races, saves progress (tested on Windows 11, RTX 4070).
- 120 fps at 1x internal resolution (the game is locked to 30/60 fps on console).
- 2x internal resolution (2560x1440) with the ROV render path: about 70–90 fps in
  races, 120 fps in menus.
- Known issues: occasional UI texture glitches at 2x; online features (RiderNet)
  are gone with EA's servers.
- Linux and macOS: untested. rexglue supports both through its Vulkan backend
  (MoltenVK on macOS). On Windows the Vulkan backend (`--gpu_backend=vulkan`)
  renders menus and races correctly at 1x, at about 95–115 fps in races on an
  RTX 4070 (D3D12 holds 120). The 2x resolution fixes below are D3D12-only, so
  use 1x there.

## Requirements

- Windows 10/11 x64, a Direct3D 12 GPU
- Visual Studio 2022 (MSVC toolset + Windows SDK), LLVM/clang 20+, CMake 3.25+,
  Ninja, Python 3
- Your own SSX disc image, extracted so that `default.xex` and the `.big`
  archives are in `game/`

## Build

1. **rexglue-sdk with the SSX fixes.** Clone rexglue-sdk v0.10.0 and apply
   `patches/rexglue-sdk-ssx-fixes.patch` (`git am`). On Windows, git checks out
   libmspack's symlinks as text files; replace the stubs in
   `thirdparty/libmspack/cabextract/mspack` with copies of their targets. Then:
   ```
   cmake --preset win-amd64
   cmake --build out/build/win-amd64 --config Release --target install
   ```
2. **Recompile the game code.** From this directory, run the SDK's `rexglue`
   tool (in the SDK install's `bin/`) on the manifest. It reads
   `game/default.xex` and writes `generated/` (~4 minutes):
   ```
   rexglue codegen ssx_manifest.toml
   ```
3. **This project** (from an environment with `vcvars64.bat`, LLVM, CMake and
   Ninja on `PATH`):
   ```
   cmake --preset win-amd64-release
   cmake --build out/build/win-amd64-release
   ```
4. Copy the SDK's runtime DLLs (`rexruntime.dll`, `rexgpu-xenos.dll`,
   `TracyClient.dll`) next to `ssx.exe` if the build didn't. If the SDK was
   configured with `-DREXGLUE_ENABLE_FIDELITYFX=ON`, `amd_fidelityfx_dx12.dll`
   is required too.

### Linux / macOS

Same steps with the SDK's `linux-amd64`, `linux-arm64`, `mac-amd64` or
`mac-arm64` preset (Vulkan only; macOS needs Xcode 16+), then this project's
matching `*-release` preset. The project's Linux presets expect `clang-20`;
override `CMAKE_C_COMPILER`/`CMAKE_CXX_COMPILER` for other versions. The SDK
copies the Vulkan loader and MoltenVK next to the executable on macOS. Run with
`scripts/play_1x_120fps.sh`.

To try the Vulkan renderer on Windows, configure the SDK with
`-DREXGLUE_USE_VULKAN=ON` and run with `--gpu_backend=vulkan`.

## Run

Copy a launcher from `scripts/` next to `ssx.exe` and put (or link) the
extracted game folder next to it as `game`:

| Launcher | Internal resolution | Render path | Notes |
|---|---|---|---|
| `play_1x_120fps.bat` | 1280x720 | RTV | Stable 120 fps, lowest GPU load and input lag |
| `play_qhd.bat` | 2560x1440 | ROV | Sharper, ~70–90 fps in races |
| `play_1x_120fps.sh` | 1280x720 | Vulkan | Linux / macOS (untested) |

An FPS counter is drawn in the top-left corner (`--ssx_show_fps=false` hides it).

## Options

| Option | Effect |
|---|---|
| `--ssx_render_fps=N` | Game render loop rate (game default 60, `0` = uncapped) |
| `--ssx_present_interval=N` | Guest vblanks per flip (game default 2) |
| `--video_mode_refresh_rate=N` | Guest refresh rate; frames ≤ rate / interval (240 → 120 fps) |
| `--resolution_scale=2` | 2x internal resolution |
| `--render_target_path_d3d12=rov` | ROV render path; at 2x, RTV spends most GPU time on EDRAM copies |
| `--ssx_show_fps=false` | Hide the FPS overlay |
| `--gpu_backend=vulkan` | Vulkan renderer when the SDK has both (SDK fix; default `any` = D3D12 first) |
| `--texture_partial_array_reload=false` | Disable partial reloads of array/3D textures (SDK fix, on by default) |

Diagnostics (off by default): `--d3d12_gpu_profile` (GPU time per category),
`--d3d12_log_texture_loads`, `--d3d12_diagnose_invalid_command_lists`,
`--log_wait_stats`, `--log_resolves`, `--dump_shaders=<dir>` (also SPIR-V on
Vulkan). With RenderDoc's Vulkan layer enabled, `--renderdoc_capture_swap=N`
captures the guest frame after swap N (`-1`: after pressing F11). Fatal errors write `ssx_crash.txt` next
to the exe.

## What's in here

| Path | Contents |
|---|---|
| `ssx_manifest.toml` | rexglue project manifest |
| `ssx_functions.toml` | Hand-fixed function boundaries |
| `ssx_datarefs.toml` | Function entry points found from data and `lis`/`addi` references |
| `src/overrides.cpp` | Guest hooks: `mfvscr` stub, frame-rate unlock (render loop interval, present interval), FPS counter |
| `src/fps_overlay.h` | ImGui FPS overlay (external overlays can't hook the runtime's swap chain) |
| `src/crash_handler.cpp` | Crash reporting to `ssx_crash.txt` |
| `tools/` | XEX image dumper and the scripts that build `ssx_datarefs.toml` |
| `patches/` | rexglue-sdk fixes (see below) |
| `scripts/` | Launchers |

### rexglue-sdk fixes (`patches/`)

- Input: crash from concurrent device-list rebuilds (missing lock)
- Config/achievement files under non-ASCII (e.g. Korean) user folders
- Resolution scaling: small-mip copies and 3D-as-2D wrapper sizing (device
  removal with `INVALID_CALL`); volume resolves used the wrong height
  (`RB_COPY_SURFACE_SLICE`, ported from xenia-canary)
- Render target 8_8_8_8 ↔ 8_8_8_8_GAMMA transfer converted in the wrong direction
  (D3D12 and Vulkan)
- `--gpu_backend` to pick D3D12 or Vulkan when both are compiled in
- Vulkan: texture fetches read the result exponent bias from the wrong fetch
  constant word (SSX textures came out at 1/256 brightness); depth → stencil
  bit transfers (GPUs without `VK_EXT_shader_stencil_export`, e.g. NVIDIA) set
  every stencil bit; `VK_KHR_portability_enumeration` is requested only on
  macOS (RenderDoc rejects it)
- Vulkan performance: partial reloads of the snow trail volume (as on D3D12),
  and no per-bit stencil transfer draws from depth targets whose stencil can
  only be zero (`--vulkan_skip_zero_stencil_transfers`)
- Partial reloads of GPU-written slices of 2D array and tiled 3D textures (SSX's
  snow trail volume was fully reloaded many times per frame)
- Optional diagnostics listed above

### Regenerating `ssx_datarefs.toml`

```
tools/xexdump/build/xexdump.exe game default.xex tools/default_image.bin
python tools/find_missing_funcs.py tools/default_image.bin generated/default/ssx_register.cpp > tools/missing2.txt
python tools/build_datarefs.py tools/missing.txt tools/missing2.txt [--exclude codegen_output.txt]
python tools/check_gotos.py   # dangling labels left by mid-function candidates
```

## 한국어

SSX(2012, Xbox 360)를 rexglue-sdk로 정적 리컴파일한 Windows 네이티브 빌드입니다.
**게임 파일과 리컴파일된 코드는 포함되어 있지 않습니다.** 직접 소유한 게임에서
`default.xex`와 데이터 파일을 추출해 `game/` 폴더에 넣고 빌드해야 합니다.

- `play_1x_120fps.bat`: 기본 해상도, 120fps 안정
- `play_qhd.bat`: 2배 해상도(QHD), 레이스 약 70~90fps
- `play_1x_120fps.sh`: Linux/macOS용 (Vulkan, 테스트 안 됨)

## License

BSD 3-Clause for this repository's own files (see `LICENSE`). The patch in
`patches/` modifies rexglue-sdk, which is also BSD 3-Clause.

## Credits

- [rexglue-sdk](https://github.com/rexglue/rexglue-sdk) (BSD 3-Clause)
- [Xenia](https://github.com/xenia-project/xenia) / xenia-canary, which rexglue's
  runtime is based on
- SSX is © Electronic Arts. This project is not affiliated with or endorsed by EA.
