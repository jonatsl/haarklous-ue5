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


def drunk():
    path = f'{DIR}/M_HK_Drunk'
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    m = AT.create_asset('M_HK_Drunk', DIR, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property('material_domain', unreal.MaterialDomain.MD_POST_PROCESS)
    m.set_editor_property('blendable_location', unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_TONEMAPPING)
    n = lambda cls, x, y: MEL.create_material_expression(m, cls, x, y)
    con = MEL.connect_material_expressions

    def sp(name, v, x, y):
        e = n(unreal.MaterialExpressionScalarParameter, x, y); e.set_editor_property('parameter_name', name); e.set_editor_property('default_value', v); return e

    def op(cls, a, b, x, y, ai='', bi=''):
        e = n(cls, x, y); con(a, ai, e, 'A'); con(b, bi, e, 'B'); return e

    uv = n(unreal.MaterialExpressionScreenPosition, -1600, 0)
    vs = n(unreal.MaterialExpressionViewSize, -1600, 200)
    one = n(unreal.MaterialExpressionConstant, -1600, 300); one.set_editor_property('r', 1.0)
    texel = op(unreal.MaterialExpressionDivide, one, vs, -1400, 250)
    level = sp('Level', 0.0, -1600, 450)
    blur = sp('BlurPx', 0.0, -1600, 550)
    ghost = n(unreal.MaterialExpressionVectorParameter, -1600, 650); ghost.set_editor_property('parameter_name', 'GhostOffset'); ghost.set_editor_property('default_value', unreal.LinearColor(0, 0, 0, 0))
    step = op(unreal.MaterialExpressionMultiply, texel, blur, -1200, 250)

    def sample(offset_node, x, y, oi=''):
        uvn = op(unreal.MaterialExpressionAdd, uv, offset_node, x - 200, y, 'ViewportUV', oi) if offset_node else None
        st = n(unreal.MaterialExpressionSceneTexture, x, y)
        st.set_editor_property('scene_texture_id', unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)
        con(uvn if uvn else uv, '' if uvn else 'ViewportUV', st, 'UVs')
        return st

    # Five-tap blur: the centre and four diagonal neighbours at BlurPx.
    taps = [sample(None, -800, -400)]
    for k, (sx, sy) in enumerate(((1, 1), (-1, 1), (1, -1), (-1, -1))):
        c = n(unreal.MaterialExpressionConstant2Vector, -1300, 700 + k * 120); c.set_editor_property('r', float(sx)); c.set_editor_property('g', float(sy))
        off = op(unreal.MaterialExpressionMultiply, step, c, -1100, 700 + k * 120)
        taps.append(sample(off, -800, -250 + k * 160))
    s = taps[0]
    for k, t in enumerate(taps[1:]):
        s = op(unreal.MaterialExpressionAdd, s, t, -550, -350 + k * 120, 'Color' if k == 0 else '', 'Color')
    fifth = n(unreal.MaterialExpressionConstant, -550, 250); fifth.set_editor_property('r', 0.2)
    avg = op(unreal.MaterialExpressionMultiply, s, fifth, -350, -200)
    # Ghost: the image shifted by GhostOffset pixels.
    goff = op(unreal.MaterialExpressionMultiply, texel, ghost, -1200, 1300, '', '')
    gmask = n(unreal.MaterialExpressionComponentMask, -1000, 1300); gmask.set_editor_property('r', True); gmask.set_editor_property('g', True); con(goff, '', gmask, '')
    gs = sample(gmask, -800, 600)
    a35 = n(unreal.MaterialExpressionConstant, -600, 800); a35.set_editor_property('r', 0.35)
    galpha = op(unreal.MaterialExpressionMultiply, level, a35, -400, 800)
    mixed = n(unreal.MaterialExpressionLinearInterpolate, -150, -50)
    con(avg, '', mixed, 'A'); con(gs, 'Color', mixed, 'B'); con(galpha, '', mixed, 'Alpha')
    rgb = n(unreal.MaterialExpressionComponentMask, 50, -50); rgb.set_editor_property('r', True); rgb.set_editor_property('g', True); rgb.set_editor_property('b', True); con(mixed, '', rgb, '')
    # Saturation 1 + .3 * Level around the luminance.
    lumw = n(unreal.MaterialExpressionConstant3Vector, 50, 150); lumw.set_editor_property('constant', unreal.LinearColor(0.2126, 0.7152, 0.0722, 1))
    lum = op(unreal.MaterialExpressionDotProduct, rgb, lumw, 250, 100)
    diff = op(unreal.MaterialExpressionSubtract, rgb, lum, 450, 0)
    k3 = n(unreal.MaterialExpressionConstant, 250, 300); k3.set_editor_property('r', 0.3)
    satk = op(unreal.MaterialExpressionMultiply, level, k3, 450, 300)
    one2 = n(unreal.MaterialExpressionConstant, 450, 400); one2.set_editor_property('r', 1.0)
    sat = op(unreal.MaterialExpressionAdd, one2, satk, 650, 350)
    scaled = op(unreal.MaterialExpressionMultiply, diff, sat, 850, 100)
    out = op(unreal.MaterialExpressionAdd, lum, scaled, 1050, 50)
    MEL.connect_material_property(out, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
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
