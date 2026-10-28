"""Real 3D facades for the blocks near home (photo-real pass), used by build_facades.py and build_assets.py.

Up to RADIUS from home (the whole near district), the brick blocks' walls are rebuilt as geometry instead of the flat web wall with windows
drawn on it: the wall has real openings, the glass sits D back in a reveal (side, head and sill faces), with a
6 cm frame, mullions and a concrete sill that sticks out. Window places follow the web facade: 3 m bays along the
wall from the start of the web's walls() u (surroundings.js), one window per bay that fits, sill 0.876 m and head
2.301 m above each storey floor (2.85 m storeys from the plinth top), the window widths of M_HK_Building (1.25,
1.75 or 2.0 m on brick, 1.0-1.5 m on plaster, the same up a column). Collision is the web's (walls stay where they
are; the player keeps 0.18 m from them).

Walls left alone (they keep the flat web facade): near a stair tower (its own facade is there), shared with
another building or Haarklous plass 17, shorter than 3.4 m, and every building that is not a 'brick' block.

covered_walls() lists the rebuilt wall pieces (world frame), so build_assets.py can drop the web's facade
triangles there; shell() builds the geometry (world frame, web axes, metres).
"""
import json
import math
import os

import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
DATA = os.path.join(ROOT, 'unreal', 'Haarklous', 'Content', 'Data')
TEX = os.path.join(ROOT, 'tools', 'out', 'textures')
RADIUS = 270.0          # inside the web's near set (NEAR_LIMIT 280 m), whose walls are in world_near
STOREY, PLINTH = 2.85, 1.425
SILL, HEAD = 0.876, 2.301

WALL, GLASS, FRAME_DARK, FRAME_WHITE, SILL_KEY = 'MI_HK_Wall3D', 'MI_HK_Glass3D', 'MI_HK_FrameBrown', 'MI_HK_FrameWhite', 'MI_HK_DetailConcrete'
SLOTS = [WALL, GLASS, FRAME_DARK, FRAME_WHITE, SILL_KEY]
BRICK_AVG = (0.3587, 0.2353, 0.1676)
PLASTER_AVG = (0.4943, 0.4405, 0.3687)
COLOUR_SCALE = 2.5          # vertex colours hold tint / 2.5 (they are clamped to 0..1); M_HK_Wall3D multiplies back


def hash01(*v):
    x = math.sin(sum(a * b for a, b in zip(v, (12.9898, 78.233, 37.719)))) * 43758.5453
    return x - math.floor(x)


def inside(poly, p):
    x, y = p
    c = False
    n = len(poly)
    for k in range(n):
        x0, y0 = poly[k]; x1, y1 = poly[(k + 1) % n]
        if (y0 > y) != (y1 > y) and x < (x1 - x0) * (y - y0) / (y1 - y0 + 1e-12) + x0:
            c = not c
    return c


def _area(r):
    return 0.5 * np.sum(r[:, 0] * np.roll(r[:, 1], -1) - np.roll(r[:, 0], -1) * r[:, 1])


def covered_walls():
    s = json.load(open(os.path.join(DATA, 'surroundings.json'), encoding='utf-8'))
    game = json.load(open(os.path.join(DATA, 'game.json'), encoding='utf-8'))
    looks = json.load(open(os.path.join(TEX, 'building-looks.json'), encoding='utf-8'))
    pal = None
    try:
        from PIL import Image
        pal = np.array(Image.open(os.path.join(TEX, 'T_BuildingPalette.png')))
    except Exception:
        pass
    th, gx, gz, ground = s['geo']['theta'], s['geo']['x'], s['geo']['z'], s['ground']
    cT, sT = math.cos(th), math.sin(th)
    to_world = lambda x, z: np.array([x * cT + z * sT + gx, -x * sT + z * cT + gz])
    towers = []
    for t in game['towers']:
        M = t['matrix']
        towers.append((M[0] * 0.1 + M[8] * 8 + M[12], M[2] * 0.1 + M[10] * 8 + M[14]))
    # Every footprint in the world frame, for the shared-wall test.
    rings = [np.array([to_world(x, z) for x, z in np.array(b['r']['o'], float).reshape(-1, 2)]) for b in s['buildings']]
    for part in s['own']['lower'] + s['own']['band'] + s['own']['upper']:
        rings.append(np.array(part['o'], float).reshape(-1, 2))
    boxes = [(r[:, 0].min(), r[:, 0].max(), r[:, 1].min(), r[:, 1].max()) for r in rings]

    def in_other(p, own_index):
        for j, (x0, x1, z0, z1) in enumerate(boxes):
            if j != own_index and x0 - 1 < p[0] < x1 + 1 and z0 - 1 < p[1] < z1 + 1 and inside(rings[j], p):
                return True
        return False

    walls = []
    for i, b in enumerate(s['buildings']):
        look = looks.get(str(i))
        if b['s'] != 'brick' or not look:
            continue
        world = rings[i]
        if np.hypot(*world.mean(axis=0)) > RADIUS:
            continue
        g0 = ground + (b.get('g') or 0)
        top = g0 + b['h']
        y0 = g0 + PLINTH
        if top - y0 < STOREY:
            continue
        lid = look['look']
        colour = tuple(pal[0, lid, :3] / 255.0) if pal is not None else (0.8, 0.6, 0.55)
        plaster = look['style'] == 'plaster'
        white = bool(pal[1, lid, 2] > 127) if pal is not None else plaster
        variant = (pal[1, lid, 3] / 255.0) if pal is not None else 0.5
        lin = np.power(np.array(colour), 2.2)
        tint = lin / PLASTER_AVG * 0.68 if plaster else lin / BRICK_AVG * (0.92 + 0.12 * variant)
        # The web's walls(): ring reversed when its signed area (map frame) is positive; u runs along that order.
        ring = np.array(b['r']['o'], float).reshape(-1, 2)
        order = ring[::-1] if _area(ring) > 0 else ring
        wo = np.array([to_world(x, z) for x, z in order])
        u = 0.0
        for k in range(len(wo)):
            a, c = wo[k], wo[(k + 1) % len(wo)]
            L = float(np.hypot(*(c - a)))
            if L < 0.02:
                continue
            d = (c - a) / L
            n = np.array([d[1], -d[0]])
            mid = (a + c) / 2
            if inside(world, mid + n * 0.3):
                n = -n
            near_tower = any(math.hypot(q[0] - tx, q[1] - tz) < 6.0 for tx, tz in towers
                             for q in (a + d * t for t in np.linspace(0, L, max(2, int(L / 2) + 1))))
            ok = L >= 3.4 and not near_tower and not inside(world, mid + n * 0.3) and not in_other(mid + n * 0.3, i)
            if ok:
                walls.append({'b': i, 'a': a, 'c': c, 'd': d, 'n': n, 'L': L, 'u0': u, 'y0': y0, 'y1': top,
                              'tint': tuple(np.clip(tint / COLOUR_SCALE, 0, 1)), 'plaster': plaster, 'white': white})
            u += L
    return walls


def on_wall(c, nrm, walls_by_cell):
    """True if a triangle centroid c (world x, y, z) with normal nrm lies on one of the rebuilt walls."""
    key = (int(math.floor(c[0] / 20)), int(math.floor(c[2] / 20)))
    for w in walls_by_cell.get(key, ()):
        p = np.array([c[0], c[2]]) - w['a']
        t = float(np.dot(p, w['d']))
        if -0.01 < t < w['L'] + 0.01 and abs(float(np.dot(p, w['n']))) < 0.05 and w['y0'] - 0.05 < c[1] < w['y1'] + 0.05:
            return True
    return False


def index_walls(walls):
    cells = {}
    for w in walls:
        lo = np.minimum(w['a'], w['c']) - 1; hi = np.maximum(w['a'], w['c']) + 1
        for i in range(int(math.floor(lo[0] / 20)), int(math.floor(hi[0] / 20)) + 1):
            for j in range(int(math.floor(lo[1] / 20)), int(math.floor(hi[1] / 20)) + 1):
                cells.setdefault((i, j), []).append(w)
    return cells


class Builder:
    """Triangle soup with per-corner position, normal, uv0, uv1, colour (RGBA) and a slot per triangle."""

    def __init__(self):
        self.P, self.N, self.U0, self.U1, self.C, self.S = [], [], [], [], [], []

    def quad(self, pts, normal, slot, uv0, uv1=None, col=(1, 1, 1, 1)):
        pts = [np.asarray(p, float) for p in pts]
        nrm = np.asarray(normal, float)
        if np.dot(np.cross(pts[1] - pts[0], pts[2] - pts[0]), nrm) < 0:
            pts = pts[::-1]; uv0 = uv0[::-1]; uv1 = uv1[::-1] if uv1 is not None else None
        uv1 = uv1 if uv1 is not None else [(0, 0)] * 4
        for tri in ((0, 1, 2), (0, 2, 3)):
            self.P.append([pts[t] for t in tri]); self.N.append([nrm] * 3)
            self.U0.append([uv0[t] for t in tri]); self.U1.append([uv1[t] for t in tri])
            self.C.append([col] * 3); self.S.append(slot)

    def arrays(self):
        return (np.array(self.P), np.array(self.N), np.array(self.U0), np.array(self.U1), np.array(self.C), np.array(self.S))


def shell(walls):
    B = Builder()
    for w in walls:
        a, d, n, L = w['a'], w['d'], w['n'], w['L']
        y0, y1 = w['y0'], w['y1']
        col = (*w['tint'], 1.0 if w['plaster'] else 0.0)
        D = 0.10 if w['plaster'] else 0.13
        P = lambda t, y, depth=0.0: np.array([a[0] + d[0] * t - n[0] * depth, y, a[1] + d[1] * t - n[1] * depth])
        nw = np.array([n[0], 0.0, n[1]])
        dw = np.array([d[0], 0.0, d[1]])
        # Windows: one per 3 m bay of the web facade that fits inside this wall with 0.35 m to spare.
        wins = []
        first = math.ceil((w['u0'] - 1.5) / 3.0)
        for bay in range(first, int((w['u0'] + L) // 3.0) + 1):
            hc = hash01(w['b'], bay, 3.3)
            W = (1.25 if hc < 0.35 else 1.75 if hc < 0.75 else 2.0) if not w['plaster'] else (1.0 if hc < 0.3 else 1.25 if hc < 0.8 else 1.5)
            cx = bay * 3.0 + 1.5 - w['u0']
            if cx - W / 2 >= 0.35 and cx + W / 2 <= L - 0.35:
                wins.append((cx - W / 2, cx + W / 2, bay))
        storeys = int((y1 - y0 - 0.3) // STOREY)
        rows = [(y0 + k * STOREY + SILL, y0 + k * STOREY + HEAD) for k in range(storeys) if y0 + k * STOREY + HEAD < y1 - 0.25]
        # Wall face: horizontal strips between window rows, and solid pieces between windows inside each row.
        cuts = [y0]
        for s0, s1 in rows:
            cuts += [s0, s1]
        cuts.append(y1)
        uvw = lambda t, y: (t + w['u0'], y - y0)
        for k in range(len(cuts) - 1):
            ya, yb = cuts[k], cuts[k + 1]
            if yb - ya < 1e-3:
                continue
            in_row = k % 2 == 1
            spans = [(0.0, L)]
            if in_row and wins:
                spans, t = [], 0.0
                for x0, x1, _ in wins:
                    spans.append((t, x0)); t = x1
                spans.append((t, L))
            for t0, t1 in spans:
                if t1 - t0 < 1e-3:
                    continue
                B.quad([P(t0, ya), P(t1, ya), P(t1, yb), P(t0, yb)], nw, 0,
                       [uvw(t0, ya), uvw(t1, ya), uvw(t1, yb), uvw(t0, yb)], col=col)
        for ri, (s0, s1) in enumerate(rows):
            floor = s0 - SILL
            for x0, x1, bay in wins:
                W, H = x1 - x0, s1 - s0
                # Reveals (wall material, uv continuing into the depth).
                B.quad([P(x0, s0), P(x0, s1), P(x0, s1, D), P(x0, s0, D)], dw, 0,
                       [uvw(x0, s0), uvw(x0, s1), uvw(x0 + D, s1), uvw(x0 + D, s0)], col=col)
                B.quad([P(x1, s0), P(x1, s1), P(x1, s1, D), P(x1, s0, D)], -dw, 0,
                       [uvw(x1, s0), uvw(x1, s1), uvw(x1 - D, s1), uvw(x1 - D, s0)], col=col)
                B.quad([P(x0, s1), P(x1, s1), P(x1, s1, D), P(x0, s1, D)], np.array([0, -1.0, 0]), 0,
                       [uvw(x0, s1), uvw(x1, s1), uvw(x1, s1 + D), uvw(x0, s1 + D)], col=col)
                B.quad([P(x0, s0), P(x1, s0), P(x1, s0, D), P(x0, s0, D)], np.array([0, 1.0, 0]), 4,
                       [(0, 0), (W, 0), (W, D), (0, D)])
                # Glass at the back of the reveal: uv0 = cell of a 3 m x 2.85 m room behind (UE turns v downward),
                # uv1 = 0..1 over the window, colour red = a random value for this window.
                seed = hash01(w['b'], bay, ri, 7.7)
                cx = (x0 + x1) / 2
                g = lambda t, y: ((t - cx) / 3.0 + 0.5, (y - floor) / STOREY)
                B.quad([P(x0, s0, D), P(x1, s0, D), P(x1, s1, D), P(x0, s1, D)], nw, 1,
                       [g(x0, s0), g(x1, s0), g(x1, s1), g(x0, s1)], [(0, 0), (1, 0), (1, 1), (0, 1)], col=(seed, 0, 0, 1))
                # Frame: a 6 cm border and mullions, 1 cm in front of the glass.
                fs = 3 if w['white'] else 2
                fd = D - 0.012
                f = 0.06
                for (t0, t1, ya, yb) in ((x0, x1, s0, s0 + f), (x0, x1, s1 - f, s1), (x0, x0 + f, s0 + f, s1 - f), (x1 - f, x1, s0 + f, s1 - f)):
                    B.quad([P(t0, ya, fd), P(t1, ya, fd), P(t1, yb, fd), P(t0, yb, fd)], nw, fs,
                           [(t0, ya), (t1, ya), (t1, yb), (t0, yb)])
                lights = 1 if W < 1.1 else 2 if W < 1.6 else 3
                for m in range(1, lights):
                    tm = x0 + W * m / lights
                    B.quad([P(tm - 0.025, s0, fd), P(tm + 0.025, s0, fd), P(tm + 0.025, s1, fd), P(tm - 0.025, s1, fd)], nw, fs,
                           [(0, 0), (0.05, 0), (0.05, H), (0, H)])
                # Transom: the upper lights of the blocks' windows.
                tr = s1 - 0.42
                B.quad([P(x0, tr - 0.02, fd), P(x1, tr - 0.02, fd), P(x1, tr + 0.02, fd), P(x0, tr + 0.02, fd)], nw, fs,
                       [(0, 0), (W, 0), (W, 0.04), (0, 0.04)])
                # Concrete sill: 6 cm out of the wall, 5 cm thick.
                so, sd = 0.06, s0 - 0.05
                for pts, nn in (([P(x0 - 0.05, s0, -so), P(x1 + 0.05, s0, -so), P(x1 + 0.05, s0, 0), P(x0 - 0.05, s0, 0)], np.array([0, 1.0, 0])),
                                ([P(x0 - 0.05, sd, -so), P(x1 + 0.05, sd, -so), P(x1 + 0.05, s0, -so), P(x0 - 0.05, s0, -so)], nw),
                                ([P(x0 - 0.05, sd, 0), P(x1 + 0.05, sd, 0), P(x1 + 0.05, sd, -so), P(x0 - 0.05, sd, -so)], np.array([0, -1.0, 0])),
                                ([P(x0 - 0.05, sd, 0), P(x0 - 0.05, sd, -so), P(x0 - 0.05, s0, -so), P(x0 - 0.05, s0, 0)], -dw),
                                ([P(x1 + 0.05, sd, -so), P(x1 + 0.05, sd, 0), P(x1 + 0.05, s0, 0), P(x1 + 0.05, s0, -so)], dw)):
                    B.quad(pts, nn, 4, [(0, 0), (W, 0), (W, 0.06), (0, 0.06)])
    return B.arrays()
