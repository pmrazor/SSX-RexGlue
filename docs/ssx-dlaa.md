# Experimental SSX DLAA checkpoint

Update: the [Preset L / Quality checkpoint](ssx-quality-preset-l.md) verifies the
current DLL and switches the rebuilt DLAA runtime to Preset L. It also adds
GPU-tested Quality output-size support. The later [race checkpoint](ssx-quality-races.md)
enabled Quality in races; the current preferred path is DLAA. The user confirmed
normal brightness after returning to DLAA. Gameplay FG remains unfinished; see
[the FG foundation](ssx-frame-generation.md) and [Reflex timing](ssx-reflex-timing.md).

Real NVIDIA DLAA now runs in the SSX scene command stream before the guest tone
mapper and HUD. The RTX 5090 live race log confirms repeated successful evaluations
at **3360x1752**, with **one scene sample per pixel (MSAA off)** and retained
history. The existing final guest frame is 3840x2160 at 3x scaling.

This is same-resolution anti-aliasing, not lower-resolution DLSS upscaling.
Whole-game motion/jitter coverage, temporal image quality, Frame Generation and
native HDR output remain unfinished. The option stays experimental and off by
default.

## Run and fallback

From this repository, use `scripts/play_dlaa.bat`. It selects the Streamline build,
the existing 3x ROV configuration and the original local `out/test-user` profile.
It enables scene inputs, camera/rider motion, corrected scene jitter and DLAA.
`-Windowed`, `-Monitor 1` and `-DryRun` work through the shared launcher.

The tested scene already renders with MSAA off; this change does not need to
disable an existing multisampled scene. A new guard rejects scene sample counts
other than one. It deliberately does not override Xenos sample layout, which
would invalidate game resolves and EDRAM addressing. The host presenter uses
`present_effect=bilinear`, with host FXAA disabled. Other guest postprocessing
remains in place; no claim is made that every game mode uses the same AA settings.

The unchanged release build remains the rendering fallback. For this local save,
run `scripts/play_4k.bat -UserDataDir "<repo>/out/test-user"`. To keep using the
Streamline build without evaluation, `scripts/play_frame_inputs.bat` explicitly
selects diagnostics with DLAA and Streamline evaluation off. Do not move saves
between profiles to test this option. A pre-test backup is retained locally under
`out/save-backups/pre-dlaa-20261004.zip`.

## Renderer changes

At the recognized full-scene tone-map draw, the command processor freezes and
retains the matching input packet, submits its scene-producing commands, and
records reconstruction on separate direct-queue command lists. The packet also
holds its producer-pool slot, preventing resource reuse while queued GPU work
still references it. Three fenced slots own reconstruction resources.

The pass decodes SSX's square-root-encoded FP16 color to linear HDR, builds camera
and captured rider/board motion, and evaluates DLAA with matching jitter and
reversed device depth. Separate view/projection constants are derived from the
captured unjittered SSX world-to-clip matrix; skewed or incompatible cameras are
rejected. Both ends of clip-history transforms use reversed depth. NGX automatic
exposure is enabled with pre-exposure and exposure scale 1; SSX's exposure texture
remains diagnostic and is not submitted as an invented scalar.

The result is re-encoded for the original tone mapper, preserving the guest
alpha channel, and copied into the texture consumed by that draw. A fresh guest
command list restores renderer bindings. HUD composition remains downstream.
This preserves linear highlights during reconstruction, but the original SDR
tone mapper and output still determine displayed brightness.

History resets on the first evaluation, missing frame sequences, incompatible
camera history, native-motion identity rejection and resolution changes. The
existing camera-motion checks identify camera discontinuities. Menu/loading
packets missing world inputs are skipped. Incompatible jitter coverage, resource
contracts, MSAA or a failed evaluation latch reconstruction off for that run and
disable subsequent scene jitter. Restarting is required to retry after a latch.

Rider/board motion remains the previously tested geometry prototype. Opponents,
other animated objects, particles and unobserved shaders need further validation.
The general input diagnostics therefore still print `dlss_ready=false`; the
specific `SSX_DLAA ... applied=true` line reports actual evaluation and insertion.

## Verification on 2026-10-04

- SDK, GPU plugin, probe and game builds succeeded.
- All 14 SSX unit tests passed, including camera decomposition/reversed-depth
  history and frozen-packet producer-pool lifetime.
- The existing 11 real SR/DLAA synthetic evaluations and 22 rejection checks pass.
- Three new 3360x1752 GPU frames exercise the complete decode, reversed-depth
  DLAA, re-encode and copy path. MSAA rejection, first-frame reset, subsequent
  history retention, finite pixels, color and alpha preservation are checked.
  Maximum encoded-channel errors were 0.004639, 0.007935 and 0.008301 against a
  0.04 threshold. The D3D12 debug-layer error count was zero for this probe.
- The live 3x race repeatedly logged `scene_samples=1 applied=true reset=false
  reason=applied insertion=before_tone_map`. A foreground observation showed
  normal-colored snow, terrain, rider and gameplay HUD. This is activation and
  basic visual verification, not a temporal-quality or performance benchmark.

Local evidence is in `out/ssx-boundary-validation-final/`,
`out/ssx-boundary-unit-results.txt` and `out/ssx-dlaa-live-20261004.txt`.
The live race has not been independently audited for all D3D12 debug messages;
the zero-error count above applies to the standalone GPU probe.

## Reproduce the source checkpoint

Apply `patches/rexglue-sdk-ssx-dlaa.patch` after the earlier
`rexglue-sdk-ssx-dlss-evaluation.patch`. The full incremental order after the
upstream SSX fixes is: present-diagnostics, capture-markers, guest-frame-capture,
streamline, test-build, ssx-frame-inputs, ssx-camera-motion, ssx-native-motion,
ssx-temporal-inputs, ssx-temporal-alignment, ssx-dlss-evaluation, ssx-dlaa.

The new patch's SHA-256 is
`C794E584196AD505BB4D1E12B1B57451BA53CF677567757EDB0C8D15855CBA89`.
Forward application, reverse application and residual-diff checks pass against
that sequence. No game assets or generated guest code are included.

Rebuild/install the patched SDK, rebuild SSX against that install, and rebuild
`tools/streamline_probe`. Run `scripts/test_streamline.ps1` in a normal developer
PowerShell session; the probe uses no game files or LLDB. Streamline stays pinned
to 2.14.1, following NVIDIA's [DLSS integration guide](https://github.com/NVIDIA-RTX/Streamline/blob/v2.14.1/docs/ProgrammingGuideDLSS.md).

The next reconstruction step is separate input/output scene dimensions and the
associated guest texture/postprocessing contracts for DLSS SR. Frame Generation
still requires its own presentation, Reflex, timing and HUD implementation.
