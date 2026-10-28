"""The construction site (new feature, asked for by the user on 28 Oct 2026): place, ground shape, machines and props.

    python tools/make-site.py

The site is a 64 x 64 m square in the map's local frame (on the 8 m grid of the far terrain, which build_assets.py
cuts open there) in the northern part of Torshovdalen, about 320 m from home: the largest free, grass-only area of
the playable map (no buildings, roads, paths, trees, bushes or game features; found by tools/make-site.py --search).

Ground: a soil height field of 0.25 m cells over the rectangle. Its surface is the game's own terrain height
(FHKData::RelLocal) plus an offset (stored here as int16 centimetres): a levelled pad (cut into the slope uphill,
filled downhill) that blends back to the terrain at the fence, a foundation pit 2.6 m deep with a ramp, and three
loose stockpiles. Below 'rock' (4.5 m under the pad / original ground, whichever is lower) nothing can be dug.

Writes unreal/Haarklous/Content/Data/site.json:
  rect {x0, s0, w, d} (local metres), cell, nx, ns, offsets (base64 int16 cm, row-major s then x), firm (the same for
  the top of the compacted ground: loose soil above it slumps to the angle of repose), rock (cm below base),
  padLevel, gate {side, from, to}, machines [{kind, x, z, yaw}] (world, web yaw), props [{kind, x, z, yaw, ...}],
  loads [{kind, x, z, yaw}]
"""
import base64
import json
import math
import os

import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
DATA = os.path.join(ROOT, 'unreal', 'Haarklous', 'Content', 'Data')
X0, S0, W, D, CELL = -80.0, 304.0, 64.0, 64.0, 0.25   # on the far terrain's 8 m grid (it is cut open there)
INSET = 9.0                       # the pad starts this far inside the fence; the slopes in between blend to the terrain


def smooth(t):
    t = np.clip(t, 0, 1)
    return t * t * (3 - 2 * t)


def main():
    s = json.load(open(os.path.join(DATA, 'surroundings.json'), encoding='utf-8'))
    th, gx, gz, ground = s['geo']['theta'], s['geo']['x'], s['geo']['z'], s['ground']
    cT, sT = math.cos(th), math.sin(th)
    to_world = lambda x, z: (x * cT + z * sT + gx, -x * sT + z * cT + gz)
    ter = s['terrain']
    n, half, step = ter['n'], ter['half'], ter['step']
    grid = np.frombuffer(base64.b64decode(ter['dm']), dtype='<i2', count=n * n).astype(np.float64).reshape(n, n) / 10.0

    def rel(x, z):                # FHKData::RelLocal
        fi = np.clip((x + half) / step, 0, n - 1.001); fj = np.clip((z + half) / step, 0, n - 1.001)
        i, j = np.floor(fi).astype(int), np.floor(fj).astype(int)
        u, v = fi - i, fj - j
        return grid[j, i] * (1 - u) * (1 - v) + grid[j, i + 1] * u * (1 - v) + grid[j + 1, i] * (1 - u) * v + grid[j + 1, i + 1] * u * v

    nx, ns = int(W / CELL) + 1, int(D / CELL) + 1
    xs = X0 + np.arange(nx) * CELL
    ss = S0 + np.arange(ns) * CELL
    LX, LS = np.meshgrid(xs, ss)
    base = ground + rel(LX, LS)
    edge = np.minimum(np.minimum(LX - X0, X0 + W - LX), np.minimum(LS - S0, S0 + D - LS))
    inner = (edge >= INSET)
    pad = float(np.median(base[inner]))
    w = smooth(edge / INSET)
    surf = base + (pad - base) * w
    rng = np.random.default_rng(7)
    noise = np.zeros_like(base)
    for k in range(6):
        fx, fs, ph = rng.uniform(0.2, 0.9), rng.uniform(0.2, 0.9), rng.uniform(0, 6.28)
        noise += np.sin(LX * fx + ph) * np.sin(LS * fs + ph * 1.3) / (k + 1)
    # Foundation pit: 18 x 12 m, 2.6 m deep, 60 degree walls, a 1:5 ramp from its east (local +x) end.
    pcx, pcs, pw, pd, depth = X0 + W * 0.42, S0 + D * 0.38, 18.0, 12.0, 2.6
    dx = np.abs(LX - pcx) - pw / 2
    ds = np.abs(LS - pcs) - pd / 2
    outside = np.maximum(dx, ds)
    wall = np.clip(-outside * math.tan(math.radians(60)), 0, depth)
    pit = wall
    ramp_x0 = pcx + pw / 2
    on_ramp = (LX > ramp_x0 - 0.5) & (LX < ramp_x0 + depth * 5) & (np.abs(LS - pcs) < 2.6)
    ramp_depth = np.clip(depth - (LX - ramp_x0) / 5, 0, depth)
    pit = np.where(on_ramp, np.maximum(pit, ramp_depth * smooth((2.6 - np.abs(LS - pcs)) / 0.8)), pit)
    surf -= pit
    # Tyre ruts and unevenness on the pad (a few centimetres).
    surf += inner * 0.035 * noise
    firm = surf.copy()
    # Stockpiles of loose soil: elongated cones at the angle of repose (about 34 degrees), on the pad's downhill side.
    piles = [(X0 + W * 0.25, S0 + D * 0.75, 5.5, 3.2, 1.6), (X0 + W * 0.62, S0 + D * 0.8, 4.5, 2.6, 1.2), (X0 + W * 0.8, S0 + D * 0.55, 3.8, 2.2, 1.0)]
    for (cx, cs, r, h, stretch) in piles:
        dist = np.hypot((LX - cx) / stretch, LS - cs)
        cone = np.clip(h * (1 - dist / r), 0, None) * (1 + 0.06 * noise)
        surf = np.maximum(surf, np.where(cone > 0, pad + cone, surf))
    off = np.round((surf - base) * 100).astype('<i2')
    firm_off = np.round((firm - base) * 100).astype('<i2')
    rock = 450
    # Gate: on the north-west side (local s0), where the park outside is within 1.5 m of the pad, so vehicles drive in
    # and out on a gentle slope (the user asked for a drivable entrance, 28 Oct 2026); 10.5 m wide (three fence panels).
    # The site offices stay by the east fence, where the first gate was.
    sides = {'x0': (X0, None), 'x1': (X0 + W, None), 's0': (None, S0), 's1': (None, S0 + D)}
    gate_side, gate_c = 's0', -43.25
    pts = []
    for rd in s['roads']:
        if rd['k'] in ('footway', 'path', 'cycleway', 'service', 'residential', 'track'):
            pts += list(np.array(rd['p']).reshape(-1, 2))
    pts = np.array(pts)
    W_ = lambda x, z: [round(v, 3) for v in to_world(x, z)]

    def yaw_to(fx, fz):           # web yaw whose forward (-sin, -cos) points along (fx, fz)
        return math.atan2(-fx, -fz)

    ex = np.array([cT, -sT]); es = np.array([sT, cT])   # world directions of local +x and +s
    def wyaw(lx_dir, ls_dir):
        v = ex * lx_dir + es * ls_dir
        return round(yaw_to(v[0], v[1]), 4)

    machines = [
        {'kind': 'excavator', 'at': (pcx - pw / 2 - 2.5, pcs + 2.0), 'face': (1, 0)},
        {'kind': 'hauler', 'at': (pcx - pw / 2 + 1.1, pcs + pd / 2 + 3.5), 'face': (1, 0)},  # body beside the excavator, free road east
        {'kind': 'tipper', 'at': (gate_c, S0 + 12.0), 'face': (0, -1)},                   # by the gate, ready to drive out
        {'kind': 'dozer', 'at': (X0 + W * 0.36, S0 + D * 0.66), 'face': (0, 1)},
        {'kind': 'loader', 'at': (X0 + W * 0.8, S0 + D * 0.55 + 10.5), 'face': (0, -1)},  # facing a stockpile
        {'kind': 'crane', 'at': (X0 + 8.0, S0 + 9.0), 'face': (1, 0)},             # loads within reach of its boom
        {'kind': 'tower', 'at': (pcx + 2.0, pcs - pd / 2 - 4.5), 'face': (-1, 0)},          # jib over the crane loads
    ]
    out_m = []
    for m in machines:
        wx, wz = to_world(*m['at'])
        out_m.append({'kind': m['kind'], 'x': round(wx, 3), 'z': round(wz, 3), 'yaw': wyaw(*m['face'])})
    # Props: fence panels (3.5 m Heras panels) along the border except a 10 m gate, site office containers, toilet,
    # floodlight towers, pallets, pipes, signs, a skip.
    props = []
    def side_points(name):
        if name in ('s0', 's1'):
            sz = S0 if name == 's0' else S0 + D
            return [(X0 + t, sz) for t in np.arange(0, W + 1e-6, 3.5)], (1, 0)
        sx = X0 if name == 'x0' else X0 + W
        return [(sx, S0 + t) for t in np.arange(0, D + 1e-6, 3.5)], (0, 1)
    for name in sides:
        p, dirv = side_points(name)
        L = len(p)
        for k in range(L - 1):
            a, b = np.array(p[k]), np.array(p[k + 1])
            mid = (a + b) / 2
            if name == gate_side and abs(mid[0] - gate_c) < 5.3:
                continue
            props.append({'kind': 'fence', 'x': W_(*mid)[0], 'z': W_(*mid)[1], 'yaw': wyaw(-dirv[1], dirv[0]), 'len': round(float(np.hypot(*(b - a))), 3)})
    # The office area by the east fence (where the first gate was).
    gx_, gs_ = X0 + W, S0 + D / 2
    inward = (-1, 0)
    along = (inward[1], -inward[0])
    def at(o_in, o_along):
        return (gx_ + inward[0] * o_in + along[0] * o_along, gs_ + inward[1] * o_in + along[1] * o_along)
    for k, (oi, oa, kind) in enumerate([(4.5, 11, 'office'), (4.5, 17.5, 'office'), (4.5, -11, 'office_wc'), (7.0, -16.5, 'toilet'),
                                        (12.0, 20, 'pallets'), (14.5, 20, 'pallets'),
                                        (12.0, 23, 'pipes'), (18, -18, 'skip'), (3, 24.5, 'light'), (3, -24.5, 'light')]):
        lx, ls = at(oi, oa)
        props.append({'kind': kind, 'x': W_(lx, ls)[0], 'z': W_(lx, ls)[1], 'yaw': wyaw(*inward)})
    # The gate: signs either side (facing out), the two gate leaves swung open outside, a light.
    for oa in (-7.0, 7.0):
        props.append({'kind': 'sign', 'x': W_(gate_c + oa, S0 + 2.0)[0], 'z': W_(gate_c + oa, S0 + 2.0)[1], 'yaw': wyaw(0, 1)})
    for sx in (-1, 1):
        hinge = gate_c + sx * 5.25
        lx, ls = hinge - sx * .15, S0 - 1.8
        props.append({'kind': 'fence', 'x': W_(lx, ls)[0], 'z': W_(lx, ls)[1], 'yaw': wyaw(-1, 0), 'len': 3.5})
    # Access track: gravel from the gate out to the nearest park path (points in the world with the ground height).
    def ground_at(x, z):
        return float(ground + rel(np.array(x), np.array(z)))
    outside = pts[pts[:, 1] < S0 - 8]
    q = outside[np.argmin(np.hypot(outside[:, 0] - gate_c, outside[:, 1] - S0))]
    # A smooth curve (quadratic Bezier): straight out of the gate, then bending round to the path; a point every ~1 m.
    p0, p2 = np.array([gate_c, S0 + .2]), np.array([float(q[0]), float(q[1])])
    p1 = np.array([gate_c, S0 - max(6.0, (S0 - p2[1]) * .6)])
    steps = max(2, int(math.ceil(np.linalg.norm(p1 - p0) + np.linalg.norm(p2 - p1))))
    track = []
    for t in np.linspace(0, 1, steps + 1):
        x, z = (1 - t) ** 2 * p0 + 2 * (1 - t) * t * p1 + t * t * p2
        wx, wz = to_world(x, z)
        track.append([round(wx, 3), round(ground_at(x, z) + .04, 3), round(wz, 3)])
    for (lx, ls) in ((X0 + 1.5, S0 + D - 1.5), (X0 + W - 1.5, S0 + D - 1.5)):
        props.append({'kind': 'light', 'x': W_(lx, ls)[0], 'z': W_(lx, ls)[1], 'yaw': wyaw(*inward)})
    # Crane loads near the tower crane: brick pallets, a bundle of steel beams, a concrete skip.
    loads = []
    for k, (oa, os_, kind) in enumerate([(-6, -3, 'bricks'), (-6, -5.5, 'bricks'), (-9.5, -4, 'beams'), (-3, -5, 'skip')]):
        lx, ls = pcx + oa, pcs - pd / 2 - 4.5 + os_
        loads.append({'kind': kind, 'x': W_(lx, ls)[0], 'z': W_(lx, ls)[1], 'yaw': wyaw(1, 0)})
    out = {'rect': {'x0': X0, 's0': S0, 'w': W, 'd': D}, 'cell': CELL, 'nx': nx, 'ns': ns,
           'offsets': base64.b64encode(off.tobytes()).decode(), 'firm': base64.b64encode(firm_off.tobytes()).decode(), 'rock': rock, 'padLevel': round(pad, 3),
           'pit': {'x': pcx, 's': pcs, 'w': pw, 'd': pd, 'depth': depth},
           'gate': {'side': gate_side, 'centre': gate_c, 'width': 10.5}, 'track': track, 'machines': out_m, 'props': props, 'loads': loads}
    json.dump(out, open(os.path.join(DATA, 'site.json'), 'w'))
    print(f'HK: site {W:.0f} x {D:.0f} m, pad at {pad:.2f} m (terrain {base.min():.1f}..{base.max():.1f}), '
          f'cut {-(surf - base).clip(None, 0).sum() * CELL * CELL:.0f} m3, fill {(surf - base).clip(0).sum() * CELL * CELL:.0f} m3, '
          f'gate {gate_side} at x {gate_c}, track {len(track)} m, {len(props)} props, centre {to_world(X0 + W / 2, S0 + D / 2)}')


main()
