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


drunk()
