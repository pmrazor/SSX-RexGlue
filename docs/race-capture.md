# Race capture and scene-resolution experiment — 2026-10-04

**Update:** The user deferred the 720p gameplay experiment. The subsequent
[resolution source audit](resolution-source-audit.md) confirms multiplicative
scaling without automatic compensation for SSX's smaller scene, and identifies
stock 4x supersampling as another option. No restart is currently requested.

## Capture evidence

The normal-race F8 capture succeeded and replayed in RenderDoc 1.46 with no
problems reported. The rider, snow/terrain and gameplay HUD are present; the
tutorial popup is absent. The track name was not recorded.

- Local file: `out/renderer-capture/race-f8.rdc` (400,537,897 bytes).
- SHA-256: `29167c3c88369a24da9c556588451b053db727cc24d3eb4b13be719be9fd7c83`.
- Matching `SSX_CAPTURE` begin/end: guest frame 60306, `saved=true`.
- Replay: 1,874 draws, 1,908 SSX markers, 454 textures, 10,256 API events.
- 1x, ROV, SDR. This capture does not measure 3x performance.
- Local analysis: `out/renderer-capture/capture-analysis/race-f8`.

The capture, shaders, generated code and exports remain ignored and local.
Resource/event IDs below refer only to this capture, not stable runtime handles.

## Resolution and the new hook

Main scene draws and the full scene tone-map draw have a **1120x584 viewport**.
Later output/HUD draws use **1280x720**. The final 3840x2160 buffer previously
verified at 3x therefore does not establish native 4K scene rendering. A simple
3x scale of this scene would be 3360x1752; that prediction still needs a 3x capture.

The generated guest function `sub_82FB6120` maps preset 4 to 1120x584 and preset
1 to 1280x720. Its direct caller `sub_82FB6968`, return address `82FB69C4`, supplies
preset 4 and writes the returned dimensions into its object's offsets 40/44.
Output dimensions are passed in r5/r6; output addresses are r7/r8.

`src/scene_resolution.cpp` adds **`ssx_scene_720p` (default false, restart required)**.
After the original selector runs, the hook changes only its returned dimensions,
and only for that caller, preset 4, requested output 1280x720 and original result
1120x584. Other calls retain the original result. This happens before resource
creation; it does not stretch an already allocated render target.
`SSX_SCENE_SIZE` records the original/selected values and whether the guard matched.
This call-site interpretation comes from source inspection; gameplay validation
of resource allocation, camera aspect and effects remains pending.

Release SSX compiled and linked. The optional CMake `SSX_BUILD_HOOK_TESTS` target
links the hook to the **actual locally generated selector**. Seven cases passed:
default stock, experiment enabled, unknown caller, different output mode, another
built-in preset, caller-supplied dimensions, and disabling the experiment again.
Each also checks memory outside the two output fields. These are CPU hook tests,
not rendering tests. Windows PowerShell 5.1 launcher dry run passed.

```powershell
cmake -S . -B out/build/win-amd64-release -DSSX_BUILD_HOOK_TESTS=ON
cmake --build out/build/win-amd64-release --target ssx ssx_scene_resolution_test
ctest --test-dir out/build/win-amd64-release -R ssx_scene_resolution --output-on-failure

# Opt-in only; the ordinary launcher explicitly disables this experiment.
.\scripts\play_4k.bat -ExperimentalScene720p -Diagnostics
```

An isolated local `out/scene-720p-test/ssx.exe` is staged with the F8-capable plugin,
1x/ROV and markers. Its experimental flag is now disabled because this test was
deferred. If this experiment is resumed, enable the flag and test it at 1x first.
It shares `out/test-user` saves: finish and close the current game before launching.
Verify `SSX_SCENE_SIZE ... selected=1280x720 ... applied=true`, then an actual
1280x720 **scene** viewport/color/depth in a race. Check the rider/camera aspect,
snow, particles, menu and HUD. Only then repeat at 3x and confirm 3840x2160 scene
resources and viewports. Keep the default false until these tests pass.

## Color, exposure and tone mapping

| Event / resource | Finding |
|---|---|
| Main scene target | Raw RT0 `00030000`: Xenos `k_2_10_10_10_FLOAT` (7e3 RGB), EDRAM base 0. ROV writes go to buffer 319, not an ordinary RenderDoc color attachment. |
| Resolve 7594 | Guest destination `088BC000`, copy format 30. Later loaded into texture 260043 at events 8109–8120. |
| Texture 260043 | 1120x584 FP16 RGBA, rider/terrain without HUD, RGB maxima 3.109375 / 3.234375 / 3.453125. |
| Draw 7654 | PS `8067098F5F2141EA`: squares sampled color, computes luminance, clamps to at least 1, then log-encodes it. |
| Draw 7680 | PS `E0F3DCEADFC919FC`: combines current/previous luminance with an exponential adaptation term. Captured constants include dt 0.011 and speed 1.5. Temporal behavior needs multiple frames. |
| Draws 7709–8026 | PS `244E54F3326D5523`: mip reduction down to 1x1. |
| Texture 1456 | 280x146, 9 mips, R16_TYPELESS allocation viewed as **R16_UNORM**. At draw 8121 mip 8 equals 0.0202487223. |
| Draw 8121 | PS `A2CD8E82699B2C0A`, VS `E857B4AF9617DC29`, 1120x584. Reads scene 260043 and exposure 1456; outputs to RGBA8 guest target. |
| Resolve 8122 | Destination `0818C000`, RGBA8. This follows the main scene tone map, with more postprocessing/particles and HUD still to come. |
| Late draws 9754–10161 | 1280x720 output/HUD region, followed by final resolve 10162 to `07724000`. A precise clean-HUD boundary/mask is not yet validated. |

The tone-map branch **b128 is enabled** in the captured constant buffer. The
shader squares the sampled RGB, applies exposure, evaluates a rational filmic
curve and square-roots the result. For each channel, ignoring channel swizzles:

```text
x = sampled_color^2 * (0.4 / exp2(23.083120346 * exposure_mip8))
y = ((x * (0.22*x + 0.03) + 0.002) /
     (x * (0.22*x + 0.30) + 0.060) - 0.0333333313) * 1.489361763
output = sqrt(abs(y))
```

The captured exposure multiplier is approximately 0.289306. Guest c255 is packed
as host float-array element 9; **host array indices are not always guest register
numbers**. The pre-tone-map FP16 texture is square-root encoded, not directly
linear-light RGB. An HDR integration must decode this signal and respect exposure,
then preserve post effects and independently handle UI brightness. Exported PNGs
show raw textures (possibly before guest swizzle/gamma) and are not display/HDR
reference images. EXR and numeric values retain the available precision.

## Depth and temporal inputs

The scene uses raw depth `000102D0`: `kD24FS8`, EDRAM base 720 tiles. The sampled
scene has 1x MSAA. Resolved textures 1442 and 265509 are 1120x584 R32_FLOAT;
their range is 0.00110810064–0.119873046875. The preview follows terrain and rider
silhouettes. Texture 1442 is loaded around events 5037–5049; 265509 around
8807–8818. These are real scene-depth candidates, not reused color textures.

Depth convention still needs matching projection constants. The translated
vertex system constants include NDC z scale -1 and offset 1; do not infer the
guest resolved-depth convention from host raster depth alone. A sampled scene
vertex shader (`C44105DD14EFDB33`) contains position-transform constants, but
camera-only vs. object transforms, matrix layout, jitter and previous transforms
are not yet validated. No dense animated-object motion vectors have been found.

DLSS SR, Frame Generation/Reflex and native HDR remain unimplemented. Concrete
remaining work is the 720p/3x scene test, a stable resource handoff at the identified
passes, validated projection/depth and previous object transforms, jitter/history
control, clean HUD composition, and an HDR path through subsequent effects and
presentation. A float resource or HDR swap-chain format alone is insufficient.

## Reproducing pass analysis

`tools/renderdoc/inspect_capture.py` produced the inventory/draw bindings and
floating-point snapshots. `tools/renderdoc/inspect_passes.py` exports selected
draws' shader disassembly, constant buffers, SRV formats and terminal exposure
mips. Open it in RenderDoc's Python Scripting pane; set `EVENTS` to draw IDs or
leave it empty to inspect the currently selected event. Both save a status JSON
and restore the selected replay event. The pass inspector was run successfully
on 7654, 7680 and 8121 in this capture. Keep all exports under ignored `out/`.
