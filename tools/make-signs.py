"""Sign and decal textures made with PIL (tools/out/textures):
  T_SiteSign, T_SiteBanner   construction site (Norwegian: building site, no entry for unauthorised persons)
  T_PBBoard, T_PBSign        paintball course: the board in the lobby and the goggles sign at the gate
  T_PBSplat                  a paint splat (white, alpha = paint), tinted per team by its material

    python tools/make-signs.py
"""
import math
import os
import random

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
OUT = os.path.join(ROOT, 'tools', 'out', 'textures')
FONT = 'C:/Windows/Fonts/arialbd.ttf'
FONT_R = 'C:/Windows/Fonts/arial.ttf'


def font(size, bold=True):
    return ImageFont.truetype(FONT if bold else FONT_R, size)


def centred(d, y, text, f, fill, w):
    tw = d.textlength(text, font=f)
    d.text(((w - tw) / 2, y), text, font=f, fill=fill)


def site_sign():
    W, H = 1000, 700
    im = Image.new('RGB', (W, H), (250, 200, 20)); d = ImageDraw.Draw(im)
    d.rectangle([12, 12, W - 13, H - 13], outline=(20, 20, 20), width=14)
    centred(d, 70, 'BYGGEPLASS', font(150), (20, 20, 20), W)
    centred(d, 280, 'ADGANG FORBUDT', font(100), (20, 20, 20), W)
    centred(d, 400, 'FOR UVEDKOMMENDE', font(80), (20, 20, 20), W)
    centred(d, 560, 'Hjelm og vernesko påbudt', font(56, False), (20, 20, 20), W)
    im.save(os.path.join(OUT, 'T_SiteSign.png'))
    W, H = 2200, 600
    im = Image.new('RGB', (W, H), (20, 90, 60)); d = ImageDraw.Draw(im)
    centred(d, 90, 'BYGGEPLASS', font(260), (240, 240, 235), W)
    centred(d, 400, 'ADGANG FORBUDT FOR UVEDKOMMENDE', font(110), (250, 200, 20), W)
    im.save(os.path.join(OUT, 'T_SiteBanner.png'))


def pb_board():
    W, H = 1300, 700
    im = Image.new('RGB', (W, H), (22, 24, 28)); d = ImageDraw.Draw(im)
    rnd = random.Random(5)
    for k in range(26):                                  # paint marks on the board
        c = rnd.choice([(30, 110, 230), (220, 60, 30), (240, 200, 30)])
        x, y, r = rnd.randrange(W), rnd.randrange(H), rnd.randrange(8, 26)
        d.ellipse([x - r, y - r, x + r, y + r], fill=c)
    d.rectangle([0, 0, W, 210], fill=(15, 16, 19))
    centred(d, 28, 'HAARKLOUS', font(110), (245, 245, 240), W)
    centred(d, 140, 'PAINTBALL  ·  REBALL', font(52), (250, 200, 20), W)
    rules = ['Maske på hele tiden i spilleområdet', 'Treff = ut: kula sprekker og merker deg',
             'Rekyl og sprett teller ikke (paint)', 'Halvautomatisk, maks 10 kuler i sekundet',
             'Lag-deathmatch  ·  Capture the flag', 'Inntil 6 mot 6']
    f = font(46, False)
    for i, t in enumerate(rules):
        d.ellipse([110, 262 + i * 70, 128, 280 + i * 70], fill=(250, 200, 20))
        d.text((150, 245 + i * 70), t, font=f, fill=(235, 235, 230))
    im.save(os.path.join(OUT, 'T_PBBoard.png'))
    W, H = 800, 600
    im = Image.new('RGB', (W, H), (250, 200, 20)); d = ImageDraw.Draw(im)
    d.rectangle([10, 10, W - 11, H - 11], outline=(20, 20, 20), width=12)
    d.rounded_rectangle([200, 70, 600, 220], radius=60, outline=(20, 20, 20), width=22)     # goggles
    d.rectangle([380, 120, 420, 200], fill=(250, 200, 20))
    d.line([(200, 150), (120, 160)], fill=(20, 20, 20), width=18); d.line([(600, 150), (680, 160)], fill=(20, 20, 20), width=18)
    centred(d, 270, 'MASKE PÅ', font(130), (20, 20, 20), W)
    centred(d, 430, 'I SPILLEOMRÅDET', font(70), (20, 20, 20), W)
    im.save(os.path.join(OUT, 'T_PBSign.png'))


def pb_splat():
    S = 256
    m = Image.new('L', (S, S), 0); d = ImageDraw.Draw(m)
    rnd = random.Random(11)
    d.ellipse([S * .3, S * .3, S * .7, S * .7], fill=255)
    for k in range(14):                                   # lobes and flung drops
        a = rnd.uniform(0, 2 * math.pi); r = rnd.uniform(.12, .42) * S; s = rnd.uniform(.03, .1) * S
        x, y = S / 2 + math.cos(a) * r, S / 2 + math.sin(a) * r
        d.ellipse([x - s, y - s, x + s, y + s], fill=255)
        d.line([(S / 2, S / 2), (x, y)], fill=255, width=int(s * .9))
    m = m.filter(ImageFilter.GaussianBlur(2.5)).point(lambda v: 255 if v > 110 else 0)
    im = Image.new('RGBA', (S, S), (255, 255, 255, 0))
    shade = Image.new('L', (S, S), 255)
    sd = ImageDraw.Draw(shade)
    for k in range(40):                                   # a little variation in the paint
        x, y, r = rnd.randrange(S), rnd.randrange(S), rnd.randrange(4, 16)
        sd.ellipse([x - r, y - r, x + r, y + r], fill=rnd.randrange(205, 255))
    im = Image.merge('RGBA', (shade, shade, shade, m))
    im.save(os.path.join(OUT, 'T_PBSplat.png'))


if __name__ == '__main__':
    os.makedirs(OUT, exist_ok=True)
    site_sign()
    pb_board()
    pb_splat()
    print('HK: sign textures in', OUT)
