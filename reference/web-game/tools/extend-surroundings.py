"""Grow the map data in surroundings-data.js from +/-600 m to +/-800 m without touching anything already there.

    python tools/extend-surroundings.py <osm-nodes.json>

29 Oct 2026: the user asked for the real Muselunden disc golf park, about 470-815 m north-east of home, beyond the old
edge of the world (walkable +/-540 m, terrain +/-600 m). This script:
- makes the terrain grid +/-800 m on the same 4 m step. Every old grid value is copied unchanged; only the new ring is read
  from the Kartverket DTM (assets/osm/kartverket-dtm-2m.tif, which covers +/-800 m), relative to the same door height;
- adds the OpenStreetMap trees (natural=tree) and street lamps (highway=street_lamp) that lie outside the area the old
  node extract covered (local x -446..449, s -382..449), up to +/-800 m. <osm-nodes.json> is an Overpass answer
  ('node[natural=tree](bbox); node[highway=street_lamp](bbox); out;'). Trees inside a building are left out, as in
  generate-surroundings.py. The Overpass answer is kept as assets/osm/torshov-nodes-800m.json, which the full generator
  also reads.
Nothing else changes: buildings, roads, areas, the own building, bushes and vehicles stay exactly as they are.
"""
import base64
import json
import math
import os
import sys

import numpy as np
from PIL import Image
from shapely.geometry import Point, Polygon
from shapely.strtree import STRtree

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
NEW_HALF, STEP = 800, 4
OLD_NODES = (-446.0, -382.0, 449.0, 449.0)          # x0, s0, x1, s1 of the old node extract (local metres)

src = open(os.path.join(ROOT, 'surroundings-data.js'), encoding='utf-8').read()
head, body = src[:src.index('{')], src[src.index('{'):src.rindex('}') + 1]
D = json.loads(body)
LAT0, LON0 = D['origin']['lat'], D['origin']['lon']
KX = 111320 * math.cos(math.radians(LAT0))

# --- terrain ---
ter = D['terrain']
old = np.frombuffer(base64.b64decode(ter['dm']), dtype='<i2', count=ter['n'] * ter['n']).reshape(ter['n'], ter['n'])
DTM_X0, DTM_Y1, DTM_RES = 264086 - 800, 6651859 + 800, 2.0     # as generate-surroundings.py
dtm = np.array(Image.open(os.path.join(ROOT, 'assets/osm/kartverket-dtm-2m.tif')), dtype=float)


def to_utm33(lat, lon):                     # the same formula as generate-surroundings.py
    a = 6378137.0; f = 1 / 298.257222101; k0 = 0.9996; lon0 = math.radians(15)
    e2 = f * (2 - f); ep2 = e2 / (1 - e2); phi = math.radians(lat); lam = math.radians(lon)
    N = a / math.sqrt(1 - e2 * math.sin(phi) ** 2); T = math.tan(phi) ** 2; C = ep2 * math.cos(phi) ** 2; A = math.cos(phi) * (lam - lon0)
    e4 = e2 * e2; e6 = e4 * e2
    M = a * ((1 - e2 / 4 - 3 * e4 / 64 - 5 * e6 / 256) * phi - (3 * e2 / 8 + 3 * e4 / 32 + 45 * e6 / 1024) * math.sin(2 * phi) + (15 * e4 / 256 + 45 * e6 / 1024) * math.sin(4 * phi) - (35 * e6 / 3072) * math.sin(6 * phi))
    x = k0 * N * (A + (1 - T + C) * A ** 3 / 6 + (5 - 18 * T + T * T + 72 * C - 58 * ep2) * A ** 5 / 120) + 500000
    y = k0 * (M + N * math.tan(phi) * (A * A / 2 + (5 - T + 9 * C + 4 * C * C) * A ** 4 / 24 + (61 - 58 * T + T * T + 600 * C - 330 * ep2) * A ** 6 / 720))
    return x, y


def height_local(x, s_):
    lat, lon = LAT0 + (-s_) / 111320, LON0 + x / KX
    ux, uy = to_utm33(lat, lon)
    c, r = (ux - DTM_X0) / DTM_RES - .5, (DTM_Y1 - uy) / DTM_RES - .5
    c = min(max(c, 0), dtm.shape[1] - 1.001); r = min(max(r, 0), dtm.shape[0] - 1.001)
    c0, r0 = int(c), int(r); fc, fr = c - c0, r - r0
    return dtm[r0, c0] * (1 - fc) * (1 - fr) + dtm[r0, c0 + 1] * fc * (1 - fr) + dtm[r0 + 1, c0] * (1 - fc) * fr + dtm[r0 + 1, c0 + 1] * fc * fr


# The door height the old grid was made with: the median of (DTM - stored value) on the old grid's outer rows.
oh = ter['half']
samples = [height_local(-oh + i * STEP, -oh + j * STEP) - old[j, i] / 10.0 for j in (0, ter['n'] - 1) for i in range(0, ter['n'], 10)]
H_DOOR = float(np.median(samples))
n = int(2 * NEW_HALF / STEP) + 1
grid = np.zeros((n, n), dtype='<i2')
off = (NEW_HALF - oh) // STEP
for j in range(n):
    for i in range(n):
        oi, oj = i - off, j - off
        if 0 <= oi < ter['n'] and 0 <= oj < ter['n']:
            grid[j, i] = old[oj, oi]
        else:
            grid[j, i] = int(round((height_local(-NEW_HALF + i * STEP, -NEW_HALF + j * STEP) - H_DOOR) * 10))
# The seam: how far the new ring's first row is from the old grid's last row (should be one grid step of slope only).
seam = [abs(int(grid[off - 1, i]) - int(grid[off, i])) / 10 for i in range(off, off + ter['n'])]
D['terrain'] = {**ter, 'half': NEW_HALF, 'n': n, 'dm': base64.b64encode(grid.tobytes()).decode('ascii')}

# --- trees and lamps outside the old node extract ---
nodes = json.load(open(sys.argv[1], encoding='utf-8'))['elements']
pairs = lambda f: [(f[i], f[i + 1]) for i in range(0, len(f), 2)]
polys = [Polygon(pairs(b['r']['o'])).buffer(0) for b in D['buildings']]
tree_index = STRtree(polys)
x0, s0, x1, s1 = OLD_NODES
added_t = added_l = 0
for e in nodes:
    x, s = (e['lon'] - LON0) * KX, -(e['lat'] - LAT0) * 111320
    if x0 <= x <= x1 and s0 <= s <= s1:
        continue
    if abs(x) > NEW_HALF or abs(s) > NEW_HALF:
        continue
    t = e.get('tags', {})
    if t.get('natural') == 'tree':
        p = Point(x, s)
        if any(polys[k].contains(p) for k in tree_index.query(p)):
            continue
        try:
            height = float(str(t.get('height', '0')).split()[0])
        except (ValueError, IndexError):
            height = 0
        D['trees'].extend([round(x, 1), round(s, 1), round(height, 1)]); added_t += 1
    elif t.get('highway') == 'street_lamp':
        D['lamps'].extend([round(x, 1), round(s, 1)]); added_l += 1

with open(os.path.join(ROOT, 'surroundings-data.js'), 'w', encoding='utf-8', newline='\n') as f:
    f.write(head + json.dumps(D, separators=(',', ':'), ensure_ascii=True) + ';\n')
with open(os.path.join(ROOT, 'assets/osm/torshov-nodes-800m.json'), 'w', encoding='utf-8', newline='\n') as f:
    json.dump({'note': 'OpenStreetMap trees and street lamps within +/-800 m (Overpass, 29 Oct 2026). Data (c) OpenStreetMap contributors, ODbL.',
               'elements': nodes}, f, separators=(',', ':'))
print(f'terrain {ter["n"]}x{ter["n"]} -> {n}x{n} (+/-{NEW_HALF} m), door height {H_DOOR:.3f} m (data {ter["doorHeight"]}), '
      f'seam step max {max(seam):.1f} m; trees +{added_t} (now {len(D["trees"]) // 3}), lamps +{added_l} (now {len(D["lamps"]) // 2}); '
      f'{len(nodes)} nodes kept in assets/osm/torshov-nodes-800m.json')
