"""Geometry helpers for the construction-site machines and props (tools/blender/build_machines.py).

Everything is built in the web frame (metres, x right, y up, forward = -z) as triangle lists with a material slot
name per triangle, then turned into Blender objects (x, -z, y) like build_assets.py does. UVs are box-projected in
metres (so tiling textures keep their real size); the vertex colour's red channel is 'grime' (more near the ground,
plus noise), used by the machine materials.
"""
import math

import numpy as np


class Mesh:
    def __init__(self):
        self.P, self.M = [], []          # triangles (3 x 3 points), slot name per triangle

    def tri(self, a, b, c, mat):
        self.P.append([np.asarray(a, float), np.asarray(b, float), np.asarray(c, float)])
        self.M.append(mat)

    def quad(self, a, b, c, d, mat):
        self.tri(a, b, c, mat); self.tri(a, c, d, mat)

    def poly(self, pts, mat):
        """Convex polygon (fan)."""
        for k in range(1, len(pts) - 1):
            self.tri(pts[0], pts[k], pts[k + 1], mat)

    def add(self, other, xf=None):
        for t, m in zip(other.P, other.M):
            self.P.append([xf(p) if xf else p for p in t]); self.M.append(m)
        return self

    def transformed(self, pos=(0, 0, 0), rot=None):
        R = rot if rot is not None else np.eye(3)
        out = Mesh()
        out.add(self, lambda p: R @ p + np.asarray(pos, float))
        return out


def rot_x(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])


def rot_y(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])


def rot_z(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])


def box(m, c, size, mat, bevel=0.0, rot=None, skip=()):
    """Box centred at c with full sizes (x, y, z); bevel > 0 chamfers all edges (one 45 degree cut)."""
    c = np.asarray(c, float); hx, hy, hz = np.asarray(size, float) / 2
    R = rot if rot is not None else np.eye(3)
    P = lambda x, y, z: R @ np.array([x, y, z]) + c
    b = min(bevel, hx * .9, hy * .9, hz * .9)
    if b <= 0:
        faces = {'+x': [(hx, -hy, hz), (hx, -hy, -hz), (hx, hy, -hz), (hx, hy, hz)],
                 '-x': [(-hx, -hy, -hz), (-hx, -hy, hz), (-hx, hy, hz), (-hx, hy, -hz)],
                 '+y': [(-hx, hy, hz), (hx, hy, hz), (hx, hy, -hz), (-hx, hy, -hz)],
                 '-y': [(-hx, -hy, -hz), (hx, -hy, -hz), (hx, -hy, hz), (-hx, -hy, hz)],
                 '+z': [(-hx, -hy, hz), (hx, -hy, hz), (hx, hy, hz), (-hx, hy, hz)],
                 '-z': [(hx, -hy, -hz), (-hx, -hy, -hz), (-hx, hy, -hz), (hx, hy, -hz)]}
        for k, f in faces.items():
            if k not in skip:
                m.quad(*[P(*v) for v in f], mat)
        return m
    # Chamfered box: the 6 faces shrunk by b, 12 edge strips, 8 corner triangles.
    ix, iy, iz = hx - b, hy - b, hz - b
    for ax in range(3):
        for sgn in (1, -1):
            if {0: 'x', 1: 'y', 2: 'z'}[ax] in [k[1] for k in skip if k[0] == ('+' if sgn > 0 else '-')]:
                continue
            u, v = [a for a in range(3) if a != ax]
            ext = [ix, iy, iz]; full = [hx, hy, hz]
            pts = []
            for (su, sv) in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
                q = [0, 0, 0]; q[ax] = sgn * full[ax]; q[u] = su * ext[u]; q[v] = sv * ext[v]
                pts.append(P(*q))
            n = np.zeros(3); n[ax] = sgn
            if np.dot(np.cross(pts[1] - pts[0], pts[2] - pts[0]), R @ n) < 0:
                pts = pts[::-1]
            m.quad(*pts, mat)
    cen = np.zeros(3)
    for a in range(3):
        for b_ in range(a + 1, 3):
            for sa in (1, -1):
                for sb in (1, -1):
                    w = 3 - a - b_
                    ext = [ix, iy, iz]; full = [hx, hy, hz]
                    pts = []
                    for sw in (-1, 1):
                        q1 = [0, 0, 0]; q1[a] = sa * full[a]; q1[b_] = sb * ext[b_]; q1[w] = sw * ext[w]
                        q2 = [0, 0, 0]; q2[a] = sa * ext[a]; q2[b_] = sb * full[b_]; q2[w] = sw * ext[w]
                        pts += [q1, q2]
                    quad = [P(*pts[0]), P(*pts[1]), P(*pts[3]), P(*pts[2])]
                    n = np.zeros(3); n[a] = sa; n[b_] = sb
                    if np.dot(np.cross(quad[1] - quad[0], quad[2] - quad[0]), R @ n) < 0:
                        quad = quad[::-1]
                    m.quad(*quad, mat)
    for sx in (1, -1):
        for sy in (1, -1):
            for sz in (1, -1):
                t = [P(sx * hx, sy * iy, sz * iz), P(sx * ix, sy * hy, sz * iz), P(sx * ix, sy * iy, sz * hz)]
                n = R @ np.array([sx, sy, sz])
                if np.dot(np.cross(t[1] - t[0], t[2] - t[0]), n) < 0:
                    t = t[::-1]
                m.tri(*t, mat)
    return m


def cyl(m, a, b, r, mat, seg=12, caps=True, r2=None):
    """Cylinder (or cone with r2) from point a to b."""
    a, b = np.asarray(a, float), np.asarray(b, float)
    r2 = r if r2 is None else r2
    d = b - a; L = np.linalg.norm(d); d /= L
    t = np.cross(d, [0, 1, 0] if abs(d[1]) < .9 else [1, 0, 0]); t /= np.linalg.norm(t)
    s = np.cross(d, t)
    ring = [(math.cos(2 * math.pi * k / seg), math.sin(2 * math.pi * k / seg)) for k in range(seg)]
    A = [a + (t * c + s * n) * r for c, n in ring]
    B = [b + (t * c + s * n) * r2 for c, n in ring]
    for k in range(seg):
        k1 = (k + 1) % seg
        m.quad(A[k], A[k1], B[k1], B[k], mat)
    if caps:
        for k in range(1, seg - 1):
            m.tri(A[0], A[k + 1], A[k], mat)
            m.tri(B[0], B[k], B[k + 1], mat)
    return m


def prism(m, profile, x0, x1, mat, side_mat=None):
    """A profile in the (z, y) plane (counter-clockwise seen from +x) extruded from x = x0 to x1."""
    side_mat = side_mat or mat
    pts = [np.array(p, float) for p in profile]
    n = len(pts)
    L = [np.array([x0, p[1], p[0]]) for p in pts]
    R = [np.array([x1, p[1], p[0]]) for p in pts]
    # side caps (convex profiles only: fan; concave ones should be split by the caller)
    for k in range(1, n - 1):
        m.tri(R[0], R[k], R[k + 1], side_mat)
        m.tri(L[0], L[k + 1], L[k], side_mat)
    for k in range(n):
        k1 = (k + 1) % n
        m.quad(L[k], L[k1], R[k1], R[k], mat)
    return m


def lathe_x(m, profile, mat, seg=24, centre=(0, 0, 0)):
    """Solid of revolution about the x axis: profile = closed list of (x, r)."""
    c = np.asarray(centre, float)
    rings = []
    for k in range(seg):
        a = 2 * math.pi * k / seg
        rings.append([c + np.array([x, r * math.cos(a), r * math.sin(a)]) for x, r in profile])
    n = len(profile)
    for k in range(seg):
        k1 = (k + 1) % seg
        for i in range(n):
            i1 = (i + 1) % n
            m.quad(rings[k][i], rings[k][i1], rings[k1][i1], rings[k1][i], mat)
    return m


def tyre(m, centre, radius, width, mat_tyre, mat_rim, rim_r=None, lugs=22, rim_dish=0.08):
    """An earthmover / truck tyre with tread lugs (axle along x) and a steel rim with bolts."""
    rim_r = rim_r or radius * 0.58
    hw = width / 2
    prof = [(-hw, rim_r * 1.02), (-hw - .01, (rim_r + radius) / 2), (-hw + .03, radius - .05), (-hw + .1, radius - .015),
            (hw - .1, radius - .015), (hw - .03, radius - .05), (hw + .01, (rim_r + radius) / 2), (hw, rim_r * 1.02)]
    lathe_x(m, prof, mat_tyre, seg=36, centre=centre)
    c = np.asarray(centre, float)
    # Tread lugs (chevrons): two rows of blocks on the crown, offset.
    for k in range(lugs):
        for side in (-1, 1):
            a = 2 * math.pi * (k + (0.5 if side > 0 else 0)) / lugs
            R = rot_x(a)
            blk = Mesh()
            box(blk, (side * hw * .45, radius + .025, 0), (hw * .75, .05, 2 * math.pi * radius / lugs * .45), mat_tyre,
                rot=rot_y(side * .45))
            m.add(blk.transformed(c, R))
    # Rim: a dished disc with a hub and bolts.
    lathe_x(m, [(-hw + .05, rim_r), (hw - .05, rim_r), (hw - .05, rim_r * .9), (rim_dish, rim_r * .85), (rim_dish, rim_r * .25),
                (rim_dish + .06, rim_r * .22), (rim_dish + .06, .01), (-hw + .05, .01)], mat_rim, seg=24, centre=centre)
    for k in range(10):
        a = 2 * math.pi * k / 10
        p = c + np.array([rim_dish + .07, math.cos(a) * rim_r * .33, math.sin(a) * rim_r * .33])
        cyl(m, p, p + np.array([.04, 0, 0]), .025, mat_rim, seg=6)
    return m


def track(m, length, height, width, mat_shoe, mat_frame, centre_x, ground=0.0, front_r=.36, rear_r=.42, rear_up=0.0, shoes=44):
    """A crawler track as a loop of shoes with grousers around a front idler and a rear sprocket, plus rollers."""
    zf, zr = -length / 2 + front_r, length / 2 - rear_r
    yf, yr = ground + front_r, ground + rear_r + rear_up
    # Path: bottom straight (front->rear), rear arc, top straight (rear->front), front arc.
    pts = []
    N = 160
    def arc(cz, cy, r, a0, a1, n):
        return [(cz + r * math.sin(a), cy - r * math.cos(a)) for a in np.linspace(a0, a1, n)]
    path = [(zf, ground)] + [(zr, ground + rear_up * 0)] + arc(zr, yr, rear_r, 0, math.pi, 30) + [(zf, yf + front_r)] + arc(zf, yf, front_r, math.pi, 2 * math.pi, 30)
    path = np.array(path, float)
    seglen = np.hypot(*np.diff(np.vstack([path, path[:1]]), axis=0).T)
    total = seglen.sum()
    cum = np.concatenate([[0], np.cumsum(seglen)])
    def at(s):
        s %= total
        i = np.searchsorted(cum, s) - 1
        i = max(0, min(i, len(path) - 1))
        p0, p1 = path[i], path[(i + 1) % len(path)]
        t = (s - cum[i]) / max(seglen[i], 1e-9)
        p = p0 + (p1 - p0) * t
        d = (p1 - p0) / max(seglen[i], 1e-9)
        return p, d
    pitch = total / shoes
    for k in range(shoes):
        p, d = at(k * pitch)
        z, y = p
        # outward normal of the loop (left of the direction of travel in the z-y plane, pointing away from the loop)
        nz, ny = d[1], -d[0]
        ang = math.atan2(ny, nz)   # rotation in the z-y plane
        R = rot_x(math.atan2(-d[1], d[0]) * -1)
        shoe = Mesh()
        box(shoe, (0, .025, 0), (width, .05, pitch * .92), mat_shoe)
        box(shoe, (0, .065, 0), (width, .04, .05), mat_shoe)      # grouser
        # orient: shoe local +y = outward normal, local z = along the track
        zdir = np.array([0, d[1], d[0]])
        ydir = np.array([0, -d[0] * 0 + (-d[0]) * 0, 0])
        out = np.array([0.0, 0.0, 0.0])
        # outward: perpendicular to direction, pointing away from the loop centre
        cy, cz = (yf + yr) / 2, (zf + zr) / 2
        perp = np.array([0, d[0], -d[1]])
        if np.dot(perp, np.array([0, y - cy, z - cz])) < 0:
            perp = -perp
        Rm = np.column_stack([np.array([1.0, 0, 0]), perp, np.cross([1.0, 0, 0], perp)])
        m.add(shoe.transformed((centre_x, y, z), Rm))
    # Idler, sprocket and rollers inside the loop.
    for (z, y, r, teeth) in ((zf, yf, front_r * .92, 0), (zr, yr, rear_r * .9, 11)):
        lathe_x(m, [(-width * .35, r), (width * .35, r), (width * .35, .05), (-width * .35, .05)], mat_frame, seg=20, centre=(centre_x, y, z))
        for k in range(teeth):
            a = 2 * math.pi * k / teeth
            box(m, (centre_x, y + math.cos(a) * (r + .03), z + math.sin(a) * (r + .03)), (width * .5, .08, .08), mat_frame, rot=rot_x(-a))
    for k in range(5):
        z = zf + (zr - zf) * (k + .5) / 5
        lathe_x(m, [(-width * .3, .13), (width * .3, .13), (width * .3, .03), (-width * .3, .03)], mat_frame, seg=12, centre=(centre_x, ground + .2, z))
    box(m, (centre_x, ground + .45 + rear_up / 2, (zf + zr) / 2), (width * .55, .38, (zr - zf) * .92), mat_frame, bevel=.04)
    return m


def lattice(m, a, b, w, h, mat, panels=None, chord=.06, brace=.03, top_open=False):
    """A box-section lattice girder from a to b (cross-section w x h, 'up' = +y unless the girder is vertical)."""
    a, b = np.asarray(a, float), np.asarray(b, float)
    d = b - a; L = np.linalg.norm(d); d /= L
    up = np.array([0, 1.0, 0]) if abs(d[1]) < .9 else np.array([0, 0, 1.0])
    side = np.cross(d, up); side /= np.linalg.norm(side)
    up = np.cross(side, d)
    corners = [(-1, -1), (1, -1), (1, 1), (-1, 1)]
    panels = panels or max(1, int(round(L / max(w, h, .8))))
    off = lambda c: side * c[0] * w / 2 + up * c[1] * h / 2
    for c in corners:
        cyl(m, a + off(c), b + off(c), chord, mat, seg=5, caps=False)
    for k in range(panels):
        p0, p1 = a + d * L * k / panels, a + d * L * (k + 1) / panels
        for i in range(4):
            c0, c1 = corners[i], corners[(i + 1) % 4]
            if top_open and i == 2:
                continue
            q0, q1 = (p0, p1) if k % 2 == 0 else (p1, p0)
            cyl(m, q0 + off(c0), q1 + off(c1), brace, mat, seg=4, caps=False)
        for i in range(4):
            c0, c1 = corners[i], corners[(i + 1) % 4]
            cyl(m, p1 + off(c0), p1 + off(c1), brace, mat, seg=4, caps=False)
    return m
