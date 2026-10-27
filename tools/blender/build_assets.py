"""Build Unreal-ready FBX files in Blender from the web game's exported geometry.

    blender -b --factory-startup -P tools/blender/build_assets.py -- [rig ...] [--blend]

Reads tools/out/build-plan/<rig>.json (from tools/web-export/make-rigs.mjs) and the geometry in tools/out/web-export,
builds one Blender object per planned asset (named like the asset, at the origin, vertices in its node's frame) and
writes tools/out/fbx/<rig>.fbx (one static mesh per object when imported with "combine meshes" off).
Level geometry (the city, the far landscape) is cut into tiles so Unreal can cull it.
Instanced meshes (trees, bushes) are exported once as base meshes plus a placement list <rig>_instances.json.
With --blend the Blender scene is also saved to blender/<rig>.blend.

Frames: web (x, y, z) -> Blender (x, -z, y) (metres, z up) -> Unreal (x, z, y) * 100 via the FBX import.
Texture repeat/offset of each material is baked into the UVs, so Unreal materials sample UV0 directly.
"""
import bpy
import json
import math
import os
import sys

import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
EXPORT = os.path.join(ROOT, 'tools', 'out', 'web-export')
PLAN = os.path.join(ROOT, 'tools', 'out', 'build-plan')
OUT = os.path.join(ROOT, 'tools', 'out', 'fbx')
BLEND = os.path.join(ROOT, 'blender')
ARGS = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
SAVE_BLEND = '--blend' in ARGS
RIGS = [a for a in ARGS if not a.startswith('--')]
TILES = {'world_near': 100.0, 'world_far': 300.0}   # tile size (m) for level geometry

MATERIALS = json.load(open(os.path.join(EXPORT, 'materials.json'), encoding='utf-8'))
MATS, TEXS = MATERIALS['materials'], MATERIALS['textures']
SKIP_TYPES = {'ShaderMaterial', 'PointsMaterial', 'SpriteMaterial', 'LineBasicMaterial'}


def to_blender(v):
    """web (x, y, z) -> Blender (x, -z, y), for N x 3 arrays."""
    return np.stack([v[:, 0], -v[:, 2], v[:, 1]], axis=1)


def array(bin_data, ref, dtype):
    return np.frombuffer(bin_data, dtype=dtype, count=ref['n'], offset=ref['off'])


def uv_transform(mat_id):
    m = MATS[mat_id] if 0 <= mat_id < len(MATS) else None
    if not m or m.get('map') is None:
        return (1.0, 1.0, 0.0, 0.0)
    t = TEXS[m['map']]
    return (t['repeat'][0], t['repeat'][1], t['offset'][0], t['offset'][1])


def gather(exp, bin_data, mesh_indices, slots):
    """Concatenate the planned meshes: corner arrays (T x 3 x ...) plus a material slot per triangle."""
    parts = []
    for mi in mesh_indices:
        m = exp['meshes'][mi]
        if m.get('instances'):
            continue
        mats = m['mat'] if isinstance(m['mat'], list) else [m['mat']]
        g = m['geo']
        n = g['count']
        pos = array(bin_data, g['pos'], np.float32).reshape(-1, 3).astype(np.float64)
        nor = array(bin_data, g['nor'], np.float32).reshape(-1, 3).astype(np.float64) if 'nor' in g else np.tile([0.0, 1.0, 0.0], (n, 1))
        uv = array(bin_data, g['uv'], np.float32).reshape(-1, 2).astype(np.float64) if 'uv' in g else np.zeros((n, 2))
        col = array(bin_data, g['col'], np.float32).reshape(-1, 3).astype(np.float64) if 'col' in g else None
        idx = array(bin_data, g['idx'], np.uint32).astype(np.int64) if 'idx' in g else np.arange(n, dtype=np.int64)
        tris = idx[: (len(idx) // 3) * 3].reshape(-1, 3)
        if g.get('flip'):
            tris = tris[:, [0, 2, 1]]
        tmat = np.zeros(len(tris), dtype=np.int64)   # index into mats
        groups = g.get('groups')
        if groups:
            keep = np.zeros(len(tris), dtype=bool)
            for gr in groups:
                s = gr['start'] // 3
                c = len(tris) - s if gr['count'] is None else gr['count'] // 3
                tmat[s:s + c] = gr['mat']
                keep[s:s + c] = True
            tris, tmat = tris[keep], tmat[keep]
        mat_ids = np.array([mats[k] if k < len(mats) else -1 for k in tmat], dtype=np.int64)
        ok = np.array([mid >= 0 and MATS[mid]['type'] not in SKIP_TYPES for mid in mat_ids], dtype=bool) if len(mat_ids) else np.zeros(0, bool)
        tris, mat_ids = tris[ok], mat_ids[ok]
        if not len(tris):
            continue
        cuv = uv[tris].copy()   # T x 3 x 2
        for mid in np.unique(mat_ids):
            ru, rv, ou, ov = uv_transform(int(mid))
            sel = mat_ids == mid
            cuv[sel, :, 0] = cuv[sel, :, 0] * ru + ou
            cuv[sel, :, 1] = cuv[sel, :, 1] * rv + ov
        slot = np.array([slots.index(int(mid)) if int(mid) in slots else 0 for mid in mat_ids], dtype=np.int64)
        parts.append({'pos': pos[tris], 'nor': nor[tris], 'uv': cuv, 'col': col[tris] if col is not None else None, 'slot': slot,
                      'vcol': any(MATS[int(mid)].get('vertexColors') for mid in np.unique(mat_ids))})
    if not parts:
        return None
    out = {k: np.concatenate([p[k] for p in parts]) for k in ('pos', 'nor', 'uv', 'slot')}
    if any(p['col'] is not None and p['vcol'] for p in parts):
        out['col'] = np.concatenate([p['col'] if (p['col'] is not None and p['vcol']) else np.ones((len(p['slot']), 3, 3)) for p in parts])
    return out


def material(key):
    m = bpy.data.materials.get(key)
    if not m:
        m = bpy.data.materials.new(key)
    return m


def build_object(name, data, slot_keys, sel=None):
    """Blender object from corner arrays (optionally a subset of triangles)."""
    if sel is not None:
        data = {k: v[sel] for k, v in data.items()}
    T = len(data['slot'])
    pos = to_blender(data['pos'].reshape(-1, 3))
    # Weld identical corners (same position, normal and uv) so the mesh is not a triangle soup.
    key = np.round(np.concatenate([pos, data['nor'].reshape(-1, 3), data['uv'].reshape(-1, 2)], axis=1) * 1e5).astype(np.int64)
    uniq, inverse = np.unique(key, axis=0, return_inverse=True)
    inverse = inverse.reshape(-1)
    verts = np.zeros((len(uniq), 3))
    verts[inverse] = pos
    me = bpy.data.meshes.new(name)
    me.vertices.add(len(verts))
    me.vertices.foreach_set('co', verts.astype(np.float32).ravel())
    me.loops.add(T * 3)
    me.loops.foreach_set('vertex_index', inverse.astype(np.int32))
    me.polygons.add(T)
    me.polygons.foreach_set('loop_start', np.arange(0, T * 3, 3, dtype=np.int32))
    me.polygons.foreach_set('material_index', data['slot'].astype(np.int32))
    me.polygons.foreach_set('use_smooth', np.ones(T, dtype=bool))
    uvl = me.uv_layers.new(name='UVMap')
    uvl.data.foreach_set('uv', data['uv'].reshape(-1, 2).astype(np.float32).ravel())
    if 'col' in data:
        ca = me.color_attributes.new('Col', 'FLOAT_COLOR', 'CORNER')
        rgba = np.concatenate([data['col'].reshape(-1, 3), np.ones((T * 3, 1))], axis=1)
        ca.data.foreach_set('color', rgba.astype(np.float32).ravel())
    me.update(calc_edges=True)
    nor = to_blender(data['nor'].reshape(-1, 3))
    lens = np.linalg.norm(nor, axis=1, keepdims=True)
    nor = np.where(lens > 1e-9, nor / np.maximum(lens, 1e-9), [0.0, 0.0, 1.0])
    me.normals_split_custom_set(nor.tolist())
    for k in slot_keys:
        me.materials.append(material(k))
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    return ob


def decompose(m16):
    """three.js column-major matrix -> (pos, quat xyzw, scale) in the web frame."""
    M = np.array(m16, dtype=np.float64).reshape(4, 4).T
    p = M[:3, 3]
    sx, sy, sz = (np.linalg.norm(M[:3, i]) for i in range(3))
    if np.linalg.det(M[:3, :3]) < 0:
        sx = -sx
    R = M[:3, :3] / np.array([sx, sy, sz])
    tr = R[0, 0] + R[1, 1] + R[2, 2]
    if tr > 0:
        s = 0.5 / math.sqrt(tr + 1.0); w = 0.25 / s; x = (R[2, 1] - R[1, 2]) * s; y = (R[0, 2] - R[2, 0]) * s; z = (R[1, 0] - R[0, 1]) * s
    elif R[0, 0] > R[1, 1] and R[0, 0] > R[2, 2]:
        s = 2.0 * math.sqrt(1.0 + R[0, 0] - R[1, 1] - R[2, 2]); w = (R[2, 1] - R[1, 2]) / s; x = 0.25 * s; y = (R[0, 1] + R[1, 0]) / s; z = (R[0, 2] + R[2, 0]) / s
    elif R[1, 1] > R[2, 2]:
        s = 2.0 * math.sqrt(1.0 + R[1, 1] - R[0, 0] - R[2, 2]); w = (R[0, 2] - R[2, 0]) / s; x = (R[0, 1] + R[1, 0]) / s; y = 0.25 * s; z = (R[1, 2] + R[2, 1]) / s
    else:
        s = 2.0 * math.sqrt(1.0 + R[2, 2] - R[0, 0] - R[1, 1]); w = (R[1, 0] - R[0, 1]) / s; x = (R[0, 2] + R[2, 0]) / s; y = (R[1, 2] + R[2, 1]) / s; z = 0.25 * s
    return p, (x, y, z, w), (sx, sy, sz)


def to_ue_transform(m16):
    """Web matrix -> Unreal (location cm, quaternion xyzw, scale), the same rule as HK::TransformToUE."""
    p, q, s = decompose(m16)
    return [p[0] * 100, p[2] * 100, p[1] * 100, -q[0], -q[2], -q[1], q[3], s[0], s[2], s[1]]


def build_rig(rig):
    plan = json.load(open(os.path.join(PLAN, rig + '.json'), encoding='utf-8'))
    src = plan['source']
    exp = json.load(open(os.path.join(EXPORT, src + '.json'), encoding='utf-8'))
    bin_data = open(os.path.join(EXPORT, src + '.bin'), 'rb').read()
    bpy.ops.wm.read_factory_settings(use_empty=True)
    made, instances = [], []
    for a in plan['assets']:
        data = gather(exp, bin_data, a['meshes'], a['slots'])
        tile = TILES.get(rig) if a.get('levelStatic') else None
        if data is not None and tile:
            cx = data['pos'][:, :, 0].mean(axis=1)
            cz = data['pos'][:, :, 2].mean(axis=1)
            ti, tj = np.floor(cx / tile).astype(int), np.floor(cz / tile).astype(int)
            # Only the mapped district is tiled; the hills, the horizon townscape and the fjord stay whole (tile 99_99).
            out = (np.abs(cx) > 700) | (np.abs(cz) > 700)
            ti[out], tj[out] = 49, 49
            for (i, j) in sorted(set(zip(ti.tolist(), tj.tolist()))):
                sel = (ti == i) & (tj == j)
                used = sorted(set(data['slot'][sel].tolist()))
                remap = {s: k for k, s in enumerate(used)}
                sub = {k: v[sel] for k, v in data.items()}
                sub['slot'] = np.array([remap[s] for s in sub['slot']])
                name = f"{a['asset']}_t{i + 50}_{j + 50}"
                build_object(name, sub, [a['slotKeys'][s] for s in used])
                made.append({'asset': name, 'tile': [i, j], 'levelStatic': True})
        elif data is not None:
            build_object(a['asset'], data, a['slotKeys'])
            made.append({'asset': a['asset'], 'levelStatic': bool(a.get('levelStatic'))})
        # Instanced meshes of this node: one base mesh each plus its placements.
        for mi in a['meshes']:
            m = exp['meshes'][mi]
            if not m.get('instances'):
                continue
            mats = m['mat'] if isinstance(m['mat'], list) else [m['mat']]
            name = f"{a['asset']}__inst{mi}"
            # The base geometry: the same mesh entry without its instance list.
            exp['meshes'].append({k: v for k, v in m.items() if k != 'instances'})
            base = gather(exp, bin_data, [len(exp['meshes']) - 1], mats)
            exp['meshes'].pop()
            if base is None:
                continue
            build_object(name, base, [a['slotKeys'][a['slots'].index(x)] if x in a['slots'] else 'M_missing' for x in mats])
            mats_arr = array(bin_data, m['instances'], np.float32).reshape(-1, 16)
            instances.append({'asset': name, 'count': int(m['instanceCount']), 'transforms': [to_ue_transform(row.tolist()) for row in mats_arr[: int(m['instanceCount'])]],
                              'cast': m.get('cast', True)})
            made.append({'asset': name, 'instanced': True})
    os.makedirs(OUT, exist_ok=True)
    bpy.ops.object.select_all(action='SELECT')
    fbx = os.path.join(OUT, rig + '.fbx')
    bpy.ops.export_scene.fbx(filepath=fbx, use_selection=True, apply_unit_scale=True, apply_scale_options='FBX_SCALE_NONE',
                             bake_space_transform=False, object_types={'MESH'}, use_mesh_modifiers=True, mesh_smooth_type='FACE',
                             use_custom_props=False, add_leaf_bones=False, axis_forward='-Z', axis_up='Y', colors_type='LINEAR', use_tspace=False)
    json.dump({'rig': rig, 'assets': made, 'instances': instances}, open(os.path.join(OUT, rig + '.manifest.json'), 'w'))
    if SAVE_BLEND:
        os.makedirs(BLEND, exist_ok=True)
        bpy.ops.wm.save_as_mainfile(filepath=os.path.join(BLEND, rig + '.blend'), compress=True)
    print(f'HK: {rig}: {len(made)} assets, {sum(i["count"] for i in instances)} instances -> {fbx}')


rigs = RIGS or sorted(f[:-5] for f in os.listdir(PLAN) if f.endswith('.json') and f != 'material-keys.json')
for r in rigs:
    build_rig(r)
