"""Building details for the photo-real pass, built headless:

    blender -b --factory-startup -P tools/blender/build_details.py

The real blocks around Haarklous plass (Street View, 27 Oct 2026) have cantilevered concrete balconies with black
steel railings (some with solid panel fronts) stacked in columns, downpipes at the corners and brick chimneys on
the ridges. The web has none of these, and they do not change the game: they have no collision and the player
cannot reach them (balconies start one storey up).

Meshes (metres, Blender z up, the wall plane at y = 0, sticking out toward -y):
  SM_Balcony        concrete slab 2.6 x 1.15 m (slot 0 concrete)
  SM_BalconyRail    its railing of steel bars, placed with it but casting no shadow (slot 0 railing)
  SM_BalconyPanel   a slab with a solid painted front (slot 0 concrete, 1 railing, 2 panel)
  SM_Downpipe       8-sided pipe, 1 m tall (scaled to the building), with wall brackets (slot 0 metal)
  SM_Chimney        brick stack with a concrete cap, its base 1.5 m below the given point (slot 0 brick, 1 cap)
  SM_Kerb           a 1 m granite kerb stone, 15 cm wide, 10 cm above the ground (slot 0 granite), placed 2 m
                    long (stretched) to halve the number of objects
each written to tools/out/fbx/details_<name>.fbx.

Placements (the same format as the tree lists, spawned by AHKGame::SpawnTrees) go to
unreal/Haarklous/Content/Data/instances/details.json: balconies in columns on about half of the long walls of
apartment blocks (three storeys or more), on some of the window bays (3 m, as M_HK_Building draws them), away from
the stair towers and not on Haarklous plass 17 itself (its own flat and balcony are real geometry); downpipes at
about half of the outside corners; two chimneys on each hipped ridge; kerb stones along both edges of the
drivable streets (the web paints them w wide), except where the ground beyond is asphalt too (junctions, drives,
parking) according to the game's own surface map.
"""
import json
import math
import os

import bpy
import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
DATA = os.path.join(ROOT, 'unreal', 'Haarklous', 'Content', 'Data')
OUT = os.path.join(ROOT, 'tools', 'out', 'fbx')
LOOKS = json.load(open(os.path.join(ROOT, 'tools', 'out', 'textures', 'building-looks.json'), encoding='utf-8'))
STOREY, PLINTH = 2.85, 1.425


# ---------- meshes ----------
class MeshBuilder:
    def __init__(self):
        self.v, self.f, self.uv, self.mat = [], [], [], []

    def quad(self, a, b, c, d, slot, uv=None):
        """Quad a-b-c-d (counter-clockwise seen from the front), uvs in metres unless given."""
        base = len(self.v)
        self.v += [a, b, c, d]
        if uv is None:
            e1 = np.subtract(b, a); e2 = np.subtract(d, a)
            l1, l2 = np.linalg.norm(e1), np.linalg.norm(e2)
            uv = [(0, 0), (l1, 0), (l1, l2), (0, l2)]
        self.uv.append(uv)
        self.f.append((base, base + 1, base + 2, base + 3))
        self.mat.append(slot)

    def box(self, x0, x1, y0, y1, z0, z1, slot, skip=()):
        P = lambda x, y, z: (x, y, z)
        faces = {
            'top': [P(x0, y0, z1), P(x1, y0, z1), P(x1, y1, z1), P(x0, y1, z1)],
            'bottom': [P(x0, y1, z0), P(x1, y1, z0), P(x1, y0, z0), P(x0, y0, z0)],
            'front': [P(x0, y0, z0), P(x1, y0, z0), P(x1, y0, z1), P(x0, y0, z1)],
            'back': [P(x1, y1, z0), P(x0, y1, z0), P(x0, y1, z1), P(x1, y1, z1)],
            'left': [P(x0, y1, z0), P(x0, y0, z0), P(x0, y0, z1), P(x0, y1, z1)],
            'right': [P(x1, y0, z0), P(x1, y1, z0), P(x1, y1, z1), P(x1, y0, z1)],
        }
        for k, q in faces.items():
            if k not in skip:
                self.quad(*q, slot)

    def object(self, name, slots):
        me = bpy.data.meshes.new(name)
        me.from_pydata([tuple(map(float, p)) for p in self.v], [], self.f)
        me.update()
        uvl = me.uv_layers.new(name='UVMap')
        for poly, uv in zip(me.polygons, self.uv):
            for li, t in zip(poly.loop_indices, uv):
                uvl.data[li].uv = t
            poly.material_index = self.mat[poly.index]
        for s in slots:
            me.materials.append(bpy.data.materials.get(s) or bpy.data.materials.new(s))
        ob = bpy.data.objects.new(name, me)
        bpy.context.scene.collection.objects.link(ob)
        return ob


def railing(m, slot, x0, x1, y, z0, z1, side=False):
    """A railing panel (both sides): the bars are drawn by M_HK_Railing from the uvs (u = metres along, v = height)."""
    L = abs(x1 - x0)
    if not side:
        q = [(x0, y, z0), (x1, y, z0), (x1, y, z1), (x0, y, z1)]
    else:
        q = [(y, x0, z0), (y, x1, z0), (y, x1, z1), (y, x0, z1)]
    uv = [(0, 0), (L, 0), (L, 1), (0, 1)]
    m.quad(*q, slot, uv)
    m.quad(q[1], q[0], q[3], q[2], slot, [(L, 0), (0, 0), (0, 1), (L, 1)])


def balcony(panel, rail_only=False):
    m = MeshBuilder()
    if not rail_only:
        m.box(-1.3, 1.3, -1.15, 0.0, -0.16, 0.0, 0)
    if panel:
        m.box(-1.27, 1.27, -1.15, -1.11, 0.0, 1.0, 2)
        m.box(-1.3, -1.26, -1.11, -0.02, 0.0, 1.0, 2)
        m.box(1.26, 1.3, -1.11, -0.02, 0.0, 1.0, 2)
        m.box(-1.3, 1.3, -1.17, -1.09, 1.0, 1.05, 1)            # handrail on top
    elif rail_only:
        railing(m, 0, -1.27, 1.27, -1.12, 0.0, 1.0)
        railing(m, 0, -1.12, -0.02, -1.27, 0.0, 1.0, side=True)
        railing(m, 0, -0.02, -1.12, 1.27, 0.0, 1.0, side=True)
    return m


def downpipe():
    m = MeshBuilder()
    r, n = 0.045, 8
    for i in range(n):
        a0, a1 = 2 * math.pi * i / n, 2 * math.pi * (i + 1) / n
        p0, p1 = (math.cos(a0) * r, math.sin(a0) * r), (math.cos(a1) * r, math.sin(a1) * r)
        m.quad((p0[0], p0[1], 0), (p1[0], p1[1], 0), (p1[0], p1[1], 1), (p0[0], p0[1], 1), 0,
               [(i / n * 0.28, 0), ((i + 1) / n * 0.28, 0), ((i + 1) / n * 0.28, 1), (i / n * 0.28, 1)])
    return m


def kerb():
    m = MeshBuilder()
    m.box(-0.5, 0.5, -0.075, 0.075, -0.2, 0.08, 0, skip=('bottom',))
    # A small chamfer along the street side of the top.
    m.quad((-0.5, -0.075, 0.08), (0.5, -0.075, 0.08), (0.5, -0.06, 0.1), (-0.5, -0.06, 0.1), 0)
    m.box(-0.5, 0.5, -0.06, 0.075, 0.08, 0.1, 0, skip=('bottom', 'front'))
    return m


DRIVABLE = {'residential', 'service', 'tertiary', 'secondary', 'primary', 'unclassified', 'trunk', 'trunk_link'}


def kerbs(s, game, to_world, ue):
    import base64
    ter = s['terrain']
    n, half, step = ter['n'], ter['half'], ter['step']
    grid = np.frombuffer(base64.b64decode(ter['dm']), dtype='<i2', count=n * n).astype(np.float64).reshape(n, n) / 10.0
    play = game['meta'].get('playHalf', 540)
    surf = np.fromfile(os.path.join(DATA, 'surface-classes.bin'), dtype=np.uint8).reshape(1024, 1024)

    def height(x, z):
        """The visible ground: surroundings.js terrainMesh triangles (a c b, b c d), 4 m cells within 300 m (the near
        terrain), else the far terrain (8 m cells, 0.5 m lower)."""
        res, drop = (4, 0.0) if max(abs(x), abs(z)) < 300 else (8, 0.5)
        k = res // step
        fi = min(n - 1 - k, max(0.0, (x + half) / step)) / k; fj = min(n - 1 - k, max(0.0, (z + half) / step)) / k
        i, j = int(fi), int(fj); u, v = fi - i, fj - j
        a, b, c, d = grid[j * k, i * k], grid[j * k, (i + 1) * k], grid[(j + 1) * k, i * k], grid[(j + 1) * k, (i + 1) * k]
        h = a + u * (b - a) + v * (c - a) if u + v <= 1 else d + (1 - u) * (c - d) + (1 - v) * (b - d)
        return s['ground'] + h - drop

    def asphalt(x, z):           # FHKData::SurfaceAt == 'asphalt'
        i, j = int((x + play) / (2 * play) * 1024), int((z + play) / (2 * play) * 1024)
        return 0 <= i < 1024 and 0 <= j < 1024 and surf[j, i] == 1

    out = []
    for r in s['roads']:
        if r['k'] not in DRIVABLE:
            continue
        pts = np.array(r['p'], dtype=float).reshape(-1, 2)
        hw = r['w'] / 2
        for a, c in zip(pts[:-1], pts[1:]):
            L = float(np.hypot(*(c - a)))
            if L < 0.5:
                continue
            d = (c - a) / L
            nrm = np.array([-d[1], d[0]])
            for side in (1, -1):
                for t in np.arange(1.0, L, 2.0):
                    p = a + d * t + nrm * side * hw
                    # The surface map has ~1 m pixels, so a slanted road edge is jagged in it: only skip where the
                    # ground is asphalt well beyond the edge (a junction, a drive, a car park).
                    if abs(p[0]) > play - 3 or abs(p[1]) > play - 3 or (asphalt(*(p + nrm * side * 1.5)) and asphalt(*(p + nrm * side * 2.5))):
                        continue
                    wx, wz = to_world(*p)
                    # The stone's street side (-y in Blender, +Y in Unreal) faces the road.
                    wn = np.array(to_world(*(p - nrm * side))) - np.array([wx, wz])
                    yaw_n = math.degrees(math.atan2(wn[1], wn[0])) - 90.0
                    out.append(ue(wx, height(*p), wz, yaw_n, sx=2.0))
    return out


def chimney():
    m = MeshBuilder()
    m.box(-0.3, 0.3, -0.45, 0.45, -1.5, 1.1, 0, skip=('top', 'bottom'))
    m.box(-0.37, 0.37, -0.52, 0.52, 1.1, 1.18, 1)
    return m


# ---------- placements ----------
def main():
    s = json.load(open(os.path.join(DATA, 'surroundings.json'), encoding='utf-8'))
    game = json.load(open(os.path.join(DATA, 'game.json'), encoding='utf-8'))
    th, gx, gz, ground = s['geo']['theta'], s['geo']['x'], s['geo']['z'], s['ground']
    cT, sT = math.cos(th), math.sin(th)
    to_world = lambda x, z: (x * cT + z * sT + gx, -x * sT + z * cT + gz)
    towers = []
    for t in game['towers']:
        M = t['matrix']
        towers.append((M[0] * 0.1 + M[8] * 8 + M[12], M[2] * 0.1 + M[10] * 8 + M[14]))

    def ue(x, y, z, yaw_deg, sx=1.0, sy=1.0, sz=1.0):
        h = math.radians(yaw_deg) / 2
        return [x * 100, z * 100, y * 100, 0.0, 0.0, math.sin(h), math.cos(h), sx, sy, sz]

    def h01(*v):
        x = math.sin(sum(a * b for a, b in zip(v, (12.9898, 78.233, 37.719)))) * 43758.5453
        return x - math.floor(x)

    lists = {'SM_Balcony': [], 'SM_BalconyRail': [], 'SM_BalconyPanel': [], 'SM_Downpipe': [], 'SM_Chimney': [], 'SM_Kerb': []}
    for i, b in enumerate(s['buildings']):
        look = LOOKS.get(str(i))
        if not look or b['s'] != 'brick':
            continue
        ring = np.array(b['r']['o'], dtype=float).reshape(-1, 2)
        world = np.array([to_world(x, z) for x, z in ring])
        g0 = ground + (b.get('g') or 0)
        top = g0 + b['h']
        storeys = int((top - g0 - PLINTH) // STOREY)
        # The web's walls() runs the ring the other way round when its signed area is positive: u accumulates along that.
        order = ring[::-1] if (0.5 * np.sum(ring[:, 0] * np.roll(ring[:, 1], -1) - np.roll(ring[:, 0], -1) * ring[:, 1])) > 0 else ring
        wo = np.array([to_world(x, z) for x, z in order])
        u = 0.0
        plaster = look['style'] == 'plaster'
        for k in range(len(wo)):
            a, c = wo[k], wo[(k + 1) % len(wo)]
            L = float(np.hypot(*(c - a)))
            if L < 0.02:
                continue
            d = (c - a) / L
            n = np.array([d[1], -d[0]])
            if inside(world, (a + c) / 2 + n * 0.4):                   # make sure n points out of the footprint
                n = -n
            yaw = math.degrees(math.atan2(n[1], n[0])) - 90.0          # mesh sticks out along Unreal +Y
            if storeys >= 3 and L >= 9 and h01(i, k, 2.0) < 0.6:
                first = math.ceil((u + 1.5) / 3.0)
                for bay in range(first, int((u + L - 1.5) // 3.0) + 1):
                    centre_u = bay * 3.0 + 1.5 - u
                    if centre_u < 1.6 or centre_u > L - 1.6 or h01(i, bay, 7.0) > 0.38:
                        continue
                    p = a + d * centre_u
                    if any(math.hypot(p[0] - tx, p[1] - tz) < 6 for tx, tz in towers):
                        continue
                    if math.hypot(p[0], p[1]) < 30:                   # Haarklous plass 17 and its own balcony
                        continue
                    kind = 'SM_BalconyPanel' if (plaster or h01(i, k, 5.0) < 0.3) else 'SM_Balcony'
                    for st in range(1, storeys):
                        y = g0 + PLINTH + st * STOREY
                        lists[kind].append(ue(p[0], y, p[1], yaw))
                        if kind == 'SM_Balcony':
                            lists['SM_BalconyRail'].append(ue(p[0], y, p[1], yaw))
            u += L
        # Downpipes at outside corners (about half of them).
        if storeys >= 2:
            m = len(world)
            for k in range(m):
                p0, p1, p2 = world[k - 1], world[k], world[(k + 1) % m]
                e1, e2 = p1 - p0, p2 - p1
                if np.hypot(*e1) < 2 or np.hypot(*e2) < 2 or h01(i, k, 11.0) > 0.5:
                    continue
                bis = (e1 / np.hypot(*e1)) - (e2 / np.hypot(*e2))
                if np.hypot(*bis) < 0.3:
                    continue
                out = bis / np.hypot(*bis)
                if inside(world, p1 + out * 0.3):
                    continue                                          # an inside corner
                q = p1 + out * 0.09
                if any(math.hypot(q[0] - tx, q[1] - tz) < 5 for tx, tz in towers) or math.hypot(q[0], q[1]) < 25:
                    continue
                lists['SM_Downpipe'].append(ue(q[0], g0 - 0.6, q[1], 0.0, 1, 1, (top - 0.15) - (g0 - 0.6)))
        # Chimneys on hipped ridges (surroundings.js hipRoof: corners pushed out .35*sqrt(2), 27 degree pitch).
        if b.get('k'):
            cr = np.array(b['k'], dtype=float).reshape(-1, 2)
            cen = cr.mean(axis=0)
            cr = np.array([p + (p - cen) / max(np.hypot(*(p - cen)), 1e-6) * 0.35 * math.sqrt(2) for p in cr])
            A, B, C, D = cr
            if np.hypot(*(A - B)) < np.hypot(*(B - C)):
                A, B, C, D = B, C, D, A
            width = np.hypot(*(B - C))
            rise = width / 2 * math.tan(math.radians(27))
            axis = (B - A) / np.hypot(*(B - A))
            r1 = (A + D) / 2 + axis * width / 2
            r2 = (B + C) / 2 - axis * width / 2
            for f in (0.3, 0.72):
                if h01(i, f, 13.0) > 0.85:
                    continue
                pl = r1 + (r2 - r1) * f
                wx, wz = to_world(*pl)
                w1, w2 = np.array(to_world(*r1)), np.array(to_world(*r2))
                yaw = math.degrees(math.atan2(w2[1] - w1[1], w2[0] - w1[0]))
                lists['SM_Chimney'].append(ue(wx, top + rise, wz, yaw))
    lists['SM_Kerb'] = kerbs(s, game, to_world, ue)
    return lists


def inside(poly, p):
    x, y = p
    c = False
    n = len(poly)
    for k in range(n):
        x0, y0 = poly[k]; x1, y1 = poly[(k + 1) % n]
        if (y0 > y) != (y1 > y) and x < (x1 - x0) * (y - y0) / (y1 - y0 + 1e-12) + x0:
            c = not c
    return c


bpy.ops.wm.read_factory_settings(use_empty=True)
os.makedirs(OUT, exist_ok=True)
for name, mb, slots in (('SM_Balcony', balcony(False), ['MI_HK_DetailConcrete']),
                        ('SM_BalconyRail', balcony(False, rail_only=True), ['MI_HK_Railing']),
                        ('SM_BalconyPanel', balcony(True), ['MI_HK_DetailConcrete', 'MI_HK_Railing', 'MI_HK_DetailPanel']),
                        ('SM_Downpipe', downpipe(), ['MI_HK_DetailMetal']),
                        ('SM_Chimney', chimney(), ['MI_HK_DetailBrick', 'MI_HK_DetailConcrete']),
                        ('SM_Kerb', kerb(), ['MI_HK_DetailGranite'])):
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o)
    ob = mb.object(name, slots)
    bpy.ops.object.select_all(action='DESELECT')
    ob.select_set(True)
    bpy.ops.export_scene.fbx(filepath=os.path.join(OUT, f'details_{name}.fbx'), use_selection=True, apply_unit_scale=True,
                             apply_scale_options='FBX_SCALE_NONE', object_types={'MESH'}, mesh_smooth_type='FACE',
                             add_leaf_bones=False, axis_forward='-Z', axis_up='Y')
lists = main()
inst = [{'asset': k, 'count': len(v), 'transforms': v, 'cast': True, 'kind': 'detail'} for k, v in lists.items() if v]
os.makedirs(os.path.join(DATA, 'instances'), exist_ok=True)
json.dump({'rig': 'details', 'instances': inst}, open(os.path.join(DATA, 'instances', 'details.json'), 'w'))
print('HK: details', {k: len(v) for k, v in lists.items()})
