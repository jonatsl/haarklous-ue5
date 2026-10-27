"""Make the tiling ground textures in Blender (headless): grass, asphalt, gravel and sand, each with colour (_D),
normal (_N, DirectX convention as Unreal expects) and roughness (_R) maps, seamless by construction (FFT noise and
wrapped stamps).

    blender -b --factory-startup -P tools/blender/make_textures.py

Writes tools/out/textures/T_<name>_{D,N,R}.png; tools/unreal/materials_ground.py imports them.
"""
import os

import bpy
import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
OUT = os.path.join(ROOT, 'tools', 'out', 'textures')
N = 1024


def fbm(beta, seed, n=N):
    """Seamless 1/f^beta noise, normalised to 0..1."""
    rng = np.random.default_rng(seed)
    fx = np.fft.fftfreq(n)[:, None]
    fy = np.fft.fftfreq(n)[None, :]
    f = np.sqrt(fx * fx + fy * fy)
    f[0, 0] = 1
    spec = (rng.normal(size=(n, n)) + 1j * rng.normal(size=(n, n))) / f ** beta
    spec[0, 0] = 0
    img = np.real(np.fft.ifft2(spec))
    return (img - img.min()) / (img.max() - img.min())


def band(seed, lo, hi, n=N):
    """Seamless noise containing only frequencies between lo and hi cycles per tile."""
    rng = np.random.default_rng(seed)
    fx = np.fft.fftfreq(n, 1 / n)[:, None]
    fy = np.fft.fftfreq(n, 1 / n)[None, :]
    f = np.sqrt(fx * fx + fy * fy)
    spec = (rng.normal(size=(n, n)) + 1j * rng.normal(size=(n, n))) * ((f >= lo) & (f <= hi))
    img = np.real(np.fft.ifft2(spec))
    return (img - img.min()) / (img.max() - img.min())


def stamps(count, rmin, rmax, seed, n=N, squash=0.7):
    """Height field of rounded pebbles (wrapped at the edges) and a per-pebble id map."""
    rng = np.random.default_rng(seed)
    h = np.zeros((n, n))
    ids = np.zeros((n, n))
    for k in range(count):
        r = rng.uniform(rmin, rmax)
        cx, cy = rng.uniform(0, n, 2)
        a = rng.uniform(0, np.pi)
        size = int(r * 1.3) + 2
        ys, xs = np.mgrid[-size:size + 1, -size:size + 1]
        u = (xs * np.cos(a) + ys * np.sin(a)) / r
        v = (-xs * np.sin(a) + ys * np.cos(a)) / (r * squash)
        d = 1 - (u * u + v * v)
        dome = np.sqrt(np.clip(d, 0, 1)) * r * 0.6
        iy = (np.arange(-size, size + 1) + int(cy)) % n
        ix = (np.arange(-size, size + 1) + int(cx)) % n
        block = h[np.ix_(iy, ix)]
        mask = dome > block
        block[mask] = dome[mask]
        h[np.ix_(iy, ix)] = block
        idb = ids[np.ix_(iy, ix)]
        idb[mask] = rng.uniform(0.0, 1.0)
        ids[np.ix_(iy, ix)] = idb
    return h, ids


def normal_map(h, strength):
    dx = (np.roll(h, -1, axis=1) - np.roll(h, 1, axis=1)) * 0.5 * strength
    dy = (np.roll(h, -1, axis=0) - np.roll(h, 1, axis=0)) * 0.5 * strength
    n = np.stack([-dx, -dy, np.ones_like(h)], axis=-1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    return n * 0.5 + 0.5


def save(name, rgb, srgb=True):
    """rgb: H x W x 3 in 0..1 (top row first)."""
    h, w = rgb.shape[:2]
    img = bpy.data.images.new(name, w, h, alpha=False, float_buffer=False)
    img.colorspace_settings.name = 'sRGB' if srgb else 'Non-Color'
    rgba = np.concatenate([np.clip(rgb, 0, 1), np.ones((h, w, 1))], axis=-1)[::-1]   # Blender rows start at the bottom
    img.pixels.foreach_set(rgba.astype(np.float32).ravel())
    img.filepath_raw = os.path.join(OUT, name + '.png')
    img.file_format = 'PNG'
    img.save()
    print('HK: wrote', name)


def mix(a, b, t):
    return a + (b - a) * t[..., None]


def col(r, g, b):
    return np.array([r, g, b]) / 255.0


def grass():
    low, mid = fbm(2.2, 1), band(2, 20, 60)
    blades = band(3, 60, 170)
    blades2 = band(4, 25, 70)
    base = mix(np.broadcast_to(col(66, 92, 40), (N, N, 3)), np.broadcast_to(col(92, 118, 52), (N, N, 3)), low)
    base = mix(base, np.broadcast_to(col(118, 122, 64), (N, N, 3)), np.clip((mid - 0.62) * 3, 0, 1) * 0.6)   # dry patches
    base *= (0.66 + 0.68 * blades)[..., None] * (0.82 + 0.36 * blades2)[..., None]
    dark = np.clip((fbm(1.6, 5) - 0.7) * 4, 0, 1)
    base *= (1 - 0.25 * dark)[..., None]
    save('T_Grass_D', base)
    save('T_Grass_N', normal_map(blades * 3 + blades2 * 2 + mid, 2.5), srgb=False)
    save('T_Grass_R', np.repeat((0.82 + 0.12 * blades)[..., None], 3, axis=2), srgb=False)


def asphalt():
    low = fbm(2.0, 11)
    grain = band(12, 250, 512)
    speck = band(13, 300, 512)
    base = np.broadcast_to(col(62, 63, 66), (N, N, 3)) * (0.85 + 0.3 * low)[..., None]
    base = base * (0.82 + 0.36 * grain)[..., None]
    light = np.clip((speck - 0.78) * 6, 0, 1)
    base = mix(base, np.broadcast_to(col(140, 138, 132), (N, N, 3)), light * 0.7)
    stains = np.clip((fbm(1.8, 14) - 0.66) * 3, 0, 1)
    base *= (1 - 0.18 * stains)[..., None]
    save('T_Asphalt_D', base)
    save('T_Asphalt_N', normal_map(grain * 1.5 + light, 1.5), srgb=False)
    save('T_Asphalt_R', np.repeat((0.86 + 0.1 * grain - 0.25 * stains)[..., None], 3, axis=2), srgb=False)


def gravel():
    h, ids = stamps(7800, 7, 15, 21)
    fine = band(22, 200, 512)
    tone = mix(np.broadcast_to(col(150, 142, 128), (N, N, 3)), np.broadcast_to(col(112, 108, 102), (N, N, 3)), ids)
    tone = mix(tone, np.broadcast_to(col(176, 160, 134), (N, N, 3)), np.clip(ids * 2 - 1.2, 0, 1))
    gaps = np.clip(1 - h / 3.0, 0, 1)
    base = mix(tone, np.broadcast_to(col(96, 90, 80), (N, N, 3)), gaps * 0.6) * (0.9 + 0.2 * fine)[..., None]
    save('T_Gravel_D', base)
    save('T_Gravel_N', normal_map(h / 6 + fine * 0.5, 1.8), srgb=False)
    save('T_Gravel_R', np.repeat((0.78 + 0.15 * gaps)[..., None], 3, axis=2), srgb=False)


def sand():
    low = fbm(2.0, 31)
    grain = band(32, 260, 512)
    ripple = band(33, 8, 24)
    base = np.broadcast_to(col(196, 176, 136), (N, N, 3)) * (0.88 + 0.22 * low)[..., None] * (0.86 + 0.28 * grain)[..., None]
    save('T_Sand_D', base)
    save('T_Sand_N', normal_map(grain + ripple * 3, 1.4), srgb=False)
    save('T_Sand_R', np.repeat((0.9 + 0.08 * grain)[..., None], 3, axis=2), srgb=False)


def save_rgba(name, rgba):
    h, w = rgba.shape[:2]
    img = bpy.data.images.new(name, w, h, alpha=True, float_buffer=False)
    img.colorspace_settings.name = 'sRGB'
    img.alpha_mode = 'STRAIGHT'
    img.pixels.foreach_set(np.clip(rgba, 0, 1)[::-1].astype(np.float32).ravel())
    img.filepath_raw = os.path.join(OUT, name + '.png')
    img.file_format = 'PNG'
    img.save()
    print('HK: wrote', name)


def leaves(name, seed, tones, count=150, size=512):
    """A cluster of leaves on a transparent card (like the web's leafCardTexture, denser and shaded), with a normal map."""
    rng = np.random.default_rng(seed)
    S = size
    rgba = np.zeros((S, S, 4))
    height = np.zeros((S, S))
    ys, xs = np.mgrid[0:S, 0:S].astype(np.float64)
    # Twigs first.
    for k in range(7):
        x0, y0 = S / 2 + rng.normal(0, 20), S * .97
        x1, y1 = S / 2 + rng.normal(0, S * .28), S * rng.uniform(.15, .5)
        t = np.clip(((xs - x0) * (x1 - x0) + (ys - y0) * (y1 - y0)) / ((x1 - x0) ** 2 + (y1 - y0) ** 2), 0, 1)
        d = np.hypot(xs - (x0 + t * (x1 - x0)), ys - (y0 + t * (y1 - y0)))
        m = d < 2.2 - t
        rgba[m] = [0.28, 0.22, 0.16, 1]
    for k in range(count):
        a = rng.uniform(0, 2 * np.pi)
        r = np.sqrt(rng.uniform()) * S * .42
        cx, cy = S / 2 + np.cos(a) * r, S * .47 + np.sin(a) * r * .9
        L, W = rng.uniform(18, 30) * S / 512, rng.uniform(8, 13) * S / 512
        rot = rng.uniform(0, np.pi)
        u = ((xs - cx) * np.cos(rot) + (ys - cy) * np.sin(rot)) / L
        v = (-(xs - cx) * np.sin(rot) + (ys - cy) * np.cos(rot)) / W
        # A pointed leaf: ellipse narrowed toward the tip (u -> 1).
        inside = (u * u + (v / (1 - 0.45 * np.clip(u, 0, 1))) ** 2) < 1
        if not inside.any():
            continue
        tone = np.array(tones[rng.integers(len(tones))]) / 255.0
        shade = 0.82 + 0.3 * (1 - np.abs(v))           # midrib lighter, edges darker
        vein = np.exp(-(v * 4) ** 2) * 0.12
        c = tone[None, :] * (shade[inside][:, None] + vein[inside][:, None]) * rng.uniform(.85, 1.15)
        rgba[inside, :3] = c
        rgba[inside, 3] = 1
        height[inside] = np.maximum(height[inside], (1 - u[inside] ** 2) * (1 - np.abs(v[inside])) * 3 + k * 0.002)
    save_rgba(name + '_D', rgba)
    save(name + '_N', normal_map(height, 1.5), srgb=False)


def bark():
    fib = band(41, 4, 30) * 0.5 + band(42, 30, 120) * 0.5
    # Stretch vertically: fibres run along the trunk.
    fib = np.repeat(fib[::8, :], 8, axis=0)
    cracks = np.clip((band(43, 10, 40) - 0.7) * 5, 0, 1)
    base = np.broadcast_to(col(92, 80, 68), (N, N, 3)) * (0.7 + 0.5 * fib)[..., None] * (1 - 0.45 * cracks)[..., None]
    save('T_Bark_D', base)
    save('T_Bark_N', normal_map(fib * 4 - cracks * 3, 2.0), srgb=False)


os.makedirs(OUT, exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True)
for make in (grass, asphalt, gravel, sand, bark):
    make()
# The web's two crown tone sets and its bush tones (surroundings.js leafMats / bushMat).
leaves('T_LeavesA', 51, [(0x3f, 0x5f, 0x2a), (0x4f, 0x72, 0x32), (0x5d, 0x82, 0x38), (0x36, 0x52, 0x2a), (0x6a, 0x8c, 0x3e)])
leaves('T_LeavesB', 52, [(0x46, 0x62, 0x2c), (0x55, 0x7a, 0x35), (0x3a, 0x55, 0x26), (0x67, 0x8a, 0x3b), (0x4b, 0x6a, 0x2f)])
leaves('T_LeavesBush', 53, [(0x34, 0x50, 0x2a), (0x40, 0x60, 0x2c), (0x2e, 0x47, 0x24), (0x4b, 0x6b, 0x31)], count=190)
