# DLSS Quality in races — 2026-10-04

Experimental DLSS Quality with Preset L now runs in SSX gameplay. The user
confirmed that the corrected race scenery is clean after an earlier version
produced purple corruption. The final build also runs through a moving race
with retained reconstruction history and separately measured GPU costs.

This checkpoint does not enable Frame Generation or native HDR. Temporal image
quality, effects, additional tracks and all animated-object motion are not fully
validated. The existing release renderer and DLAA launcher remain available.

## Run

Use `scripts/play_quality.bat` in this checkout. It selects the Streamline build,
the original `out/test-user` save, Quality/L, jitter, camera and native rider/board
motion, scene color, fullscreen 4K presentation and GPU timing diagnostics.
D3D12 validation is explicitly disabled for gameplay. It is enabled by the
standalone GPU test runner. No LLDB is used.

For the monitor index used in this session:

```bat
scripts\play_quality.bat -Monitor 1
```

The monitor option remains configurable because enumeration differs between PCs.
Use `scripts/play_dlaa.bat` for the previous reconstruction mode or
`scripts/play_4k.bat` for the preserved release renderer. Saves and game assets
remain local and ignored. The temporary launch TOML was restored after the test;
the Quality launcher supplies its own explicit options on subsequent launches.

## Actual resolution path

| Stage | Tested race dimensions |
|---|---|
| Original guest scene | 1120x584 |
| Native scene raster, color, depth and motion | 2240x1168 (2x) |
| DLSS Quality/L reconstructed scene | 3360x1752 (3x) |
| Guest tone map and postprocessing | 3x each pass's original dimensions |
| Final guest composition, HUD and presentation | 3840x2160 |

These are actual distinct raster and reconstruction sizes. The 4K final frame
still includes SSX's original scene-to-output enlargement. This does not claim
a reconstructed 3840x2160 scene for the stock 1120x584 race viewport. The separate
2560x1440-to-3840x2160 synthetic GPU case remains tested; the optional game scene
size override remains off. No 720p gameplay test was required.

The signed NVIDIA DLSS 310.9.1.0 binary and explicit Preset L from the
[preceding checkpoint](ssx-quality-preset-l.md) remain installed. The live NGX
log confirms `(Quality) Using App hint Preset L`.

## Renderer changes

The renderer now owns separate 2x and 3x ROV render-target and shader-pipeline
caches. A guarded SSX scene boundary starts native 2x rasterization; the main
tone-map boundary restores 3x rendering after reconstruction. Raster viewport,
ROV addressing and jitter use the active raster scale. Texture sampling retains
the canonical 3x texture-storage scale. The low-scale shader cache is kept
separate from the existing on-disk cache.

To preserve the game's shared-memory resolve addressing, resolved textures
remain stored at 3x. Before a low-scale resolve, only its affected EDRAM tile
spans are replicated into 3x storage. Clear values are propagated back. Color
and depth extraction selects the original native 2x samples for DLSS. At the
tone-map boundary, the reconstructed color replaces the full 3x scene source;
the guest alpha channel is resampled separately. HUD rendering stays after
reconstruction.

The conversion operates on packed words, preserving 32/64-bit layouts and each
auxiliary 1x/2x/4x MSAA sample. The scene supplied to DLSS is single-sampled.
Some unrelated auxiliary passes still use MSAA; this is not a claim that every
game render target has MSAA removed. The earlier purple result involved the
auxiliary-pass transition and sample interpretation. The corrected conversion
preserves those layouts instead of falling back partway through a jittered
scene. Clear operations also update the retained tile-layout metadata.

Quality uses immutable bindful resource tables because the EDRAM buffer changes
between phases. Shader instances are resolved in the active pipeline cache, and
graphics state is restored after the color/depth compute extraction. Outstanding
resources and query slots retain their existing fence lifetimes.

Missing or ambiguous inputs skip reconstruction and restore 3x postprocessing;
hard prerequisite/evaluation failures disable further reconstruction and jitter.
Scenes that do not enter the recognized path render at the normal scale. A
Quality launch requires supported DLSS, 3x output and ROV; invalid setup reports
an initialization error. The independent release launcher remains the reliable
fallback rather than claiming every experimental failure is invisible.

## Verification

- SDK, probe and SSX Release builds passed. Installed and staged `rexruntime.dll`
  hashes match: `161D2997BFA7516DFED6086F4E648501AEE0420E6237F7E2839F4E8DCA2C4DE7`.
- All 14 SSX unit tests passed, including history, jitter, camera/depth, frame
  lifetime and boundary guards.
- New tests dispatch the production conversion shaders and compare every output
  word for packed 32/64-bit data with 1/2/4 samples. Native 2x-to-3x-to-2x data
  is bit-exact; untouched tiles retain their sentinels. R32 depth and FP16 scene
  extraction are checked separately.
- Real NVIDIA GPU evaluation passed 11 SR evaluations, 22 rejected-input checks
  and nine SSX decode/reconstruct/encode/copy frames. History resets and retained
  history, separately resampled alpha, and completed timestamp readback passed.
- The D3D12 debug layer reported zero errors in the probe. Disabled/missing-runtime
  fallback cases and clean shutdown passed. Windows PowerShell 5.1 now retains
  the child process handle before waiting so a successful process exposes its
  exit code reliably.
- The user confirmed clean race scenery in `ssx_020.log`. The final `ssx_021.log`
  run shows Quality applying at 2240x1168 -> 3360x1752, `scene_samples=1`, retained
  history and no reconstruction fallback during the recorded race sample.
  Inspection of the final moving race showed scenery, rider and gameplay HUD.
- Launcher dry-run confirms Quality on, DLAA off, original save, 3x output and
  D3D12 validation off. Earlier launchers remain available.

GPU evidence is in `out/race-quality-validation-final-ps5/`; the live timing
summary is `live-timings.json` there. Full local gameplay logs are under
`out/build/win-amd64-streamline/logs/`. These files are ignored; game captures
and assets are not included in the patch.

## Performance measurement and limits

The recorded final sample spans 20 two-second profile windows, from the first
successful ordinary-race reconstruction at 20:02:03 through 20:02:44 local time.
D3D12 validation is off, while GPU timestamps and diagnostics remain enabled.
The rider and camera move, so this is diagnostic evidence, not a repeatable A/B
benchmark against native rendering or DLAA.

| Measurement | Median | Observed range |
|---|---:|---:|
| Rendered FPS | 87.7 | 83.4–100.9 |
| EDRAM scale conversion per frame | 0.395 ms | 0.37–0.40 ms |
| Input preparation (32 sampled frames) | 0.067 ms | 0.055–0.260 ms |
| DLSS dispatch | 1.325 ms | 1.316–1.336 ms |
| Encode and copy | 0.071 ms | 0.071–0.073 ms |

The original `GPU busy` line covers the command processor's lists, excluding
the separate reconstruction list. It must not be read as complete GPU utilization.
The new reconstruction timestamps read completed slots without adding GPU waits.
Their `timed_frame` identifies the earlier frame actually measured.

The preceding validation-enabled race often ran around 55–70 FPS. Disabling
validation and warming shaders changes that result, but different camera paths
prevent attributing an exact gain. DLSS still adds about 1.3 ms, and the current
compatibility conversion adds work. This checkpoint does not promise a speedup.
Next performance work should compare the same scene with validation off and
profile CPU submission alongside the GPU stages before replacing the 3x resolve
storage with a more direct native-resolution path.

## Patch and rebuild

Apply `patches/rexglue-sdk-ssx-quality-races.patch` after
`rexglue-sdk-ssx-quality-preset-l.patch` and that checkpoint's prerequisites.
SDK base is `eed22e8a490fb8a60f8c392a391c3d5eb16c04a4` (v0.10.0 plus the SSX fixes).
The new patch contains 20 source/header/compiled-shader files, no game assets.
SHA-256: `B83477AAE2C863A3B24A06AD8A48F4C0940039586BCBEA246C27E13009614380`.
Forward application, reverse application and residual-diff checks pass. The
unrelated local libmspack adjustment remains outside the patch.

Rebuild/install the SDK and rebuild SSX against that install. Configure
`tools/streamline_probe` with `-DSSX_SDK_SOURCE_DIR=<patched-sdk-source>` so it
tests the exact production conversion bytecode, then run
`scripts/test_streamline.ps1` on the normal Windows host. If that source path is
omitted, conversion testing prints an explicit SKIP; the standalone test must
not be treated as proof of gameplay coverage.
