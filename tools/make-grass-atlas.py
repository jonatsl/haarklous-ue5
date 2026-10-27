"""Grass card atlas for the 3D lawn (photo-real pass), from the CC0 Poly Haven grass_medium_01 texture maps
(tools/fetch-library.py):

    python tools/make-grass-atlas.py

The model's atlas holds five photographed-looking grass tufts at the bottom. Each is cut out by its shape, cleaned
(the stems below the root line, a stray leaf patch and a long stick are removed) and packed into one 2048 x 1024
atlas with the colour bled into the transparent parts (no dark fringes in the mip maps).
Writes tools/out/textures/T_GrassCards_D.png (RGBA: colour + opacity), T_GrassCards_N.png (normal) and
grass-cards.json (each tuft's UV rectangle and its width / height ratio) for tools/blender/build_grass.py.
"""
import json
import os

import numpy as np
from PIL import Image
from scipy import ndimage

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
LIB = os.path.join(ROOT, 'tools', 'out', 'library', 'grass_medium_01')
OUT = os.path.join(ROOT, 'tools', 'out', 'textures')
W, H = 2048, 1024


def tufts(alpha):
    m = alpha > 60
    m[:1450] = False                      # the tufts are in the bottom quarter of the atlas
    lab, _ = ndimage.label(ndimage.binary_dilation(m, iterations=3))
    out = []
    for i, s in enumerate(ndimage.find_objects(lab)):
        k = (lab[s] == i + 1) & m[s]
        if k.sum() >= 15000 and s[0].start > 1500:
            out.append((s, k))
    return out


def clean(rgb, a, keep, x_end):
    a = a.astype(np.float32) / 255 * keep
    # A leaf patch and a long stick lie next to two tufts in the source atlas: cut at a fixed x.
    if x_end:
        a[:, x_end:] = 0
    rows = a.sum(axis=1)
    # Root line = the densest row in the lower part of the tuft; below it are stems of the model's base geometry.
    lower = int(len(rows) * 0.45)
    base = lower + int(np.argmax(rows[lower:]))
    a[base + 6:] = 0
    cols = a.sum(axis=0)
    lit = np.where(cols > 0.04 * cols.max())[0]
    x0, x1 = max(0, lit[0] - 4), min(a.shape[1], lit[-1] + 5)
    ys = np.where(a.sum(axis=1) > 0)[0]
    y0 = max(0, ys[0] - 4)
    return rgb[y0:base + 3, x0:x1], a[y0:base + 3, x0:x1]


def bleed(rgb, a):
    """Spread the colour of the blades into the transparent pixels around them."""
    rgb = rgb.astype(np.float32)
    w = (a > 0.3).astype(np.float32)
    acc, wacc = rgb * w[..., None], w.copy()
    for r in (2, 6, 18, 60):
        acc2 = np.stack([ndimage.uniform_filter(acc[..., c], r * 2 + 1) for c in range(3)], -1)
        w2 = ndimage.uniform_filter(wacc, r * 2 + 1)
        fill = (wacc < 1e-3) & (w2 > 1e-4)
        acc[fill] = acc2[fill] / w2[fill, None]
        wacc[fill] = 1
    acc[wacc < 1e-3] = rgb[a > 0.3].mean(axis=0)
    return acc


def main():
    d = np.array(Image.open(os.path.join(LIB, 'grass_medium_01_diff_2k.jpg')).convert('RGB'))
    n = np.array(Image.open(os.path.join(LIB, 'grass_medium_01_nor_gl_2k.jpg')).convert('RGB'))
    al = np.array(Image.open(os.path.join(LIB, 'grass_medium_01_alpha_2k.jpg')).convert('L'))
    pieces = []
    # Source x (atlas pixels) where the leaf patch (tuft at x 394) and the stick (tuft at x 1194) start.
    cut = {394: 1012, 1194: 1676}
    for s, keep in tufts(al):
        x_end = cut.get(s[1].start, 0) - s[1].start if s[1].start in cut else 0
        rgb, a = clean(np.concatenate([d[s], n[s]], axis=-1), al[s], keep, x_end)
        pieces.append((rgb, a))
    pieces.sort(key=lambda p: -p[0].shape[1])
    # Shelf packing: two rows of 512 px.
    atlas = np.zeros((H, W, 6), np.float32)
    alpha = np.zeros((H, W), np.float32)
    rects, x, y, row_h = [], 0, 0, 0
    for rgb, a in pieces:
        h, w = a.shape
        scale = min(1.0, (H // 2 - 8) / h)
        im = Image.fromarray(rgb[..., :3]).resize((max(1, int(w * scale)), max(1, int(h * scale))), Image.LANCZOS)
        nm = Image.fromarray(rgb[..., 3:]).resize(im.size, Image.LANCZOS)
        am = np.array(Image.fromarray((a * 255).astype(np.uint8)).resize(im.size, Image.LANCZOS)).astype(np.float32) / 255
        w2, h2 = im.size
        if x + w2 > W:
            x, y, row_h = 0, y + row_h + 8, 0
        assert y + h2 <= H, 'atlas full'
        atlas[y:y + h2, x:x + w2, :3] = np.array(im)
        atlas[y:y + h2, x:x + w2, 3:] = np.array(nm)
        alpha[y:y + h2, x:x + w2] = am
        rects.append({'u0': x / W, 'v0': y / H, 'u1': (x + w2) / W, 'v1': (y + h2) / H, 'aspect': w2 / h2})
        x, row_h = x + w2 + 8, max(row_h, h2)
    colour = bleed(atlas[..., :3], alpha)
    normal = atlas[..., 3:].copy()
    normal[alpha < 0.05] = (128, 128, 255)
    os.makedirs(OUT, exist_ok=True)
    rgba = np.concatenate([np.clip(colour, 0, 255), alpha[..., None] * 255], axis=-1).astype(np.uint8)
    Image.fromarray(rgba, 'RGBA').save(os.path.join(OUT, 'T_GrassCards_D.png'))
    Image.fromarray(np.clip(normal, 0, 255).astype(np.uint8)).save(os.path.join(OUT, 'T_GrassCards_N.png'))
    json.dump({'tufts': rects}, open(os.path.join(OUT, 'grass-cards.json'), 'w'), indent=1)
    print(f'HK: grass atlas with {len(rects)} tufts -> {os.path.join(OUT, "T_GrassCards_D.png")}')


main()
