# DLSS evaluation backend checkpoint

This records the isolated backend milestone. The subsequent
[in-game DLAA checkpoint](ssx-dlaa.md) connects it before SSX tone mapping and adds
reversed-depth support; see that page for current activation and validation status.

The backend now records real NVIDIA DLSS Super Resolution commands. This is an
isolated GPU-tested implementation, **not yet a gameplay DLSS option**. SSX still
displays its existing renderer. Frame Generation and native HDR output remain
unfinished.

## Implementation

Apply `patches/rexglue-sdk-ssx-dlss-evaluation.patch` after
`rexglue-sdk-ssx-temporal-alignment.patch`. The public `Streamline` interface keeps
NVIDIA headers private and adds `SRFrame`, `EvaluateSR` and `ReleaseSR`.

The API supplies a matching frame token and viewport for common constants, all
four resource tags and `slEvaluateFeature`. It requires unjittered row-major
projection/inverse and clip-history matrices, a valid camera basis, input-pixel
jitter, non-inverted device depth, and previous-minus-current pixel motion that
includes camera and object motion but excludes jitter. Input extents must match
and fit NVIDIA's queried resolution bounds. Format, output UAV, alias, camera,
duplicate-frame and jitter checks fail closed.

Color uses the established SSX policy: decoded linear FP16 before exposure,
HDR input enabled, automatic NGX exposure, pre-exposure and exposure scale 1.
The guest's diagnostic exposure texture is not tagged. This preserves values
above 1 in reconstruction; it does not implement HDR monitor output.

First evaluation, explicit cuts, sequence gaps, input size changes and rejected
inputs reset history. Ordinary consecutive frames retain it. Frame IDs here are
the caller's reconstruction sequence; the future SSX adapter must detect guest
frame gaps and cuts and must not increment it merely for repeated UI paints.
Mode/output size changes require explicit GPU drain and `ReleaseSR` first.

Inputs enter in `NON_PIXEL_SHADER_RESOURCE`, output in `UNORDERED_ACCESS`.
Streamline restores resource states. Callers retain textures through GPU
completion, submit even partially recorded failed evaluations, and restore
command-list bindings before subsequent guest rendering. The test uses its own
command list and drains before reusing memory or releasing a feature.

This follows the pinned official [DLSS guide](https://github.com/NVIDIA-RTX/Streamline/blob/v2.14.1/docs/ProgrammingGuideDLSS.md)
and [manual integration guide](https://github.com/NVIDIA-RTX/Streamline/blob/v2.14.1/docs/ProgrammingGuideManualHooking.md).
No Streamline runtime DLL or driver was modified.

## GPU verification

`tools/streamline_probe --evaluate-smoke` synthesizes a stationary perspective
scene with a moving foreground rectangle. Its depth, object motion and jitter
are known analytically; it uses no game files. It evaluates:

- Seven 2560x1440 → 3840x2160 Quality frames, with normal history, explicit cut,
  sequence gap, invalid-input recovery and duplicate rejection.
- Two 2240x1168 → 3360x1752 Quality frames after output resize/reallocation.
- Two 3840x2160 DLAA frames after mode/output change.

Each output starts filled with a negative sentinel, then is read back after the
GPU fence. Every RGB pixel must be written and finite. Brightness statistics and
the known foreground's linear HDR color are checked. The shared presentation
probe also exercises 12 hidden presents and two swap-chain resizes with FG off.
The D3D12 debug layer must report zero errors, and the process must exit normally.
These checks establish actual evaluation and resource lifetime behavior. They do
not establish SSX reconstruction quality, temporal stability or performance.

Verified on the RTX 5090 on 2026-10-04: all 11 evaluations passed, all 22 invalid
input/configuration attempts were rejected, all outputs had zero non-finite or
unwritten RGB values, foreground RGB stayed within 0.1 of (8, 2, 0.5), and the
D3D12 error count was zero. The SR process exited normally in 5.90 seconds;
disabled and missing-SDK cases each exited in 0.86 seconds. These are whole-test
durations, not game frame times. The SDK and probe build passed; the
`REX_HAS_STREAMLINE=0` translation unit also passed a separate syntax check.
Local transcripts are under `out/sr-evaluation-validation/`.

NVIDIA still logs the earlier FG-state synchronization advisory and a default
backbuffer-extent warning while FG is off. Tagging also logs an SDK clone alignment
normalization warning. These are not D3D12 validation errors; FG integration and
its resource retention/performance are not established by this test.

Build/install the patched SDK as before, rebuild `tools/streamline_probe`, then
run from the repository in a normal developer PowerShell session:

```powershell
./scripts/test_streamline.ps1
```

The runner tests SR, Streamline disabled, and a missing-runtime fallback in
separate child processes. It preserves logs under `out/streamline-validation`,
has a timeout, and only stops its own timed-out test child. Timeout is a failure.
It does not require LLDB and does not launch or stop SSX.

## Debugger popup and shutdown investigation

Visual Studio's installed `lldb.exe` cannot start because `liblldb.dll` is absent.
Invoking it caused the recurring popup. It has exited; the test workflow uses
Windows native stack tracing instead. No game or launcher dependency on LLDB was
added, and no loose DLL was downloaded into the game directory.

The separate restricted-process hang was reproduced in NGX shutdown. Native
stacks placed the main thread in `NvTelemetryAPI64!UninitializeTelemetry` and
the busy worker inside `NvTelemetryBridge64` file/pipe access. The identical
probe outside the restriction completed in about four seconds, including normal
NGX shutdown. This supports an environment-specific driver cleanup issue; it
does not justify changing production shutdown or Windows/NVIDIA settings.

## Next game integration boundary

The backend must consume the matching SSX packet on the scene command stream,
before tone mapping and HUD composition. Existing temporal passes currently
produce diagnostic resources on the presentation side. Move/arrange their
production at the scene boundary, derive validated separate SSX view/projection
constants, and feed reconstructed color into the remaining guest postprocessing
with the correct encoding and dimensions. Preserve jitter-off fallback when an
input family is unsupported. Validate actual SSX output before enabling a player
option; FG additionally needs its own timing, Reflex and HUD integration.
