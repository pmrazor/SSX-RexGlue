# SSX (2012) — rexglue static recompilation

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
2. **This project** (from an environment with `vcvars64.bat`, LLVM, CMake and
   Ninja on `PATH`):
   ```
   cmake --preset win-amd64-release
   cmake --build out/build/win-amd64-release
   ```
   The first build runs codegen on `game/default.xex` (~4 minutes) and then
   compiles the generated sources.
3. Copy the SDK's runtime DLLs (`rexruntime.dll`, `rexgpu-xenos.dll`,
   `TracyClient.dll`) next to `ssx.exe` if the build didn't. If the SDK was
   configured with `-DREXGLUE_ENABLE_FIDELITYFX=ON`, `amd_fidelityfx_dx12.dll`
   is required too.

## Run

Copy a launcher from `scripts/` next to `ssx.exe` and put (or link) the
extracted game folder next to it as `game`:

| Launcher | Internal resolution | Render path | Notes |
|---|---|---|---|
| `play_1x_120fps.bat` | 1280x720 | RTV | Stable 120 fps, lowest GPU load and input lag |
| `play_qhd.bat` | 2560x1440 | ROV | Sharper, ~70–90 fps in races |

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
