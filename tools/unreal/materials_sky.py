"""Photographed sky on the cloud dome (photo-real pass, 28 Oct 2026). Run headless with tools/unreal/run-python.ps1
after tools/make-sky.py; changes the existing level (the dome's material) without re-placing anything.

M_HK_SkyPhoto: unlit "is sky" material (no aerial perspective, no fog), two-sided, the long-lat HDR photo sampled by
view direction and turned so its sun lies on the game's sun (sky.json u0); Exposure matches it to the scene's
fixed exposure, SunCap tames the photo's sun. The engine's sky atmosphere still lights the scene (the dome is kept
out of the sky light and reflection captures); the photo replaces only what you see.
"""
import json
import os

import unreal

AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
LES = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
TEX = os.path.join(ROOT, 'tools', 'out', 'textures')
SKY = json.load(open(os.path.join(TEX, 'sky.json'), encoding='utf-8'))
EXPOSURE = 1.6 / SKY['skyMedian'] * 0.2      # the photo's sky median to about the brightness of the old blue sky

SKY_HLSL = r'''
float3 d = normalize(-C);
float u = atan2(d.y, d.x) / 6.2831853 + U0;
float v = acos(clamp(d.z, -1.0, 1.0)) / 3.1415927;
return float2(frac(u), v);
'''


def log(*a):
    print('HK:', *a)


def texture():
    t = unreal.AssetImportTask()
    t.filename = os.path.join(TEX, 'T_SkyPhoto.exr')
    t.destination_path = '/Game/Haarklous/Textures/Lib'
    t.destination_name = 'T_SkyPhoto'
    t.automated = True; t.replace_existing = True; t.save = False
    AT.import_asset_tasks([t])
    tex = unreal.load_asset('/Game/Haarklous/Textures/Lib/T_SkyPhoto')
    tex.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_HDR_COMPRESSED)
    tex.set_editor_property('mip_gen_settings', unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)   # no seam where u wraps
    tex.set_editor_property('address_x', unreal.TextureAddress.TA_WRAP)
    tex.set_editor_property('address_y', unreal.TextureAddress.TA_CLAMP)
    tex.set_editor_property('never_stream', True)
    EAL.save_loaded_asset(tex)
    return tex


def material(tex):
    path = '/Game/Haarklous/Materials/M_HK_SkyPhoto'
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    m = AT.create_asset('M_HK_SkyPhoto', '/Game/Haarklous/Materials', unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property('two_sided', True)
    m.set_editor_property('is_sky', True)
    n = lambda cls, y: MEL.create_material_expression(m, cls, -800, y)
    cam = n(unreal.MaterialExpressionCameraVectorWS, 0)
    u0 = n(unreal.MaterialExpressionScalarParameter, 150); u0.set_editor_property('parameter_name', 'U0'); u0.set_editor_property('default_value', SKY['u0'])
    c = n(unreal.MaterialExpressionCustom, 300)
    c.set_editor_property('code', SKY_HLSL)
    c.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT2)
    ins = []
    for nm in ('C', 'U0'):
        ci = unreal.CustomInput(); ci.set_editor_property('input_name', nm); ins.append(ci)
    c.set_editor_property('inputs', ins)
    MEL.connect_material_expressions(cam, '', c, 'C')
    MEL.connect_material_expressions(u0, '', c, 'U0')
    s = n(unreal.MaterialExpressionTextureSampleParameter2D, 450)
    s.set_editor_property('parameter_name', 'Sky')
    s.set_editor_property('texture', tex)
    s.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    MEL.connect_material_expressions(c, '', s, 'UVs')
    e = n(unreal.MaterialExpressionScalarParameter, 600); e.set_editor_property('parameter_name', 'Exposure'); e.set_editor_property('default_value', EXPOSURE)
    mul = n(unreal.MaterialExpressionMultiply, 750)
    MEL.connect_material_expressions(s, 'RGB', mul, 'A')
    MEL.connect_material_expressions(e, '', mul, 'B')
    # The photo's sun is ~45,000 times the sky: capped so it blooms like a sun without flooding the lens flares.
    cap = n(unreal.MaterialExpressionScalarParameter, 900); cap.set_editor_property('parameter_name', 'SunCap'); cap.set_editor_property('default_value', 300.0)
    mn = n(unreal.MaterialExpressionMin, 1050)
    MEL.connect_material_expressions(mul, '', mn, 'A')
    MEL.connect_material_expressions(cap, '', mn, 'B')
    MEL.connect_material_property(mn, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.recompile_material(m)
    EAL.save_loaded_asset(m)
    return m


def main():
    m = material(texture())
    LES.load_level('/Game/Maps/Torshov')
    done = 0
    for a in EAS.get_all_level_actors():
        if a.get_actor_label() == 'Cloud dome':
            a.static_mesh_component.set_material(0, m)
            # Seen only: the sky light and reflection captures keep the engine's sky atmosphere, which matches the
            # sun light (capturing the photo's sun would flood the scene with a second sun).
            a.static_mesh_component.set_editor_property('visible_in_reflection_captures', False)
            a.static_mesh_component.set_editor_property('visible_in_real_time_sky_captures', False)
            a.set_actor_hidden_in_game(False)
            done += 1
    for a in EAS.get_all_level_actors():
        if a.get_actor_label() == 'Sky light':
            a.light_component.recapture_sky()
    LES.save_current_level()
    log(f'sky photo on {done} dome(s), exposure {EXPOSURE:.3f}, u0 {SKY["u0"]:.4f}')


main()
