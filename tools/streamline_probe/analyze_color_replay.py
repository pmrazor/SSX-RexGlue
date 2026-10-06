"""Compare local pre-tone-map captures; never package game-derived pixels."""
import argparse
import json
from pathlib import Path

import numpy as np

parser = argparse.ArgumentParser()
parser.add_argument('original', type=Path)
parser.add_argument('reconstructed', type=Path)
parser.add_argument('report', type=Path)
args = parser.parse_args()
shape = (1752, 3360, 4)
before = np.fromfile(args.original, '<f2').reshape(shape).astype('f4')
after = np.fromfile(args.reconstructed, '<f2').reshape(shape).astype('f4')
assert np.isfinite(before).all() and np.isfinite(after).all()
a, b = before[:, :, :3] ** 2, after[:, :, :3] ** 2
weights = np.array([0.2126, 0.7152, 0.0722], dtype='f4')
ya, yb = a @ weights, b @ weights
lit = ya > 0.01
bright = ya >= np.quantile(ya, 0.9)
report = {
    'description': 'One captured color image replayed for 32 static DLAA/L frames; synthetic camera/depth, zero jitter. Not a motion or live exposure proof.',
    'width': 3360, 'height': 1752,
    'mean_linear_rgb_ratio': float(b.mean() / a.mean()),
    'mean_linear_luminance_ratio': float(yb.mean() / ya.mean()),
    'brightest_input_decile_luminance_ratio': float(yb[bright].mean() / ya[bright].mean()),
    'lit_pixel_luminance_ratio_percentiles_5_50_95_99': np.quantile(yb[lit] / ya[lit], [.05, .5, .95, .99]).tolist(),
    'original_linear_channel_peak': float(a.max()),
    'reconstructed_linear_channel_peak': float(b.max()),
    'original_linear_luminance_percentiles_50_90_99': np.quantile(ya, [.5, .9, .99]).tolist(),
    'reconstructed_linear_luminance_percentiles_50_90_99': np.quantile(yb, [.5, .9, .99]).tolist(),
    'alpha_bit_exact': bool(np.array_equal(before[:, :, 3], after[:, :, 3])),
}
args.report.write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
