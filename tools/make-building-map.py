"""Building map for the facade and roof materials (photo-real pass): which building looks like what.

    python tools/make-building-map.py

The web draws every apartment block in the same dark brick. The real ones differ (Street View and Esri imagery,
27 Oct 2026): the blocks around Haarklous plass are light salmon brick with dark-brown wooden windows and grey
concrete plinths, their neighbours yellow or red plaster with white windows; roofs are dark grey slate, some red
clay tile. OSM gives building:colour for about 90 buildings (all those around home) and roof:colour for some; the
others get a believable Torshov mix, the same for buildings of one block.

Writes tools/out/textures/
  T_BuildingId.png       top-down map of the district in the Unreal world frame, one palette index per building
                         (0 = no building), nearest-filtered by the materials
  T_BuildingPalette.png  256 x 2: row 0 = facade colour (sRGB), row 1 = R style (0 brick, 255 plaster),
                         G roof (0 slate, 255 red tile), B window frames (0 dark brown, 255 white), A variant
  building-map.json      origin and size of the map in Unreal centimetres (material parameters)
  building-looks.json    each building's look, for the balconies, downpipes and chimneys
"""
import json
import math
import os

import numpy as np
from PIL import Image, ImageDraw

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
DATA = os.path.join(ROOT, 'unreal', 'Haarklous', 'Content', 'Data')
OUT = os.path.join(ROOT, 'tools', 'out', 'textures')
OSM = os.path.join(ROOT, 'reference', 'web-game', 'assets', 'osm', 'torshov-osm.json')
SIZE = 2048

SALMON = (203, 156, 137)          # OSM #cb9c89, the real blocks around Haarklous plass (brick)
RANDOM_LOOKS = [                  # (weight, style, colour) for buildings without an OSM colour
    (0.45, 'brick', SALMON),
    (0.12, 'plaster', (229, 211, 170)),   # pale yellow (OSM #e5d3aa)
    (0.10, 'plaster', (214, 180, 112)),   # ochre
    (0.10, 'plaster', (196, 116, 92)),    # oxide red
    (0.08, 'plaster', (226, 174, 165)),   # pink (OSM #e2aea5)
    (0.08, 'plaster', (221, 207, 172)),   # beige (OSM #ddcfac)
    (0.07, 'plaster', (174, 169, 149)),   # grey (OSM #aea995)
]


def hexrgb(h):
    h = h.lstrip('#')
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def hash01(*v):
    x = math.sin(sum(a * b for a, b in zip(v, (12.9898, 78.233, 37.719)))) * 43758.5453
    return x - math.floor(x)


def main():
    s = json.load(open(os.path.join(DATA, 'surroundings.json'), encoding='utf-8'))
    th, gx, gz = s['geo']['theta'], s['geo']['x'], s['geo']['z']
    cT, sT = math.cos(th), math.sin(th)

    def to_world(x, z):          # surroundings.js toWorld(x, s)
        return x * cT + z * sT + gx, -x * sT + z * cT + gz

    # OSM roof colours, matched to the web's buildings by centroid (map frame: x east, s = -north).
    o = s['origin']
    kx = 111320 * math.cos(o['lat'] * math.pi / 180)
    osm = []
    for e in json.load(open(OSM, encoding='utf-8'))['elements']:
        t = e.get('tags', {})
        if e['type'] == 'way' and 'building' in t and e.get('geometry'):
            g = e['geometry']
            osm.append((np.mean([(p['lon'] - o['lon']) * kx for p in g]), -np.mean([(p['lat'] - o['lat']) * 111320 for p in g]), t))
    osm_xy = np.array([(a, b) for a, b, _ in osm])

    looks, index = [], {}

    def look_id(style, colour, roof, white, variant):
        key = (style, colour, roof, white, variant)
        if key not in index:
            index[key] = len(looks) + 1
            looks.append(key)
        return index[key]

    polys, per_building = [], {}
    for i, b in enumerate(s['buildings']):
        ring = np.array(b['r']['o'], dtype=float).reshape(-1, 2)
        cx, cz = ring.mean(axis=0)
        if b['s'] == 'shed':
            continue
        tags = {}
        d = np.hypot(osm_xy[:, 0] - cx, osm_xy[:, 1] - cz)
        if d.min() < 4:
            tags = osm[int(d.argmin())][2]
        # One look per block: buildings in the same 70 m cell share the dice roll.
        cell = (math.floor(cx / 70), math.floor(cz / 70))
        r = hash01(cell[0], cell[1], 3.1)
        variant = int(hash01(cx, cz, 1.7) * 8)
        colour = b.get('c') or tags.get('building:colour')
        if b['s'] == 'plaster':
            # The web's own plaster buildings (no windows in the web): keep their colour.
            pc = [0xe3dccb, 0xd8cfbd, 0xcdd2cb, 0xe6e0d4, 0xd9c7a8][i % 5]
            style, rgb = 'plaster', hexrgb(colour) if colour else ((pc >> 16) & 255, (pc >> 8) & 255, pc & 255)
        elif colour:
            rgb = hexrgb(colour)
            style = 'brick' if rgb == SALMON else 'plaster'
        else:
            acc = 0
            for w, st, c in RANDOM_LOOKS:
                acc += w
                if r < acc:
                    break
            style, rgb = st, c
        rc = tags.get('roof:colour', '')
        if rc.startswith('#'):
            rr, rg, rb = hexrgb(rc)
            tile = rr > rg + 25
        else:
            tile = hash01(cell[0], cell[1], 9.3) < (0.35 if style == 'plaster' else 0.12)
        lid = look_id(style, rgb, tile, style == 'plaster', variant)
        polys.append(([to_world(x, z) for x, z in ring], lid))
        per_building[i] = {'style': style, 'web': b['s'], 'tile': tile, 'look': lid}

    # Haarklous plass 17 itself (the web's own building, already in the world frame): salmon brick, slate roof.
    own = s['own']
    lid = look_id('brick', SALMON, False, False, 3)
    for part in own['lower'] + own['band'] + own['upper']:
        ring = np.array(part['o'], dtype=float).reshape(-1, 2)
        polys.append(([tuple(p) for p in ring], lid))
    assert len(looks) < 256, len(looks)

    pts = np.array([p for poly, _ in polys for p in poly]) * 100.0      # Unreal cm: X = web x, Y = web z
    lo, hi = pts.min(axis=0) - 2000, pts.max(axis=0) + 2000
    span = float(max(hi - lo))
    cm_px = span / SIZE
    img = Image.new('L', (SIZE, SIZE), 0)
    dr = ImageDraw.Draw(img)
    for poly, lid in polys:
        xy = [((x * 100 - lo[0]) / cm_px, (z * 100 - lo[1]) / cm_px) for x, z in poly]
        dr.polygon(xy, fill=lid, outline=lid)
    os.makedirs(OUT, exist_ok=True)
    img.save(os.path.join(OUT, 'T_BuildingId.png'))

    pal = np.zeros((2, 256, 4), dtype=np.uint8)
    for k, (style, rgb, tile, white, variant) in enumerate(looks, start=1):
        pal[0, k] = (*rgb, 255)
        pal[1, k] = (255 if style == 'plaster' else 0, 255 if tile else 0, 255 if white else 0, variant * 32)
    Image.fromarray(pal, 'RGBA').save(os.path.join(OUT, 'T_BuildingPalette.png'))
    json.dump({'origin': [float(lo[0]), float(lo[1])], 'size': span, 'cmPerPixel': cm_px, 'looks': len(looks)},
              open(os.path.join(OUT, 'building-map.json'), 'w'), indent=1)
    # Each building's look (index in surroundings.json buildings) for tools/blender/build_details.py.
    json.dump(per_building, open(os.path.join(OUT, 'building-looks.json'), 'w'))
    styles = {}
    for st, rgb, tile, white, v in looks:
        styles[(st, tile)] = styles.get((st, tile), 0) + 1
    print(f'HK: building map {SIZE} px, {cm_px:.0f} cm/px, {len(polys)} footprints, {len(looks)} looks {styles}')


main()
