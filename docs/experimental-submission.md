# Experimental graphics build

This source branch adds a Windows options launcher and the SSX-specific renderer
integration for DLAA/DLSS, Reflex, Frame Generation and native HDR. It requires the
patched SDK below; a stock ReXGlue binary will not implement these options.

## Development credit

The contributor used **ChatGPT ASTRA throughout the development of these
experimental graphics upgrades**: source investigation, implementation, debugging,
launcher development and validation tooling. The contributor directed the work
and supplied gameplay testing and visual feedback. This disclosure covers this
contribution, not authorship of the original SSX-ReXGlue, ReXGlue/Xenia or NVIDIA
components, whose existing credits and licenses remain intact. The validation
limits below still apply.

## Launch and settings

Open **SSX.Options.exe** beside the built `ssx.exe`, or `scripts/play_options.bat`.
The classic Windows dialog has Graphics, HDR brightness, Advanced and Files tabs.
Save stores preferences; Save & Play starts the selected executable with explicit
options. Preferences are local to `%LOCALAPPDATA%\SSX-ReXGlue\launcher.json` and a
backup is retained when replacing them. `--settings path.json` selects another
preferences file. Settings take effect at the next game launch.

Choose your own extracted game directory containing `default.xex` and the `.big`
archives. The development layout detects the existing `out/test-user` profile;
the portable layout lets you select your existing user-data directory. Blank uses
the runtime default. Reset keeps file/save paths. No game files or saves are uploaded.

| Option | Available settings and dependencies |
| --- | --- |
| Output/window size | 1280×720, 1920×1080, 2560×1440, 3840×2160. Fullscreen follows the desktop display mode; this does not switch Windows' refresh rate/resolution. |
| Internal frame scale | 1×, 2×, 3× independently of HDR, DLAA and FG. DLAA at 1× is selectable. |
| Antialiasing/upscaling | Original game MSAA, DLAA, experimental DLSS Quality/Balanced/Performance. DLAA/DLSS disable scene MSAA. |
| Model preset | NVIDIA default, E/F (deprecated legacy), J, K, L, M. L remains the existing checkpoint. Reserved presets that merely alias the default and removed presets are omitted. |
| Frame Generation | Off, 2×, 3×, 4×, 5×, 6× total output. FG requires DLAA and enables Reflex timing. DLAA works with FG off. |
| Reflex | Off, On, On + Boost. Standalone Reflex does not require DLAA or HDR. |
| HDR | Independent on/off, 400–3,000-nit peak, paper white, UI white, highlight strength, exposure compensation, calibration patches. Windows HDR must already be enabled. |
| Original options | Windowed/fullscreen, monitor, D3D12 RTV/ROV/automatic, render cap, guest refresh/flip interval, keyboard/mouse, partial texture reload and diagnostics. |

Automatic rendering selects ROV where needed. Explicit RTV is incompatible with
the HDR compositor and DLAA/DLSS. Selecting an invalid dependency displays a reason
and prevents launching; it does not silently change antialiasing or resolution.

The total FG multiplier includes the real frame: 3× means two generated frames.
The retained NVIDIA capability log for the development RTX 5090 reports a maximum
of five generated frames (6× total). The game checks the actual GPU/driver limit
at runtime. Unsupported multipliers remain off and log `unsupported_multiplier`;
they are not silently clamped or claimed active. Dynamic MFG is not implemented.
The displayed FG FPS comes from NVIDIA presentation counts, not rendered FPS
multiplied by the requested setting.

## Resolution and experimental upscaling limits

SSX's normal race scene is 1120×584 inside a 1280×720 final frame. At 3×, those
become 3360×1752 and 3840×2160. The internal scale labels describe the final game
frame, not a promise that all original passes cover that full scene resolution.
Window/output size is separate; e.g. a 1× game frame can be displayed at 4K.
The experimental 1280×720 **base scene** override remains off in this launcher.

The new DLSS modes use NVIDIA's requested mode and optimal input dimensions, with
matching color, depth, pixel-space motion and jitter. However, ReXGlue's EDRAM
renderer uses integer scales. The experimental bridge rounds raster scale up and
resizes the temporal inputs before DLSS when needed. At a 3× output frame, all
three upscaling modes rasterize at 2×; Balanced/Performance additionally resize
their inputs. At 1×, rendering remains 1×, with lower-resolution DLSS inputs.
**These modes are not a complete fractional-resolution renderer, and standard
DLSS performance gains must not be assumed.** The input-resizing path has been
compiled, but has not been GPU/gameplay tested. DLAA bypasses it.

## HDR and fallback

Native HDR starts with decoded scene color before SDR clipping. It preserves
original grading, bloom, later world effects and premultiplied HUD blending.
Display black maps to zero; authored Tricky grading is not forced to black.
HDR can decode the scene without creating NVIDIA tokens or enabling DLAA/FG.

The overlay identifies native HDR versus SDR mapped into HDR, gives the rejection
reason, and counts race fallback frames. Menus, loading and pause use SDR mapping.
Unknown or incomplete render paths fall back; whole-game coverage is not assured.
Keep the status display enabled when evaluating the experimental build.

## Validation and deliberately untested settings

The earlier 3× internal scale, DLAA preset L, 1,000-nit HDR + 3× FG checkpoint was
confirmed in live races. The user confirmed the Tricky activation/deactivation
color correction. Those observations are not a full-game certification.

For this options/independence revision:

- Windows SDK/runtime/GPU plugin, game and native launcher compile successfully.
- 69 relevant SDK unit tests, both generated-code hook tests and 72 asset-free
  launcher configuration checks pass. These are not DLSS/FG image-quality tests.
- Seven fixed-1,000-nit HDR GPU cases checked independent scene decoding and
  1×/2×/3× input composition: 168 reference and 168 HUD-free channel checks pass;
  138,240,000 output comparisons are identical; no D3D12 validation errors.
  These checks preceded the subsequent experimental DLSS input-resizer addition.
- Independent HDR without DLAA, lower-scale DLAA/FG, and all cross-combinations
  still need live gameplay confirmation in this final build.
- **At the user's request, DLSS Quality/Balanced/Performance, alternative model
  presets, different FG multipliers, and different HDR peak brightnesses were
  not run for this submission.** Mark these settings experimental/untested.
  Historical 2× FG, Quality and 3,000-nit offscreen checks are separate checkpoints,
  not verification of the expanded launcher or every new combination.
- No 3,000-nit TV calibration, all-track/character coverage, arbitrary aspect-ratio
  FG, or performance improvement is claimed. FG output currently requires 16:9.
- Physics/controller consumption remains at the game's 30 Hz tick; rendering and
  Reflex run at render rate with the game's interpolation. FG does not accelerate
  physics or provide new input samples for its generated frames.

## Reproduce the source build

Use Windows 10/11 x64, Visual Studio 2022 with the Windows SDK, clang/LLVM 20+,
CMake 3.25+, Ninja, Git and Python 3. The launcher uses Windows PowerShell 5.1,
.NET Framework 4.8 and WinForms. Compile from a VS x64 developer environment.
Running the private build also requires the Microsoft Visual C++ x64 runtime.
The packaged game/runtime/plugin do not import `liblldb.dll`.

Start with a **separate clean** ReXGlue checkout pinned to
`f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`. Configure your Git author/committer
identity before applying the original nine SSX fix commits. The script checks
the revision, local changes and patch hashes; it never resets an existing checkout.

```powershell
git clone --recursive https://github.com/rexglue/rexglue-sdk.git rexglue-sdk
git -C rexglue-sdk checkout f5337cdc947ff6d4c4196737e2c807a48f2a1fc2
git -C rexglue-sdk submodule update --init --recursive
./scripts/apply_experimental_sdk.ps1 -SdkSource ./rexglue-sdk
./scripts/prepare_streamline.ps1
$sl = (Resolve-Path ./out/vendor/streamline-2.14.1).Path
cmake -S rexglue-sdk -B rexglue-sdk/out/build/ssx-experimental -G "Ninja Multi-Config" `
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ `
  -DREXGLUE_ENABLE_FIDELITYFX=OFF -DREXGLUE_ENABLE_STREAMLINE=ON "-DREXGLUE_STREAMLINE_DIR=$sl"
cmake --build rexglue-sdk/out/build/ssx-experimental --config Release --parallel 8
cmake --install rexglue-sdk/out/build/ssx-experimental --config Release --prefix rexglue-sdk/out/install/ssx-experimental
./scripts/build_windows.ps1 -SdkPath ./rexglue-sdk/out/install/ssx-experimental -GameDir 'D:/Games/SSX'
./scripts/play_options.bat
```

`prepare_streamline.ps1` pins and hash-checks Streamline **2.14.1**. Its production
bundle includes DLSS **310.9.1.0**. Patch order and SHA-256 hashes are recorded in
`patches/experimental-series.json`; the final patch removes feature/scale coupling
and adds model/mode/multiplier selection. Checked-in HLSL bytecode is included;
the new input and resolve-extract shaders compile with Windows SDK `fxc`, `cs_5_1`,
entry `main`, optimization `/O3`.

On Windows, enable Git symlink support before checkout, or replace libmspack's
symlink stubs in `thirdparty/libmspack/cabextract/mspack` with their actual target
files as described in the main README. That local vendor repair is not included
in the graphics patch series. SDK requirements and original fixes are described
in the main README. Upstream
build failures from missing vendor dependencies are not remedied by copying
arbitrary debugger DLLs beside the game. Use the installed SDK runtime/plugin.

For a private local build folder, `scripts/package_experimental.ps1` accepts
`-BuildDirectory`, `-RuntimeDirectory` and a **new** `-Destination`; it copies an
allowlist of runtime/launcher files and vendor licenses and writes SHA-256 hashes.
It excludes assets, saves, captures, personal settings and generated source.
Do not upload the private game executable: the upstream submission is source and
SDK patches, with game code generated locally from the user's own copy.

## Integration references

- [NVIDIA DLSS guide, Streamline 2.14.1](https://github.com/NVIDIA-RTX/Streamline/blob/v2.14.1/docs/ProgrammingGuideDLSS.md)
- [NVIDIA FG guide and runtime multiplier limit](https://github.com/NVIDIA-RTX/Streamline/blob/v2.14.1/docs/ProgrammingGuideDLSS_G.md)
- [Preset definitions](https://github.com/NVIDIA-RTX/Streamline/blob/v2.14.1/include/sl_dlss.h)
- [HDR checkpoint](ssx-hdr.md), [FG checkpoint](ssx-frame-generation.md), [Reflex checkpoint](ssx-reflex-timing.md)
