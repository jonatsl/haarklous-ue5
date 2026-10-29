"""The real Muselunden DiscGolfPark (12 holes) for the game: tee and basket places in the local map frame.

    python tools/make-muselunden.py

29 Oct 2026, the user's goal: "implement the real muselunden disc golf park at its real location with all the holes".
Sources:
- Layout and lengths: UDisc, "Muselunden DiscGolfPark", layout "Muselunden 12 hull (2022)" (the course's main layout):
  12 holes, all par 3, 1782 m walking; hole lengths below (UDisc 'holeDistance', metres). UDisc's hole map needs a login,
  so the places come from the club's map instead.
- Places: Muselunden Frisbeeklubb, "Midlertidig banekart 2021" (muselunden.no, Banekart 2021.JPG, 1171 x 936 px). It has
  the same 12 holes as UDisc's 2022 layout (holes 8, 11, 12 were marked "under arbeid"). The tee pads and baskets were
  read off the map in pixels.
- Georeferencing: an affine fit of the map's two football pitches (their red outlines, 8 corners) to OpenStreetMap ways
  172317127 (Muselunden kunstgressbane) and 172317126 (Muselunden minibane). RMS error 2.2 m (the map is drawn by hand).
- Course outline: OSM way 785654764 (leisure=disc_golf_course, "Muselunden DiscGolfPark").
Each hole keeps the map's direction and midpoint and is scaled to UDisc's length (the 2021 map is a sketch; the ends move
at most about 8 m); a hole whose tee or basket lands on a car road or in a building slides along its own line until clear. Writes muselunden-data.js (window.MUSELUNDEN).
"""
import json
import math
import os

import numpy as np
from shapely.geometry import LineString, Point, Polygon

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
CAR_ROADS = ('primary', 'secondary', 'tertiary', 'residential', 'service', 'unclassified', 'trunk', 'tram')
# Pitch corners: map pixels -> local metres (x east, s south) from the OSM ways.
PITCH_PX = [(459, 404), (628, 621), (495, 724), (326, 507), (780.3, 537.7), (840.3, 613.7), (714.7, 710.3), (655.7, 634)]
PITCH_LOCAL = [(431.4, -386.8), (491.6, -309.9), (441.3, -270.5), (381.1, -347.4), (545.2, -338.7), (569.3, -308.0), (522.1, -270.9), (497.9, -301.7)]
# hole: (tee pixel, basket pixel), UDisc 2022 length in metres, par
HOLES = {1: ((1080, 479), (791, 401), 106.3, 3), 2: ((766, 467.5), (890, 510), 53.0, 3), 3: ((685, 794), (480, 818.5), 68.0, 3),
         4: ((455, 712.5), (323.5, 541), 63.0, 3), 5: ((342.5, 261), (270.5, 248), 35.0, 3), 6: ((164, 136), (222, 283), 50.0, 3),
         7: ((249, 464), (374, 352.5), 65.0, 3), 8: ((538, 449), (711.5, 489), 64.0, 3), 9: ((545, 385.5), (721.5, 322.5), 76.0, 3),
         10: ((755.5, 253.5), (989, 314), 77.0, 3), 11: ((998.5, 167.5), (945, 61), 52.0, 3), 12: ((885.5, 107.5), (969, 256), 47.0, 3)}


def main():
    A = np.array([[u, v, 1] for u, v in PITCH_PX]); B = np.array(PITCH_LOCAL)
    M, *_ = np.linalg.lstsq(A, B, rcond=None)
    rms = float(np.sqrt(((A @ M - B) ** 2).sum(1).mean()))
    to_local = lambda p: np.array([p[0], p[1], 1.0]) @ M
    # A tee or basket the map's error puts on a car road or in a building slides along its own hole (same length and
    # direction), 0.5 m at a time, until it is clear (hole 6's tee: the service road beside it).
    t = open(os.path.join(ROOT, 'surroundings-data.js'), encoding='utf-8').read()
    S = json.loads(t[t.index('{'):t.rindex('}') + 1])
    pairs = lambda f: [(f[i], f[i + 1]) for i in range(0, len(f), 2)]
    roads = [(LineString(pairs(r['p'])), r['w'] / 2 + 1.5) for r in S['roads'] if len(r['p']) >= 4 and r['k'] in CAR_ROADS]
    blds = [Polygon(pairs(b['r']['o'])).buffer(1.0) for b in S['buildings']]
    clear = lambda p: not any(l.distance(Point(*p)) < w for l, w in roads) and not any(b.contains(Point(*p)) for b in blds)
    holes = []
    for n, (tp, bp, length, par) in HOLES.items():
        t, b = to_local(tp), to_local(bp)
        mid, d = (t + b) / 2, b - t
        u = d / np.linalg.norm(d)
        slide = next(k for k in (0, .5, -.5, 1, -1, 1.5, -1.5, 2, -2, 2.5, -2.5, 3, -3, 3.5, -3.5, 4, -4, 5, -5, 6, -6)
                     if clear(mid + u * (k - length / 2)) and clear(mid + u * (k + length / 2)))
        t2, b2 = mid + u * (slide - length / 2), mid + u * (slide + length / 2)
        holes.append({'n': n, 'par': par, 'tee': [round(float(t2[0]), 1), round(float(t2[1]), 1)], 'basket': [round(float(b2[0]), 1), round(float(b2[1]), 1)],
                      'length': length, 'mapLength': round(float(np.linalg.norm(d)), 1), 'moved': round(float(abs(np.linalg.norm(d) - length) / 2 + abs(slide)), 1)})
    osm = json.load(open(os.path.join(ROOT, 'assets/osm/torshov-osm.json'), encoding='utf-8'))
    lat0, lon0 = 59.936404, 10.776657
    kx = 111320 * math.cos(math.radians(lat0))
    way = next(e for e in osm['elements'] if e['type'] == 'way' and e['id'] == 785654764)
    outline = [round(v, 1) for p in way['geometry'] for v in ((p['lon'] - lon0) * kx, -(p['lat'] - lat0) * 111320)]
    out = {'name': 'Muselunden DiscGolfPark', 'layout': 'Muselunden 12 hull (2022)', 'par': sum(h['par'] for h in holes), 'outline': outline,
           'sources': 'UDisc layout "Muselunden 12 hull (2022)" (lengths, par); Muselunden Frisbeeklubb "Midlertidig banekart 2021" (places), '
                      'georeferenced to the OSM pitches 172317127 / 172317126; OSM way 785654764 (course).', 'holes': holes}
    with open(os.path.join(ROOT, 'muselunden-data.js'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('/* The real Muselunden DiscGolfPark, 12 holes (tools/make-muselunden.py). Local map metres: x east, s south. */\n'
                'window.MUSELUNDEN=' + json.dumps(out, separators=(',', ':')) + ';\n')
    print(f'Muselunden: 12 holes, par {out["par"]}, {sum(h["length"] for h in holes):.0f} m of holes; georeference RMS {rms:.2f} m; '
          f'largest end move {max(h["moved"] for h in holes)} m; farthest point {max(math.hypot(*h[k]) for h in holes for k in ("tee", "basket")):.0f} m from home')


main()
