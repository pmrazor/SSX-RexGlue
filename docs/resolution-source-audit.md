# Resolution scaling source audit — 2026-10-04

The 720p scene experiment is deferred at the user's request. No restart or new
gameplay test was performed for this audit. The hook remains default-off; the
previously staged experiment's local configuration is also now disabled.

## Conclusion

ReXGlue already implements real draw-resolution scaling inherited from Xenia.
It multiplies guest viewport/scissor dimensions and preserves higher-resolution
rendered data through EDRAM resolves and eligible texture loads. It does **not**
normalize SSX's 1120x584 scene to 1280x720 before applying that scale.

For the same game state as the captured stock race:

| Scale | Main scene viewport | Final guest image before host presentation |
|---|---|---|
| 1x | 1120x584 | 1280x720 |
| 2x | 2240x1168 | 2560x1440 |
| 3x | 3360x1752 | 3840x2160 |
| 4x | 4480x2336 | 5120x2880 |

The 1x row is captured evidence. The 2x–4x scene sizes are source-derived for
unchanged guest registers and an unclamped scale. The final 3x source/swap-chain
size was independently logged as 3840x2160. This audit does not claim a 3x or 4x
scene capture, stable performance, or correct scaling of every effect.

## Revisions and comparison boundaries

- SSX upstream `b8bc334ece118cf62c320f160cb948e2620b5a91`.
- ReXGlue v0.10.0 `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`.
- SSX's nine supplied SDK patches, applied locally through
  `eed22e8a490fb8a60f8c392a391c3d5eb16c04a4`.
- Compared original SDK files using `git show` and the SDK patch diff; our later
  logging/capture changes are not the basis for the scaling conclusion.
- Read Xenia's official `master` source on 2026-10-04 as a corroborating comparison.
  An exact Xenia ancestor of this SDK was not established. Do not treat current
  Xenia source as the SDK revision actually in use. The pinned local SDK is the
  authority for this application's behavior.

## Exact renderer path

Paths in this table are relative to `work/rexglue-sdk`. Line numbers refer to the
current local tree with diagnostics; function names also locate the original tag.

| Step | Source | What it does |
|---|---|---|
| Select scale | `src/graphics/pipeline/texture/cache.cpp:240`, `TextureCache::GetConfigDrawResolutionScale` | Reads shared `resolution_scale`, with nondefault per-axis settings taking precedence; clamps integer factors. No target monitor dimensions or game title enter this calculation. |
| Initialize renderer | `src/graphics/d3d12/command_processor.cpp:1066`, `SetupContext` | Passes the same effective scale to render-target and texture caches. Hardware limits can reduce it. |
| Read each viewport | `src/graphics/d3d12/command_processor.cpp:2653`, `IssueDraw` | Calls `GetHostViewportInfo` with guest registers and the fixed scale; separately multiplies scissor offsets/extents. |
| Compute viewport | `src/graphics/util/draw.cpp:376`, `GetHostViewportInfo` | Computes guest viewport extent from guest scale/offset registers, then multiplies that extent by the axis resolution scale at line 387. NDC corrections handle clipping, rounding and offsets; they do not target 720p/4K. |
| Set D3D12 viewport | `src/graphics/d3d12/command_processor.cpp:3833`, `UpdateFixedFunctionState` | Copies the computed extents directly into `D3D12_VIEWPORT.Width/Height`. No subsequent scene-resolution compensation occurs here. |
| ROV storage | `src/graphics/d3d12/render_target_cache.cpp:206` | Allocates emulated EDRAM storage multiplied by scale X times scale Y. This retains extra rasterized samples, rather than just stretching the finished frame. |
| Resolve | `src/graphics/util/draw.cpp:1124`, `ResolveInfo::GetCopyShader`; `src/graphics/d3d12/render_target_cache.cpp`, `Resolve` | Copies resolved guest regions using the same integer scale, with format conversion and scaled resolve memory. Alignment/padding does not imply more visible scene pixels. |
| Sample resolved textures | `src/graphics/pipeline/texture/cache.cpp:838`, `FindOrCreateTexture`; `src/graphics/d3d12/texture_cache.cpp:1546`, `CreateTexture` | Recognizes eligible scaled resolve ranges and allocates host width/height as guest dimensions times scale. Oversized/unsupported paths can fall back; ordinary asset textures are not all multiplied. |
| Present | `src/graphics/d3d12/command_processor.cpp:2121`, `IssueSwap`; `src/ui/d3d12/d3d12_presenter.cpp` | Selects the active final image, handles gamma/postprocessing and fits it to the host surface. Allocation-padding correction is distinct from changing earlier scene draws. |

The decisive viewport relation is:

```text
host viewport extent = guest viewport extent × configured integer scale
```

Xenia's corresponding
[viewport calculation](https://raw.githubusercontent.com/xenia-project/xenia/master/src/xenia/gpu/draw_util.cc)
and [D3D12 draw submission](https://raw.githubusercontent.com/xenia-project/xenia/master/src/xenia/gpu/d3d12/d3d12_command_processor.cc)
use the same mechanism. Its [scale selection](https://raw.githubusercontent.com/xenia-project/xenia/master/src/xenia/gpu/texture_cache.cc)
also operates on integer per-axis factors; ReXGlue adds the shared convenience option.

## What the SSX author's changes do

The original `scripts/play_qhd.bat` requests `--resolution_scale=2` and ROV. Its
2560x1440 comment describes the scaled 1280x720 final image; the script does not
change SSX's scene-size preset. The original handwritten overrides concern VSCR,
frame counting, render-loop timing and present intervals, not scene dimensions.

The supplied SDK patches include important scaling fixes:

- Clamp copies into small mip levels to the actual host mip extent. Odd-sized
  mip chains otherwise cause out-of-bounds copies and device errors.
- Scale the dimensions of the 3D-as-2D texture wrapper to match its data.
- Correct color/gamma ownership transfers.
- Reload changed slices/blocks and correct volume-resolve addressing, including
  SSX snow-trail textures.

These changes make the existing scaler work more correctly. They do not insert
a 1120x584-to-1280x720 scene-size override or a monitor-driven fractional factor.

## Options without another 720p test

The checked SDK's hard limit is **7 per axis**, not 3
(`include/rex/graphics/pipeline/texture/cache.h:63`). D3D12 additionally checks
tiled-resource support and per-resource virtual-address bits
(`src/graphics/d3d12/texture_cache.cpp:1095`). This establishes that 4x can be
requested, not that every SSX effect or device will run correctly at 4x.

Thus a stock 4x path could render this scene at 4480x2336 and present to a 4K
desktop. That would be **supersampling/downsampling to 4K**, not an exact
3840x2160 scene viewport. It preserves the game's original scene-size selection,
but scales other eligible passes too (16/9 as many raster pixels as 3x, before
considering workload-specific costs). No new 4x launcher or setting was enabled
in this audit.

An exact 3840x2160 scene would need a confirmed change to the base scene size or
a more involved resolution-aware renderer path. A single existing integer factor
cannot map 1120x584 exactly to 3840x2160. The guarded 720p hook is one candidate,
not a prerequisite for continued renderer work and not yet runtime-validated.

## What source inspection answers, and what is game-specific

| Reusable renderer work | SSX-specific identification or validation |
|---|---|
| Command submission, shader translation, resource state/lifetime, ROV/RTV, resolve formats and scaling | Which draws represent world color, depth, exposure, tone mapping, transparency and HUD |
| Reading/uploading shader constants | Which values are camera projection, object transforms or skeleton animation; previous-frame identity and camera cuts |
| Building a DLSS integration and managing its resources | Correct depth convention, jitter application, exposure units, camera and animated-object motion, and history resets |
| HDR swap chain, output transfer/color space and presentation controls | Preserving SSX brightness before its SDR tone map, adapting later effects, and separating UI brightness |

ReXGlue loads guest shader bytecode (`LoadShader`) and uploads referenced guest
constants (`UpdateBindings`). The infrastructure sees registers, buffers and
shader instructions; it does not inherently label a constant as SSX's camera or
a draw as the HUD. Xenia's source is not SSX's original engine source.

The local generated game code and dumped guest shaders can also be inspected
without more gameplay. The existing full race capture tells us which of those
shaders and resources are actually active and in what order. It already identified
SSX's encoded scene color, exposure and tone mapping, so it can support continued
source work now. Later captures should answer specific unresolved questions or
validate an implemented change, rather than repeat facts established by source.

The pinned SDK's experimental temporal presenter still supplies the color texture
as depth and motion, zero jitter and an unconditional history reset. It does not
provide ready-to-use valid DLSS inputs. No DLSS/FG/HDR feature was enabled here.
