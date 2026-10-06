# Experimental Windows graphics options, DLAA/FG and native HDR

**AI assistance disclosure:** I used **ChatGPT ASTRA throughout the development
of this contribution**, including source investigation, implementation, debugging,
launcher development and validation tooling. I directed the work and supplied
gameplay testing and visual feedback. The original SSX-ReXGlue, ReXGlue/Xenia and
NVIDIA components retain their existing authorship and licenses. AI assistance
does not imply verification of the untested settings listed below.

SSX previously exposed its resolution and runtime settings mainly through batch
files. This experimental source change adds a native Windows options dialog and
an ordered, hash-checked ReXGlue patch series for the SSX graphics integration.
HDR, DLAA and internal resolution are independently selectable; FG requires DLAA
and Reflex, while DLAA works with FG off. Native HDR preserves scene highlights,
original grading, bloom, later world effects and HUD blending, with peak brightness
up to 3,000 nits and an explicit native/fallback indicator.

The launcher exposes DLAA/DLSS model presets, experimental Quality/Balanced/
Performance, 2×–6× FG output, brightness calibration, display settings and original
runtime controls. The game checks the actual NVIDIA multiplier limit. A preserved
rendering fallback remains available. ReXGlue is pinned to v0.10.0's source revision
plus the included patches; NVIDIA integration is pinned to Streamline 2.14.1.

Validation: the SDK, game and launcher build; 69 relevant SDK unit tests, both
game hook tests and 72 launcher checks pass. Seven fixed-1,000-nit GPU composition
cases cover independent HDR decoding and 1×/2×/3× extents. The earlier 3× scale,
DLAA L, 1,000-nit HDR + 3× FG checkpoint and Tricky grading fix were exercised in
live races. These results do not certify every scene or the final option matrix.

**Deliberately untested for this submission:** new DLSS modes/presets, alternative
FG amounts and HDR peak brightness settings. Independent combinations still need
gameplay coverage. Balanced/Performance currently resize temporal inputs from an
integer raster scale, so conventional DLSS performance gains are not established.
Whole-game effect coverage, arbitrary aspect-ratio FG, dynamic MFG, physical
3,000-nit TV calibration and render-rate physics/input consumption are not claimed.

See `docs/experimental-submission.md` for the build recipe and exact limitations.
The source submission excludes game assets, generated game code, executable game
builds, personal saves/settings, captures and proprietary NVIDIA binaries.
