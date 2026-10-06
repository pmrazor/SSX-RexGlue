# Native HDR composition checkpoint — 2026-10-05

The graded native scene and bloom/world/HUD path are runtime-confirmed. A follow-up
fixes HUD multiply blending and restores the authored post-effect grading
coordinates; the user confirmed the result looks much better. Native HDR + 3x
Frame Generation is now implemented and runtime-confirmed. The user confirms the
Tricky transition corruption is fixed. Live measurements verify that removing the
incorrect subtraction of the transition LUT's bright zero-input value preserves
the intended colors. This is a working gameplay checkpoint at 1,000 nits; the
latest race counter still includes 400 SDR-fallback frames out of 12,240 (3.27%).
Native HDR is not guaranteed for every frame or untested effect. See below for evidence
and the correction to the earlier claim about the last HDR-only session.
The user tested the 1,000-nit version and reported that it looks good, requesting
slightly brighter highlights. The follow-up adds a separate highlight-strength
control and a shadow correction after the user reported raised blacks. The
3,000-nit target is supported for the user's TV; it is tested by GPU
readback, not displayed at 3,000 nits on the Alienware. The implementation restores
the original filmic tone curve and live color-grading LUT for the retained native
scene, with an HDR extension for highlights, original bloom, late world draws and
the game HUD as described below.

## Implemented

- Retain the actual FP16 **DLAA output before SSX's SDR tone mapper**, with the
  same frame identity and bounded, reference/fence-protected ownership as the
  temporal inputs. Values above one reach the HDR pass; this is not an inverse
  tone map of the final SDR image.
- Apply SSX's spatial exposure field using its fetch exponent, exposure key,
  log scale and point/linear clamp sampler. Unknown sampler, resource, frame or
  exposure contracts fall back to the complete SDR image mapped into HDR.
- Capture the original 32x32x32 grading LUT and final post shader's live c0..2
  constants each frame. Copy the volume into the same bounded, fence-protected
  frame packet as the native scene; a borrowed texture-cache pointer cannot
  preserve contents. No game LUT asset is bundled or checked into Git.
- Apply the original per-channel rational filmic curve, square-root encoding,
  LUT coordinate scale/offset, texture channel order and filtering, and output
  gain/offset. Apply the matching frame's 256-entry display gamma ramp with
  interpolation, then convert display codes to linear display light. Intermediate
  native HDR color is not quantized into the original 8-bit SDR render targets.
- Preserve highlight intensity from the retained FP16 scene separately from the
  SDR LUT lookup. Solve the original curve for its SDR white point (about 3
  exposed scene units in the captured race). Above it, normalize RGB uniformly
  for the LUT lookup and extend the graded color's brightness toward the selected
  peak using the native scene intensity. Different bright values remain distinct
  even where the original SDR tone map/LUT would clamp. This highlight extension
  is an HDR adaptation, not a claim that the original game authored an HDR LUT.
- Anchor the validated tone curve at exact zero before grading, and remove the
  display gamma ramp's black offset. A zero-preserving grade and the calibration
  background output exactly PQ code zero, with near-black steps retained. Authored
  LUT light, including Tricky's transient flash colors, is preserved. Subtracting
  the live LUT's zero-input color was incorrect and is removed by the correction
  below. The earlier approximate shadow toe is unused in the graded path.
- Convert Rec.709 primaries to Rec.2020 and encode absolute ST.2084 PQ into an
  `R10G10B10A2_UNORM` swap chain. Unknown/missing/duplicate LUT passes, unsupported
  fetch contracts or display ramps fall back to the complete SDR image mapped
  into HDR; they never silently show the ungraded native scene.
- Negotiate `RGB_FULL_G2084_NONE_P2020` only when Windows HDR is active on the
  containing output and DXGI supports presentation in that space. Recheck after
  display moves/changes; use SDR codes and SDR color-space signaling otherwise.
  HDR10 metadata describes the selected peak, not a guessed frame-average value.
- Composite the host FPS overlay in linear display light with an independent
  UI white setting. Menus, pause screens and unsupported frames show the complete
  SDR game at UI white. The regular SDR/DLAA/FG builds remain available.
- Include small-area peak/paper-white/UI calibration patterns. Peak controls the
  luminance ceiling, not overall brightness. The setting is not silently capped
  to the current monitor's reported peak.

## Running

From the repository, run `scripts/play_hdr_fg.bat -Monitor 1 -Keyboard` on
the current PC. This uses the corrected `out/build/win-amd64-hdr-fg/ssx.exe`, the
original `out/test-user` save, 3x ROV, MSAA off, DLAA, Reflex and 3x FG. Windows HDR
must be on. The normal launcher leaves the detailed HDR light trace disabled.
The actual race scene is 3360x1752, with a 3840x2160 final display image.
The launcher enables `d3d12_ssx_hdr_layers`; append `-HDRSceneOnly` to select the
previous graded-scene diagnostic path. `-FGMultiplier 2` selects 2x generation.
`scripts/play_hdr_preview.bat` points to the preserved earlier HDR-only runtime;
it does not contain this latest grading correction.

| Option | Default | Range / purpose |
| --- | --- | --- |
| `-HDRPeakNits` | 1000 | 400–3000, display peak target |
| `-HDRPaperWhiteNits` | 200 | 80–500, diffuse white; below peak |
| `-HDRUINits` | 200 | 80–500, game HUD, host overlay and frontend white; at most peak |
| `-HDRHighlightBoost` | 1.25 | 1–4, strengthen highlights without changing diffuse white or peak |
| `-HDRExposureEV` | 0 | −5 to +5, overall scene exposure compensation |
| `-HDRCalibration` | off | Show small calibration patches instead of gameplay |

For the TV, select its monitor index and append `-HDRPeakNits 3000`. Leave paper
white/UI brightness independent of this change. `-HDRHighlightBoost 1` disables
the added highlight boost; original grading and the zero-black adjustment remain. Calibration's top patch compares 90% of
the selected peak on the left against the full peak on the right; the middle
bar is paper white and the bottom bar UI white. Settings other than the pattern
toggle require relaunching. Display tone mapping and physical peak need visual
calibration on the TV; GPU code values are not a luminance-meter measurement.

## Validation

The graded SDK and SSX build; 61 relevant SDK tests and both game CTests pass.
The standalone `tools/hdr_probe` checks native highlight separation above 1,
absolute PQ nits, nonuniform spatial exposure, independent opaque/translucent UI,
SDR frontend/output fallback, exposure compensation, calibration, full 4K output
and slot reuse after resizing. Initial run: 13 cases, 312 channel comparisons,
maximum error **one 10-bit code**, **zero D3D12 validation errors**, clean exit.
Logs are local under ignored `out/hdr-validation/`.

After the brightness/black feedback, the updated probe passes **17 cases / 408
channel comparisons**, again with at most one code of error and zero D3D12
errors. These additionally check the 1.25 highlight boost at both peaks,
exact 10-bit black code zero, and a strictly increasing near-black step series
with the shadow curve enabled. Mathematical zero does not measure the panel's black.

The grading revision passes **27 GPU cases / 648 channel comparisons**, with
maximum error **one 10-bit code**, **zero D3D12 validation errors**, and clean
exit. Added cases use identity, non-separable colored, lifted-black and changed
LUTs; point/trilinear volume filtering; changed display gamma; independent UI;
and 1,000/3,000-nit highlight/near-black checks. Negative checks reject missing,
duplicate or mismatched inputs and altered constants. The CPU reference is based
on the captured shader instructions and quantized synthetic volume nodes. This
is not a whole-image comparison against a captured original frame: later world
passes and bloom are still absent. Logs: `out/hdr-validation/gpu-grading-02.log`.

The graded 1,000-nit build is staged and launched with the original profile.
Live `ssx_005.log` confirms `SSX_HDR_SCENE ... native=1 ... grading=1`, a captured
LUT with swizzle `60A`, no failed/duplicate LUT capture, and successful 3x DLAA.
A local diagnostic snapshot is under `out/hdr-validation/live-grading/`. These
are sampled diagnostics, not a rendered-frame count. The user's visual
comparison remains pending. The log also reports LUT capture/failure flags
and swizzle to distinguish active native grading from the SDR fallback.

Live `out/build/win-amd64-hdr/logs/ssx_003*.log` confirms HDR10/Rec.2020 transport,
successful metadata submission, 1,000-nit selected/reported peak, and native HDR
scene consumption alongside successful DLAA at 3360x1752. The user confirmed
the preview looks good and requested stronger highlights. Screenshots cannot
establish physical brightness or HDR clipping. Multi-display transitions and TV
output are not yet runtime-verified.

## Bloom, later world effects and game HUD

The saved race capture places the full-scene tone mapper (`A2CD8E82699B2C0A`) at
EID 8121. Numerous world/transparent draws follow it. The final post shader
`86455B1D2BA4A035` at EID 9727 adds bloom and samples a 3D color-grading LUT; the
final copy at EID 9754 is followed by game HUD draws. These stages use SDR 8-bit
storage and cannot be treated as an already-HDR image.

The new renderer translation records actual surviving world/HUD fragments into
RGBA16F layers alongside the original ROV color writes. Each layer has two planes:
source contribution RGB and per-channel destination transmission RGB, with
composition `C + base*T`.
Depth/stencil testing, alpha rejection, scissor coverage, shader discard and draw
order remain governed by the guest draw. Source/destination alpha, source-color,
additive and source-factor destination-color RGB blends are supported. The latter
implements SSX's multiply HUD quads. Non-affine destination-color factors,
unsupported operations, partial RGB writes and unknown target layouts reject
the native composition for that frame. This uses
actual draws, not an image-difference estimate of HUD opacity. Depth-only helper
draws are recognized using their effective ROV keep masks.

World collection begins after the tone map and ends before the captured FXAA
pass (`19A8B131C04F61F7`). Auxiliary effect targets still run normally; their
upsampling/composition into the main target contributes to the world layer.
The final grading pass supplies a copied quarter-resolution bloom texture and
live bloom gain. The HDR pass composites world draws in their authored code
space, adds the original squared bloom contribution, and applies the original
LUT. Native highlight intensity remains separate and is attenuated by foreground
transmission, so an opaque world effect cannot expose a hidden bright surface.

HUD collection begins after the final scene copy. A bounded scene base is mapped
back through the display ramp and composited in the original guest code space,
then converted to display light at `HDRUINits`. The separate HDR highlight residual
is attenuated by RGB transmission. This preserves colored multiply blends and
avoids treating authored code-space alpha as linear-light alpha. Empty HUD pixels
are an exact identity; a nonzero display-ramp origin cannot raise scene blacks.
The normal SDR render still runs as a complete fallback. Layer textures belong
to the same bounded, frame/fence-owned packet as the native scene and LUT. Two
extra transmission planes add about 127 MiB to EDRAM allocation plus the retained
transmission textures in occupied frame-pool slots; the performance cost of this
correction has not yet been profiled in a race.

This is a mixed-precision HDR adaptation: the retained scene and new layers are
FP16, while original offscreen particles/bloom textures still have their guest
SDR precision. It does not recover values already clipped inside those effects.
The native path omits the late SDR FXAA filter; DLAA still processes the earlier
scene, and later transparent edges require visual assessment. Intermediate
8-bit per-draw quantization/clamping is not reproduced in the FP16 layers.
These are concrete limits, not a claim that every original post-process target
has been converted to native HDR. The HDR + Frame Generation handoff is described
below; explicit UI recomposition remains unfinished.

### State-dependent grading and Tricky clipping

The final-post adapter recognizes these audited microcode programs:

| Pixel shader | Operations beyond bloom and LUT |
| --- | --- |
| `86455B1D2BA4A035` | Default grade |
| `418794134AA3A6AF` | Linear-color overlay before the LUT |
| `94262AE20BE5EC7A` | Colored radial vignette after the LUT |
| `88095FB3FE6C244D` | Two post-LUT overlays and radial modulation |
| `48CD7B80EE1F1D67` | Pre-LUT overlay plus both post-LUT overlays |

The packet retains each used texture and c3/c4/c196–199/c255 from that exact
frame. RGBA8, FP16 and the live Tricky BC3 overlay textures are retained without
reinterpreting storage. Sampler clamp/wrap, point/linear filtering, component swizzle and the
original operation order are preserved. Missing resources, unsupported sampler
states, unknown shaders or degenerate radial parameters use the complete SDR
fallback. The last two variants have GPU reference coverage; their live textures
have now been captured in a live race. Visual transition quality remains unresolved.

The guest clamps bloom-amplified colors per channel at its SDR LUT boundary and
final 8-bit target. The current adapter preserves those authored LUT coordinates
and display bounds, carrying excess intensity separately through the HDR
highlight shoulder. An earlier version normalized bloom/overlay RGB uniformly
before the LUT and final display mapping; the user reported overly saturated
Tricky colors, so that added normalization has been removed. Native scene
highlight extension remains in place. Foreground transmission attenuates native
highlight energy; highlights cannot leak through an opaque effect. This keeps
the original LUT and authored effect coefficients while adapting their clipped
highlights to HDR. It cannot restore precision already lost inside guest
offscreen effect textures.

The reported solid HUD rectangles exposed a separate enum error: Xbox blend
factor 8 is destination color, while destination alpha is 10. The initial scalar
layer path treated 8 as alpha. The corrected translation uses the SDK's named
Xbox factors and RGB transmission. A white multiply quad now leaves the
background untouched; colored multiply quads attenuate each channel correctly.

### Validation of the new composition

- SDK/game build; **64 relevant SDK tests and both game CTests pass**.
- `tools/hdr_probe`: **56 cases, 1,344 channel comparisons**, maximum one 10-bit
  code error, zero D3D12 errors. Includes transparent, opaque and additive world
  and HUD layers, original bloom, combined layers, zero black, missing/unsupported
  inputs, RGB multiply layers, lifted gamma with empty HUD, all audited post variants, strong bloom
  highlight separation, invalid post samplers/radial parameters, and both
  1,000/3,000-nit outputs. Log: `out/hdr-validation/hdr-rgb-composition-probe-01.log`.
- `tools/hdr_layer_probe`: the actual translated, locally supplied SSX color
  shader executes through ROV. **21 cases / 126 channel checks**, zero D3D12 errors:
  ordered alpha, additive, source/destination-alpha factors, disabled capture,
  alpha/depth/scissor rejection, effective color-mask disabling, nonzero pixel
  coordinates, bounds, colored/ordered multiply and white multiply identity.
  An additional independent comparison checks reconstructed RGB against the
  original Xbox ROV output, allowing its per-draw 8-bit rounding. Log:
  `out/hdr-validation/hdr-rgb-layer-probe-01.log`. No guest microcode is bundled.
  `reference.hlsl` is the corresponding new ROV operation sequence, compiled
  with FXC `/Od` and `/O3`.
- Local replay of the saved race's bloom/grading pass matches its captured output
  within one 8-bit code. This validates that pass, not the entire new HDR image.
- Live tests `ssx_006`–`ssx_008` captured world/bloom/HUD but rejected native
  composition. Corrections now skip effective depth-only masks, identify the
  main target using pitch rather than every draw's viewport, and restrict the
  tone-map boundary to the full-scene viewport. Smaller tone-map passes must not
  reset the world layer. A detailed fault mask distinguishes boundary, clear,
  snapshot and unsupported-draw failures.
- During `ssx_008`, the user activated Tricky. The log records final shader
  `418794134AA3A6AF` / vertex shader `4C38FF234A403414`, with a live 840x438
  BGRA pre-grading input and wrap sampler. The original default grade and the
  `94262AE20BE5EC7A` vignette variant also occur. The previous build omitted these
  variants and remained in SDR fallback; it is not evidence of native HDR Tricky.
- `ssx_010` confirms native world/bloom/HUD composition through the default and
  pre-LUT overlay variants: `native=1 grading=1 layers=1`, `failed=false faults=0`.
  The user reported excess Tricky saturation and solid HUD rectangles in this
  build. These logs are archived under `out/hdr-validation/tricky-hud-feedback/`.
- The RGB transmission and authored grading corrections pass the checks above.
  The user confirmed the corrected 1,000-nit image looks much better. `ssx_011`
  records native composition with complete world/bloom/HUD layers and no layer
  faults, including the pre-LUT overlay variant. This is one tested race, not
  exhaustive visual coverage of all effects.

### Native HDR + Frame Generation

The HDR composition compute pass now writes a second RGB10/PQ output immediately
before game HUD and host overlay composition. It shares scene exposure, original
grading, bloom, later world effects, highlight shoulder, Rec.2020 conversion and
PQ encoding with the displayed image. FG never receives the old SDR HUD-free
texture when the backbuffer is HDR10. This follows NVIDIA Streamline 2.14.1's
HDR10 format and matching post-process/color-space requirements.

The six-slot FG color pool retains this target and its source packet, including
depth and immutable DLAA motion, until both the producing submission and NVIDIA's
independent input-consumption fence complete. Unknown consumption quarantines a
slot and disables generation. Pool exhaustion suspends FG instead of reusing an
in-flight texture. RGB10 HDR and 8-bit SDR are explicit, checked input contracts;
FP16/scRGB is rejected for the FG display-color input.

Generation requires a fresh, valid unpaused race frame, matched DLAA/Reflex token,
complete HDR world/HUD composition and a focused window. Calibration, menus,
pause, missing inputs and SDR fallback suspend it. Display/color-space or HDR
calibration-setting changes drain pending reads and require a shared history
reset before resuming. Resolution/fullscreen transitions retain the existing
FG suspension and swap-chain recreation path. Changes between final post shaders
also skip FG for the transition frame and request a shared reset on the next
DLAA/FG token, without overwriting already-submitted DLAA constants. Both 2x and 3x are supported; the
overlay distinguishes rendered FPS from NVIDIA's actual presentation count.

No scalar game UI-alpha is invented: SSX's RGB multiply HUD and nonlinear PQ
composition do not have the original SDR alpha contract. NVIDIA receives the
complete final image and the matching HDR HUD-free image and uses its HUD
extraction path. Explicit UI recomposition remains unimplemented. HUD quality
on generated frames therefore still needs visual assessment.

Validation for this integration:

- SDK and game build; **67 SDK tests and both game CTests pass**.
- HDR GPU probe: **59 cases / 1,416 final-color channel comparisons**, maximum
  one 10-bit code error, plus **768 HDR HUD-free channel comparisons**. Includes
  original post variants, multiply HUDs, both brightness targets, a full 4K pass,
  rejected SDR/calibration targets and six-slot exhaustion/consumer-fence reuse.
  BC3 Tricky overlays with the observed sampler contract. **Zero D3D12 validation
  errors**. `out/hdr-fg-validation/composition-tricky-02.log`.
- Real HDR10 3x FG probe: **333 presentations / 111 rendered 4K frames**,
  **120 / 40** after resizing to 2560x1440, and **80 / 40** after switching to 2x.
  Nine suspension cases, four explicit resets, invalid extent/color-contract
  checks, unloaded fallback and teardown pass. FG status zero; 263 matched,
  ordered Reflex driver reports and **zero D3D12 validation errors** in the
  debug-layer run. Logs: `out/hdr-fg-validation/presentation-debug-02/`.
- Live `win-amd64-hdr-fg/logs/ssx_001.log` confirms `native=1 grading=1 layers=1`
  alongside active `HDR10_PQ_2020` FG. At 1,080 rendered frames NVIDIA reports
  3,220 presentations. After startup each measured 120-frame interval adds 360,
  with status zero and advancing input-consumption fences. Startup/transition
  presentations remain included in the cumulative count. Observed output was
  about 150–168 FPS, not a controlled performance benchmark. Later samples reach
  5,880 rendered / 17,620 presented frames with the same steady 3x ratio.
- The user reported a very brief bright/colored flash when both Tricky levels
  activate. The saved log shows `88095FB3FE6C244D` uses two 1280x720 BC3_UNORM
  overlays at tf3/tf4. The first HDR + FG build rejected that format (`faults=80`
  hexadecimal), switched to SDR-mapped fallback, and suspended generation. The
  correction copies and samples those compressed textures in their original
  format and resets FG history on post-program transitions. Its GPU checks pass;
  the next run captures both textures (`textures=6 faults=0`) and native HDR,
  but the user confirms the flash remains. Original evidence:
  `out/hdr-fg-validation/tricky-transition-01/`.

The 3,000-nit TV setting passes offscreen color checks. Visible FG testing uses
1,000 nits on the current PC; TV output has not been runtime-verified. The preserved
HDR-only runtime remains under `win-amd64-hdr`; the new build is separate under
`win-amd64-hdr-fg`. The SDR FG launcher is unchanged.

## Rebuilding and review

Apply `patches/rexglue-sdk-ssx-hdr-preview.patch` after
`rexglue-sdk-ssx-fg-multiframe.patch`, which follows `ssx-fg-gameplay`. The patch
contains SDK source, tests and compiled shader bytecode. Game launcher/overlay
and the GPU probe are separate repository changes. No game assets are included.
The patch is exported with a disposable Git index; unrelated SDK changes and
the real index are preserved.
Current patch SHA256:
`7c4a5e23a305b5c12316d462671460e3ae31501334f2a7d1585ae8269708a63a`.
Apply the grading follow-up `patches/rexglue-sdk-ssx-hdr-grading.patch` after
that preview patch. Grading patch SHA256:
`8b1522e8939373543dfb1289cea90aa4a5ffde2ddbe72acc1686ce652c629e29`.
Apply `patches/rexglue-sdk-ssx-hdr-layers.patch` after grading. Layers patch SHA256:
`e2b295bf29f2b33a1a2add181124877d81b2a62961492407e5950d98f1762b3b`.
All three patches passed forward/reverse application and zero residual source checks.
The working graded scene build is backed up under `out/hdr-validation/pre-layers-runtime/`.
The staged RGB correction hashes and test counts are in
`out/hdr-validation/layers-rgb-build-hashes.json`. The candidate is running at
1,000 nits with the original save; the user confirmed its improved appearance.
The preceding native layers build is backed up under
`out/hdr-validation/pre-rgb-layer-runtime/`.
The prior HDR executable/runtime are backed up locally under
`out/hdr-validation/pre-grading-runtime/`; normal SDR/DLAA/FG launchers remain
available. The grading build hashes are in
`out/hdr-validation/grading-build-hashes.json`.

Apply `patches/rexglue-sdk-ssx-hdr-fg.patch` after the layers patch. SHA256:
`eb4a617051fb3e198d736b70ae4db553b9acbf25513f8911abe3302de18714b0`.
It passes forward/reverse application and zero residual source checks using a
disposable index. Current staged HDR + FG hashes are recorded in
`out/hdr-fg-validation/status-build-hashes.json`. The preceding Tricky build hashes
are in `out/hdr-fg-validation/tricky-build-hashes.json`. The prior HDR + FG runtime is
backed up under `out/hdr-fg-validation/pre-tricky-runtime/`.

Build/install the SDK using the existing Streamline preset, then build SSX into
the separate `win-amd64-hdr` runtime output directory. The ordinary CMake cache
is restored afterwards. Recompile the shader with
`tools/hdr_probe/rebuild_shader.ps1 -SdkSource <sdk> -Fxc <Windows SDK fxc.exe>`.
Build the probe against the installed SDK with its `CMAKE_PREFIX_PATH`, then run
it on the host GPU with the game closed for performance measurements. The probe
does not open a window or send 3,000-nit imagery to the monitor.

References: [Microsoft Advanced Color](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/high-dynamic-range),
[DXGI HDR10 metadata units](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_5/ns-dxgi1_5-dxgi_hdr_metadata_hdr10),
[NVIDIA Streamline 2.14.1 FG guide](https://github.com/NVIDIA-RTX/Streamline/blob/v2.14.1/docs/ProgrammingGuideDLSS_G.md).

## Native HDR status and regression checkpoint

The old overlay reported the HDR target, not the actual composition path. That
was insufficient: a PQ swap chain also carried the SDR-mapped fallback. In the
retained portion of the last RGB-corrected HDR-only log (`ssx_011*.log`, roughly
18:54–19:05), **all 511 sampled HDR records report native=0**. The earlier claim
that this session verified the RGB correction in native HDR was not supported.
The user reports that this build looked clean and that the brief Tricky flash
returned after adding FG. Preserve that report; do not assume FG is the cause.
The preserved executable and DLLs still match `layers-rgb-build-hashes.json`.

The saved HDR + FG run in `tricky-transition-02/` has **73 native and 32 fallback
samples**. The samples are 120 guest frames apart and include menus/pauses;
these are neither exact frame counts nor a race-only fallback percentage.

The new overlay shows green **Native HDR**, amber **SDR fallback** with the
rejected input stage, **SDR mapped to HDR | menu/pause/loading**, or **SDR output**
when the display cannot accept HDR. A race-only fallback count and percentage
count unique successfully presented rendered frames, excluding generated frames,
menus, pauses, repeats, calibration and SDR output. An amber recent-fallback
notice remains for five seconds after recovery, making single-frame failures
visible. Counters cover the session and do not reset at each race. Missing frame
timing is shown explicitly and cannot be classified into the race denominator.
`SSX_HDR_PATH` logs every path/post-program transition; `SSX_HDR_COUNTS` provides
exact cumulative race counts. No image or game asset is uploaded.

Regression checks directly compare the current color pass against archived DXBC
confirmed byte-for-byte inside the preserved HDR-only runtime. The same inputs,
constants and descriptors yield **50,116,608 channel comparisons with zero
difference**. With versus without writing the extra FG HUD-free output yields
**25,073,664 comparisons with zero difference**, including full 4K, strong bloom,
Tricky variants, compressed overlays and both nit targets. All 59 color cases
still pass with zero D3D12 errors; 69 SDK tests and both game tests pass.
This checks composition on synthetic inputs, not the timing/resources of the
live flash or NVIDIA-generated images. Logs and the reference shader manifest:
`out/hdr-fg-validation/regression/`. Reproduce with `ssx_hdr_probe
--reference-shader <archived.cso>`; without this flag the with/without-FG-output
comparison still runs. The status build is launched with the same save and
1,000-nit/3x-FG settings. The startup video visibly shows the expected SDR-mapped
frontend label and FG suspended. The player-controlled race subsequently showed
7,849 native frames while the fallback count remained at the initial 311 race-entry
frames, through 19:39:10. The user reports seeing the Tricky flash while the
indicator remains native. Later records contain another missing-input/scene
interval ending at 19:39:58, bringing cumulative fallback to 591. Do not describe
the entire session as fallback-free. Snapshots: `out/hdr-fg-validation/status-live-01/`
and `status-live-02/`. This makes native HDR composition the leading suspect;
the status reports the chosen path, not visual correctness or generated-frame quality.

## SDR world-effect boundary correction

Source inspection found that the affine late-world recording was squared without
restoring the original RGBA8 UNORM target's [0,1] domain. Additive SDR draws can
accumulate contribution values above one in the FP16 recording. These values
were incorrectly promoted to HDR highlight energy by the subsequent excess-light
calculation. The correction clamps the reconstructed world code before the original
post shader's square, bloom and grading operations. Native FP16 scene excess stays
separate, so it retains highlight detail; bloom still enters after the boundary.
This restores the final target boundary, not every intermediate draw's 8-bit
quantization or saturation history, which the affine representation cannot encode.

A new offscreen GPU regression reproduces the defect with FG absent: SDR world
effects over a black scene produce a Rec.2020 channel equivalent to 366.49 nits
at 200-nit paper white. The old shader fails the independent output-bound check.
The corrected shader passes that check at both 1,000 and 3,000 nits, verifies
opaque effects occlude scene highlights, and preserves distinct native highlights
behind additive effects. All **63 GPU cases, 1,512 CPU-reference channel checks,
864 HUD-free checks, 69 SDK tests and both game tests pass**, with zero D3D12
errors. The final image is bit-identical with and without writing the FG HUD-free
output across 25,098,240 channel comparisons. This is a demonstrated HDR defect;
it is not yet proof of the cause of the live Tricky flash.

Evidence: `out/hdr-fg-validation/world-boundary/{before,after}.log` and
`candidate-hashes.json`. The candidate is `out/build/win-amd64-hdr-world`; the
status runtime is backed up in `pre-world-boundary-runtime/`. After the user closed
SSX, the three files were staged and hash-verified in `win-amd64-hdr-fg` and
relaunched at 19:47:57 with the original save, 1,000-nit HDR and 3x FG. The user
retested and the Tricky flash remains, as detailed below. The archived-shader zero-difference test above
describes the preceding status build: this correction intentionally changes world
overflow cases, so comparison to that archived shader will now report differences.

## Tricky transition color diagnostic

The world-boundary fix did **not** resolve the reported flash. The user confirms
it also occurs when Tricky ends and describes briefly corrupted, almost inverted
colors. `world-boundary-live/` preserves the run: native HDR remains selected
through the recorded Tricky post-program changes, with no increase in fallback
frames in that sequence. FG investigation is deferred at the user's request;
the next run retains 3x FG and its existing code/settings.

`--d3d12_ssx_hdr_light_trace=true` enables an opt-in HDR diagnostic shader. It
records 576 sparse samples of the actual calculation per composed frame, before
HUD composition. Three caller-fenced GPU/readback slots bound memory usage;
statistics are read only when the slot's existing presentation fence has completed.
No GPU wait is added for the diagnostic, and no game image or asset is exported.
The trace buffer uses 73,728 bytes per slot on each side of the readback. This is
diagnostic overhead, not a performance configuration. Ordinary launchers leave it off.

Each `SSX_HDR_LIGHT` record includes the guest frame, post-program hash, reset,
scene white point, nonfinite-value count and 32-element mean/min/max arrays.
The arrays contain eight four-component rows, in order:

1. Exposed native scene RGB; original native excess.
2. Reconstructed world code RGB before the UNORM boundary; maximum transmission.
3. Light entering the grading LUT; pre-LUT excess.
4. Post-LUT/effect light RGB; peak output code before display gamma.
5. Native HDR RGB in nits before the HUD; final excess.
6. Matching guest SDR post-effects RGB through its display gamma in nits; validity.
7. Evaluated grading black RGB; pre-LUT overlay alpha.
8. Evaluated grading white RGB; post-effect transmission.

The reference is the original guest post-effects image from the same frame,
not a synthesized SDR conversion of the HDR result. Missing/unsupported reference
inputs have validity zero. `tools/hdr_probe/analyze_light_trace.py` accepts local
logs from **one run**, reports program transitions and the largest color changes,
and optionally writes a local JSON summary using `--output`.

All **67 GPU cases, 1,608 CPU-reference channel checks, 960 HUD-free checks,
69 SDK tests and both game tests pass**, with zero D3D12 errors. Four full-4K
diagnostic cases cover normal, pre-overlay and both Tricky variants; trace values
are finite, retain native highlights and have the correct reference channel order.
The complete comparison totals **124,631,040 channels with zero output difference**,
including diagnostics and optional HUD-free output versus ordinary composition.
These tests validate the measurement; the live flash cause remains unresolved.
Evidence is in `out/hdr-fg-validation/light-trace/`. The diagnostic candidate is
`out/build/win-amd64-hdr-light-trace` and uses the existing 1,000-nit HDR/3x FG launch
arguments plus the trace flag; no FG setting is disabled.
The diagnostic files were staged and hash-verified in the normal HDR + FG runtime,
then launched at 20:04:53 with the original save. Hashes: `light-trace/build-hashes.json`.
The previous runtime is preserved in `pre-light-trace-runtime/`. The next Tricky
transition is needed to obtain the measured live color values.

## Measured Tricky grading fault and correction

`light-trace/live-01/analysis.json` contains 4,181 consecutive measured native
frames with valid matching SDR references and no nonfinite values. The larger
`live-02/` snapshot contains 16,345 frames. During ordinary grading the evaluated
LUT black is zero. At transitions it briefly rises to 0.849321 or 0.982302 in
linear display light. The old operation `(graded - LUT_black) / (1 - LUT_black)`
then removes color channels and magnifies the remaining range. For example,
frame 8726 measures mean HDR RGB **(0, 0, 66.35) nits**, versus matching guest SDR
RGB **(32.53, 115.13, 172.56) nits**. The preceding native frame matches its SDR
reference closely. Native HDR remains selected. The user's screenshot shows the
same severe cyan/blue channel clipping, crushed shadows and dark vignette.

The correction uses `DisplayLinear(0)` for the output black offset, rather than
the live grading LUT's zero-input color. It also anchors the mathematically-zero
tone curve before the LUT to avoid tiny constant-rounding offsets. This preserves
authored transition grading and the original radial/overlay effects. It does not
force those effects' deliberately brightened dark colors to zero. Normal measured
grading already maps black to zero. Native scene highlight extension, bloom,
HUD blending, peak calibration and all FG settings/code remain unchanged.

A bright-at-black, color-reversing synthetic LUT reproduces the old channel-loss
failure on the GPU. The new shader passes all five post variants at both 1,000
and 3,000 nits, alongside ordinary zero-black/near-black, lifted-display-gamma,
authored-grade-lift, native highlight and HUD cases. **77 GPU cases, 1,848 reference
channel checks and 1,200 HUD-free checks pass**, with zero D3D12 errors; **69 SDK
and both game tests pass**. Optional diagnostics/HUD-free writes leave output
identical across 124,692,480 comparisons. The old failure and final passing run
are `grade-black/before.log` and `grade-black/after-final.log`. The intermediate
run identified a CPU reference rounding discrepancy at exact black; anchoring
the validated curve before grading resolves that without suppressing LUT light.

Candidate: `out/build/win-amd64-hdr-grade-black`; hashes and the user's local
screenshot are in `out/hdr-fg-validation/grade-black/`. The previous diagnostic
runtime is preserved in `pre-grade-black-runtime/`. After the user closed SSX,
the three candidate files were staged and hash-verified, then relaunched at
20:13:56 with the original save, 1,000-nit HDR, 3x FG and HDR measurement logging.
The user subsequently confirmed: "That fixed it!"

The matching `grade-black/live-01/` snapshot contains 11,955 native HDR light
records with no nonfinite samples or missing SDR references. It includes 38
frames with evaluated LUT black above 0.5; none exhibits the earlier erased-channel
signature (mean HDR channel below 1 nit while its matching SDR channel exceeds
20 nits). For example, frame 6775 now measures HDR RGB (29.42, 106.78, 166.53)
nits versus guest SDR (29.27, 106.73, 166.59), with LUT black 0.849321. These are
sparse image averages, not a pixel-perfect full-game guarantee.

The latest retained counter at 20:18:03 reports 11,840 native and 400 fallback
race frames, or 3.27% fallback. This includes the initial 129-frame fallback
count and a later missing-input/scene interval; do not describe the run as
fallback-free. Menu/pause/loading SDR mapping is separate from these counters.
The retained FG statistics report 24,820 presentations from 8,280 rendered frames,
multiplier 3, status 0 and HDR10 PQ output. No FG code or setting changed for the fix.
The run contains filesystem lookup errors but no logged GPU error; the GPU probe's
zero-validation-error result is a separate controlled test.

The 1,000-nit configuration is ready for regular play with the visible fallback
indicator. The 3,000-nit configuration passes GPU readback tests, but physical TV
calibration, display switching and whole-game effect coverage remain unverified.
