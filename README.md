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

**Experimental options build:** open `SSX.Options.exe` or `scripts/play_options.bat`.
The contributor used **ChatGPT ASTRA throughout the development of these
experimental graphics upgrades**, including source investigation, implementation,
debugging, launcher development and validation tooling. The contributor directed
the work and supplied gameplay testing and visual feedback. This credit applies
to this contribution; the original SSX-ReXGlue, ReXGlue/Xenia and NVIDIA work
retains its existing authorship and licenses.

The [submission guide](docs/experimental-submission.md) describes the independent
HDR/DLAA/resolution controls, selectable DLSS models and experimental upscaling,
FG multipliers, reproducible SDK patch series and deliberately untested settings.
FG requires DLAA; DLAA can run with FG off. HDR does not require either feature.

Latest local checkpoints: [2x gameplay FG / 3x GPU validation and measured FPS](docs/ssx-frame-generation.md),
and [native HDR + Frame Generation](docs/ssx-hdr.md). HDR testing uses 1,000 nits on the
Alienware, with a selectable 3,000-nit TV target. Original scene grading is restored
with native highlight preservation. Bloom, later world draws and the game HUD
use the corrected native HDR composition path; the user confirmed improved HUD
blending and color. HDR + 3x FG is runtime-confirmed in the tested races.
`scripts/play_hdr_fg.bat` uses a separate experimental build;
`scripts/play_hdr_preview.bat` preserves the prior HDR-only build for comparison.
The current HDR + FG build shows native/fallback status, rejection reason and a
race-only fallback counter. The Tricky transition grading correction was confirmed
by the user; see the HDR notes for the diagnosis and remaining coverage limits.

The following performance figures are upstream reports, not results measured on
the RTX 5090. See [the graphics checkpoint](docs/graphics-checkpoint.md) for this
branch's implementation and validation status. An optional NVIDIA SDK connection
is now built and GPU-tested; see [the DLSS/FG checkpoint](docs/streamline-checkpoint.md).
The [SSX input collector](docs/ssx-frame-inputs.md) now copies scene color, depth
and exposure candidates and carries their frame metadata into presentation.
An experimental [camera motion pass](docs/ssx-camera-motion.md) is now implemented,
GPU-tested and exercised in a 3x race. The new [native rider motion prototype](docs/ssx-native-motion.md)
uses captured deformed geometry and passes 16 GPU tests. A live 3x readback and
repeatable GPU replay exposed and fixed projection-rounding gaps; rider/board
coverage is validated for that captured frame. Broader motion validation remains.
Experimental in-game DLAA and DLSS Quality are now active in tested races.
Generated SSX frames and native HDR are runtime-confirmed in tested races;
broader effects, motion and transition coverage remains incomplete.

The [FG foundation checkpoint](docs/ssx-frame-generation.md) adds real analytic
FG validation and retains SSX's actual DLAA motion inputs through presentation.
The [Reflex timing checkpoint](docs/ssx-reflex-timing.md) adds driver latency
reports and a live-tested native frame trace. The opt-in `scripts/play_reflex.bat`
runs Reflex sleep and fresh controller sampling for every normal rendered update,
delivering the matching snapshot whenever a physics job is due. Rendering is
no longer coupled to 30 FPS: the corrected race trace presents 333 distinct DLAA
frames in five seconds (66.6 FPS), with 333 matching NVIDIA reports and all 150
physics input handoffs verified. Physics/controller processing remains at 30 Hz;
extra frames retain SSX's original interpolation. Ordinary DLAA remains the
fallback. Render-rate camera input response remains unfinished; newer FG results
are recorded in the checkpoint linked above.
The follow-up `patches/rexglue-sdk-ssx-reflex-lineage.patch` applies after
`rexglue-sdk-ssx-fg-reflex.patch`; it adds sample generations and an ordered
pre-render GPU marker. Unit tests and a live race trace pass, with 350 complete
GPU-marker→DLAA→swap chains and no frame-ID mismatches. The subsequent
`rexglue-sdk-ssx-frame-tokens.patch` carries a real NVIDIA token through DLAA and
the image mailbox to its first actual Present. A race trace verifies 374 complete
chains with no identity/order errors. `scripts/play_frame_tokens.bat` enables
this transport-only mode. `rexglue-sdk-ssx-reflex-coupled.patch` applies next,
followed by `rexglue-sdk-ssx-reflex-frame-driven.patch` for the current experiment.
`scripts/play_reflex_sync.bat` retains the older coupled 30 FPS diagnostic.

The [performance checkpoint](docs/performance-checkpoint.md) removes unused
native-motion diagnostic atomics with byte-identical motion output. The measured
saving is 0.028 ms in a paused race and 0.041 ms in captured-frame replay;
whole-game FPS gains are not established. `scripts/play_profile.bat` enables
per-pass GPU timings. Apply `rexglue-sdk-ssx-reflex-transitions.patch`, then
`rexglue-sdk-ssx-performance.patch` after the render-driven patch above.

The [jitter/color checkpoint](docs/ssx-temporal-inputs.md) adds a pre-tone-map
scene-color decoder and guarded exposure diagnostics. Live testing exposed and
fixed mismatched geometry/material jitter that caused purple corruption. The
repeated race renders normally with jitter enabled; broader coverage remains
unfinished, and jitter stays off by default except in the explicit DLAA/Quality paths.

The [alignment/exposure checkpoint](docs/ssx-temporal-alignment.md) adds automatic
projection recognition across shader variants, matching clip-interpolator and
lighting corrections, early depth capture, alignment checks and a DLSS automatic
exposure policy. A live rider readback and shader/color GPU probes are verified;
whole-game coverage remains unverified.

The [DLSS evaluation checkpoint](docs/ssx-dlss-evaluation.md) adds real frame
tokens, constants, resource tags, evaluation and history/release handling. A
synthetic GPU test verifies 4K SR and DLAA output, resets and resource lifetimes.
The NGX shutdown stall was isolated to restricted test execution; the host probe
exits cleanly.

The [in-game DLAA checkpoint](docs/ssx-dlaa.md) connects decoded scene color,
reversed depth, camera/rider motion and jitter before the guest tone mapper and
HUD. `scripts/play_dlaa.bat` selects this experimental mode with the original
local save. The tested race uses 3360x1752 scene rendering, MSAA off and 4K final
output. Live logs confirm evaluation and retained history; temporal image quality
and whole-game coverage remain under validation. The release build stays available
as the fallback.

The [Preset L / Quality checkpoint](docs/ssx-quality-preset-l.md) verifies the
current official DLSS 310.9.1.0 DLL, explicitly selects model L, and tests real
2560x1440-to-3840x2160 Quality GPU reconstruction. The SSX reconstruction API now
supports distinct input/output sizes. The subsequent [Quality race checkpoint](docs/ssx-quality-races.md)
connects native 2x scene rendering to 3x reconstruction and postprocessing.
`scripts/play_quality.bat` uses the original local save. A live race confirms
2240x1168 inputs, 3360x1752 reconstructed scene color and 3840x2160 final output.
The corrected race is visibly free of the reported purple corruption; broader
image-quality coverage and a controlled performance comparison remain pending.
The expanded options build also exposes untested Balanced/Performance and model
selection. Its integer-raster input-resizing limitation and validation status are
described in the submission guide; no upscaling performance gain is claimed.

- Boots, plays races, saves progress (tested on Windows 11, RTX 4070).
- 120 fps at 1x internal resolution (the game is locked to 30/60 fps on console).
- 2x internal resolution (2560x1440) with the ROV render path: about 70–90 fps in
  races, 120 fps in menus.
- Known issues: occasional UI texture glitches at 2x; online features (RiderNet)
  are gone with EA's servers.

## Requirements

- Windows 10/11 x64, a Direct3D 12 GPU
- Visual Studio 2022 (MSVC toolset + Windows SDK), LLVM/clang 20+, CMake 3.25+,
  Ninja, Python 3
- Your own SSX disc image, extracted so that `default.xex` and the `.big`
  archives are in `game/`

## Build

1. **rexglue-sdk with the SSX fixes.** Clone rexglue-sdk with submodules at
   v0.10.0 (`f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`) and apply
   `patches/rexglue-sdk-ssx-fixes.patch` (`git am`). Then use `git apply` for
   `patches/rexglue-sdk-present-diagnostics.patch` and, if building SDK unit tests,
   `patches/rexglue-sdk-test-build.patch`. On Windows, git checks out
   libmspack's symlinks as text files; replace the stubs in
   `thirdparty/libmspack/cabextract/mspack` with copies of their targets. Then:
   ```
   cmake --preset win-amd64 -DREXGLUE_ENABLE_FIDELITYFX=OFF
   cmake --build out/build/win-amd64 --config Release --target install
   ```
2. **This project** (from an environment with `vcvars64.bat`, LLVM, CMake and
   Ninja on `PATH`):
   ```
   powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build_windows.ps1 -SdkPath "D:\dev\rexglue-sdk\out\install\win-amd64" -GameDir "D:\Games\SSX"
   ```
   The helper creates an ignored `game` junction only if absent, runs codegen,
   configures CMake against the specified SDK, and builds. Existing game folders
   are preserved. Omit `-GameDir` when `game/default.xex` is already available.
   Generated C++ remains local and ignored. For manual builds, run the installed
   SDK's `rexglue codegen ssx_manifest.toml` **before** the first CMake configure;
   subsequent builds use the generated dependency tracking.
3. CMake stages the runtime DLLs, the Xenos GPU plugin and launchers next to
   `out/build/win-amd64-release/ssx.exe`. If the SDK was
   configured with `-DREXGLUE_ENABLE_FIDELITYFX=ON`, `amd_fidelityfx_dx12.dll`
   is required too.

## Run

Copy a launcher from `scripts/` next to `ssx.exe` and put (or link) the
extracted game folder next to it as `game`:

| Launcher | Final scaled guest frame | Render path | Notes |
|---|---|---|---|
| `play_1x_120fps.bat` | 1280x720 | RTV | Stable 120 fps, lowest GPU load and input lag |
| `play_qhd.bat` | 2560x1440 | ROV | Sharper, ~70–90 fps in races |
| `play_4k.bat` | 3840x2160 at 3x for a 720p guest frame | ROV | 4K output and ground training tutorial checked on RTX 5090; no HUD glitches reported in that test |

These sizes describe the final guest frame. A full race capture confirms a
1120x584 scene before the 1280x720 output. The opt-in `-ExperimentalScene720p`
launcher switch requests a 1280x720 base scene through a guarded game hook.
It builds and passes CPU hook tests; gameplay and 3x scene validation are pending.
Ordinary launches explicitly keep it off. See [race capture findings](docs/race-capture.md).
The experiment is currently deferred. [Source audit](docs/resolution-source-audit.md)
explains the existing scaler and the alternative of stock 4x supersampling.

The 4K launcher needs `launch_ssx.ps1` beside it (both are staged by CMake).
It also works directly from the repository's `scripts` folder after a Release build:

```powershell
.\scripts\play_4k.bat -GameDir "D:\Games\SSX"
.\scripts\play_4k.bat -GameDir "D:\Games\SSX" -Windowed -Diagnostics
.\scripts\play_4k.bat -DryRun
```

`-Diagnostics` requires the new SDK diagnostic patch. `-DryRun` prints the command
without running the game. `-ExePath` selects another build, and `SSX_GAME_DIR` can
supply the asset path. `-UserDataDir` selects a separate local save/cache folder;
omit it to retain the SDK's normal user-data location. `-Monitor 1` selects the
primary display (`0` leaves the SDK default). `-Keyboard` enables controller
emulation: Space = A, Backspace = B, Enter = Start, WASD = left stick,
Shift+arrows = D-pad. `launch_ssx.ps1 -Profile 1x` or `-Profile qhd` provides
comparable fallback runs with explicit per-axis scaling. The original launchers
remain available. The new profiles explicitly request a guest flip interval of
2 at 240 guest Hz (a 120 Hz ceiling); this does not change Windows' monitor mode.
Fullscreen uses the desktop resolution and refresh rate. Select 3840x2160 at
240 Hz for the AW3225QF in Windows before testing.

For direct `ssx.exe` launches, an adjacent `ssx.toml` may hold these settings.
SSX preloads that file before the pinned SDK resolves paths. Use absolute local
paths for `game_data_root` and `user_data_root`; command-line values override the
file. Keep personal paths/configuration in the ignored build directory. At high
Windows DPI, `window_width`/`window_height` are logical units; fullscreen uses
the monitor's physical desktop size.

`tools/graphics_probe` is a standalone CMake project for a read-only D3D12/DXGI
adapter and monitor report. It prints ROV support, desktop resolution/refresh,
output color space and driver-reported luminance without changing display settings.

For renderer investigation, apply `patches/rexglue-sdk-capture-markers.patch`
after the present diagnostic patch, followed by
`patches/rexglue-sdk-guest-frame-capture.patch`. Rebuild/install the SDK and rebuild
SSX. Enable
`--gpu_debug_markers=true` when capturing. Draw labels correlate shader hashes,
viewports and raw guest target/depth state. **F8** captures a complete guest GPU
frame when launched through RenderDoc. This was verified in title-screen and
normal-race captures and replays. F12 may capture only the independently refreshed presenter. See
[capture instructions](docs/capture-next-frame.md) and
[validation details](docs/graphics-checkpoint.md).

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
| `--ssx_scene_720p=true` | Experimental 1280x720 base scene; default off, restart required, gameplay validation pending |
| `--ssx_log_scene_size=true` | Log original/selected scene dimensions and whether the experiment matched |
| `--texture_partial_array_reload=false` | Disable partial reloads of array/3D textures (SDK fix, on by default) |

Diagnostics (off by default): `--d3d12_gpu_profile` (GPU time per category),
`--d3d12_log_texture_loads`, `--d3d12_diagnose_invalid_command_lists`,
`--log_wait_stats`. Fatal errors write `ssx_crash.txt` next to the exe.

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

## License

BSD 3-Clause for this repository's own files (see `LICENSE`). The patch in
`patches/` modifies rexglue-sdk, which is also BSD 3-Clause.

## Credits

- [rexglue-sdk](https://github.com/rexglue/rexglue-sdk) (BSD 3-Clause)
- [Xenia](https://github.com/xenia-project/xenia) / xenia-canary, which rexglue's
  runtime is based on
- SSX is © Electronic Arts. This project is not affiliated with or endorsed by EA.

Experimental gameplay FG presentation is implemented behind a disabled-by-default
validation switch. See the [FG checkpoint](docs/ssx-frame-generation.md) for the
patch order, passing analytic GPU tests, launcher and remaining race/HUD checks.
