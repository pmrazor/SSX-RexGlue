"""Audit camera-projection signatures in local SSX disassembly and draw records.

This reports candidates and draw counts, not proof of complete scene coverage.
It does not embed guest shader code or modify the renderer's allowlist.
"""
import argparse
import collections
import json
import re
from pathlib import Path


def audit(root, output):
    hashes = set()
    pattern = re.compile(
        r'\bmad (r\d+),[^\n]*\bc192[^\n]*\n'
        r'(?:[^\n]*\n){0,3}[^\n]*max oPos, \1, \1'
    )
    for path in (root / 'shaders').glob('*.ucode.vert'):
        text = path.read_text(encoding='utf-8')
        if re.search(r'\b(cjmp|jmp|loop|exec_pred)\b', text):
            continue
        if pattern.search(text) and all(f'c{reg}' in text for reg in (193, 194, 195)):
            hashes.add(path.name.split('.')[0][7:])
    counts = collections.Counter()
    draw_path = root / 'capture-analysis/race-f8/draws.json'
    for draw in json.loads(draw_path.read_text(encoding='utf-8')):
        viewport = draw['viewport']
        if (viewport['width'], viewport['height']) == (1120, 584) and draw['name'].startswith('VS '):
            counts[draw['name'].split(',')[0][3:]] += 1
    result = dict(
        hashes=sorted(hashes),
        matched_draws=sum(v for k, v in counts.items() if k in hashes),
        full_scene_draws=sum(counts.values()),
        counts=dict(counts),
        limitation='Signature audit only; viewport/depth gating and other passes require validation.',
    )
    output.write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(f"{len(hashes)} shader signatures; {result['matched_draws']}/{result['full_scene_draws']} captured draws")


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture_root', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    audit(args.capture_root, args.output)
