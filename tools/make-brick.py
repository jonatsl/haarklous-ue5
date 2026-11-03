"""The brick of the Torshov blocks, from the Poly Haven scan brick_wall_09 (2 Nov 2026, realism pass):

    python tools/make-brick.py

In Google Street View the walls around Haarklous plass are red-brown brick with dark grey, slightly recessed mortar
and a lively mix of lighter and darker (purple-brown) bricks. The scan has light beige mortar, which made whole walls
read pink from a distance. This darkens the mortar (found with the scan's own height map) and spreads the bricks'
colours a little more, and writes
  tools/out/library/brick_wall_09/brick_wall_09_hk_diff_2k.png   used as T_Brick_D (materials_photoreal.py LIBRARY)
and prints the new average linear colour, which materials_photoreal.py and blender/facade_model.py use as BRICK_AVG
(the materials tint the scan so a wall's average is its building's colour).
"""
import os

import numpy as np
from PIL import Image, ImageFilter

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
LIB = os.path.join(ROOT, 'tools', 'out', 'library', 'brick_wall_09')
MORTAR = np.array([86, 83, 79]) / 255.0          # dark grey cement mortar (sRGB)


def lin(c):
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def srgb(c):
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * np.power(np.clip(c, 0, None), 1 / 2.4) - 0.055)


c = np.asarray(Image.open(os.path.join(LIB, 'brick_wall_09_diff_2k.jpg')).convert('RGB')).astype(float) / 255
disp = Image.open(os.path.join(LIB, 'brick_wall_09_disp_2k.jpg')).convert('L')
d = np.asarray(disp.filter(ImageFilter.GaussianBlur(1.5))).astype(float) / 255
local = np.asarray(disp.filter(ImageFilter.GaussianBlur(14))).astype(float) / 255
valley = np.clip((local - d) / 0.025, 0, 1)                         # lower than its surroundings: joints and pits
# Joints are long lines (bed joints along x, head joints along y); pits in the brick faces are small spots.
def run(a, n, axis):
    k = np.ones(n) / n
    return np.apply_along_axis(lambda v: np.convolve(np.concatenate([v[-n:], v, v[:n]]), k, mode='same')[n:-n], axis, a)
bed, head = run(valley, 41, 1), run(valley, 17, 0)
mortar = np.clip(np.maximum((bed - 0.27) / 0.28, (head - 0.37) / 0.28), 0, 1)
mortar = np.asarray(Image.fromarray((mortar * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.2))).astype(float)[..., None] / 255
L = lin(c)
# Bricks: spread their colour around the wall's mean (more contrast between bricks), a touch redder.
mean = L[mortar[..., 0] < 0.2].mean(axis=0)
B = np.clip(mean + (L - mean) * 1.45, 0, 1) * np.array([1.04, 0.97, 0.95])
out = B * (1 - mortar) + lin(MORTAR) * (0.85 + 0.3 * (L.mean(axis=2, keepdims=True) / L.mean())) * mortar
img = (np.clip(srgb(out), 0, 1) * 255 + 0.5).astype(np.uint8)
Image.fromarray(img).save(os.path.join(LIB, 'brick_wall_09_hk_diff_2k.png'))
avg = lin(img.astype(float) / 255).reshape(-1, 3).mean(axis=0)
print('BRICK_AVG = (%.4f, %.4f, %.4f)' % tuple(avg))
