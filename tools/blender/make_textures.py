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
    # Realism pass: a little less saturated, with dry straw patches, darker clover and specks of soil.
    base = mix(np.broadcast_to(col(68, 88, 46), (N, N, 3)), np.broadcast_to(col(96, 114, 60), (N, N, 3)), low)
    base = mix(base, np.broadcast_to(col(134, 128, 78), (N, N, 3)), np.clip((mid - 0.58) * 3, 0, 1) * 0.75)   # dry patches
    clover = smooth((band(6, 30, 80) - 0.66) * 5)
    base = mix(base, np.broadcast_to(col(52, 78, 44), (N, N, 3)), clover * 0.7)
    base *= (0.62 + 0.76 * blades)[..., None] * (0.8 + 0.4 * blades2)[..., None]
    dark = np.clip((fbm(1.6, 5) - 0.7) * 4, 0, 1)
    base *= (1 - 0.25 * dark)[..., None]
    soil = band(7, 200, 512) > 0.86
    base = np.where(soil[..., None], mix(base, np.broadcast_to(col(92, 78, 58), (N, N, 3)), np.full((N, N), 0.7)), base)
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
    # Realism pass: fine cracks (thin contour lines of a low-frequency field) and darker sealed patches.
    field = band(15, 4, 14) + 0.06 * (band(18, 40, 160) - 0.5)        # jagged, not smooth curves
    cracks = np.clip(1 - np.abs(field - 0.5) / 0.0035, 0, 1) * (band(16, 6, 20) > 0.62)
    sealed = smooth((band(17, 3, 8) - 0.72) * 6)
    base = base * (1 - 0.4 * cracks)[..., None] * (1 - 0.2 * sealed)[..., None]
    save('T_Asphalt_D', base)
    save('T_Asphalt_N', normal_map(grain * 1.5 + light - cracks * 2.5, 1.5), srgb=False)
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


def clouds():
    """Sky clouds like the web's sky shader: c = smoothstep(.5, .78, fbm), shade .92 + .08 * fbm(3x); alpha = c * .85."""
    v = fbm(1.9, 61)
    w = fbm(2.3, 62)
    c = np.clip((v - 0.5) / 0.28, 0, 1)
    c = c * c * (3 - 2 * c)
    shade = 0.92 + 0.08 * w
    rgba = np.stack([shade, shade, shade, c * 0.85], axis=-1)
    save_rgba('T_Clouds', rgba)


def smooth(x):
    x = np.clip(x, 0, 1)
    return x * x * (3 - 2 * x)


def brick():
    """Running-bond brick for the facades: one tile = 4 bricks x 14 courses = 0.96 x 0.924 m (240 x 66 mm module,
    10 mm joints), so it repeats seamlessly. Red-brown bricks with per-brick colour, worn edges, a few burnt headers;
    recessed sandy mortar."""
    rng = np.random.default_rng(71)
    y, x = np.mgrid[0:N, 0:N].astype(np.float64)
    ch, bw = N / 14.0, N / 4.0
    course = np.floor(y / ch)
    off = np.where(course % 2 == 1, bw / 2, 0.0)
    xb = (x + off) % N
    bi = np.floor(xb / bw)
    ix, iy = xb - bi * bw, y - course * ch                     # position inside the brick cell (px)
    jw, jh = 8 / 240 * bw, 8 / 66 * ch                        # joint widths (px)
    # Distance to the joint (px): 0 at the mortar edge, growing into the brick face.
    dist = np.minimum.reduce([ix - jw / 2, bw - jw / 2 - ix, iy - jh / 2, ch - jh / 2 - iy])
    face = smooth(dist / 4.0)                                  # rounded arrises over ~4 px
    ids = (bi * 31 + course * 17) % 997
    table = rng.uniform(0, 1, (997, 4))
    r = table[ids.astype(int)]
    palette = np.array([col(160, 74, 56), col(140, 64, 50), col(176, 92, 68), col(124, 58, 46), col(150, 82, 62), col(110, 52, 42)])
    pick = np.minimum((r[..., 0] * len(palette)).astype(int), len(palette) - 1)
    base = palette[pick] * (0.88 + 0.24 * r[..., 1])[..., None]
    fine = band(72, 120, 400)
    blot = fbm(1.7, 73)
    mottle = band(78, 18, 60)                                   # clouding inside each brick
    base = base * (0.8 + 0.4 * fine)[..., None] * (0.9 + 0.2 * blot)[..., None] * (0.78 + 0.44 * mottle)[..., None]
    sand = band(79, 350, 512) > 0.8                             # pale sand grains
    iron = band(80, 300, 512) < 0.12                            # dark iron spots
    base = np.where(sand[..., None], mix(base, np.broadcast_to(col(190, 150, 120), (N, N, 3)), np.full((N, N), 0.5)), base)
    base = np.where(iron[..., None], base * 0.55, base)
    chips = (band(74, 30, 90) > 0.72) & (dist < 7)               # chipped edges show darker clay
    base = np.where(chips[..., None], base * 0.7, base)
    mortar = mix(np.broadcast_to(col(128, 122, 112), (N, N, 3)), np.broadcast_to(col(98, 94, 88), (N, N, 3)), band(75, 40, 200))
    soot = 1 - 0.18 * smooth((fbm(1.5, 76) - 0.55) * 3)          # weathering
    rgb = mix(mortar, base, face) * soot[..., None]
    height = face * (0.85 + 0.15 * fine) - (1 - face) * 0.15 * band(77, 80, 300)
    height = np.where(chips, height * 0.6, height)
    save('T_Brick_D', rgb)
    save('T_Brick_N', normal_map(height * 6, 1.6), srgb=False)
    save('T_Brick_R', np.repeat((0.78 + 0.12 * fine + 0.1 * (1 - face))[..., None], 3, axis=2), srgb=False)


def metal_roof():
    """Standing-seam zinc roofing: 16 seams per tile (like the web's roof texture), panels a little uneven, streaks
    running down the slope, a few darker stains."""
    y, x = np.mgrid[0:N, 0:N].astype(np.float64)
    pw = N / 16.0
    px = x % pw
    seam = np.exp(-((px - pw / 2) / 2.2) ** 2)                  # raised seam in the middle of each panel edge
    panel = np.floor(x / pw)
    rng = np.random.default_rng(81)
    tone = rng.uniform(0.92, 1.08, 16)[panel.astype(int)]
    s = band(82, 6, 90)
    streak = sum(np.roll(s, k, axis=0) for k in range(0, 96, 3)) / 32.0   # smeared along the slope (wrapping)
    streak = (streak - streak.min()) / (streak.max() - streak.min())
    stains = smooth((fbm(1.6, 83) - 0.62) * 3)
    base = np.broadcast_to(col(132, 142, 140), (N, N, 3)) * tone[..., None] * (0.9 + 0.16 * streak)[..., None]
    base = base * (1 - 0.25 * stains)[..., None] * (1 + 0.12 * seam)[..., None]
    save('T_RoofMetal_D', base)
    save('T_RoofMetal_N', normal_map(seam * 6 + streak * 0.6 + band(84, 4, 12) * 1.5, 1.2), srgb=False)
    save('T_RoofMetal_R', np.repeat((0.45 + 0.25 * streak + 0.2 * stains)[..., None], 3, axis=2), srgb=False)


def plaster():
    """Rendered wall (stucco): fine sand grain and gentle trowel waves, near white so the web's colour tints it."""
    grain = band(91, 150, 512)
    waves = fbm(2.4, 92)
    dirt = smooth((fbm(1.6, 93) - 0.6) * 2.5)
    shade = (0.95 + 0.05 * grain) * (0.985 + 0.03 * waves) * (1 - 0.03 * dirt)   # clean paint, fine grain
    save('T_Plaster_D', np.repeat(shade[..., None], 3, axis=2))
    save('T_Plaster_N', normal_map(grain * 1.2 + waves * 3, 1.0), srgb=False)
    save('T_Plaster_R', np.repeat((0.86 + 0.1 * grain)[..., None], 3, axis=2), srgb=False)


def concrete():
    """Cast concrete for plinths and sills: pores, aggregate speckle and water stains."""
    h, _ = stamps(900, 1.5, 4, 101)
    pores = np.clip(h / 2.4, 0, 1)
    speck = band(102, 200, 512)
    stain = smooth((fbm(1.7, 103) - 0.55) * 2.5)
    shade = (0.9 + 0.12 * speck) * (1 - 0.12 * stain) * (1 - 0.35 * pores)
    save('T_Concrete_D', np.repeat(shade[..., None], 3, axis=2) * col(214, 210, 202))
    save('T_Concrete_N', normal_map(speck * 1.5 - pores * 4 + fbm(2.2, 104) * 2, 1.0), srgb=False)
    save('T_Concrete_R', np.repeat((0.8 + 0.12 * speck - 0.15 * stain)[..., None], 3, axis=2), srgb=False)


os.makedirs(OUT, exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True)
for make in (grass, asphalt, gravel, sand, bark, clouds, brick, metal_roof, plaster, concrete):
    make()
# The web's two crown tone sets and its bush tones (surroundings.js leafMats / bushMat).
leaves('T_LeavesA', 51, [(0x3f, 0x5f, 0x2a), (0x4f, 0x72, 0x32), (0x5d, 0x82, 0x38), (0x36, 0x52, 0x2a), (0x6a, 0x8c, 0x3e)])
leaves('T_LeavesB', 52, [(0x46, 0x62, 0x2c), (0x55, 0x7a, 0x35), (0x3a, 0x55, 0x26), (0x67, 0x8a, 0x3b), (0x4b, 0x6a, 0x2f)])
leaves('T_LeavesBush', 53, [(0x34, 0x50, 0x2a), (0x40, 0x60, 0x2c), (0x2e, 0x47, 0x24), (0x4b, 0x6b, 0x31)], count=190)
