# DLSS / Frame Generation: SDK connection checkpoint

2026-10-04. DLSS SR and Frame Generation now take priority. The 720p scene
experiment stays off. This checkpoint implements the NVIDIA SDK connection in
ReXGlue's actual D3D12 provider and tests it on the RTX 5090. **It does not yet
reconstruct SSX frames with DLSS or generate extra frames.**

## Code delivered

- `patches/rexglue-sdk-streamline.patch`: optional Windows x64 backend, default
  off, for the pinned SSX-patched ReXGlue v0.10.0 tree. The public header hides
  NVIDIA types, and builds without NVIDIA headers/binaries remain supported.
- Signed interposer loading from an explicit directory, manual device/factory
  upgrades before command queue and swap-chain creation, native-interface
  rollback on attachment failure, and correct shutdown/module lifetimes.
  Existing presenter swap chains use the upgraded factory. Its proxy routes
  creation, GetBuffer, GetCurrentBackBufferIndex, Present and ResizeBuffers
  through NVIDIA's required hooks.
- Real adapter-LUID support queries for SR, FG, Reflex and PCL; real DLSS optimal
  settings queries. FG and Reflex options explicitly initialize to off. No
  per-frame constants, fake depth/motion, or synthetic simulation markers are
  submitted as if they represented SSX.
- `scripts/prepare_streamline.ps1`: verify the official release archive hash,
  extract headers, and stage only the required production DLLs and their
  license files. It writes a DLL hash manifest. All staged files live under
  ignored `out/`; nothing proprietary is added to Git.
- `tools/streamline_probe`: builds against the installed patched runtime and
  calls the same D3D12 provider that SSX uses. It creates a hidden BGRA8 flip
  swap chain, clears/presents 12 frames, resizes 4K -> 1440p -> 4K, checks D3D12
  validation messages, and destroys the provider normally.
- `win-amd64-streamline` SSX build preset keeps the development executable in
  a separate output directory. It needs the patched SDK prefix explicitly;
  the preset itself does not enable an unfinished rendering feature.

Two opt-in, initialization-only cvars exist in the patched SDK:

```toml
d3d12_streamline = false
d3d12_streamline_path = "D:/dev/SSX-RexGlue/out/streamline-runtime"
```

Setting the first to true enables only this connection checkpoint. It is not
a DLSS/FG quality switch. The normal launcher and scene-size experiment remain
unchanged by this checkpoint.

## Pinned dependency

[NVIDIA Streamline 2.14.1](https://github.com/NVIDIA-RTX/Streamline/releases/tag/v2.14.1),
source commit `2122257e0fce486f91b385aa63b9a09b0a34b363`.

Archive: `streamline-sdk-v2.14.1.zip`, 275,994,000 bytes.
SHA-256: `92c4d954631a1710da86ca3fa8d5034f2b9503838c95fc4ae977ae149319781b`.

Runtime files: `sl.interposer.dll`, `sl.common.dll`, `sl.dlss.dll`,
`sl.dlss_g.dll`, `sl.reflex.dll`, `sl.pcl.dll`, `nvngx_dlss.dll`,
`nvngx_dlssg.dll`. Do not point deployment at every DLL in the SDK: that also
lets NGX initialize unrelated Ray Reconstruction binaries. OTA/downloaded
plugin loading is disabled in the integration preferences.

The SDK uses a stable custom-engine development project GUID, not an invented
NVIDIA application ID. Shipping identity, NVIDIA onboarding and redistribution
requirements still need review before distributing a finished integration.

Streamline 2.14.1 has an invalid C++23 function alias in `sl_pcl.h`:
`using to_underlying = std::to_underlying;`. CMake corrects this to
`using std::to_underlying;` only in copied build headers. The downloaded SDK
and signed production binaries are untouched.

## Verification

SDK runtime and Xenos plugin build with Streamline enabled. The SDK unit suite
reports 209 passed, four existing BitStream skips, zero failures. The backend
also builds with Streamline compiled out.

The separate SSX Release executable built successfully at
`out/build/win-amd64-streamline/ssx.exe`; its existing generated-guest hook test
also passes. This executable has not been gameplay-tested with the NVIDIA
connection enabled. `out/build/win-amd64-release` retains the original baseline.

The host RTX 5090 probe reports `eOk` for SR, FG, Reflex and PCL, and Reflex
low-latency availability. NVIDIA reports a maximum of five generated frames
per real frame; this is a capability query, **not a tested frame multiplier**.

For 3840x2160 output, the actual optimal-settings results were:

| Mode | Recommended input |
|---|---|
| Quality | 2560x1440 |
| Balanced | 2227x1253 |
| Performance | 1920x1080 |
| Ultra Performance | 1280x720 |
| DLAA | 3840x2160 |

The following probe cases pass presentation, both resizes and shutdown, with
zero D3D12 validation errors:

1. Production SDK connected on the host, with SR/FG/Reflex disabled.
2. Runtime option disabled.
3. Missing interposer, falling back to standard D3D12.
4. Unsigned file in place of the interposer: rejected before loading, standard
   D3D12 fallback succeeds.
5. SDK compiled without Streamline, despite the runtime option requesting it.

The final enabled probe transcript is saved locally at
`out/build/streamline-probe/streamline-final.txt`. The other cases have matching
`streamline-disabled.txt`, `streamline-missing.txt`, `streamline-unsigned.txt`
and `streamline-compiled-out.txt` transcripts beside it.

The hidden window does not establish visible gameplay correctness, throughput,
latency, temporal image quality, or generated-frame presentation. NVIDIA emits
an advisory about synchronizing future FG state queries with the present thread
and a warning that the omitted optional backbuffer extent defaults to the full
buffer. No per-frame resources are tagged at this checkpoint.

Sandbox-only runs stalled in driver telemetry teardown. A native stack sample
showed `NVSDK_NGX_D3D12_Shutdown -> UninitializeTelemetry` waiting for the
telemetry worker in `WaitNamedPipeW`. The identical probe exits normally on
the host outside the sandbox. Do not modify Windows or NVIDIA telemetry
settings or bypass SDK shutdown to work around this test-environment issue.
The installed LLDB was also unusable because `liblldb.dll` was missing; this
was a debugger dependency issue, not an SSX/game DLL dependency.

## Reproduce

Use the existing Visual Studio/Clang developer environment. From this repository:

```powershell
./scripts/prepare_streamline.ps1
```

Apply the new patch in the already SSX-patched SDK checkout, then configure
using an absolute path to the extracted SDK:

```powershell
git apply D:/dev/SSX-RexGlue/patches/rexglue-sdk-streamline.patch
cmake --preset win-amd64 -DREXGLUE_ENABLE_STREAMLINE=ON -DREXGLUE_ENABLE_FIDELITYFX=OFF -DREXGLUE_STREAMLINE_DIR=D:/dev/SSX-RexGlue/out/vendor/streamline-2.14.1
cmake --build --preset win-amd64-release --target rexruntime rexgpu-xenos
cmake --install out/build/win-amd64 --config Release --prefix out/install/win-amd64-streamline
```

The SDK install may also require already-built normal SDK targets, including
codegen, as in the baseline build. Then, from this repository:

```powershell
$sdk = 'D:/dev/rexglue-sdk/out/install/win-amd64-streamline'
cmake -S tools/streamline_probe -B out/build/streamline-probe -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++ "-DCMAKE_PREFIX_PATH=$sdk"
cmake --build out/build/streamline-probe
Push-Location out/build/streamline-probe
./ssx_streamline_probe.exe ../../streamline-runtime --debug
./ssx_streamline_probe.exe --disabled --debug
./ssx_streamline_probe.exe ./missing-sdk --expect-fallback --debug
Pop-Location

cmake --preset win-amd64-streamline "-Drexglue_DIR=$sdk/lib/cmake/rexglue" "-DCMAKE_PREFIX_PATH=$sdk" -DREXSDK_DIR=
cmake --build --preset win-amd64-streamline --parallel 12
```

## Later checkpoints

The [DLSS evaluation checkpoint](ssx-dlss-evaluation.md) now implements the real
SR call path and validates synthetic GPU output. The input/motion/jitter work
below has advanced in the linked checkpoints. SSX gameplay insertion and FG
remain unfinished; this document records the original SDK connection stage.

## Next implementation boundary at this stage: SSX frame inputs

Update: [the SSX frame input checkpoint](ssx-frame-inputs.md) now implements
candidate collection, GPU snapshots and mailbox ownership. The table below
still describes the remaining semantic/temporal requirements; raw snapshots
alone do not fulfill them.

The later [camera motion checkpoint](ssx-camera-motion.md) implements camera-only
GPU vectors and history with synthetic and live checks. Animated-object motion,
full temporal validation, jitter and NVIDIA frame evaluation remain unfinished.

The existing [race capture](race-capture.md) supplies a concrete starting point.
Further generic 720p gameplay tests are not a prerequisite for this work.

| Input / operation | Established evidence | Still required |
|---|---|---|
| Color | Main scene resolve sampled by tone-map PS `A2CD8E82699B2C0A`; RGB is square-root encoded | Decode the signal for SR and preserve later particles/postprocessing; choose and implement the upscaling insertion boundary |
| Depth | Matching 1120x584 R32F resolved depth exists in the capture | Validate camera projection, range/reversal, alignment and per-frame extraction; ROV EDRAM itself is not a conventional host depth texture |
| Camera motion | Referenced VS `C44105DD14EFDB33` has position transforms | Separate view/projection from object transforms; maintain previous rendered camera and camera-cut detection |
| Animated-object motion | No validated dense motion buffer identified | Previous object/skinning transforms and per-pixel velocities; camera reprojection alone will not handle the rider |
| Jitter/history | No validated SSX jitter injection yet | Apply jitter to actual scene rasterization, supply matching unjittered constants; reset for cuts, loads and size changes |
| Exposure | Adapted log-luminance texture and tone-map exposure math identified | Decode/adapt units to the chosen DLSS color signal; use correct pre-exposure/history |
| HUD / FG | Late 1280x720 composition precedes final swap | Determine the exact HUD boundary; retain output-sized HUD-less color and UI coverage through presentation |
| Reflex / frame IDs | Guest present and render-cap hooks exist | Identify real simulation boundaries and link their IDs through guest submission to host presentation |

ReXGlue can paint the most recent guest output independently of a new guest
simulation/render frame. Therefore a token per UI paint, or labeling the guest
present wrapper as simulation start, would provide incorrect timing. SR belongs
on the scene's command stream; FG must consume retained resources associated
with the matching guest frame at actual presentation. Neither can safely be
implemented by passing the final color texture as every input to the existing
experimental FSR presenter.

No new capture is requested at this checkpoint. The next source work is the
SSX-specific temporal producer and resource ownership through the renderer's
guest-output mailbox. A later capture should validate those implemented inputs.
