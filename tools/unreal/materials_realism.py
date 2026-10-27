"""Realistic surfaces for the most visible web materials (run headless with tools/unreal/run-python.ps1 after
import_assets.py; tools/build-assets.ps1 step 5 does).

The web material instances keep their names, slots and colours; only their parent changes:
  MI_facade            -> M_HK_Facade     brick at true size (T_Brick_*), the web's windows (positions read from its own
                                          facade texture) with painted frames, concrete sills and glass that shows a
                                          fake room behind it (interior mapping) under the engine's sky reflection
  MI_brick, MI_plinth  -> M_HK_Brick      the same brick, sized from the web texture's repeat
  MI_roof              -> M_HK_RoofMetal  standing-seam zinc (T_RoofMetal_*)
  MI_plaster_*         -> M_HK_Plaster    the web colour on rendered stucco (T_Plaster_*)
UVs are the web's (texture repeat already applied in Blender), so 1 UV = one web texture tile:
facade 12 m x 2.85 m (4 window bays per tile), brick 2.4 m, roof 8 m, plaster 1.6 m.
"""
import os

import unreal

AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
TEX_IN = os.path.join(ROOT, 'tools', 'out', 'textures')
TEX_DIR = '/Game/Haarklous/Textures/Real'
MAT_DIR = '/Game/Haarklous/Materials'
WEB_DIR = '/Game/Haarklous/Materials/Web'


def log(*a):
    print('HK:', *a)


def import_textures():
    tasks = []
    for base in ('Brick', 'RoofMetal', 'Plaster', 'Concrete'):
        for suffix in ('D', 'N', 'R'):
            t = unreal.AssetImportTask()
            t.filename = os.path.join(TEX_IN, f'T_{base}_{suffix}.png')
            t.destination_path = TEX_DIR
            t.destination_name = f'T_{base}_{suffix}'
            t.automated = True; t.replace_existing = True; t.save = False
            tasks.append(t)
    AT.import_asset_tasks(tasks)
    for base in ('Brick', 'RoofMetal', 'Plaster', 'Concrete'):
        n = unreal.load_asset(f'{TEX_DIR}/T_{base}_N')
        n.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_NORMALMAP)
        n.set_editor_property('srgb', False)
        r = unreal.load_asset(f'{TEX_DIR}/T_{base}_R')
        r.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_MASKS)
        r.set_editor_property('srgb', False)
        for t in (n, r, unreal.load_asset(f'{TEX_DIR}/T_{base}_D')):
            EAL.save_loaded_asset(t)
    log('textures imported')


class Graph:
    """Small helper around MaterialEditingLibrary for building a master material."""

    def __init__(self, name):
        path = f'{MAT_DIR}/{name}'
        if EAL.does_asset_exist(path):
            EAL.delete_asset(path)
        self.m = AT.create_asset(name, MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
        self.y = 0

    def node(self, cls, x=-1200, y=None):
        if y is None:
            y = self.y; self.y += 140
        return MEL.create_material_expression(self.m, cls, x, y)

    def con(self, a, ao, b, bi):
        MEL.connect_material_expressions(a, ao, b, bi)

    def out(self, a, prop, ao=''):
        MEL.connect_material_property(a, ao, prop)

    def scalar(self, name, v):
        e = self.node(unreal.MaterialExpressionScalarParameter); e.set_editor_property('parameter_name', name); e.set_editor_property('default_value', v); return e

    def vector(self, name, v):
        e = self.node(unreal.MaterialExpressionVectorParameter); e.set_editor_property('parameter_name', name); e.set_editor_property('default_value', unreal.LinearColor(*v)); return e

    def tex(self, name, path, uv, kind):
        e = self.node(unreal.MaterialExpressionTextureSampleParameter2D)
        e.set_editor_property('parameter_name', name)
        e.set_editor_property('texture', unreal.load_asset(path))
        e.set_editor_property('sampler_type', {'color': unreal.MaterialSamplerType.SAMPLERTYPE_COLOR,
                                               'normal': unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL,
                                               'mask': unreal.MaterialSamplerType.SAMPLERTYPE_MASKS}[kind])
        e.set_editor_property('sampler_source', unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS)
        if uv is not None:
            self.con(uv, '', e, 'UVs')
        return e

    def mul(self, a, b, ao='', bo=''):
        e = self.node(unreal.MaterialExpressionMultiply); self.con(a, ao, e, 'A'); self.con(b, bo, e, 'B'); return e

    def uv_scaled(self, scale_param):
        tc = self.node(unreal.MaterialExpressionTextureCoordinate)
        return self.mul(tc, scale_param)

    def finish(self):
        self.m.set_editor_property('used_with_static_lighting', True)
        self.m.set_editor_property('used_with_instanced_static_meshes', True)
        MEL.recompile_material(self.m)
        EAL.save_loaded_asset(self.m)
        log('created', self.m.get_path_name())
        return self.m


def pbr_master(name, base, tint_default, rough_scale, metallic):
    """Tinted tiling PBR set: BaseColor = T_<base>_D x Tint, normal, roughness x RoughScale, metallic."""
    g = Graph(name)
    scale = g.vector('UVScale', (1, 1, 0, 0))
    mask = g.node(unreal.MaterialExpressionComponentMask); mask.set_editor_property('r', True); mask.set_editor_property('g', True)
    g.con(scale, '', mask, '')
    uv = g.uv_scaled(mask)
    d = g.tex('Albedo', f'{TEX_DIR}/T_{base}_D', uv, 'color')
    n = g.tex('Normal', f'{TEX_DIR}/T_{base}_N', uv, 'normal')
    r = g.tex('Rough', f'{TEX_DIR}/T_{base}_R', uv, 'mask')
    tint = g.vector('Color', tint_default)
    g.out(g.mul(d, tint, 'RGB', ''), unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(n, unreal.MaterialProperty.MP_NORMAL, 'RGB')
    g.out(g.mul(r, g.scalar('RoughScale', rough_scale), 'R', ''), unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(g.scalar('Metallic', metallic), unreal.MaterialProperty.MP_METALLIC)
    return g.finish()


FACADE_HLSL = r'''
// Web facade texture (facade() in materials.js): one tile = 12 m x one storey (2.85 m), 4 bays of 3 m. In each bay
// the window opening (frame outer edge) is x 75..180 / 256 px and y 47..169 / 244 px; the sill y 169..177 px.
float2 cell = float2(frac(UV.x * 4.0), frac(UV.y));
float2 id = float2(floor(UV.x * 4.0), floor(UV.y));
const float x0 = 75.0 / 256.0, x1 = 180.0 / 256.0, y0 = 47.0 / 244.0, y1 = 169.0 / 244.0;
float inWin = step(x0, cell.x) * step(cell.x, x1) * step(y0, cell.y) * step(cell.y, y1);
float inSill = step(68.0 / 256.0, cell.x) * step(cell.x, 188.0 / 256.0) * step(169.0 / 244.0, cell.y) * step(cell.y, 177.0 / 244.0);
float lum = dot(Lay, float3(0.3, 0.59, 0.11));
float frame = inWin * step(0.6, lum) * step(Lay.b - Lay.r, 0.05);        // painted frames, mullions, closed blinds
float glass = inWin * (1.0 - frame);

// Room behind the glass (interior mapping): a box 3 m wide, 2.85 m high and 4 m deep, entered through the bay.
float3 d = -normalize(V);                                                  // view ray in tangent space, into the wall
float3 rd = float3(d.x / 3.0, d.y / 2.85, max(-d.z, 1e-3) / 4.0);
float3 p = float3(cell.x, cell.y, 0.0);
float tx = rd.x > 0.0 ? (1.0 - p.x) / rd.x : -p.x / min(rd.x, -1e-4);
float ty = rd.y > 0.0 ? (1.0 - p.y) / rd.y : -p.y / min(rd.y, -1e-4);
float tz = 1.0 / rd.z;
float t = min(tx, min(ty, tz));
float3 h = p + rd * t;
float r1 = frac(sin(dot(id + round(WN.xy * 3.0) * 17.0, float2(12.9898, 78.233))) * 43758.5453);
float r2 = frac(r1 * 91.37), r3 = frac(r1 * 47.11), r4 = frac(r1 * 13.7);
float3 wall = r1 < 0.35 ? float3(0.80, 0.77, 0.70) : r1 < 0.55 ? float3(0.66, 0.71, 0.76) : r1 < 0.75 ? float3(0.79, 0.74, 0.60) : r1 < 0.9 ? float3(0.67, 0.72, 0.62) : float3(0.80, 0.69, 0.66);
float3 c;
if (t == tz)
{
    c = wall;
    if (r2 < 0.45 && abs(h.x - 0.25) < 0.1 && h.y > 0.28) c = float3(0.30, 0.22, 0.16);              // door
    if (r3 > 0.5 && abs(h.x - 0.66) < 0.13 && abs(h.y - 0.42) < 0.09) c = float3(0.22, 0.24, 0.28);   // picture
    if (r4 > 0.55 && h.y > 0.62 && abs(h.x - 0.5 - 0.2 * (r2 - 0.5)) < 0.22) c = float3(0.30, 0.31, 0.34) * (0.8 + 0.4 * r3); // sofa/shelf
}
else if (t == ty)
    c = rd.y > 0.0 ? float3(0.40, 0.28, 0.18) * (0.88 + 0.12 * sin(h.z * 90.0)) : float3(0.88, 0.88, 0.86); // floor / ceiling
else
    c = wall * 0.86;
float light = lerp(1.0, 0.38, saturate(h.z)) * (r2 > 0.82 ? 1.25 : 1.0);
c *= light;
// Curtains and blinds, just behind the glass.
float wx = saturate((cell.x - x0) / (x1 - x0)), wy = saturate((cell.y - y0) / (y1 - y0));
float3 cloth = r1 < 0.4 ? float3(0.86, 0.83, 0.76) : r1 < 0.7 ? float3(0.55, 0.57, 0.58) : float3(0.30, 0.34, 0.45);
float cw = 0.1 + 0.18 * r2;
if (r3 < 0.55 && (wx < cw || wx > 1.0 - cw)) c = cloth * (0.75 + 0.25 * sin(wx * 90.0)) * 0.9;
if (r3 > 0.86 && wy < 0.25 + 0.5 * r4) c = float3(0.84, 0.83, 0.80) * (0.85 + 0.15 * step(0.5, frac(wy * 28.0)));
// Shadow of the window reveal along the frame.
c *= 0.55 + 0.45 * smoothstep(0.0, 0.08, min(min(wx, 1.0 - wx), min(wy, 1.0 - wy)));

float isBrick = (1.0 - inWin) * (1.0 - inSill);
Emis = glass * c * Bright;
Rough = isBrick * BR + frame * 0.38 + glass * 0.04 + inSill * 0.7;
Nrm = normalize(lerp(float3(0.0, 0.0, 1.0), BN, isBrick + inSill * 0.6));
return isBrick * BD + frame * Lay * 1.08 + inSill * CD * float3(0.86, 0.83, 0.76) + glass * 0.02;
'''


def facade_master():
    g = Graph('M_HK_Facade')
    tc = g.node(unreal.MaterialExpressionTextureCoordinate)
    layout = g.tex('Layout', '/Game/Haarklous/Textures/Web/T_Web_6', tc, 'color')
    bscale = g.vector('BrickScale', (12.5, 3.084, 0, 0))      # 12 m / 0.96 m, 2.85 m / 0.924 m
    bmask = g.node(unreal.MaterialExpressionComponentMask); bmask.set_editor_property('r', True); bmask.set_editor_property('g', True)
    g.con(bscale, '', bmask, '')
    buv = g.mul(tc, bmask)
    bd = g.tex('BrickAlbedo', f'{TEX_DIR}/T_Brick_D', buv, 'color')
    bn = g.tex('BrickNormal', f'{TEX_DIR}/T_Brick_N', buv, 'normal')
    br = g.tex('BrickRough', f'{TEX_DIR}/T_Brick_R', buv, 'mask')
    cd = g.tex('ConcreteAlbedo', f'{TEX_DIR}/T_Concrete_D', buv, 'color')
    # View vector in tangent space (UVs: x along u, y along v = down the storey, z out of the wall).
    cam = g.node(unreal.MaterialExpressionCameraVectorWS)
    tv = g.node(unreal.MaterialExpressionTransform)
    tv.set_editor_property('transform_source_type', unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD)
    tv.set_editor_property('transform_type', unreal.MaterialVectorCoordTransform.TRANSFORM_TANGENT)
    g.con(cam, '', tv, '')
    wn = g.node(unreal.MaterialExpressionVertexNormalWS)
    bright = g.scalar('InteriorBrightness', 0.09)   # rooms look darker than the sunlit street from outside
    c = g.node(unreal.MaterialExpressionCustom, -500, 0)
    c.set_editor_property('code', FACADE_HLSL)
    c.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    ins = []
    for nm in ('UV', 'V', 'Lay', 'BD', 'BN', 'BR', 'CD', 'WN', 'Bright'):
        ci = unreal.CustomInput(); ci.set_editor_property('input_name', nm); ins.append(ci)
    c.set_editor_property('inputs', ins)
    outs = []
    for nm, ty in (('Emis', unreal.CustomMaterialOutputType.CMOT_FLOAT3), ('Rough', unreal.CustomMaterialOutputType.CMOT_FLOAT1), ('Nrm', unreal.CustomMaterialOutputType.CMOT_FLOAT3)):
        co = unreal.CustomOutput(); co.set_editor_property('output_name', nm); co.set_editor_property('output_type', ty); outs.append(co)
    c.set_editor_property('additional_outputs', outs)
    g.con(tc, '', c, 'UV'); g.con(tv, '', c, 'V'); g.con(layout, 'RGB', c, 'Lay'); g.con(bd, 'RGB', c, 'BD')
    g.con(bn, 'RGB', c, 'BN'); g.con(br, 'R', c, 'BR'); g.con(cd, 'RGB', c, 'CD'); g.con(wn, '', c, 'WN'); g.con(bright, '', c, 'Bright')
    g.out(c, unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(c, unreal.MaterialProperty.MP_EMISSIVE_COLOR, 'Emis')
    g.out(c, unreal.MaterialProperty.MP_ROUGHNESS, 'Rough')
    g.out(c, unreal.MaterialProperty.MP_NORMAL, 'Nrm')
    return g.finish()


WIND_HLSL = r'''
// Wind: the whole tree leans and sways slowly (more the higher above its base), leaves flutter faster.
// LP = vertex position in the unit-size tree mesh (about 0..160 from base to crown top), WP = world position (cm).
float h = saturate(LP.z / 160.0);
float ph = dot(WP.xy, float2(0.0007, 0.0011));
float sway = sin(T * 1.1 + ph) * 0.6 + sin(T * 2.3 + ph * 1.7) * 0.25;
float flutter = sin(T * 7.0 + dot(WP, float3(0.05, 0.07, 0.03))) * Flutter;
return float3(sway * Amp * h * h + flutter, (sway * 0.4) * Amp * h * h + flutter * 0.6, 0.0);
'''


def foliage_master():
    """Leaves and bark of the Blender trees and bushes: same parameter names as M_HK_Master (BaseMap, NormalMap,
    Color, Roughness) so the instances keep their textures; masked, two-sided foliage shading (light comes through
    the leaves) and wind in the vertex shader."""
    g = Graph('M_HK_Foliage')
    m = g.m
    m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
    m.set_editor_property('two_sided', True)
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
    m.set_editor_property('opacity_mask_clip_value', 0.45)
    tc = g.node(unreal.MaterialExpressionTextureCoordinate)
    base = g.tex('BaseMap', '/Engine/EngineResources/WhiteSquareTexture', tc, 'color')
    nrm = g.tex('NormalMap', '/Engine/EngineMaterials/DefaultNormal', tc, 'normal')
    color = g.vector('Color', (1, 1, 1, 1))
    albedo = g.mul(base, color, 'RGB', '')
    g.out(albedo, unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(nrm, unreal.MaterialProperty.MP_NORMAL, 'RGB')
    g.out(g.scalar('Roughness', 0.78), unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(base, unreal.MaterialProperty.MP_OPACITY_MASK, 'A')
    # Light through the leaves: a deeper, yellower green than the surface.
    g.out(g.mul(albedo, g.vector('Transmission', (0.75, 0.9, 0.35, 1))), unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
    c = g.node(unreal.MaterialExpressionCustom, -500, 600)
    c.set_editor_property('code', WIND_HLSL)
    c.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    ins = []
    for nm in ('LP', 'WP', 'T', 'Amp', 'Flutter'):
        ci = unreal.CustomInput(); ci.set_editor_property('input_name', nm); ins.append(ci)
    c.set_editor_property('inputs', ins)
    g.con(g.node(unreal.MaterialExpressionLocalPosition), '', c, 'LP')
    g.con(g.node(unreal.MaterialExpressionWorldPosition), '', c, 'WP')
    g.con(g.node(unreal.MaterialExpressionTime), '', c, 'T')
    g.con(g.scalar('WindAmplitude', 22.0), '', c, 'Amp')
    g.con(g.scalar('LeafFlutter', 1.2), '', c, 'Flutter')
    g.out(c, unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    return g.finish()


def car_paint_master():
    """Vehicle body paint (the web's clear-coated MeshPhysicalMaterial): metallic base under a glossy lacquer layer.
    'Color' is the parameter AHKRigActor::SetPaint sets for the cars' seven colours."""
    g = Graph('M_HK_CarPaint')
    g.m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_CLEAR_COAT)
    g.out(g.vector('Color', (0.5, 0.5, 0.5, 1)), unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(g.scalar('Metallic', 0.5), unreal.MaterialProperty.MP_METALLIC)
    g.out(g.scalar('Roughness', 0.35), unreal.MaterialProperty.MP_ROUGHNESS)
    # ClearCoat and ClearCoatRoughness are not reachable from Python here; unconnected they default to a full,
    # slightly soft lacquer layer (1.0 and 0.1), which suits car paint.
    return g.finish()


GRASS_WPO_HLSL = r'''
// H = height along the blade (vertex colour red), Tm = time. Sway grows toward the tip; near the fade distance the
// blades shrink into the ground instead of popping out (Fade = PerInstanceFadeAmount, 0 visible .. 1 gone).
// (Custom-node input names are not case-sensitive, so not "t" and "T".)
float ph = dot(WP.xy, float2(0.013, 0.009));
float sway = (sin(Tm * 1.9 + ph) * 0.7 + sin(Tm * 4.3 + ph * 2.1) * 0.3) * Amp * H * H;
return float3(sway, sway * 0.6, -H * 16.0 * Fade);
'''


def grass():
    """3D grass near the camera (AHKGame::UpdateGrass): the Blender clump and its material, coloured like the
    ground's grass texture at the same spot (same 3 m tiling as M_HK_Ground)."""
    t = unreal.AssetImportTask()
    t.filename = os.path.join(ROOT, 'tools', 'out', 'fbx', 'grass.fbx')
    t.destination_path = '/Game/Haarklous/Meshes/grass'
    t.destination_name = 'SM_GrassClump'
    t.automated = True; t.replace_existing = True; t.save = False
    ui = unreal.FbxImportUI()
    ui.set_editor_property('import_mesh', True)
    ui.set_editor_property('import_as_skeletal', False)
    ui.set_editor_property('import_materials', False)
    ui.set_editor_property('import_textures', False)
    sd = ui.get_editor_property('static_mesh_import_data')
    sd.set_editor_property('combine_meshes', True)
    sd.set_editor_property('generate_lightmap_u_vs', False)
    sd.set_editor_property('auto_generate_collision', False)
    sd.set_editor_property('vertex_color_import_option', unreal.VertexColorImportOption.REPLACE)
    sd.set_editor_property('normal_import_method', unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
    t.options = ui
    AT.import_asset_tasks([t])
    g = Graph('M_HK_Grass')   # one-sided: the clump has both sides as triangles, all with upward normals
    wp = g.node(unreal.MaterialExpressionWorldPosition)
    wxy = g.node(unreal.MaterialExpressionComponentMask); wxy.set_editor_property('r', True); wxy.set_editor_property('g', True)
    g.con(wp, '', wxy, '')
    div = g.node(unreal.MaterialExpressionDivide); g.con(wxy, '', div, 'A'); g.con(g.scalar('GroundTile', 300.0), '', div, 'B')
    ground = g.tex('GroundGrass', '/Game/Haarklous/Textures/Ground/T_Grass_D', div, 'color')
    vc = g.node(unreal.MaterialExpressionVertexColor)
    shade = g.node(unreal.MaterialExpressionLinearInterpolate)
    g.con(g.scalar('RootShade', 0.5), '', shade, 'A'); g.con(g.scalar('TipShade', 1.5), '', shade, 'B'); g.con(vc, 'R', shade, 'Alpha')
    col = g.mul(g.mul(ground, shade, 'RGB', ''), g.vector('Tint', (0.92, 1.0, 0.86, 1)))
    g.out(col, unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(g.scalar('Roughness', 0.85), unreal.MaterialProperty.MP_ROUGHNESS)
    # No specular: thin blades seen edge-on would mirror the sky at grazing angles and turn grey-blue.
    g.out(g.scalar('Specular', 0.0), unreal.MaterialProperty.MP_SPECULAR)
    c = g.node(unreal.MaterialExpressionCustom, -500, 600)
    c.set_editor_property('code', GRASS_WPO_HLSL)
    c.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    ins = []
    for nm in ('H', 'WP', 'Tm', 'Amp', 'Fade'):
        ci = unreal.CustomInput(); ci.set_editor_property('input_name', nm); ins.append(ci)
    c.set_editor_property('inputs', ins)
    g.con(vc, 'R', c, 'H'); g.con(wp, '', c, 'WP'); g.con(g.node(unreal.MaterialExpressionTime), '', c, 'Tm')
    g.con(g.scalar('WindAmplitude', 4.0), '', c, 'Amp')
    g.con(g.node(unreal.MaterialExpressionPerInstanceFadeAmount), '', c, 'Fade')
    g.out(c, unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    m = g.finish()
    sm = unreal.load_asset('/Game/Haarklous/Meshes/grass/SM_GrassClump')
    if sm:
        sm.set_material(0, m)
        EAL.save_loaded_asset(sm)
        log('grass clump ready')
    else:
        log('grass clump missing')


def reparent(mi_name, master, vectors=None, scalars=None, keep_color=False):
    mi = unreal.load_asset(f'{WEB_DIR}/{mi_name}')
    if not mi:
        log('missing', mi_name); return
    color = None
    if keep_color:
        color = MEL.get_material_instance_vector_parameter_value(mi, 'Color')
    mi.set_editor_property('parent', master)
    MEL.clear_all_material_instance_parameters(mi)
    if color is not None:
        MEL.set_material_instance_vector_parameter_value(mi, 'Color', color)
    for k, v in (vectors or {}).items():
        MEL.set_material_instance_vector_parameter_value(mi, k, unreal.LinearColor(*v))
    for k, v in (scalars or {}).items():
        MEL.set_material_instance_scalar_parameter_value(mi, k, v)
    MEL.update_material_instance(mi)
    EAL.save_loaded_asset(mi)
    return mi


import_textures()
facade = facade_master()
brick = pbr_master('M_HK_Brick', 'Brick', (1, 1, 1, 1), 1.0, 0.0)
roof = pbr_master('M_HK_RoofMetal', 'RoofMetal', (0.82, 0.84, 0.84, 1), 1.15, 0.35)
plaster = pbr_master('M_HK_Plaster', 'Plaster', (1, 1, 1, 1), 1.0, 0.0)

reparent('MI_facade', facade)
reparent('MI_brick', brick, vectors={'UVScale': (2.5, 2.597, 0, 0)})                    # 2.4 m web tile / brick tile
reparent('MI_plinth', brick, vectors={'UVScale': (12.5, 1.542, 0, 0), 'Color': (0.72, 0.62, 0.58, 1)})  # 12 x 1.425 m, darker
reparent('MI_roof', roof)
n = 0
for path in EAL.list_assets(WEB_DIR, recursive=False, include_folder=False):
    name = path.split('/')[-1].split('.')[0]
    if name.startswith('MI_plaster_'):
        # plaster(color) in materials.js paints its texture in this colour (the instance's own name).
        hx = int(name.split('_')[2], 16)
        lin = [((c / 255.0) / 12.92 if c / 255.0 <= 0.04045 else ((c / 255.0 + 0.055) / 1.055) ** 2.4) for c in ((hx >> 16) & 255, (hx >> 8) & 255, hx & 255)]
        reparent(name, plaster, vectors={'Color': (lin[0], lin[1], lin[2], 1)}); n += 1
log(f'reparented facade, brick, plinth, roof and {n} plaster instances')

# Trees and bushes: keep each instance's textures and colour, change the parent.
foliage = foliage_master()
for key in ('MI_HK_LeavesA', 'MI_HK_LeavesB', 'MI_HK_LeavesBush', 'MI_HK_Bark'):
    mi = unreal.load_asset(f'{MAT_DIR}/Foliage/{key}')
    if not mi:
        log('missing', key); continue
    mi.set_editor_property('parent', foliage)
    if key == 'MI_HK_Bark':
        # Solid bark, normal lighting, swaying with its crown but no flutter.
        ov = unreal.MaterialInstanceBasePropertyOverrides()
        ov.set_editor_property('override_blend_mode', True); ov.set_editor_property('blend_mode', unreal.BlendMode.BLEND_OPAQUE)
        ov.set_editor_property('override_two_sided', True); ov.set_editor_property('two_sided', False)
        ov.set_editor_property('override_shading_model', True); ov.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
        mi.set_editor_property('base_property_overrides', ov)
        MEL.set_material_instance_scalar_parameter_value(mi, 'LeafFlutter', 0.0)
    elif key == 'MI_HK_LeavesBush':
        MEL.set_material_instance_scalar_parameter_value(mi, 'WindAmplitude', 4.0)
        MEL.set_material_instance_scalar_parameter_value(mi, 'LeafFlutter', 0.6)
    MEL.update_material_instance(mi)
    EAL.save_loaded_asset(mi)
log('foliage instances use M_HK_Foliage')

# Vehicle paint: the opaque MI_Physical_* instances (the see-through ones are the windows).
paint = car_paint_master()
n = 0
for path in EAL.list_assets(WEB_DIR, recursive=False, include_folder=False):
    name = path.split('/')[-1].split('.')[0]
    if not name.startswith('MI_Physical_'):
        continue
    mi = unreal.load_asset(f'{WEB_DIR}/{name}')
    parent = mi.get_editor_property('parent')
    # Paint: opaque under the web master, or already paint from an earlier run (its master was just recreated, so
    # the parent is gone or is the new M_HK_CarPaint).
    already = parent is None or parent.get_name() == 'M_HK_CarPaint'
    if not already and MEL.get_material_instance_scalar_parameter_value(mi, 'Opacity') < 0.99:
        continue
    color = MEL.get_material_instance_vector_parameter_value(mi, 'Color')
    rough = MEL.get_material_instance_scalar_parameter_value(mi, 'Roughness')
    metal = MEL.get_material_instance_scalar_parameter_value(mi, 'Metallic')
    reparent(name, paint, vectors={'Color': (color.r, color.g, color.b, 1)}, scalars={'Roughness': max(0.25, rough), 'Metallic': metal if already else metal * 0.8})
    n += 1
log(f'{n} paint instances use M_HK_CarPaint')

grass()
