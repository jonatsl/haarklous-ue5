"""Materials of the real car (tools/blender/car_model.py, the VW Golf Mk7-style body), run headless with
tools/unreal/run-python.ps1 after import_assets.py (tools/build-assets.ps1 step 5 does):

  M_HK_CarPaint   body paint: a metallic base under a clear lacquer coat (ClearCoat 1, ClearCoatRoughness 0.03), so
                  the sun and sky glare off it; metal flake amount from the colour (white paint is solid, the dark and
                  silver ones metallic); road dust on the lowest 30 cm. 'Color' is set per car by AHKRigActor::SetPaint.
  M_HK_CarGlass   tinted glass, lit as a surface (sky and sun reflections), see-through from inside.
  M_HK_CarPart    everything else: colour, metal, roughness, specular, glow and an optional texture (the number
                  plates with their Plate index, the headlight reflectors).
  MI_HK_Car*      one instance per new car material key (car_model.NEW_KEYS), put on the car meshes' slots by name;
                  the web's tail-light key gets a red lens.
The lighter bodies SM_car__body_LOD1 / _LOD2 are switched by distance in the game (AHKRigActor::SetCullDistance):
the commandlet cannot make them the mesh's own LODs (no static mesh editor subsystem there).
"""
import os

import unreal

AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
TEX_IN = os.path.join(ROOT, 'tools', 'out', 'textures')
TEX_DIR = '/Game/Haarklous/Textures/Car'
MAT_DIR = '/Game/Haarklous/Materials'
CAR_DIR = '/Game/Haarklous/Materials/Car'
MESH_DIR = '/Game/Haarklous/Meshes/car'
F1, F3 = unreal.CustomMaterialOutputType.CMOT_FLOAT1, unreal.CustomMaterialOutputType.CMOT_FLOAT3


def log(*a):
    print('HK:', *a)


class Graph:
    def __init__(self, name):
        path = f'{MAT_DIR}/{name}'
        if EAL.does_asset_exist(path):
            EAL.delete_asset(path)
        self.m = AT.create_asset(name, MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
        self.y = 0

    def node(self, cls, x=-1100):
        e = MEL.create_material_expression(self.m, cls, x, self.y)
        self.y += 140
        return e

    def con(self, a, ao, b, bi):
        MEL.connect_material_expressions(a, ao, b, bi)

    def out(self, a, prop, ao=''):
        return MEL.connect_material_property(a, ao, prop)

    def scalar(self, name, v):
        e = self.node(unreal.MaterialExpressionScalarParameter); e.set_editor_property('parameter_name', name); e.set_editor_property('default_value', v); return e

    def vector(self, name, v):
        e = self.node(unreal.MaterialExpressionVectorParameter); e.set_editor_property('parameter_name', name); e.set_editor_property('default_value', unreal.LinearColor(*v)); return e

    def tex(self, name, path, uv=None):
        e = self.node(unreal.MaterialExpressionTextureSampleParameter2D)
        e.set_editor_property('parameter_name', name)
        e.set_editor_property('texture', unreal.load_asset(path))
        if uv is not None:
            self.con(uv, '', e, 'UVs')
        return e

    def custom(self, code, out_type, inputs, outputs=()):
        c = self.node(unreal.MaterialExpressionCustom, -700)
        c.set_editor_property('code', code)
        c.set_editor_property('output_type', out_type)
        ins = []
        for nm, _, _ in inputs:
            ci = unreal.CustomInput(); ci.set_editor_property('input_name', nm); ins.append(ci)
        c.set_editor_property('inputs', ins)
        outs = []
        for nm, ty in outputs:
            co = unreal.CustomOutput(); co.set_editor_property('output_name', nm); co.set_editor_property('output_type', ty); outs.append(co)
        c.set_editor_property('additional_outputs', outs)
        for nm, e, pin in inputs:
            self.con(e, pin, c, nm)
        return c

    def finish(self):
        MEL.recompile_material(self.m)
        EAL.save_loaded_asset(self.m)
        log('created', self.m.get_path_name())
        return self.m


def import_textures():
    tasks = []
    for name in ('T_CarPlate', 'T_CarHeadlight'):
        t = unreal.AssetImportTask()
        t.filename = os.path.join(TEX_IN, name + '.png')
        t.destination_path = TEX_DIR
        t.destination_name = name
        t.automated = True; t.replace_existing = True; t.save = False
        tasks.append(t)
    AT.import_asset_tasks(tasks)
    for name in ('T_CarPlate', 'T_CarHeadlight'):
        t = unreal.load_asset(f'{TEX_DIR}/{name}')
        t.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_VEHICLE)
        EAL.save_loaded_asset(t)


PAINT_HLSL = r'''
// Lum of the paint decides the flake (metal) amount: white and light paint are solid colours, dark and silver ones
// metallic. Road dust (fine grey-brown) on the lowest 30 cm of the body; the body's uv is its side view in metres
// (u along the car, v = height; the FBX import turned v over: height = 1 - v).
float lum = dot(Col, float3(0.2126, 0.7152, 0.0722));
Metal = lerp(0.62, 0.08, smoothstep(0.42, 0.72, lum));
float h = 1.0 - UV.y;
float dust = (1.0 - smoothstep(0.18, 0.48, h)) * 0.35;
Rough = lerp(BaseRough, 0.7, dust);
return lerp(Col, float3(0.24, 0.215, 0.18), dust);
'''


def paint_master():
    g = Graph('M_HK_CarPaint')
    g.m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_CLEAR_COAT)
    uv = g.node(unreal.MaterialExpressionTextureCoordinate)
    c = g.custom(PAINT_HLSL, F3, [('Col', g.vector('Color', (0.5, 0.5, 0.5, 1)), ''), ('BaseRough', g.scalar('Roughness', 0.36), ''), ('UV', uv, '')],
                 [('Metal', F1), ('Rough', F1)])
    # Clear coat and its roughness are not material outputs Python can reach; the material-attributes node has them.
    g.m.set_editor_property('use_material_attributes', True)
    mk = g.node(unreal.MaterialExpressionMakeMaterialAttributes, -350)
    g.con(c, '', mk, 'BaseColor')
    g.con(c, 'Metal', mk, 'Metallic')
    g.con(c, 'Rough', mk, 'Roughness')
    for name, v in (('ClearCoat', 1.0), ('ClearCoatRoughness', 0.03)):
        ok = MEL.connect_material_expressions(g.scalar(name, v), '', mk, name)
        log('clear coat input', name, 'connected' if ok else 'NOT connected')
    g.out(mk, unreal.MaterialProperty.MP_MATERIAL_ATTRIBUTES)
    return g.finish()


def glass_master():
    g = Graph('M_HK_CarGlass')
    g.m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    g.m.set_editor_property('translucency_lighting_mode', unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
    g.out(g.vector('Color', (0.012, 0.016, 0.02, 1)), unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(g.scalar('Opacity', 0.62), unreal.MaterialProperty.MP_OPACITY)
    g.out(g.scalar('Roughness', 0.02), unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(g.scalar('Metallic', 0.0), unreal.MaterialProperty.MP_METALLIC)
    g.out(g.scalar('Specular', 1.0), unreal.MaterialProperty.MP_SPECULAR)
    return g.finish()


PART_HLSL = r'''
// Plate atlas: 2 x 4 cells, Plate picks one (AHKRigActor::SetPaint gives every car its own). Other parts: Cells 1.
float idx = floor(Plate + 0.5);
float2 cell = float2(fmod(idx, Cols), floor(idx / Cols));
return float2((UV.x + cell.x) / Cols, (UV.y + cell.y) / Rows);
'''


def part_master():
    g = Graph('M_HK_CarPart')
    uv = g.node(unreal.MaterialExpressionTextureCoordinate)
    atlas_uv = g.custom(PART_HLSL, unreal.CustomMaterialOutputType.CMOT_FLOAT2,
                        [('UV', uv, ''), ('Plate', g.scalar('Plate', 0.0), ''), ('Cols', g.scalar('Cols', 1.0), ''), ('Rows', g.scalar('Rows', 1.0), '')])
    t = g.tex('BaseMap', '/Engine/EngineResources/WhiteSquareTexture', atlas_uv)
    col = g.node(unreal.MaterialExpressionMultiply); g.con(t, 'RGB', col, 'A'); g.con(g.vector('Color', (1, 1, 1, 1)), '', col, 'B')
    g.out(col, unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(g.scalar('Metallic', 0.0), unreal.MaterialProperty.MP_METALLIC)
    g.out(g.scalar('Roughness', 0.5), unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(g.scalar('Specular', 0.5), unreal.MaterialProperty.MP_SPECULAR)
    g.out(g.vector('Emissive', (0, 0, 0, 1)), unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return g.finish()


# key: (colour (linear), metallic, roughness, specular, emissive, texture, extra scalars)
PARTS = {
    'MI_HK_CarHeadlight': ((0.30, 0.32, 0.34), 1.0, 0.07, 0.5, (0, 0, 0), None, {}),
    'MI_HK_CarDRL': ((1.0, 1.0, 1.0), 0.0, 0.1, 0.5, (5.0, 5.2, 5.6), None, {}),
    'MI_HK_CarTailDark': ((0.05, 0.002, 0.002), 0.0, 0.06, 0.6, (0.02, 0, 0), None, {}),
    'MI_HK_CarAmber': ((0.70, 0.26, 0.02), 0.0, 0.08, 0.6, (0, 0, 0), None, {}),
    'MI_HK_CarGlossBlack': ((0.006, 0.006, 0.007), 0.0, 0.06, 0.6, (0, 0, 0), None, {}),
    'MI_HK_CarRubber': ((0.012, 0.012, 0.013), 0.0, 0.72, 0.3, (0, 0, 0), None, {}),
    'MI_HK_CarGap': ((0.002, 0.002, 0.002), 0.0, 0.95, 0.05, (0, 0, 0), None, {}),
    'MI_HK_CarMirror': ((0.88, 0.9, 0.92), 1.0, 0.02, 0.5, (0, 0, 0), None, {}),
    'MI_HK_CarPlate': ((1.0, 1.0, 1.0), 0.0, 0.38, 0.5, (0, 0, 0), 'T_CarPlate', {'Cols': 2.0, 'Rows': 4.0}),
    'MI_HK_CarGrille': ((0.016, 0.016, 0.017), 0.0, 0.42, 0.4, (0, 0, 0), None, {}),
    'MI_HK_CarTyre': ((0.022, 0.022, 0.023), 0.0, 0.88, 0.35, (0, 0, 0), None, {}),
    'MI_HK_CarRim': ((0.52, 0.54, 0.57), 1.0, 0.26, 0.5, (0, 0, 0), None, {}),
    'MI_HK_CarDisc': ((0.22, 0.22, 0.23), 1.0, 0.48, 0.5, (0, 0, 0), None, {}),
    # The web's tail-light key, now the red lens of the tail lights (lit, as Norwegian cars drive with lights on).
    'MI_Standard_a00000_369': ((0.32, 0.008, 0.008), 0.0, 0.07, 0.7, (0.55, 0.0, 0.0), None, {}),
}


def instances(part):
    out = {}
    for key, (col, metal, rough, spec, emis, tex, extra) in PARTS.items():
        web = key.startswith('MI_Standard_')
        path = f'{MAT_DIR}/Web/{key}' if web else f'{CAR_DIR}/{key}'
        mi = unreal.load_asset(path) if web else None
        if not web:
            if EAL.does_asset_exist(path):
                EAL.delete_asset(path)
            mi = AT.create_asset(key, CAR_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        if not mi:
            log('missing', key); continue
        mi.set_editor_property('parent', part)
        MEL.clear_all_material_instance_parameters(mi)
        mi.set_editor_property('base_property_overrides', unreal.MaterialInstanceBasePropertyOverrides())
        MEL.set_material_instance_vector_parameter_value(mi, 'Color', unreal.LinearColor(*col, 1))
        MEL.set_material_instance_vector_parameter_value(mi, 'Emissive', unreal.LinearColor(*emis, 1))
        for k, v in (('Metallic', metal), ('Roughness', rough), ('Specular', spec), *extra.items()):
            MEL.set_material_instance_scalar_parameter_value(mi, k, v)
        if tex:
            MEL.set_material_instance_texture_parameter_value(mi, 'BaseMap', unreal.load_asset(f'{TEX_DIR}/{tex}'))
        MEL.update_material_instance(mi)
        EAL.save_loaded_asset(mi)
        out[key] = mi
    return out


def reparent_web(name, master, vectors=None, scalars=None):
    mi = unreal.load_asset(f'{MAT_DIR}/Web/{name}')
    if not mi:
        log('missing', name); return
    mi.set_editor_property('parent', master)
    MEL.clear_all_material_instance_parameters(mi)
    mi.set_editor_property('base_property_overrides', unreal.MaterialInstanceBasePropertyOverrides())
    for k, v in (vectors or {}).items():
        MEL.set_material_instance_vector_parameter_value(mi, k, unreal.LinearColor(*v))
    for k, v in (scalars or {}).items():
        MEL.set_material_instance_scalar_parameter_value(mi, k, v)
    MEL.update_material_instance(mi)
    EAL.save_loaded_asset(mi)


def assign(mis):
    n = 0
    for path in EAL.list_assets(MESH_DIR, recursive=False, include_folder=False):
        sm = unreal.load_asset(path)
        if not isinstance(sm, unreal.StaticMesh):
            continue
        changed = False
        for i, sl in enumerate(sm.get_editor_property('static_materials')):
            key = str(sl.get_editor_property('material_slot_name'))
            if key in mis:
                sm.set_material(i, mis[key]); changed = True
        if changed:
            EAL.save_loaded_asset(sm); n += 1
    log(f'car materials on {n} meshes')


import_textures()
paint = paint_master()
glass = glass_master()
part = part_master()
mis = instances(part)
# Body paint: every opaque MI_Physical_* (cars and motorcycles) is under M_HK_CarPaint (materials_realism.py made
# them children of an earlier M_HK_CarPaint, which this script replaced).
n = 0
for path in EAL.list_assets(f'{MAT_DIR}/Web', recursive=False, include_folder=False):
    name = path.split('/')[-1].split('.')[0]
    mi = unreal.load_asset(path)
    if name.startswith('MI_Physical_') and isinstance(mi, unreal.MaterialInstanceConstant):
        p = mi.get_editor_property('parent')
        if p is None or p.get_name() == 'M_HK_CarPaint':
            mi.set_editor_property('parent', paint)
            MEL.update_material_instance(mi)
            EAL.save_loaded_asset(mi)
            n += 1
log(f'{n} paint instances on the new M_HK_CarPaint')
reparent_web('MI_Physical_1d2328_366', glass)       # the car's windows (the web's glassM)
assign(mis)
