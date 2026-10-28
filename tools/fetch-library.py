"""Download the free CC0 photo-scanned textures the realism pass uses (allowed by the user on 27 Oct 2026):

    python tools/fetch-library.py

Sources: Poly Haven (polyhaven.com, public API, CC0) and ambientCG (ambientcg.com, CC0). 2K JPG maps (and one 4K
HDR sky photo), into
tools/out/library/<asset>/ (not in git; the imported Unreal textures are). Files already there are kept.
Credits: docs/REPORT.md "Licences" and README.
"""
import io
import json
import os
import urllib.request
import zipfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
OUT = os.path.join(ROOT, 'tools', 'out', 'library')
UA = {'User-Agent': 'haarklous-ue5-build (local project)'}

# Poly Haven asset -> the maps to fetch (API names). Textures and the texture maps of a few models.
POLYHAVEN = {
    'brick_wall_09': ['Diffuse', 'nor_gl', 'Rough', 'AO', 'Displacement'],          # salmon brick of the home blocks
    'plastered_wall_02': ['Diffuse', 'nor_gl', 'Rough'],                             # plaster, tinted per building
    'concrete_wall_008': ['Diffuse', 'nor_gl', 'Rough'],                             # plinths, balconies, sills
    'asphalt_02': ['Diffuse', 'nor_gl', 'Rough'],                                    # roads
    'asphalt_04': ['Diffuse', 'nor_gl', 'Rough'],
    'grey_roof_01': ['Diffuse', 'nor_gl', 'Rough', 'AO'],                            # dark slate roofs of the blocks
    'clay_roof_tiles_02': ['Diffuse', 'nor_gl', 'Rough', 'AO'],                      # red tile roofs
    'bark_brown_02': ['Diffuse', 'nor_gl', 'Rough'],                                 # tree bark
    'grass_bermuda_01': ['Diffuse', 'Alpha', 'nor_gl', 'Rough', 'AO'],               # real grass blades (model maps)
    'grass_medium_01': ['Diffuse', 'Alpha', 'nor_gl', 'dry_diff'],
    'tree_small_02': ['leaves_diff', 'leaves_alpha', 'leaves_nor_gl'],               # leaf sprigs for the crowns
    # Construction site (allowed by the user on 28 Oct 2026): soil, gravel, machine and container metal.
    'farm_soil': ['Diffuse', 'nor_gl', 'Rough'],
    'excavated_soil_wall': ['Diffuse', 'nor_gl', 'Rough'],
    'gravelly_sand': ['Diffuse', 'nor_gl', 'Rough'],
    'metal_plate': ['Diffuse', 'nor_gl', 'Rough'],
    'corrugated_iron_02': ['Diffuse', 'nor_gl', 'Rough'],
    'rusty_metal_02': ['Diffuse', 'nor_gl', 'Rough'],
}
# Geometry of the two grass models (glTF, 1K textures), to cut the blade cards from their atlases.
POLYHAVEN_GLTF = ['grass_bermuda_01', 'grass_medium_01']
AMBIENTCG = ['Grass001', 'Grass004', 'Ground037']                                    # lawn and worn ground
# The sky photo (allowed by the user on 28 Oct 2026): sun at 38 degrees like the game's sun, partly cloudy.
HDRI = 'kloofendal_38d_partly_cloudy_puresky'


def get(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=900) as r:
        return r.read()


def save(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, 'wb').write(data)


def polyhaven():
    for asset, maps in POLYHAVEN.items():
        files = json.loads(get(f'https://api.polyhaven.com/files/{asset}'))
        for m in maps:
            f = files[m]['2k']['jpg']
            path = os.path.join(OUT, asset, os.path.basename(f['url']))
            if not os.path.exists(path):
                save(path, get(f['url']))
                print('HK: got', os.path.relpath(path, ROOT), round(f['size'] / 1e6, 1), 'MB')
        if asset in POLYHAVEN_GLTF:
            g = files['gltf']['1k']['gltf']
            path = os.path.join(OUT, asset, 'gltf', os.path.basename(g['url']))
            if not os.path.exists(path):
                save(path, get(g['url']))
                for rel, inc in g.get('include', {}).items():
                    save(os.path.join(OUT, asset, 'gltf', rel), get(inc['url']))
                print('HK: got', os.path.relpath(path, ROOT))


def hdri():
    f = json.loads(get(f'https://api.polyhaven.com/files/{HDRI}'))['hdri']['4k']['hdr']
    path = os.path.join(OUT, HDRI, os.path.basename(f['url']))
    if not os.path.exists(path):
        save(path, get(f['url']))
        print('HK: got', os.path.relpath(path, ROOT), round(f['size'] / 1e6, 1), 'MB')


# Realistic people (allowed by the user on 28 Oct 2026): the MakeHuman MPFB add-on for Blender (GPL code; the people it
# makes are CC0) and CC0 asset packs from makehumancommunity.org.
MPFB = 'https://extensions.blender.org/download/sha256:c075b3e9b94b8e5556a5f03f4653602d02ea306de08126e8f55ed6dee28a2dad/add-on-mpfb-v2.0.18.zip?repository=%2Fapi%2Fv1%2Fextensions%2F&blender_version_min=4.2.0'
MH_PACKS = ['makehuman_system_assets', 'skins01', 'skins02', 'shirts01', 'pants01', 'shoes01', 'hats02', 'hair01']


def makehuman():
    folder = os.path.join(OUT, 'makehuman')
    os.makedirs(folder, exist_ok=True)
    path = os.path.join(folder, 'add-on-mpfb-v2.0.18.zip')
    if not os.path.exists(path):
        save(path, get(MPFB))
        print('HK: got MPFB add-on', round(os.path.getsize(path) / 1e6, 1), 'MB')
    for p in MH_PACKS:
        path = os.path.join(folder, f'{p}_cc0.zip')
        if not os.path.exists(path):
            save(path, get(f'https://files2.makehumancommunity.org/asset_packs/{p}/{p}_cc0.zip'))
            print('HK: got', p, round(os.path.getsize(path) / 1e6, 1), 'MB')


def ambientcg():
    for asset in AMBIENTCG:
        folder = os.path.join(OUT, asset)
        if os.path.isdir(folder) and os.listdir(folder):
            continue
        z = get(f'https://ambientcg.com/get?file={asset}_2K-JPG.zip')
        with zipfile.ZipFile(io.BytesIO(z)) as zf:
            for n in zf.namelist():
                if n.lower().endswith(('.jpg', '.png')):
                    save(os.path.join(folder, os.path.basename(n)), zf.read(n))
        print('HK: got', asset, round(len(z) / 1e6, 1), 'MB')


if __name__ == '__main__':
    polyhaven()
    hdri()
    ambientcg()
    makehuman()
    total = sum(os.path.getsize(os.path.join(d, f)) for d, _, fs in os.walk(OUT) for f in fs)
    print(f'HK: library {total / 1e6:.0f} MB in {os.path.relpath(OUT, ROOT)}')
