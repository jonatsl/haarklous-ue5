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

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import car_model  # noqa: E402  (photo-real pass: the car's new body and wheels)
import tree_model  # noqa: E402  (photo-real pass: tree crowns with limbs and leaf masses)
import facade_model  # noqa: E402  (photo-real pass: 3D facades near home replace the flat web walls)

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


def glows(mid):
    """Lamp globes and other light sources (emissive colour x intensity >= .5) cast no shadow: in Unreal the lamp's
    light sits just under its globe, which would throw a dark disc onto the ceiling."""
    m = MATS[mid]
    e = m.get('emissive') or 0
    lum = (0.2126 * ((e >> 16) & 255) + 0.7152 * ((e >> 8) & 255) + 0.0722 * (e & 255)) / 255
    return lum * (m.get('emissiveIntensity') or 0) >= 0.5


def horizon_with_hole(pos, nor, uv, exp, bin_data):
    """The web's 18 km horizon plane was drawn in a separate pass behind the district; in one Unreal scene it would cut
    through the mapped area, so it gets a hole over the district terrain (+-600 m in the map frame). The hole's rim
    follows the terrain's edge heights (-42..29 m) and slopes out to the web's plane height, so there is no gap or
    overhang between them to look through (Unreal's sky is dark below the horizon; the web drew sky behind the gap)."""
    data = json.load(open(os.path.join(ROOT, 'unreal', 'Haarklous', 'Content', 'Data', 'surroundings.json'), encoding='utf-8'))
    th, gx, gz = data['geo']['theta'], data['geo']['x'], data['geo']['z']
    o, a, b = pos[0], pos[1] - pos[0], pos[2] - pos[0]          # corners of the plane: o, o+a, o+b (+ o+a+b)
    y = float(pos[:, 1].mean())
    def uv_at(x, z):
        # Solve x,z = o + u*a + v*b on the xz plane, then interpolate the corner uvs.
        M = np.array([[a[0], b[0]], [a[2], b[2]]])
        u, v = np.linalg.solve(M, np.array([x - o[0], z - o[2]]))
        return uv[0] + (uv[1] - uv[0]) * u + (uv[2] - uv[0]) * v
    outer = [pos[0], pos[1], pos[3], pos[2]] if len(pos) >= 4 else list(pos)
    cx, cz = np.mean([p[0] for p in outer]), np.mean([p[2] for p in outer])
    outer = sorted(outer, key=lambda p: math.atan2(p[2] - cz, p[0] - cx))
    # The rim: the district terrain's outermost vertices, in order around the district, 0.3 m below the edge.
    terrain = next(m for m in exp['meshes'] if m.get('name') == 'Terrain (district)')
    tp = array(bin_data, terrain['geo']['pos'], np.float32).reshape(-1, 3).astype(np.float64)
    dx, dz = tp[:, 0] - gx, tp[:, 2] - gz
    lx, lz = dx * math.cos(th) - dz * math.sin(th), dx * math.sin(th) + dz * math.cos(th)
    r = np.maximum(np.abs(lx), np.abs(lz))
    rim = np.where(r > r.max() - 0.5)[0]
    rim = rim[np.argsort(np.arctan2(tp[rim, 2] - cz, tp[rim, 0] - cx))]
    P = [np.array([p[0], y, p[2]]) for p in outer] + [tp[k] - [0, 0.3, 0] for k in rim]
    U = [uv_at(p[0], p[2]) for p in P]
    # Each rim vertex joins the outer corner nearest to it by angle; where that changes, a triangle fills the corner.
    oang = np.array([math.atan2(p[2] - cz, p[0] - cx) for p in outer])
    def corner(p):
        d = np.abs((math.atan2(p[2] - cz, p[0] - cx) - oang + math.pi) % (2 * math.pi) - math.pi)
        return int(np.argmin(d))
    n = len(rim)
    cs = [corner(P[4 + k]) for k in range(n)]
    tris = []
    for k in range(n):
        k1 = (k + 1) % n
        c0, c1 = cs[k], cs[k1]
        tris.append([4 + k, 4 + k1, c1])
        if c0 != c1:
            tris.append([4 + k, c1, c0])
    tris = np.array(tris, dtype=np.int64)
    P, U = np.array(P), np.array(U)
    # Keep every triangle facing up (+y).
    up = np.cross(P[tris[:, 1]] - P[tris[:, 0]], P[tris[:, 2]] - P[tris[:, 0]])[:, 1] < 0
    tris[up] = tris[up][:, [0, 2, 1]]
    return P, np.tile([0.0, 1.0, 0.0], (len(P), 1)), U, None, tris


def quad_corners(c, n_axis, size, rng):
    """Two triangles of a square card centred at c (web frame), randomly turned; returns corner positions and uvs."""
    q = rng.normal(size=4)
    q /= np.linalg.norm(q)
    x, y, z, w = q
    R = np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                  [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                  [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])
    pts = np.array([[-1, -1, 0], [1, -1, 0], [1, 1, 0], [-1, 1, 0]]) * size / 2
    pts = pts @ R.T + c
    uv = np.array([[0, 0], [1, 0], [1, 1], [0, 1]], dtype=np.float64)
    return pts[[0, 1, 2, 0, 2, 3]].reshape(2, 3, 3), uv[[0, 1, 2, 0, 2, 3]].reshape(2, 3, 2)


def cylinder_tris(a, b, r0, r1, sides, slot, v_repeat=1.0):
    """Tapered cylinder from a to b (web frame) as corner arrays."""
    a, b = np.asarray(a, float), np.asarray(b, float)
    axis = b - a
    L = np.linalg.norm(axis)
    d = axis / L
    t = np.cross(d, [0.0, 0.0, 1.0] if abs(d[2]) < 0.9 else [1.0, 0.0, 0.0])
    t /= np.linalg.norm(t)
    s = np.cross(d, t)
    P, N, U = [], [], []
    for i in range(sides):
        a0, a1 = 2 * np.pi * i / sides, 2 * np.pi * (i + 1) / sides
        n0, n1 = np.cos(a0) * t + np.sin(a0) * s, np.cos(a1) * t + np.sin(a1) * s
        p00, p01, p10, p11 = a + n0 * r0, a + n1 * r0, b + n0 * r1, b + n1 * r1
        u0, u1 = i / sides * 2, (i + 1) / sides * 2
        P += [[p00, p10, p11], [p00, p11, p01]]
        N += [[n0, n0, n1], [n0, n1, n1]]
        U += [[[u0, 0], [u0, v_repeat], [u1, v_repeat]], [[u0, 0], [u1, v_repeat], [u1, 0]]]
    n = len(P)
    return {'pos': np.array(P), 'nor': np.array(N), 'uv': np.array(U), 'slot': np.full(n, slot)}


def concat(parts):
    return {k: np.concatenate([p[k] for p in parts]) for k in ('pos', 'nor', 'uv', 'slot')}


def foliage(kind, variant):
    """Better trees and bushes in the web's own normalised instance space (surroundings.js plantTrees):
    trunk = cylinder of height 1 (radius .17 -> .11), scaled per tree; crown = cards around (0, .55, 0) within ~1, scaled
    (r * .95, crown * .5, r * .95); bush = cards around (0, .35, 0). Same placements and sizes, denser crowns, branches."""
    rng = np.random.default_rng(100 + variant)
    if kind == 'trunk':
        parts, y = [], 0.0
        bend = rng.normal(0, 0.03, 2)
        for k in range(4):
            y1 = y + 0.25
            c0 = np.array([bend[0] * y * y, y, bend[1] * y * y]); c1 = np.array([bend[0] * y1 * y1, y1, bend[1] * y1 * y1])
            r0 = 0.17 - 0.06 * y + (0.05 if k == 0 else 0); r1 = 0.17 - 0.06 * y1
            parts.append(cylinder_tris(c0 - [0, 0.02 if k == 0 else 0, 0], c1, r0, r1, 10, 0, 0.8))
            y = y1
        return concat(parts), 1
    lift = 0.35 if kind == 'bush' else 0.55
    cards = 130 if kind == 'bush' else 280
    centre = np.array([0.0, lift, 0.0])
    parts = []
    P, N, U = [], [], []
    for k in range(cards):
        d = np.array([rng.uniform(-1, 1), rng.uniform(-0.5, 1.1), rng.uniform(-1, 1)])
        d /= np.linalg.norm(d)
        c = d * (0.3 + 0.65 * rng.uniform() ** 0.6) + centre
        size = rng.uniform(0.42, 0.72) if kind != 'bush' else rng.uniform(0.38, 0.62)
        tri, uv = quad_corners(c, None, size, rng)
        nrm = tri - centre
        nrm /= np.linalg.norm(nrm, axis=-1, keepdims=True)
        P.append(tri); N.append(nrm); U.append(uv)
    leaves = {'pos': np.concatenate(P), 'nor': np.concatenate(N), 'uv': np.concatenate(U), 'slot': np.zeros(cards * 2, dtype=np.int64)}
    parts.append(leaves)
    if kind != 'bush':
        for k in range(6):
            a = 2 * np.pi * k / 6 + rng.uniform(-.3, .3)
            tip = np.array([np.cos(a) * rng.uniform(.45, .7), lift + rng.uniform(.1, .55), np.sin(a) * rng.uniform(.45, .7)])
            parts.append(cylinder_tris([0, -0.05, 0], tip, 0.07, 0.025, 6, 1, 1.0))
        parts.append(cylinder_tris([0, -0.05, 0], [0, lift + .7, 0], 0.08, 0.03, 7, 1, 1.0))
    return concat(parts), (2 if kind != 'bush' else 1)


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
        if m.get('type') == 'PlaneGeometry' and (m.get('params') or {}).get('width', 0) >= 10000:
            pos, nor, uv, col, tris = horizon_with_hole(pos, nor, uv, exp, bin_data)
        if g.get('flip'):
            tris = tris[:, [0, 2, 1]]
        tmat = np.zeros(len(tris), dtype=np.int64)   # index into mats
        # three.js uses geometry groups only when the mesh has an array of materials.
        groups = g.get('groups') if isinstance(m['mat'], list) else None
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
                      'vcol': any(MATS[int(mid)].get('vertexColors') for mid in np.unique(mat_ids)),
                      'cast': np.array([not glows(int(mid)) for mid in mat_ids], dtype=bool)})
    if not parts:
        return None
    out = {k: np.concatenate([p[k] for p in parts]) for k in ('pos', 'nor', 'uv', 'slot', 'cast')}
    if any(p['col'] is not None and p['vcol'] for p in parts):
        out['col'] = np.concatenate([p['col'] if (p['col'] is not None and p['vcol']) else np.ones((len(p['slot']), 3, 3)) for p in parts])
    return out


# The web car's exterior materials (body paint, glass, bumpers, lights, plate, handles): replaced by car_model.
CAR_EXTERIOR = {'MI_Physical_111214_362', 'MI_Physical_1d2328_363', 'MI_polymer_141516', 'MI_Standard_e8eef2_365',
                'MI_Standard_a00000_366', 'MI_chrome', 'MI_Standard_e8eef2_368', 'MI_Standard_a00000_369',
                'MI_Standard_f2f2ee_370'}


def corners_to_data(P, N, U, K, slot_keys):
    T = len(K)
    return {'pos': P, 'nor': N, 'uv': U, 'slot': np.array([slot_keys.index(k) for k in K], dtype=np.int64),
            'cast': np.ones(T, dtype=bool)}


def car_parts(asset, data, slot_keys):
    """Photo-real pass: the car keeps the web's interior, seats, dashboard and steering wheel, gets a smooth body
    (car_model.car_shell) and alloy wheels (car_model.car_wheel, mirrored on the left side)."""
    if asset == 'SM_car__body':
        keep = np.array([slot_keys[s] not in CAR_EXTERIOR for s in data['slot']], dtype=bool)
        inner = {k: v[keep] for k, v in data.items() if k in ('pos', 'nor', 'uv', 'slot', 'cast')}
        shell = corners_to_data(*car_model.car_shell(), slot_keys)
        return {k: np.concatenate([inner[k], shell[k]]) for k in inner}
    if asset.startswith('SM_car__wheels_') and asset.endswith('_wheel'):
        P, N, U, K = car_model.car_wheel()
        if int(asset.split('_')[-2]) in (0, 2):                 # the left wheels (x = -0.78): outer face toward -x
            P, N = P * [-1, 1, 1], N * [-1, 1, 1]
            P, N, U = P[:, ::-1], N[:, ::-1], U[:, ::-1]
        return corners_to_data(P, N, U, K, slot_keys)
    return data


# Photo-real pass: these rigs keep the web's shapes but get rounded edges (a small bevel that catches the light, as
# real metal and plastic edges do) and smooth, weighted normals instead of the web's flat facets.
SOFTEN_RIGS = {'motorcycle', 'excavator', 'helicopter', 'plane'}


def soften(ob):
    import bmesh
    me = ob.data
    bm = bmesh.new()
    bm.from_mesh(me)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-4)
    bm.to_mesh(me)
    bm.free()
    if me.has_custom_normals:
        me.attributes.remove(me.attributes['custom_normal'])
    for poly in me.polygons:
        poly.use_smooth = True
    bev = ob.modifiers.new('round edges', 'BEVEL')
    bev.width = 0.012
    bev.segments = 2
    bev.limit_method = 'ANGLE'
    bev.angle_limit = math.radians(35)
    bev.harden_normals = True
    ob.modifiers.new('smooth by angle', 'WEIGHTED_NORMAL').keep_sharp = True


TERRAIN_KEYS = ('MI_Standard_a6a6a6_0', 'MI_Standard_a6a6a6_18')   # the web's near and far terrain materials


def drop_site_terrain(data, slot_keys):
    """The construction site (tools/make-site.py, site.json) is the game's own soil mesh: the terrain triangles inside
    its square (map-local frame, on the far terrain's 8 m grid) are left out of the tiles."""
    site_path = os.path.join(ROOT, 'unreal', 'Haarklous', 'Content', 'Data', 'site.json')
    keys = [slot_keys.index(k) for k in TERRAIN_KEYS if k in slot_keys]
    if not keys or not os.path.exists(site_path):
        return data
    site = json.load(open(site_path, encoding='utf-8'))
    r = site['rect']
    sur = json.load(open(os.path.join(ROOT, 'unreal', 'Haarklous', 'Content', 'Data', 'surroundings.json'), encoding='utf-8'))
    th, gx, gz = sur['geo']['theta'], sur['geo']['x'], sur['geo']['z']
    cT, sT = math.cos(th), math.sin(th)
    px, pz = data['pos'][:, :, 0] - gx, data['pos'][:, :, 2] - gz
    lx, ls = px * cT - pz * sT, px * sT + pz * cT
    e = 1e-3
    inside = ((lx >= r['x0'] - e) & (lx <= r['x0'] + r['w'] + e) & (ls >= r['s0'] - e) & (ls <= r['s0'] + r['d'] + e)).all(axis=1)
    drop = inside & np.isin(data['slot'], keys)
    if drop.any():
        print(f'HK: {int(drop.sum())} terrain triangles under the construction site left out')
    return {k: v[~drop] for k, v in data.items()}


def drop_rebuilt_facades(data, slot_keys):
    """Photo-real pass: the web's flat facade triangles on the walls that tools/blender/build_facades.py rebuilds
    in 3D (facade_model.covered_walls) are left out of the city tiles."""
    cells = facade_model.index_walls(facade_model.covered_walls())
    fac = slot_keys.index('MI_facade')
    cen = data['pos'].mean(axis=1)
    drop = np.array([s == fac and facade_model.on_wall(c, None, cells) for s, c in zip(data['slot'], cen)], dtype=bool)
    print(f'HK: {int(drop.sum())} flat facade triangles replaced by 3D facades')
    return {k: v[~drop] for k, v in data.items()}


# Motorcycle wheels (game-vehicles.js Motorcycle: 17" cast wheels, front 0.3 m x 0.12 m, rear 0.31 m x 0.17 m, six
# spokes): the web's torus tyre and flat spokes become a rounded tyre and a cast rim; the brake discs are kept.
MOTO_WHEELS = {'SM_motorcycle__fw': (0.30, 0.12), 'SM_motorcycle__rw': (0.31, 0.17)}


def moto_parts(asset, data, slot_keys):
    if asset not in MOTO_WHEELS:
        return data
    R, W = MOTO_WHEELS[asset]
    tyre, rim, disc = 'MI_Standard_161616_439', 'MI_paint_17181a', 'MI_metal_b7bcc0'
    keep = np.array([slot_keys[s] == disc for s in data['slot']], dtype=bool)
    discs = {k: v[keep] for k, v in data.items() if k in ('pos', 'nor', 'uv', 'slot', 'cast')}
    wheel = corners_to_data(*car_model.car_wheel(R, W, 6, tyre, rim, centred=True), slot_keys)
    return {k: np.concatenate([discs[k], wheel[k]]) for k in discs}


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
        if rig == 'car' and data is not None:
            data = car_parts(a['asset'], data, a['slotKeys'])
        if rig == 'motorcycle' and data is not None:
            data = moto_parts(a['asset'], data, a['slotKeys'])
        if rig in ('world_near', 'world_far') and data is not None:
            data = drop_site_terrain(data, a['slotKeys'])
        if rig == 'world_near' and data is not None and 'MI_facade' in a['slotKeys']:
            data = drop_rebuilt_facades(data, a['slotKeys'])
        tile = TILES.get(rig) if a.get('levelStatic') else None
        if data is not None and tile:
            cx = data['pos'][:, :, 0].mean(axis=1)
            cz = data['pos'][:, :, 2].mean(axis=1)
            ti, tj = np.floor(cx / tile).astype(int), np.floor(cz / tile).astype(int)
            # Only the mapped district is tiled; the hills, the horizon townscape and the fjord stay whole (tile 99_99).
            out = (np.abs(cx) > 700) | (np.abs(cz) > 700)
            ti[out], tj[out] = 49, 49
            for (i, j) in sorted(set(zip(ti.tolist(), tj.tolist()))):
                for cast in (True, False):
                    sel = (ti == i) & (tj == j) & (data['cast'] == cast)
                    if not sel.any():
                        continue
                    used = sorted(set(data['slot'][sel].tolist()))
                    remap = {s: k for k, s in enumerate(used)}
                    sub = {k: v[sel] for k, v in data.items()}
                    sub['slot'] = np.array([remap[s] for s in sub['slot']])
                    name = f"{a['asset']}_t{i + 50}_{j + 50}" + ('' if cast else '__noshadow')
                    build_object(name, sub, [a['slotKeys'][s] for s in used])
                    made.append({'asset': name, 'tile': [i, j], 'levelStatic': True, 'castShadow': cast})
        elif data is not None and a.get('levelStatic') and not data['cast'].all():
            # Level geometry with lamp globes: the globes become their own mesh that casts no shadow.
            build_object(a['asset'], data, a['slotKeys'], data['cast'])
            build_object(a['asset'] + '__noshadow', data, a['slotKeys'], ~data['cast'])
            made.append({'asset': a['asset'], 'levelStatic': True})
            made.append({'asset': a['asset'] + '__noshadow', 'levelStatic': True, 'castShadow': False})
        elif data is not None:
            ob = build_object(a['asset'], data, a['slotKeys'])
            if rig in SOFTEN_RIGS:
                soften(ob)
            made.append({'asset': a['asset'], 'levelStatic': bool(a.get('levelStatic'))})
        # Instanced meshes of this node: one base mesh each plus its placements.
        for mi in a['meshes']:
            m = exp['meshes'][mi]
            if not m.get('instances'):
                continue
            mats = m['mat'] if isinstance(m['mat'], list) else [m['mat']]
            name = f"{a['asset']}__inst{mi}"
            # Trees and bushes get new Blender geometry in the web's normalised instance space (same placements).
            leaf = any(MATS[x].get('alphaTest', 0) and MATS[x]['type'] == 'MeshStandardMaterial' for x in mats if x >= 0)
            if m['type'] == 'CylinderGeometry':
                base, nslots = foliage('trunk', 0)
                build_object(name, base, ['MI_HK_Bark'])
                key = 'MI_HK_Bark'
            elif leaf:
                is_bush = m['instanceCount'] == len(json.load(open(os.path.join(ROOT, 'unreal', 'Haarklous', 'Content', 'Data', 'surroundings.json'), encoding='utf-8'))['bushes']) // 3
                crown_index = sum(1 for x in instances if 'Leaves' in x.get('kind', '') and 'Bush' not in x.get('kind', ''))
                key = 'MI_HK_LeavesBush' if is_bush else ('MI_HK_LeavesA' if crown_index % 2 == 0 else 'MI_HK_LeavesB')
                if is_bush:
                    base, nslots = foliage('bush', crown_index)
                else:
                    base = tree_model.crown(crown_index, far=rig == 'world_far')
                build_object(name, base, [key] + ([] if is_bush else ['MI_HK_Bark']))
            else:
                # The base geometry: the same mesh entry without its instance list.
                exp['meshes'].append({k: v for k, v in m.items() if k != 'instances'})
                base = gather(exp, bin_data, [len(exp['meshes']) - 1], mats)
                exp['meshes'].pop()
                if base is None:
                    continue
                build_object(name, base, [a['slotKeys'][a['slots'].index(x)] if x in a['slots'] else 'M_missing' for x in mats])
                key = ''
            mats_arr = array(bin_data, m['instances'], np.float32).reshape(-1, 16)
            instances.append({'asset': name, 'count': int(m['instanceCount']), 'transforms': [to_ue_transform(row.tolist()) for row in mats_arr[: int(m['instanceCount'])]],
                              'cast': m.get('cast', True), 'kind': key})
            made.append({'asset': name, 'instanced': True})
    os.makedirs(OUT, exist_ok=True)
    bpy.ops.object.select_all(action='SELECT')
    fbx = os.path.join(OUT, rig + '.fbx')
    bpy.ops.export_scene.fbx(filepath=fbx, use_selection=True, apply_unit_scale=True, apply_scale_options='FBX_SCALE_NONE',
                             bake_space_transform=False, object_types={'MESH'}, use_mesh_modifiers=True, mesh_smooth_type='FACE',
                             use_custom_props=False, add_leaf_bones=False, axis_forward='-Z', axis_up='Y', colors_type='LINEAR', use_tspace=False)
    json.dump({'rig': rig, 'assets': made, 'instances': instances}, open(os.path.join(OUT, rig + '.manifest.json'), 'w'))
    if instances:
        # Placements of instanced meshes (trees, bushes) for the game, which spawns them at run time.
        inst_dir = os.path.join(ROOT, 'unreal', 'Haarklous', 'Content', 'Data', 'instances')
        os.makedirs(inst_dir, exist_ok=True)
        json.dump({'rig': rig, 'instances': instances}, open(os.path.join(inst_dir, rig + '.json'), 'w'))
    if SAVE_BLEND:
        os.makedirs(BLEND, exist_ok=True)
        bpy.ops.wm.save_as_mainfile(filepath=os.path.join(BLEND, rig + '.blend'), compress=True)
    print(f'HK: {rig}: {len(made)} assets, {sum(i["count"] for i in instances)} instances -> {fbx}')


rigs = RIGS or sorted(f[:-5] for f in os.listdir(PLAN) if f.endswith('.json') and f != 'material-keys.json')
for r in rigs:
    build_rig(r)
