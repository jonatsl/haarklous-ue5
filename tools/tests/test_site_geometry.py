"""Checks of tools/blender/site_geometry.py that run without Blender (issue #1, like the web tests/paintball.test.cjs):
the end caps of a prism fill the profile exactly, also for the concave jersey barrier.
    python tools/tests/test_site_geometry.py
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'blender'))
from site_geometry import cap_tris  # noqa: E402

JERSEY = [(-.3, 0), (.3, 0), (.3, .08), (.13, .32), (.1, .85), (-.1, .85), (-.13, .32), (-.3, .08)]


def area(p):
    return sum(p[i][0] * p[(i + 1) % len(p)][1] - p[(i + 1) % len(p)][0] * p[i][1] for i in range(len(p))) / 2


def check(name, p):
    tris = cap_tris(p)
    assert len(tris) == len(p) - 2, f'{name}: {len(tris)} triangles'
    a = area(p)
    parts = [area([p[i] for i in t]) for t in tris]
    assert all((x > 0) == (a > 0) for x in parts), f'{name}: a triangle is wound backwards'
    assert abs(sum(parts) - a) < 1e-9, f'{name}: triangles cover {sum(parts)}, outline {a}'
    print(f'{name}: {len(tris)} triangles, area {a:.4f} ok')


check('jersey', JERSEY)
check('jersey reversed', JERSEY[::-1])
check('square', [(0, 0), (1, 0), (1, 1), (0, 1)])
