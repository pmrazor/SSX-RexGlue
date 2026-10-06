# Jitter coverage, aligned inputs and exposure checkpoint

This incremental checkpoint applies after `rexglue-sdk-ssx-temporal-inputs.patch`
on the pinned ReXGlue v0.10.0 source and preceding SSX patches. It changes actual
shader translation, constant uploads, input collection and the DLSS options API.
The displayed game still uses SSX's own tone mapping and normal presentation.
DLSS evaluation, generated frames and HDR display output are not enabled.

## Jitter follows shader structure

`AnalyzeSsxVertexShader` parses native guest instructions once per vertex shader.
It tracks component identities through register writes, swizzles and copies, and
recognizes a complete row-vector projection using the audited c156, c160 or c192
matrix families. It verifies that all four position components use the same
three input coordinates. This works after different skinning/deformation code
and does not depend on the shader hash.

Copies of projected XY into interpolators are identified by their data flow.
Those exports receive the same clip displacement as raster position, while
material UVs, depth and W are preserved. Unsupported control flow, predicated
position exports, conflicting writes and transformed screen UVs are rejected.
The finite family recognizer is not a universal shader decompiler.

The local inventory contains 180 vertex shaders. Recognition handles 76,
including 12 beyond the 65 manually audited geometry/light-volume variants.
One audited unusual projection remains an explicit exception. Five fixed-screen
shaders and 16 pixel shaders that reconstruct positions from depth have separate
audited rules. A shader hash list alone is no longer the mechanism for extending
geometry coverage; new variants of the recognized math are handled as loaded.
Other projection algorithms and screen effects still require investigation.

The DXBC modification bit confines corrected interpolator code to the opt-in SSX
path. Shader-cache versioning invalidates older translations. The normal launch
path uses the original translations. Runtime title, viewport, depth allocation,
clip convention and scale checks still gate jitter. Shadow and auxiliary depth
allocations are excluded.

Lighting reads jittered depth using corrected inverse projection. Uploaded c175
is `row3 - jitterX * row0 - jitterY * row1`; the source guest registers and camera
metadata remain unjittered. Fixed-screen lighting and sky geometry stay fixed.
The procedural sky receives the inverse-ray correction in its vertex constants.
These corrections avoid moving a screen rectangle merely because it reads depth.

## Color/depth provenance and alignment

Depth is copied from the full-scene input to the early lighting pass, before
scene tone mapping. A late depth-reduction copy remains a diagnostic fallback,
but it does not pass the reconstruction alignment gate. For the existing local
`race-f8.rdc` capture, the early and late exported depth arrays are texel-for-texel
equal; that observation applies only to that captured frame.

`CheckSsxTemporalAlignment` checks same-frame resources, missing/duplicate inputs,
camera consistency, exact viewport origin/scale, matching mip-zero dimensions,
depth format/fetch conventions, early-depth ordering and unknown eligible draws
before color capture. `compatible_metadata` describes this structural check; it
does not certify every pixel or every game area. The copies use the same integer
texel coordinates without resampling to the presentation size.

At the existing 3× setting, the sampled scene is **3360×1752** and the final
guest frame is **3840×2160**. This checkpoint does not change that distinction.
There were no new 720p gameplay tests.

## Exposure decision

SSX's 3× exposure mip is a spatial 3×1 field. Its values differ, so treating it
as a scalar or averaging it into a DLSS exposure input is not equivalent to the
game's tone map. The decoded FP16 scene is instead exposed to reconstruction in
its native linear units, with:

- `colorBuffersHDR = true`, `useAutoExposure = true`;
- `preExposure = 1`, `exposureScale = 1`;
- no guest exposure texture supplied as a DLSS exposure tag.

This follows the automatic-exposure option in the pinned
[NVIDIA Streamline 2.14.1 DLSS guide](https://github.com/NVIDIA-RTX/Streamline/blob/v2.14.1/docs/ProgrammingGuideDLSS.md).
The host decoder applies no exposure before this boundary. The game's own
spatial exposure remains available to its original tone map. It is retained as
a diagnostic, and invalid or absent guest exposure no longer blocks valid color
decoding. This decision does not establish physical luminance in nits or prove
that earlier guest passes never clipped highlights. DLSS's metering and image
quality still need testing once evaluation is connected.

`Streamline::ConfigureSsxSR` sends these options to the real `slDLSSSetOptions`
entry point. It does not evaluate DLSS or activate the mode in gameplay.

## Verification

- SDK and SSX build successfully; installed and staged renderer DLL hashes match.
- Eleven SSX SDK unit cases and the SSX scene-resolution hook test pass.
- The structural shader probe scans 180 local shaders. All 82 recognized or
  specially audited vertex shaders produce valid D3D12 pipelines; 38 need clip
  interpolator corrections. Debug-layer errors: zero.
- Recognition is unchanged when the shader hash is replaced. For all 76
  structurally recognized shaders, predicate, wrong-matrix and truncated-input
  mutations are rejected. These are parser/translation tests, not raster-image
  comparisons.
- Eleven color GPU cases and the alignment-gate checks pass, including absent
  guest exposure, nonuniform exposure, 3360×1752 and 3840×2160 resources. Debug-layer
  errors: zero. HDR sample values above one survive decoding.
- The pass-aware audit of the existing race frame covers 730 geometry/light-volume
  draws and four fixed-screen draws through the main tone-map pass. Two auxiliary
  depth draws are excluded; no eligible draw in that frame is unclassified.
- An actual moving-race readback at 3× with nonzero jitter contains 17,034 native
  rider pixels, none outside the projected mesh and no nonfinite vectors. The
  maximum error in 2,571 interior CPU-reference comparisons is 0.002279 pixels.
  The scene-color overlay lines up with the visible rider. This is frame 7771
  versus 7769 from the first alignment build; its five extra live shader variants
  prompted the structural recognizer above. It is not a whole-scene motion proof.

The final structural build was then tested in the same normal race. All **21**
recorded early-depth race packets had zero unknown eligible draws before color;
all **22** compatible color diagnostic packets had zero invalid sampled pixels.
The maximum sampled linear channel was **45.5625**, retained above SDR white.
Desktop inspection showed normally colored terrain, rider, lighting and HUD.
These observations are a sampled race check, not coverage of every game effect.

The final readback is frame **10976** versus **10974**, at 3360×1752 with nonzero
jitter. Early depth is draw 800 and color is draw 1325; no unknown eligible draws
precede color capture. There are **33,541** native rider/board pixels out of
33,549 projected mesh pixels, with zero outside the mesh and zero nonfinite
motion values. The maximum error over **10,113** interior CPU-reference pixels
is **0.002412 pixels**. The scene-color/coverage overlay visibly follows the
rider and board. This validates that captured geometry against the captured
depth and color; it does not prove motion for all other animated objects.

Local evidence is in ignored `out/alignment-structural-readback/frame-10976/`,
`out/alignment-live-summary.json`, `out/structural-shader-results.txt`,
`out/alignment-color-results-final.txt` and `out/alignment-unit-results.txt`.

The NVIDIA options call succeeds and its presentation probe reports zero D3D12
errors, but shutdown hangs in NGX. A control run omitting the new options call
hangs at the same point. Therefore this is **not a passing end-to-end Streamline
test**. The two headless test processes were stopped after recording the evidence.
The normal game test keeps Streamline disabled. This teardown issue must be
resolved before enabling the NVIDIA path for regular play.

## Reproducing and extending coverage

Apply `patches/rexglue-sdk-ssx-temporal-alignment.patch` after the previous temporal
input patch. Build/install the SDK, then build the `win-amd64-streamline` SSX
preset. The preceding patch is unchanged. Forward application, reverse application
and a residual source comparison were checked using an alternate Git index.

`scripts/play_temporal_inputs.bat -Jitter` uses the existing original-profile 3×
test setup. Without `-Jitter`, the launcher's fallback remains unjittered. Read
`SSX_ALIGNMENT`, `SSX_JITTER_UNKNOWN` and `SSX_COLOR_GPU` in the local logs.
Unknown eligible draws make the reconstruction alignment result invalid; this
diagnostic checkpoint continues displaying the guest renderer and never silently
turns on DLSS. Jitter without temporal reconstruction can shimmer.

Build `tools/jitter_shader_probe` with `CMAKE_PREFIX_PATH` pointing at the installed
SDK and `SSX_SDK_SOURCE` pointing at its source. Give the probe a **local** directory
of `.ucode.bin.vert` files. The probe does not embed or redistribute game shaders.
`tools/jitter_pass_audit.py <capture-analysis-directory> <sdk-source> <output.json>`
compares existing draw/marker JSON and local disassembly with the audited rules.
New shader families can be investigated from local dumps without asking the user
to visit every game area. Representative live scenes remain necessary to check
lighting, transparency and visual stability.

Update: the [DLSS evaluation checkpoint](ssx-dlss-evaluation.md) adds and GPU-tests
real SDK evaluation on synthetic inputs. The shutdown stall was isolated to
restricted test execution; the normal host probe exits cleanly. Gameplay DLSS
insertion remains unfinished.

Further work: validate remaining screen-effect families, handle their motion
requirements, choose the reconstructed-color insertion point before the original
tone map, and wire frame tokens/constants/tags/evaluation with reset handling.
Full-game coverage, all animated objects, DLSS quality and FG remain unverified.
