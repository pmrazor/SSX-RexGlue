# SSX camera motion checkpoint

2026-10-04. Continues [SSX frame inputs](ssx-frame-inputs.md), using the same
SSX-patched ReXGlue v0.10.0 revision and the separate development build.

**Camera-only motion vectors are implemented and GPU-tested.** This is an
experimental temporal-input producer. It does not yet implement rider/object
motion, DLSS Super Resolution, or Frame Generation, and it does not change the
game's displayed image. No NVIDIA evaluation is called with these partial inputs.

## Implemented path

`patches/rexglue-sdk-ssx-camera-motion.patch` adds:

- Camera-associated viewport metadata, captured on the known world shader draw.
  It preserves signed XY scales, effective offsets (including guest window and
  optional half-pixel offsets), reversed Z mapping, and validity/conflict checks.
- Double-precision inversion and multiplication of the current and last consumed
  camera matrices. SSX row-vector conventions are preserved; identical cameras
  use an exact identity transform.
- A D3D12 compute pass producing `R32G32_FLOAT` scene-resolution motion and an
  `R8_UINT` camera-reprojection validity texture. Pixel `(x,y)` stores
  `previousPixel - currentPixel`, with positive X right and positive Y down.
- Guest-frame history: repeated UI paints reuse the last vector texture. Skipped
  guest frames use the last consumed camera, rather than an unseen rendered frame.
  Missing or conflicting inputs, resize, explicit reset, backwards/large frame
  gaps, elapsed gaps over 250 ms, and a conservative large-camera-change guard
  reset history. An eight-frame gap limit is a prototype policy, not game timing.
- Three bounded output slots, descriptor heaps protected by paint fences, input
  references retained until GPU completion, and fallback when resources are busy
  or allocation/pipeline creation fails. The pass uses the existing direct queue.
- Sparse GPU diagnostics every 120 guest frames, sampled on a 32-pixel grid.
  Readback happens after fence completion without an added wait on gameplay.

The reconstruction reverses the actual guest viewport transform:

```text
guestPixel = (hostPixel + 0.5) / resolutionScale
clipXY = (guestPixel - effectiveViewportOffset) / signedViewportScale
clipZ = (resolvedDepth - viewportZOffset) / viewportZScale
previousClip = (clipXY, clipZ, 1) * inverse(currentWorldToClip) * previousWorldToClip
previousPixel = previousClip.xy / previousClip.w * previousHostViewportScale
                + previousHostViewportOffset
motion = previousPixel - (hostPixel + 0.5)
```

The observed SSX path uses Z scale `-1`, offset `+1`, conventional perspective
divide, and a D24FS8 guest depth resolve expanded to R32_FLOAT. Unsupported
viewport conventions and depth fetch conversions are rejected. Clear depth,
out-of-range/non-finite depth, and reprojection behind the prior camera produce
zero vectors with validity zero. Offscreen reprojections retain their vectors.

The direction and scene-pixel units follow NVIDIA's
[DLSS programming guide](https://raw.githubusercontent.com/NVIDIA/DLSS/main/doc/DLSS_Programming_Guide_Release.pdf).
An eventual Streamline consumer would use `mvecScale={1/renderWidth,1/renderHeight}`
for these units, as documented in the pinned
[Streamline 2.14.1 guide](https://github.com/NVIDIA-RTX/Streamline/blob/v2.14.1/docs/ProgrammingGuideDLSS.md).
That consumer is not enabled by this patch.

## Validation

The production compute shader and runtime class run in `tools/motion_probe`,
using the RTX 5090 with the D3D12 debug layer and Streamline disabled. Sixteen
GPU cases pass: identity, XY camera translation over varying depth, roll, forward
movement, invalid depths, repeated paints, first/explicit/gap/resize resets,
odd extents, the actual 3360x1752 scene, and a 3840x2160 buffer. Expected vectors
are derived from analytic geometry independently of the implementation's matrix
inverse. Every output pixel and validity value is read back and checked.
Maximum observed error is 0.000465 scene pixels; D3D12 validation reports zero
errors. The 4K synthetic buffer is not a claim that SSX's default scene is 4K.

The SDK suite reports 216 passed and four existing BitStream skips (220 discovered).
The SSX scene-hook test also passes. The development executable and renderer DLLs
were rebuilt. Probe output, filtered gameplay diagnostics, hashes and a summary
are preserved locally under `out/motion-validation/`.

During the user's normal race at the existing 3x setting, the compute pass ran
at 3360x1752. GPU readback contains nonzero motion while riding and no non-finite
vectors in the sampled records. After pausing, sampled motion settles below
0.001 pixels, with all 5775 sampled depths admitting camera reprojection.
The saved log summary contains 120 GPU readbacks / 693,000 sampled vectors,
including 18 records with motion, 101 stationary records, and one reset record.
The observed world viewport is `(560,-292,560,292)` and passes the new validity
guard. Menu/loading frames without a recognized camera do not generate vectors.

This confirms live execution and basic temporal behavior, not per-pixel alignment
against prior terrain images, correct disocclusion handling, or animated-object
coverage. Gameplay was not run with the D3D12 debug layer; the zero-error result
above belongs to the synthetic GPU probe. Performance has not been benchmarked.

## Reproduce and inspect

Apply this patch **after** all patches in [the frame-input checkpoint](ssx-frame-inputs.md),
including `rexglue-sdk-ssx-frame-inputs.patch`. Forward application against that
exact sequence and reverse application against the working source were checked.
Build/install the SDK into the separate Streamline development prefix, then
rebuild SSX with `win-amd64-streamline` as in the prior checkpoint.

The SDK includes precompiled shader bytecode, matching its existing shader
workflow. To regenerate it with Windows SDK FXC:

```powershell
./tools/motion_probe/rebuild_shader.ps1 -SdkSource 'D:/dev/rexglue-sdk' -Fxc 'fxc.exe'
```

To build and run the GPU probe in a VS x64/clang environment:

```powershell
$sdk = 'D:/dev/rexglue-sdk/out/install/win-amd64-streamline'
cmake -S tools/motion_probe -B out/build/motion-probe -G Ninja `
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++ "-Drexglue_DIR=$sdk/lib/cmake/rexglue"
cmake --build out/build/motion-probe
Push-Location out/build/motion-probe
./ssx_motion_probe.exe
Pop-Location
```

Run `scripts/play_camera_motion.bat` from this repository. It keeps the existing
3x ROV / 3840x2160 presentation configuration and explicitly shares the original
`out/test-user` save profile. The profile was backed up under `out/save-backups/`
before restarting. The native scene remains 3360x1752 for the observed 1120x584
guest viewport. No 720p scene experiment is enabled.

Startup flags are `d3d12_ssx_frame_inputs=true` and `d3d12_ssx_camera_motion=true`.
Both features default off in source. `play_frame_inputs.bat` explicitly disables
camera motion while retaining collection; `play_4k.bat` uses the preserved baseline
executable. The ignored development `ssx.toml` also enables motion diagnostics
for direct launches of that executable.

The log contains `SSX_MOTION`, `SSX_MOTION_GPU`, and `SSX_INPUT_VIEWPORT` records.
RenderDoc can inspect the named resources `SSX camera-only motion XY pixels`
and `SSX camera-only reprojection validity (not object coverage)` after their
compute dispatch in the presenter command list. Their dimensions are the scene
dimensions, not the final presentation dimensions.

## Remaining work before DLSS/FG

The validity texture means a camera-only reprojection is mathematically defined.
It does **not** identify static pixels: the rider and moving objects currently get
the static-world assumption too, so those vectors are not correct for animation.
It also does not test previous-depth occlusion or color correspondence. There is
no jitter injection/removal in this checkpoint, and large camera changes are a
heuristic guard rather than a hook into every SSX camera-cut/loading event.

The existing race capture uses 104 distinct vertex shaders. The investigated
dual-transform shaders `44C82C829ADB8288`, `4A18AEB29F12501A`, and the skinned
`5A38F6C29383E414` are present in the shader dump but absent from that race's draw
markers. They do not establish a usable existing object-velocity buffer. No
scene-size vertex shader in that capture uses the dumped relative `a0` constant
addressing pattern. Subsequent post-VS mesh reconstruction identified the rider
and board and confirmed that their draw buffers contain final world-space positions.
The [native rider motion checkpoint](ssx-native-motion.md) now snapshots these
deformed vertices; the producing animation code and authoritative object identity
remain to be traced. This camera-only launcher retains its original behavior.

Dense animated motion needs the prior position of the **same visible surface**,
including skinning or vertex deformation, and depth-tested coverage. Previous
object matrices alone are insufficient for deforming vertices. Remaining DLSS
work also includes jitter, exposure/color decoding, the scene insertion point,
HUD separation, validated resets, and matching simulation/presentation timing.
The current camera pass is at presentation for validation; SR integration will
need to place its inputs and evaluation correctly relative to SSX's later effects.
