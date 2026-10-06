"""Prepare a local SSX FG color replay; optionally compare GPU output to its final image.

Residuals include real HUD/effects. This does not infer UI alpha or assert that
arbitrary game scenes are supported. Capture assets remain in ignored out/.
"""
import argparse
import json
import struct
from pathlib import Path
import numpy as np

p = argparse.ArgumentParser()
p.add_argument('capture', type=Path)
p.add_argument('--gpu-output', type=Path)
a = p.parse_args()
m = json.loads((a.capture / 'metadata.json').read_text())
assert (m['source_width'], m['source_height']) == (3360, 1752)
assert (m['final_width'], m['final_height']) == (3840, 2160)
assert m['source_swizzle'] == 0x60A and m['source_signs'] == 0
assert not m['gamma_pwl'] and len(m['gamma_ramp']) == 256
source = (a.capture / 'post-effects.bin').read_bytes()
assert len(source) == 3360 * 1752 * 4
packet = a.capture / 'fg-color-replay.bin'
packet.write_bytes(struct.pack('<258I', 3360, 1752, *m['gamma_ramp']) + source)
print(f'Local replay: {packet}')
if a.gpu_output:
    rgba = np.fromfile(a.gpu_output, dtype=np.uint8).reshape(2160, 3840, 4)
    packed = np.fromfile(a.capture / 'final-guest.bin', dtype='<u4').reshape(2160, 3840)
    final = np.stack([(packed >> shift) & 1023 for shift in (0, 10, 20)], axis=-1)
    # Match the presenter UNORM conversion, preserving the captured LUT.
    final = np.rint(final.astype(np.float32) * (255 / 1023)).astype(np.int16)
    error = np.max(np.abs(rgba[:, :, :3].astype(np.int16) - final), axis=-1)
    regions = {}
    for row in range(3):
        for col in range(3):
            e = error[row * 720:(row + 1) * 720, col * 1280:(col + 1) * 1280]
            regions[f'row{row}_col{col}'] = {
                'median_code_error': float(np.median(e)),
                'p90_code_error': float(np.quantile(e, .9)),
                'fraction_within_2_codes': float(np.mean(e <= 2))}
    result = dict(guest_frame=m['guest_frame'], regions=regions,
                  mean_rgb_ratio=float(rgba[:, :, :3].mean()/max(final.mean(), 1)),
                  scope='HUD-free GPU conversion versus captured guest presentation. '
                        'Differences include HUD and late effects; inspect scene regions. '
                        'No fitted exposure, UV offset, or inferred UI alpha.')
    (a.capture / 'fg-color-comparison.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
