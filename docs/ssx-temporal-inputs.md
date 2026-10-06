# SSX jitter, scene color and exposure checkpoint

This records the earlier checkpoint. See the subsequent
[alignment/exposure checkpoint](ssx-temporal-alignment.md) for structural shader
recognition, corrected lighting/clip interpolation, early depth and the chosen
automatic-exposure policy.

This is an experimental input-processing checkpoint on the pinned ReXGlue
v0.10.0 SDK (`f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`) plus the preceding
SSX patches. DLSS SR, Frame Generation and HDR display output remain disabled.
The original guest tone mapping and presentation still produce the displayed image.

## Implemented

`rexglue-sdk-ssx-temporal-inputs.patch` adds:

- `SsxSceneColor`: a fenced, three-slot D3D12 compute pass that produces a separate
  FP16 linear RGB texture before SSX's tone curve, plus an R32_FLOAT 1×1 exposure
  candidate and GPU diagnostics. Missing/ambiguous resources and unsupported
  fetch conventions reject the input. Repeated paints reuse the same result.
- Opt-in raster jitter for 55 audited world/rider vertex shaders. A 16-phase
  Halton(2,3) sequence is indexed by guest frame, expressed in **host scene pixels**,
  and inserted into the guest-to-host clip offset after the viewport cache. It
  affects geometry rasterization, including depth, rather than moving the final
  image. Identical draws do not accumulate jitter. Unrecognized shaders are skipped.
- Unjittered camera matrices remain in the frame packet. Camera reprojection
  removes current jitter before inverse projection. The native rider pass
  rasterizes with current jitter but excludes it from previous-minus-current
  motion. Previous-frame jitter is deliberately absent from those vectors.
- Metadata records actual jitter, applied draw count and skipped candidates.
  Native capture JSON version 2 and the replay tools preserve jitter. Readers
  still accept version 1 captures and packed replay metadata.

All new renderer flags default to false. `d3d12_ssx_jitter` requires input
collection and the SSX title ID; the shaders are further gated by the known
scene viewport and reversed-depth clip convention. This is **partial scene
jitter**, not a validated full-scene temporal integration. Camera vectors for
unjittered surfaces cannot yet be treated as correct when this mode is enabled.

## Color and exposure evidence

The captured full-scene tone-map draw uses VS `E857B4AF9617DC29` and
PS `A2CD8E82699B2C0A`. The guest pixel shader:

1. Samples fetch 0, squares RGB, retaining its original channel order through
   the shader's input/output permutations.
2. When b128 is true, samples fetch 1 at explicit LOD c1.x and computes
   `exposure = c0.x * exp2(-c255.x * sampled_value)`.
3. Multiplies linear RGB by that exposure, applies a rational tone curve using
   c2–c8, then encodes the result with square roots.

The new pass stops before steps 2–3 for its color output. It applies the signed
six-bit texture exponent adjustment before squaring. The supported captured
color representation is an FP16 view with identity host swizzle `0x688` and
signed component metadata `0x55`; other sign/swizzle conventions are rejected.
There is no clamp to [0,1]. Values not representable in FP16 or nonfinite values
produce zero and are flagged in sampled diagnostics; this is not an HDR validity
guarantee for an entire frame. Alpha is 1, not a reconstructed transparency mask.

At 3×, the copied exposure mip is **3×1**, not 1×1. SSX samples it using the scene
UV. Only a uniform mip has one exact exposure independent of UV/filtering. The
compute pass examines every exposure texel and emits a positive scalar only
when they are exactly equal. Even a one-UNORM-step difference produces zero
and `scalar_valid=false`; it is never silently averaged. Diagnostics retain the
minimum and maximum resulting multipliers. When SSX disables its exposure
branch, the multiplier is 1 without requiring a log-exposure resource.

This reconstructs the multiplier used by the observed tone-map shader. It does
not prove physical luminance units or that earlier lighting shaders never
pre-expose their output. Streamline's `preExposure` value still needs a producer
audit. Preserving the values available here also cannot restore highlights
already clipped by an earlier guest pass.

The current official [NVIDIA DLSS guide](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS.md)
requires separate jitter information and matrices without jitter. It describes
a 1×1 exposure input and automatic exposure as an alternative. This checkpoint
does not set DLSS constants or evaluate DLSS. Its host-pixel displacement still
needs verification against Streamline's integration convention at that boundary.

## Verification

Built and installed the modified SDK, rebuilt SSX, and staged the matching
runtime/plugin in `out/build/win-amd64-streamline`. The older release build is
the rendering fallback. CMake now also stages the runtime and dynamically loaded
GPU plugin on builds that do not relink SSX. A plugin-only rebuild previously
left an obsolete DLL beside the executable; matching installed/staged SHA-256
hashes verified the correction.

- 9 SDK SSX unit cases pass, including phase/scale selection, mode-change resets,
  stationary jitter cancellation, history and pool lifetime cases.
- 10 scene-color GPU cases pass: signed encoded RGB, exponent adjustment,
  highlights through 36, exact exposure math, branch-off, nonuniform exposure,
  resource reuse and resize. Tests include 3360×1752 and 3840×2160 allocations.
- 19 camera GPU cases pass, including changing jitter with a stationary camera
  and with camera motion. Maximum error is below 0.00047 pixels.
- 19 native geometry GPU cases pass, including jitter with deformation and
  combined camera/object motion, plus the previous non-fused projection regression.
  Maximum error is below 0.00037 pixels.
- All three GPU probes report **zero D3D12 validation errors**.
- A separate synthetic version-2 capture/replay with nonzero current/previous
  jitter passes: 1,024 native pixels, zero outside the projected mesh, zero
  nonfinite vectors and zero error in 805 interior CPU-reference comparisons.
- The SSX scene-hook CTest passes. Launcher dry-run selects the original profile,
  3× ROV and the expected experimental flags.

These analytic/synthetic GPU checks are distinct from the live checks below.
No new 720p gameplay tests were used.

## Live race checks and corrected regression

The first five-shader jitter experiment caused severe purple material corruption.
Turning jitter off while leaving color/exposure diagnostics on restored normal
colors, confirmed by the user and desktop inspection. The clean run recorded 59
color diagnostic packets, zero invalid sampled pixels and a maximum sampled
linear channel value of **36.753906**. All 59 exposure candidates were nonuniform.
The corrupted run's peak of 984.3906 must not be used as highlight evidence.

Source inspection of the existing capture showed that the rider's later material
draws used different vertex shaders from its normal/depth draws. For example,
EID 3128 used VS `9897AE6BB3FD8C0B`, while later rider material draws included
`C68B1C50089DDABE`, `6AC767636C7FD2DD`, `3B73061891C4300C` and
`D3AFB22410B2671E`. The first experiment displaced only the earlier draws.

The allowlist now includes both passes and other captured shaders with the same
straight-line c192–c195 projection-to-position pattern. A local signature audit
finds 55 hashes covering 755 of 887 full-scene draws in the existing capture;
that count does not include runtime clip/depth gating. A regression check covers
the rider's material-pass shader selection.

With this change, the user repeated the same race **with jitter enabled** and
confirmed normal colors; desktop inspection agreed. Example live counters report
732 jittered draws and 147 skipped candidates, then 633 and 99 as the view changes.
Thus the visible purple regression is corrected for this race, but full-scene
coverage, screen-space sampling alignment, camera cuts and other tracks remain
unverified. The SDK flag and normal launcher still default to jitter off.

The three local logs are `ssx_009.log` (failed first jitter experiment),
`ssx_010.log` (clean color-only comparison) and `ssx_011.log` (expanded jitter).
Filtered evidence, shader audit and binary hashes are saved under ignored
`out/temporal-validation/`. The original profile was backed up before testing.

## Running the checkpoint

Close an existing game before switching builds. Run
`scripts/play_temporal_inputs.bat` for color/exposure diagnostics and existing
camera/native motion, with the original `out/test-user` save and 3× configuration.
Append `-Jitter` for the temporary partial-scene jitter experiment. Without
reconstruction, subpixel shimmer is expected. `scripts/play_native_motion.bat`
explicitly disables both new flags for the previous rendering behavior.

During a normal race, ride for 10–15 seconds. The local runtime log contains
`SSX_COLOR_GPU` and, if enabled, `SSX_JITTER`. Check scalar validity, exposure range,
linear peak, invalid sample count and skipped draw count. Do not infer complete
jitter coverage from the applied count: the skipped counter counts only draws
passing the viewport/clip-state gate, and excluded shadows or post effects need
separate analysis. The game does not need to stop moving for this log check.

## Rebuilding

Apply the incremental SDK patches through `rexglue-sdk-ssx-native-motion.patch`,
then `rexglue-sdk-ssx-temporal-inputs.patch`. Its forward/reverse application and
selected-source residual diff were checked. Compiled shader bytecode is included.
After shader edits, use `tools/scene_color_probe/rebuild_shader.ps1` with
`-SdkSource` and the Windows SDK's `fxc.exe` path. Build/install the SDK and build
SSX with the existing `win-amd64-streamline` preset.

`tools/scene_color_probe/CMakeLists.txt` builds the standalone D3D12 probe using
`CMAKE_PREFIX_PATH` set to that installed SDK. The camera and native probes remain
under `tools/motion_probe` and `tools/native_motion_probe`. Captures, logs, binaries,
generated code and game assets stay in ignored local directories.

## Remaining before DLSS evaluation

The live exposure field is nonuniform, so it cannot be passed directly as one
scalar. Investigate DLSS automatic exposure while preserving SSX's spatial
exposure for its own tone map; finish the earlier lighting/pre-exposure audit;
expand and verify jitter coverage for terrain variants, sky, particles and other
world passes; validate color/depth/motion alignment and HUD exclusion; and choose
where reconstructed color re-enters SSX's postprocessing. The current side buffers
are attached to presentation for diagnostics, not substituted into the guest
pipeline. Then connect NVIDIA frame tokens, constants, tags and evaluation with
the established history/reset handling and a complete input validity decision.
