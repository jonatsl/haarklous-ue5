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


CLOUD_HLSL = r'''
float3 d = normalize(Dir);
if (d.z <= 0.0) return float4(0, 0, 0, 0);
// surroundings.js sky shader: uv = d.xz / (d.y + .12) * 1.6; c = smoothstep(.5, .78, fbm(uv + (3.1, 1.7)));
// col = .92 + .08 * fbm(uv * 3); alpha = c * .85 * smoothstep(0, .18, d.y). A slow drift is added.
float2 uv = d.xy / (d.z + 0.12) * 1.6 + float2(Time * 0.004, Time * 0.0015);
float v = 0, a = 0.5; float2 p = uv + float2(3.1, 1.7);
for (int i = 0; i < 5; i++) {
  float2 ip = floor(p), fp = frac(p); fp = fp * fp * (3.0 - 2.0 * fp);
  float n00 = frac(sin(dot(ip, float2(127.1, 311.7))) * 43758.5453);
  float n10 = frac(sin(dot(ip + float2(1, 0), float2(127.1, 311.7))) * 43758.5453);
  float n01 = frac(sin(dot(ip + float2(0, 1), float2(127.1, 311.7))) * 43758.5453);
  float n11 = frac(sin(dot(ip + float2(1, 1), float2(127.1, 311.7))) * 43758.5453);
  v += a * lerp(lerp(n00, n10, fp.x), lerp(n01, n11, fp.x), fp.y); p *= 2.03; a *= 0.5; }
float w = 0; a = 0.5; p = uv * 3.0;
for (int k = 0; k < 4; k++) {
  float2 ip = floor(p), fp = frac(p); fp = fp * fp * (3.0 - 2.0 * fp);
  float n00 = frac(sin(dot(ip, float2(127.1, 311.7))) * 43758.5453);
  float n10 = frac(sin(dot(ip + float2(1, 0), float2(127.1, 311.7))) * 43758.5453);
  float n01 = frac(sin(dot(ip + float2(0, 1), float2(127.1, 311.7))) * 43758.5453);
  float n11 = frac(sin(dot(ip + float2(1, 1), float2(127.1, 311.7))) * 43758.5453);
  w += a * lerp(lerp(n00, n10, fp.x), lerp(n01, n11, fp.x), fp.y); p *= 2.03; a *= 0.5; }
float c = smoothstep(0.5, 0.78, v);
float shade = 0.92 + 0.08 * w;
float3 col = float3(1, 1, 1) * shade * lerp(0.78, 1.0, saturate(dot(d, normalize(SunDir)) * 0.5 + 0.5));
return float4(col, c * 0.85 * smoothstep(0.0, 0.18, d.z));
'''


def clouds():
    '''M_HK_Clouds: the web sky shader's clouds (unlit, translucent) for a dome over the physical sky.'''
    path = f'{DIR}/M_HK_Clouds'
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    m = AT.create_asset('M_HK_Clouds', DIR, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property('two_sided', True)
    n = lambda cls, x, y: MEL.create_material_expression(m, cls, x, y)
    wp = n(unreal.MaterialExpressionWorldPosition, -900, 0)
    cam = n(unreal.MaterialExpressionCameraPositionWS, -900, 150)
    sub = n(unreal.MaterialExpressionSubtract, -700, 50)
    MEL.connect_material_expressions(wp, '', sub, 'A'); MEL.connect_material_expressions(cam, '', sub, 'B')
    t = n(unreal.MaterialExpressionTime, -700, 250)
    sun = n(unreal.MaterialExpressionVectorParameter, -700, 350); sun.set_editor_property('parameter_name', 'SunDir'); sun.set_editor_property('default_value', unreal.LinearColor(0.12, -0.78, 0.62, 0))
    sun3 = n(unreal.MaterialExpressionComponentMask, -550, 350); sun3.set_editor_property('r', True); sun3.set_editor_property('g', True); sun3.set_editor_property('b', True)
    MEL.connect_material_expressions(sun, '', sun3, '')
    c = n(unreal.MaterialExpressionCustom, -400, 100)
    c.set_editor_property('code', CLOUD_HLSL)
    c.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    ins = []
    for name in ('Dir', 'Time', 'SunDir'):
        ci = unreal.CustomInput(); ci.set_editor_property('input_name', name); ins.append(ci)
    c.set_editor_property('inputs', ins)
    MEL.connect_material_expressions(sub, '', c, 'Dir')
    MEL.connect_material_expressions(t, '', c, 'Time')
    MEL.connect_material_expressions(sun3, '', c, 'SunDir')
    rgb = n(unreal.MaterialExpressionComponentMask, -150, 50); rgb.set_editor_property('r', True); rgb.set_editor_property('g', True); rgb.set_editor_property('b', True)
    MEL.connect_material_expressions(c, '', rgb, '')
    alpha = n(unreal.MaterialExpressionComponentMask, -150, 200); alpha.set_editor_property('a', True)
    MEL.connect_material_expressions(c, '', alpha, '')
    bright = n(unreal.MaterialExpressionScalarParameter, -150, 320); bright.set_editor_property('parameter_name', 'Brightness'); bright.set_editor_property('default_value', 2.2)
    em = n(unreal.MaterialExpressionMultiply, 50, 80)
    MEL.connect_material_expressions(rgb, '', em, 'A'); MEL.connect_material_expressions(bright, '', em, 'B')
    MEL.connect_material_property(em, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(alpha, '', unreal.MaterialProperty.MP_OPACITY)
    MEL.recompile_material(m)
    EAL.save_loaded_asset(m)
    log('created', path)


drunk()
clouds()
