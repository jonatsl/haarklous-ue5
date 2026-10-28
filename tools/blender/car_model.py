"""A realistic hatchback body and alloy wheels for the car rig (photo-real pass), used by build_assets.py.

The web's car (game-vehicles.js class Car) is a few extruded side profiles: flat panels with sharp edges. This
builds a smooth shell over the same outline instead: a cage of cross-sections along the car (web car frame:
x right, y up, front at z = -2.12, rear at z = 2.1) following the web's profile points (bumper 0.62 m, cowl
0.92 m, roof 1.44 m from z = -0.05 to 1.45, hatch down to 0.95 m), wheel arches over the web's wheels (z = +-1.3,
radius 0.31), then two levels of subdivision. Glass, lights, bumpers and plates are faces of the shell; an inner
lining (headliner, dark trim) makes it solid from the driver's seat. The wheels are a rounded tyre and a five-spoke
rim in the web wheel's own frame (axle along x). Only the look changes: the web's sizes, wheel places, seats,
dashboard, steering wheel and hands are kept.

Functions return corner arrays like build_assets.gather(): pos/nor (T x 3 x 3, web frame), uv (T x 3 x 2), and
material keys per triangle.
"""
import math

import bmesh
import bpy
import numpy as np

PAINT, GLASS, TRIM = 'MI_Physical_111214_362', 'MI_Physical_1d2328_363', 'MI_polymer_141516'
HEAD, TAIL, PLATE, CHROME = 'MI_Standard_e8eef2_365', 'MI_Standard_a00000_366', 'MI_Standard_f2f2ee_370', 'MI_chrome'
LINER, DASH = 'MI_fabric_8d8a84', 'MI_polymer_1e1f21'
TYRE, RIM = 'MI_Standard_151515_357', 'MI_metal_a8adb2'

WHEEL_Z, WHEEL_R, ARCH_R = 1.3, 0.31, 0.39


def interp(points, z):
    """Piecewise-linear y(z) through (z, y) points."""
    zs, ys = zip(*points)
    return float(np.interp(z, zs, ys))


# Web profile (game-vehicles.js): lower shell and the glasshouse.
BELT = [(-2.12, 0.62), (-1.9, 0.80), (-0.7, 0.92), (1.75, 0.95), (2.05, 0.90), (2.1, 0.80)]
ROOF = [(-2.12, 0.62), (-1.9, 0.80), (-0.75, 0.92), (-0.05, 1.42), (1.45, 1.44), (1.98, 0.97), (2.1, 0.80)]


def bottom(z):
    """Underside of the shell: 0.27 m, raised in an arch over each wheel."""
    y = 0.27
    for wz in (-WHEEL_Z, WHEEL_Z):
        d = abs(z - wz)
        if d < ARCH_R:
            y = max(y, WHEEL_R + math.sqrt(ARCH_R * ARCH_R - d * d) * 0.92)
    return y


def ring(z):
    """Half cross-section at z, from the bottom centre up the right side to the roof centre: (x, y) points."""
    yb, belt, roof = bottom(z), interp(BELT, z), interp(ROOF, z)
    end = min(1.0, (2.12 - abs(z)) / 0.25) if z < 0 else min(1.0, (2.1 - abs(z)) / 0.25)
    w = 0.89 * (0.72 + 0.28 * math.sqrt(max(end, 0.0)))             # rounded nose and tail in plan
    glass = roof - belt                                               # height of the glasshouse here
    # Over the bonnet and the tail the upper points form a gently crowned panel; where the glasshouse rises they
    # blend into its tumblehome (window line drawn in) and roof.
    t = min(1.0, max(0.0, glass / 0.3))
    crown = [(w * 0.93, belt + 0.02), (w * 0.8, belt + 0.035), (w * 0.6, belt + 0.045), (w * 0.3, belt + 0.05), (0.0, belt + 0.052)]
    house = [(w * 0.93, belt + 0.025), (w - 0.088, belt + glass * 0.5), (w - 0.16, roof - min(0.04, glass * 0.3)), (w * 0.4, roof), (0.0, roof + 0.004)]
    upper = [(c[0] + (h[0] - c[0]) * t, c[1] + (h[1] - c[1]) * t) for c, h in zip(crown, house)]
    # The door's widest line stays level over the wheel arches (only kept above the arch), so the sides do not dent.
    mid = max((0.27 + belt) / 2, yb + 0.08)
    return [(0.0, yb), (w * 0.84, yb), (w * 0.985, yb + 0.07), (w, mid), (w * 0.985, belt - 0.01)] + upper


STATIONS = [-2.12, -2.08, -2.0, -1.9, -1.69, -1.6, -1.48, -1.3, -1.12, -1.0, -0.91, -0.75, -0.55, -0.3, -0.05, 0.25,
            0.45, 0.58, 0.85, 1.12, 1.0 + 0.0, 1.45, 0.0][:0] or \
    [-2.12, -2.08, -2.0, -1.9, -1.69, -1.6, -1.48, -1.3, -1.12, -1.0, -0.91, -0.75, -0.55, -0.3, -0.05, 0.25, 0.45, 0.58,
     0.91, 1.0, 1.12, 1.3, 1.48, 1.6, 1.69, 1.85, 1.98, 2.05, 2.1]


def face_material(cx, cy, cz, quad=None):
    """Material of a cage face from its centre (web frame) and, for the side quads, which band of the cross-section
    it is (ring points 0..9 run up the right side, 10..17 down the left: quads 5, 6 / 11, 12 are the glasshouse
    sides, 7..10 the roof)."""
    ax = abs(cx)
    if cy < 0.4 and (ax > 0.8 or cy < bottom(cz) + 0.02):
        return TRIM                                                       # sills and the underside
    if quad is None:
        return PAINT
    if -0.75 <= cz <= -0.05 and 6 <= quad <= 11:
        return GLASS                                                      # windscreen
    if 1.45 <= cz <= 1.98 and 6 <= quad <= 11:
        return GLASS                                                      # hatch window
    if quad in (5, 6, 11, 12) and -0.5 < cz < 1.5:
        return TRIM if 0.45 < cz < 0.6 else GLASS                         # side windows, black B pillar
    return PAINT


def cage():
    """bmesh cage: rings at STATIONS, closed with caps, material keys on faces."""
    bm = bmesh.new()
    rows = []
    for z in STATIONS:
        half = ring(z)
        pts = half + [(-x, y) for (x, y) in reversed(half[1:-1])]
        rows.append([bm.verts.new((x, y, z)) for (x, y) in pts])
    n = len(rows[0])
    keys = []
    for a, b in zip(rows[:-1], rows[1:]):
        for i in range(n):
            j = (i + 1) % n
            f = bm.faces.new((a[i], a[j], b[j], b[i]))
            c = f.calc_center_median()
            # Ring points 0..9 run up the right side, 10..17 down the left: quads 5..12 are the glasshouse and roof.
            keys.append(face_material(c.x, c.y, c.z, i))
    for r, flip in ((rows[0], True), (rows[-1], False)):
        f = bm.faces.new(list(reversed(r)) if flip else r)
        c = f.calc_center_median()
        keys.append(face_material(c.x, c.y, c.z))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return bm, keys


def to_corners(ob, keys_by_index, uv_fn=None):
    """Evaluated (modified) Blender object -> corner arrays in the web frame (the object was built in web xyz)."""
    dg = bpy.context.evaluated_depsgraph_get()
    ev = ob.evaluated_get(dg)
    me = ev.to_mesh()
    me.calc_loop_triangles()
    P, N, U, K = [], [], [], []
    for t in me.loop_triangles:
        P.append([me.vertices[v].co[:] for v in t.vertices])
        N.append([me.corner_normals[l].vector[:] for l in t.loops])
        K.append(keys_by_index[me.polygons[t.polygon_index].material_index])
        U.append([uv_fn(me.vertices[v].co) if uv_fn else (0.0, 0.0) for v in t.vertices])
    ev.to_mesh_clear()
    return np.array(P), np.array(N), np.array(U), K


def make_object(bm, keys, subdiv, name):
    names = sorted(set(keys))
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    for k in names:
        me.materials.append(bpy.data.materials.get(k) or bpy.data.materials.new(k))
    for poly, k in zip(me.polygons, keys):
        poly.material_index = names.index(k)
        poly.use_smooth = True
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    if subdiv:
        m = ob.modifiers.new('sub', 'SUBSURF')
        m.levels = m.render_levels = subdiv
    return ob, names


def car_shell():
    """Outer shell plus an inner lining just inside it, as corner arrays and keys."""
    bm, keys = cage()
    ob, names = make_object(bm, keys, 2, 'car_shell_tmp')
    P, N, U, K = to_corners(ob, names, lambda co: (co.z, co.y))
    # Lining: the same surface moved 8 mm inward along its normals, faces turned around: headliner above the belt,
    # dark trim below.
    Pi = P - N * 0.008
    Pi = Pi[:, ::-1]
    Ni = -N[:, ::-1]
    Ui = U[:, ::-1]
    # No lining behind the glass: one see-through layer is enough (two would double the translucent cost).
    solid = np.array([k != GLASS for k in K], dtype=bool)
    Pi, Ni, Ui = Pi[solid], Ni[solid], Ui[solid]
    Ki = [LINER if tri[:, 1].mean() > 1.3 else DASH for tri in Pi]
    bpy.data.objects.remove(ob)
    parts = [(P, N, U, K), (Pi, Ni, Ui, Ki)]
    parts += [mirror_part(s) for s in (-1, 1)]
    parts += [plates()]
    return concat(parts)


def concat(parts):
    P = np.concatenate([p[0] for p in parts]); N = np.concatenate([p[1] for p in parts])
    U = np.concatenate([p[2] for p in parts]); K = sum([list(p[3]) for p in parts], [])
    return P, N, U, K


def box_part(c, size, key, subdiv=0, bevel=0.0):
    """A box centred at c (web frame) with full sizes, optionally with rounded (bevelled) edges."""
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    for v in bm.verts:
        v.co.x = v.co.x * size[0] + c[0]; v.co.y = v.co.y * size[1] + c[1]; v.co.z = v.co.z * size[2] + c[2]
    if bevel:
        bmesh.ops.bevel(bm, geom=list(bm.edges), offset=bevel, segments=3, profile=0.5, affect='EDGES')
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    ob, names = make_object(bm, [key] * len(bm.faces), subdiv, 'box_tmp')
    out = to_corners(ob, names, lambda co: (co.x, co.y))
    bpy.data.objects.remove(ob)
    return out


def mirror_part(side):
    return box_part((side * 0.97, 1.0, -0.55), (0.1, 0.11, 0.17), PAINT, subdiv=2)


def plates():
    """Bumpers, grille, lights, number plates and door handles at the web car's own places (game-vehicles.js)."""
    parts = [box_part((0.0, 0.42, -2.06), (1.8, 0.22, 0.18), TRIM, bevel=0.05),         # bumpers
             box_part((0.0, 0.42, 2.03), (1.8, 0.22, 0.18), TRIM, bevel=0.05),
             box_part((0.0, 0.66, -2.08), (0.62, 0.12, 0.06), TRIM, bevel=0.02),         # grille
             box_part((0.0, 0.47, -2.165), (0.52, 0.11, 0.012), PLATE),
             box_part((0.0, 0.62, 2.12), (0.52, 0.11, 0.012), PLATE)]
    for s in (-1, 1):
        parts += [box_part((s * 0.62, 0.71, -2.06), (0.36, 0.12, 0.09), HEAD, bevel=0.03),
                  box_part((s * 0.68, 0.82, 2.04), (0.32, 0.16, 0.08), TAIL, bevel=0.03),
                  box_part((s * 0.875, 0.86, 0.1), (0.02, 0.03, 0.16), CHROME, bevel=0.008)]
    return concat(parts)


def lathe(profile, segments, key):
    """Closed surface of revolution around the x axis from a closed (x, r) cross-section (counter-clockwise or
    not: the normals are made to point outward)."""
    bm = bmesh.new()
    vs = [bm.verts.new((x, r, 0.0)) for x, r in profile]
    edges = [bm.edges.new((vs[i], vs[(i + 1) % len(vs)])) for i in range(len(vs))]
    bmesh.ops.spin(bm, geom=vs + edges, cent=(0, 0, 0), axis=(1, 0, 0), angle=2 * math.pi, steps=segments, use_merge=True)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    ob, names = make_object(bm, [key] * len(bm.faces), 0, 'lathe_tmp')
    out = to_corners(ob, names)
    bpy.data.objects.remove(ob)
    return out


def car_wheel():
    """Tyre (rounded section, 0.31 m radius, 0.2 m wide) and a five-spoke alloy rim; axle along x (the web
    wheel's frame), outer face toward +x (mirrored for the left wheels by build_assets)."""
    R, W = WHEEL_R, 0.2
    tyre = [(-W / 2 + 0.02, 0.2), (-W / 2, 0.23), (-W / 2 - 0.005, 0.27), (-W / 2 + 0.01, 0.298), (-W / 2 + 0.035, R),
            (W / 2 - 0.035, R), (W / 2 - 0.01, 0.298), (W / 2 + 0.005, 0.27), (W / 2, 0.23), (W / 2 - 0.02, 0.2)]
    rim = [(W / 2 - 0.015, 0.21), (W / 2 - 0.015, 0.18), (-W / 2 + 0.02, 0.18), (-W / 2 + 0.02, 0.205), (-W / 2 + 0.012, 0.212)]
    hub = [(W / 2 - 0.06, 0.0), (W / 2 - 0.06, 0.055), (W / 2 - 0.035, 0.065), (W / 2 - 0.03, 0.0)]
    parts = [lathe(tyre, 36, TYRE), lathe(rim, 36, RIM), lathe(hub, 16, RIM)]
    # Five spokes from the hub to the rim, dished toward the outer face.
    for i in range(5):
        a = 2 * math.pi * i / 5
        bm = bmesh.new()
        bmesh.ops.create_cube(bm, size=1.0)
        for v in bm.verts:
            x, y, z = v.co
            t = y + 0.5                                                 # 0 at the hub .. 1 at the rim
            r = 0.05 + t * 0.135
            half_w = 0.03 - t * 0.012
            off = z * half_w * 2
            xx = W / 2 - 0.045 - t * 0.02 + x * 0.026
            v.co = (xx, r * math.cos(a) - off * math.sin(a), r * math.sin(a) + off * math.cos(a))
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
        ob, names = make_object(bm, [RIM] * 6, 0, 'spoke_tmp')
        parts.append(to_corners(ob, names))
        bpy.data.objects.remove(ob)
    return concat(parts)
