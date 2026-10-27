"""Ground material for the terrain (run headless with tools/unreal/run-python.ps1, after import_assets.py).

The web painted its terrain from the map (one colour per surface). Here the same map data drives a real material:
a surface mask (tools/web-export/exporter-ground.js: R asphalt, G gravel, B sand) blends tiling grass, asphalt, gravel
and sand textures (tools/blender/make_textures.py) at world scale, and the web's own map painting tints the grass
(woods darker, pitches greener). Then every terrain slot of the city tiles gets the new material.
"""
import json
import os

import unreal

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
TEX = os.path.join(ROOT, 'tools', 'out', 'textures')
EXPORT = os.path.join(ROOT, 'tools', 'out', 'web-export')
AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
MDIR = '/Game/Haarklous/Materials'
TDIR = '/Game/Haarklous/Textures/Ground'
LAYERS = [('Grass', 300.0), ('Asphalt', 400.0), ('Gravel', 160.0), ('Sand', 200.0)]


def log(*a):
    print('HK:', *a)


def import_tex(src, name, kind):
    task = unreal.AssetImportTask()
    task.filename = src; task.destination_path = TDIR; task.destination_name = name
    task.automated = True; task.replace_existing = True; task.save = False
    AT.import_asset_tasks([task])
    t = unreal.load_asset(f'{TDIR}/{name}')
    if kind == 'N':
        t.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_NORMALMAP); t.set_editor_property('srgb', False)
    elif kind in ('R', 'M'):
        t.set_editor_property('srgb', False); t.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_MASKS if kind == 'R' else unreal.TextureCompressionSettings.TC_DEFAULT)
    t.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_WORLD)
    EAL.save_loaded_asset(t)
    return t


def master():
    path = f'{MDIR}/M_HK_Ground'
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    m = AT.create_asset('M_HK_Ground', MDIR, unreal.Material, unreal.MaterialFactoryNew())
    n = lambda cls, x, y: MEL.create_material_expression(m, cls, x, y)
    con = MEL.connect_material_expressions

    def op(cls, a, b, x, y, ai='', bi=''):
        e = n(cls, x, y); con(a, ai, e, 'A'); con(b, bi, e, 'B'); return e

    def tparam(name, x, y, sampler, default):
        e = n(unreal.MaterialExpressionTextureSampleParameter2D, x, y)
        e.set_editor_property('parameter_name', name); e.set_editor_property('sampler_type', sampler)
        e.set_editor_property('texture', unreal.load_asset(default)); return e

    def lerp(a, b, t, x, y, ti=''):
        e = n(unreal.MaterialExpressionLinearInterpolate, x, y); con(a, '', e, 'A'); con(b, '', e, 'B'); con(t, ti, e, 'Alpha'); return e

    wp = n(unreal.MaterialExpressionWorldPosition, -2400, 0)
    wxy = n(unreal.MaterialExpressionComponentMask, -2200, 0); wxy.set_editor_property('r', True); wxy.set_editor_property('g', True); con(wp, '', wxy, '')
    mask = tparam('Mask', -2200, -600, unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR, '/Engine/EngineResources/Black')
    paint = tparam('MapPaint', -2200, -350, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, '/Engine/EngineResources/WhiteSquareTexture')
    white = '/Engine/EngineResources/WhiteSquareTexture'
    layers = {}
    for i, (name, tile) in enumerate(LAYERS):
        y = 300 + i * 700
        t = n(unreal.MaterialExpressionScalarParameter, -2200, y); t.set_editor_property('parameter_name', f'{name}Tile'); t.set_editor_property('default_value', tile)
        uv = op(unreal.MaterialExpressionDivide, wxy, t, -2000, y)
        d = tparam(f'{name}D', -1700, y, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, white)
        nn = tparam(f'{name}N', -1700, y + 220, unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, '/Engine/EngineMaterials/DefaultNormal')
        r = tparam(f'{name}R', -1700, y + 440, unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR, white)
        for e in (d, nn, r):
            con(uv, '', e, 'UVs')
        layers[name] = (d, nn, r)
    # Grass tinted by the web's map painting (relative to its plain grass colour #62783f).
    ref = n(unreal.MaterialExpressionVectorParameter, -1500, -350); ref.set_editor_property('parameter_name', 'GrassRef'); ref.set_editor_property('default_value', unreal.LinearColor(0.122, 0.188, 0.050, 1))
    ref3 = n(unreal.MaterialExpressionComponentMask, -1450, -250); ref3.set_editor_property('r', True); ref3.set_editor_property('g', True); ref3.set_editor_property('b', True); con(ref, '', ref3, '')
    ratio = op(unreal.MaterialExpressionDivide, paint, ref3, -1300, -350, 'RGB', '')
    half = n(unreal.MaterialExpressionConstant, -1300, -200); half.set_editor_property('r', 0.45)
    one = n(unreal.MaterialExpressionConstant, -1300, -120); one.set_editor_property('r', 1.0)
    tint = lerp(one, ratio, half, -1100, -300)
    clampt = n(unreal.MaterialExpressionClamp, -950, -300); con(tint, '', clampt, ''); clampt.set_editor_property('min_default', 0.4); clampt.set_editor_property('max_default', 1.6)
    grass_d = op(unreal.MaterialExpressionMultiply, layers['Grass'][0], clampt, -900, 300, 'RGB', '')
    # Blend: grass -> asphalt (R) -> gravel (G) -> sand (B).
    chans = [('Asphalt', 'R'), ('Gravel', 'G'), ('Sand', 'B')]
    c, nrm, rough = grass_d, layers['Grass'][1], layers['Grass'][2]
    for k, (name, ch) in enumerate(chans):
        d, nn, r = layers[name]
        c = lerp(c, d, mask, -600 + k * 200, 0, ch)
        nrm = lerp(nrm, nn, mask, -600 + k * 200, 400, ch)
        rough = lerp(rough, r, mask, -600 + k * 200, 800, ch)
    MEL.connect_material_property(c, '', unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(nrm, '', unreal.MaterialProperty.MP_NORMAL)
    rr = n(unreal.MaterialExpressionComponentMask, 100, 800); rr.set_editor_property('r', True); con(rough, '', rr, '')
    MEL.connect_material_property(rr, '', unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.recompile_material(m)
    EAL.save_loaded_asset(m)
    return m


def main():
    textures = {}
    for name, _ in LAYERS:
        for kind in ('D', 'N', 'R'):
            textures[f'{name}{kind}'] = import_tex(os.path.join(TEX, f'T_{name}_{kind}.png'), f'T_{name}_{kind}', kind)
    masks = {k: import_tex(os.path.join(EXPORT, 'textures', f'ground_{k}.png'), f'T_GroundMask_{k}', 'M') for k in ('near', 'far')}
    m = master()
    # The web's terrain materials: the one whose map is the 4096 px painting (near) and the 2048 px one (far).
    mats = json.load(open(os.path.join(EXPORT, 'materials.json'), encoding='utf-8'))
    tex = mats['textures']
    keys = json.load(open(os.path.join(ROOT, 'tools', 'out', 'build-plan', 'material-keys.json'), encoding='utf-8'))
    terrain = {}
    for k in keys:
        if k.get('map') is not None and k['type'] == 'MeshStandardMaterial' and tex[k['map']]['w'] in (4096, 2048) and k.get('color') == 0xa6a6a6:
            terrain['near' if tex[k['map']]['w'] == 4096 else 'far'] = (k['key'], k['map'])
    made = {}
    for which, (key, tex_id) in terrain.items():
        path = f'{MDIR}/MI_Ground_{which}'
        if EAL.does_asset_exist(path):
            EAL.delete_asset(path)
        mi = AT.create_asset(f'MI_Ground_{which}', MDIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        MEL.set_material_instance_parent(mi, m)
        MEL.set_material_instance_texture_parameter_value(mi, 'Mask', masks[which])
        paint = unreal.load_asset(f'/Game/Haarklous/Textures/Web/T_Web_{tex_id}')
        if paint:
            MEL.set_material_instance_texture_parameter_value(mi, 'MapPaint', paint)
        for name, _ in LAYERS:
            for kind in ('D', 'N', 'R'):
                MEL.set_material_instance_texture_parameter_value(mi, f'{name}{kind}', textures[f'{name}{kind}'])
        EAL.save_loaded_asset(mi)
        made[key] = mi
        log('ground material', which, 'replaces', key)
    # Assign to every terrain slot of the city tiles.
    count = 0
    for folder in ('/Game/Haarklous/Meshes/world_near', '/Game/Haarklous/Meshes/world_far'):
        for p in EAL.list_assets(folder, recursive=False):
            sm = unreal.load_asset(p)
            if not isinstance(sm, unreal.StaticMesh):
                continue
            changed = False
            for i, sl in enumerate(sm.get_editor_property('static_materials')):
                key = str(sl.get_editor_property('material_slot_name'))   # slot names are the web material keys
                if key in made:
                    sm.set_material(i, made[key]); changed = True
            if changed:
                EAL.save_loaded_asset(sm); count += 1
    log(f'ground material on {count} meshes')


main()
