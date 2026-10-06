# Renderer investigation

All paths below are relative to SDK v0.10.0 plus the bundled SSX fixes unless
explicitly labeled SSX. Function names anchor the notes across the patches.

The [resolution source audit](resolution-source-audit.md) now traces the original
SDK, the SSX author's patches and corresponding Xenia code. Existing scaling
multiplies each guest viewport; it does not normalize SSX's scene to 720p. The
720p hook test is deferred; existing captures and source remain available for
continued resource/temporal investigation without another playthrough.

| Area | Source entry point | What is established / still needed |
|---|---|---|
| Scaling | `src/graphics/pipeline/texture/cache.cpp`, `TextureCache::GetConfigDrawResolutionScale`; `src/graphics/d3d12/command_processor.cpp`, setup | Shared scale, per-axis precedence, backend clamping. |
| Guest draws and constants | `src/graphics/d3d12/command_processor.cpp`, `IssueDraw`, `UpdateSystemConstantValues` | Viewport/scissor and guest vertex/pixel constants exist. Game-semantic camera matrices, jitter and previous transforms are unidentified. |
| Color and depth storage | `src/graphics/d3d12/render_target_cache.cpp` and `include/rex/graphics/d3d12/render_target_cache.h` | Guest target formats map to host storage. The ROV path uses an EDRAM UAV; it is not automatically a directly sampleable scene-depth texture. Need the correct pass, depth encoding and extraction/resolve. |
| Shader translation | `src/graphics/pipeline/shader/dxbc_translator*` | Guest shader execution is translated. A shader/call-site signature is needed before changing SSX camera or tone-map behavior. |
| Final guest color | `D3D12CommandProcessor::IssueSwap` / `RequestSwapTexture` | Actual resolved source and active bounds are known. The gamma/optional FXAA pass consumes this source; it is not an identified scene-linear pre-tone-map buffer. |
| Gamma / postprocessing | `src/graphics/shaders/apply_gamma*`; `IssueSwap` | Guest gamma conversion is visible. SSX's own tone-map and highlight clipping must be traced earlier in guest rendering. |
| Presentation | `src/ui/d3d12/d3d12_presenter.cpp`, `RefreshGuestOutputImpl`, paint path, `ConnectOrReconnectPaintingToSurfaceFromUIThread` | Mailbox guest-output resources feed final presentation; lifetime and thread ownership matter for integration. |
| Output formats | `include/rex/ui/d3d12/d3d12_presenter.h` | Guest/intermediate R10G10B10A2_UNORM; swap-chain B8G8R8A8_UNORM. Floating-point scene brightness and HDR color-space negotiation are not supplied here. |
| HUD | SSX `src/fps_overlay.h`; guest frame before `IssueSwap` | Host FPS overlay is separate ImGui. The game's HUD is inside guest rendering and needs separate identification; hiding the FPS overlay does not produce HUD-less SSX color. |
| Frame timing | SSX `src/overrides.cpp`, `sub_8233D770`, `sub_83046C48`, `sub_8233C870` | Guest present wrapper, render-loop interval and vblank scheduler hooks exist. No validated simulation-start hook or Streamline frame token association yet. |
| Capture support | `src/graphics/flags.cpp` | `gpu_debug_markers` and `dump_shaders` already exist. Shader dumps and GPU captures contain game-derived material and must remain local. |

The optional `rexglue-sdk-capture-markers.patch` now annotates D3D12 guest draws,
resolves and swaps. It records frame IDs, shader hashes, viewport sizes and raw
guest target/depth/resolve registers without changing render inputs. The plugin
build passed; a RenderDoc 1.46 F8 title-screen replay now contains 374 SSX labels
and 347 draws. Existing PSO names
and shader-dump filenames provide matching guest shader hashes. See
[the prepared capture workflow](capture-next-frame.md).

The first outdoor capture is analyzed in
[ground-training-capture.md](ground-training-capture.md). Its initial resource
contents include a HUD-free 1120x584 FP16 scene (RGB maxima 3.71875, 3.828125,
4.0) and matching-size R32_FLOAT depth candidates. These are promising inputs,
but the capture contains only presenter commands, so it cannot establish their
producer shaders, current-frame validity, depth convention or tone-map order.
In particular, 3x scaling of a 1280x720 final image does not prove that the earlier
scene is rendered at 3840x2160. The 1120x584 scene allocation needs investigation.

`rexglue-sdk-guest-frame-capture.patch` adds the optional command
`d3d12_capture_guest_frame`. SSX binds it to F8. An atomic request crosses from
the UI to the GPU worker; RenderDoc starts before a guest frame's first submission
and ends after its swap submission. It includes intermediate submissions and
does not depend on the independent presenter refresh cadence. The title-screen
smoke test logged matching begin/end for guest frame 3012 and `saved=true`, and
replayed without detected problems. The subsequent normal-race capture succeeded:
1,874 draws and 1,908 SSX markers. See [race-capture.md](race-capture.md) for the
identified scene/exposure/tone-map passes, shader constants, depth candidates,
the 1120x584 scene-size selector, and the guarded 720p scene experiment.

There are no validated SSX camera, skeleton, animated-object velocity, exposure,
HUD-mask or camera-cut hooks in the current handwritten code. This does not prove
the game lacks those data internally. Camera/depth reprojection alone cannot
represent a moving rider, deforming gear or independently animated objects.

## NVIDIA integration reference

Reviewed NVIDIA's current Streamline documentation on 2026-10-04. The release
endpoint resolves to [Streamline 2.14.1](https://github.com/NVIDIA-RTX/Streamline/releases/tag/v2.14.1).
The implementation now pins 2.14.1 and builds the optional SDK connection in
ReXGlue's D3D12 provider. Signed production binaries are staged locally under
ignored `out/`. See [the connection checkpoint](streamline-checkpoint.md) for
code, support queries, presentation/fallback tests and remaining frame inputs.

- [DLSS SR guide](https://github.com/NVIDIA-RTX/Streamline/blob/v2.14.1/docs/ProgrammingGuideDLSS.md): initialize early, query adapter support and optimal render sizes, tag real color/depth/motion resources and output, provide per-frame constants with the correct jitter and motion-vector units, and evaluate on the correct command list. Exposure handling must match the color signal.
- [DLSS-G guide](https://github.com/NVIDIA-RTX/Streamline/blob/v2.14.1/docs/ProgrammingGuideDLSS_G.md): integrate its presentation path, maintain required resource lifetimes, supply matching constants/frame IDs and dense camera plus object motion. Identify HUD-less color and UI coverage. Integrate Streamline Reflex and synchronize present markers with frame constants.
- [Reflex guide](https://github.com/NVIDIA-RTX/Streamline/blob/v2.14.1/docs/ProgrammingGuideReflex.md): instrument the actual simulation, render submission and presentation boundaries. The existing guest-FPS counter is not a Reflex integration or a count of generated display frames.

## HDR validation boundary

First identify the last scene buffer retaining highlight information and the
guest shader that maps it into SDR. Inspect bright snow, sky and sun with exposure
changes. If values are clipped before the selected hook, a later PQ or scRGB
conversion cannot recover the lost detail. Format promotion may require guest
shader/resolve changes, including ROV storage and blending semantics.

After that evidence exists, implement a consistent linear-light path, gamut and
transfer conversion, output capability checks, and tone mapping with explicit
peak brightness, paper white and separate UI brightness. Validate calibration
against the selected AW3225QF display mode; do not assume its effective sustained
and small-highlight brightness from the model name. Preserve the tested SDR path.
