# Performance checkpoint — 2026-10-05

The first optimization removes unused diagnostic atomics from ordinary native
rider-motion draws. The source, SDK binaries and game build are updated. This
is a small measured GPU saving, not a large FPS improvement.

## Changes

`patches/rexglue-sdk-ssx-performance.patch` applies after
`rexglue-sdk-ssx-reflex-transitions.patch`. SHA256:
`5ecefbb5bb9aada95ec90751f108b6fdcb37e3d0c0c84bab19ab52daa320dad7`.
The patch was checked forward and backward against the preceding patch series,
using a disposable Git index. Unrelated SDK changes are excluded.

Previously the native-motion pixel shader updated four shared counters on
every accepted moving fragment, although their readback ran only every 120
frames. A second pixel-shader variant removes those counters entirely. The
normal `d3d12_ssx_native_statistics=sampled` mode uses the original instrumented
shader only when a readback is due, including the first draw. Counter clears
and resource transitions are also skipped on other frames. Geometry matching,
projection, jitter, depth rejection, motion output and history are unchanged.

The runtime setting under F4 → UI → D3D12 supports:

- `sampled`: normal behavior; counters once every 120 frames.
- `off`: no counters; useful for isolated GPU comparisons.
- `every_frame`: original counter workload for A/B measurements.

GPU profiling now separates `color_ms`, `camera_ms` and `native_ms` within
`inputs_ms`. Timings are read only after the existing submission fence completes;
profiling introduces no additional synchronous GPU wait.

`scripts/play_profile.bat` enables GPU timestamps with 3× DLAA, render-driven
Reflex On and the original local save. Ordinary launchers leave timestamps off
unless `-GpuProfile` or the applicable `-Diagnostics` option is passed. Swap and
presenter logging are explicit. Native/FG capture paths and FG input retention
are disabled unless requested, preventing stale local TOML capture requests
from creating unwanted readbacks. The baseline run inherited such a request and
wrote 125,053,804 bytes on race entry; this fix prevents that work on ordinary
launches. Its effect on startup hitch duration has not been measured.

## Measurements

Hardware: RTX 5090, driver 617.14. Existing 3× ROV rendering, single-sample race
scene, DLAA Preset L. The tested scene is **3360×1752**, with **3840×2160** final
output. Resolution, DLAA model and exposure policy were not reduced.

| Measurement | Original counters | Optimized ordinary frame | Saving |
|---|---:|---:|---:|
| Captured-frame replay, native pass median | 0.051072 ms | 0.010080 ms | 0.040992 ms |
| Same paused race, native pass median | 0.036 ms | 0.008 ms | 0.028 ms |
| Same paused race, all temporal inputs median | 0.131 ms | 0.100 ms | 0.031 ms |

Replay used local frame 5435, 10 matched draws and 47,584 native-motion pixels.
The game was closed. The probe alternated old/new order over eight rounds,
discarded eight warmup samples per mode per round, and retained 256 GPU timestamp
samples per mode. Uploads, camera reconstruction, CPU work, captures, DLAA and
presentation are outside the timed native pass. Debug validation was off for
the benchmark and on for correctness tests. Sampled mode still incurs the old
diagnostic cost once every 120 frames.

The live comparison used the same process, paused scene and camera, with the
settings overlay closed. Each mode had a 30-second measurement window after
five seconds of settling, yielding 16 asynchronous timing samples. Original
counters ran 11:05:17–11:05:47 local time; sampled mode ran 11:07:08–11:07:38.
Normal sampled mode was restored afterward. These sparse measurements do not
establish an improvement in whole-game FPS or tail frame times.

Local evidence is in ignored `out/performance-validation/`: benchmark CSV/JSON,
original and three optimized replay outputs, `live-comparison.json`, and the
two filtered live logs. The raw live log is `ssx_035*.log`.

## Validation

- 57 analytic native-motion GPU cases pass across all three counter modes,
  including deformation, camera motion, jitter, occlusion, topology mismatch,
  resets and precise guest projection at 3360×1752 and 3840×2160.
- Zero D3D12 validation errors in those cases and the captured-frame replays.
- Motion and validity bytes match the pre-change replay exactly in all modes.
  Motion SHA256: `2475178e948e595c372493d98ccaa715b321dec4d9adfb80ab44a5b13a3ac16e`.
  Validity SHA256: `f2da6ddd0971e6a730a5a2f8464eadbf226a4e1b952789749d53d6ebc82ae6b4`.
- SDK and game builds pass, as do 25 SSX SDK tests and both game CTest targets.
- The updated live race retains DLAA history and produces native motion.
  `timing-259803192791400.csv` verifies 327 complete frame-input/DLAA/present
  chains, 149 physics input snapshots, zero unbound inputs, zero aborts and no
  identity/order errors. All 328 completed trace tokens have ordered driver
  reports. This is integration evidence, not physical input-to-photon testing.

To reproduce the isolated comparison after building `tools/native_motion_probe`:

```powershell
python tools/native_motion_probe/prepare_replay.py <local-capture-directory>
ssx_native_motion_probe.exe --replay <local-capture-directory> <new-output-directory> off
ssx_native_motion_probe.exe --benchmark <local-capture-directory> <new-benchmark-directory>
python tools/native_motion_probe/analyze_benchmark.py <new-benchmark-directory>/native-benchmark.csv
```

Keep all captured game buffers local and out of version control. The probe
refuses to overwrite an existing capture directory.

## Remaining costs

The original renderer's draw category dominates: approximately 7.7–9.7 ms in
the observed scenes. DLAA costs approximately 2.5 ms, with some higher samples.
Color decoding and camera motion each cost approximately 0.05 ms; encoding
costs approximately 0.075 ms. The command-processor profile excludes the
separate DLAA submission, so its reported GPU-busy percentage is not total
device utilization. Draw-category time is aggregate instrumentation, not yet a
ranking of individual shaders.

The next substantial optimization needs attribution within the original
renderer: expensive guest shaders, ROV work, texture loads and submission
stalls. No rendering shortcut or reduced-quality model has been substituted.
Gameplay FG and native HDR remain unfinished; the existing Reflex transition
limitations are documented separately in `ssx-reflex-timing.md`.
