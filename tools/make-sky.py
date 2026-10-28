"""Sky photo for the dome (photo-real pass): the CC0 Poly Haven HDR kloofendal_38d_partly_cloudy_puresky
(tools/fetch-library.py), whose sun stands at 38 degrees like the game's sun.

    python tools/make-sky.py

Writes tools/out/textures/T_SkyPhoto.exr (the long-lat HDR as half floats, imported by materials_sky.py),
T_SkyAmbient.hdr (the same sky with its sun clipped, for the sky light) and
sky.json with the turn that puts the photo's sun at the game's sun azimuth: the material samples
u = atan2(dir.y, dir.x) / 2pi + u0, v = acos(dir.z) / pi with dir the Unreal world direction.
"""
import json
import math
import os

os.environ['OPENCV_IO_ENABLE_OPENEXR'] = '1'
import cv2  # noqa: E402
import numpy as np  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
SRC = os.path.join(ROOT, 'tools', 'out', 'library', 'kloofendal_38d_partly_cloudy_puresky', 'kloofendal_38d_partly_cloudy_puresky_4k.hdr')
OUT = os.path.join(ROOT, 'tools', 'out', 'textures')


def main():
    im = cv2.imread(SRC, cv2.IMREAD_UNCHANGED)                      # BGR float32, 4096 x 2048
    lum = im[..., 2] * 0.2126 + im[..., 1] * 0.7152 + im[..., 0] * 0.0722
    y, x = np.unravel_index(np.argmax(lum), lum.shape)
    h, w = lum.shape
    u_sun, elev = x / w, 90 - y / h * 180
    game = json.load(open(os.path.join(ROOT, 'unreal', 'Haarklous', 'Content', 'Data', 'game.json'), encoding='utf-8'))
    sd = game['meta']['sunDir']                                     # web (x, y, z) toward the sun -> Unreal (x, z, y)
    az = math.atan2(sd[2], sd[0])
    u0 = (u_sun - az / (2 * math.pi)) % 1.0
    os.makedirs(OUT, exist_ok=True)
    cv2.imwrite(os.path.join(OUT, 'T_SkyPhoto.exr'), im.astype(np.float32), [cv2.IMWRITE_EXR_TYPE, cv2.IMWRITE_EXR_TYPE_HALF])
    sky_median = float(np.median(lum[: h // 2]))
    # Ambient light for the sky light: the same photo with the sun clipped (the sun light already does the sun),
    # 1024 x 512, as Radiance HDR (Unreal imports it as a long-lat cube map).
    amb = im.copy()
    cap = sky_median * 12
    scale = np.minimum(1.0, cap / np.maximum(lum, 1e-6))
    amb *= scale[..., None]
    amb = cv2.resize(amb, (1024, 512), interpolation=cv2.INTER_AREA)
    cv2.imwrite(os.path.join(OUT, 'T_SkyAmbient.hdr'), amb.astype(np.float32))
    json.dump({'u0': u0, 'sunElevation': elev, 'skyMedian': sky_median}, open(os.path.join(OUT, 'sky.json'), 'w'), indent=1)
    print(f'HK: sky photo sun at u {u_sun:.3f}, {elev:.1f} deg; game sun azimuth {math.degrees(az):.1f} deg -> u0 {u0:.4f}')


main()
