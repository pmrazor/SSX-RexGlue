"""Inspect a local SSX native-motion readback (numpy + Pillow required).

Outputs remain inside the ignored capture directory. Geometry and color contain
game-derived data. The CPU reference is a diagnostic, not an SSX coverage proof:
floating-point/raster edge differences and the prototype depth tolerance matter.
"""
import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw


def inspect(folder, projection='guest'):
    meta = json.loads((folder / 'metadata.json').read_text())
    w, h = meta['width'], meta['height']
    for f in meta['files']:
        assert (folder / f['name']).stat().st_size == f['rows'] * f['row_bytes'], f['name']
    motion = np.fromfile(folder / 'motion.bin', '<f4').reshape(h, w, 2)
    valid = np.fromfile(folder / 'validity.bin', 'u1').reshape(h, w)
    depth = np.fromfile(folder / 'depth.bin', '<f4').reshape(h, w)
    native = valid == 2
    magnitude = np.linalg.norm(motion.astype('f8'), axis=2)
    ref = np.full((h, w, 2), np.nan)
    silhouette = np.zeros((h, w), bool)
    front_depth = np.zeros((h, w), 'f8')
    interior = np.zeros((h, w), bool)
    current = (folder / 'current-geometry.bin').read_bytes()
    previous = (folder / 'previous-geometry.bin').read_bytes()
    rows = []
    poses = []
    key_fields = ['shader', 'index_address', 'vertex_count', 'index_count', 'vertex_endian', 'index_endian']
    def key(d):
        return tuple(d[k] for k in key_fields)
    def vertices(data, draw):
        return np.ndarray((draw['vertex_count'], 3), dtype='>f4', buffer=data,
                          offset=draw['vertex_offset'], strides=(32, 4)).astype('f8')
    def indices(data, draw):
        return np.frombuffer(data, '>u2', draw['index_count'], draw['index_offset']).reshape(-1, 3)
    def project(points, draw):
        if projection == 'double':
            return np.c_[points, np.ones(len(points))] @ np.array(draw['camera']).reshape(4, 4)
        # Xenos/ReXGlue rounds each MUL and ADD separately. NumPy's separate
        # float32 ufuncs prevent FMA. Raster interpolation below uses float64.
        p = points.astype('f4')
        matrix = np.array(draw['camera'], dtype='f4').reshape(4, 4)
        clip = p[:, 2:3] * matrix[2] + matrix[3]
        clip = p[:, 1:2] * matrix[1] + clip
        return (p[:, 0:1] * matrix[0] + clip).astype('f8')
    for draw in meta['current']['draws']:
        matches = [d for d in meta['previous']['draws'] if key(d) == key(draw)]
        if len(matches) != 1 or sum(key(d) == key(draw) for d in meta['current']['draws']) != 1:
            continue
        old = matches[0]
        a, b = vertices(current, draw), vertices(previous, old)
        poses.append((a, b))
        ia, ib = indices(current, draw), indices(previous, old)
        uv_a = np.ndarray((len(a), 8), 'u1', current, draw['vertex_offset'] + 24, (32, 1))
        uv_b = np.ndarray((len(b), 8), 'u1', previous, old['vertex_offset'] + 24, (32, 1))
        ca, cb = project(a, draw), project(b, old)
        viewport, old_viewport = np.array(draw['viewport']), np.array(old['viewport'])
        scale = np.array([meta['current']['scale_x'], meta['current']['scale_y']])
        old_scale = np.array([meta['previous']['scale_x'], meta['previous']['scale_y']])
        with np.errstate(divide='ignore', invalid='ignore'):
            screen = (ca[:, :2] / ca[:, 3:4] * viewport[:2] + viewport[2:]) * scale + np.array(meta['current'].get('jitter_pixels', [0, 0]))
        delta = b - a
        centroid_delta = delta.mean(axis=0)
        rows.append(dict(shader=draw['shader'], vertices=len(a), triangles=len(ia),
                         max_world_displacement=float(np.linalg.norm(delta, axis=1).max()),
                         deformation_about_translation=float(np.linalg.norm(delta - centroid_delta, axis=1).max())))
        for triangle, old_triangle in zip(ia, ib):
            if not np.array_equal(triangle, old_triangle) or triangle.max() >= len(a):
                continue
            if not np.array_equal(uv_a[triangle], uv_b[triangle]):
                continue
            if not (np.isfinite(ca[triangle]).all() and np.isfinite(cb[triangle]).all()):
                continue
            if np.any(ca[triangle, 3] <= 1e-5) or np.any(cb[triangle, 3] <= 1e-5):
                continue
            p = screen[triangle]
            lo = np.maximum(np.floor(p.min(axis=0)).astype(int), [0, 0])
            hi = np.minimum(np.ceil(p.max(axis=0)).astype(int), [w - 1, h - 1])
            if np.any(hi < lo):
                continue
            x, y = np.meshgrid(np.arange(lo[0], hi[0] + 1) + .5,
                               np.arange(lo[1], hi[1] + 1) + .5)
            den = (p[1, 1]-p[2, 1])*(p[0, 0]-p[2, 0]) + (p[2, 0]-p[1, 0])*(p[0, 1]-p[2, 1])
            if abs(den) < 1e-10:
                continue
            l0 = ((p[1, 1]-p[2, 1])*(x-p[2, 0]) + (p[2, 0]-p[1, 0])*(y-p[2, 1])) / den
            l1 = ((p[2, 1]-p[0, 1])*(x-p[2, 0]) + (p[0, 0]-p[2, 0])*(y-p[2, 1])) / den
            bary = np.stack([l0, l1, 1-l0-l1], axis=-1)
            inside = np.min(bary, axis=-1) >= 0
            z = 1 - bary @ (ca[triangle, 2] / ca[triangle, 3])
            inside &= (z >= 0) & (z <= 1)
            region = np.s_[lo[1]:hi[1]+1, lo[0]:hi[0]+1]
            silhouette[region] |= inside
            front_depth[region][inside] = np.maximum(front_depth[region][inside], z[inside])
            d = depth[region]
            gate = inside & (d > 0) & (d <= 1) & (abs(z - d) <= np.maximum(2e-6, d * 1e-4))
            weighted_previous = (bary / ca[triangle, 3]) @ cb[triangle]
            with np.errstate(divide='ignore', invalid='ignore'):
                previous_pixel = (weighted_previous[..., :2] / weighted_previous[..., 3:4] * old_viewport[:2] + old_viewport[2:]) * old_scale
            expected = previous_pixel - np.stack([x, y], axis=-1) + np.array(meta['current'].get('jitter_pixels', [0, 0]))
            ref[region][gate] = expected[gate]
            # Exclude pixels within one pixel of a triangle edge from numeric comparison.
            edges = np.linalg.norm(p[[1, 2, 0]] - p[[2, 0, 1]], axis=1)
            away = np.min(bary * abs(den) / np.maximum(edges, 1e-10), axis=-1) > 1
            interior[region][gate] = away[gate]
    comparable = native & interior & np.isfinite(ref).all(axis=2)
    errors = np.linalg.norm(ref[comparable] - motion[comparable], axis=1)
    depth_error = depth.astype('f8') - front_depth
    gaps = silhouette & ~native
    locations = np.argwhere(native)
    rigid_residual = []
    if poses:
        a = np.concatenate([p[0] for p in poses])
        b = np.concatenate([p[1] for p in poses])
        aa, bb = a - a.mean(axis=0), b - b.mean(axis=0)
        u, _, vt = np.linalg.svd(aa.T @ bb)
        sign = np.diag([1., 1., np.linalg.det(u @ vt)])
        residual = np.linalg.norm(aa @ (u @ sign @ vt) - bb, axis=1)
        rigid_residual = np.percentile(residual, [50, 95, 100]).tolist()
    bbox = [int(v) for v in [locations[:, 1].min(), locations[:, 0].min(), locations[:, 1].max()+1, locations[:, 0].max()+1]] if len(locations) else None
    stats = dict(frame=meta['current']['frame'], previous=meta['previous']['frame'], width=w, height=h,
                 projection=projection,
                 best_rigid_fit_residual_world=rigid_residual,
                 native_pixels=int(native.sum()), native_bbox=bbox,
                 nonfinite_motion=int((~np.isfinite(motion).all(axis=2)).sum()),
                 native_outside_projected_mesh=int((native & ~silhouette).sum()),
                 projected_mesh_pixels=int(silhouette.sum()),
                 gap_depth_error_percentiles=np.percentile(depth_error[gaps], [0, 5, 50, 95, 100]).tolist() if gaps.any() else [],
                 accepted_depth_error_percentiles=np.percentile(depth_error[native], [0, 5, 50, 95, 100]).tolist() if native.any() else [],
                 cpu_reference_pixels=int(np.isfinite(ref).all(axis=2).sum()),
                 compared_interior_pixels=int(comparable.sum()),
                 motion_error_percentiles=np.percentile(errors, [50, 95, 99, 100]).tolist() if len(errors) else [],
                 native_motion_percentiles=np.percentile(magnitude[native], [0, 50, 95, 100]).tolist() if native.any() else [],
                 draws=rows,
                 limitation='One captured frame; candidate mesh identity and prototype depth gate, not full dynamic coverage.')
    (folder / 'analysis.json').write_text(json.dumps(stats, indent=2))
    color_path = folder / 'color.bin'
    if color_path.exists() and meta['color_format'] == 10:
        color = np.fromfile(color_path, '<f2').astype('f4').reshape(h, w, 4)[..., :3]
        # Diagnostic visualization of encoded scene values, not HDR tone mapping.
        background = np.uint8(np.clip(np.nan_to_num(color), 0, 1) * 255)
    else:
        background = np.repeat(np.uint8(np.clip(depth * 255, 0, 255))[..., None], 3, axis=2)
    overlay = background.copy()
    overlay[native] = (overlay[native].astype('f4') * .35 + np.array([0, 255, 120]) * .65).astype('u1')
    Image.fromarray(background).save(folder / 'scene-encoded.png')
    Image.fromarray(overlay).save(folder / 'native-coverage.png')
    mask_image = np.zeros((h, w, 3), 'u1')
    mask_image[silhouette] = [110, 40, 40]
    mask_image[native] = [0, 255, 120]
    Image.fromarray(mask_image).save(folder / 'mesh-coverage.png')
    if bbox:
        box = (max(0, bbox[0]-40), max(0, bbox[1]-40), min(w, bbox[2]+40), min(h, bbox[3]+40))
        panels = [Image.fromarray(background).crop(box), Image.fromarray(overlay).crop(box), Image.fromarray(mask_image).crop(box)]
        panel_w, panel_h = panels[0].size
        contact = Image.new('RGB', (panel_w*3, panel_h+30), (20, 20, 20))
        draw = ImageDraw.Draw(contact)
        for i, (panel, title) in enumerate(zip(panels, ['Encoded scene', 'Native motion coverage', 'Mesh / accepted pixels'])):
            contact.paste(panel, (i*panel_w, 30)); draw.text((i*panel_w+8, 8), title)
        contact.save(folder / 'rider-comparison.png')
    print(json.dumps({k: v for k, v in stats.items() if k != 'draws'}, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--projection', choices=['guest', 'double'], default='guest')
    args = parser.parse_args()
    inspect(args.directory, args.projection)
