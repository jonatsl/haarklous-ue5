"""Textures for the real cars (tools/blender/car_model.py, tools/unreal/materials_cars.py):

    python tools/make-car-textures.py

  T_CarPlate.png      eight Norwegian number plates (520 x 110 mm: white, black border and characters, the blue band
                      with the Norwegian flag and N on the left) in a 2 x 4 atlas, 1024 x 256 px each; the cars take
                      one each (AHKRigActor::SetPaint sets the material's Plate index)
  T_CarHeadlight.png  the inside of a headlight under its clear cover: chrome reflector bowls, projector ring, fins
Written to tools/out/textures.
"""
import math
import os

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
OUT = os.path.join(ROOT, 'tools', 'out', 'textures')
FONT = r'C:\Windows\Fonts\bahnschrift.ttf'          # DIN 1451 style, close to the plate lettering
# Electric-car series (EL, EK, EV, EB, EC) are most of Oslo's new cars; a few petrol/diesel series.
PLATES = ['EL 47213', 'EK 20981', 'EV 63402', 'DN 11870', 'EB 30562', 'EL 95301', 'KH 72648', 'EC 48127']
W, H = 1024, 256                                    # one plate cell; the plate is 520:110 = 4.73, the cell 4:1


def plate(text):
    # Draw at the real aspect, then squeeze to the cell (the uvs stretch it back on the 4:1 face of the model... the
    # model's face is 0.52 x 0.11 m and maps the whole cell, so draw at 4.73:1 and resize to 4:1).
    pw, ph = 1211, 256
    im = Image.new('RGB', (pw, ph), (242, 242, 238))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([3, 3, pw - 4, ph - 4], radius=18, outline=(20, 20, 20), width=9)
    band = 120
    d.rounded_rectangle([12, 12, band, ph - 13], radius=10, fill=(0, 51, 153))
    # Norwegian flag: red with a white-edged blue Nordic cross (proportions 22 x 16).
    fx, fy, fw = 30, 34, 72
    fh = fw * 16 // 22
    u = fw / 22.0
    d.rectangle([fx, fy, fx + fw, fy + fh], fill=(186, 12, 47))
    d.rectangle([fx + 6 * u, fy, fx + 10 * u, fy + fh], fill=(255, 255, 255))
    d.rectangle([fx, fy + 6 * u, fx + fw, fy + 10 * u], fill=(255, 255, 255))
    d.rectangle([fx + 7 * u, fy, fx + 9 * u, fy + fh], fill=(0, 32, 91))
    d.rectangle([fx, fy + 7 * u, fx + fw, fy + 9 * u], fill=(0, 32, 91))
    f_n = ImageFont.truetype(FONT, 78)
    try:
        f_n.set_variation_by_name('Bold')
    except Exception:
        pass
    d.text((band / 2 + 6, ph - 62), 'N', font=f_n, fill=(255, 255, 255), anchor='mm')
    f = ImageFont.truetype(FONT, 205)
    try:
        f.set_variation_by_name('SemiBold Condensed')
    except Exception:
        try:
            f.set_variation_by_name('Bold')
        except Exception:
            pass
    d.text((band + (pw - band) / 2, ph / 2 + 6), text, font=f, fill=(18, 18, 18), anchor='mm')
    im = im.filter(ImageFilter.GaussianBlur(0.6))
    return im.resize((W, H), Image.LANCZOS)


def headlight():
    w, h = 512, 128
    im = Image.new('L', (w, h), 120)
    px = im.load()
    for y in range(h):
        for x in range(w):
            u, v = x / w, y / h
            val = 125 + 40 * math.sin(u * 90)                         # fine vertical fins of the inner bezel
            for cu, r in ((0.30, 0.42), (0.62, 0.36)):                # reflector bowls (bright rim, darker middle)
                d = math.hypot((u - cu) * 4.0, (v - 0.5)) / r
                if d < 1:
                    val = 90 + 150 * d ** 2
            d = math.hypot((u - 0.30) * 4.0, (v - 0.5)) / 0.16          # projector lens ring
            if 0.75 < d < 1:
                val = 245
            px[x, y] = max(0, min(255, int(val)))
    return im.filter(ImageFilter.GaussianBlur(1.2)).convert('RGB')


os.makedirs(OUT, exist_ok=True)
atlas = Image.new('RGB', (2 * W, 4 * H))
for i, t in enumerate(PLATES):
    atlas.paste(plate(t), ((i % 2) * W, (i // 2) * H))
atlas.save(os.path.join(OUT, 'T_CarPlate.png'))
headlight().save(os.path.join(OUT, 'T_CarHeadlight.png'))
print('car textures:', ', '.join(PLATES))
