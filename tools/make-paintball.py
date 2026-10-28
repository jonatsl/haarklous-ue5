"""The urban paintball / reball course (new feature, asked for by the user on 28 Oct 2026): place, floor level, walls,
play area, lobby, team bases, flags, spawn points and the cover (containers, building shells, walls with windows,
barriers, a car wreck, cable spools, sandbags, barrels, pallets, tyres).

Place: in the park valley next to the construction site, a 64 x 48 m paved yard in the map's local frame
(x -112..-48, s 376..424, on the far terrain's 8 m grid, which build_assets.py cuts open). The ground there falls 7 m
from east to west, so the yard is one level (the median height) with retaining walls: a parapet on the low sides, the
hill held back on the high sides. The play area is 60 x 40 m (x -110..-50, s 378..418) inside a 5 m net; south of it a
spectator corridor and the gate (x -88..-84, where the lawn outside is level with the yard). The lobby is the lawn
outside the gate: walking into it joins the paintball lobby (the first one in is the admin).

Course coordinates (u, v): metres from the play area's centre along local x and s; the course rig's root sits there,
turned by the map's theta, so (u, y, v) maps to the world like the rig's children. Blue plays from the west end
(u = -30), red from the east (u = +30); the cover is point-symmetric so both sides are equal.

Output: unreal/Haarklous/Content/Data/paintball.json (read by HKPaintball.cpp and tools/blender/build_paintball.py).
"""
import base64
import json
import math
import os

import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
DATA = os.path.join(ROOT, 'unreal', 'Haarklous', 'Content', 'Data')
RECT = dict(x0=-112.0, s0=376.0, w=64.0, d=48.0)          # the yard (terrain cut), local frame
PLAY = dict(x0=-110.0, s0=378.0, w=60.0, d=40.0)          # inside the net
GATE = (-88.0, -84.0)                                    # x range of the gate in the south wall (s = 424)
LOBBY = dict(x0=-98.0, s0=419.0, w=28.0, d=17.0)          # spectator corridor + the lawn outside the gate

# One half of the course (blue, u < 0); the other half is the same turned by 180 degrees about the centre.
# kind, u, v, yaw (radians, about the vertical, course coordinates)
HALF = [
    ('barrier', -24.0, -6.0, math.pi / 2), ('barrier', -24.0, 6.0, math.pi / 2),
    ('container', -17.0, -12.0, 0.0), ('container', -15.0, 11.5, math.pi / 2),
    ('shell', -8.5, -4.0, 0.0), ('car', -20.0, 2.0, .5), ('spool', -12.0, 16.0, 0.0),
    ('sandbags', -5.0, 12.5, math.pi / 2), ('wall', -6.0, -15.0, 0.0), ('barrels', -22.0, -16.5, 0.0),
    ('pallets', -27.0, 14.0, 0.0), ('tires', -3.0, -9.5, 0.0), ('barrier', 0.0, 13.5, 0.0), ('pallets', -1.5, 17.5, .3),
    ('wall', -13.0, 4.5, math.pi / 2),
]
CENTRE = [('wall', 0.0, 0.0, math.pi / 2)]                 # on the centre itself (already symmetric)

# Collision boxes of each kind in its own frame: (x, z, w, d, y0, h); w along x, d along z.
def boxes(kind):
    if kind == 'barrier':
        return [(0, 0, 3.0, .6, 0, .9)]
    if kind == 'container':
        return [(0, 0, 6.06, 2.44, 0, 2.6)]
    if kind == 'car':
        return [(0, 0, 4.2, 1.8, 0, .95), (.2, 0, 2.2, 1.6, .95, .5)]
    if kind == 'spool':
        return [(0, 0, 1.0, 1.6, 0, 1.6)]
    if kind == 'sandbags':
        return [(0, 0, 3.0, .7, 0, 1.0)]
    if kind == 'barrels':
        return [(x, z, .6, .6, 0, .9) for x, z in ((0, 0), (.7, .2), (.3, .75))]
    if kind == 'pallets':
        return [(0, 0, 1.2, 1.0, 0, 1.3)]
    if kind == 'tires':
        return [(0, 0, .75, .75, 0, 1.0), (.8, 0, .75, .75, 0, .75)]
    if kind == 'wall':     # 3 m concrete wall with a window (1 m wide, 0.9 to 1.6 m)
        return [(-1.0, 0, 1.0, .25, 0, 2.2), (1.0, 0, 1.0, .25, 0, 2.2), (0, 0, 1.0, .25, 0, .9), (0, 0, 1.0, .25, 1.6, .6)]
    if kind == 'shell':    # 7 x 6 m roofless building: a door in each long wall, a window in each short wall
        t, h, out = .25, 2.7, []
        out += [(-2.0, -3.0, 3.0, t, 0, h), (2.8, -3.0, 1.4, t, 0, h), (1.55, -3.0, 1.1, t, 2.1, .6)]     # north: door 1.0..2.1
        out += [(2.0, 3.0, 3.0, t, 0, h), (-2.8, 3.0, 1.4, t, 0, h), (-1.55, 3.0, 1.1, t, 2.1, .6)]      # south: door -2.1..-1.0
        for sx in (-3.5, 3.5):
            out += [(sx, -1.75, t, 2.5, 0, h), (sx, 1.75, t, 2.5, 0, h), (sx, 0, t, 1.0, 0, .9), (sx, 0, t, 1.0, 1.7, 1.0)]
        return out
    raise ValueError(kind)


def main():
    s = json.load(open(os.path.join(DATA, 'surroundings.json'), encoding='utf-8'))
    th, gx, gz = s['geo']['theta'], s['geo']['x'], s['geo']['z']
    cT, sT = math.cos(th), math.sin(th)
    to_world = lambda x, z: (x * cT + z * sT + gx, -x * sT + z * cT + gz)
    ter = s['terrain']
    n, half, step = ter['n'], ter['half'], ter['step']
    grid = np.frombuffer(base64.b64decode(ter['dm']), dtype='<i2', count=n * n).astype(np.float64).reshape(n, n) / 10.0

    def ground(x, z):              # FHKData::HeightWorld at local (x, s)
        fi = np.clip((x + half) / step, 0, n - 1.001); fj = np.clip((z + half) / step, 0, n - 1.001)
        i, j = np.floor(fi).astype(int), np.floor(fj).astype(int)
        u, v = fi - i, fj - j
        return s['ground'] + grid[j, i] * (1 - u) * (1 - v) + grid[j, i + 1] * u * (1 - v) + grid[j + 1, i] * (1 - u) * v + grid[j + 1, i + 1] * u * v

    xs = np.arange(RECT['x0'], RECT['x0'] + RECT['w'] + .1, 1.0)
    ss = np.arange(RECT['s0'], RECT['s0'] + RECT['d'] + .1, 1.0)
    X, S = np.meshgrid(xs, ss)
    floor = round(float(np.median(ground(X, S))), 2)
    cx, cs = PLAY['x0'] + PLAY['w'] / 2, PLAY['s0'] + PLAY['d'] / 2
    # The wall's outer ground along the yard's edge, every metre (for the retaining walls in Blender).
    def edge(points):
        return [[round(float(px - cx), 2), round(float(ps - cs), 2), round(float(ground(px, ps)), 2)] for px, ps in points]
    x0, x1, s0, s1 = RECT['x0'], RECT['x0'] + RECT['w'], RECT['s0'], RECT['s0'] + RECT['d']
    edges = {'north': edge([(x, s0) for x in xs]), 'south': edge([(x, s1) for x in xs]),
             'west': edge([(x0, z) for z in ss]), 'east': edge([(x1, z) for z in ss])}
    pieces = []
    for k, u, v, yaw in HALF:
        pieces.append({'kind': k, 'u': u, 'v': v, 'yaw': round(yaw, 4)})
        pieces.append({'kind': k, 'u': -u, 'v': -v, 'yaw': round(yaw + math.pi, 4)})
    for k, u, v, yaw in CENTRE:
        pieces.append({'kind': k, 'u': u, 'v': v, 'yaw': round(yaw, 4)})
    coll = []
    for p in pieces:
        c, sn = math.cos(p['yaw']), math.sin(p['yaw'])
        for bx, bz, w, d, y0, h in boxes(p['kind']):
            # three.js rotation about y: (x, z) -> (x cos + z sin, -x sin + z cos)
            coll.append([round(p['u'] + bx * c + bz * sn, 3), round(p['v'] - bx * sn + bz * c, 3), w, d, y0, h, p['yaw']])
    # Team bases: flag stand, base zone (|u| > 25), six spawn points facing the other end.
    bases = []
    for team, sgn in ((0, -1), (1, 1)):
        spawns = [[sgn * 28.5, sgn * v, round(sgn * math.pi / 2, 4)] for v in (-10, -6, -2, 2, 6, 10)]   # yaw -pi/2 faces +u
        bases.append({'team': team, 'flag': [sgn * 28.0, 0.0], 'spawns': spawns, 'zone': sgn * 25.0})
    out = {
        'rect': RECT, 'play': PLAY, 'lobby': LOBBY, 'gate': GATE, 'floor': floor,
        'centre': {'x': cx, 's': cs, 'world': [round(v, 3) for v in to_world(cx, cs)], 'theta': round(th, 6)},
        'edges': edges, 'pieces': pieces, 'boxes': coll, 'bases': bases,
    }
    json.dump(out, open(os.path.join(DATA, 'paintball.json'), 'w'))
    gnd = ground(X, S)
    print(f'HK: paintball yard {RECT["w"]:.0f} x {RECT["d"]:.0f} m at {floor:.2f} m (ground {gnd.min():.1f}..{gnd.max():.1f}), '
          f'{len(pieces)} cover pieces, {len(coll)} boxes, centre {out["centre"]["world"]}, '
          f'gate ground {float(ground(-86.0, 424.0)):.2f} m')


if __name__ == '__main__':
    main()
