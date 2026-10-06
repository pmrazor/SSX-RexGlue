# Next renderer checkpoint: one local GPU frame

The initial 4K ground training tutorial check is recorded in
[graphics-checkpoint.md](graphics-checkpoint.md). A full normal-race F8 capture
is now available and analyzed in [race-capture.md](race-capture.md), identifying
scene color, exposure and tone mapping. The workflow below is retained for
subsequent experiments; another stock race capture is not currently needed.

## Prepared local build

The current workspace has `out/renderer-capture/ssx.exe` with a separately built
GPU plugin, the F8 guest-frame capture command, and an adjacent machine-local
`ssx.toml`. This config uses:

- 1x rendering and ROV, to reduce capture size while retaining the 4K build's
  rendering path; it is not a new 4K performance profile.
- A window, GPU debug markers and shader dumps under `out/renderer-capture/shaders`.
- The supplied external game directory and the existing `out/test-user` save folder.

This folder and all game-derived data are ignored by Git. The capture plugin
compiled successfully. Its F8 title-screen capture replayed successfully under
RenderDoc 1.46 with 347 draws and 374 SSX markers. Do not run the
capture build concurrently with the current game because they share test saves.
For another machine or a clean checkout, apply the optional capture-marker patch
after the present-diagnostic patch, then apply
`patches/rexglue-sdk-guest-frame-capture.patch`, rebuild/install the SDK, and rebuild SSX.

## Smallest useful capture

RenderDoc 1.46 is now installed on this PC. The first outdoor F12 capture
recorded only the UI presenter refreshing an existing image. Use the rebuilt
game's **F8** binding to capture from the guest frame start through its submitted
swap. This avoids depending on the separate UI presentation cadence. Do not
trigger F12 or a RenderDoc UI capture at the same time.

1. Finish the current test and close SSX normally.
2. In RenderDoc, choose **File > Launch Application**. Select the prepared
   `out/renderer-capture/ssx.exe`. Set its containing folder as the working
   directory. Leave command-line arguments empty; the adjacent config has the
   local paths and capture settings.
3. Launch, enter a normal race (tutorial prompts can obscure the scene), and reach a view with
   the rider, snow, terrain and gameplay HUD visible. Capture while riding or
   standing in the scene, with the pause/settings menu closed.
4. Press **F8 once** with the game focused. RenderDoc should show a new
   **User-defined Capture**. Open it and choose **File > Save Capture As**:
   `out/renderer-capture/<unique-test-name>.rdc`. Preserve the earlier files.
   Close the game normally when finished.
5. Provide only the local `.rdc` path and the track/scene description. Keep the
   capture and shader dumps on this PC; no upload is needed.

The capture uses RenderDoc's official
[StartFrameCapture/EndFrameCapture API](https://github.com/baldurk/renderdoc/blob/v1.46/renderdoc/api/app/renderdoc_app.h).
In the local log, matching `SSX_CAPTURE begin guest_frame=N` and
`SSX_CAPTURE end guest_frame=N saved=true` confirm the request was processed.
If F8 logs that the optional patch is unavailable, the wrong GPU plugin was staged.
If capture or replay fails, keep the local failure log and report the error;
do not change graphics drivers or rendering settings to conceal the failure.

## What will be inspected

Trace the final color backward through resolves, shader hashes and textures;
identify scene color, the depth encoding, tone mapping and HUD draws. The optional
labels start with `SSX draw`, `SSX resolve`, and `SSX swap`. Raw target/depth
registers must be decoded using this exact SDK revision. Pipeline state includes
the bound guest constant buffers needed to search for camera/projection data.

This first frame can identify candidate resources. Valid previous transforms,
animated-object motion, jitter, exposure, camera cuts and history resets still
require temporal investigation afterward. HDR additionally needs confirmation
of unclipped scene brightness before its selected hook.

## Local analysis tool

With a saved capture open in RenderDoc 1.46, open **Window > Python Scripting**,
open `tools/renderdoc/inspect_capture.py` and click **Run**. It writes
`capture-analysis/<capture name>/status.json` beside the capture, plus action and
resource inventories, draw bindings, and floating-point EXR/PNG snapshots.
Check `complete: true`; a failure includes its traceback. Keep the capture and
exports under ignored `out/`. The PNG range normalization is only a visualization;
EXR files and JSON min/max retain the original floating-point values.

For a focused inspection, `tools/renderdoc/inspect_passes.py` exports the selected
draw's shader disassembly, constant buffers, SRV formats and terminal mip values.
Set its `EVENTS` tuple for a batch. It was verified on the race's luminance,
adaptation and tone-map draws (7654, 7680, 8121). Guest constant buffers may be
packed; correlate host array indices with shader disassembly.
