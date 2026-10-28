"""Tree crowns with a real structure (photo-real pass), used by build_assets.foliage() and build_trees.py.

The web's crown is one round cloud of cards. Real Torshov street and park trees (lime, maple, birch) show a
trunk leader, a few curved main limbs, side branches and separate leaf masses with sky between them. This builds
that in the web's own normalised crown space (surroundings.js plantTrees: origin just below the trunk top, scaled
(r * .95, crown * .5, r * .95), so x/z run about -1..1 and y 0..2), with a similar triangle budget:

  leader      the trunk continuing up to y 1.55, tapering
  limbs       5-7 main limbs from y 0..0.8 on the leader, curving out to radius 0.5-0.9 and up 0.45-0.95
  side twigs  two per limb, from its middle
  fill        7-9 masses around the crown from low to high, so it reads full and round from a distance
  lobes       a leaf mass at every limb and twig tip and the top: 12-15 leaf-cluster cards of 0.4-0.6 (the scanned leaves of
              tools/make-leaf-atlas.py) inside an ellipsoid of radius 0.32-0.5, denser at the surface
Card normals blend the direction from the crown centre and from the lobe centre, so each lobe is lit as a volume
inside the crown's light. Slot 0 leaves, slot 1 bark. 'far' variants (the trees beyond 300 m) have no side twigs
and fewer cards. Corner arrays in the web frame, as build_assets.gather() makes them.
"""
import math

import numpy as np


def _rot(rng):
    q = rng.normal(size=4)
    q /= np.linalg.norm(q)
    x, y, z, w = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def _card(c, size, rng):
    pts = np.array([[-1, -1, 0], [1, -1, 0], [1, 1, 0], [-1, 1, 0]]) * size / 2
    pts = pts @ _rot(rng).T + c
    uv = np.array([[0, 0], [1, 0], [1, 1], [0, 1]], dtype=np.float64)
    return pts[[0, 1, 2, 0, 2, 3]].reshape(2, 3, 3), uv[[0, 1, 2, 0, 2, 3]].reshape(2, 3, 2)


def _cylinder(a, b, r0, r1, sides, slot, v_repeat=1.0):
    a, b = np.asarray(a, float), np.asarray(b, float)
    d = (b - a) / np.linalg.norm(b - a)
    t = np.cross(d, [0.0, 0.0, 1.0] if abs(d[2]) < 0.9 else [1.0, 0.0, 0.0])
    t /= np.linalg.norm(t)
    s = np.cross(d, t)
    P, N, U = [], [], []
    for i in range(sides):
        a0, a1 = 2 * np.pi * i / sides, 2 * np.pi * (i + 1) / sides
        n0, n1 = np.cos(a0) * t + np.sin(a0) * s, np.cos(a1) * t + np.sin(a1) * s
        p00, p01, p10, p11 = a + n0 * r0, a + n1 * r0, b + n0 * r1, b + n1 * r1
        u0, u1 = i / sides * 2, (i + 1) / sides * 2
        P += [[p00, p10, p11], [p00, p11, p01]]
        N += [[n0, n0, n1], [n0, n1, n1]]
        U += [[[u0, 0], [u0, v_repeat], [u1, v_repeat]], [[u0, 0], [u1, v_repeat], [u1, 0]]]
    return {'pos': np.array(P), 'nor': np.array(N), 'uv': np.array(U), 'slot': np.full(len(P), slot)}


def _curve(p0, p1, bend, n):
    """Points along a gently bowed line from p0 to p1 (bow upward by 'bend')."""
    out = []
    for k in range(n + 1):
        t = k / n
        p = p0 + (p1 - p0) * t
        p = p + np.array([0.0, bend * math.sin(math.pi * t), 0.0])
        out.append(p)
    return out


def crown(variant, far=False):
    rng = np.random.default_rng(300 + variant * 17 + (5 if far else 0))
    parts, lobes = [], []
    # Leader.
    lead = [np.array([0.0, -0.05, 0.0]), np.array([0.02, 0.6, -0.01]), np.array([-0.02, 1.1, 0.02]), np.array([0.0, 1.55, 0.0])]
    rad = [0.06, 0.045, 0.03, 0.015]
    for k in range(3):
        parts.append(_cylinder(lead[k], lead[k + 1], rad[k], rad[k + 1], 6, 1))
    lobes.append((lead[-1] + [0, 0.12, 0], 0.32))
    # Main limbs, spread around (golden angle) with some randomness.
    limbs = int(rng.integers(5, 8))
    upright = 0.25 if variant % 2 else 0.0                       # variant B is taller and narrower
    for i in range(limbs):
        a = i * 2.39996 + rng.uniform(-0.3, 0.3)
        y0 = 0.05 + 0.75 * (i + rng.uniform(0, 0.6)) / limbs
        base = np.array([0.0, y0, 0.0])
        reach = rng.uniform(0.5, 0.88) * (1 - upright * 0.5)
        tip = np.array([math.cos(a) * reach, y0 + rng.uniform(0.45, 0.95) + upright, math.sin(a) * reach])
        pts = _curve(base, tip, rng.uniform(0.05, 0.18), 2)
        r0 = 0.04 - 0.012 * (y0 / 0.8)
        for k in range(2):
            parts.append(_cylinder(pts[k], pts[k + 1], r0 * (1 - 0.45 * k), r0 * (1 - 0.45 * (k + 1)) + 0.004, 5, 1))
        lobes.append((tip, rng.uniform(0.4, 0.5)))
        if far:
            continue
        mid = pts[1]
        for side in (-1, 1):
            b = a + side * rng.uniform(0.5, 0.9)
            twig = mid + np.array([math.cos(b) * rng.uniform(0.22, 0.38), rng.uniform(0.15, 0.4), math.sin(b) * rng.uniform(0.22, 0.38)])
            parts.append(_cylinder(mid, twig, r0 * 0.45, 0.006, 4, 1))
            lobes.append((twig, rng.uniform(0.32, 0.4)))
    # Fill: a few masses around the middle of the crown on short twigs, so it reads full from a distance.
    for i in range(int(rng.integers(7, 10))):
        a = i * 2.39996 + rng.uniform(-0.4, 0.4)
        c = np.array([math.cos(a) * rng.uniform(0.4, 0.62), rng.uniform(0.35, 1.3) + upright * 0.5, math.sin(a) * rng.uniform(0.4, 0.62)])
        lobes.append((c, rng.uniform(0.4, 0.5)))
        if not far:
            parts.append(_cylinder(np.array([0.0, c[1] - 0.3, 0.0]), c, 0.012, 0.005, 4, 1))
    # Leaf masses.
    centre = np.array([0.0, 0.9, 0.0])
    P, N, U = [], [], []
    for c, lr in lobes:
        cards = int(rng.integers(12, 16)) if not far else int(rng.integers(15, 19))
        for _ in range(cards):
            d = rng.normal(size=3)
            d /= np.linalg.norm(d)
            p = c + d * lr * rng.uniform(0.35, 1.0) ** 0.5 * np.array([1.0, 0.8, 1.0])
            size = rng.uniform(0.4, 0.6) * (1.15 if far else 1.0)
            tri, uv = _card(p, size, rng)
            n1 = tri - centre
            n1 /= np.linalg.norm(n1, axis=-1, keepdims=True)
            n2 = tri - c
            n2 /= np.maximum(np.linalg.norm(n2, axis=-1, keepdims=True), 1e-6)
            nrm = 0.6 * n1 + 0.4 * n2
            nrm /= np.linalg.norm(nrm, axis=-1, keepdims=True)
            P.append(tri); N.append(nrm); U.append(uv)
    leaves = {'pos': np.concatenate(P), 'nor': np.concatenate(N), 'uv': np.concatenate(U),
              'slot': np.zeros(len(P) * 2, dtype=np.int64)}
    parts.insert(0, leaves)
    return {k: np.concatenate([p[k] for p in parts]) for k in ('pos', 'nor', 'uv', 'slot')}
