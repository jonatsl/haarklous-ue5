"""The construction site (new feature, asked for by the user on 28 Oct 2026): place, ground shape, machines and props.

    python tools/make-site.py

28 Oct 2026, later: the user asked for a bigger site, 140 x 90 m. The fence encloses 140 x 90 m (local x -86..54,
s 275..365) inside a 144 x 96 m soil field (x -88..56, s 272..368, on the 8 m grid of the far terrain, which
build_assets.py cuts open there) in Torshovdalen, about 320 m from home. It takes in the first 64 x 64 m site (the
valley floor) and the slope east of it. No free 140 x 90 m area exists near home (every nearby spot has paths and some
trees in it); the park paths, trees, bushes and lamps inside the field are gone (build_assets.py, AHKGame::SpawnTrees,
FHKData), and the long path through the valley now runs in through the north gate and out through the south gate.

Ground: a soil height field of 0.25 m cells over the field. Its surface is the game's own terrain height
(FHKData::RelLocal) plus an offset (stored here as int16 centimetres): a graded platform (a plane fitted to the
ground, at most 5 % along x and 3 % along s) that blends back to the terrain over the outer 12 m (cut banks uphill,
fill downhill), a foundation pit 3 m deep with a ramp, and stockpiles of loose soil. Below 'rock' (4.5 m under the
platform / original ground, whichever is lower) nothing can be dug.

Writes unreal/Haarklous/Content/Data/site.json:
  rect {x0, s0, w, d} (the soil field, local metres), fence {x0, s0, w, d}, cell, nx, ns, offsets (base64 int16 cm,
  row-major s then x), firm (the same for the top of the compacted ground: loose soil above it slumps to the angle of
  repose), rock (cm below base), padLevel (the platform at the field's centre), grade, pit, gates [{side, centre,
  width}] (gate: the first), tracks [[[x, y, z] ...] ...] (gravel aprons through the gates, world), machines
  [{kind, x, z, yaw}] (world, web yaw), props [{kind, x, z, yaw, ...}], loads [{kind, x, z, yaw}]
"""
import base64
import json
import math
import os

import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
DATA = os.path.join(ROOT, 'unreal', 'Haarklous', 'Content', 'Data')
X0, S0, W, D, CELL = -88.0, 272.0, 144.0, 96.0, 0.25    # the soil field, on the far terrain's 8 m grid (cut open there)
FX0, FS0, FW, FD = -86.0, 275.0, 140.0, 90.0             # the fence: 140 x 90 m (the user's size)
INSET = 12.0                      # the platform starts this far inside the field; the banks in between meet the terrain
GATES = [('s0', 2.8), ('s1', -9.7)]   # where the valley path met the fence: north and south gates, 10.5 m wide


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

    def ground_at(x, z):
        return float(ground + rel(np.array(x), np.array(z)))

    nx, ns = int(W / CELL) + 1, int(D / CELL) + 1
    xs = X0 + np.arange(nx) * CELL
    ss = S0 + np.arange(ns) * CELL
    LX, LS = np.meshgrid(xs, ss)
    base = ground + rel(LX, LS)
    edge = np.minimum(np.minimum(LX - X0, X0 + W - LX), np.minimum(LS - S0, S0 + D - LS))
    inner = (edge >= INSET)
    # The platform: the plane closest to the ground (least absolute cut and fill), its slopes limited so the machines
    # work on a gentle grade.
    xc, sc = X0 + W / 2, S0 + D / 2
    bi, xi, si = base[inner][::7], LX[inner][::7], LS[inner][::7]
    best = None
    for gxs in np.linspace(-.05, .05, 21):
        for gss in np.linspace(-.03, .03, 13):
            r = bi - gxs * (xi - xc) - gss * (si - sc)
            a = float(np.median(r))
            cost = float(np.abs(r - a).sum())
            if best is None or cost < best[0]:
                best = (cost, a, float(gxs), float(gss))
    _, pad, gxs, gss = best
    level = lambda x, z: pad + gxs * (x - xc) + gss * (z - sc)
    plane = level(LX, LS)
    w = smooth(edge / INSET)
    surf = base + (plane - base) * w
    rng = np.random.default_rng(7)
    noise = np.zeros_like(base)
    for k in range(6):
        fx, fs, ph = rng.uniform(0.2, 0.9), rng.uniform(0.2, 0.9), rng.uniform(0, 6.28)
        noise += np.sin(LX * fx + ph) * np.sin(LS * fs + ph * 1.3) / (k + 1)
    # Foundation pit: 30 x 16 m, 3 m deep, 60 degree walls, a 1:6 ramp from its east (local +x) end.
    pcx, pcs, pw, pd, depth = -48.0, 324.0, 30.0, 16.0, 3.0
    dx = np.abs(LX - pcx) - pw / 2
    ds = np.abs(LS - pcs) - pd / 2
    outside = np.maximum(dx, ds)
    wall = np.clip(-outside * math.tan(math.radians(60)), 0, depth)
    pit = wall
    ramp_x0 = pcx + pw / 2
    on_ramp = (LX > ramp_x0 - 0.5) & (LX < ramp_x0 + depth * 6) & (np.abs(LS - pcs) < 3.0)
    ramp_depth = np.clip(depth - (LX - ramp_x0) / 6, 0, depth)
    pit = np.where(on_ramp, np.maximum(pit, ramp_depth * smooth((3.0 - np.abs(LS - pcs)) / 0.8)), pit)
    surf -= pit
    # Tyre ruts and unevenness on the platform (a few centimetres).
    surf += inner * 0.035 * noise
    firm = surf.copy()
    # Stockpiles of loose soil: elongated cones at the angle of repose (about 34 degrees).
    piles = [(-72.0, 350.0, 5.5, 3.2, 1.6), (-20.0, 350.0, 4.5, 2.6, 1.2), (8.0, 332.0, 3.8, 2.2, 1.0),
             (24.0, 300.0, 5.0, 2.9, 1.4), (32.0, 348.0, 4.2, 2.4, 1.2)]
    for (cx, cs, r, h, stretch) in piles:
        dist = np.hypot((LX - cx) / stretch, LS - cs)
        cone = np.clip(h * (1 - dist / r), 0, None) * (1 + 0.06 * noise)
        surf = np.maximum(surf, np.where(cone > 0, level(cx, cs) + cone, surf))
    off = np.round((surf - base) * 100).astype('<i2')
    firm_off = np.round((firm - base) * 100).astype('<i2')
    rock = 450
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

    north = GATES[0][1]
    machines = [
        {'kind': 'excavator', 'at': (pcx - pw / 2 - 2.5, pcs + 2.0), 'face': (1, 0)},
        {'kind': 'hauler', 'at': (pcx - pw / 2 + 1.1, pcs + pd / 2 + 3.5), 'face': (1, 0)},  # body beside the excavator, free road east
        {'kind': 'tipper', 'at': (north, FS0 + 12.0), 'face': (0, -1)},                      # by the north gate, ready to drive out
        {'kind': 'dozer', 'at': (-20.0, 338.0), 'face': (0, 1)},                             # facing a stockpile
        {'kind': 'loader', 'at': (24.0, 300.0 + 10.5), 'face': (0, -1)},                     # facing a stockpile
        {'kind': 'crane', 'at': (pcx - 18.0, pcs - pd / 2 - 9.0), 'face': (1, 0)},          # loads within reach of its boom
        {'kind': 'tower', 'at': (pcx + 2.0, pcs - pd / 2 - 4.5), 'face': (-1, 0)},          # jib over the crane loads
    ]
    out_m = []
    for m in machines:
        wx, wz = to_world(*m['at'])
        out_m.append({'kind': m['kind'], 'x': round(wx, 3), 'z': round(wz, 3), 'yaw': wyaw(*m['face'])})
    # Props: fence panels (3.5 m Heras panels) along the fence except the gates, site office containers, toilet,
    # floodlight towers, pallets, pipes, signs, a skip.
    props = []

    def side_points(name):
        if name in ('s0', 's1'):
            sz = FS0 if name == 's0' else FS0 + FD
            return [(FX0 + t, sz) for t in list(np.arange(0, FW, 3.5)) + [FW]], (1, 0)
        sx = FX0 if name == 'x0' else FX0 + FW
        return [(sx, FS0 + t) for t in list(np.arange(0, FD, 3.5)) + [FD]], (0, 1)
    for name in ('x0', 'x1', 's0', 's1'):
        p, dirv = side_points(name)
        for k in range(len(p) - 1):
            a, b = np.array(p[k]), np.array(p[k + 1])
            if np.hypot(*(b - a)) < .2:
                continue
            mid = (a + b) / 2
            if any(name == gs and abs(mid[0] - gc) < 5.3 for gs, gc in GATES):
                continue
            props.append({'kind': 'fence', 'x': W_(*mid)[0], 'z': W_(*mid)[1], 'yaw': wyaw(-dirv[1], dirv[0]), 'len': round(float(np.hypot(*(b - a))), 3)})
    # The office area inside the north gate, east of it.
    gx_, gs_ = north + 22.0, FS0
    inward = (0, 1)
    along = (-1, 0)          # from the office area toward the gate

    def at(o_in, o_along):
        return (gx_ + inward[0] * o_in + along[0] * o_along, gs_ + inward[1] * o_in + along[1] * o_along)
    for (oi, oa, kind) in [(4.5, -4, 'office'), (4.5, -10.5, 'office'), (4.5, 7, 'office_wc'), (7.0, 12.5, 'toilet'),
                           (14.0, -6, 'pallets'), (16.5, -6, 'pallets'), (14.0, -2, 'pipes'), (18, 10, 'skip'), (2.5, 16, 'light')]:
        lx, ls = at(oi, oa)
        props.append({'kind': kind, 'x': W_(lx, ls)[0], 'z': W_(lx, ls)[1], 'yaw': wyaw(*inward)})
    # Each gate: signs either side (facing out), the two gate leaves swung open outside, and a gravel apron through it
    # from the cut path outside to 10 m inside (a smooth curve, a point every ~1 m, on the ground or the soil).
    gates, tracks = [], []
    for gside, gc in GATES:
        sz, out = (FS0, -1.0) if gside == 's0' else (FS0 + FD, 1.0)
        for oa in (-7.0, 7.0):
            props.append({'kind': 'sign', 'x': W_(gc + oa, sz - out * 2.0)[0], 'z': W_(gc + oa, sz - out * 2.0)[1], 'yaw': wyaw(0, -out)})
        for sx in (-1, 1):
            hinge = gc + sx * 5.25
            lx, ls = hinge - sx * .15, sz + out * 1.8
            props.append({'kind': 'fence', 'x': W_(lx, ls)[0], 'z': W_(lx, ls)[1], 'yaw': wyaw(-1, 0), 'len': 3.5})
        gates.append({'side': gside, 'centre': gc, 'width': 10.5})
        edge_s = S0 if out < 0 else S0 + D
        cand = pts[(pts[:, 1] - edge_s) * out > 1]
        q = cand[np.argmin(np.hypot(cand[:, 0] - gc, cand[:, 1] - sz))]
        p0, p2 = np.array([float(q[0]), float(q[1])]), np.array([gc, sz - out * 10.0])
        p1 = np.array([gc, sz + out * max(4.0, abs(q[1] - sz) * .6)])
        steps = max(2, int(math.ceil(np.linalg.norm(p1 - p0) + np.linalg.norm(p2 - p1))))
        tr = []
        for t in np.linspace(0, 1, steps + 1):
            x, z = (1 - t) ** 2 * p0 + 2 * (1 - t) * t * p1 + t * t * p2
            wx, wz = to_world(x, z)
            i, j = int(round((x - X0) / CELL)), int(round((z - S0) / CELL))
            y = float(surf[j, i]) if 0 <= i < nx and 0 <= j < ns else ground_at(x, z)
            tr.append([round(wx, 3), round(y + .04, 3), round(wz, 3)])
        tracks.append(tr)
    for (lx, ls) in ((FX0 + 1.5, FS0 + FD - 1.5), (FX0 + FW - 1.5, FS0 + FD - 1.5), (FX0 + 1.5, FS0 + 1.5), (pcx, pcs + pd / 2 + 8)):
        props.append({'kind': 'light', 'x': W_(lx, ls)[0], 'z': W_(lx, ls)[1], 'yaw': wyaw(0, 1)})
    # Crane loads near the tower crane: brick pallets, a bundle of steel beams, a concrete skip.
    loads = []
    for (oa, os_, kind) in [(-6, -3, 'bricks'), (-6, -5.5, 'bricks'), (-9.5, -4, 'beams'), (-3, -5, 'skip')]:
        lx, ls = pcx + oa, pcs - pd / 2 - 4.5 + os_
        loads.append({'kind': kind, 'x': W_(lx, ls)[0], 'z': W_(lx, ls)[1], 'yaw': wyaw(1, 0)})
    out = {'rect': {'x0': X0, 's0': S0, 'w': W, 'd': D}, 'fence': {'x0': FX0, 's0': FS0, 'w': FW, 'd': FD}, 'cell': CELL, 'nx': nx, 'ns': ns,
           'offsets': base64.b64encode(off.tobytes()).decode(), 'firm': base64.b64encode(firm_off.tobytes()).decode(), 'rock': rock,
           'padLevel': round(pad, 3), 'grade': [round(gxs, 4), round(gss, 4)], 'pit': {'x': pcx, 's': pcs, 'w': pw, 'd': pd, 'depth': depth},
           'gate': gates[0], 'gates': gates, 'tracks': tracks, 'machines': out_m, 'props': props, 'loads': loads}
    json.dump(out, open(os.path.join(DATA, 'site.json'), 'w'))
    d = surf - base
    print(f'HK: site field {W:.0f} x {D:.0f} m, fence {FW:.0f} x {FD:.0f} m, platform {pad:.2f} m at the centre, '
          f'grade {gxs * 100:.1f} % / {gss * 100:.1f} % (terrain {base.min():.1f}..{base.max():.1f}), '
          f'cut {-d.clip(None, 0).sum() * CELL * CELL:.0f} m3 (max {-d.min():.1f} m), fill {d.clip(0).sum() * CELL * CELL:.0f} m3 '
          f'(max {d.max():.1f} m), tracks {[len(t) for t in tracks]} points, {len(props)} props, centre {to_world(X0 + W / 2, S0 + D / 2)}')
    for gside, gc in GATES:
        sz = FS0 if gside == 's0' else FS0 + FD
        j, i = int(round((sz - S0) / CELL)), int(round((gc - X0) / CELL))
        print(f'HK: gate {gside} at x {gc}: soil {surf[j, i]:.2f}, terrain 6 m outside {ground_at(gc, sz + (-6 if gside == "s0" else 6)):.2f}')


main()
