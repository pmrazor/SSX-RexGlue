# SSX frame input checkpoint

2026-10-04. Continues the [Streamline connection](streamline-checkpoint.md).
The D3D12 renderer now collects SSX-specific input candidates and passes owned,
immutable snapshots to presentation. **DLSS SR and Frame Generation remain off.**
This is the input producer, not a completed temporal reconstruction integration.

Follow-up: [the camera motion checkpoint](ssx-camera-motion.md) now consumes these
snapshots, adds viewport metadata, and computes camera-only vectors with tests.
The collection-only details and original validation below describe this earlier
checkpoint; animated-object motion and DLSS/FG still require further work.

## Implemented

`patches/rexglue-sdk-ssx-frame-inputs.patch` adds an opt-in collector to the pinned,
SSX-patched ReXGlue v0.10.0 renderer. Set `d3d12_ssx_frame_inputs=true` at startup.
The normal default is false; no Streamline initialization is required to collect.

Pass selection requires title `4541096D`, the observed guest shader hashes and
guest viewport dimensions. It recognizes the captured 1120x584 scene and the
optional 1280x720 scene size; enabling collection does not change scene size.
The quarter-size bloom/tone-map pass is excluded despite sharing shader hashes.

| Candidate | Collection point | Packet contents |
|---|---|---|
| Encoded scene color | VS `E857B4AF9617DC29`, PS `A2CD8E82699B2C0A`, fetch 0 | Separate FP16 RGBA texture at the actual scaled scene extent |
| Exposure | Same pass, fetch 1, branch b128 and explicit LOD from PS c1.x | Separate R16 typeless texture containing that mip; R16_UNORM view candidate; PS c0–c8 and c255 |
| Resolved scene depth | VS `7365E5786AD4C10C`, PS `0E6C83314BF3FBA1`, fetch 0 | Separate full-size R32_FLOAT texture before the half-size min/max reduction |
| Camera candidate | Full-scene draws using VS `C44105DD14EFDB33` | Guest c192–c195, sample count and a conflict flag if these differ between matching draws or contain non-finite values |
| First object transform | Same vertex shader | c244–c247 separately, rather than labelling the object/camera product as camera motion |
| Depth interpretation | First matching world draw | Original guest viewport Z scale/offset and RB_DEPTH_INFO |

Each copied texture retains the six raw guest fetch words, host swizzle, component
sign modes, guest address, source mip, format and actual dimensions. This matters:
the captured FP16 scene texture uses signed components (`0x55`). Copying storage
does not apply the fetch's swizzle, exponent adjustment or shader color decoding.
The scene signal is square-root encoded, not already linear HDR.

Resolution scaling is read from the actual cached texture's key. The guest
binding key does not acquire the scaling flag when `FindOrCreateTexture` chooses
scaled storage. Reading the binding key initially caused correct 3x color/depth
resources to be rejected; the live log exposed this and the collector now follows
the same texture-key rule as `IsTextureBindingScaled`.

An exposure mip can contain more than one texel when scaling is enabled. The
collector preserves the actual small mip instead of silently treating it as a
scalar. Unsupported bindings, layouts, formats, dimensions or missing branches
fail collection for that role. Repeated matching color/depth/exposure passes mark
the packet ambiguous instead of overwriting an earlier copy in flight.

## Ownership and synchronization

The collector runs after texture loading and before the selected guest draw.
It copies into its own committed resources and restores the texture cache's
tracked source state. Holding a reference to a cache texture alone would not
protect its contents against later resolves/uploads.

A six-slot pool only reuses a packet when no consumer owns it and its producing
GPU submission has completed. Saturation skips collection for that frame without
waiting on gameplay. At the captured 3x scene extent, six color/depth packets cost
approximately 404 MiB before allocation alignment and small exposure resources.
The memory is bounded, but collection overhead still needs gameplay measurement.

The packet is sealed with its guest frame number at swap, submitted before
publication, and attached to the same mailbox entry as that frame's final image.
The presenter takes both references under its existing consumer lock and retains
input references through the paint fence. UI repaints retain the guest frame ID;
they do not create additional simulation frames. Missing packets replace old
mailbox inputs. No Streamline resource tagging or evaluation runs yet.

The probe caught a source-write ordering issue for tiny exposure mips on this RTX
5090: with direct COPY_SOURCE-to-SRV restoration, a subsequent source overwrite
changed the snapshot readback. Restoring through COMMON made the same test pass,
including when barriers are submitted in one batch. The renderer and probe share
`GuestInputTexture::GetCopyRestoreBarriers`, including that transition. This is
an observed test result, not a claim to have established a general driver defect.
[Microsoft's resource-barrier documentation](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12)
describes the state-transition contract used by the copy path.

## Verification

- Patched runtime, Xenos renderer and separate SSX development executable build.
- SDK unit suite: 212 passed, four existing BitStream skips, zero failures.
  New tests cover pass selection, exclusion of unrelated/bloom draws, bounded
  pool reuse across CPU ownership and GPU completion, stale metadata clearing,
  and missing/failed/duplicate input masks. The separate unbuilt PPC test target
  is outside this unit-suite invocation (`ctest -L unit`).
- Existing SSX generated-code scene hook test passes; its seven cases retain
  coverage of both the normal behavior and the disabled-by-default scene option.
- `tools/frame_inputs_probe` passes on the RTX 5090 with the D3D12 debug layer:
  3360x1752 FP16 color, reuse, resize to 3840x2160, R32 depth, mip-8 R16 exposure
  from 280x146 and 840x438 sources, and exposure reuse. It verifies exact bytes
  after overwriting the source and reports zero D3D12 validation errors.
- Probe transcript: `out/build/frame-inputs-probe/frame-inputs-test.txt`.
- Patch application checked forward against the previous checkpoint and in
  reverse against the current SDK source, using a separate Git index.

The user then drove a normal race at 3x and paused it. The live renderer reports
3360x1752 FP16 color, 3360x1752 R32 depth, and a 3x1 exposure texture from mip 8.
The presenter logs receipt of complete packets. A desktop inspection confirmed
the rider and terrain behind the pause menu; the restored profile had reached
the world map without repeating the tutorial.

The saved log sample contains 109 packet records: 96 complete and 13 early menu
packets containing color/exposure only. All 70 records with a camera candidate
also contain all three resources, with no failed or duplicate inputs and no
within-frame matrix conflicts. There are 15 distinct recorded matrices as the
view changes, with 1–62 matching world draws per sampled frame. The final paused
frame has 39 matching draws. This supports treating c192–c195 as a camera
candidate; it does not yet validate temporal reprojection or object motion.
Guest viewport Z scale/offset are -1/+1, and depth info is `000102D0` in this run.

The local evidence is preserved in `out/frame-inputs-validation/`: filtered
`ssx-inputs-3x.log`, `summary.json` (including executable/DLL hashes),
`gpu-copy-test.txt`, and `sdk-unit-tests.txt`. The log's missing packets occur
before matching scene passes; there were no logged pool-busy skips in this sample.

These checks establish live pass detection, packet delivery and CPU metadata,
plus the separately tested allocator/copy path. They do not read back and compare
the live game's copied texels, or establish that depth, exposure, matrices and
late effects are already valid DLSS inputs. Gameplay performance and visual
regression testing across tracks remain limited.

## Run the input check

Close an existing SSX instance, then run `scripts/play_frame_inputs.bat`. It uses
the separate `out/build/win-amd64-streamline/ssx.exe`, the usual 3x ROV setup,
and the same external game folder discovery as `play_4k.bat`. Optional arguments
include `-GameDir "D:\Games\SSX"`, `-Windowed`, `-Keyboard`, and `-DryRun`.
The 1280x720 scene experiment stays off.

The launcher explicitly uses `out/test-user`, the profile used by the earlier
baseline and capture sessions. The development executable's local `ssx.toml`
also points there, so direct launches load the same progress. Before this fix,
the development build used ReXGlue's default user folder and showed a new profile;
the original save had not been erased. A local backup of the original profile,
content headers and achievements is kept under ignored `out/save-backups/`.

Enter a normal outdoor race, ride for 10–15 seconds, pause, and leave it running.
First inspect the newest development build `logs/ssx_*.log` for `SSX_INPUTS` records. This is
enough to establish pass detection and packet completeness; no new large RenderDoc
capture is needed for that initial check.

Expected resource masks are `captured=7 failed=0 duplicates=0`: bit 1 is color,
bit 2 depth, bit 4 exposure. This means **raw resources complete**, not DLSS ready.
Inspect `camera_samples` and `camera_conflict` separately. For the stock scene at
3x, expected color/depth dimensions are 3360x1752. `SSX_INPUT_TEXTURE` records
include mip and fetch metadata; `SSX_INPUT_CAMERA` records the matrix and guest Z
mapping. `packet=missing` is explicit when no known pass was seen or the pool was
busy. Menus and loading may legitimately have no complete scene packet.

The ordinary `scripts/play_4k.bat` and `out/build/win-amd64-release` remain the
working fallback. This checkpoint does not require the NVIDIA DLLs or LLDB.

## Build and patch order

Starting with SDK v0.10.0 plus the original nine SSX patches, apply the earlier
checkpoint patches in this order, then the new input patch:

1. `rexglue-sdk-present-diagnostics.patch`
2. `rexglue-sdk-capture-markers.patch`
3. `rexglue-sdk-guest-frame-capture.patch`
4. `rexglue-sdk-streamline.patch`
5. `rexglue-sdk-test-build.patch`
6. `rexglue-sdk-ssx-frame-inputs.patch`

Use the [Streamline checkpoint build instructions](streamline-checkpoint.md#reproduce)
for the optional SDK configuration, dedicated install prefix and SSX development
preset. Build `rexruntime`, `rexgpu-xenos`, and `unit_tests`, run
`ctest --test-dir out/build/win-amd64 -C Release -L unit --output-on-failure`, then
install the SDK to `out/install/win-amd64-streamline` and rebuild SSX.

The GPU probe uses the installed SDK:

```powershell
$sdk = 'D:/dev/rexglue-sdk/out/install/win-amd64-streamline'
cmake -S tools/frame_inputs_probe -B out/build/frame-inputs-probe -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++ "-Drexglue_DIR=$sdk/lib/cmake/rexglue" "-DCMAKE_PREFIX_PATH=$sdk"
cmake --build out/build/frame-inputs-probe
Push-Location out/build/frame-inputs-probe
./ssx_frame_inputs_probe.exe
Pop-Location
```

It uses the normal D3D12 provider with Streamline disabled, no game assets and
no visible window. It is not an end-to-end emulation or gameplay test.

## Next implementation boundary

Pass detection and complete packets are now verified in the live 3x race. Next,
decode color/exposure with the guest fetch semantics, validate depth/projection
conventions through reprojection, and determine the right insertion point around
later effects and HUD. The recorded world-to-clip matrix has not yet been proven
to generate correct temporal reprojection across frames.

Camera reprojection alone cannot supply motion for the rider, animated objects,
deformation or particles. Previous transforms/bones, object identity, jitter,
history/reset policy, HUD separation, and render versus simulation/present timing
still need implementation and validation before calling DLSS SR or FG. No zero
motion substitute, color-as-depth input, or unconditional history reset is sent
to NVIDIA as if it were a correct integration.
