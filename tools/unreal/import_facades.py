"""3D facades near home (photo-real pass): materials, import of tools/out/fbx/facades.fbx (tools/blender/
build_facades.py) and their actors in /Game/Maps/Torshov. Run headless with tools/unreal/run-python.ps1 after
materials_photoreal.py; build_level.py calls place() too when it rebuilds the level. Bake the lighting afterwards.

  M_HK_Wall3D    scanned brick or plaster, tinted by the vertex colour (the building's real look, x 2.5),
                 alpha 1 = plaster; uv0 in metres
  M_HK_Glass3D   glass with a room behind it (interior mapping, as in M_HK_Building): uv0 = cell of a 3 x 2.85 m
                 room, uv1 = 0..1 over the window, vertex colour red = this window's random value
  MI_HK_FrameBrown / MI_HK_FrameWhite   painted wooden frames (M_HK_DetailPBR)
"""
import json
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
LES = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
FBX = os.path.join(ROOT, 'tools', 'out', 'fbx')
LIB = '/Game/Haarklous/Textures/Lib'
MAT = '/Game/Haarklous/Materials'
DEST = '/Game/Haarklous/Meshes/facades'
F1, F3 = unreal.CustomMaterialOutputType.CMOT_FLOAT1, unreal.CustomMaterialOutputType.CMOT_FLOAT3


def log(*a):
    print('HK:', *a)


class G:
    def __init__(self, name):
        if EAL.does_asset_exist(f'{MAT}/{name}'):
            EAL.delete_asset(f'{MAT}/{name}')
        self.m = AT.create_asset(name, MAT, unreal.Material, unreal.MaterialFactoryNew())
        self.y = 0

    def n(self, cls):
        self.y += 140
        return MEL.create_material_expression(self.m, cls, -1200, self.y)

    def tex(self, name, path, uv, kind):
        e = self.n(unreal.MaterialExpressionTextureSampleParameter2D)
        e.set_editor_property('parameter_name', name)
        e.set_editor_property('texture', unreal.load_asset(path))
        e.set_editor_property('sampler_type', {'color': unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, 'normal': unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL,
                                               'mask': unreal.MaterialSamplerType.SAMPLERTYPE_MASKS}[kind])
        e.set_editor_property('sampler_source', unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS)
        MEL.connect_material_expressions(uv, '', e, 'UVs')
        return e

    def scalar(self, name, v):
        e = self.n(unreal.MaterialExpressionScalarParameter); e.set_editor_property('parameter_name', name); e.set_editor_property('default_value', v); return e

    def custom(self, code, out, inputs, extra=()):
        c = self.n(unreal.MaterialExpressionCustom)
        c.set_editor_property('code', code); c.set_editor_property('output_type', out)
        ins = []
        for nm, _, _ in inputs:
            ci = unreal.CustomInput(); ci.set_editor_property('input_name', nm); ins.append(ci)
        c.set_editor_property('inputs', ins)
        outs = []
        for nm, ty in extra:
            co = unreal.CustomOutput(); co.set_editor_property('output_name', nm); co.set_editor_property('output_type', ty); outs.append(co)
        c.set_editor_property('additional_outputs', outs)
        for nm, e, pin in inputs:
            MEL.connect_material_expressions(e, pin, c, nm)
        return c

    def finish(self):
        self.m.set_editor_property('used_with_static_lighting', True)
        MEL.recompile_material(self.m)
        EAL.save_loaded_asset(self.m)
        return self.m


WALL_HLSL = r'''
float3 c = lerp(BD, PD, VA) * VC.rgb * Scale;
Rough = lerp(BR, PR, VA);
Nrm = normalize(lerp(BN, PN, VA));
return c;
'''

GLASS_HLSL = r'''
// Room behind the glass, as M_HK_Building draws it: a box 3 m wide, 2.85 m high and 4 m deep. Cell = uv0 (x across,
// y down from the ceiling), W = uv1 over the window, R = this window's random value.
float3 d = -normalize(V);
float dz = max(-d.z, 1e-3);
float3 rd = float3(d.x / 3.0, d.y / 2.85, dz / 4.0);
float2 cell = Cell;
float tx = rd.x > 0.0 ? (1.0 - cell.x) / rd.x : -cell.x / min(rd.x, -1e-4);
float ty = rd.y > 0.0 ? (1.0 - cell.y) / rd.y : -cell.y / min(rd.y, -1e-4);
float tz = 1.0 / rd.z;
float t = min(tx, min(ty, tz));
float3 h = float3(cell, 0.0) + rd * t;
float r1 = R, r2 = frac(r1 * 91.37), r3 = frac(r1 * 47.11), r4 = frac(r1 * 13.7);
float3 room = r1 < 0.35 ? float3(0.80, 0.77, 0.70) : r1 < 0.55 ? float3(0.66, 0.71, 0.76) : r1 < 0.75 ? float3(0.79, 0.74, 0.60) : r1 < 0.9 ? float3(0.67, 0.72, 0.62) : float3(0.80, 0.69, 0.66);
float3 c = room;
if (t == tz)
{
    if (r2 < 0.45 && abs(h.x - 0.25) < 0.1 && h.y > 0.28) c = float3(0.30, 0.22, 0.16);
    if (r3 > 0.5 && abs(h.x - 0.66) < 0.13 && abs(h.y - 0.42) < 0.09) c = float3(0.22, 0.24, 0.28);
    if (r4 > 0.55 && h.y > 0.62 && abs(h.x - 0.5 - 0.2 * (r2 - 0.5)) < 0.22) c = float3(0.30, 0.31, 0.34) * (0.8 + 0.4 * r3);
}
else if (t == ty)
    c = rd.y > 0.0 ? float3(0.40, 0.28, 0.18) * (0.88 + 0.12 * sin(h.z * 90.0)) : float3(0.88, 0.88, 0.86);
else
    c = room * 0.86;
c *= lerp(1.0, 0.38, saturate(h.z)) * (r2 > 0.82 ? 1.25 : 1.0);
float cx = W.x, cy = W.y;
float3 cloth = r1 < 0.4 ? float3(0.86, 0.83, 0.76) : r1 < 0.7 ? float3(0.55, 0.57, 0.58) : float3(0.30, 0.34, 0.45);
float cw = 0.1 + 0.18 * r2;
if (r3 < 0.55 && (cx < cw || cx > 1.0 - cw)) c = cloth * (0.75 + 0.25 * sin(cx * 90.0)) * 0.9;
if (r3 > 0.86 && cy < 0.25 + 0.5 * r4) c = float3(0.84, 0.83, 0.80) * (0.85 + 0.15 * step(0.5, frac(cy * 28.0)));
return c * Bright;
'''


def materials():
    g = G('M_HK_Wall3D')
    tc = g.n(unreal.MaterialExpressionTextureCoordinate)
    bs = g.n(unreal.MaterialExpressionMultiply); MEL.connect_material_expressions(tc, '', bs, 'A'); MEL.connect_material_expressions(g.scalar('BrickScale', 1 / 2.01), '', bs, 'B')
    ps = g.n(unreal.MaterialExpressionMultiply); MEL.connect_material_expressions(tc, '', ps, 'A'); MEL.connect_material_expressions(g.scalar('PlasterScale', 1 / 2.23), '', ps, 'B')
    vc = g.n(unreal.MaterialExpressionVertexColor)
    c = g.custom(WALL_HLSL, F3, [('BD', g.tex('BrickAlbedo', f'{LIB}/T_Brick_D', bs, 'color'), 'RGB'), ('BN', g.tex('BrickNormal', f'{LIB}/T_Brick_N', bs, 'normal'), 'RGB'),
                                 ('BR', g.tex('BrickRough', f'{LIB}/T_Brick_R', bs, 'mask'), 'R'), ('PD', g.tex('PlasterAlbedo', f'{LIB}/T_Plaster_D', ps, 'color'), 'RGB'),
                                 ('PN', g.tex('PlasterNormal', f'{LIB}/T_Plaster_N', ps, 'normal'), 'RGB'), ('PR', g.tex('PlasterRough', f'{LIB}/T_Plaster_R', ps, 'mask'), 'R'),
                                 ('VC', vc, ''), ('VA', vc, 'A'), ('Scale', g.scalar('ColourScale', 2.5), '')], [('Rough', F1), ('Nrm', F3)])
    MEL.connect_material_property(c, '', unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(c, 'Rough', unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.connect_material_property(c, 'Nrm', unreal.MaterialProperty.MP_NORMAL)
    wall = g.finish()

    g = G('M_HK_Glass3D')
    cell = g.n(unreal.MaterialExpressionTextureCoordinate)
    win = g.n(unreal.MaterialExpressionTextureCoordinate); win.set_editor_property('coordinate_index', 1)
    cam = g.n(unreal.MaterialExpressionCameraVectorWS)
    tv = g.n(unreal.MaterialExpressionTransform)
    tv.set_editor_property('transform_source_type', unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD)
    tv.set_editor_property('transform_type', unreal.MaterialVectorCoordTransform.TRANSFORM_TANGENT)
    MEL.connect_material_expressions(cam, '', tv, '')
    vc = g.n(unreal.MaterialExpressionVertexColor)
    c = g.custom(GLASS_HLSL, F3, [('Cell', cell, ''), ('W', win, ''), ('V', tv, ''), ('R', vc, 'R'), ('Bright', g.scalar('InteriorBrightness', 0.09), '')])
    MEL.connect_material_property(c, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    base = g.n(unreal.MaterialExpressionConstant3Vector); base.set_editor_property('constant', unreal.LinearColor(0.015, 0.015, 0.015, 1))
    MEL.connect_material_property(base, '', unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(g.scalar('Roughness', 0.04), '', unreal.MaterialProperty.MP_ROUGHNESS)
    glass = g.finish()

    detail = unreal.load_asset(f'{MAT}/M_HK_DetailPBR')
    frames = {}
    for name, col in (('MI_HK_FrameBrown', (0.16, 0.08, 0.05)), ('MI_HK_FrameWhite', (1.6, 1.6, 1.58))):
        path = f'{MAT}/Details/{name}'
        if not EAL.does_asset_exist(path):
            AT.create_asset(name, f'{MAT}/Details', unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        mi = unreal.load_asset(path)
        MEL.set_material_instance_parent(mi, detail)
        for k, kind in (('Albedo', 'D'), ('Normal', 'N'), ('Rough', 'R')):
            MEL.set_material_instance_texture_parameter_value(mi, k, unreal.load_asset(f'{LIB}/T_Plaster_{kind}'))
        MEL.set_material_instance_vector_parameter_value(mi, 'UVScale', unreal.LinearColor(1 / 2.23, 1 / 2.23, 0, 0))
        MEL.set_material_instance_vector_parameter_value(mi, 'Color', unreal.LinearColor(*col, 1))
        MEL.set_material_instance_scalar_parameter_value(mi, 'RoughScale', 0.6)
        MEL.update_material_instance(mi)
        EAL.save_loaded_asset(mi)
        frames[name] = mi
    return {'MI_HK_Wall3D': wall, 'MI_HK_Glass3D': glass, 'MI_HK_FrameBrown': frames['MI_HK_FrameBrown'],
            'MI_HK_FrameWhite': frames['MI_HK_FrameWhite'], 'MI_HK_DetailConcrete': unreal.load_asset(f'{MAT}/Details/MI_HK_DetailConcrete')}


def import_meshes(mats):
    man = json.load(open(os.path.join(FBX, 'facades.manifest.json'), encoding='utf-8'))
    t = unreal.AssetImportTask()
    t.filename = os.path.join(FBX, 'facades.fbx'); t.destination_path = DEST
    t.automated = True; t.replace_existing = True; t.save = False
    ui = unreal.FbxImportUI()
    ui.set_editor_property('import_mesh', True); ui.set_editor_property('import_as_skeletal', False)
    ui.set_editor_property('import_materials', False); ui.set_editor_property('import_textures', False)
    sd = ui.get_editor_property('static_mesh_import_data')
    sd.set_editor_property('combine_meshes', False)
    sd.set_editor_property('generate_lightmap_u_vs', True)
    sd.set_editor_property('auto_generate_collision', False)
    sd.set_editor_property('vertex_color_import_option', unreal.VertexColorImportOption.REPLACE)
    sd.set_editor_property('normal_import_method', unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS)
    t.options = ui
    AT.import_asset_tasks([t])
    order = ['MI_HK_Wall3D', 'MI_HK_Glass3D', 'MI_HK_FrameBrown', 'MI_HK_FrameWhite', 'MI_HK_DetailConcrete']
    n = 0
    for a in man['assets']:
        sm = unreal.load_asset(f"{DEST}/{a['asset']}")
        if not sm:
            log('missing facade mesh', a['asset']); continue
        for i, sl in enumerate(sm.get_editor_property('static_materials')):
            key = str(sl.get_editor_property('material_slot_name'))
            sm.set_material(i, mats.get(key, mats[order[min(i, 4)]]))
        sm.set_editor_property('light_map_resolution', 512)
        EAL.save_loaded_asset(sm)
        n += 1
    log(f'{n} 3D facade meshes imported')
    return man


def place(man=None):
    """Static actors for the facade tiles at the origin (the meshes are in world coordinates), tag HKGen."""
    man = man or json.load(open(os.path.join(FBX, 'facades.manifest.json'), encoding='utf-8'))
    for a in EAS.get_all_level_actors():
        if a.get_actor_label().startswith('SM_Facade3D_'):
            EAS.destroy_actor(a)
    n = 0
    for a in man['assets']:
        sm = unreal.load_asset(f"{DEST}/{a['asset']}")
        if not sm:
            continue
        act = EAS.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, 0))
        act.set_actor_label(a['asset'])
        act.tags = [unreal.Name('HKGen')]
        act.set_folder_path(unreal.Name('Generated/facades'))
        comp = act.static_mesh_component
        comp.set_mobility(unreal.ComponentMobility.STATIC)
        comp.set_static_mesh(sm)
        n += 1
    log(f'{n} 3D facade actors placed')


if __name__ == '__main__':
    if '--materials-only' in os.environ.get('HK_ARGS', ''):
        materials()
        sys.exit(0)
    m = import_meshes(materials())
    LES.load_level('/Game/Maps/Torshov')
    place(m)
    LES.save_current_level()
