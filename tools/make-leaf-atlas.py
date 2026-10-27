"""Leaf-cluster texture for the tree crowns (photo-real pass), from the CC0 Poly Haven tree_small_02 leaf maps
(tools/fetch-library.py):

    python tools/make-leaf-atlas.py

The crowns are cards of 1.5-3 m (tools/blender/build_assets.py foliage()), so one card must show hundreds of leaves.
The source atlas has single scanned leaves with slivers of other texture around them: the leaves are cut out by
colour and shape (green, solid, larger than a stem) and scattered over a 1024 px card in a ragged cluster, about
35 px (7 cm) each, with twigs underneath and lighter / yellower variety. Writes tools/out/textures/T_Leaves_D.png
(RGBA) and T_Leaves_N.png (normal from the leaf shading).
"""
import math
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter
from scipy import ndimage

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
LIB = os.path.join(ROOT, 'tools', 'out', 'library', 'tree_small_02')
OUT = os.path.join(ROOT, 'tools', 'out', 'textures')
S = 1024


def leaves():
    d = np.array(Image.open(os.path.join(LIB, 'tree_small_02_leaves_diff_2k.jpg')).convert('RGB')).astype(np.float32)
    a = np.array(Image.open(os.path.join(LIB, 'tree_small_02_leaves_alpha_2k.jpg')).convert('L')) > 128
    r, g, b = d[..., 0], d[..., 1], d[..., 2]
    green = (g > r * 1.02) & (g > b * 1.1) & (g < 200)
    m = ndimage.binary_opening(a & green, iterations=5)          # stems and slivers are thinner than this
    m[:, 1450:] = False                                           # the filled region on the right is not leaves
    lab, n = ndimage.label(m)
    out = []
    for i, s in enumerate(ndimage.find_objects(lab)):
        k = lab[s] == i + 1
        h, w = k.shape
        if k.sum() < 4000 or k.sum() > 60000 or max(h, w) > 3.5 * min(h, w):
            continue
        # The leaf blade only: a wide opening cuts off its stalk and the slivers of other texture stuck to it.
        k = ndimage.binary_opening(np.pad(k, 12), structure=np.ones((3, 3)), iterations=10)[12:-12, 12:-12]
        if k.sum() < 3000:
            continue
        ys, xs = np.nonzero(k)
        ev = np.linalg.eigvalsh(np.cov(np.stack([xs, ys])))
        if ev[1] > 5.5 * ev[0] or d[s][k].mean() < 55:                # slivers and dark stalk ends are not leaves
            continue
        rgba = np.dstack([d[s], k * 255.0]).astype(np.uint8)
        out.append(Image.fromarray(rgba, 'RGBA'))
    return out


def main():
    rng = np.random.default_rng(5)
    pieces = leaves()
    canvas = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    draw = ImageDraw.Draw(canvas)
    # Twigs: a few branching lines from the lower middle.
    for _ in range(7):
        x, y = S * (0.5 + rng.uniform(-0.1, 0.1)), S * 0.8
        ang = -math.pi / 2 + rng.uniform(-1.1, 1.1)
        for seg in range(4):
            l = rng.uniform(40, 80)
            nx, ny = x + math.cos(ang) * l, y + math.sin(ang) * l
            draw.line([(x, y), (nx, ny)], fill=(62, 50, 38, 255), width=max(2, 5 - seg))
            x, y, ang = nx, ny, ang + rng.uniform(-0.5, 0.5)
    # Leaves in a ragged round cluster, denser in the middle, each turned, sized and tinted at random.
    placed = 0
    for _ in range(9000):
        if placed >= 1100:
            break
        px, py = rng.uniform(0, S), rng.uniform(0, S)
        rr = math.hypot(px - S / 2, py - S / 2) / (S / 2)
        edge = 0.82 + 0.16 * math.sin(math.atan2(py - S / 2, px - S / 2) * 5 + 1.3)
        if rr > edge or rng.uniform() < rr * 0.4:
            continue
        leaf = pieces[int(rng.integers(len(pieces)))]
        size = rng.uniform(32, 48)
        sc = size / max(leaf.size)
        im = leaf.resize((max(4, int(leaf.size[0] * sc)), max(4, int(leaf.size[1] * sc))), Image.LANCZOS)
        im = im.rotate(rng.uniform(0, 360), expand=True, resample=Image.BICUBIC)
        arr = np.array(im).astype(np.float32)
        tint = np.array([1.0, 1.0, 1.0]) * rng.uniform(0.75, 1.15)
        if rng.uniform() < 0.25:
            tint *= [1.12, 1.08, 0.8]                                 # younger, yellower leaves
        arr[..., :3] = np.clip(arr[..., :3] * tint, 0, 255)
        canvas.alpha_composite(Image.fromarray(arr.astype(np.uint8), 'RGBA'), (int(px - im.size[0] / 2), int(py - im.size[1] / 2)))
        placed += 1
    rgba = np.array(canvas).astype(np.float32)
    alpha = rgba[..., 3] / 255
    # Bleed colour into the transparent parts (no dark fringes in the mip maps).
    col = rgba[..., :3]
    w = (alpha > 0.5).astype(np.float32)
    acc, wacc = col * w[..., None], w.copy()
    for r in (2, 6, 18, 60):
        acc2 = np.stack([ndimage.uniform_filter(acc[..., c], r * 2 + 1) for c in range(3)], -1)
        w2 = ndimage.uniform_filter(wacc, r * 2 + 1)
        fill = (wacc < 1e-3) & (w2 > 1e-4)
        acc[fill] = acc2[fill] / w2[fill, None]
        wacc[fill] = 1
    acc[wacc < 1e-3] = col[alpha > 0.5].mean(axis=0)
    out = np.dstack([np.clip(acc, 0, 255), alpha * 255]).astype(np.uint8)
    os.makedirs(OUT, exist_ok=True)
    Image.fromarray(out, 'RGBA').save(os.path.join(OUT, 'T_Leaves_D.png'))
    # Normal: leaves bulge a little (from the blurred coverage) plus their own shading.
    hgt = ndimage.gaussian_filter(alpha, 3) * 2 + ndimage.gaussian_filter(out[..., 1].astype(np.float32) / 255, 1) * 0.6
    gx, gy = np.gradient(hgt)
    nrm = np.dstack([-gx * 4, gy * 4, np.ones_like(gx)])
    nrm /= np.linalg.norm(nrm, axis=-1, keepdims=True)
    Image.fromarray(((nrm * 0.5 + 0.5) * 255).astype(np.uint8)).save(os.path.join(OUT, 'T_Leaves_N.png'))
    print(f'HK: leaf cluster from {len(pieces)} scanned leaves, {placed} placed -> {os.path.join(OUT, "T_Leaves_D.png")}')


main()
