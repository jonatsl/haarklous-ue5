"""Pull apart surfaces that lie in the same plane and overlap (3 Nov 2026), used by build_assets.py.

The web game draws some surfaces exactly on top of others and settles which one shows with polygonOffset (the flat's
walls are pushed back, game-apartment.js R()) or simply by draw order. Unreal ignores both, so such pairs fought over
the same pixels and flickered (the flat: neighbouring rooms' wall paints, skirting, kitchen paint, wall tiles; the
stairwells: brick, plaster, terrazzo). Here, in every plane where triangles of different materials overlap, each
material is moved off the plane along the normal by STEP times its rank, the largest surface staying put (so a strip
of skirting or a patch of tiles comes forward of the wall it lies on). 1.2 mm is far below what the eye can see at the
distances these are seen from, and plenty for the depth buffer.

    offsets = coplanar.plane_offsets(P, K)      # P: T x 3 x 3 corners, K: material key per triangle
    P = P + N_face * offsets[:, None, None]
"""
from collections import defaultdict

import numpy as np

STEP = 0.0012


def _side(p, a, b):
    return (b[0] - a[0]) * (p[1] - a[1]) - (b[1] - a[1]) * (p[0] - a[0])


def overlap_area(A, B):
    """Area of the overlap of two 2D triangles (Sutherland-Hodgman clipping of A by B)."""
    if _side(B[2], B[0], B[1]) < 0:
        B = B[::-1]
    poly = list(A)
    for k in range(3):
        a, b = B[k], B[(k + 1) % 3]
        inp, poly = poly, []
        for i in range(len(inp)):
            p, q = inp[i], inp[(i + 1) % len(inp)]
            sp, sq = _side(p, a, b), _side(q, a, b)
            if sp >= 0:
                poly.append(p)
            if (sp >= 0) != (sq >= 0):
                t = sp / (sp - sq)
                poly.append((p[0] + (q[0] - p[0]) * t, p[1] + (q[1] - p[1]) * t))
        if not poly:
            return 0.0
    return abs(sum(poly[i][0] * poly[(i + 1) % len(poly)][1] - poly[(i + 1) % len(poly)][0] * poly[i][1]
                   for i in range(len(poly)))) / 2


def face_normals(P):
    n = np.cross(P[:, 1] - P[:, 0], P[:, 2] - P[:, 0])
    return n / np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-12), np.linalg.norm(n, axis=1) / 2


def plane_offsets(P, K, step=STEP):
    """Offset (metres, along each triangle's own normal) for every triangle; 0 for most."""
    K = np.asarray(K)
    n, area = face_normals(P)
    d = (n * P[:, 0]).sum(axis=1)
    key = np.round(np.concatenate([n * 50, d[:, None] * 500], axis=1)).astype(np.int64)    # planes within ~2 mm
    groups = defaultdict(list)
    for i in np.nonzero(area > 2e-6)[0]:
        groups[tuple(key[i])].append(i)
    out = np.zeros(len(P))
    for idx in groups.values():
        if len(set(K[idx].tolist())) < 2:
            continue
        idx = np.array(idx)
        nn = n[idx[0]]
        u = np.cross(nn, [0, 0, 1] if abs(nn[2]) < 0.9 else [1, 0, 0]); u /= np.linalg.norm(u); v = np.cross(nn, u)
        Q = np.stack([P[idx] @ u, P[idx] @ v], axis=2)
        lo, hi = Q.min(axis=1), Q.max(axis=1)
        involved = set()
        for a in range(len(idx)):
            cand = np.nonzero(np.all((lo[a] < hi - 1e-4) & (lo < hi[a] - 1e-4), axis=1))[0]
            for b in cand:
                if b > a and K[idx[a]] != K[idx[b]] and overlap_area([tuple(x) for x in Q[a]], [tuple(x) for x in Q[b]]) > 1e-6:
                    involved.update((K[idx[a]], K[idx[b]]))
        if not involved:
            continue
        total = {m: area[idx][K[idx] == m].sum() for m in involved}
        for rank, m in enumerate(sorted(involved, key=lambda m: -total[m])):
            out[idx[K[idx] == m]] = rank * step
    return out


def separate(data, slot_keys, name=''):
    """build_assets corner data (pos, nor, ... per triangle) with the overlapping coplanar surfaces pulled apart."""
    K = [slot_keys[s] for s in data['slot']]
    off = plane_offsets(data['pos'], K)
    moved = int((off > 0).sum())
    if moved:
        n, _ = face_normals(data['pos'])
        data = dict(data)
        data['pos'] = data['pos'] + (n * off[:, None])[:, None, :]
        print(f'HK: {name}: {moved} triangles moved off planes they shared with other materials')
    return data
