# Reflex timing checkpoints

## Transition and live-mode changes — partial runtime verification

Follow-up patch `rexglue-sdk-ssx-reflex-transitions.patch` applies after the
render-driven checkpoint below. SHA256:
`7bbe8bc1e5a4b427a216ec23117021ceecba52f85419b89291ecc8447b4d7006`.
Forward, reverse and residual patch checks pass with a disposable index.

Source inspection found a control-only branch in worker `823D2DF0` which reads
controls but never calls the two-channel physics publisher `823D1B10`. The new
hook records entry into that publisher explicitly. The scheduler accepts a
completed control-only update, while rejecting incomplete expected samples or
missing controller snapshots. It logs detailed state on a failed handoff.

The synchronous frontend/loading renderer `8236E878` now has input/pacing and an
ordered GPU marker at its own renderer-lock call site. Guest state 9 skips its
render body and is excluded. A full Reflex token can complete on ordinary
rendering without a DLAA evaluation; the earlier transport-only DLAA path
retains its evaluation requirement.

`ssx_reflex_mode` adds live `off`, `on` and `boost` choices to F4 settings under
SSX/Reflex. Changes are applied at a new frame boundary, keeping Sleep and PCL
markers in Off mode. PCL initialization retains its original message target
when options change from a guest frame thread. Launchers pass the chosen mode.

The frontend worker family (`823D4C80`, `823D5E28`, `823D62B0`, `823CF578`) and
renderers (`8237DA98`, `8237DF28`, `8237E1A0`) now participate in the same
frame-driven protocol. Expected sample publications are explicit; control-only
updates are accepted only after their input snapshots complete.

SDK/probe/game builds, 25 SSX SDK tests, 15 timing-analysis tests, two
driver-report tests and both game CTest targets pass. The game hook target has
10 cases. The build is staged. World-map driver reports now appear, and the
performance-build race trace retains ordered input/DLAA/present ownership.
One loading transition in `ssx_034` still reached the 500 ms physics-handoff
timeout (`render=16942`, input complete, worker not complete). Full transition
coverage and live Off/On/Boost cycling remain unverified.

With the user's administrator approval, NVIDIA's bundled official verifier ran
its Reflex On comparison cycle. It reported seven warnings, so this is not a
clean verification pass. The race moved to a results screen during the cycle;
the varying workload also prevents treating it as a controlled performance
benchmark. The Boost cycle was not completed. Results are preserved locally in
`out/reflex-official-validation/`. The verifier exited gracefully, reported
that clock limits were released, and test mode was disabled successfully before
the optimization measurements. No verifier process or clock override remains
from that run. Software driver latency is not physical input-to-photon latency.

## Render-driven gameplay Reflex — 2026-10-05

**The experimental 30 FPS rendering coupling is removed.** The corrected live
race trace presents 333 distinct DLAA frames in 4.998 seconds (66.6 FPS), with
matching NVIDIA driver reports and verified input delivery to every captured
physics update. This is a diagnostic observation on this race, not a performance
guarantee or a measured input-to-photon improvement. Gameplay FG remains off.

Run `scripts/play_reflex.bat` for the current experiment: original `out/test-user`
save, 3× DLAA, Reflex On. The scene remains 3360×1752 with final 3840×2160 output.
`launch_ssx.ps1 -ReflexSyncMode on` enables render-driven scheduling; `off` and
`boost` use the same scheduling with different NVIDIA pacing modes. The ordinary
`scripts/play_dlaa.bat` remains the fallback with this scheduler disabled.
`play_reflex_sync.bat` explicitly retains the older coupled 30 FPS diagnostic.

### Input and scheduling

Each normal rendered update waits for the prior frame's presentation, then runs
NVIDIA sleep and SimulationStart before sampling all four host controller slots.
An immutable snapshot carries that update ID. When SSX's original dispatcher has
a physics job due, that job consumes the snapshot through its existing controller
processing path; the gamepad wrapper uses flag 1. Its separate flag-8 device query
retains XAM's original rejection behavior. Other callers and independent polling
retain their existing behavior. The scheduler checks all four slot reads and both
published sample channels before ending an update containing physics work.

Rendering no longer waits for a future physics tick. Frames between ticks use
SSX's original interpolation and previous/next sample selections. The per-render
NVIDIA ID follows the ordered GPU marker, actual DLAA, mailbox image and actual
Present. Repainting a completed frame cannot submit another full PCL timeline.

This follows the fixed-step scheduling pattern discussed by NVIDIA in its
[custom-engine Reflex guidance](https://forums.developer.nvidia.com/t/implementing-nvidia-reflex-latency-markers-into-a-custom-engine/266092),
with the markers described by the [Streamline Reflex guide](https://github.com/NVIDIA-RTX/Streamline/blob/v2.14.1/docs/ProgrammingGuideReflex.md).
**Physics and the game's controller processing remain at their original 30 Hz.**
Host controller acquisition runs at render rate. There is no new render-rate
camera/pose response: extra renders interpolate earlier physics states. A short
host-input-to-Present interval therefore does not measure gameplay response or
physical input-to-photon latency.

### Code and verification

`patches/rexglue-sdk-ssx-reflex-frame-driven.patch` applies after
`ssx-reflex-coupled` and its prerequisites. SHA256:
`9d9749fb5d05c17b1749560cbf3b4a1c0571da3ebe178117861d2b98b066c227`.
Its seven SDK files pass forward/reverse/residual checks using a disposable index.
The matching game hook, launcher and trace-analysis changes are in this repository.
Unrelated SDK changes and its real index are preserved.

SDK, probe and game builds pass, as do 24 SSX SDK tests, two game test targets,
and 13 Python timing/driver-analysis tests. Tests cover extra renders without
physics, snapshot ownership, stale/missing controller reads, sample-slot mismatch
and the earlier token/phase invariants.

Live evidence: `out/reflex-timing/timing-256092863075500.csv` and adjacent JSON,
joined to `out/build/win-amd64-streamline/logs/ssx_032*.log`:

| Measurement | Result |
| --- | ---: |
| Capture duration | 4.998 seconds |
| Distinct successful DLAA presentations | 333 (66.6 FPS) |
| Complete frame-input→DLAA→Present timelines | 332, with two capture-edge partial timelines |
| Physics jobs consuming verified snapshots | 150 |
| Captured physics input calls without a frame binding | 0 |
| Matching ordered NVIDIA driver reports | 333 |
| Identity/order errors, aborted frames, repeated presentations | 0 |
| Unknown sample generations / slot mismatches | 0 / 0 |

Both interpolation channels select matching worker-generation pairs in all 330
fully captured paired selections. Eight selections reference generations written
before the capture began. The screenshot shows an unpaused race, rider, terrain
and HUD at 69 FPS. The user reported normal controls and picture in the initial
uncoupled run, then entered the corrected build for this final trace.
The initial trace exposed an incorrect flag-0 assumption in the snapshot hook;
it did **not** pass input delivery validation. The flag-1 fix and native
consumption checks were built and staged before the final successful trace.

Whole-session log review also finds five `physics_handoff` cancellations at
10:00:45, during the transition from missing scene inputs to race rendering.
They precede the successful measured race trace and recovered through the
fallback path. The precise missing handoff component was not captured; loading
transitions are not yet validated as a continuous Reflex timeline. The zero-abort
result in the table applies to the five-second race capture, not the entire session.

The separate GPU regression `out/reflex-frame-driven-regression-02` passes:
111 rendered / 220 presented 4K frames, 40 / 79 resized frames, 216 completed
timelines, 132 matched driver reports, zero D3D12 validation errors and clean
process exit. The first restricted-desktop attempt timed out without a focused
test window; the successful retry ran on the normal desktop. These are analytic
FG results; SSX gameplay FG remains unfinished.

Remaining checks include broader transitions, pause/resume and latency pings in
this new scheduling mode, the NVIDIA verification overlay and physical latency
measurement. Full-rate camera response, FG color/HUD/presentation integration,
and native HDR remain separate work. The earlier checkpoints below retain their
historical results and limitations.

## Historical coupled gameplay Reflex — 2026-10-05

**Real Reflex sleep and the complete input/simulation/render/Present timeline
now run in an opt-in 30 Hz SSX path.** Player-controlled races and a run after
pause/resume pass the ownership and driver-report checks below. Gameplay FG
remains off. This is an experimental scheduling checkpoint, not a finished
high-refresh integration or a measured input-to-photon improvement.

Run `scripts/play_reflex_sync.bat` for 3× DLAA with the original `out/test-user`
profile. `launch_ssx.ps1 -ReflexSyncMode on -ReflexCoupledPhysics` selects it explicitly; `off` and
`boost` retain the same coupled scheduling for comparison. `disabled` is the
default, including the ordinary DLAA launcher, and restores faster interpolated
rendering. The coupled mode intentionally renders one frame per actual 30 Hz
simulation update, preserving the physics rate. Menu/alternate paths keep their
existing scheduling; the full Reflex claim applies to the instrumented race path.

### Code

`rexglue-sdk-ssx-reflex-coupled.patch` applies after `ssx-frame-tokens` and its
existing prerequisite chain. SHA256:
`ac461305433c72190e6fa084a2cef3b82356f9d8ac2abcd4da3ed529ca9aecbf`.
Forward/reverse/residual checks use a separate index and preserve unrelated SDK
changes. The game hooks, launcher and analysis scripts accompany the SDK patch.

- A scheduler coordinates the normal render job and the actual 30 Hz worker
  outside their guest locks. NVIDIA sleep precedes the worker body and its real
  `8317DDB0` controller read. Simulation markers bracket the original worker.
- Both published sample channels retain their exact slot, generation and time.
  The renderer calls SSX's original selection routine at that published time and
  verifies the selected generation. It does not interpolate an older worker pair.
- The worker's NVIDIA token is bound to that render, passed through the ordered
  GPU marker into actual DLAA, and retained with the image through Present.
  Render markers cover D3D12 submission; Present markers wrap the actual call.
  Cached repeated paints do not submit a second full timeline.
- Missing input/samples, timeouts and detach cancel incomplete frames without
  inventing successful markers. Waits have a 500 ms recovery limit. Streamline
  calls are serialized; scheduler callbacks obey scheduler→API lock order.
- Windows PCL messages and F13 queue latency pings for actual worker input.
  F13 is withheld from game bindings. Mouse-down queues the flash marker.
  Physical Reflex Analyzer measurements and visible flash verification remain
  untested.

### Validation

SDK/runtime/plugin, probe and game built successfully. All 23 SSX SDK tests,
both game test targets and ten Python timing/driver-analysis tests pass. Tests reject
old sample owners, wrong input/token IDs, extra interpolation and phase errors.
The NVIDIA GPU regression (`out/reflex-coupled-fg-regression-01`) passed with
111 rendered / 220 presented 4K frames, 40 / 79 resized frames, 216 completed
timelines and 132 matched driver reports. D3D12 validation reported zero errors
and the process exited cleanly; that remains an analytic FG test.

The user confirmed player control for the live SSX session, using the original
save, native 3360×1752 scene DLAA and final 3840×2160 output:

| Local five-second trace | Complete input→samples→DLAA→Present chains | Matching ordered NVIDIA reports | Errors / aborted frames |
| --- | ---: | ---: | ---: |
| `timing-254328293502900.csv` | 149 (two capture-edge partial chains) | 150 | 0 / 0 |
| `timing-254435985705900.csv` | 150 | 150 | 0 / 0 |
| `timing-254599643947900.csv` (after reported resume) | 150 | 150 | 0 / 0 |

Traces and adjacent JSON reports are local under `out/reflex-timing`; driver
records are in `out/build/win-amd64-streamline/logs/ssx_030*.log`. Each race
sample contains 150 worker input reads, 300 selected published samples, 150
DLAA evaluations and 150 actual Present calls, with no repeated paints. Driver
reports match the same completed NVIDIA token IDs. The report checker preserves
timestamps and permits at most the existing 2 µs driver quantization tolerance.
CPU intervals and driver GPU-end intervals exclude physical input and scanout.
`timing-254645415722200.csv` also records one injected F13 ping consumed at worker
input, with 149 complete chains, two capture-edge partial chains and zero errors
or aborted frames. No failed PCL calls were logged. The actual NVIDIA verification
overlay and a hardware latency analyzer were not exercised.

Create `out/reflex-timing/capture.request` during gameplay, then run:

```powershell
python tools/streamline_probe/analyze_frame_timing.py out/reflex-timing/timing-<id>.csv
python tools/streamline_probe/analyze_reflex_reports.py out/reflex-timing/timing-<id>.csv out/build/win-amd64-streamline/logs/ssx_<session>.log
```

The remaining work is full-rate input/camera updates for faster real renders,
broader transitions and race coverage, and FG's HUD-free final color/presentation
integration. Native HDR remains separate and unfinished. The earlier checkpoints
below describe how this integration was established; their disabled-status
statements refer to those earlier builds.

## Original timing instrumentation — 2026-10-04

## Implemented

- Streamline 2.14.1 Reflex Off, On and On + Boost; sleep before the analytic
  simulation; ordered PCL simulation, render submission and actual Present
  markers. DLAA, FG and these markers share one token in the GPU test.
- Driver latency reports exposed through `Streamline::GetReflexState`. Missing
  data remains unavailable rather than being reported as zero latency.
- SSX hooks around independent input polling, worker dispatch/job and render
  dispatch/job, with a five-second bounded capture (maximum 32,768 records).
- A versioned diagnostic ID travels inside the actual `VdSwap` GPU packet into
  the D3D12 command processor, then through the corresponding presenter mailbox.
  Repainting the same guest image retains its ID. Normal four-word swap packets
  keep their existing behavior. Instrumentation emits no gameplay PCL markers.
- Reconstruction CPU timing joins to the command-processor frame ID. This can
  reveal reconstruction occurring before CPU `VdSwap`, when a swap-only token
  assignment would already be too late for DLAA.

## Verified

- SDK, GPU plugin, probe and game build succeeded; 19 SSX SDK unit tests and
  the scene-hook executable passed.
- `out/reflex-validation-03/frame-generation`: 216 completed analytic frame
  tokens, 132 complete matching driver reports; 4K FG 111 rendered / 221 presented,
  resize 40 / 79, zero D3D12 validation errors, clean process exit.
- All three Reflex modes were exercised with FG unloaded. Disabling Reflex
  while FG is active is rejected. The test covers FG suspension/resume and resize.
- Driver 617.14 occasionally returns adjacent timestamps reversed by one
  microsecond. The test permits at most two microseconds; raw timestamps are
  preserved in `reflex-reports.csv`. This is not a latency-reduction benchmark.
- Initial SSX startup trace: 8,951 events and 515 complete guest→CP→present ID
  chains. It recorded 1,008 repeated present calls for already displayed images.
  It did not contain gameplay reconstruction or the normal race job callbacks;
  it is not evidence of race timing or simulation ownership.
- User-confirmed unpaused race, `out/reflex-timing/timing-209850031922200.csv`:
  6,869 events over five seconds, 150 worker jobs, 355 render jobs and 355 DLAA
  reconstructions; 533 successful Present calls, including 209 repeated paints.
  There are 321 complete guest→CP→present identities, with no missing render
  scope on captured guest swaps. All five execution domains are separate threads.
  The captured workload therefore has approximately 30 Hz worker updates and
  71 rendered frames/s. These are diagnostic observations, not a benchmark.
  All 353 mapped reconstructions in this capture began after their CPU swap
  emission; this does not establish that a future frame cannot run ahead of it.

## Small local test

Run `scripts/play_reflex_timing.bat`. It uses the same 3× DLAA configuration and
`out/test-user` save profile. Enter a normal race and leave the pause menu closed.
Create an empty `out/reflex-timing/capture.request` file. After about five seconds,
the input polling thread writes `timing-<timestamp>.csv` in that directory.
No game assets or image capture are included.

Analyze it with:

```powershell
python tools/streamline_probe/analyze_frame_timing.py out/reflex-timing/timing-<timestamp>.csv
python tests/test_frame_timing_analysis.py
```

The adjacent JSON distinguishes repeated paints and incomplete capture edges.
`Present` return is not monitor scanout. CPU durations include diagnostic overhead.

## Sample lineage and ordered GPU marker checkpoint

`rexglue-sdk-ssx-reflex-lineage.patch` applies after the FG/Reflex patch. SHA256:
`33d005fe8080f67f5ba63ab91b5c7456ccdaf096c4dc34e8483f8e5aee972b58`.
Forward, reverse and residual checks pass using a disposable index.

The game now assigns generations to writer-owned slots in `829E1688`, announces
publication before `829E1AE8` releases them, and records the pinned previous/next
selections and interpolation factor after `829E17A0`. A bounded 128-slot table
matches channel, slot and timestamp; slot reuse always replaces the generation,
including when the timestamp repeats. Unknown or overflowing entries produce
zero identity. Publication begin/end are separate because rendering can select a
sample before the publication wrapper has returned. Worker-side controller calls
through `8317DDB0` now carry the actual worker TLS ID.

During a capture, the normal render job inserts a versioned NOP after acquiring
the renderer/device locks (`82FB55E8`, return address `8237DCCC`). It resolves
the immediate device from that renderer, uses SSX's own reservation helper
`8232A580`, and preserves the calling PPC context and stack scratch. It rejects
unrecognized/deferred contexts and reservation failures. The ordered packet
carries the CPU render ID before rendering commands; DLAA and the corresponding
CP swap record this same ID. Swap and state restore clear the ID. Two markers
without an intervening swap invalidate it until the next swap.

This is diagnostic plumbing, not a live NVIDIA frame token or Reflex sleep.
It is intentionally restricted to the normal race render path until runtime
coverage is measured. Ordinary NOPs and existing swap packet formats are unchanged.

Validation: 21 SSX SDK tests pass; both game hook tests pass, including five
synthetic device cases for packet placement, exact count/cursor advancement,
register preservation, reservation failure, deferred context, wrong call site,
and capture-disabled fallback. Two Python analyzer tests cover mixed worker
generations, invalid slot associations, repeated paints and ordered GPU IDs.
SDK and full game builds succeeded; both game tests also pass against the staged
runtime. The staged runtime and GPU plugin hashes match the installed SDK. SSX
was relaunched at the world map with the original local save and 3× DLAA.
A malformed temporary launch TOML
initially produced a missing-game-path error; it was corrected and parsed before
relaunch. The original on-disk configuration was restored after successful boot.

The analyzer reports `ordered_gpu_marker_to_reconstruction_to_swap_chains`,
marker/swap mismatches, missing markers and `sample_lineage` statistics.
Generations written before the capture are counted separately from unknown IDs.
It still does not label a simulation or input-to-photon measurement verified.

### Live race result

The user confirmed an unpaused race for
`out/reflex-timing/timing-211133859485300.csv` (11,145 events, five seconds):

- 352 normal render jobs, 150 worker jobs and 150 worker-side input calls; all
  input calls were linked to their actual worker scope.
- 350 complete ordered GPU marker→DLAA→swap chains, zero marker/swap
  mismatches, zero duplicate-marker conflicts and zero rejected CPU markers.
  One reconstruction lacked a marker at the beginning of capture, before the
  first GPU marker arrived. None lacked a marker after that first arrival.
- 1,408 selected samples, all with known generations and zero slot mismatches.
  1,382 link to writes inside the capture; the other 26 were written before it.
- Both channels selected two samples on every captured render. All 344 renders
  whose full two-channel writes were inside the capture selected matching worker
  pairs across the channels. Interpolation factors ranged from 0.009986 to 0.999994.
- 346 complete guest→CP→present identities; 528 successful Present calls include
  179 repeated paints and three boundary calls without a captured swap ID.
- DLAA logs report successful single-sample 3360×1752 scene reconstruction at 3×.
  The final output remains 3840×2160. No gameplay FG or NVIDIA timing markers
  were enabled. The process subsequently logged a normal window-close shutdown.

This verifies sample associations and ordered rendering identities for this
race. It also establishes that a single worker token cannot simply be copied to
every rendered frame: a render combines two worker generations, and the same
pair can feed several renders. NVIDIA frame-token ownership and pacing must
respect that architecture. This capture does not establish latency reduction or
coverage of alternate render callbacks, loading, camera cuts, or other tracks.

## Remaining integration work

SSX has an input polling loop in `82366420`→`82366B88` on a separate thread,
with a roughly 5 ms wait. This is the loop covered by the input events. The
worker also reaches an additional controller-state path through `82BD6CF0`→
`8317DDB0`; the current input trace is not complete input-to-photon measurement.
Its simulation worker and render scheduler are separate. Observing the most
recently completed worker ID does **not** prove the
renderer consumed that worker's state. The trace deliberately does not turn that
observation into a Reflex simulation identity.

Source inspection located a concrete publication/selection mechanism to follow:
the worker calls `829E18E0` to acquire a writable sample and `829E1AE8` to publish
it. The latter stores a double timestamp in the selected sample and changes its
state to 2. The renderer calls `829E17A0`/`829E1580` to select samples around its
render time and calculate an interpolation factor. The channel stores selected
indices at +48/+52, a write index at +72, count at +76 and sample-pointer array
at +88. `829E1A60` releases the render selection. Both worker and renderer use
two channels. These are source-derived roles, not original symbol names.
The sample-generation and GPU-marker boundaries are now verified in the normal
race path above. Alternate callbacks still require coverage. The NVIDIA token
transport checkpoint below carries that verified render identity through SR
and presentation, but has no simulation markers or sleep.

Identify the input/update boundary before allocating the full Reflex timeline.
Route PCL pings through the corresponding input-consumption frame and validate
driver reports in gameplay before enabling Reflex pacing. Streamline API calls
are now serialized internally; callers retain responsibility for submission
order and GPU resource lifetimes.

References: [NVIDIA Reflex guide](https://github.com/NVIDIA-RTX/Streamline/blob/v2.14.1/docs/ProgrammingGuideReflex.md),
[PCL guide](https://github.com/NVIDIA-RTX/Streamline/blob/v2.14.1/docs/ProgrammingGuidePCL.md),
[FG guide](https://github.com/NVIDIA-RTX/Streamline/blob/v2.14.1/docs/ProgrammingGuideDLSS_G.md).

## NVIDIA render-token transport checkpoint — 2026-10-05

`rexglue-sdk-ssx-frame-tokens.patch` applies after `ssx-reflex-lineage` in the
existing patch chain. SHA256:
`b249daac79bf2d800996a6a527ebf6024e8f1f1d5a90dcca6e28caf656cb8279`.
Forward/reverse/residual checks pass using a separate Git index. Unrelated SDK
changes and the working fallback are preserved.

The opt-in `ssx_frame_token_handoff` flag keeps the ordered render marker active
outside captures. The D3D12 command processor allocates a real Streamline token
at that marker, before DLAA. The same token accompanies frozen inputs, SR
evaluation, the published image, and its first actual swap-chain Present.
Three retained slots bound pending tokens. Replaced tokens are retired, missing
or failed SR tokens cannot claim a match, and repeated paints invoke normal
Present without claiming another token presentation. Calls into Streamline are
serialized, including the actual Present callback. Full Reflex timelines and
render-only tokens cannot be mixed while active.

This adapter does **not** emit PCL simulation/render/Present markers or call
Reflex sleep. Holding a real token through Present proves application transport;
it does not establish a driver Reflex measurement or enable gameplay FG.
`BeginFrame` remains the separate full-timeline API exercised by the analytic
FG test. Render-only tokens are rejected by its marker and FG-tagging APIs.

### Validation

- SDK and full game build successfully. All 21 SSX SDK tests and both game
  test targets pass. The hook test now covers six device/capture cases, including
  continuous markers with capture off. Five Python analyzer tests cover token,
  image, result and ordering mismatches, repeats, retirement and capture edges.
- `test_streamline.ps1 -RenderTokensOnly` evaluates and presents 80 real DLAA
  frames (64 at 4K, 16 after resizing to 2560×1440), with cross-thread token
  allocation and zero D3D12 validation errors. The 80 duplicate callback checks
  and missing/retired/occluded-result checks are CPU state tests, not extra
  visually presented frames. Logs: `out/frame-token-validation-01`.
- The existing full FG regression still passes: 111 rendered / 221 presented
  4K frames, 40 rendered / 79 presented resized frames, 216 completed timelines,
  132 matched ordered driver reports, zero D3D12 validation errors, clean exit.
  Logs: `out/frame-token-fg-regression-01`. These analytic tests are not an
  SSX gameplay FG result or a performance benchmark.
- The staged runtime and GPU plugin match the installed SDK byte-for-byte.
  SSX launched with the original local profile and 3× DLAA. Its original TOML
  was restored byte-for-byte after boot; game assets and captures stay local.

The user confirmed an unpaused race for
`out/reflex-timing/timing-252650081080700.csv` (13,131 events / five seconds):

- 383 tokens created and 383 DLAA evaluations. There were 376 first token
  presentations, 199 repeated paints and six explicit token retirements.
- 374 complete GPU-marker→DLAA→image→Present chains; two first presentations
  had incomplete capture-edge chains. No token/image/order/result mismatches,
  duplicate terminal claims, marker conflicts or unmarked reconstructions.
- 575 successful actual Present calls. All 377 complete two-channel sample
  selections use matching worker pairs; 1,532 selections have known generations
  and zero slot mismatches. 150 worker-side input calls match their worker scope.
- Logs report successful single-sample DLAA at the native 3× scene extent
  (3360×1752), with final 3840×2160 output. Gameplay Reflex and FG remained off.

Run `scripts/play_frame_tokens.bat` to reproduce the configuration, and use the
same five-second capture request described above. The analyzer's
`nvidia_token_transport` object reports the chain and any errors. Event totals
can differ at the two capture boundaries; repeat counts are not generated frames.

### Input/update boundary investigation

The live vtable for the pre-selection callback (`837DF000`) resolves to
`823FB648`, a movie-playback path. The render-prepare callback (`838F2298`)
resolves to `82FB69D8`, which prepares viewport/time shader constants. Neither
has been established as per-render camera/input consumption. No latency marker
was added based solely on those callback names.

NVIDIA's Reflex team explains that its simulation interval can represent a
per-frame update rather than the physics tick, and that input sampling is the
key boundary in engines with separate fixed-rate physics. See the
[official NVIDIA discussion](https://forums.developer.nvidia.com/t/implementing-nvidia-reflex-latency-markers-into-a-custom-engine/266092).
SSX's 30 Hz worker and interpolated renders require tracing that ownership;
neither reusing one worker token across several renders nor fabricating the
simulation interval immediately before Present satisfies the integration.
