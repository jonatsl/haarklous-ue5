"""Extra materials made by script (run headless with tools/unreal/run-python.ps1):

M_HK_Drunk  - post-process material for the drunk effect, the web's CSS filter blur(dl * 2.2 px) saturate(1 + dl * .3)
              plus the drifting "ghost" double image drawn at alpha .35 * dl (game-main.js renderFrame).
              Parameters: Level (0..1), BlurPx, GhostOffset (x, y in pixels).
"""
import unreal

AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
DIR = '/Game/Haarklous/Materials'


def log(*a):
    print('HK:', *a)


DRUNK_HLSL = r'''
// game-main.js renderFrame(): CSS blur(dl * 2.2 px) saturate(1 + dl * .3) on the canvas, plus a "ghost" copy of the
// frame drawn at alpha .35 * dl with a drifting offset. PostProcessInput0 (index 14) is the tonemapped frame.
float2 uv = GetDefaultSceneTextureUV(Parameters, 14);
float2 px = View.BufferSizeAndInvSize.zw;
float3 c = SceneTextureLookup(uv, 14, false).rgb;
c += SceneTextureLookup(uv + float2(Blur, Blur) * px, 14, false).rgb;
c += SceneTextureLookup(uv + float2(-Blur, Blur) * px, 14, false).rgb;
c += SceneTextureLookup(uv + float2(Blur, -Blur) * px, 14, false).rgb;
c += SceneTextureLookup(uv + float2(-Blur, -Blur) * px, 14, false).rgb;
c *= 0.2;
float3 g = SceneTextureLookup(uv + Ghost.xy * px, 14, false).rgb;
c = lerp(c, g, 0.35 * Level);
float l = dot(c, float3(0.2126, 0.7152, 0.0722));
return l + (c - l) * (1.0 + 0.3 * Level);
'''


def drunk():
    path = f'{DIR}/M_HK_Drunk'
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    m = AT.create_asset('M_HK_Drunk', DIR, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property('material_domain', unreal.MaterialDomain.MD_POST_PROCESS)
    m.set_editor_property('blendable_location', unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_TONEMAPPING)
    n = lambda cls, x, y: MEL.create_material_expression(m, cls, x, y)

    def sp(name, v, x, y):
        e = n(unreal.MaterialExpressionScalarParameter, x, y); e.set_editor_property('parameter_name', name); e.set_editor_property('default_value', v); return e

    level = sp('Level', 0.0, -800, 0)
    blur = sp('BlurPx', 0.0, -800, 120)
    ghost = n(unreal.MaterialExpressionVectorParameter, -800, 240); ghost.set_editor_property('parameter_name', 'GhostOffset'); ghost.set_editor_property('default_value', unreal.LinearColor(0, 0, 0, 0))
    # A SceneTexture node so PostProcessInput0 is bound for SceneTextureLookup in the custom code.
    st = n(unreal.MaterialExpressionSceneTexture, -800, 400)
    st.set_editor_property('scene_texture_id', unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)
    c = n(unreal.MaterialExpressionCustom, -400, 100)
    c.set_editor_property('code', DRUNK_HLSL)
    c.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    ins = []
    for name in ('Level', 'Blur', 'Ghost', 'Scene'):
        ci = unreal.CustomInput(); ci.set_editor_property('input_name', name); ins.append(ci)
    c.set_editor_property('inputs', ins)
    MEL.connect_material_expressions(level, '', c, 'Level')
    MEL.connect_material_expressions(blur, '', c, 'Blur')
    MEL.connect_material_expressions(ghost, '', c, 'Ghost')
    MEL.connect_material_expressions(st, 'Color', c, 'Scene')
    MEL.connect_material_property(c, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.recompile_material(m)
    EAL.save_loaded_asset(m)
    log('created', path)


def clouds():
    """M_HK_Clouds: the web sky's clouds (seamless texture from tools/blender/make_textures.py) projected by view
    direction like the web shader (uv = d.xz / (d.y + .12)), unlit and translucent, on a dome over the physical sky."""
    import os
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
    task = unreal.AssetImportTask()
    task.filename = os.path.join(root, 'tools', 'out', 'textures', 'T_Clouds.png'); task.destination_path = '/Game/Haarklous/Textures/Sky'
    task.destination_name = 'T_Clouds'; task.automated = True; task.replace_existing = True; task.save = True
    AT.import_asset_tasks([task])
    tex = unreal.load_asset('/Game/Haarklous/Textures/Sky/T_Clouds')
    path = f'{DIR}/M_HK_Clouds'
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    m = AT.create_asset('M_HK_Clouds', DIR, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property('two_sided', True)
    # The web drew its sky without fog (fog: false); with fogging the 30 km dome takes the sky atmosphere's aerial
    # perspective and the clouds vanish into the sky colour.
    m.set_editor_property('use_translucency_vertex_fog', False)
    n = lambda cls, x, y: MEL.create_material_expression(m, cls, x, y)
    con = MEL.connect_material_expressions

    def op(cls, a, b, x, y, ai='', bi=''):
        e = n(cls, x, y); con(a, ai, e, 'A'); con(b, bi, e, 'B'); return e

    wp = n(unreal.MaterialExpressionWorldPosition, -1400, 0)
    cam = n(unreal.MaterialExpressionCameraPositionWS, -1400, 150)
    d = n(unreal.MaterialExpressionNormalize, -1000, 50)
    con(op(unreal.MaterialExpressionSubtract, wp, cam, -1200, 50), '', d, '')

    def custom(code, out, x, y):
        e = n(unreal.MaterialExpressionCustom, x, y)
        e.set_editor_property('code', code); e.set_editor_property('output_type', out)
        ci = unreal.CustomInput(); ci.set_editor_property('input_name', 'D'); e.set_editor_property('inputs', [ci])
        con(d, '', e, 'D')
        return e

    # Unreal z is the web's y: uv = d.xz / (d.y + .12) and the fade smoothstep-ish(0, .18, d.y) of the web shader.
    uv0 = custom('return D.xy / (D.z + 0.12);', unreal.CustomMaterialOutputType.CMOT_FLOAT2, -700, 50)
    scale = n(unreal.MaterialExpressionScalarParameter, -650, 350); scale.set_editor_property('parameter_name', 'CloudScale'); scale.set_editor_property('default_value', 0.35)
    uv1 = op(unreal.MaterialExpressionMultiply, uv0, scale, -350, 50)
    pan = n(unreal.MaterialExpressionPanner, -200, 50); pan.set_editor_property('speed_x', 0.0008); pan.set_editor_property('speed_y', 0.0003)
    con(uv1, '', pan, 'Coordinate')
    ts = n(unreal.MaterialExpressionTextureSampleParameter2D, 0, 50); ts.set_editor_property('parameter_name', 'Clouds'); ts.set_editor_property('texture', tex)
    con(pan, '', ts, 'UVs')
    # alpha = cloud alpha * smoothstep-ish(0, .18, d.y) (the web fades clouds out toward the horizon)
    fade = custom('return saturate(D.z / 0.18);', unreal.CustomMaterialOutputType.CMOT_FLOAT1, -500, 300)
    alpha = op(unreal.MaterialExpressionMultiply, ts, fade, 250, 200, 'A', '')
    bright = n(unreal.MaterialExpressionScalarParameter, 0, 300); bright.set_editor_property('parameter_name', 'Brightness'); bright.set_editor_property('default_value', 1.1)
    em = op(unreal.MaterialExpressionMultiply, ts, bright, 250, 50, 'RGB', '')
    MEL.connect_material_property(em, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(alpha, '', unreal.MaterialProperty.MP_OPACITY)
    MEL.recompile_material(m)
    EAL.save_loaded_asset(m)
    log('created', path)


drunk()
clouds()
