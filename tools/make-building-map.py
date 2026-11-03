"""Building map for the facade and roof materials (photo-real pass): which building looks like what.

    python tools/make-building-map.py

The web draws every apartment block in the same dark brick. The real ones differ (Street View and Esri imagery,
27 Oct and 2 Nov 2026): the blocks around Haarklous plass are red-brown brick with dark mortar, dark-brown wooden
windows and grey concrete plinths (OSM tags them #cb9c89, a pale salmon that looked pink in the game: the brick
is darker), some blocks are orange-red brick with white windows and red tile roofs, their neighbours pale yellow or
red plaster with white windows; roofs dark grey slate or red clay tile. OSM gives building:colour for about 90
buildings (all those around home) and roof:colour for some; 172 more were looked at in Street View on 3 Nov 2026
(tools/street-view-looks.json); the others get the mix seen there, the same for buildings of one block.

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

SALMON = (203, 156, 137)          # OSM #cb9c89, the tag of the real brick blocks around Haarklous plass
BRICK = (150, 98, 80)             # their real brick (the average of brick and dark mortar, Street View 2 Nov 2026)
ORANGE = (178, 98, 70)            # the orange-red brick blocks with white windows (Haarklous plass, north side)
# Buildings without an OSM colour: the look seen in Street View (tools/street-view-looks.json, 3 Nov 2026: 172
# buildings within 400 m of home), else a dice roll from the mix seen there. Of 47 blocks looked at, about 70 % were
# plaster (mostly pale yellow, sand and cream, some ochre and white functionalist blocks with dark frames), 17 % brick
# (red-brown, around Sinsen and Knut Alvssons vei) and 8 % new dark brick blocks.
STREET_VIEW = os.path.join(ROOT, 'tools', 'street-view-looks.json')
RANDOM_LOOKS = [                  # (weight, style, colour, white window frames)
    (0.12, 'brick', BRICK, True),
    (0.06, 'brick', ORANGE, True),
    (0.20, 'plaster', (229, 211, 165), True),   # pale yellow (OSM #e5d3aa)
    (0.16, 'plaster', (222, 202, 150), True),   # sand
    (0.13, 'plaster', (228, 214, 178), True),   # cream
    (0.11, 'plaster', (220, 192, 115), True),   # Torshov yellow-ochre
    (0.08, 'plaster', (236, 234, 228), False),  # white functionalist, dark frames
    (0.06, 'plaster', (226, 210, 168), False),  # beige with dark frames
    (0.08, 'plaster', (82, 82, 86), False),     # new dark brick blocks and grey panels
]   # (oxide red, pink and grey plaster: OSM-tagged ones keep theirs; none was seen untagged)
TILE = {BRICK: 0.3, ORANGE: 0.85,  # share of red clay tile roofs by look (plaster 0.55), where OSM has no roof colour
        (236, 234, 228): 0, (82, 82, 86): 0}   # functionalist and new blocks: flat roofs


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

    seen = [(e, p) for e in json.load(open(STREET_VIEW, encoding='utf-8'))['looks'] for p in e['at']]

    def street_view(cx, cz):     # the last entry wins (a single building after its block)
        hit = None
        for e, (x, z) in seen:
            if abs(x - cx) < 1 and abs(z - cz) < 1:
                hit = e
        return hit

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
            white = True
        elif colour:
            rgb = hexrgb(colour)
            style = 'brick' if rgb == SALMON else 'plaster'
            rgb, white = (BRICK, False) if rgb == SALMON else (rgb, True)
        elif street_view(cx, cz):
            # The colour seen, snapped to the nearest of the mix's (the palette holds 255 looks of 8 variants each).
            e = street_view(cx, cz)
            style, white = e['style'], e['white']
            rgb = min({c for _, st, c, _ in RANDOM_LOOKS if st == style}, key=lambda c: sum((a - b) ** 2 for a, b in zip(c, e['rgb'])))
        else:
            acc = 0
            for w, st, c, wh in RANDOM_LOOKS:
                acc += w
                if r < acc:
                    break
            style, rgb, white = st, c, wh
        rc = tags.get('roof:colour', '')
        if rc.startswith('#'):
            rr, rg, rb = hexrgb(rc)
            tile = rr > rg + 25
        else:
            tile = hash01(cell[0], cell[1], 9.3) < TILE.get(rgb, 0.55)
        lid = look_id(style, rgb, tile, white, variant)
        polys.append(([to_world(x, z) for x, z in ring], lid))
        per_building[i] = {'style': style, 'web': b['s'], 'tile': tile, 'look': lid, 'rgb': list(rgb), 'white': white, 'variant': variant * 32 / 255.0}
    # The shops (game-shops.js) are cut out of their buildings' outlines; the wall the web rebuilds above each shop
    # front (on the old outline) takes its look from here, so the shop's rectangle gets its building's look too.
    game_path = os.path.join(DATA, 'game.json')
    if os.path.exists(game_path):
        for sh in json.load(open(game_path, encoding='utf-8')).get('shops', {}).get('shops', []):
            if sh['building'] not in per_building:
                continue
            o, u, v = sh['origin'], sh['u'], sh['v']
            at = lambda a, b_: (o[0] + u[0] * a + v[0] * b_, o[1] + u[1] * a + v[1] * b_)
            polys.append(([at(0, -.3), at(sh['W'], -.3), at(sh['W'], sh['D']), at(0, sh['D'])], per_building[sh['building']]['look']))

    # Haarklous plass 17 itself (the web's own building, already in the world frame): red-brown brick, slate roof.
    own = s['own']
    lid = look_id('brick', BRICK, False, False, 3)
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
    for st, rgb, tile, wh, v in looks:
        styles[(st, tile)] = styles.get((st, tile), 0) + 1
    print(f'HK: building map {SIZE} px, {cm_px:.0f} cm/px, {len(polys)} footprints, {len(looks)} looks {styles}')


main()
