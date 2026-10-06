# Graphics checkpoint — 2026-10-04

Latest direction: prioritize DLSS SR and Frame Generation and defer the 720p
scene gameplay experiment. [The NVIDIA connection checkpoint](streamline-checkpoint.md)
records the optional renderer integration and real RTX 5090 tests. DLSS/FG
frame processing remains unfinished. The [SSX input collector](ssx-frame-inputs.md)
now supplies owned color/depth/exposure candidates and transform metadata to
presentation, with CPU ownership tests and GPU copy/readback tests passing.
The live 3x race also reports complete color, depth and exposure packets and
consistent camera candidates; the original test save is shared with this build.
The [camera motion checkpoint](ssx-camera-motion.md) adds actual GPU reprojection,
history handling and sampled gameplay diagnostics. The subsequent
[native rider motion prototype](ssx-native-motion.md) captures deformed geometry
and rasterizes its motion, with 16 synthetic GPU tests passing and live 3x
moving/stationary diagnostics recorded. A live readback and repeatable GPU replay
now validate rider/board pixels for one frame and fix a projection-rounding bug
that left gaps across body triangles. Broader coverage, identity and temporal
validation remain. DLSS/FG evaluation is not implemented.
[The resolution audit](resolution-source-audit.md) confirms that
existing 3x scaling predicts a 3360x1752 scene for the captured 1120x584 guest
viewport, with a 3840x2160 final image. It also documents the stock 4x alternative.
The experimental scene-size hook remains disabled, including in its staged config.

The [jitter/color/exposure checkpoint](ssx-temporal-inputs.md) adds a GPU scene
color decoder, guarded scalar exposure and opt-in partial scene raster jitter.
Jitter is excluded from camera and native motion. All 48 GPU cases across the
three probes pass, with zero D3D12 validation errors, plus a jitter capture/replay
check. Live testing exposed purple corruption from mismatched geometry/material
jitter; extending coverage fixed it in the repeated race. The color-only comparison
preserves values above 1 and confirms nonuniform exposure. Full scene coverage and
pre-exposure semantics remain to be validated before DLSS evaluation.

## Checkpoint 1: baseline and 4K configuration

Branch: `graphics/4k-baseline`. Upstream SSX commit:
`b8bc334ece118cf62c320f160cb948e2620b5a91`. The workspace was empty; no prior
checkouts or game files were assumed. No applicable AGENTS.md was found in the
workspace ancestors or SSX repository. The SDK's CONTRIBUTING.md and linked
contributing guide were inspected.

Implemented:

- A 4K launcher selecting 3x on both axes, ROV, 3840x2160 window settings,
  borderless fullscreen and bilinear presentation. No temporal reconstruction.
- Explicit 1x and QHD comparison profiles, windowed mode, external asset paths,
  dry-run output, and opt-in diagnostics. Original launchers retained.
- A Windows build helper that runs codegen before the first CMake configure.
  Clean CMake configuration now explains the missing generated-file prerequisite.
- CMake stages the runtime-loaded Xenos GPU plugin and launchers beside ssx.exe.
  Launcher edits are staged even when the executable does not need relinking.
- SSX preloads its adjacent `ssx.toml` before the pinned SDK resolves content
  paths. Direct desktop launches now honor game/user folder settings; command-line
  values still have priority. Previously a configured game folder produced a
  `--game_data_root was not provided` startup error.
- SDK diagnostic patch: `SSX_SWAP` logs actual source allocation, active output,
  guest texture/packet sizes, effective scale, render path, and formats when they
  change. `SSX_PRESENT` logs surface/swap-chain sizes, format and tearing support
  on connect/resize. Both diagnostics default off and add no GPU readback.
- Separate SDK test-build patch fixes the unit target's missing xxHash dependency
  and malformed escape sequences in its depfile assertions (raw strings retain
  the intended literal backslashes).
- Asset, generated-code, capture and machine-local CMake exclusions.

### Exact SDK baseline

The manifest declares `sdk_version = "0.10.0"`. Its generation comment records
`0.10.0.2-dev.gc94f5eb`. Tag v0.10.0 resolves to
`f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`; the recorded generator commit resolves
to `c94f5ebdcb3c9d1a460ca48e04f9758448f8d518`. **Their Git trees are identical**:
`93d1bc10733b23e1c16475eb5c62e3bb2a68daa1`. Thus the comment does not imply a
different source implementation from this tag.

All nine bundled SSX patches applied cleanly using `git am`. Patch-file SHA-256:
`9c8fe62bcdf0c973a7e906d7c9d1252ef640bc923a3619e90efcad10e2b73709`.
Submodules were checked out at their recorded commits. Fifteen Windows
libmspack symlink stubs were materialized from their in-repository targets.
The SDK checkout is in `../../work/rexglue-sdk`, on `graphics/ssx-sdk-baseline`.
The local applied-patch baseline is `eed22e8a490fb8a60f8c392a391c3d5eb16c04a4`;
commit identity/timestamps will naturally differ when the mailbox is reapplied.

### What the source confirms

| Question | Finding |
|---|---|
| Recompiled rendering? | Guest PPC becomes C++; GPU commands use the Xenia-derived Xenos renderer. |
| 3x available? | Yes. `TextureCache::GetConfigDrawResolutionScale` accepts 3; per-axis overrides take precedence. D3D12 also clamps against hardware limits. |
| 4K rendering established? | Final source/active region and swap chain are 3840x2160 at 3x3 ROV. Native scene resolution remains unverified: the subsequent 1x outdoor capture contains a 1120x584 scene-color candidate feeding a 1280x720 final image. See the capture findings. |
| Fullscreen behavior? | SDL uses borderless desktop fullscreen, not an exclusive display-mode switch. |
| UI scaling glitches? | Reported upstream at 2x. Title and in-race settings menu align correctly at 3x. User reports no missing, stretched or flickering HUD during the ground training tutorial. Other tracks remain untested. |
| Experimental temporal FSR? | Confirmed color reused as depth and motion vectors, zero jitter, fixed 16.666 ms, and history reset every frame. Disabled for this baseline. |
| HDR output? | Presenter guest/intermediate buffers use R10G10B10A2_UNORM; swap chain uses B8G8R8A8_UNORM. These formats do not establish native HDR. |

See [renderer investigation](renderer-investigation.md) for exact source entry points.

### Build and validation record

Host GPU: NVIDIA RTX 5090, driver 617.14 (queried locally).
The read-only `tools/graphics_probe` confirmed ROV support and resource binding
tier 3. It identified `Alienware AW3225QF(DisplayPort HDR)` at 3840x2160 / 240 Hz,
10 bits per color, desktop PQ/BT.2020. Driver-reported peak and full-frame
luminance were both 1000 nits; these metadata are not measured calibration values.
A second display was 3840x2160 / 60 Hz, SDR. The renderer remains SDR regardless
of the Windows desktop HDR setting.
Visual Studio 2022 Community with MSVC 14.44.35207; Windows SDK 10.0.26100.0;
CMake 3.31.6; Visual Studio's Ninja; LLVM 19.1.5. Project documentation recommends
LLVM 20+; SDK CMake enforces 18+. The installed 19.1.5 successfully built the SDK
baseline and the generated SSX application successfully.

- Original SSX configure: reproduced missing `generated/rexglue.cmake` failure.
- Patched SDK Release baseline: built `rexglue.exe`, `rexruntime.dll`, and
  `rexgpu-xenos.dll`; `rexglue --version` returned `0.10.0.9-dev.geed22e8`.
- Launcher: Windows PowerShell 5.1 dry run passed. A native argument probe verified
  paths containing spaces, working directory, 3x/ROV selection, windowed mode,
  diagnostic switches, and preservation of a nonzero child exit code (37).
- New CMake prerequisite error verified. Diagnostic patch reverse-application
  check passed against the edited SDK tree.
- SDK diagnostics rebuild and Release installation passed. An independent
  installed-package consumer configured, compiled and ran successfully; the
  installed package exports the Xenos plugin target used by the project.
- SDK Release unit suite: **209 passed, 4 existing BitStream-write skips,
  0 failed** (213 discovered). SDK unit tests do not verify SSX rendering.
- The build helper's missing-game preflight was verified before assets were supplied.
- SSX code generation completed (719 files; first pass 152.9 seconds) from the
  supplied XEX. Title ID 4541096D, media ID 55ABAB6C, version 0.0.0.2. The
  `mfvscr` warning corresponds to the existing handwritten override.
- Repository dangling-goto check: no dangling labels reported.
- SSX Release application: built and linked successfully, including the direct-
  launch config fix. Incremental rebuild passed and staged launcher hashes match.
- 1x/RTV: visible title screen, approximately 120 guest FPS. Log confirms an
  active 1280x720 frame. At this desktop's 150% scaling, a window requested as
  1280x720 logical units has a 1920x1080 physical swap chain.
- 3x/ROV fullscreen: visible title and intro; approximately 120 guest FPS once
  startup work settles. This is **not a gameplay benchmark**. Runtime evidence:

  ```text
  SSX_PRESENT surface=3840x2160 swapchain=3840x2160 dxgi_format=87 allows_tearing=true
  SSX_SWAP frame=1 packet=1280x720 texture_guest=1280x720 source=3840x2160 active=3840x2160 draw_scale=3x3 path=rov xenos_format=6 dxgi_format=27
  ```

- Test saves/cache are isolated under ignored `out/test-user`. Full logs are
  local under `out/build/win-amd64-release/logs`; `ssx_004.log` is the visible
  1x test and `ssx_005.log` is the initial visible 4K test.
- Both paths emit invalid texture-fetch warnings and failed searches for some
  guest asset/cache paths. Startup still renders; their gameplay impact remains
  unverified. No invalid-fetch bypass was enabled.
- Shell-launched tests ran on a separate desktop and could produce audio without
  a window visible to the user. The working visible tests were launched through
  desktop controls. This is separate from the TOML startup-path fix.

### Ground training tutorial check

The user entered the ground training tutorial and paused it, and reported no
missing, stretched or flickering HUD while riding. The paused scene and audio
settings menu were inspected directly: the mountain/rider scene is visible
behind the menu, text and selection highlighting align, and the image fills
the fullscreen output. No controls or settings were changed during this check.

The same `ssx_005` session retains the established 3x/ROV/4K output; no source
dimension or swap-chain change was reported. Logs rotate, so evidence was
extracted to ignored `out/validation/race-session-metrics.txt` and summarized
in `race-session-summary.json` before older files could be overwritten.

At 13:19:01–13:19:17 local time, nine two-second profiler samples reported
117.3–119.0 FPS (median 118.3), median GPU busy 92%, while the race was paused.
The preceding minute ranged from 52.6 to 120.1 FPS, median 117.05, but includes
unlabeled riding/loading/pause transitions. It is not a reproducible race
benchmark or a frame-time percentile measurement. Diagnostics and texture-fetch
warning logging were enabled. A fixed moving section and a same-scene 1x
comparison are still needed for performance conclusions.

Initial 4K launch/tutorial checkpoint is established. This does not establish
every track's stability, motion quality, native resolution of every effect,
or any DLSS/HDR functionality.

### Capture preparation

`patches/rexglue-sdk-capture-markers.patch`, applied after the present diagnostics,
adds optional draw/resolve/swap markers using the existing `gpu_debug_markers`
flag. Labels include guest frame ID, shader hashes, host viewport, raw guest
render-target/depth registers and resolve destination. They add metadata only
and are disabled by default. PSO names already carry matching shader hashes;
the new labels add the ROV/guest state needed to interpret those draws.

The updated GPU plugin compiled and linked successfully. Patch reverse-check
passed. An isolated `out/renderer-capture` executable folder contains this plugin
and a 1x/ROV windowed config with shader dumping enabled to reduce the first
capture's size. The first outdoor F12 capture replayed but contained only
presentation commands. Its initial contents reveal FP16 color and depth
candidates; see [capture findings](ground-training-capture.md).

The subsequent `rexglue-sdk-guest-frame-capture.patch` and SSX F8 binding compiled
and passed a real RenderDoc 1.46 title-screen capture/replay smoke test. Matching
begin/end logs identify guest frame 3012 (`saved=true`); the capture contains
347 draws and 374 SSX markers. The reusable Python inspection script completed
successfully. The normal-race F8 capture subsequently succeeded and replayed:
guest frame 60306, 1,874 draws, 1,908 SSX markers. Scene color/depth, exposure
and tone-map passes are now identified in [race-capture.md](race-capture.md).

The main 4K build retains the previously tested GPU plugin. This capture
folder shares the test save location, so close the game before launching it.

See [the single-frame capture instructions](capture-next-frame.md). The race
capture confirms a 1120x584 scene before 1280x720 output. The new guarded
`ssx_scene_720p` experiment builds and passes seven cases against the generated
guest selector. It remains off by default; gameplay at 1x, then a 3x scene capture,
are the next resolution checks. A separate 1x test build is staged locally.

### Smallest useful runtime check

The user has now supplied the local extracted game directory with `default.xex`
and `.big` archives. It is linked through the ignored `game` junction. Assets stay
on disk and out of Git; do not upload or commit them.

For a repeatable performance check beyond the tutorial, run
`play_4k.bat -GameDir "..." -Diagnostics` and play one race for about 60 seconds,
opening a pause menu once. Return the latest
file from `logs/` beside ssx.exe and note the track, HUD glitches, and whether
fullscreen filled the AW3225QF. The new diagnostic log should show
`active=3840x2160`, `draw_scale=3x3`, `path=rov` and `swapchain=3840x2160` during
gameplay. Allocation padding in `source` can legitimately exceed the active size.
These values verify the final scaled path; a scene capture is still needed to
establish which earlier passes are full resolution. Compare the same scene with
`launch_ssx.ps1 -Profile 1x -GameDir "..." -Diagnostics`.

The required stock race capture is now available locally. It provides enough
evidence to begin the scene-resolution experiment and trace tone mapping; no
replacement stock capture is currently needed. The next capture should test the
new scene-size hook, following [race-capture.md](race-capture.md).

## Later checkpoints

2. Identify scene/depth/tonemap/HUD passes and temporal state from a real SSX
   frame; add a validated resource handoff with frame ownership and resets.
3. DLSS SR: integrate real inputs, including dynamic-object motion; compare
   moving rider, snow trails, camera cuts, menus and loading against native 4K.
4. Frame Generation: integrate Streamline presentation and Reflex with matching
   simulation/render/present frame IDs, HUD handling and reset behavior.
5. HDR: preserve measured scene values before SDR clipping and implement output
   color management plus peak, paper-white and UI controls with calibration.

DLSS SR, Frame Generation, Reflex and native HDR are **not implemented** in this
checkpoint. No placeholder depth/motion resources or HDR-only swap-chain toggle
are presented as completed features.
