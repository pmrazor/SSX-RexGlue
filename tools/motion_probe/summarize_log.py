"""Summarize local SSX motion diagnostics without exporting game assets."""
import argparse
import collections
import json
from pathlib import Path
import re

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('logs', nargs='+', type=Path)
parser.add_argument('--output', required=True, type=Path)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
lines = sorted(set(line for path in args.logs
                   for line in path.read_text(errors='replace').splitlines()
                   if any(tag in line for tag in ('SSX_MOTION', 'SSX_INPUT_CAMERA',
                                                  'SSX_INPUT_VIEWPORT', 'SSX_INPUTS frame='))))
(args.output / 'ssx-motion-3x.log').write_text('\n'.join(lines) + '\n', encoding='utf-8')


def fields(line):
    return dict(re.findall(r'(\w+)=([^ ]+)', line))


gpu = [fields(line) for line in lines if 'SSX_MOTION_GPU ' in line]
motion = [fields(line) for line in lines if 'SSX_MOTION frame=' in line]
viewports = [fields(line) for line in lines if 'SSX_INPUT_VIEWPORT ' in line]
summary = {
    'sources': [path.name for path in args.logs],
    'gpu_readback_records': len(gpu),
    'total_sampled_vectors': sum(int(r['sampled']) for r in gpu),
    'nonfinite_vectors': sum(int(r['nonfinite']) for r in gpu),
    'records_with_motion_above_0_01_pixel': sum(int(r['moving']) > 0 for r in gpu),
    'records_with_stationary_valid_samples': sum(int(r['valid']) > 0 and int(r['moving']) == 0 for r in gpu),
    'maximum_sampled_vector_pixels': max((float(r['max_pixels']) for r in gpu), default=None),
    'last_gpu_record': gpu[-1] if gpu else None,
    'logged_dispatch_extents': sorted(set(r['size'] for r in motion if r['dispatched'] == 'true')),
    'logged_status_counts': dict(collections.Counter(r['reason'] for r in motion)),
    'valid_viewports': sorted(set(r['xy'] for r in viewports if r['valid'] == 'true')),
    'coverage': 'camera_only',
    'dlss_ready': False,
    'limitations': 'Sparse execution/finite-value diagnostics do not establish dense object motion or temporal image alignment.',
}
(args.output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
print(json.dumps(summary, indent=2))
