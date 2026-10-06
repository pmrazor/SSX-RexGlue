# Ground training capture — 2026-10-04

Input: local `out/renderer-capture/ground-training.rdc` (319,224,833 bytes),
RenderDoc 1.46. The user captured outdoor riding with rider, snow, terrain and HUD.
All capture data, textures and shader disassembly remain in ignored `out/`.

## Established observations

The thumbnail and replay show the requested scene. The recorded command stream
is a presenter refresh, however: three draws at EIDs 50, 62 and 64, followed by
Present at 69. There are no recorded guest SSX draw/resolve/swap markers.

- EID 50 samples resource 1132 (1280x720 R10G10B10A2_UNORM) using a simple
  sampling pixel shader. Its 1920x1080 viewport writes swap-chain resource 309,
  B8G8R8A8_UNORM. The game HUD is already inside resource 1132.
- EIDs 62 and 64 sample resource 306 and use the same overlay pipeline; these
  are the host overlay draws, not the game's HUD-composition passes.
- The 1920x1080 swap chain is consistent with the capture config's 1280x720
  logical window on this desktop at 150% DPI. The capture uses 1x/ROV by design.
- The inventory includes 514 textures. Some initial contents expose useful
  intermediate snapshots even though their producing commands are absent.

Resource IDs below apply only to this file, never to a runtime resource hook.

| Resource | Format and dimensions | Measured contents / interpretation |
|---|---|---|
| 21838 | R16G16B16A16_FLOAT, 1120x584 | Outdoor rider/terrain without HUD. RGB maximum (3.71875, 3.828125, 4.0), minimum (0.08984375, 0.076171875, 0.107421875). Candidate scene color retaining values above 1. |
| 1457, 1458 | R16G16B16A16_FLOAT, 560x292 and 280x146 | Reduced-resolution color candidates, RGB peaks up to 3.77734375 / 3.734375. Role and pass order unconfirmed. |
| 1445, 27069 | R32_FLOAT, 1120x584 | Depth-like silhouettes matching rider/terrain; range 0 to 0.15394020080566406. Projection, reversal and encoding unconfirmed. |
| 27071 | R32_FLOAT, 560x292 | Reduced-resolution depth candidate, range 0 to 0.15344417095184326. |
| 1108 | R8G8B8A8_TYPELESS, 1280x720 | Final-size guest color candidate; maximum RGB 1. Producer absent. |
| 1132 | R10G10B10A2_UNORM, 1280x720 | Proven presenter input; maximum RGB 0.99706745. Contains the game HUD. |

These observations support investigating native HDR before final presentation.
They do not establish scene-linear units, exposure, preserved sun detail, the
absence of earlier clipping, or a calibrated HDR output. A peak of 4.0 may itself
reflect a limit or encoding; shader tracing is required.

The scene-sized allocation is 1120x584, not 1280x720. The existing 4K launcher
and logs establish a 3840x2160 final scaled image, not native 4K for every scene
pass. Determine whether scene resolution is fixed, dynamic, padded or affected
by the original game's settings before implementing an SSX resolution hook.

## Capture-boundary fix and validation

The independent UI presenter can refresh a previously completed game image.
An F12 capture bounded by host Present calls can therefore miss guest rendering.
The new optional SDK patch captures from the start of a guest frame through its
submitted swap. The SSX F8 binding invokes it through the existing command registry.
Repeated requests coalesce atomically, an existing capture is left alone, and
only a capture started by this code is ended during normal completion or shutdown.

Both the GPU plugin and SSX executable rebuilt successfully. The patch passed
`git apply --reverse --check`. A real F8 title-screen capture logged guest frame
3012 begin/end and `saved=true`; replay reported no problems. The saved
`out/renderer-capture/title-f8-smoke.rdc` contains 1,689 API events, 347 draws,
374 SSX markers and 33 textures. Its broader frame window includes both the
guest work and any concurrent host presentation.

`tools/renderdoc/inspect_capture.py` ran successfully on that capture, exporting
the inventory, draw bindings, floating-point min/max, EXR snapshots and PNG
previews. Preview normalization is for inspection only, not an HDR implementation.
The original outdoor analysis is under `out/capture-analysis`.

Next input: one outdoor F8 capture from the rebuilt capture executable. Use the
[updated steps](capture-next-frame.md). Then correlate shader hashes, guest
target/resolve registers and resource usage to find scene color, depth,
tone mapping and HUD boundaries. No temporal inputs or HDR hook are validated yet.
