"""Compare a local SSX capture with the implemented jitter shader families.

Consumes existing RenderDoc draw/marker JSON and local shader disassembly.
Outputs hashes and counts only. This is a static audit of one captured frame,
not a proof of pixel alignment or coverage on other tracks.
"""
import argparse
import bisect
import collections
import json
import re
from pathlib import Path


def function_body(source, name):
    start = source.index(name + '(')
    begin = source.index('{', start)
    level = 1
    end = begin + 1
    while level:
        level += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[begin:end]


def audit(capture, sdk, output):
    source = (sdk / 'include/rex/ui/d3d12/ssx_jitter.h').read_text(encoding='utf-8')
    names = ['IsSsxJitterShader', 'SsxFixedScreenShader',
             'SsxInverseProjectionPixelShader', 'SsxInverseProjectionVertexShader']
    families = {name: set(re.findall(r'0x([0-9A-F]{16})ull', function_body(source, name)))
                for name in names}
    masks = {h: int(mask) for h, mask in re.findall(
        r'case 0x([0-9A-F]{16})ull:\s*return (\d+)',
        function_body(source, 'SsxClipInterpolatorMask'))}
    draws = json.loads((capture / 'draws.json').read_text())
    actions = json.loads((capture / 'actions.json').read_text())
    markers = sorted((a['eid'], a['name']) for a in actions if a['name'].startswith('SSX draw '))
    marker_ids = [m[0] for m in markers]
    tone = [d['eid'] for d in draws if d['name'] == 'VS E857B4AF9617DC29, PS A2CD8E82699B2C0A'
            and (d['viewport']['width'], d['viewport']['height']) == (1120, 584)]
    if len(tone) != 1:
        raise ValueError('Expected exactly one audited tone-map draw')
    counts = collections.Counter()
    unknown = []
    inverse = collections.Counter()
    for draw in draws:
        if draw['eid'] > tone[0] or not draw['name'].startswith('VS '):
            continue
        vp = draw['viewport']
        if (vp['x'], vp['y'], vp['width'], vp['height']) != (0, 0, 1120, 584):
            continue
        index = bisect.bisect_right(marker_ids, draw['eid']) - 1
        marker = markers[index][1] if index >= 0 else ''
        vs = draw['name'][3:19]
        if f'VS={vs}' not in marker:
            raise ValueError(f"Missing matching marker for draw {draw['eid']}")
        depth = re.search(r' Z=([0-9A-F]{8})', marker)
        if not depth:
            raise ValueError('Missing guest depth allocation')
        if depth[1] != '000102D0':
            counts['auxiliary_depth_excluded'] += 1
            continue
        if vs in families['IsSsxJitterShader']:
            counts['raster'] += 1
        elif vs in families['SsxFixedScreenShader']:
            counts['fixed_screen'] += 1
        else:
            counts['unknown'] += 1
            unknown.append(dict(eid=draw['eid'], vs=vs))
        ps = re.search(r'PS ([0-9A-F]{16})', draw['name'])
        if ps and ps[1] in families['SsxInverseProjectionPixelShader']:
            inverse['pixel_draws'] += 1
        if vs in families['SsxInverseProjectionVertexShader']:
            inverse['vertex_draws'] += 1
    # Independently check that each clip-varying mask names an export copying
    # the same XY source as oPos, after oPos has been written.
    shader_root = capture.parent.parent / 'shaders'
    verified = {}
    for vs, expected in masks.items():
        disasm = (shader_root / f'shader_{vs}.ucode.vert').read_text(encoding='utf-8')
        position = re.search(r'max oPos, (r\d+)(?:\.([xyzw]{4}))?,', disasm)
        if not position:
            raise ValueError(f'{vs}: missing audited position export')
        actual = 0
        for match in re.finditer(r'max o(\d+)(?:\.([xyzw_01]{4}))?, ' + position[1] +
                                 r'(?:\.([xyzw]{4}))?,', disasm[position.end():]):
            if (match[2] or 'xyzw')[:2] == 'xy' and (match[3] or 'xyzw')[:2] == (position[2] or 'xyzw')[:2]:
                actual |= 1 << int(match[1])
        if actual != expected:
            raise ValueError(f'{vs}: clip mask {actual} differs from implementation {expected}')
        verified[vs] = actual
    result = dict(tone_map_event=tone[0], counts=dict(counts), inverse=dict(inverse),
                  registered_families={k: len(v) for k, v in families.items()},
                  verified_clip_masks=verified, unknown=unknown,
                  limitation='One existing frame; source signatures and pass provenance only. Runtime state and pixel alignment require live validation.')
    output.write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps({k: v for k, v in result.items() if k != 'verified_clip_masks'}, indent=2))
    if unknown:
        raise SystemExit(1)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path, help='Directory containing draws.json and actions.json')
    parser.add_argument('sdk', type=Path, help='Modified ReXGlue SDK checkout')
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    audit(args.capture, args.sdk, args.output)
