"""A real car for the car rig: a VW Golf Mk7-style five-door hatchback, used by build_assets.py.

The web's car (game-vehicles.js class Car) is a 4.2 m hatchback, 1.8 m wide and 1.45 m high, with 0.31 m wheels at
z = +-1.3 and x = +-0.78. Those are almost exactly the sizes of a seventh-generation VW Golf (4.26 x 1.80 x 1.45 m,
2.64 m wheelbase, 205/55 R16 tyres), the most common hatchback on Oslo's streets, so the body is a Golf Mk7 in the
web car's own frame (x right, y up, ground at y = 0, front at z = -2.14, rear at z = 2.075); the web's sizes, wheel
places, seats, dashboard, steering wheel and hands are kept (only the look changes; the user asked for real cars on
2 Nov 2026).

How it is built:
  - The body is a cage of cross-sections along the car (STATIONS), each from the floor up the right side to the roof
    centre (ring()): floor, wheel-arch liners, sill, door, the sharp shoulder line ("tornado line"), the belt, the
    side glass leaning in, roof rail and roof (or bonnet / tailgate where there is no glasshouse), from the Golf's
    side profile, plan and section curves. Two levels of Catmull-Clark subdivision make the surface.
  - Glass, window seals, the black B pillar, the windscreen frit, sills and arch liners are chosen per final
    triangle (classify()), so their edges follow the subdivided surface.
  - Lights, grille, badges, intakes, reflectors and the panel gaps are thin "decals": outlines in a front, rear or
    side view, cut into small triangles and projected onto the body (BVH ray casts), 1.5-3 mm above it, so their
    edges are crisp at any distance without fighting the paint.
  - Mirrors, door handles, wipers and Norwegian number plates are small separate parts.
  - An inner lining (headliner, dark trim) 8 mm inside the shell makes it solid from the driver's seat.
  - Wheels: 205/55 R16 tyres with tread grooves and a ten-spoke (five pairs) alloy rim, brake disc, bolts.

Functions return corner arrays like build_assets.gather(): pos/nor (T x 3 x 3, web frame), uv (T x 3 x 2), and
material keys per triangle. NEW_KEYS are materials this file adds (made by tools/unreal/materials_cars.py).
"""
import math

import bmesh
import bpy
import numpy as np
from mathutils import Vector
from mathutils import geometry as mgeo
from mathutils.bvhtree import BVHTree

# The web car's material keys (game-vehicles.js Car): paint, glass, black trim, lights, plate, handles, interior.
PAINT, GLASS, TRIM = 'MI_Physical_111214_365', 'MI_Physical_1d2328_366', 'MI_polymer_141516'
HEAD, TAIL, PLATE, CHROME = 'MI_Standard_e8eef2_368', 'MI_Standard_a00000_369', 'MI_Standard_f2f2ee_373', 'MI_chrome'
LINER, DASH = 'MI_fabric_8d8a84', 'MI_polymer_1e1f21'
TYRE, RIM = 'MI_Standard_151515_360', 'MI_metal_a8adb2'
# Materials added for the real car (tools/unreal/materials_cars.py makes them).
LAMP, DRL, TAIL_DARK, AMBER = 'MI_HK_CarHeadlight', 'MI_HK_CarDRL', 'MI_HK_CarTailDark', 'MI_HK_CarAmber'
GLOSS, RUBBER, GAP, MIRROR = 'MI_HK_CarGlossBlack', 'MI_HK_CarRubber', 'MI_HK_CarGap', 'MI_HK_CarMirror'
NUMBER, GRILLE = 'MI_HK_CarPlate', 'MI_HK_CarGrille'
WTYRE, WRIM, WDISC = 'MI_HK_CarTyre', 'MI_HK_CarRim', 'MI_HK_CarDisc'
NEW_KEYS = [LAMP, DRL, TAIL_DARK, AMBER, GLOSS, RUBBER, GAP, MIRROR, NUMBER, GRILLE, WTYRE, WRIM, WDISC]

WHEEL_Z, WHEEL_R, ARCH_R = 1.3, 0.31, 0.375


def curve(points):
    zs, ys = zip(*points)
    return lambda z: float(np.interp(z, zs, ys))


# ---------- the Golf Mk7's lines (web car frame, metres) ----------
# Centre line from the bumper nose over the bonnet, windscreen, roof, spoiler, rear window and tailgate.
TOP = curve([(-2.14, 0.47), (-2.135, 0.53), (-2.125, 0.59), (-2.11, 0.645), (-2.09, 0.70), (-2.065, 0.745),
             (-2.035, 0.778), (-2.0, 0.798), (-1.95, 0.812), (-1.80, 0.840), (-1.50, 0.885), (-1.20, 0.920), (-0.98, 0.945),
             (-0.90, 0.958), (-0.86, 0.985), (-0.60, 1.13), (-0.35, 1.262), (-0.15, 1.36), (-0.05, 1.397),
             (0.08, 1.428), (0.30, 1.447), (0.55, 1.452), (0.90, 1.445), (1.20, 1.428), (1.42, 1.412), (1.54, 1.400),
             (1.60, 1.368), (1.70, 1.262), (1.80, 1.150), (1.88, 1.055), (1.94, 1.005), (1.99, 0.955), (2.03, 0.90),
             (2.055, 0.81), (2.07, 0.71), (2.075, 0.61)])
# Floor / lower edge of the bumpers.
FLOOR = curve([(-2.14, 0.38), (-2.135, 0.31), (-2.125, 0.275), (-2.11, 0.25), (-2.08, 0.232), (-2.03, 0.215), (-1.90, 0.20),
               (1.90, 0.205), (2.0, 0.225), (2.05, 0.255), (2.07, 0.30), (2.075, 0.40)])
# Plan: half width of the body at the shoulder.
WIDTH = curve([(-2.14, 0.56), (-2.13, 0.64), (-2.115, 0.70), (-2.10, 0.74), (-2.08, 0.775), (-2.055, 0.805),
               (-2.02, 0.833), (-1.97, 0.856), (-1.90, 0.873), (-1.75, 0.889), (-1.50, 0.898), (-1.00, 0.900),
               (1.20, 0.900), (1.60, 0.896), (1.80, 0.884), (1.94, 0.862), (2.01, 0.838), (2.045, 0.808), (2.065, 0.76),
               (2.075, 0.69)])
# Shoulder line (the sharp crease from the headlight to the tail light, just above the door handles).
SHOULDER = curve([(-2.14, 0.66), (-2.06, 0.750), (-1.97, 0.790), (-1.70, 0.825), (-1.30, 0.855), (-0.90, 0.878),
                  (0.0, 0.900), (1.0, 0.915), (1.60, 0.928), (1.90, 0.935), (2.075, 0.90)])
# Belt: bottom edge of the side windows.
BELT = curve([(-0.92, 0.985), (-0.30, 0.995), (0.50, 1.008), (1.30, 1.025), (1.95, 1.03)])
# The top of the side: A pillar, roof rail, the spoiler's side and the tailgate's D pillar.
RAIL = curve([(-0.92, 0.985), (-0.86, 1.005), (-0.50, 1.175), (-0.10, 1.358), (0.05, 1.381), (0.50, 1.400),
              (1.0, 1.394), (1.42, 1.370), (1.60, 1.325), (1.75, 1.190), (1.88, 1.070), (1.95, 1.020)])
RAIL_W = curve([(-0.92, 0.850), (-0.50, 0.780), (-0.06, 0.665), (0.40, 0.675), (1.20, 0.670), (1.40, 0.655),
                (1.60, 0.628), (1.75, 0.605), (1.90, 0.625)])
B_PILLAR = (0.215, 0.305)
GLASS_Z = (-0.92, 1.95)          # where the cross-sections have a glasshouse (windscreen base .. tailgate glass foot)
MIRROR_AT = (-0.72, 0.985)       # door mirror base (z, y) at the side window's front corner
HANDLE_Z = (-0.06, 0.98)         # door handles
WIPER_Z = -0.835                 # wipers parked at the foot of the windscreen
PLATE_Y = (0.548, 0.745)         # number plate centres, front and rear


def arch_height(z):
    """Top of the wheel-arch opening at z (side view), or None outside the arches."""
    for wz in (-WHEEL_Z, WHEEL_Z):
        d = abs(z - wz)
        if d < ARCH_R:
            return WHEEL_R + math.sqrt(ARCH_R * ARCH_R - d * d)
    return None


def ring(z):
    """Half cross-section at z: 19 (x, y) points from the floor centre up the right side to the top centre."""
    W, yb, top = WIDTH(z), FLOOR(z), TOP(z)
    ysh = min(SHOULDER(z), top - 0.03)                                  # near the nose and tail the top comes down
    ya = arch_height(z)
    if ya is None:
        low = [(0.0, yb), (min(0.42, W * 0.5), yb), (min(0.62, W * 0.7), yb + 0.004), (min(0.76, W * 0.85), yb + 0.010),
               (W * 0.95, max(yb + 0.02, 0.232)), (W * 0.985, max(yb + 0.045, 0.262))]
        widen = 1.0
    else:
        # Arch: the liner goes in from the lip, up and over the tyre, then down to the floor beside the wheel.
        low = [(0.0, yb), (0.42, yb), (0.555, yb + 0.004), (0.575, ya + 0.012), (W * 0.93, ya + 0.022), (W * 0.992, ya)]
        widen = 1.006                                                   # the wings flare a little over the wheels
    y5 = low[-1][1]
    y6 = min(max(0.40, y5 + 0.03), y5 + (ysh - y5) * 0.30)
    y7 = min(max(0.60, y6 + 0.03), y5 + (ysh - y5) * 0.62)
    y8 = min(max(ysh - 0.055, y7 + 0.01), ysh - 0.004)
    side = [(W * 0.994 * widen, y6), (W * 0.99 * widen, y7), (W * 0.997, y8), (W, ysh), (W * 0.993, ysh + 0.012)]
    glass = max(0.0, RAIL(z) - BELT(z)) if GLASS_Z[0] <= z <= GLASS_Z[1] else 0.0
    t = min(1.0, glass / 0.22)
    yr = RAIL(z) if t > 0 else ysh + 0.04
    wr = RAIL_W(z) if t > 0 else W * 0.84
    yblt = BELT(z) if t > 0 else ysh + 0.03
    # Bonnet / tailgate deck: a crowned panel from the wing tops to the centre line.
    wing = min(ysh + 0.042, top - 0.004)
    rise = top - wing
    deck = [(W * 0.972, ysh + (wing - ysh) * 0.5), (W * 0.945, ysh + (wing - ysh) * 0.78), (W * 0.90, ysh + (wing - ysh) * 0.93),
            (W * 0.84, wing), (W * 0.68, wing + rise * 0.52), (W * 0.46, wing + rise * 0.80), (W * 0.23, wing + rise * 0.95),
            (0.0, top)]
    # Glasshouse: belt ledge, glass leaning in with a little outward bulge, roof rail, roof (or windscreen / rear
    # window across the top).
    span = top - yr
    house = [(W - 0.028, yblt - 0.012), (W - 0.040, yblt + 0.006), ((W - 0.040 + wr) / 2 + 0.012, (yblt + yr) / 2),
             (wr, yr), (wr - 0.045, yr + span * 0.60 + 0.012), (wr * 0.66, yr + span * 0.88 + 0.006),
             (wr * 0.33, top + 0.001), (0.0, top + 0.002)]
    upper = [(d[0] + (h[0] - d[0]) * t, d[1] + (h[1] - d[1]) * t) for d, h in zip(deck, house)]
    pts = low + side + upper
    # Heights never go down the side (2 mm steps at least); squeeze into the room under the top if needed.
    ys = [p[1] for p in pts[5:]]
    for i in range(1, len(ys) - 1):
        ys[i] = max(ys[i], ys[i - 1] + 0.002) if i < 7 else ys[i]
    lo, hi = pts[4][1] + 0.004, top + 0.002
    if min(ys) < lo or max(ys) > hi:
        a, b = min(ys), max(ys)
        ys = [lo + (y - a) * (hi - lo) / max(b - a, 1e-6) for y in ys]
    return pts[:5] + [(p[0], y) for p, y in zip(pts[5:], ys)]


STATIONS = [-2.14, -2.135, -2.125, -2.11, -2.09, -2.065, -2.035, -2.0, -1.95, -1.90, -1.82, -1.745, -1.69, -1.665, -1.60,
            -1.50, -1.40, -1.30, -1.20, -1.10, -1.00, -0.94, -0.92, -0.88, -0.80, -0.68, -0.55, -0.40, -0.25, -0.10,
            0.03, 0.15, 0.215, 0.305, 0.45, 0.60, 0.75, 0.86, 0.91, 0.935, 1.00, 1.10, 1.20, 1.30, 1.40, 1.50, 1.60,
            1.665, 1.69, 1.745, 1.81, 1.88, 1.94, 1.99, 2.03, 2.055, 2.07, 2.075]


# ---------- Blender helpers ----------
def make_object(bm, keys, subdiv, name, smooth=True):
    names = sorted(set(keys))
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    for k in names:
        me.materials.append(bpy.data.materials.get(k) or bpy.data.materials.new(k))
    for poly, k in zip(me.polygons, keys):
        poly.material_index = names.index(k)
        poly.use_smooth = smooth
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    if subdiv:
        m = ob.modifiers.new('sub', 'SUBSURF')
        m.levels = m.render_levels = subdiv
    return ob, names


def to_corners(ob, keys_by_index, uv_fn=None):
    """Evaluated (modified) Blender object -> corner arrays in the web frame (the object was built in web xyz)."""
    dg = bpy.context.evaluated_depsgraph_get()
    ev = ob.evaluated_get(dg)
    me = ev.to_mesh()
    me.calc_loop_triangles()
    n = len(me.loop_triangles)
    vi = np.zeros(n * 3, dtype=np.int64); me.loop_triangles.foreach_get('vertices', vi)
    li = np.zeros(n * 3, dtype=np.int64); me.loop_triangles.foreach_get('loops', li)
    pi = np.zeros(n, dtype=np.int64); me.loop_triangles.foreach_get('polygon_index', pi)
    co = np.zeros(len(me.vertices) * 3); me.vertices.foreach_get('co', co); co = co.reshape(-1, 3)
    cn = np.zeros(len(me.loops) * 3); me.corner_normals.foreach_get('vector', cn); cn = cn.reshape(-1, 3)
    mi = np.zeros(len(me.polygons), dtype=np.int64); me.polygons.foreach_get('material_index', mi)
    P = co[vi].reshape(n, 3, 3)
    N = cn[li].reshape(n, 3, 3)
    K = [keys_by_index[k] for k in mi[pi]]
    U = np.array([[uv_fn(p) for p in tri] for tri in P]) if uv_fn else np.zeros((n, 3, 2))
    ev.to_mesh_clear()
    return P, N, U, K


def concat(parts):
    parts = [p for p in parts if len(p[3])]
    P = np.concatenate([p[0] for p in parts]); N = np.concatenate([p[1] for p in parts])
    U = np.concatenate([p[2] for p in parts]); K = sum([list(p[3]) for p in parts], [])
    return P, N, U, K


# ---------- body ----------
def cage():
    bm = bmesh.new()
    rows = []
    for z in STATIONS:
        half = ring(z)
        pts = half + [(-x, y) for (x, y) in reversed(half[1:-1])]
        rows.append([bm.verts.new((x, y, z)) for (x, y) in pts])
    n = len(rows[0])
    for a, b in zip(rows[:-1], rows[1:]):
        for i in range(n):
            j = (i + 1) % n
            bm.faces.new((a[i], a[j], b[j], b[i]))
    bm.faces.new(list(reversed(rows[0])))
    bm.faces.new(rows[-1])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return bm


def side_plane(z0, y0, z1, y1, inside):
    """Half-space bounded by the line (z0, y0)-(z1, y1) of the side view, swept across the car (along x); the normal
    points toward the inside point (z, y)."""
    n = np.array([0.0, -(z1 - z0), y1 - y0])
    n /= np.linalg.norm(n)
    p0 = np.array([0.0, y0, z0])
    if np.dot(np.array([0.0, inside[1], inside[0]]) - p0, n) < 0:
        n = -n
    return p0, n


def plane_3d(a, b, along, inside):
    """Half-space bounded by the plane through the line a-b that contains the direction 'along'."""
    a, b = np.array(a, float), np.array(b, float)
    n = np.cross(b - a, np.array(along, float))
    n /= np.linalg.norm(n)
    if np.dot(np.array(inside, float) - a, n) < 0:
        n = -n
    return a, n


def windows():
    """Every window: the half-spaces bounding its glass (glass where all are positive), a test for the faces near it
    (the cuts only touch those), and the bands around it: seal (rubber, outside the glass) and frit (black ceramic,
    inside the glass edge of the windscreen and rear window)."""
    out = []
    for s in (1, -1):
        belt = side_plane(-0.85, BELT(-0.85) + 0.012, 1.36, BELT(1.36) + 0.012, (0.3, 1.2))
        top = [side_plane(za, RAIL(za) - 0.012, zb, RAIL(zb) - 0.012, (0.3, 1.0)) for za, zb in ((-0.12, 0.05), (0.05, 0.5), (0.5, 1.07))]
        front = side_plane(-0.80, 0.99, -0.11, 1.37, (0.3, 1.1))
        rear = side_plane(1.315, 1.03, 1.06, 1.38, (0.3, 1.1))
        near = (lambda s: lambda c, n: c[0] * s > 0.55 and -0.9 < c[2] < 1.4 and 0.94 < c[1] < 1.43 and n[0] * s > 0.3)(s)
        for zone in ((-1.0, B_PILLAR[0]), (B_PILLAR[1], 2.0)):
            cut = [(np.array([0.0, 0.0, zone[0]]), np.array([0.0, 0.0, 1.0])), (np.array([0.0, 0.0, zone[1]]), np.array([0.0, 0.0, -1.0]))]
            out.append({'kind': 'side', 'planes': [belt] + top + [front, rear] + cut, 'near': near, 'seal': 0.02, 'frit': 0.0})
    # Windscreen: between the A pillars, above the cowl, below the roof header.
    ws_n = (0.0, 0.80, -0.60)
    planes = [(np.array([0.0, 0.99, 0.0]), np.array([0.0, 1.0, 0.0])), (np.array([0.0, 0.0, -0.115]), np.array([0.0, 0.0, -1.0]))]
    for s in (1, -1):
        planes.append(plane_3d((s * (RAIL_W(-0.83) - 0.055), RAIL(-0.83), -0.83), (s * (RAIL_W(-0.12) - 0.055), RAIL(-0.12), -0.12), ws_n, (0.0, 1.2, -0.5)))
    out.append({'kind': 'front', 'planes': planes, 'seal': 0.02, 'frit': 0.045,
                'near': lambda c, n: -0.95 < c[2] < -0.05 and c[1] > 0.95 and abs(c[0]) < 0.84 and n[1] > 0.15 and n[2] < -0.05})
    # Rear window: under the spoiler, above the tailgate, between the D pillars.
    rw_n = (0.0, 0.65, 0.76)
    planes = [(np.array([0.0, 0.0, 1.592]), np.array([0.0, 0.0, 1.0])), (np.array([0.0, 1.075, 0.0]), np.array([0.0, 1.0, 0.0]))]
    for s in (1, -1):
        planes.append(plane_3d((s * 0.565, 1.36, 1.592), (s * 0.525, 1.075, 1.885), rw_n, (0.0, 1.2, 1.75)))
    out.append({'kind': 'rear', 'planes': planes, 'seal': 0.02, 'frit': 0.035,
                'near': lambda c, n: 1.52 < c[2] < 1.97 and c[1] > 1.02 and abs(c[0]) < 0.66 and n[2] > 0.15})
    return out


WINDOWS = []


def inside(win, c, grow=0.0):
    return all(np.dot(c - p0, n) > -grow for p0, n in win['planes'])


def classify(c, n):
    x, y, z = abs(c[0]), c[1], c[2]
    W = WIDTH(z)
    ya = arch_height(z)
    if y < FLOOR(z) + 0.025 and x < W * 0.93 and n[1] < -0.3:
        return TRIM                                                     # underside
    if ya is not None and x < W * 0.955 and y < ya + 0.035 and y > FLOOR(z) + 0.004:
        return TRIM                                                     # wheel-arch liner
    for win in WINDOWS:
        if win['near'](c, n) and inside(win, c):
            # The windscreen and rear window have a black ceramic band (frit) along their edges.
            return GLOSS if win['frit'] and not inside(win, c, -win['frit']) else GLASS
    if B_PILLAR[0] < z < B_PILLAR[1] and abs(n[0]) > 0.3 and x > 0.55 and BELT(z) + 0.012 < y < RAIL(z) - 0.012:
        return GLOSS                                                    # B pillar
    for win in WINDOWS:
        if win['near'](c, n) and inside(win, c, win['seal']):
            return RUBBER                                               # window seals
    if y < 0.258 and abs(n[0]) > 0.3 and -1.9 < z < 1.85:
        return TRIM                                                     # sill trim
    return PAINT


def cut_windows(bm):
    """Cut the subdivided body along every window edge (and its seal and frit bands, the B pillars and the sill
    line), so the glass and trim boundaries are straight, clean edges."""
    def cut(co, no, near):
        faces = [f for f in bm.faces if near(np.array(f.verts[0].co[:]), np.array(f.normal[:]))]
        if not faces:
            return
        geom = list({v for f in faces for v in f.verts}) + list({e for f in faces for e in f.edges}) + faces
        bmesh.ops.bisect_plane(bm, geom=geom, dist=1e-6, plane_co=Vector(tuple(co)), plane_no=Vector(tuple(no)))
        bm.normal_update()

    offsets = (np.zeros(3), np.array([0, 0.06, 0]), np.array([0, -0.06, 0]), np.array([0, 0, 0.06]), np.array([0, 0, -0.06]))
    for win in WINDOWS:
        wide = (lambda w: lambda c, n: any(w['near'](c + d, n) for d in offsets))(win)
        for p0, nrm in win['planes']:
            for g in ((0.0, win['seal'], -win['frit']) if win['frit'] else (0.0, win['seal'])):
                cut(p0 - nrm * g, nrm, wide)
    for z in B_PILLAR:
        cut((0.0, 0.0, z), (0.0, 0.0, 1.0), lambda c, n: abs(c[0]) > 0.55 and 0.9 < c[1] < 1.55 and abs(n[0]) > 0.2)
    cut((0.0, 0.258, 0.0), (0.0, 1.0, 0.0), lambda c, n: abs(n[0]) > 0.2 and 0.2 < c[1] < 0.32)


def body_mesh(level):
    global WINDOWS
    WINDOWS = windows()
    ob, names = make_object(cage(), [PAINT], level, 'car_shell_tmp')
    dg = bpy.context.evaluated_depsgraph_get()
    me = bpy.data.meshes.new_from_object(ob.evaluated_get(dg))
    bpy.data.objects.remove(ob)
    bm = bmesh.new()
    bm.from_mesh(me)
    bpy.data.meshes.remove(me)
    bm.normal_update()
    cut_windows(bm)
    bmesh.ops.triangulate(bm, faces=bm.faces)
    bm.normal_update()
    bm.verts.ensure_lookup_table()
    co = np.array([v.co[:] for v in bm.verts]); vn = np.array([v.normal[:] for v in bm.verts])
    tri = np.array([[v.index for v in f.verts] for f in bm.faces])
    bm.free()
    P, N = co[tri], vn[tri]
    U = np.stack([P[:, :, 2], P[:, :, 1]], axis=2)
    C = P.mean(axis=1)
    Nf = np.cross(P[:, 1] - P[:, 0], P[:, 2] - P[:, 0])
    Nf /= np.maximum(np.linalg.norm(Nf, axis=1, keepdims=True), 1e-12)
    K = [classify(c, nn) for c, nn in zip(C, Nf)]
    return P, N, U, K


# ---------- decals (outlines projected onto the body) ----------
class Projector:
    def __init__(self, P):
        verts = P.reshape(-1, 3)
        tris = np.arange(len(verts)).reshape(-1, 3)
        self.tree = BVHTree.FromPolygons([Vector(v) for v in verts], tris.tolist(), epsilon=0.0)

    def hit(self, origin, direction):
        loc, nor, _, _ = self.tree.ray_cast(Vector(origin), Vector(direction))
        return loc, nor


def fill(outline, max_edge=0.025):
    """Triangles filling a closed 2D outline, split until no edge is longer than max_edge (to follow the curved
    body once projected). Returns a T x 3 x 2 array."""
    pts = [Vector((p[0], p[1], 0.0)) for p in outline]
    tris = mgeo.tessellate_polygon([pts])
    out = [np.array([outline[a], outline[b], outline[c]], float) for a, b, c in tris]
    done = []
    while out:
        t = out.pop()
        e = [np.linalg.norm(t[1] - t[0]), np.linalg.norm(t[2] - t[1]), np.linalg.norm(t[0] - t[2])]
        if max(e) <= max_edge:
            done.append(t)
            continue
        a, b, c = t
        ab, bc, ca = (a + b) / 2, (b + c) / 2, (c + a) / 2
        out += [np.array([a, ab, ca]), np.array([ab, b, bc]), np.array([ca, bc, c]), np.array([ab, bc, ca])]
    return np.array(done)


def circle(cx, cy, r, n=24):
    return [(cx + r * math.cos(2 * math.pi * i / n), cy + r * math.sin(2 * math.pi * i / n)) for i in range(n)]


def strip(points, width):
    """Outline of a band of this width along a 2D polyline."""
    p = np.array(points, float)
    left, right = [], []
    for i in range(len(p)):
        d = p[min(i + 1, len(p) - 1)] - p[max(i - 1, 0)]
        d /= max(np.linalg.norm(d), 1e-9)
        nrm = np.array([-d[1], d[0]]) * width / 2
        left.append(p[i] + nrm); right.append(p[i] - nrm)
    return [tuple(q) for q in left + right[::-1]]


def mirror_x(outline):
    return [(-x, y) for x, y in reversed(outline)]


def decal(proj, outline, view, key, lift=0.0018, max_edge=0.03):
    """Project a 2D outline onto the body. view: 'front' (x, y seen from ahead), 'rear' (x, y from behind), 'right' /
    'left' (z, y from that side). uv = the outline's own 0..1 box."""
    T = fill(outline, max_edge)
    o = np.array(outline, float)
    lo, hi = o.min(axis=0), np.maximum(o.max(axis=0), o.min(axis=0) + 1e-6)
    P, N, U = [], [], []
    for tri in T:
        ps, ns = [], []
        for a, b in tri:
            if view == 'front':
                origin, dirn = (a, b, -3.0), (0, 0, 1)
            elif view == 'rear':
                origin, dirn = (a, b, 3.0), (0, 0, -1)
            elif view == 'right':
                origin, dirn = (2.0, b, a), (-1, 0, 0)
            else:
                origin, dirn = (-2.0, b, a), (1, 0, 0)
            loc, nor = proj.hit(origin, dirn)
            if loc is None:
                break
            if nor.dot(Vector(dirn)) > 0:
                nor = -nor
            ps.append(np.array(loc) + np.array(nor) * lift)
            ns.append(np.array(nor))
        if len(ps) < 3:
            continue
        P.append(ps); N.append(ns)
        U.append([((a - lo[0]) / (hi[0] - lo[0]), (b - lo[1]) / (hi[1] - lo[1])) for a, b in tri])
    if not P:
        return np.zeros((0, 3, 3)), np.zeros((0, 3, 3)), np.zeros((0, 3, 2)), []
    P, N = np.array(P), np.array(N)
    # Face the triangles outward (along their projected normal).
    fn = np.cross(P[:, 1] - P[:, 0], P[:, 2] - P[:, 0])
    flip = (fn * N.mean(axis=1)).sum(axis=1) < 0
    P[flip] = P[flip][:, ::-1]; N[flip] = N[flip][:, ::-1]
    U = np.array(U); U[flip] = U[flip][:, ::-1]
    return P, N, U, [key] * len(P)


HEADLIGHT = [(0.395, 0.712), (0.395, 0.788), (0.60, 0.803), (0.76, 0.815), (0.80, 0.814), (0.80, 0.736),
             (0.76, 0.729), (0.62, 0.715)]
TAILLIGHT = [(0.37, 0.914), (0.37, 0.986), (0.60, 1.006), (0.78, 1.016), (0.80, 1.015), (0.80, 0.908),
             (0.78, 0.906), (0.60, 0.902)]
# The ends of the lights round the corners, seen from the side (z, y).
HEAD_WRAP = [(-2.075, 0.736), (-1.995, 0.748), (-1.955, 0.775), (-1.975, 0.806), (-2.075, 0.814)]
TAIL_WRAP = [(1.94, 0.936), (2.0, 0.912), (2.055, 0.912), (2.055, 1.012), (2.0, 1.018), (1.94, 1.012)]


def decals(proj, level):
    """Front, rear and side details. level 0 = everything; 1 = no panel gaps; 2 = only lights and grilles."""
    parts = []
    add = lambda *a, **k: parts.append(decal(proj, *a, **k))
    for s in (1, -1):
        m = (lambda o: o) if s == 1 else mirror_x
        add(m(HEADLIGHT), 'front', LAMP)
        add(m(TAILLIGHT), 'rear', TAIL)
        side = 'right' if s == 1 else 'left'
        add(HEAD_WRAP, side, LAMP)
        add(TAIL_WRAP, side, TAIL)
        if level < 2:
            # Headlight inside: projector lens, LED daytime strip along the lower edge, amber indicator outboard.
            add(m(circle(0.535, 0.758, 0.03, 20)), 'front', CHROME, lift=0.0032)
            add(m(strip([(0.42, 0.722), (0.62, 0.726), (0.79, 0.738)], 0.008)), 'front', DRL, lift=0.0032)
            add(m([(0.74, 0.784), (0.79, 0.789), (0.79, 0.806), (0.74, 0.803)]), 'front', AMBER, lift=0.0032)
            # Tail light: a smoked lower band with the bright L-shaped signature above it.
            add(m([(0.40, 0.920), (0.79, 0.925), (0.79, 0.955), (0.40, 0.952)]), 'rear', TAIL_DARK, lift=0.0032)
            add(m(strip([(0.42, 0.972), (0.78, 0.990)], 0.006)), 'rear', DRL, lift=0.0034)
            # Fog lights in the front side intakes, reflectors in the rear bumper.
            add(m(circle(0.70, 0.362, 0.034, 20)), 'front', LAMP, lift=0.0032)
            add(m([(0.62, 0.395), (0.82, 0.392), (0.82, 0.418), (0.62, 0.420)]), 'rear', TAIL_DARK, lift=0.0025)
        add(m([(0.55, 0.302), (0.79, 0.312), (0.815, 0.425), (0.585, 0.428)]), 'front', GRILLE)
    # Upper grille between the headlights with its chrome bar, the lower intake, the rear diffuser.
    add([(-0.395, 0.714), (0.395, 0.714), (0.395, 0.787), (0.0, 0.794), (-0.395, 0.787)], 'front', GLOSS)
    add([(-0.395, 0.722), (0.395, 0.722), (0.395, 0.731), (-0.395, 0.731)], 'front', CHROME, lift=0.0032)
    add([(-0.46, 0.300), (0.46, 0.300), (0.41, 0.442), (-0.41, 0.442)], 'front', GRILLE)
    add([(-0.62, 0.275), (0.62, 0.275), (0.60, 0.33), (-0.60, 0.33)], 'rear', TRIM, max_edge=0.02)
    if level < 2:
        # Round badges (a chrome ring around a dark disc, no maker's logo) front and back.
        for view, cy, r in (('front', 0.758, 0.062), ('rear', 0.966, 0.056)):
            add(circle(0.0, cy, r, 28), view, CHROME, lift=0.0032)
            add(circle(0.0, cy, r * 0.84, 28), view, GLOSS, lift=0.0045)
            add(strip([(-r * 0.45, cy + r * 0.35), (0, cy - r * 0.55), (r * 0.45, cy + r * 0.35)], 0.009), view, CHROME, lift=0.0058)
        # Third brake light under the spoiler, rear wiper.
        add([(-0.22, 1.37), (0.22, 1.37), (0.22, 1.385), (-0.22, 1.385)], 'rear', TAIL_DARK, lift=0.0025)
    if level < 1:
        # Panel gaps: front door front edge, between the doors, rear door around the wheel arch, fuel flap,
        # the tailgate's cut.
        for side in ('right', 'left'):
            for line in ([(-0.832, 0.992), (-0.842, 0.80), (-0.856, 0.65), (-0.872, 0.45), (-0.878, 0.27)],
                         [(0.258, 1.01), (0.261, 0.70), (0.265, 0.27)],
                         [(1.19, 1.03), (1.15, 0.89), (1.12, 0.765), (1.05, 0.655), (0.98, 0.575), (0.925, 0.475),
                          (0.898, 0.38), (0.892, 0.27)]):
                add(strip(line, 0.0045), side, GAP, lift=0.0015, max_edge=0.04)
            # Door handles' finger recesses are under the handles (separate parts).
        flap = [(1.50, 0.805), (1.66, 0.805), (1.67, 0.82), (1.67, 0.885), (1.655, 0.90), (1.505, 0.90), (1.49, 0.885), (1.49, 0.82)]
        for a, b in zip(flap, flap[1:] + flap[:1]):
            add(strip([a, b], 0.004), 'right', GAP, lift=0.0015, max_edge=0.04)
        for s in (1, -1):
            m = (lambda o: o) if s == 1 else mirror_x
            add(m(strip([(0.59, 1.36), (0.61, 1.20), (0.635, 1.08), (0.66, 1.018)], 0.0045)), 'rear', GAP, lift=0.0015, max_edge=0.04)
            add(m(strip([(0.37, 0.902), (0.37, 0.70), (0.40, 0.598)], 0.0045)), 'rear', GAP, lift=0.0015, max_edge=0.04)
        add(strip([(-0.40, 0.598), (0.40, 0.598)], 0.0045), 'rear', GAP, lift=0.0015, max_edge=0.04)
    return concat(parts)


# ---------- small parts ----------
def shape(verts, faces, key, subdiv=0, uv_fn=None):
    bm = bmesh.new()
    vs = [bm.verts.new(v) for v in verts]
    for f in faces:
        bm.faces.new([vs[i] for i in f])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    ob, names = make_object(bm, [key] * len(faces), subdiv, 'part_tmp', smooth=subdiv > 0)
    out = to_corners(ob, names, uv_fn)
    bpy.data.objects.remove(ob)
    return out


def box(c, size, key, subdiv=0, uv_fn=None):
    hx, hy, hz = size[0] / 2, size[1] / 2, size[2] / 2
    v = [(c[0] + sx * hx, c[1] + sy * hy, c[2] + sz * hz) for sx in (-1, 1) for sy in (-1, 1) for sz in (-1, 1)]
    f = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
    return shape(v, f, key, subdiv, uv_fn)


def recolour(part, keys_fn):
    P, N, U, K = part
    C = P.mean(axis=1)
    Nf = np.cross(P[:, 1] - P[:, 0], P[:, 2] - P[:, 0])
    Nf /= np.maximum(np.linalg.norm(Nf, axis=1, keepdims=True), 1e-12)
    return P, N, U, [keys_fn(c, n, k) for c, n, k in zip(C, Nf, K)]


def rounded_box(c, size, r, key, taper=None):
    """Box with rounded edges (bevel r); taper(v) may move its corners before the bevel."""
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    for v in bm.verts:
        v.co = Vector((c[0] + v.co.x * size[0], c[1] + v.co.y * size[1], c[2] + v.co.z * size[2]))
        if taper:
            v.co = Vector(taper(v.co))
    bmesh.ops.bevel(bm, geom=list(bm.edges), offset=r, segments=3, profile=0.5, affect='EDGES')
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    ob, names = make_object(bm, [key] * len(bm.faces), 0, 'rbox_tmp')
    out = to_corners(ob, names)
    bpy.data.objects.remove(ob)
    return out


def mirror(s):
    """Door mirror (Golf Mk7: a housing about 21 x 12 x 9 cm on a short arm from the front corner of the side window):
    glass on its back, an amber repeater along its lower outer edge."""
    z0, y0 = MIRROR_AT
    arm = rounded_box((s * 0.885, y0 + 0.012, z0 + 0.03), (0.09, 0.03, 0.08), 0.01, PAINT)
    cx, cy, cz = s * 0.985, y0 + 0.055, z0 + 0.02
    hx, hy, hz = 0.105, 0.06, 0.045

    def taper(v):
        # Narrower and lower toward the front (the side facing the wind), outer end swept back.
        f = (v[2] - cz + hz) / (2 * hz)                                 # 0 at the front .. 1 at the back
        out = (v[0] - cx) * s / hx                                      # -1 inner .. 1 outer
        return (v[0], cy + (v[1] - cy) * (0.75 + 0.25 * f), v[2] + max(0.0, out) * 0.012 * (1 - f))
    housing = rounded_box((cx, cy, cz), (2 * hx, 2 * hy, 2 * hz), 0.03, PAINT, taper)

    def k(c, n, key):
        if n[2] > 0.8 and abs(c[0] - cx) < hx - 0.02 and abs(c[1] - cy) < hy - 0.018:
            return MIRROR
        if n[1] < -0.5 and (c[0] - cx) * s > hx * 0.2:
            return AMBER
        return key
    return concat([arm, recolour(housing, k)])


def handle(s, z, y):
    W = WIDTH(z)
    return box((s * (W + 0.006), y, z), (0.022, 0.03, 0.17), PAINT, 2)


def plate(proj, view, y):
    """Norwegian number plate (520 x 110 mm) standing 6 mm proud of the body, its face mapped 0..1 for the plate
    texture (tools/unreal/materials_cars.py), black edges."""
    z0 = -3.0 if view == 'front' else 3.0
    loc, _ = proj.hit((0.0, y, z0), (0, 0, 1 if view == 'front' else -1))
    zf = loc[2] + (-0.008 if view == 'front' else 0.008)
    zb = loc[2] + (0.02 if view == 'front' else -0.02)
    w, h = 0.52, 0.11
    face = zf
    v = [(-w / 2, y - h / 2, face), (w / 2, y - h / 2, face), (w / 2, y + h / 2, face), (-w / 2, y + h / 2, face),
         (-w / 2, y - h / 2, zb), (w / 2, y - h / 2, zb), (w / 2, y + h / 2, zb), (-w / 2, y + h / 2, zb)]
    f = [(0, 1, 2, 3), (4, 7, 6, 5), (0, 4, 5, 1), (1, 5, 6, 2), (2, 6, 7, 3), (3, 7, 4, 0)]
    # Read from outside: seen from ahead, +x is on the viewer's left.
    part = shape(v, f, TRIM, 0, lambda p: ((w / 2 - p[0]) / w if view == 'front' else (p[0] + w / 2) / w, (p[1] - (y - h / 2)) / h))

    def k(c, n, key):
        return NUMBER if abs(n[2]) > 0.9 and (c[2] - (zf + zb) / 2) * (-1 if view == 'front' else 1) > 0 else key
    return recolour(part, k)


def wipers():
    """Two wiper arms parked along the bottom of the windscreen (left-hand drive), 2 cm above the glass."""
    out = []
    for x0, x1 in ((-0.72, 0.10), (0.06, 0.70)):
        pts = []
        for x in (x0, x1):
            z = WIPER_Z
            pts.append((x, TOP(z) + 0.012, z))
        (a, b) = pts
        out.append(box(((a[0] + b[0]) / 2, a[1], a[2]), (abs(b[0] - a[0]), 0.012, 0.018), GLOSS))
    return concat(out)


def lining(P, N, U, K):
    """The shell 8 mm inward, turned around: headliner above the windows, dark trim below (no lining behind glass)."""
    C = P.mean(axis=1)
    solid = np.array([k in (PAINT, TRIM, GLOSS, RUBBER) for k in K], dtype=bool) & (C[:, 2] > GLASS_Z[0] - 0.06) & (C[:, 2] < GLASS_Z[1] + 0.03) & (C[:, 1] > 0.32)
    Pi = (P - N * 0.008)[solid][:, ::-1]
    Ni = -N[solid][:, ::-1]
    Ui = U[solid][:, ::-1]
    Ki = [LINER if tri[:, 1].mean() > 1.3 else DASH for tri in Pi]
    return Pi, Ni, Ui, Ki


def car_shell(level=0):
    """The whole outside of the car. level 0 (close: subdivided twice, every detail, lining), 1 (once, no gaps or
    lining), 2 (the cage, lights and grilles only)."""
    body = body_mesh(2 if level == 0 else 1 if level == 1 else 0)
    proj = Projector(body[0])
    parts = [body, decals(proj, level)]
    if level == 0:
        parts.append(lining(*body))
    parts += [mirror(s) for s in (1, -1)]
    if level < 2:
        parts += [handle(s, z, SHOULDER(z) - 0.03) for s in (1, -1) for z in HANDLE_Z]
        parts += [wipers()]
    parts += [plate(proj, 'front', PLATE_Y[0]), plate(proj, 'rear', PLATE_Y[1])]
    return concat(parts)


class variant:
    """Build another car with this module's machinery: `with car_model.variant(other.OVERRIDES): car_shell()`.
    OVERRIDES replaces module names (curves, stations, windows(), decals(), part positions) for the block."""

    def __init__(self, overrides):
        self.overrides, self.saved = overrides, {}

    def __enter__(self):
        g = globals()
        for k, v in self.overrides.items():
            self.saved[k] = g[k]
            g[k] = v
        return self

    def __exit__(self, *exc):
        globals().update(self.saved)


# ---------- wheels ----------
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


def car_wheel(R=WHEEL_R, W=0.2, spokes=5, tyre_key=TYRE, rim_key=RIM, centred=False):
    """Motorcycle wheels (build_assets.moto_parts, centred=True): a rounded tyre and a cast rim with straight
    spokes in the middle of the wheel; axle along x. The car uses golf_wheel()."""
    side = R - 0.11
    tyre = [(-W / 2 + 0.02, side), (-W / 2, side + 0.03), (-W / 2 - 0.005, R - 0.04), (-W / 2 + 0.01, R - 0.012),
            (-W / 2 + 0.035, R), (W / 2 - 0.035, R), (W / 2 - 0.01, R - 0.012), (W / 2 + 0.005, R - 0.04),
            (W / 2, side + 0.03), (W / 2 - 0.02, side)]
    lip = side + 0.01
    rim = [(W / 2 - 0.015, lip), (W / 2 - 0.015, side - 0.02), (-W / 2 + 0.02, side - 0.02), (-W / 2 + 0.02, lip - 0.005),
           (-W / 2 + 0.012, lip + 0.002)]
    hx = 0.0 if centred else W / 2 - 0.045
    hub = [(hx - 0.015, 0.0), (hx - 0.015, 0.055), (hx + 0.01, 0.065), (hx + 0.015, 0.0)]
    parts = [lathe(tyre, 36, tyre_key), lathe(rim, 36, rim_key), lathe(hub, 16, rim_key)]
    reach = side - 0.02 - 0.05
    for i in range(spokes):
        a = 2 * math.pi * i / spokes
        bm = bmesh.new()
        bmesh.ops.create_cube(bm, size=1.0)
        for v in bm.verts:
            x, y, z = v.co
            t = y + 0.5
            r = 0.05 + t * reach
            half_w = 0.03 - t * 0.012
            off = z * half_w * 2
            xx = (x * 0.024) if centred else (W / 2 - 0.045 - t * 0.02 + x * 0.026)
            v.co = (xx, r * math.cos(a) - off * math.sin(a), r * math.sin(a) + off * math.cos(a))
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
        ob, names = make_object(bm, [rim_key] * 6, 0, 'spoke_tmp')
        parts.append(to_corners(ob, names))
        bpy.data.objects.remove(ob)
    return concat(parts)


def golf_wheel(R=WHEEL_R, W=0.205):
    """205/55 R16 tyre (tread with four grooves, sidewall bulge, rim guard) on a 16 inch ten-spoke alloy (five pairs,
    dished toward the outer face +x), brake disc and five bolts. Axle along x; build_assets mirrors the left wheels."""
    rb = 0.203                                                          # 16 inch bead seat
    h = W / 2
    tread = [(-h + 0.03, R - 0.004)]
    for gx in (-0.055, -0.018, 0.018, 0.055):                           # circumferential grooves 9 mm wide, 7 mm deep
        tread += [(gx - 0.0045, R), (gx - 0.0045, R - 0.007), (gx + 0.0045, R - 0.007), (gx + 0.0045, R)]
    tread += [(h - 0.03, R - 0.004)]
    tyre = ([(-h + 0.012, rb + 0.004), (-h - 0.002, rb + 0.03), (-h - 0.008, rb + 0.065), (-h - 0.006, R - 0.035),
             (-h + 0.006, R - 0.012)] + tread +
            [(h - 0.006, R - 0.012), (h + 0.006, R - 0.035), (h + 0.008, rb + 0.065), (h + 0.002, rb + 0.03), (h - 0.012, rb + 0.004)])
    barrel = [(h - 0.004, rb + 0.012), (h - 0.012, rb + 0.016), (h - 0.016, rb - 0.006), (-h + 0.016, rb - 0.012),
              (-h + 0.010, rb + 0.008), (-h + 0.002, rb + 0.010), (-h + 0.002, rb - 0.024), (h - 0.002, rb - 0.016)]
    face = h - 0.030                                                    # the spokes' outer face (dish)
    centre = [(face - 0.04, 0.0), (face - 0.04, 0.075), (face, 0.082), (face + 0.006, 0.07), (face + 0.008, 0.0)]
    cap = [(face + 0.004, 0.0), (face + 0.004, 0.032), (face + 0.012, 0.028), (face + 0.013, 0.0)]
    disc = [(-0.02, 0.06), (-0.02, 0.145), (0.006, 0.145), (0.006, 0.06)]
    P, N, U, K = lathe(barrel, 48, WRIM)
    inner = (N * P).sum(axis=2).mean(axis=1) - (N[:, :, 0] * P[:, :, 0]).mean(axis=1) < 0      # faces looking at the axle
    barrel_part = (P, N, U, [WDISC if i else k for i, k in zip(inner, K)])
    parts = [lathe(tyre, 48, WTYRE), barrel_part, lathe(centre, 24, WRIM), lathe(cap, 20, CHROME), lathe(disc, 32, WDISC)]
    reach = rb - 0.02
    for i in range(5):
        for pair in (-1, 1):
            a = 2 * math.pi * i / 5 + pair * 0.105
            bm = bmesh.new()
            bmesh.ops.create_cube(bm, size=1.0)
            for v in bm.verts:
                x, y, z = v.co
                t = y + 0.5                                             # 0 at the centre .. 1 at the barrel
                r = 0.072 + t * (reach - 0.072)
                half_w = 0.021 - t * 0.005
                off = z * half_w * 2 - pair * (1 - t) * 0.012          # the pair meets at the centre
                xx = face - t * 0.022 - 0.012 * (1 - t) + x * (0.028 - t * 0.01)
                v.co = (xx, r * math.cos(a) - off * math.sin(a), r * math.sin(a) + off * math.cos(a))
            bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
            ob, names = make_object(bm, [WRIM] * 6, 0, 'spoke_tmp')
            parts.append(to_corners(ob, names))
            bpy.data.objects.remove(ob)
        a = 2 * math.pi * (i + 0.5) / 5                                 # wheel bolt between the spoke pairs
        bolt = [(face - 0.004, 0.0), (face - 0.004, 0.009), (face + 0.006, 0.009), (face + 0.009, 0.006), (face + 0.009, 0.0)]
        P, N, U, K = lathe(bolt, 8, CHROME)
        off = np.array([0.0, 0.048 * math.cos(a), 0.048 * math.sin(a)])
        parts.append((P + off, N, U, K))
    return concat(parts)
