"""A second real car body for the car rig: a VW ID.3-style electric hatchback (2 Nov 2026), built by car_model.py.

The ID.3 (4.26 x 1.81 x 1.55 m) is one of the commonest cars in Oslo and has nearly the Golf's footprint, so it fits
the web car (4.2 x 1.8 m, wheels at z +-1.3, x +-0.78) too: build_assets.py makes it as SM_car__body_B, and the cars
whose web colour is silver, white or blue use it (AHKRigActor::MeshVariant, HKGameModules.cpp). Only the look
differs from the Golf: a short bonnet with the windscreen starting over the front wheels, a high belt rising to the
back, a big rear quarter window, slim headlights joined by a black band, a large black lower intake, slim tail lights
joined by a black glass band. Same web car frame as car_model.py (x right, y up, front at -z).

    with car_model.variant(car_id3.OVERRIDES):
        P, N, U, K = car_model.car_shell(level)
"""
import car_model as cm
from car_model import (CHROME, DRL, GAP, GLOSS, GRILLE, LAMP, TAIL, TAIL_DARK, TRIM, circle, concat, curve, decal,
                       mirror_x, plane_3d, side_plane, strip)
import numpy as np

TOP = curve([(-2.10, 0.52), (-2.095, 0.60), (-2.085, 0.68), (-2.07, 0.76), (-2.05, 0.825), (-2.02, 0.862),
             (-1.97, 0.88), (-1.85, 0.898), (-1.65, 0.926), (-1.45, 0.955), (-1.34, 0.972), (-1.30, 0.988),
             (-1.28, 1.00), (-1.0, 1.15), (-0.7, 1.30), (-0.45, 1.415), (-0.30, 1.47), (-0.18, 1.497), (0.1, 1.52),
             (0.5, 1.524), (0.9, 1.515), (1.25, 1.495), (1.45, 1.475), (1.56, 1.46), (1.62, 1.415), (1.72, 1.29),
             (1.82, 1.16), (1.88, 1.085), (1.94, 1.03), (1.99, 0.98), (2.02, 0.90), (2.035, 0.80), (2.042, 0.68),
             (2.045, 0.58)])
FLOOR = curve([(-2.10, 0.40), (-2.095, 0.32), (-2.085, 0.28), (-2.06, 0.25), (-2.0, 0.225), (-1.90, 0.205),
               (1.90, 0.21), (1.98, 0.24), (2.02, 0.27), (2.04, 0.32), (2.045, 0.40)])
WIDTH = curve([(-2.10, 0.58), (-2.095, 0.66), (-2.085, 0.72), (-2.07, 0.765), (-2.045, 0.80), (-2.01, 0.83),
               (-1.96, 0.855), (-1.88, 0.875), (-1.75, 0.893), (-1.50, 0.902), (1.30, 0.904), (1.60, 0.90), (1.80, 0.89),
               (1.92, 0.87), (1.99, 0.845), (2.025, 0.81), (2.04, 0.76), (2.045, 0.70)])
SHOULDER = curve([(-2.10, 0.68), (-2.04, 0.80), (-1.95, 0.845), (-1.60, 0.875), (-1.20, 0.905), (-0.50, 0.94),
                  (0.50, 0.965), (1.40, 0.985), (1.90, 0.99), (2.045, 0.96)])
BELT = curve([(-1.30, 1.0), (-0.60, 1.02), (0.40, 1.05), (1.20, 1.085), (1.60, 1.10), (1.90, 1.10)])
RAIL = curve([(-1.30, 1.0), (-1.24, 1.03), (-0.90, 1.205), (-0.50, 1.39), (-0.22, 1.465), (0.10, 1.48),
              (0.60, 1.482), (1.10, 1.468), (1.45, 1.435), (1.60, 1.40), (1.75, 1.26), (1.88, 1.13), (1.94, 1.10)])
RAIL_W = curve([(-1.30, 0.865), (-0.90, 0.80), (-0.40, 0.70), (0.0, 0.675), (0.80, 0.68), (1.30, 0.67), (1.55, 0.645),
                (1.75, 0.62), (1.92, 0.64)])
STATIONS = [-2.10, -2.095, -2.085, -2.07, -2.045, -2.01, -1.96, -1.90, -1.82, -1.745, -1.69, -1.665, -1.60, -1.50, -1.40,
            -1.34, -1.30, -1.28, -1.20, -1.10, -1.00, -0.94, -0.925, -0.88, -0.80, -0.68, -0.55, -0.40, -0.25, -0.10, 0.06,
            0.15, 0.30, 0.45, 0.60, 0.75, 0.86, 0.91, 0.935, 1.00, 1.10, 1.20, 1.30, 1.40, 1.50, 1.60, 1.665, 1.69, 1.745,
            1.81, 1.88, 1.94, 1.99, 2.02, 2.035, 2.042, 2.045]
B_PILLAR = (0.06, 0.15)


def windows():
    out = []
    for s in (1, -1):
        b0, b1, b2 = (-1.2, BELT(-1.2) + 0.012), (0.4, BELT(0.4) + 0.012), (1.65, BELT(1.65) + 0.012)
        belt = [side_plane(*b0, *b1, (0.3, 1.3)), side_plane(*b1, *b2, (0.3, 1.3))]
        zs = (-0.30, -0.10, 0.40, 1.00, 1.42)
        top = [side_plane(za, RAIL(za) - 0.012, zb, RAIL(zb) - 0.012, (0.3, 1.1)) for za, zb in zip(zs, zs[1:])]
        front = side_plane(-1.25, 1.035, -0.32, 1.455, (0.3, 1.2))
        rear = side_plane(1.62, 1.115, 1.45, 1.42, (0.3, 1.2))
        near = (lambda s: lambda c, n: c[0] * s > 0.55 and -1.30 < c[2] < 1.68 and 0.98 < c[1] < 1.50 and n[0] * s > 0.3)(s)
        for zone in ((-2.0, B_PILLAR[0]), (B_PILLAR[1], 2.0)):
            cut = [(np.array([0.0, 0.0, zone[0]]), np.array([0.0, 0.0, 1.0])), (np.array([0.0, 0.0, zone[1]]), np.array([0.0, 0.0, -1.0]))]
            out.append({'kind': 'side', 'planes': belt + top + [front, rear] + cut, 'near': near, 'seal': 0.02, 'frit': 0.0})
    ws_n = (0.0, 0.85, -0.52)
    planes = [(np.array([0.0, 1.012, 0.0]), np.array([0.0, 1.0, 0.0])), (np.array([0.0, 0.0, -0.30]), np.array([0.0, 0.0, -1.0]))]
    for s in (1, -1):
        planes.append(plane_3d((s * (RAIL_W(-1.24) - 0.06), RAIL(-1.24), -1.24), (s * (RAIL_W(-0.32) - 0.06), RAIL(-0.32), -0.32), ws_n, (0.0, 1.2, -0.8)))
    out.append({'kind': 'front', 'planes': planes, 'seal': 0.02, 'frit': 0.05,
                'near': lambda c, n: -1.34 < c[2] < -0.20 and c[1] > 0.97 and abs(c[0]) < 0.87 and n[1] > 0.15 and n[2] < -0.05})
    rw_n = (0.0, 0.70, 0.71)
    planes = [(np.array([0.0, 0.0, 1.60]), np.array([0.0, 0.0, 1.0])), (np.array([0.0, 1.10, 0.0]), np.array([0.0, 1.0, 0.0]))]
    for s in (1, -1):
        planes.append(plane_3d((s * 0.57, 1.41, 1.60), (s * 0.535, 1.10, 1.875), rw_n, (0.0, 1.25, 1.75)))
    out.append({'kind': 'rear', 'planes': planes, 'seal': 0.02, 'frit': 0.035,
                'near': lambda c, n: 1.54 < c[2] < 1.96 and c[1] > 1.06 and abs(c[0]) < 0.66 and n[2] > 0.15})
    return out


HEADLIGHT = [(0.30, 0.768), (0.30, 0.810), (0.55, 0.826), (0.76, 0.836), (0.80, 0.834), (0.80, 0.780), (0.70, 0.770),
             (0.55, 0.765)]
TAILLIGHT = [(0.36, 0.975), (0.36, 1.03), (0.60, 1.045), (0.78, 1.05), (0.80, 1.048), (0.80, 0.985), (0.60, 0.975)]
HEAD_WRAP = [(-2.03, 0.780), (-1.97, 0.792), (-1.94, 0.812), (-1.97, 0.832), (-2.03, 0.836)]
TAIL_WRAP = [(1.94, 0.988), (2.0, 0.978), (2.03, 0.982), (2.03, 1.04), (2.0, 1.05), (1.94, 1.048)]


def decals(proj, level):
    parts = []
    add = lambda *a, **k: parts.append(decal(proj, *a, **k))
    for s in (1, -1):
        m = (lambda o: o) if s == 1 else mirror_x
        side = 'right' if s == 1 else 'left'
        add(m(HEADLIGHT), 'front', LAMP)
        add(m(TAILLIGHT), 'rear', TAIL)
        add(HEAD_WRAP, side, LAMP)
        add(TAIL_WRAP, side, TAIL)
        if level < 2:
            add(m(strip([(0.33, 0.814), (0.55, 0.824), (0.78, 0.830)], 0.007)), 'front', DRL, lift=0.0032)
            add(m(circle(0.47, 0.787, 0.02, 20)), 'front', CHROME, lift=0.0032)
            add(m(circle(0.62, 0.789, 0.02, 20)), 'front', CHROME, lift=0.0032)
            add(m(strip([(0.40, 1.022), (0.60, 1.036), (0.78, 1.040)], 0.006)), 'rear', DRL, lift=0.0034)
            add(m([(0.62, 0.40), (0.80, 0.40), (0.80, 0.428), (0.62, 0.428)]), 'rear', TAIL_DARK, lift=0.0025)
        add(m([(0.66, 0.34), (0.80, 0.36), (0.80, 0.52), (0.69, 0.50)]), 'front', GRILLE)           # air curtains
    # The black band between the headlights, the big lower intake, the black glass band across the tail, the diffuser.
    add([(-0.30, 0.768), (0.30, 0.768), (0.30, 0.810), (-0.30, 0.810)], 'front', GLOSS)
    add([(-0.62, 0.28), (0.62, 0.28), (0.55, 0.50), (-0.55, 0.50)], 'front', GRILLE)
    add([(-0.36, 0.975), (0.36, 0.975), (0.36, 1.03), (-0.36, 1.03)], 'rear', GLOSS)
    add([(-0.60, 0.29), (0.60, 0.29), (0.58, 0.35), (-0.58, 0.35)], 'rear', TRIM, max_edge=0.02)
    if level < 2:
        for view, cy, r in (('front', 0.789, 0.036), ('rear', 0.915, 0.05)):
            add(circle(0.0, cy, r, 28), view, CHROME, lift=0.0032)
            add(circle(0.0, cy, r * 0.84, 28), view, GLOSS, lift=0.0045)
        add([(-0.22, 1.44), (0.22, 1.44), (0.22, 1.455), (-0.22, 1.455)], 'rear', TAIL_DARK, lift=0.0025)
    if level < 1:
        for side in ('right', 'left'):
            for line in ([(-1.10, 1.012), (-0.98, 0.86), (-0.90, 0.72), (-0.885, 0.45), (-0.88, 0.27)],
                         [(0.105, 1.05), (0.108, 0.70), (0.11, 0.27)],
                         [(1.05, 1.08), (1.08, 0.90), (1.06, 0.76), (1.0, 0.66), (0.95, 0.57), (0.915, 0.47),
                          (0.895, 0.38), (0.89, 0.27)]):
                add(strip(line, 0.0045), side, GAP, lift=0.0015, max_edge=0.04)
        flap = [(1.55, 0.865), (1.70, 0.865), (1.71, 0.88), (1.71, 0.955), (1.695, 0.97), (1.555, 0.97), (1.54, 0.955), (1.54, 0.88)]
        for a, b in zip(flap, flap[1:] + flap[:1]):
            add(strip([a, b], 0.004), 'right', GAP, lift=0.0015, max_edge=0.04)
        for s in (1, -1):
            m = (lambda o: o) if s == 1 else mirror_x
            add(m(strip([(0.60, 1.41), (0.62, 1.25), (0.645, 1.10), (0.66, 1.05)], 0.0045)), 'rear', GAP, lift=0.0015, max_edge=0.04)
            add(m(strip([(0.36, 0.975), (0.36, 0.75), (0.40, 0.62)], 0.0045)), 'rear', GAP, lift=0.0015, max_edge=0.04)
        add(strip([(-0.40, 0.62), (0.40, 0.62)], 0.0045), 'rear', GAP, lift=0.0015, max_edge=0.04)
    return concat(parts)


OVERRIDES = {'TOP': TOP, 'FLOOR': FLOOR, 'WIDTH': WIDTH, 'SHOULDER': SHOULDER, 'BELT': BELT, 'RAIL': RAIL,
             'RAIL_W': RAIL_W, 'STATIONS': STATIONS, 'B_PILLAR': B_PILLAR, 'GLASS_Z': (-1.30, 1.94),
             'windows': windows, 'decals': decals, 'MIRROR_AT': (-1.08, 1.01), 'HANDLE_Z': (-0.55, 0.55),
             'WIPER_Z': -1.255, 'PLATE_Y': (0.585, 0.80)}
