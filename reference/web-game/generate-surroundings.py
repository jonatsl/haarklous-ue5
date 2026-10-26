"""Build surroundings-data.js from the OpenStreetMap extract in assets/osm/torshov-osm.json.

Map data © OpenStreetMap contributors, ODbL 1.0 (https://www.openstreetmap.org/copyright).

Placement (documented in docs/HANDOFF.md):
- Origin is the address node of Haarklous plass 17A (59.936404 N, 10.776657 E). Local metres: E east, N north.
- The apartment's balcony facade (model z = 0) lies on the south-west facade of OSM building 1031709397.
  Model +x runs north-west along that facade; the living room's outer north-west corner (model x = 2.883)
  is the facade corner where the five-storey wing 1031709412 begins (FINN photo 7 shows that wing beside the balcony).
- OSM gives the block 11.77 m deep, the floor plan about 10.32 m including the kitchen facade wall.
  Neighbouring geometry and imagery stay rigid (courtyard side exact); only the own building is squeezed
  in depth so its street facade meets the kitchen window wall. The 1.45 m difference is disclosed.
- Storeys: 2.85 m floor to floor, 4th-floor floor at model y = 0, street level at y = -9.975
  (first floor raised a half storey; the stair entrance is on the half landing).
"""
from pathlib import Path
import json
import math

from shapely.geometry import Polygon, LineString, Point, box
from shapely.ops import unary_union

root = Path(__file__).resolve().parent
osm = json.loads((root / 'assets/osm/torshov-osm.json').read_text(encoding='utf-8'))
LAT0, LON0 = 59.936404, 10.776657
KX = 111320 * math.cos(math.radians(LAT0))
OWN_ID, CORNER_NEXT = 1031709397, None
STOREY, GROUND, PLINTH = 2.85, -9.975, 1.425
APARTMENT_RIGHT_X, KITCHEN_FACADE_Z = 2.883, 10.32


def en(p):
    return ((p['lon'] - LON0) * KX, (p['lat'] - LAT0) * 111320)


ways = [e for e in osm['elements'] if e['type'] == 'way']
own = next(e for e in ways if e['id'] == OWN_ID)
own_pts = [en(p) for p in own['geometry']][:-1]
# Facade corner shared with wing 1031709412, and the next vertex along the south-west facade (south-east).
corner = min(own_pts, key=lambda p: math.hypot(p[0] + 9.09, p[1] + 6.57))
i = own_pts.index(corner)
nxt = own_pts[(i + 1) % len(own_pts)]
dx, dy = nxt[0] - corner[0], nxt[1] - corner[1]
L = math.hypot(dx, dy)
ue, un = -dx / L, -dy / L          # model +x (north-west) as an east/north unit vector
ve, vn = un, -ue                   # model +z (north-east, into the building)


def plan(e, n):
    """East/north metres -> model plan (x, z)."""
    a, b = e - corner[0], n - corner[1]
    return (APARTMENT_RIGHT_X + a * ue + b * un, a * ve + b * vn)


# Three.js group transform taking local (E, y, -N) to model coordinates.
theta = math.atan2(-un, ue)
gx, gz = plan(0, 0)
own_plan = [plan(*p) for p in own_pts]
own_depth = max(z for x, z in own_plan if abs(x - 2) < 12)
# The street facade line: the long edge parallel to x with the largest z.
street_z = sorted({round(z, 2) for x, z in own_plan})[-3]


def squeeze(z):
    if z <= 0:
        return z
    if z <= street_z:
        return z * KITCHEN_FACADE_Z / street_z
    return z - (street_z - KITCHEN_FACADE_Z)


own_poly = Polygon([(x, squeeze(z)) for x, z in own_plan]).buffer(0)
# Replace the mapped stair bay with the modelled stair tower (x -1.45..1.665, out to z 11.12).
tower = box(-1.45, 4.90, 1.665, 11.12)
# The modelled stair tower replaces the mapped stair bay; the facade stops at the kitchen wall plane.
# No surface of the building may coincide with a stair-tower wall (that caused visible z-fighting).
own_poly = own_poly.intersection(box(-100, -100, 100, KITCHEN_FACADE_Z))
# Apartment shell (rooms plus walls) from the model outline, and the balcony slab outside it.
outline = [(0, 0), (2.883, 0), (2.883, 4.899), (5.653, 4.899), (5.653, 10), (1.665, 10), (1.665, 5.079), (1.845, 5.079), (1.845, 4.719), (0, 4.719)]
apartment = Polygon(outline).buffer(.33, join_style=2)
band = own_poly.difference(tower).difference(apartment)
lower = own_poly.difference(tower)


def rings(geom, digits=2):
    out = []
    for poly in getattr(geom, 'geoms', [geom]):
        if poly.is_empty or poly.area < .05:
            continue
        poly = poly.simplify(.01)
        out.append({'o': [round(v, digits) for c in poly.exterior.coords[:-1] for v in c],
                    'h': [[round(v, digits) for c in r.coords[:-1] for v in c] for r in poly.interiors]})
    return out


def hip_rect(poly):
    rect = poly.minimum_rotated_rectangle
    if rect.area <= 0 or poly.area / rect.area < .86:
        return None
    return [round(v, 2) for c in list(rect.exterior.coords)[:-1] for v in c]


def levels_of(t):
    if 'height' in t:
        try:
            return None, float(str(t['height']).split()[0])
        except ValueError:
            pass
    kind = t.get('building', 'yes')
    lv = t.get('building:levels')
    try:
        lv = float(lv) if lv else None
    except ValueError:
        lv = None
    if lv is None:
        lv = {'apartments': 4, 'house': 2, 'terrace': 2, 'kindergarten': 1, 'school': 3, 'commercial': 3, 'office': 4, 'civic': 2}.get(kind, 1)
    if kind in ('apartments', 'terrace', 'office', 'commercial', 'school') and lv >= 3:
        return lv, PLINTH + lv * STOREY + .5
    return lv, lv * 3.0 + .3


def local(p):
    e, n = en(p)
    return (e, -n)


buildings, areas, roads, footprints, footprints_kept = [], [], [], [], []
AREA_KIND = {'grass': 'grass', 'park': 'grass', 'garden': 'grass', 'meadow': 'grass', 'village_green': 'grass', 'recreation_ground': 'grass',
             'playground': 'playground', 'pitch': 'pitch', 'parking': 'parking', 'wood': 'wood', 'scrub': 'grass', 'flowerbed': 'flowerbed',
             'sports_centre': 'pitch', 'school': 'yard', 'kindergarten': 'yard', 'residential': None, 'commercial': None}
ROAD_WIDTH = {'primary': 11, 'trunk': 12, 'trunk_link': 7, 'secondary': 9, 'tertiary': 8, 'residential': 6.5, 'unclassified': 6, 'living_street': 5,
              'service': 3.6, 'pedestrian': 5, 'footway': 2.4, 'path': 2, 'cycleway': 2.5, 'steps': 2, 'track': 3}
for w in ways:
    t = w.get('tags', {})
    pts = [local(p) for p in w['geometry']]
    if 'building' in t and w['id'] != OWN_ID and t.get('building') != 'roof':
        if len(pts) < 4:
            continue
        poly = Polygon(pts).buffer(0)
        if poly.is_empty or poly.area < 4:
            continue
        lv, h = levels_of(t)
        kind = t.get('building', 'yes')
        style = 'brick' if kind in ('apartments', 'terrace') else 'plaster' if kind in ('kindergarten', 'school', 'civic', 'commercial', 'office', 'house') else 'shed'
        footprints.append(poly)
        for part in getattr(poly, 'geoms', [poly]):
            r = rings(part)
            if r:
                footprints_kept.append(part)
                buildings.append({'r': r[0], 'h': round(h, 2), 's': style, 'k': hip_rect(part) if style == 'brick' else None,
                                  'c': t.get('building:colour')})
        continue
    kind = None
    for key in ('leisure', 'landuse', 'natural', 'amenity'):
        if key in t:
            kind = AREA_KIND.get(t[key], 'water' if t[key] == 'water' else None)
            if kind:
                break
    if kind and len(pts) >= 4 and pts[0] == pts[-1]:
        poly = Polygon(pts).buffer(0)
        for r in rings(poly, 1):
            areas.append({'k': kind, **r})
        continue
    hw = t.get('highway')
    if hw in ROAD_WIDTH:
        roads.append({'k': hw, 'w': ROAD_WIDTH[hw], 'n': t.get('name'), 'p': [round(v, 1) for c in pts for v in c]})
    elif t.get('railway') in ('tram',):
        roads.append({'k': 'tram', 'w': 2.6, 'p': [round(v, 1) for c in pts for v in c]})

blocked = unary_union(footprints)
trees, lamps = [], []
own_local = Polygon([local(p) for p in own['geometry']])
for e in osm['elements']:
    if e['type'] != 'node':
        continue
    t = e.get('tags', {})
    x, s = local(e)
    if t.get('natural') == 'tree':
        if blocked.contains(Point(x, s)) or own_local.buffer(1).contains(Point(x, s)):
            continue
        try:
            height = float(str(t.get('height', '0')).split()[0])
        except (ValueError, IndexError):
            height = 0
        trees.extend([round(x, 1), round(s, 1), round(height, 1)])
    elif t.get('highway') == 'street_lamp':
        lamps.extend([round(x, 1), round(s, 1)])

# Courtyard in front of the balcony (FINN photo 9: central path, lawns, hedged playground, trees).
# OSM maps little inside it, so a simple dressing is laid out along the courtyard's long axis.
def to_local_from_plan(x, z):
    a, b = x - APARTMENT_RIGHT_X, z
    e = corner[0] + a * ue + b * ve
    n = corner[1] + a * un + b * vn
    return (e, -n)


front = Point(*to_local_from_plan(2.0, -15.0))
near_fp = unary_union([f for f in footprints if f.distance(front) < 140] + [own_local])
open_space = near_fp.convex_hull.buffer(-2).difference(near_fp.buffer(1.5))
courtyard = next((g for g in getattr(open_space, 'geoms', [open_space]) if g.buffer(1).contains(front)), None)
dressing = None
if courtyard is not None:
    rect = courtyard.minimum_rotated_rectangle
    cs = list(rect.exterior.coords)[:4]
    e1 = (cs[1][0] - cs[0][0], cs[1][1] - cs[0][1]); e2 = (cs[2][0] - cs[1][0], cs[2][1] - cs[1][1])
    if math.hypot(*e1) < math.hypot(*e2):
        cs = cs[1:] + cs[:1]
        e1, e2 = e2, (cs[2][0] - cs[1][0], cs[2][1] - cs[1][1])
    mid_a = ((cs[0][0] + cs[3][0]) / 2, (cs[0][1] + cs[3][1]) / 2)
    mid_b = ((cs[1][0] + cs[2][0]) / 2, (cs[1][1] + cs[2][1]) / 2)
    axis = LineString([mid_a, mid_b])
    path = axis.intersection(courtyard)
    length = axis.length
    ux_, uz_ = (mid_b[0] - mid_a[0]) / length, (mid_b[1] - mid_a[1]) / length
    nx_, nz_ = -uz_, ux_
    def at(t, side):
        return (mid_a[0] + ux_ * t + nx_ * side, mid_a[1] + uz_ * t + nz_ * side)
    # Playground beside the path near the middle, fenced by a low hedge.
    t0 = length * .45
    play = Polygon([at(t0, 3.5), at(t0 + 11, 3.5), at(t0 + 11, 11), at(t0, 11)])
    play = play.intersection(courtyard.buffer(-1))
    trees_c, hedges, benches = [], [], []
    k = 0
    for t in range(8, int(length) - 6, 13):
        for side in (-7.5, 7.5):
            p = Point(*at(t + (k % 3) * 1.7, side + (k % 2) * 1.2))
            k += 1
            if courtyard.buffer(-2).contains(p) and not play.buffer(2).contains(p):
                trees_c.extend([round(p.x, 1), round(p.y, 1), round(10 + (k * 37 % 50) / 10, 1)])
    for t in range(6, int(length) - 4, 9):
        if t0 - 2 < t < t0 + 12:
            continue
        for side in (-2.2, 2.2):
            a, b = Point(*at(t, side)), Point(*at(t + 5, side))
            if courtyard.buffer(-1).contains(a) and courtyard.buffer(-1).contains(b):
                hedges.append([round(v, 2) for v in (a.x, a.y, b.x, b.y)])
    for t in (t0 - 4, t0 + 14):
        p = Point(*at(t, -2.6))
        if courtyard.contains(p):
            benches.append([round(p.x, 2), round(p.y, 2), round(math.atan2(uz_, ux_), 3)])
    dressing = {'path': [round(v, 2) for c in (path.coords if path.geom_type == 'LineString' else axis.coords) for v in c], 'pathWidth': 3.0,
                'playground': rings(play)[0] if not play.is_empty else None,
                'axis': [round(ux_, 4), round(uz_, 4)], 'trees': trees_c, 'hedges': hedges, 'benches': benches}

# ---------------- Open-world additions ----------------
import base64
import random
from PIL import Image
import numpy as np

rng = random.Random(17)

# Terrain: Kartverket DTM (nhm_dtm_topo_25833, 2 m), 1.6 km square centred on the address, row 0 = north.
DTM_X0, DTM_Y1, DTM_RES = 264086 - 800, 6651859 + 800, 2.0
dtm = np.array(Image.open(root / 'assets/osm/kartverket-dtm-2m.tif'), dtype=float)


def to_utm33(lat, lon):
    a = 6378137.0; f = 1 / 298.257222101; k0 = 0.9996; lon0 = math.radians(15)
    e2 = f * (2 - f); ep2 = e2 / (1 - e2); phi = math.radians(lat); lam = math.radians(lon)
    N = a / math.sqrt(1 - e2 * math.sin(phi) ** 2); T = math.tan(phi) ** 2; C = ep2 * math.cos(phi) ** 2; A = math.cos(phi) * (lam - lon0)
    e4 = e2 * e2; e6 = e4 * e2
    M = a * ((1 - e2 / 4 - 3 * e4 / 64 - 5 * e6 / 256) * phi - (3 * e2 / 8 + 3 * e4 / 32 + 45 * e6 / 1024) * math.sin(2 * phi) + (15 * e4 / 256 + 45 * e6 / 1024) * math.sin(4 * phi) - (35 * e6 / 3072) * math.sin(6 * phi))
    x = k0 * N * (A + (1 - T + C) * A ** 3 / 6 + (5 - 18 * T + T * T + 72 * C - 58 * ep2) * A ** 5 / 120) + 500000
    y = k0 * (M + N * math.tan(phi) * (A * A / 2 + (5 - T + 9 * C + 4 * C * C) * A ** 4 / 24 + (61 - 58 * T + T * T + 600 * C - 330 * ep2) * A ** 6 / 720))
    return x, y


def height_local(x, s_):
    """Terrain height (m above sea level) at local east x, south s."""
    lat, lon = LAT0 + (-s_) / 111320, LON0 + x / KX
    ux, uy = to_utm33(lat, lon)
    c, r = (ux - DTM_X0) / DTM_RES - .5, (DTM_Y1 - uy) / DTM_RES - .5
    c = min(max(c, 0), dtm.shape[1] - 1.001); r = min(max(r, 0), dtm.shape[0] - 1.001)
    c0, r0 = int(c), int(r); fc, fr = c - c0, r - r0
    return (dtm[r0, c0] * (1 - fc) * (1 - fr) + dtm[r0, c0 + 1] * fc * (1 - fr) + dtm[r0 + 1, c0] * (1 - fc) * fr + dtm[r0 + 1, c0 + 1] * fc * fr)


door_local = to_local_from_plan(.75, 11.2)
H_DOOR = height_local(*door_local)
rel = lambda x, s_: height_local(x, s_) - H_DOOR
TER_HALF, TER_STEP = 600, 4
n = int(2 * TER_HALF / TER_STEP) + 1
grid = np.zeros((n, n), dtype=np.int16)
for j in range(n):
    for i in range(n):
        grid[j, i] = int(round(rel(-TER_HALF + i * TER_STEP, -TER_HALF + j * TER_STEP) * 10))
terrain = {'half': TER_HALF, 'step': TER_STEP, 'n': n, 'doorHeight': round(H_DOOR, 2), 'dm': base64.b64encode(grid.tobytes()).decode('ascii'),
           'attribution': 'Height data © Kartverket (DTM, CC BY 4.0)'}

for b, fp in zip(buildings, footprints_kept):
    ring = b['r']['o']; pts_ = [(ring[i], ring[i + 1]) for i in range(0, len(ring), 2)]
    cx = sum(p_[0] for p_ in pts_) / len(pts_); cs_ = sum(p_[1] for p_ in pts_) / len(pts_)
    b['g'] = round(rel(cx, cs_), 2)
    b['gm'] = round(min(rel(*p_) for p_ in pts_), 2)

road_lines = [(r_['k'], r_.get('n'), LineString([(r_['p'][i], r_['p'][i + 1]) for i in range(0, len(r_['p']), 2)])) for r_ in roads if len(r_['p']) >= 4]
street_lines = [ln for k, nm, ln in road_lines if k in ('residential', 'tertiary', 'service', 'living_street', 'unclassified', 'secondary', 'primary', 'pedestrian', 'footway')]
car_roads = [ln for k, nm, ln in road_lines if k in ('residential', 'tertiary', 'service', 'primary', 'secondary', 'living_street', 'unclassified')]

# Enterable buildings: a stair tower (same layout as ours) on the facade facing the nearest street or path.
# Tower frame: x along the facade, z outward, facade plane at z = 10.32, tower x -1.45..1.665, z 4.9..11.12.
enterable = []
origin_pt = Point(0, 0)
cands = sorted([(footprints_kept[k], k) for k, b in enumerate(buildings) if b['s'] == 'brick'], key=lambda t: t[0].distance(origin_pt))
for poly, k in cands:
    if len(enterable) >= 8 or poly.distance(origin_pt) > 230:
        break
    ring = list(poly.exterior.coords)[:-1]
    best = None
    for i in range(len(ring)):
        a_, b_ = ring[i], ring[(i + 1) % len(ring)]
        L_ = math.hypot(b_[0] - a_[0], b_[1] - a_[1])
        if L_ < 9:
            continue
        mid = ((a_[0] + b_[0]) / 2, (a_[1] + b_[1]) / 2)
        tx, ts = (b_[0] - a_[0]) / L_, (b_[1] - a_[1]) / L_
        nrm = (ts, -tx)
        if poly.contains(Point(mid[0] + nrm[0] * .5, mid[1] + nrm[1] * .5)):
            nrm = (-nrm[0], -nrm[1])
        outside = Point(mid[0] + nrm[0] * 3, mid[1] + nrm[1] * 3)
        if any(f.contains(outside) for f in footprints) or own_local.buffer(1).contains(outside):
            continue
        if not poly.contains(Point(mid[0] - nrm[0] * 6, mid[1] - nrm[1] * 6)):
            continue
        dist = min((ln.distance(outside) for ln in street_lines), default=99)
        score = dist - L_ * .05
        if best is None or score < best[0]:
            best = (score, mid, nrm)
    if not best:
        continue
    _, mid, nrm = best
    ex, es = nrm[1], -nrm[0]       # tower +x direction (proper rotation with +z = outward normal)
    ox, os_ = mid[0] - nrm[0] * 10.32 - ex * .1075, mid[1] - nrm[1] * 10.32 - es * .1075
    to_w = lambda tx, tz, ox=ox, os_=os_, ex=ex, es=es, nrm=nrm: (ox + tx * ex + tz * nrm[0], os_ + tx * es + tz * nrm[1])
    door_pt = to_w(.75, 11.2)
    notched = poly.difference(Polygon([to_w(-1.5, 4.85), to_w(1.72, 4.85), to_w(1.72, 12), to_w(-1.5, 12)]))
    enterable.append({'b': k, 'x': round(ox, 3), 's': round(os_, 3), 'rot': round(math.atan2(nrm[0], nrm[1]), 5),
                      'y': round(rel(*door_pt), 2), 'collide': rings(notched)})
    buildings[k]['enter'] = len(enterable) - 1

# Park (Torshovdalen side): the largest grass/park area within 350 m.
# Torshovdalen park: OSM relation 959781 (assets/osm/torshovdalen.json), minus buildings and streets.
tdal = json.loads((root / 'assets/osm/torshovdalen.json').read_text(encoding='utf-8'))
rel_park = next(e for e in tdal['elements'] if e['type'] == 'relation')
outer = [Polygon([local(p_) for p_ in m_['geometry']]) for m_ in rel_park['members'] if m_['role'] == 'outer']
inner = [Polygon([local(p_) for p_ in m_['geometry']]) for m_ in rel_park['members'] if m_['role'] == 'inner']
park = unary_union(outer).difference(unary_union(inner)) if inner else unary_union(outer)
park = park.difference(unary_union([f.buffer(6) for f in footprints] + [own_local.buffer(6)]))
park = park.difference(unary_union([ln.buffer(5) for ln in car_roads]))
park = max(getattr(park, 'geoms', [park]), key=lambda g: g.area)
tree_pts = [Point(trees[i], trees[i + 1]) for i in range(0, len(trees), 3)]


def clear_line(a_, b_, margin):
    ln = LineString([a_, b_])
    return all(t_.distance(ln) > margin for t_ in tree_pts) and park.buffer(-2).contains(ln)


holes = []
best = None
minx, mins, maxx, maxs = park.bounds
for _ in range(6000):
    t1 = (rng.uniform(minx, maxx), rng.uniform(mins, maxs))
    a1 = rng.uniform(0, 2 * math.pi)
    b1 = (t1[0] + 70 * math.cos(a1), t1[1] + 70 * math.sin(a1))
    if not clear_line(t1, b1, 3.5):
        continue
    t2 = (b1[0] + 8 * math.cos(a1 + 1.2), b1[1] + 8 * math.sin(a1 + 1.2))
    for a2 in (a1 + 1.9, a1 - 1.9, a1 + 2.6, a1 - 2.6, a1 + 1.3, a1 - 1.3):
        b2 = (t2[0] + 70 * math.cos(a2), t2[1] + 70 * math.sin(a2))
        if clear_line(t2, b2, 3.5):
            slope = abs(rel(*t1) - rel(*b1)) + abs(rel(*t2) - rel(*b2))
            score = Point(t1).distance(origin_pt) * .2 + slope
            if best is None or score < best[0]:
                best = (score, t1, b1, t2, b2)
            break
if best:
    _, t1, b1, t2, b2 = best
    holes = [{'tee': [round(t1[0], 2), round(t1[1], 2)], 'basket': [round(b1[0], 2), round(b1[1], 2)], 'par': 3},
             {'tee': [round(t2[0], 2), round(t2[1], 2)], 'basket': [round(b2[0], 2), round(b2[1], 2)], 'par': 3}]

court = None
for _ in range(4000):
    c_ = (rng.uniform(minx, maxx), rng.uniform(mins, maxs)); ang = rng.uniform(0, math.pi)
    d_ = (math.cos(ang), math.sin(ang)); q = (-d_[1], d_[0])
    corners = [(c_[0] + d_[0] * sx * 7.5 + q[0] * sy * 2, c_[1] + d_[1] * sx * 7.5 + q[1] * sy * 2) for sx, sy in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
    cp = Polygon(corners)
    if not park.buffer(-2).contains(cp) or any(t_.distance(cp) < 2.5 for t_ in tree_pts):
        continue
    if holes and any(LineString([h['tee'], h['basket']]).distance(cp) < 12 for h in holes):
        continue
    hs = [rel(*p_) for p_ in corners]
    score = (max(hs) - min(hs)) * 4 + Point(c_).distance(origin_pt) * .05
    if court is None or score < court[0]:
        court = (score, c_, ang)
petanque = {'x': round(court[1][0], 2), 's': round(court[1][1], 2), 'rot': round(court[2], 4), 'length': 15, 'width': 4} if court else None

# Parked cars along the nearest street, motorcycle beside the stair door, excavator on the street side.
street = min(car_roads, key=lambda ln: ln.distance(Point(*door_local)))
cars = []
proj = street.project(Point(*door_local))
for off in (-27, -20, -13, 13, 20, 34):
    d_ = proj + off
    if d_ < 3 or d_ > street.length - 3:
        continue
    p1, p2, pc = street.interpolate(d_ - 1), street.interpolate(d_ + 1), street.interpolate(d_)
    tx, ts = p2.x - p1.x, p2.y - p1.y; L_ = math.hypot(tx, ts); tx, ts = tx / L_, ts / L_
    for side in (1, -1):
        cx, cs_ = pc.x - ts * 2.6 * side, pc.y + tx * 2.6 * side
        cp = Point(cx, cs_)
        if any(f.buffer(1.2).contains(cp) for f in footprints) or own_local.buffer(1.5).contains(cp) or Point(*door_local).distance(cp) < 6:
            continue
        cars.append({'x': round(cx, 2), 's': round(cs_, 2), 'heading': round(math.atan2(tx, ts), 4), 'model': len(cars) % 3})
        break
door_out = to_local_from_plan(.75, 13.5)
bike = to_local_from_plan(3.6, 12.6)
digger = to_local_from_plan(-10.0, 20.0)
vehicles = {'cars': cars, 'motorcycle': {'x': round(bike[0], 2), 's': round(bike[1], 2)},
            'excavator': {'x': round(digger[0], 2), 's': round(digger[1], 2)},
            'missionStart': [round(door_out[0], 2), round(door_out[1], 2)]}

bushes = []
for poly in footprints:
    if poly.distance(origin_pt) > 260:
        continue
    ring = list(poly.exterior.coords)
    for i in range(len(ring) - 1):
        a_, b_ = ring[i], ring[i + 1]; L_ = math.hypot(b_[0] - a_[0], b_[1] - a_[1])
        if L_ < 6:
            continue
        tx, ts = (b_[0] - a_[0]) / L_, (b_[1] - a_[1]) / L_
        for k in range(int(L_ // 5)):
            if rng.random() < .55:
                continue
            t_ = 2.5 + k * 5 + rng.uniform(-1, 1)
            if t_ > L_ - 1:
                break
            for side in (1, -1):
                p_ = Point(a_[0] + tx * t_ + ts * side * 1.4, a_[1] + ts * t_ - tx * side * 1.4)
                if poly.contains(p_):
                    continue
                if any(f.contains(p_) for f in footprints) or own_local.buffer(1).contains(p_):
                    break
                if min((ln.distance(p_) for ln in car_roads), default=99) < 5:
                    break
                bushes.extend([round(p_.x, 1), round(p_.y, 1), round(rng.uniform(.7, 1.5), 2)])
                break
for _ in range(220):
    p_ = Point(rng.uniform(minx, maxx), rng.uniform(mins, maxs))
    if park.buffer(-1).contains(p_) and park.exterior.distance(p_) < 12 and all(LineString([h['tee'], h['basket']]).distance(p_) > 6 for h in holes):
        bushes.extend([round(p_.x, 1), round(p_.y, 1), round(rng.uniform(.8, 1.8), 2)])

data = {
    'attribution': 'Map data © OpenStreetMap contributors (ODbL). Height data © Kartverket (CC BY 4.0). Aerial imagery, when online: Esri World Imagery (Esri, Maxar, Earthstar Geographics).',
    'origin': {'lat': LAT0, 'lon': LON0},
    'geo': {'theta': round(theta, 6), 'x': round(gx, 3), 'z': round(gz, 3)},
    'ground': GROUND, 'storey': STOREY, 'plinth': PLINTH,
    'depthDifference': round(street_z - KITCHEN_FACADE_Z, 2),
    'own': {'lower': rings(lower), 'band': rings(band), 'upper': rings(own_poly.difference(tower)), 'roof': [r for r in (hip_rect(own_poly.intersection(box(-100, -100, APARTMENT_RIGHT_X, 100))), hip_rect(own_poly.intersection(box(APARTMENT_RIGHT_X, -100, 100, 100)))) if r], 'bandTop': 2.62,
            'top': round(PLINTH + 4 * STOREY + .5 + GROUND, 3)},
    'ownLocal': [round(v, 2) for p in own['geometry'][:-1] for v in local(p)],
    # Aerial imagery leans: roofs appear displaced by this many metres (east, south) per metre of height.
    'imageryLean': [-.6, .075],
    'courtyard': dressing,
    'terrain': terrain, 'enterable': enterable, 'discgolf': holes, 'petanque': petanque, 'vehicles': vehicles, 'bushes': bushes,
    'buildings': buildings, 'areas': areas, 'roads': roads, 'trees': trees, 'lamps': lamps,
}
out = '/* Generated by generate-surroundings.py. ' + data['attribution'] + ' */\nwindow.SURROUNDINGS=' + json.dumps(data, separators=(',', ':')) + ';\n'
(root / 'surroundings-data.js').write_text(out, encoding='utf-8')
print(f"enterable {len(enterable)}, disc golf holes {len(holes)}, petanque {bool(petanque)}, cars {len(cars)}, bushes {len(bushes)//3}, terrain {n}x{n}, door height {H_DOOR:.1f} m")
print(f"surroundings-data.js: {len(buildings)} buildings, {len(areas)} areas, {len(roads)} roads, {len(trees)//3} trees, "
      f"{len(out)//1024} KB; street facade z {street_z:.2f} (depth difference {street_z - KITCHEN_FACADE_Z:.2f} m), theta {math.degrees(theta):.1f}°")
