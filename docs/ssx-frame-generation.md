# Experimental Frame Generation checkpoint

For the later native HDR + 3x FG implementation and live presentation counts,
see [the HDR checkpoint](ssx-hdr.md#native-hdr--frame-generation). It uses
`scripts/play_hdr_fg.bat`; the earlier SDR results remain below.

## 3x and FPS counter follow-up

`rexglue-sdk-ssx-fg-multiframe.patch` follows `ssx-fg-gameplay` (SHA256
`9c43248bfc18feddbe56b7fc6c33d284a1c3f84401a24771e711ad4c415d62c6`).
`play_fg.bat` defaults to 3x total output (two generated frames); append
`-FGMultiplier 2` for the prior mode. Runtime capability checks reject unsupported
counts. The overlay separately shows Render FPS and measured FG output FPS from
NVIDIA's actual presentation counts. It clears stale rates on suspension, mode
changes and time gaps, rather than multiplying the render counter by a setting.

57 relevant SDK tests and both game CTests passed. GPU test
`out/fg-gameplay-validation/3x-regression-01` produced 333 presentations from 111
4K rendered frames, 120 from 40 after resizing, and 80 from 40 with 2x fallback.
It passed suspended/reset/invalid-input checks with zero D3D12 errors and clean
shutdown. The 3x gameplay multiplier and overlay still need a live-game check.
The user reported no visible HUD ghosting in the preceding **2x** gameplay run;
this does not establish whole-game artifact-free HUD interpolation.

The separate [HDR preview](ssx-hdr.md) currently disables FG while its post-effects
and HUD color pipeline is being integrated. The SDR FG launcher remains available.

## Earlier 2x gameplay checkpoint

The SSX presenter is now wired for experimental 2x FG, with retained DLAA inputs,
shared Reflex tokens and a display-sized HUD-free color pass. The unpaused color
comparison passes. **2x gameplay generation is confirmed in an unpaused race**
after the render-ID and startup-option fixes. Pause/resume, window transitions
and visual artifact checks remain in progress; this is still experimental.

## Current presenter checkpoint (2026-10-05)

Apply `rexglue-sdk-ssx-fg-gameplay.patch` after `rexglue-sdk-ssx-performance.patch`.
SHA256: `10b0c82932a0391fda8f77401c9fffdefbc17e9fe07f7189e5b1ec6c27b4df98`.
Forward, reverse and residual checks pass. The existing SDK index and unrelated
changes remain untouched.

The presenter requires matching retained DLAA motion/camera/reset and frame token,
a fresh guest image, a focused 3840x2160 window, a race-render decision, and Reflex
On/Boost. It uses one generated frame, avoids `DXGI_PRESENT_RESTART`, nulls invalid
tags, suspends for missing inputs and surface changes, and logs actual NVIDIA
presentation counts separately from rendered FPS. Camera/history resets use the
same constants as DLAA; a late activation requests a reset on a subsequent frame
instead of overwriting constants already evaluated by DLAA.

The color pool holds the producer packet and retained depth/motion until both its
own submission and NVIDIA's input-consumption fence complete. It releases completed
packets promptly so the six-slot producer pool cannot be starved by idle color
slots. A missing consumer fence disables generation and prevents unsafe reuse;
a valid fence with value zero is the observed initial reset/no-async-work case.
Normal DLAA launches unload the FG plugin before swap-chain creation.
Fullscreen toggles disconnect/drain the FG presenter before changing the actual
OS window and reconnect afterwards. The guarded candidate builds and passes
26 SDK/2 game tests. It is staged separately in `out/build/win-amd64-fg`,
used by `play_fg.bat`. Window transitions still require live checks.

The game adapter marks only render job `8237DCA8` as a race candidate. It reads the
same manager pointer at `0x838AB8F0` and byte +30 that worker `823D2DF0` uses to
invoke pause transitions `823D2590`/`823D2D40`. Each decision is attached to an exact
render ID in a bounded table. Unknown IDs and other renderer paths fail closed.
The presenter obtains that ID from the exact retained Streamline token, not the
optional swap timing diagnostic (which is zero in the live race). The accessor
rejects absent, foreign and retired tokens. An 80-frame GPU token regression
passes identity, retirement, repeated-present and resize checks with zero D3D12
errors and clean shutdown (`out/fg-gameplay-validation/render-identity`).
The first enabled launch still fell back because CLI11 consumed the next plugin
option after the explicit empty `--d3d12_ssx_native_capture_path=` argument. This
disabled `d3d12_ssx_fg_inputs` despite the intended command line. A reproducing
test failed before the fix. The parser now expands known empty string options
into two argv entries before CLI11 parsing, preserving the empty value and the
following option. All 55 cvar/SSX SDK tests pass. That initial run is explicitly
not evidence of gameplay FG, regardless of its clean appearance.
This source-derived pause gate still needs live pause/resume verification and
broader menu/results coverage.

Builds and checks:

- SDK/runtime, game and GPU probe build successfully; 26 SSX SDK tests and both
  game CTests pass.
- New GPU color test checks 76,962 channel samples: maximum error one 8-bit code.
  Format/extent rejection, six-slot exhaustion, producer retention and independent
  consumer-fence reuse checks pass; zero D3D12 errors and clean host shutdown.
- FG regression `out/fg-gameplay-validation/fg-regression-03`: 222 presentations
  from 111 rendered 4K frames; 80 from 40 at 2560x1440 after resize; nine suspended
  test frames, reset/invalid-input checks, unloaded fallback, 202 matching ordered
  Reflex driver reports, zero D3D12 errors and clean shutdown.
- Earlier attempt 01 generated 146/111 and failed the count threshold; attempt 02
  reached 222/111 but lost foreground focus after resize. The test now checks
  foreground focus, waits again at surface transitions and warms up before rate
  assertions. Count thresholds were not lowered. These are correctness tests,
  not a game performance benchmark.
- Live game run `out/build/win-amd64-fg/logs/ssx_002.log` reaches 6,240 rendered
  frames and 12,470 NVIDIA presentations. Every measured 120-frame interval after
  startup adds 240 presentations, status zero and a progressing input fence.
  No `SSX_FG disabled` backend failure is observed. The difference of ten in the
  cumulative ratio comes from startup/transition frames; no counters are hidden.
  A local snapshot is saved in `out/fg-gameplay-validation/live-02`.
  FG switched off with `pause_or_frontend` at 13:34:15.260. The user intentionally
  closed SSX at 13:34:21 rather than completing the requested pause/resume check.
  This records a suspension and intentional exit, not verified pause/resume or
  full renderer teardown: the application logs its existing hard-exit path.
- Live FG timing capture `timing-268795124855700.csv`: 300 completed tokens match
  300 ordered NVIDIA driver reports with no report/identity/order errors and no
  aborted frames. It contains 299 complete input/DLAA/present chains and 146
  verified worker/physics input snapshots. Two unbound input calls/incomplete
  capture chains remain in the analyzer, so its whole-capture input-handoff
  verdict is false; this is not a claim of full Reflex certification. The measured
  driver interval excludes physical input latency and display scanout.

`scripts/play_fg.bat` starts the presenter with the original `out/test-user` save,
3x DLAA, Reflex On and local paired-capture support. It leaves generation disabled.
After validation, `-EnableExperimentalFG` opts in, or `d3d12_ssx_fg_enable` can be
changed live. `scripts/play_dlaa.bat` and `scripts/play_reflex.bat` remain fallbacks.
The generated-frame rate is reported by `SSX_FG_STATS`; the existing FPS overlay
continues to report rendered frames.

**Limitations:** The HUD-free path currently accepts only the observed 3x SDR
full-screen composition and table gamma ramp. No game UI-alpha buffer is supplied;
UI recomposition is not enabled. NVIDIA can use the HUD-free image, but HUD quality
and large menu overlays still require validation. No native HDR is added here.
The unpaused paired capture `frame-60587-268015619894700` records the exact LUT.
`prepare_fg_color_replay.py` and GPU `--fg-color-replay` reproduce the 3360x1752
source's full-screen resize and display conversion. The three central grid
regions have every pixel within two 8-bit codes of the final guest image, with
median/p90 error zero. Larger differences occur in HUD regions. GPU arithmetic
checks cover 76,962 channels with maximum error two codes and no D3D12 errors.
The independent CPU alignment search selects zero source-pixel offset and a
central mean RGB ratio of 0.99996, without fitted exposure. This verifies one
unpaused outdoor race capture, not every game effect or HUD composition.

The integration follows the pinned [NVIDIA Streamline 2.14.1 FG guide](https://github.com/NVIDIA-RTX/Streamline/blob/v2.14.1/docs/ProgrammingGuideDLSS_G.md).

## Earlier foundation


## Code and tests

`patches/rexglue-sdk-ssx-fg-reflex.patch` adds the Streamline FG/Reflex backend,
retained SSX motion, immutable post-effects capture, display-color prototype and
native timing capture. It applies after `rexglue-sdk-ssx-quality-races.patch`
and its prerequisite chain. Forward, reverse and residual patch checks pass;
the SDK's real Git index and unrelated libmspack changes were not modified.

Patch SHA256:
`2b78c38975255620db243b1ec79d0f8f7b4f794d67ce8156e22e7e874dd54d3d`

Follow-up `rexglue-sdk-ssx-reflex-lineage.patch` adds explicit simulation sample
generations and an ordered pre-render GPU marker. See
[Reflex timing](ssx-reflex-timing.md#sample-lineage-and-ordered-gpu-marker-checkpoint)
for tests and live race validation. `rexglue-sdk-ssx-frame-tokens.patch` then
adds bounded NVIDIA token transport through actual DLAA and presentation,
verified in an unpaused race. The subsequent `ssx-reflex-coupled` patch enables
real Reflex pacing in an opt-in 30 Hz race path, with actual worker input,
published-sample ownership and matched NVIDIA driver reports. The next
`ssx-reflex-frame-driven` patch removes the 30 FPS rendering coupling while
preserving fixed-step physics and original interpolation. Its corrected race
trace records 333 DLAA presentations in five seconds, verified snapshot delivery
to all 150 physics jobs, and no timeline errors. Gameplay FG remains disabled.
See the current [Reflex checkpoint](ssx-reflex-timing.md).

The backend uses a shared token for simulation, render, SR, FG and Present;
requires valid depth, motion, camera/jitter and post-tone-map HUD-free color;
requests one generated frame; handles suspension, reset and resource-consumption
fences; and refuses FG activation without Reflex. Load/unload requires draining
and recreating the swap chain. The token follow-up serializes Streamline API
calls internally. Callers still own submission order and GPU resource lifetimes.

The analytic test runs real DLAA and FG on a moving foreground over a static
background with known depth, motion, jitter and UI alpha. It tests suspension,
history resets, invalid extents, resizing and an unloaded fallback. The earlier foundation
run produced 221 presentations from 111 4K rendered frames and 79 from 40 resized
frames, with zero D3D12 validation errors and clean shutdown. This fenced test
is not a performance benchmark. See [Reflex timing](ssx-reflex-timing.md) for the
driver reports and live SSX trace.

`scripts/test_streamline.ps1 -FrameGeneration` runs SR, fallback and FG probes.
The visible FG validation window must receive focus. Tests use the signed
production Streamline 2.14.1 runtime and DLSS 310.9.1.0, Preset L.

## SSX inputs

`SsxReconstruction` can retain the exact motion texture, camera, jitter and reset
used by successful DLAA evaluation. A bounded pool holds textures until both
their producer fence and CPU consumers release them. Unit tests cover reuse and
ownership; GPU readback confirms static retained vectors exclude jitter and
simultaneously held frames retain distinct textures. Live races report
`retained_motion=true`.

The collector copies the post-effects input to SSX's final fullscreen scene draw,
before the HUD. A paired capture retains that source, final guest image, and
exact display gamma ramp. An unpaused capture shows the rider/terrain without
HUD in the source. The later paused capture is unsuitable for validating final
color alignment because the pause overlay darkens the final image.

The current presenter checkpoint above connects and analytically GPU-tests
`SsxFGColor`. Real guest color agreement and HUD behavior remain unverified.

## Remaining work

- Validate broader transitions and pause/resume in the new render-driven Reflex
  path. Fresh host input and faster rendering pass the race trace; physics and
  controller processing still run at 30 Hz, with no new render-rate camera response.
- Validate the post-effects color conversion and game HUD handling for FG.
- Runtime-verify the presenter handoff and its reset/suspend behavior in races,
  loading, camera discontinuities, pauses and resizing.
- Check moving rider/terrain/HUD quality and frame pacing in actual races.

`scripts/play_dlaa.bat` remains the working reconstruction path. The diagnostic
launchers preserve `out/test-user`. Generated code, game files and captures stay
local and ignored by Git.
