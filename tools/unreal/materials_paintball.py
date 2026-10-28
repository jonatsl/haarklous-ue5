"""Materials and meshes of the paintball course (new feature, 28 Oct 2026); after tools/unreal/materials_site.py.

  python tools/make-signs.py
  powershell -File tools\\unreal\\run-python.ps1 tools\\unreal\\materials_paintball.py

MI_PB_* instances of the site's parent materials (M_HK_SitePaint, M_HK_SitePBR, M_HK_SiteGlow, M_HK_SiteGlass,
M_HK_SiteMesh, M_HK_SiteImage) in /Game/Haarklous/Materials/Site, plus M_HK_Splat (masked paint splat). Imports the
course rig (tools/out/fbx/paintball.fbx -> /Game/Haarklous/Meshes/paintball) and the parts
(tools/out/fbx/paintball_parts.fbx -> /Game/Haarklous/Meshes/paintball_parts).
"""
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from materials_site import G, mi, import_fbx, tex, TDIR, TEX, FBX, MDIR  # noqa: E402

EAL = unreal.EditorAssetLibrary


def log(*a):
    print('HK:', *a)


def lin(r, g, b):
    return tuple(((c / 255.0) / 12.92 if c / 255.0 <= .04045 else ((c / 255.0 + .055) / 1.055) ** 2.4) for c in (r, g, b))


def splat_material():
    # The splat quad is 0.18 m with box-projected uvs in metres (V flipped by the FBX import): UVScale / UVOffset map it
    # onto the whole texture. Drawn with instanced meshes.
    g = G('M_HK_Splat')
    g.m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
    def rg(node):
        m = g.n(unreal.MaterialExpressionComponentMask); m.set_editor_property('r', True); m.set_editor_property('g', True)
        g.c(node, '', m, '')
        return m
    tc = g.n(unreal.MaterialExpressionTextureCoordinate)
    uv = g.n(unreal.MaterialExpressionAdd)
    g.c(g.mul(tc, rg(g.vector('UVScale', (1 / .18, -1 / .18, 0, 0))), '', ''), '', uv, 'A'); g.c(rg(g.vector('UVOffset', (.5, 1.09 / .18, 0, 0))), '', uv, 'B')
    t = g.tex('Splat', f'{TDIR}/T_PBSplat', uv, 'color')
    g.out(g.mul(t, g.vector('Color', (1, .5, 0, 1)), 'RGB', ''), unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(t, unreal.MaterialProperty.MP_OPACITY_MASK, 'A')
    g.out(g.scalar('Roughness', .3), unreal.MaterialProperty.MP_ROUGHNESS)       # wet paint
    return g.finish(True)


def main():
    tex(os.path.join(TEX, 'T_PBBoard.png'), 'T_PBBoard', 'D')
    tex(os.path.join(TEX, 'T_PBSign.png'), 'T_PBSign', 'D')
    tex(os.path.join(TEX, 'T_PBSplat.png'), 'T_PBSplat', 'D')
    load = lambda n: unreal.load_asset(f'{MDIR}/{n}')
    paint, pbr, glow, glass, mesh, image = (load(n) for n in ('M_HK_SitePaint', 'M_HK_SitePBR', 'M_HK_SiteGlow', 'M_HK_SiteGlass',
                                                              'M_HK_SiteMesh', 'M_HK_SiteImage'))
    splat = splat_material()
    blue, red = lin(30, 95, 215), lin(205, 45, 30)
    m = {}
    m['MI_PB_Asphalt'] = mi('MI_PB_Asphalt', pbr, {'Color': (.95, .95, .95, 1), 'UVScale': (.3, .3, 0, 0)}, {'RoughScale': 1.05},
                            {'Albedo': 'T_Asphalt_D', 'Normal': 'T_Asphalt_N', 'Rough': 'T_Asphalt_R'})
    m['MI_PB_Plaster'] = mi('MI_PB_Plaster', pbr, {'Color': (1.1, 1.05, .98, 1), 'UVScale': (.5, .5, 0, 0)}, {'Grime': .5},
                            {'Albedo': 'T_Plaster_D', 'Normal': 'T_Plaster_N', 'Rough': 'T_Plaster_R'})
    m['MI_PB_Line'] = mi('MI_PB_Line', paint, {'Color': (*lin(225, 225, 220), 1)}, {'Grime': .4, 'Roughness': .7})
    m['MI_PB_Blue'] = mi('MI_PB_Blue', paint, {'Color': (*blue, 1)}, {'Grime': .2, 'Roughness': .6})
    m['MI_PB_Red'] = mi('MI_PB_Red', paint, {'Color': (*red, 1)}, {'Grime': .2, 'Roughness': .6})
    m['MI_PB_Sandbag'] = mi('MI_PB_Sandbag', paint, {'Color': (*lin(140, 122, 88), 1)}, {'Grime': .6, 'Roughness': .95})
    m['MI_PB_Barrel'] = mi('MI_PB_Barrel', paint, {'Color': (*lin(35, 70, 140), 1)}, {'Grime': .7, 'Roughness': .55})
    m['MI_PB_ContainerRed'] = mi('MI_PB_ContainerRed', pbr, {'Color': (1.1, .38, .26, 1), 'UVScale': (.5, .5, 0, 0)}, {'Metallic': .3},
                                 {'Albedo': 'T_Corrugated_D', 'Normal': 'T_Corrugated_N', 'Rough': 'T_Corrugated_R'})
    m['MI_PB_ContainerGreen'] = mi('MI_PB_ContainerGreen', pbr, {'Color': (.38, .75, .45, 1), 'UVScale': (.5, .5, 0, 0)}, {'Metallic': .3},
                                   {'Albedo': 'T_Corrugated_D', 'Normal': 'T_Corrugated_N', 'Rough': 'T_Corrugated_R'})
    m['MI_PB_Net'] = mi('MI_PB_Net', mesh)
    m['MI_PB_Marker'] = mi('MI_PB_Marker', paint, {'Color': (*lin(26, 26, 30), 1)}, {'Grime': 0.0, 'Roughness': .6})
    m['MI_PB_Hopper'] = mi('MI_PB_Hopper', paint, {'Color': (*lin(15, 15, 18), 1)}, {'Grime': 0.0, 'Roughness': .5})
    m['MI_PB_Tank'] = mi('MI_PB_Tank', paint, {'Color': (*lin(90, 92, 98), 1)}, {'Grime': 0.0, 'Roughness': .3})
    m['MI_PB_Lens'] = mi('MI_PB_Lens', glass)
    m['MI_PB_Ball_Blue'] = mi('MI_PB_Ball_Blue', glow, {'Color': (.15, .55, 1.0, 1)}, {'Glow': .8})
    m['MI_PB_Ball_Red'] = mi('MI_PB_Ball_Red', glow, {'Color': (1.0, .35, .05, 1)}, {'Glow': .8})
    m['MI_PB_Splat_Blue'] = mi('MI_PB_Splat_Blue', splat, {'Color': (.08, .35, .95, 1)})
    m['MI_PB_Splat_Red'] = mi('MI_PB_Splat_Red', splat, {'Color': (.95, .3, .03, 1)})
    m['MI_PB_Board'] = mi('MI_PB_Board', image, {'UVScale': (1 / 2.6, 1 / 1.4, 0, 0), 'UVOffset': (.5, 1.45 / 1.4, 0, 0)}, {}, {'Image': 'T_PBBoard'})
    m['MI_PB_Sign'] = mi('MI_PB_Sign', image, {'UVScale': (1 / .8, 1 / .6, 0, 0), 'UVOffset': (.5, .95 / .6, 0, 0)}, {}, {'Image': 'T_PBSign'})
    for n in ('MI_Site_Concrete', 'MI_Site_Brick', 'MI_Site_Steel', 'MI_Site_Wood', 'MI_Site_Rubber', 'MI_Site_Galv', 'MI_Site_Dark',
              'MI_Site_Container', 'MI_Site_Container2'):
        m[n] = unreal.load_asset(f'/Game/Haarklous/Materials/Site/{n}')
    log('paintball', import_fbx(os.path.join(FBX, 'paintball.fbx'), '/Game/Haarklous/Meshes/paintball', m), 'meshes')
    log('paintball parts', import_fbx(os.path.join(FBX, 'paintball_parts.fbx'), '/Game/Haarklous/Meshes/paintball_parts', m), 'meshes')


main()
