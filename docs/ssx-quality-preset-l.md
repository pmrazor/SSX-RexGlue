# Preset L and Quality GPU checkpoint — 2026-10-04

Update: the later [Quality race checkpoint](ssx-quality-races.md) implements and
tests the gameplay connection described as pending below. This document records
the preceding DLL, model-selection and synthetic-GPU checkpoint.

The runtime is verified as current, Preset L is explicit, and Quality upscaling
passes the RTX 5090 GPU tests. **Quality is not yet a playable SSX mode.** The
gameplay adapter continues to use DLAA; its rebuilt runtime now requests Preset L.

## Runtime verification

NVIDIA's [latest Streamline release](https://github.com/NVIDIA-RTX/Streamline/releases/latest)
was 2.14.1, published September 8, 2026, when checked on October 4. Its production
`nvngx_dlss.dll` is **310.9.1.0**. The installed file has a valid NVIDIA Corporation
signature and exactly matches the current binary in
[NVIDIA's DLSS repository](https://github.com/NVIDIA/DLSS/blob/main/lib/Windows_x86_64/rel/nvngx_dlss.dll):

- Size: 58,956,912 bytes.
- Git blob: `875981984f56e71b4fde6605484ef767dadf9910`.
- SHA-256: `3975567B8943C53ACCE397F2B72380092F84F162D00B0D2C7D08A1025C563983`.

No DLL replacement was necessary. `scripts/verify_dlss_runtime.ps1` checks the
signed, pinned binary before tests. The test process also verifies the loaded
module path, so checking an unused DLL on disk cannot satisfy the test. A future
release requires a new official-source check and pin update; this script does
not claim that a fixed hash remains the latest indefinitely.

All DLSS mode preset fields now request `sl::DLSSPreset::ePresetL`, separately
from selecting Quality or DLAA mode. NVIDIA's NGX logs confirmed
`(Quality) Using App hint Preset L` and `(DLAA) Using App hint Preset L`.
The runner requires those confirmations. No NVIDIA App or driver settings were
changed. This uses the [official per-mode preset options](https://github.com/NVIDIA-RTX/Streamline/blob/v2.14.1/docs/ProgrammingGuideDLSS.md).

## Implemented and tested

`SsxReconstruction::Submit` now accepts an explicit Quality mode and a separately
sized destination. It checks input dimensions against NVIDIA's optimal-size
query, creates output-sized FP16 resources, drains/releases history on mode or
output changes, and copies the complete reconstructed result into that destination.
The gameplay caller's default remains DLAA.

The re-encoding shader resamples the original guest alpha independently of DLSS
RGB when upscaling. Equal-size DLAA retains the original texel exactly. Tests use
an alpha gradient and inspect every output pixel, including edges, to catch
out-of-range loads and truncated copies. MSAA and incorrect mode/dimension pairs
are rejected before submitting GPU work.

The final GPU run passed these SSX-format reconstruction cases, three frames each:

| Mode / model | Input | Output | History |
|---|---|---|---|
| DLAA / L | 3360x1752 | 3360x1752 | First frame resets; next two retain history |
| Quality / L | 2240x1168 | 3360x1752 | Same |
| Quality / L | 2560x1440 | 3840x2160 | Same |

Maximum encoded RGB error was 0.013672 against a 0.04 limit. Maximum alpha error
was 0.000304 against a 0.001 limit. Every output pixel was finite and overwritten.
The D3D12 debug layer reported **zero errors**. The existing moving-rectangle test
also passed all 11 evaluations and 22 rejection checks using Preset L. Disabled
and missing-runtime fallbacks exited normally. All 14 SSX unit tests passed.

These use synthetic, analytically known scenes through real NVIDIA GPU dispatch,
including the production SSX decoder, reversed-depth camera/motion path, encoder
and copy. They do not measure SSX race image quality, motion ghosting or game FPS.
No 720p gameplay test or new game capture was performed.

Final evidence: `out/quality-preset-l-validation-final/` and
`out/quality-preset-l-unit-results.txt`. SDK, probe and game builds passed. The
installed and staged `rexruntime.dll` hashes matched after rebuilding.

## Remaining gameplay integration

The current guest tone-map pass renders at the scene dimensions. The existing
adapter also hands reconstruction that same-sized source texture as its copy
destination. A larger Quality result cannot be copied there or shrunk back
without losing the intended output resolution. Merely changing the mode flag
would not implement Quality rendering.

Next work must separate the game's low-resolution scene resources from its
full-resolution tone-map/postprocessing destinations, propagate the reconstructed
texture through later resolves, and preserve HUD composition at output resolution.
The SDK's global integer scaler alone does not supply that separation. The new
Quality API rejects inconsistent dimensions and remains unavailable as a gameplay
launcher option until those consumers are connected and checked.

The working `scripts/play_dlaa.bat` now uses Preset L after this rebuild; the
preserved release build is unchanged. The original save profile remains in place.

## Rebuild and patch

Apply `patches/rexglue-sdk-ssx-quality-preset-l.patch` after
`rexglue-sdk-ssx-dlaa.patch` and its documented prerequisites. SHA-256:
`7D391D5E6BB5184D6447CE1045F97F8D1F070C93D86D28A35942F71D8FDD0589`.
Forward/reverse application and residual-diff checks pass. The incremental patch
contains source and compiled shader bytecode, no assets or generated game code.

Rebuild/install the SDK, rebuild `tools/streamline_probe` and SSX against that
install, then run `scripts/test_streamline.ps1` in a normal developer PowerShell.
The runner uses no LLDB and saves runtime provenance, effective-preset evidence,
GPU readback checks and fallback results locally.
