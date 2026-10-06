# Native rider motion checkpoint — 2026-10-04

An experimental native geometry pass now overlays rider/board motion onto the
camera motion buffer. It uses the game's final deformed vertices, not optical
flow or an inferred rigid rider transform. DLSS SR and Frame Generation remain
disabled. This checkpoint is limited to the shader families observed in the
existing race capture; it is not a claim of complete animated-object coverage.

## Source and capture evidence

The existing local `race-f8.rdc` capture contains eight foreground mesh draws at
events 3128, 3145, 3150, 3157, 3164, 3178, 3185 and 3192. Reconstruction from
RenderDoc post-VS vertices and triangle indices shows the rider's body, clothing,
head, backpack and board. Corresponding later color-pass draws have the same
post-VS positions. No additional 720p gameplay test was used.

These shaders fetch 32-byte vertices from disassembly `vf0` (physical fetch
constant 95). Position is three big-endian floats at byte 0; normal/tangent
attributes follow, and UV data starts at byte 24. The positions are already in
world space, near `(431, 1224, -370)` in this capture. Guest constants c192..195
transform them directly to clip space. The producer of the deformed buffers has
not yet been reverse-engineered; native history can retain its output without
reimplementing the animation solver.

The collector currently recognizes these exact vertex/pixel shader pairs:

| Vertex shader | Pixel shader |
|---|---|
| `9897AE6BB3FD8C0B` | `78C15EF5A4F22B90` |
| `1DF106D9BBC3DAB5` | `9B85AFC510BDB8EC` |
| `0F20F851EA9BECB1` | `5EDF9E6FD35933CC` |
| `6E147097CF5A252A` | `5A65EA089B5672CF` |

## Implementation

`CollectSsxGeometry` copies GPU-resident vertices and indices before their draw,
after shared-memory residency/upload. The collector only accepts the observed
vertex layout, 16-bit indexed triangle lists, endian modes, zero index offset,
and reversed-depth viewport. Each existing frame-input pool slot has an optional
8 MiB geometry allocation and a maximum of 32 draws. Overflow or unsupported
draws are rejected without blocking gameplay. Producer fences and shared frame
references protect snapshots from reuse.

`SsxNativeMotion` retains the previous **consumed** frame to match camera history,
including when presentation skips guest frames. It rejects duplicate mesh keys,
missing history, camera/viewport mismatches and camera resets. Repeated paints
reuse the result. Separate descriptor slots and frame references remain alive
until the paint submission completes.

Candidate mesh identity combines the static index address, shader, vertex/index
counts and endian modes. The raster vertex shader additionally compares all three
triangle indices and their UV data between frames; invalid indices or mismatches
reject the whole triangle. It transforms current and previous deformed positions
with their respective camera matrices and computes previous-minus-current pixel
motion using perspective interpolation. Pixels must match the copied scene depth.
Validity value 2 marks accepted native geometry; 1 retains the camera-only result.

This identity is **not yet an authoritative SSX object ID**. Address reuse,
identical meshes disappearing/reappearing, LOD changes, alpha-tested materials,
multi-sample edges and the depth tolerance still need gameplay/capture validation.
Rejected or uncaptured dynamic surfaces retain camera-only motion and must not
be presented to DLSS as a fully validated motion field.

## Verification

The SDK runtime and Xenos plugin build successfully. All 220 discovered SDK unit
tests completed with 216 passing and four existing BitStream skips. The standalone
`tools/native_motion_probe` exercises the actual camera and native raster passes:

- Stationary, rigid-plus-camera and non-rigid deformation at 64x64, 3360x1752
  and 3840x2160, with full pixel readback against independent analytic answers.
- Foreground depth occlusion, changed UVs/topology, out-of-range indices,
  ambiguous mesh keys, history resets and repeated paints.
- All 15 GPU cases passed on the RTX 5090; maximum error was 0.000353 pixel,
  with zero D3D12 debug-layer errors.

The first live 3x gameplay run and pause check now execute the native pass.
The saved log snapshot contains 113 draw summaries: 103 matched all 10 captured
meshes, while 10 had no captured meshes. Of 105 GPU diagnostic readbacks, 22
contained moving fragments and 82 had zero world displacement and no fragments
moving more than 0.01 pixel. The stationary sample had 33,205 accepted fragments
and maximum motion 0.002677 pixel. Moving samples reached 52.816 pixels and
0.895 world units of displacement. A visible in-game pause menu confirmed the
stationary state. The track name was not independently established.

The 16 camera-only GPU regression cases and seven scene-hook checks also pass.
The original save profile is backed up; the live executable and DLL hashes and
filtered logs are saved under ignored `out/native-motion-validation/`.

The counters confirm execution and basic temporal behavior, not SSX-wide motion
validity. A subsequent live readback and GPU replay now validate one moving-rider
frame and exposed the projection rounding bug described below.
`SSX_NATIVE` reports captured/rejected/matched draws; `SSX_NATIVE_GPU` reports
accepted fragments, moving fragments, maximum motion and world displacement.
Fragment counts can include overdraw and are not unique-pixel coverage measurements.

### Direct motion readback

Normal RenderDoc presentation captures were insufficient for this pass: the first
contained a repeated presentation, and a three-frame sequence triggered
`reason=frame_gap` while capture initialization stalled presentation. The history
reset correctly suppressed native reprojection. Those captures do not validate
native pixels; the reset guard has not been weakened to accommodate the tool.

A diagnostic readback now copies the actual motion/validity targets, matching
scene depth/color, and both retained geometry snapshots. It records the current
and previous camera, viewport, draw keys and buffer offsets alongside them.
Copies run on the existing paint queue; files are written after the submission
fence completes. The directory must be new, row padding is removed, and the
final `metadata.json` marks completion. The one-shot disk write may stall a later
presentation, but it cannot change the already-copied frame or its history.

Run `scripts/play_native_motion.bat -NativeCaptureDirectory <absolute-local-path>`.
Create that directory, then create an empty `capture.request` file inside it when
the rider is visible and moving. The next eligible native draw saves a
`frame-<guest-frame>` subdirectory. The trigger is sampled at most four times per
second and runs once per process. Use a fresh directory for each launch. Keep
these game-derived files under ignored `out/`; do not commit or upload them.

`tools/native_motion_probe/inspect_readback.py <frame-directory>` uses NumPy and
Pillow to export a coverage overlay and compare motion against a CPU reference.
It reproduces guest float32 multiply/add rounding, then uses double-precision
raster interpolation. `--projection double` is available to study numerical
differences from ideal projection. Edge rules, floating-point differences and the
prototype depth gate limit direct pixel equality. This does not establish
complete dynamic object coverage.

The 15 native GPU cases still pass with zero D3D12 errors. The 64x64 deformation
case additionally checks that capture waits for completion, rejects duplicate
requests/existing directories, preserves the saved GPU bytes exactly and removes
row padding. Its independent CPU comparison has zero error over 770 interior
pixels. The live readback and additional regression below bring the current
native GPU suite to **16 passing cases**, with zero D3D12 errors.

### Moving-rider capture and projection fix

The local 3x readback of guest frame **15742**, using previous frame **15741**,
contains ten matched rider/board draws at 3360x1752. The original native pass
accepted 18,828 pixels but left large triangular gaps within the visible body and
board. Replaying the retained inputs through the actual GPU passes reproduced
both the live motion and validity textures **byte-for-byte**, establishing a
repeatable local test without another gameplay run.

The cause was projection arithmetic. ReXGlue's
`src/graphics/pipeline/shader/dxbc_translator_alu.cpp` translates Xenos MAD into
separate MUL and ADD instructions to prevent fused rounding. The prototype used
HLSL `mad`, which changed projected depth at large world coordinates. The native
shader now uses `precise` separate multiplication/addition in guest instruction
order. The depth tolerance was not relaxed.

A synthetic cancellation case (`4097 * (1 + 2^-12) - 4097.5`) fails with the old
shader: all native pixels are rejected and 729 interior expectations fail. It
passes with the corrected shader. All other 15 native GPU cases still pass.

Replaying the **same saved gameplay inputs** with the correction produces:

- 28,396 native pixels across the rider, clothing, backpack, head, hands and board.
- Zero non-finite motion vectors and zero native pixels outside the projected
  captured meshes under the guest-rounding CPU reference.
- 28,395 CPU depth-gated reference pixels; raster boundary differences remain.
- 7,785 interior pixels compared numerically: median error 0.000929 pixel,
  99th percentile 0.001906 pixel, maximum 0.002294 pixel.
- Native motion up to 7.789 pixels. A best rigid fit between the two full rider
  poses leaves up to 0.01823 world units of residual, consistent with deformation
  beyond a single rigid transform.

The corrected runtime is built and staged for `play_native_motion.bat`. This is
GPU verification against a **live captured frame**, not a second live run of the
corrected build and not proof of all poses, riders, LODs, occlusion cases or other
moving objects. Candidate identity and the prototype depth gate remain limits.
DLSS and Frame Generation are still disabled.

For local replay, run `prepare_replay.py <frame-directory>`, then
`ssx_native_motion_probe.exe --replay <frame-directory> <new-output-directory>`.
The packer stores only metadata in the ignored capture directory. The probe reads
the local geometry/depth/color, executes the production camera/native passes,
and writes another diagnostic readback. Existing output directories are refused.
No proprietary frame data is embedded in the tool's source or tests.

## Build and launch

Apply `patches/rexglue-sdk-ssx-native-motion.patch` after the existing original SSX
fixes and, in order, present-diagnostics, capture-markers, guest-frame-capture,
streamline, test-build, ssx-frame-inputs and ssx-camera-motion patches. The native
motion pass does not require Streamline evaluation. Rebuild/install the SDK and
build the SSX development preset against that install.

`scripts/play_native_motion.bat` selects the development executable, original
`out/test-user` save profile, existing 3x ROV scene scale and 4K presentation.
No experimental 720p scene override is enabled. All new features default off
in SDK source. `play_camera_motion.bat` explicitly disables the native overlay;
`play_frame_inputs.bat` disables both motion passes. `play_4k.bat` keeps using the
preserved baseline executable.

Rebuild shader bytecode with `tools/native_motion_probe/rebuild_shaders.ps1`,
passing `-SdkSource` and `-Fxc`. Configure the GPU probe with CMake against the
modified installed SDK and run `ssx_native_motion_probe.exe`. It forces the D3D12
debug layer on and Streamline off.

Remaining DLSS inputs include validated coverage for other moving objects and
effects, jitter, color/exposure decoding, HUD separation, scene insertion and
game-event resets. Frame Generation also needs Reflex and presentation timing.
