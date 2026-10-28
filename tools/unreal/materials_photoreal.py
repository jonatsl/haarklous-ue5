"""Photo-real pass (27 Oct 2026, run headless with tools/unreal/run-python.ps1 after materials_realism.py;
tools/build-assets.ps1 step 5 does). Uses the CC0 scans from tools/fetch-library.py and the building map from
tools/make-building-map.py.

  M_HK_Building (MI_facade)  every apartment block looks like its real self: the building map says salmon brick
                             (the blocks around Haarklous plass) or plaster in the building's colour, and dark-brown or
                             white windows. Windows have real depth in the shader: the glass sits 10-14 cm back in a
                             reveal that is seen from the side and casts the sun's shadow onto frame and glass;
                             frames, mullions and sashes; 1, 2 or 3 lights per window, the same in a column; rooms
                             behind the glass; rain streaks under the sills on plaster.
  M_HK_Plinth (MI_plinth)    grey concrete base with recessed basement windows.
  M_HK_Roof (MI_roof)        dark slate or red clay tiles per building, rows following each roof face; flat roofs
                             are bitumen.
  MI_Ground_near/far         photo-scanned lawn (ambientCG Grass001) and asphalt (Poly Haven asphalt_02).
  M_HK_GrassCards            the 3D lawn: cards cut from real grass tufts (tools/make-grass-atlas.py).
  Trees                      scanned bark.
"""
import json
import os

import unreal

AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
LIB = os.path.join(ROOT, 'tools', 'out', 'library')
TEX = os.path.join(ROOT, 'tools', 'out', 'textures')
LIB_DIR = '/Game/Haarklous/Textures/Lib'
MAT_DIR = '/Game/Haarklous/Materials'
WEB_DIR = '/Game/Haarklous/Materials/Web'
MAP = json.load(open(os.path.join(TEX, 'building-map.json'), encoding='utf-8'))

# name -> (folder, file prefix, maps): D colour, N normal (OpenGL, green flipped on import), R roughness, A ambient occlusion
PH = lambda a: (a, a + '_', {'D': 'diff_2k.jpg', 'N': 'nor_gl_2k.jpg', 'R': 'rough_2k.jpg'})
LIBRARY = {
    'Brick': PH('brick_wall_09'),
    'Plaster': PH('plastered_wall_02'),
    'Concrete': PH('concrete_wall_008'),
    'Asphalt': PH('asphalt_02'),
    'Slate': PH('grey_roof_01'),
    'Tiles': PH('clay_roof_tiles_02'),
    'Bitumen': PH('asphalt_04'),
    'Bark': PH('bark_brown_02'),
    'Lawn': ('Grass001', 'Grass001_2K-JPG_', {'D': 'Color.jpg', 'N': 'NormalGL.jpg', 'R': 'Roughness.jpg'}),
}
# Average linear colour of each scan (measured from the 2K maps), to tint them to a wanted colour.
BRICK_AVG = (0.3587, 0.2353, 0.1676)
PLASTER_AVG = (0.4943, 0.4405, 0.3687)


def log(*a):
    print('HK:', *a)


def import_file(src, name, kind):
    t = unreal.AssetImportTask()
    t.filename = src; t.destination_path = LIB_DIR; t.destination_name = name
    t.automated = True; t.replace_existing = True; t.save = False
    AT.import_asset_tasks([t])
    tex = unreal.load_asset(f'{LIB_DIR}/{name}')
    if kind == 'N':
        tex.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_NORMALMAP)
        tex.set_editor_property('srgb', False)
        tex.set_editor_property('flip_green_channel', True)
    elif kind == 'R':
        tex.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_MASKS)
        tex.set_editor_property('srgb', False)
    elif kind == 'ID':
        tex.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_GRAYSCALE)
        tex.set_editor_property('srgb', False)
    elif kind == 'PAL':
        tex.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP)
        tex.set_editor_property('srgb', False)
    if kind in ('ID', 'PAL'):
        tex.set_editor_property('mip_gen_settings', unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        tex.set_editor_property('filter', unreal.TextureFilter.TF_NEAREST)
        tex.set_editor_property('never_stream', True)
    if kind == 'CARDS':
        # Thin blades vanish from alpha-tested mip maps; keep each mip's coverage at the clip value instead.
        tex.set_editor_property('do_scale_mips_for_alpha_coverage', True)
        tex.set_editor_property('alpha_coverage_thresholds', unreal.Vector4(0, 0, 0, 0.45 if 'Leaves' in name else 0.4))
    tex.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_WORLD)
    EAL.save_loaded_asset(tex)
    return tex


def import_all():
    for key, (folder, prefix, maps) in LIBRARY.items():
        for kind, suffix in maps.items():
            import_file(os.path.join(LIB, folder, prefix + suffix), f'T_{key}_{kind}', kind)
    import_file(os.path.join(TEX, 'T_BuildingId.png'), 'T_BuildingId', 'ID')
    import_file(os.path.join(TEX, 'T_BuildingPalette.png'), 'T_BuildingPalette', 'PAL')
    import_file(os.path.join(TEX, 'T_GrassCards_D.png'), 'T_GrassCards_D', 'CARDS')
    import_file(os.path.join(TEX, 'T_GrassCards_N.png'), 'T_GrassCards_N', 'N')
    import_file(os.path.join(TEX, 'T_Leaves_D.png'), 'T_Leaves_D', 'CARDS')
    import_file(os.path.join(TEX, 'T_Leaves_N.png'), 'T_Leaves_N', 'N')
    log('library textures imported')


class Graph:
    """Small helper around MaterialEditingLibrary for building a master material."""

    def __init__(self, name):
        path = f'{MAT_DIR}/{name}'
        if EAL.does_asset_exist(path):
            EAL.delete_asset(path)
        self.m = AT.create_asset(name, MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
        self.y = 0

    def node(self, cls, x=-1400):
        self.y += 140
        return MEL.create_material_expression(self.m, cls, x, self.y)

    def con(self, a, ao, b, bi):
        MEL.connect_material_expressions(a, ao, b, bi)

    def out(self, a, prop, ao=''):
        MEL.connect_material_property(a, ao, prop)

    def scalar(self, name, v):
        e = self.node(unreal.MaterialExpressionScalarParameter); e.set_editor_property('parameter_name', name); e.set_editor_property('default_value', v); return e

    def vector(self, name, v):
        e = self.node(unreal.MaterialExpressionVectorParameter); e.set_editor_property('parameter_name', name); e.set_editor_property('default_value', unreal.LinearColor(*v)); return e

    def mask(self, a, chans, ao=''):
        e = self.node(unreal.MaterialExpressionComponentMask)
        for c in 'rgba':
            e.set_editor_property(c, c in chans)
        self.con(a, ao, e, ''); return e

    def tex(self, name, path, uv, kind):
        e = self.node(unreal.MaterialExpressionTextureSampleParameter2D)
        e.set_editor_property('parameter_name', name)
        e.set_editor_property('texture', unreal.load_asset(path))
        e.set_editor_property('sampler_type', {'color': unreal.MaterialSamplerType.SAMPLERTYPE_COLOR,
                                               'normal': unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL,
                                               'mask': unreal.MaterialSamplerType.SAMPLERTYPE_MASKS,
                                               'lgray': unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE,
                                               'lcolor': unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR}[kind])
        # Shared samplers: lightmaps, shadow maps and a dozen textures stay under Direct3D 11's 16-sampler limit.
        e.set_editor_property('sampler_source', unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS)
        if uv is not None:
            self.con(uv, '', e, 'UVs')
        return e

    def mul(self, a, b, ao='', bo=''):
        e = self.node(unreal.MaterialExpressionMultiply); self.con(a, ao, e, 'A'); self.con(b, bo, e, 'B'); return e

    def add(self, a, b, ao='', bo=''):
        e = self.node(unreal.MaterialExpressionAdd); self.con(a, ao, e, 'A'); self.con(b, bo, e, 'B'); return e

    def custom(self, code, out_type, inputs, outputs=()):
        """inputs: [(name, expression, output pin)], outputs: [(name, type)] as additional outputs."""
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

    def to_tangent(self, e):
        t = self.node(unreal.MaterialExpressionTransform)
        t.set_editor_property('transform_source_type', unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD)
        t.set_editor_property('transform_type', unreal.MaterialVectorCoordTransform.TRANSFORM_TANGENT)
        self.con(e, '', t, ''); return t

    def finish(self):
        self.m.set_editor_property('used_with_static_lighting', True)
        self.m.set_editor_property('used_with_instanced_static_meshes', True)
        MEL.recompile_material(self.m)
        EAL.save_loaded_asset(self.m)
        log('created', self.m.get_path_name())
        return self.m


F1, F2, F3, F4 = (unreal.CustomMaterialOutputType.CMOT_FLOAT1, unreal.CustomMaterialOutputType.CMOT_FLOAT2,
                  unreal.CustomMaterialOutputType.CMOT_FLOAT3, unreal.CustomMaterialOutputType.CMOT_FLOAT4)

MAP_HLSL = r'''
// Building map lookup: the point P (cm, world) moved In cm into the building, as the texel centre of the 2048 px map.
float2 uv = (P.xy - N.xy * In - Org.xy) / Sz;
return (floor(uv * 2048.0) + 0.5) / 2048.0;
'''

PAL_HLSL = r'''
// Palette rows: 0 = facade colour, 1 = style. The id map stores the palette index / 255.
float u = (round(Id * 255.0) + 0.5) / 256.0;
Row1 = float2(u, 0.75);
return float2(u, 0.25);
'''


def building_lookup(g, inset_cm):
    """Nodes that read this pixel's building from the map: returns (colour row, style row, id) expressions."""
    wp = g.node(unreal.MaterialExpressionWorldPosition)
    wn = g.node(unreal.MaterialExpressionVertexNormalWS)
    org = g.vector('MapOrigin', (MAP['origin'][0], MAP['origin'][1], 0, 0))
    uv = g.custom(MAP_HLSL, F2, [('P', wp, ''), ('N', wn, ''), ('Org', org, ''), ('Sz', g.scalar('MapSize', MAP['size']), ''), ('In', g.scalar('MapInset', inset_cm), '')])
    idt = g.tex('BuildingId', f'{LIB_DIR}/T_BuildingId', uv, 'lgray')
    pal = g.custom(PAL_HLSL, F2, [('Id', idt, 'R')], [('Row1', F2)])
    row0 = g.tex('Palette0', f'{LIB_DIR}/T_BuildingPalette', pal, 'lcolor')
    row1 = g.node(unreal.MaterialExpressionTextureSampleParameter2D)
    row1.set_editor_property('parameter_name', 'Palette1')
    row1.set_editor_property('texture', unreal.load_asset(f'{LIB_DIR}/T_BuildingPalette'))
    row1.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    row1.set_editor_property('sampler_source', unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS)
    g.con(pal, 'Row1', row1, 'UVs')
    return row0, row1, idt, wp, wn


FACADE_HLSL = r'''
// Facade UV (the web's): 1 = 12 m along the wall, 1 = one storey of 2.85 m, v growing downward. 4 bays of 3 m.
// Tangent space: x along u, y along v (down), z out of the wall. Lengths in metres.
float plaster = Look.r, white = Look.b, variant = Look.a;
float3 lin = pow(max(Col, 0.0), 2.2);
float3 wallC = lerp(BD * lin / BAvg * BrickK * (0.92 + 0.12 * variant), PD * lin / PAvg * PlasterK, plaster);
float3 wallN = normalize(lerp(BN, PN, plaster));
float wallR = lerp(BR, PR, plaster);

float bayF = UV.x * 4.0;
float colId = floor(bayF);
float2 p = float2(frac(bayF) * 3.0, frac(UV.y) * 2.85);
float hc = frac(sin(colId * 12.9898 + Seed * 78.233) * 43758.5453);
// Window width: the blocks' wide three-light windows and narrower two-light ones, the same in a whole column.
float W = plaster < 0.5 ? (hc < 0.35 ? 1.25 : hc < 0.75 ? 1.75 : 2.0) : (hc < 0.3 ? 1.0 : hc < 0.8 ? 1.25 : 1.5);
float Hh = 1.425;
float ox0 = 1.5 - W * 0.5, ox1 = 1.5 + W * 0.5, oy0 = 0.549, oy1 = oy0 + Hh;
float D = lerp(0.14, 0.10, plaster);
// No windows below the first storey (walls of the web's plaster buildings start at the ground, not a plinth).
float allow = step(UV.y, MaxV);
float inOpen = step(ox0, p.x) * step(p.x, ox1) * step(oy0, p.y) * step(p.y, oy1) * allow;
float inSill = step(ox0 - 0.05, p.x) * step(p.x, ox1 + 0.05) * step(oy1, p.y) * step(p.y, oy1 + 0.06) * allow;

float3 d = -normalize(V);
float dz = max(-d.z, 1e-3);
float2 slope = d.xy / dz;                       // sideways metres per metre of depth
float2 q = p + slope * D;                       // where the ray meets the glass plane
float inQ = step(ox0, q.x) * step(q.x, ox1) * step(oy0, q.y) * step(q.y, oy1);

// Reveal: which side wall the ray meets first when it does not reach the glass.
float tx = slope.x > 0.0 ? (ox1 - p.x) / max(slope.x, 1e-4) : (ox0 - p.x) / min(slope.x, -1e-4);
float ty = slope.y > 0.0 ? (oy1 - p.y) / max(slope.y, 1e-4) : (oy0 - p.y) / min(slope.y, -1e-4);
float3 revN = tx < ty ? float3(slope.x > 0.0 ? -1.0 : 1.0, 0.0, 0.0) : float3(0.0, slope.y > 0.0 ? -1.0 : 1.0, 0.0);

// Window at the glass plane: frame, mullions, sashes and glass.
float wx = q.x - ox0, wy = q.y - oy0;
float lights = W < 1.1 ? 1.0 : W < 1.6 ? 2.0 : 3.0;
float edge = min(min(wx, W - wx), min(wy, Hh - wy));
float frame = step(edge, 0.06);
float lw = W / lights;
float lx = frac(wx / lw) * lw;                  // position inside the light
float mull = (lights > 1.0 && (lx < 0.045 || lx > lw - 0.045) && wx > 0.06 && wx < W - 0.06) ? 1.0 : 0.0;
float sash = step(min(min(lx, lw - lx), min(wy, Hh - wy)), 0.095) * (1.0 - frame) * (1.0 - mull);
float wood = max(max(frame, mull), sash);
float glass = 1.0 - wood;
// Sun shadow of the reveal on frame and glass.
float lit = 1.0;
if (L.z > 0.02)
{
    float2 s = q + L.xy / L.z * D;
    lit = (step(ox0, s.x) * step(s.x, ox1) * step(oy0, s.y) * step(s.y, oy1)) > 0.5 ? 1.0 : 0.35;
}
else lit = 0.6;

// Room behind the glass (interior mapping): 3 m wide, 2.85 m high, 4 m deep.
float2 cell = float2(q.x / 3.0, q.y / 2.85);
float3 rd = float3(d.x / 3.0, d.y / 2.85, dz / 4.0);
float rtx = rd.x > 0.0 ? (1.0 - cell.x) / rd.x : -cell.x / min(rd.x, -1e-4);
float rty = rd.y > 0.0 ? (1.0 - cell.y) / rd.y : -cell.y / min(rd.y, -1e-4);
float rtz = 1.0 / rd.z;
float t = min(rtx, min(rty, rtz));
float3 h = float3(cell, 0.0) + rd * t;
float r1 = frac(sin(dot(float2(colId, floor(UV.y)) + Seed * 3.0, float2(12.9898, 78.233))) * 43758.5453);
float r2 = frac(r1 * 91.37), r3 = frac(r1 * 47.11), r4 = frac(r1 * 13.7);
float3 room = r1 < 0.35 ? float3(0.80, 0.77, 0.70) : r1 < 0.55 ? float3(0.66, 0.71, 0.76) : r1 < 0.75 ? float3(0.79, 0.74, 0.60) : r1 < 0.9 ? float3(0.67, 0.72, 0.62) : float3(0.80, 0.69, 0.66);
float3 c = room;
if (t == rtz)
{
    if (r2 < 0.45 && abs(h.x - 0.25) < 0.1 && h.y > 0.28) c = float3(0.30, 0.22, 0.16);
    if (r3 > 0.5 && abs(h.x - 0.66) < 0.13 && abs(h.y - 0.42) < 0.09) c = float3(0.22, 0.24, 0.28);
    if (r4 > 0.55 && h.y > 0.62 && abs(h.x - 0.5 - 0.2 * (r2 - 0.5)) < 0.22) c = float3(0.30, 0.31, 0.34) * (0.8 + 0.4 * r3);
}
else if (t == rty)
    c = rd.y > 0.0 ? float3(0.40, 0.28, 0.18) * (0.88 + 0.12 * sin(h.z * 90.0)) : float3(0.88, 0.88, 0.86);
else
    c = room * 0.86;
c *= lerp(1.0, 0.38, saturate(h.z)) * (r2 > 0.82 ? 1.25 : 1.0);
float cx = saturate(wx / W), cy = saturate(wy / Hh);
float3 cloth = r1 < 0.4 ? float3(0.86, 0.83, 0.76) : r1 < 0.7 ? float3(0.55, 0.57, 0.58) : float3(0.30, 0.34, 0.45);
float cw = 0.1 + 0.18 * r2;
if (r3 < 0.55 && (cx < cw || cx > 1.0 - cw)) c = cloth * (0.75 + 0.25 * sin(cx * 90.0)) * 0.9;
if (r3 > 0.86 && cy < 0.25 + 0.5 * r4) c = float3(0.84, 0.83, 0.80) * (0.85 + 0.15 * step(0.5, frac(cy * 28.0)));
c *= 0.55 + 0.45 * smoothstep(0.0, 0.1, min(min(cx, 1.0 - cx), min(cy, 1.0 - cy))) * lit;

float3 frameC = white > 0.5 ? float3(0.78, 0.78, 0.76) : float3(0.075, 0.036, 0.020);
frameC *= sash > 0.5 ? 0.92 : 1.0;

// Rain streaks under the sills (plaster shows them).
float below = inOpen < 0.5 && p.x > ox0 && p.x < ox1 && p.y > oy1 + 0.06 ? saturate(1.0 - (p.y - oy1 - 0.06) / 0.9) : 0.0;
float streak = below * (0.5 + 0.5 * sin(p.x * 37.0 + sin(p.x * 11.0) * 2.0));
wallC *= 1.0 - streak * lerp(0.05, 0.14, plaster);

float reveal = inOpen * (1.0 - inQ);
float atGlass = inOpen * inQ;
float wall = (1.0 - inOpen) * (1.0 - inSill);
float3 sillC = CD * float3(0.95, 0.93, 0.9);

Emis = atGlass * glass * c * Bright;
Rough = wall * wallR + reveal * wallR + atGlass * (wood * 0.5 + glass * 0.04) + inSill * 0.65;
Nrm = normalize(wall * wallN + reveal * revN + (atGlass + inSill) * float3(0.0, 0.0, 1.0) + float3(0.0, 0.0, 1e-4));
return wall * wallC + reveal * wallC * 0.85 + atGlass * (wood * frameC * lit + glass * 0.015) + inSill * sillC;
'''


def building_master():
    g = Graph('M_HK_Building')
    tc = g.node(unreal.MaterialExpressionTextureCoordinate)
    # Plaster buildings of the web without windows could use this too: UV scale/offset to the facade convention.
    fuv = g.add(g.mul(tc, g.mask(g.vector('FacadeUVScale', (1, 1, 0, 0)), 'rg')), g.mask(g.vector('FacadeUVOffset', (0, 0, 0, 0)), 'rg'))
    row0, row1, idt, wp, wn = building_lookup(g, 60.0)
    brick_uv = g.mul(fuv, g.mask(g.vector('BrickScale', (12.0 / 2.01, 2.85 / 2.01, 0, 0)), 'rg'))
    plaster_uv = g.mul(fuv, g.mask(g.vector('PlasterScale', (12.0 / 2.23, 2.85 / 2.23, 0, 0)), 'rg'))
    bd = g.tex('BrickAlbedo', f'{LIB_DIR}/T_Brick_D', brick_uv, 'color')
    bn = g.tex('BrickNormal', f'{LIB_DIR}/T_Brick_N', brick_uv, 'normal')
    br = g.tex('BrickRough', f'{LIB_DIR}/T_Brick_R', brick_uv, 'mask')
    pd = g.tex('PlasterAlbedo', f'{LIB_DIR}/T_Plaster_D', plaster_uv, 'color')
    pn = g.tex('PlasterNormal', f'{LIB_DIR}/T_Plaster_N', plaster_uv, 'normal')
    pr = g.tex('PlasterRough', f'{LIB_DIR}/T_Plaster_R', plaster_uv, 'mask')
    cd = g.tex('ConcreteAlbedo', f'{LIB_DIR}/T_Concrete_D', brick_uv, 'color')
    v = g.to_tangent(g.node(unreal.MaterialExpressionCameraVectorWS))
    sun = g.to_tangent(g.node(unreal.MaterialExpressionSkyAtmosphereLightDirection))
    seed = g.custom('return Id * 255.0 + dot(round(N.xy * 2.0), float2(7.0, 13.0));', F1, [('Id', idt, 'R'), ('N', wn, '')])
    c = g.custom(FACADE_HLSL, F3, [
        ('UV', fuv, ''), ('V', v, ''), ('L', sun, ''), ('Look', row1, 'RGBA'), ('Col', row0, 'RGB'),
        ('BD', bd, 'RGB'), ('BN', bn, 'RGB'), ('BR', br, 'R'), ('PD', pd, 'RGB'), ('PN', pn, 'RGB'), ('PR', pr, 'R'),
        ('CD', cd, 'RGB'), ('Seed', seed, ''), ('Bright', g.scalar('InteriorBrightness', 0.09), ''),
        ('BAvg', g.vector('BrickAvg', (*BRICK_AVG, 1)), 'RGB'), ('PAvg', g.vector('PlasterAvg', (*PLASTER_AVG, 1)), 'RGB'),
        ('BrickK', g.scalar('BrickStrength', 1.0), ''), ('PlasterK', g.scalar('PlasterStrength', 0.68), ''),
        ('MaxV', g.scalar('WindowMaxV', 1.0), '')],
        [('Emis', F3), ('Rough', F1), ('Nrm', F3)])
    g.out(c, unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(c, unreal.MaterialProperty.MP_EMISSIVE_COLOR, 'Emis')
    g.out(c, unreal.MaterialProperty.MP_ROUGHNESS, 'Rough')
    g.out(c, unreal.MaterialProperty.MP_NORMAL, 'Nrm')
    return g.finish()


PLINTH_HLSL = r'''
// Plinth UV (the web's): 1 = 12 m along the wall, 1 = 1.425 m, v growing downward from the plinth top.
// Basement windows of the web's plinth texture: x 98..158 / 256 of each 3 m bay, y 30..64 / 122.
float2 p = float2(frac(UV.x * 4.0) * 3.0, frac(UV.y) * 1.425);
float ox0 = 1.148, ox1 = 1.852, oy0 = 0.35, oy1 = 0.75, D = 0.08;
float inOpen = step(ox0, p.x) * step(p.x, ox1) * step(oy0, p.y) * step(p.y, oy1);
float3 d = -normalize(V);
float2 q = p + d.xy / max(-d.z, 1e-3) * D;
float inQ = step(ox0, q.x) * step(q.x, ox1) * step(oy0, q.y) * step(q.y, oy1);
float edge = min(min(q.x - ox0, ox1 - q.x), min(q.y - oy0, oy1 - q.y));
float frame = step(edge, 0.045) + step(abs(q.x - 1.5), 0.025);
float reveal = inOpen * (1.0 - inQ), atGlass = inOpen * inQ, wall = 1.0 - inOpen;
// Darker, damp concrete near the ground.
float damp = smoothstep(1.1, 1.425, p.y) * 0.25;
float3 wallC = CD * Tint * (1.0 - damp);
Rough = wall * CR + reveal * CR + atGlass * lerp(0.08, 0.5, saturate(frame));
Nrm = normalize(lerp(float3(0.0, 0.0, 1.0), CN, wall + reveal));
return wall * wallC + reveal * wallC * 0.7 + atGlass * lerp(float3(0.02, 0.022, 0.025), float3(0.55, 0.55, 0.53), saturate(frame));
'''


def plinth_master():
    g = Graph('M_HK_Plinth')
    tc = g.node(unreal.MaterialExpressionTextureCoordinate)
    cuv = g.mul(tc, g.mask(g.vector('ConcreteScale', (12.0 / 2.71, 1.425 / 2.71, 0, 0)), 'rg'))
    cd = g.tex('ConcreteAlbedo', f'{LIB_DIR}/T_Concrete_D', cuv, 'color')
    cn = g.tex('ConcreteNormal', f'{LIB_DIR}/T_Concrete_N', cuv, 'normal')
    cr = g.tex('ConcreteRough', f'{LIB_DIR}/T_Concrete_R', cuv, 'mask')
    v = g.to_tangent(g.node(unreal.MaterialExpressionCameraVectorWS))
    c = g.custom(PLINTH_HLSL, F3, [('UV', tc, ''), ('V', v, ''), ('CD', cd, 'RGB'), ('CN', cn, 'RGB'), ('CR', cr, 'R'),
                                   ('Tint', g.vector('Tint', (1.45, 1.6, 2.25, 1)), 'RGB')], [('Rough', F1), ('Nrm', F3)])
    g.out(c, unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(c, unreal.MaterialProperty.MP_ROUGHNESS, 'Rough')
    g.out(c, unreal.MaterialProperty.MP_NORMAL, 'Nrm')
    return g.finish()


ROOF_UV_HLSL = r'''
// Rows of slates and tiles follow each roof face: u along the eave, v down the slope (cm -> tiles).
float2 s = length(N.xy) > 0.05 ? normalize(N.xy) : float2(0.0, 1.0);
float2 a = float2(-s.y, s.x);
float c = max(N.z, 0.3);
Tan = float3(a, 0.0);
return float2(dot(P.xy, a), -dot(P.xy, s) / c) / Size;
'''

ROOF_HLSL = r'''
// Slate (dark grey) or red clay tiles from the building map; flat roofs are bitumen. Normal maps are turned from
// the face's own frame (T along the eave) into world space.
float flat = step(0.97, N.z);
float tile = Look.g * (1.0 - flat);
float3 col = lerp(SD * float3(1.05, 1.08, 1.12), TD * float3(0.95, 0.95, 0.95), tile);
float3 nt = lerp(SN, TN, tile);
float r = lerp(SR, TR, tile);
col = lerp(col, BD * 0.55, flat);
nt = lerp(nt, BN, flat);
r = lerp(r, 0.9, flat);
// Large-scale weathering: patches a little lighter or darker.
float w = sin(P.x * 0.0031 + sin(P.y * 0.0023) * 2.0) * sin(P.y * 0.0027 + 1.3);
col *= 0.9 + 0.1 * w;
float3 T = normalize(Tg - N * dot(Tg, N));
float3 B = cross(N, T);
WN = normalize(T * nt.x + B * nt.y + N * nt.z);
Rough = r;
return col;
'''


def roof_master():
    g = Graph('M_HK_Roof')
    g.m.set_editor_property('tangent_space_normal', False)
    row0, row1, idt, wp, wn = building_lookup(g, 0.0)
    uvs = {}
    for key, size in (('Slate', 400.0), ('Tiles', 250.0)):
        uvs[key] = g.custom(ROOF_UV_HLSL, F2, [('P', wp, ''), ('N', wn, ''), ('Size', g.scalar(f'{key}Size', size), '')], [('Tan', F3)])
    wxy = g.mul(g.mask(wp, 'rg'), g.scalar('BitumenScale', 1.0 / 404.0))
    sd = g.tex('SlateAlbedo', f'{LIB_DIR}/T_Slate_D', uvs['Slate'], 'color')
    sn = g.tex('SlateNormal', f'{LIB_DIR}/T_Slate_N', uvs['Slate'], 'normal')
    sr = g.tex('SlateRough', f'{LIB_DIR}/T_Slate_R', uvs['Slate'], 'mask')
    td = g.tex('TilesAlbedo', f'{LIB_DIR}/T_Tiles_D', uvs['Tiles'], 'color')
    tn = g.tex('TilesNormal', f'{LIB_DIR}/T_Tiles_N', uvs['Tiles'], 'normal')
    tr = g.tex('TilesRough', f'{LIB_DIR}/T_Tiles_R', uvs['Tiles'], 'mask')
    bd = g.tex('BitumenAlbedo', f'{LIB_DIR}/T_Bitumen_D', wxy, 'color')
    bn = g.tex('BitumenNormal', f'{LIB_DIR}/T_Bitumen_N', wxy, 'normal')
    c = g.custom(ROOF_HLSL, F3, [('P', wp, ''), ('N', wn, ''), ('Tg', uvs['Slate'], 'Tan'), ('Look', row1, 'RGBA'),
                                 ('SD', sd, 'RGB'), ('SN', sn, 'RGB'), ('SR', sr, 'R'), ('TD', td, 'RGB'), ('TN', tn, 'RGB'), ('TR', tr, 'R'),
                                 ('BD', bd, 'RGB'), ('BN', bn, 'RGB')], [('WN', F3), ('Rough', F1)])
    g.out(c, unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(c, unreal.MaterialProperty.MP_NORMAL, 'WN')
    g.out(c, unreal.MaterialProperty.MP_ROUGHNESS, 'Rough')
    return g.finish()


GRASS_WPO_HLSL = r'''
// H = height on the card (vertex colour red), Tm = time. Sway grows toward the top; near the fade distance the
// cards sink into the ground instead of popping out (Fade = PerInstanceFadeAmount: 1 fully shown .. 0 at the cull
// distance).
float ph = dot(WP.xy, float2(0.013, 0.009));
float sway = (sin(Tm * 1.9 + ph) * 0.7 + sin(Tm * 4.3 + ph * 2.1) * 0.3) * Amp * H;
return float3(sway, sway * 0.6, -(H * 14.0 + 1.0) * (1.0 - Fade));
'''

GRASS_COLOUR_HLSL = r'''
// The tuft's light and dark blades around the colour of the lawn texture under the card (so the cards match the
// ground around them), a little variety per card, darker toward the roots where blades shade each other, and at
// the very root the ground itself so the cards grow out of it without a seam.
float3 c = A / AAvg * Ground * Match * (0.88 + 0.24 * G);
c *= lerp(0.55, 1.05, saturate(H * 1.4));
return lerp(Ground * Match * 0.8, c, saturate(H * 4.0));
'''


def grass_cards():
    """The 3D lawn (AHKGame::UpdateGrass): the Blender card clump and its material."""
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
    g = Graph('M_HK_GrassCards')   # one-sided: the clump has both sides as triangles, all with upward normals
    g.m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
    g.m.set_editor_property('opacity_mask_clip_value', 0.4)
    tc = g.node(unreal.MaterialExpressionTextureCoordinate)
    atlas = g.tex('Cards', f'{LIB_DIR}/T_GrassCards_D', tc, 'color')
    wp = g.node(unreal.MaterialExpressionWorldPosition)
    guv = g.mul(g.mask(wp, 'rg'), g.scalar('GroundScale', 1.0 / 250.0))
    ground = g.tex('Lawn', f'{LIB_DIR}/T_Lawn_D', guv, 'color')
    vc = g.node(unreal.MaterialExpressionVertexColor)
    col = g.custom(GRASS_COLOUR_HLSL, F3, [('A', atlas, 'RGB'), ('AAvg', g.vector('AtlasAvg', (0.0828, 0.0983, 0.0236, 1)), 'RGB'),
                                           ('Match', g.scalar('GroundMatch', 1.1), ''), ('G', vc, 'G'), ('H', vc, 'R'), ('Ground', ground, 'RGB')])
    g.out(col, unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(atlas, unreal.MaterialProperty.MP_OPACITY_MASK, 'A')
    g.out(g.scalar('Roughness', 0.8), unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(g.scalar('Specular', 0.15), unreal.MaterialProperty.MP_SPECULAR)
    w = g.custom(GRASS_WPO_HLSL, F3, [('H', vc, 'R'), ('WP', wp, ''), ('Tm', g.node(unreal.MaterialExpressionTime), ''),
                                      ('Amp', g.scalar('WindAmplitude', 2.5), ''), ('Fade', g.node(unreal.MaterialExpressionPerInstanceFadeAmount), '')])
    g.out(w, unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    m = g.finish()
    sm = unreal.load_asset('/Game/Haarklous/Meshes/grass/SM_GrassClump')
    if sm:
        sm.set_material(0, m)
        EAL.save_loaded_asset(sm)
        log('grass cards ready')
    else:
        log('grass clump missing')


def reparent(mi_name, master, vectors=None, scalars=None):
    mi = unreal.load_asset(f'{WEB_DIR}/{mi_name}')
    if not mi:
        log('missing', mi_name); return
    mi.set_editor_property('parent', master)
    MEL.clear_all_material_instance_parameters(mi)
    for k, v in (vectors or {}).items():
        MEL.set_material_instance_vector_parameter_value(mi, k, unreal.LinearColor(*v))
    for k, v in (scalars or {}).items():
        MEL.set_material_instance_scalar_parameter_value(mi, k, v)
    MEL.update_material_instance(mi)
    EAL.save_loaded_asset(mi)
    return mi


def ground():
    """The terrain keeps M_HK_Ground; its lawn and asphalt layers become photo scans."""
    for which in ('near', 'far'):
        mi = unreal.load_asset(f'{MAT_DIR}/MI_Ground_{which}')
        if not mi:
            log('missing ground', which); continue
        for layer, key, tile in (('Grass', 'Lawn', 250.0), ('Asphalt', 'Asphalt', 300.0)):
            for kind in ('D', 'N', 'R'):
                MEL.set_material_instance_texture_parameter_value(mi, f'{layer}{kind}', unreal.load_asset(f'{LIB_DIR}/T_{key}_{kind}'))
            MEL.set_material_instance_scalar_parameter_value(mi, f'{layer}Tile', tile)
        # A lawn is fully rough: the scan's roughness map let the sky shine off it and turned it pale and frosty.
        MEL.set_material_instance_texture_parameter_value(mi, 'GrassR', unreal.load_asset('/Engine/EngineResources/WhiteSquareTexture'))
        # The scanned lawn is darker and greener than the painted one the map tint was set for.
        MEL.set_material_instance_vector_parameter_value(mi, 'GrassRef', unreal.LinearColor(0.10, 0.17, 0.045, 1))
        MEL.update_material_instance(mi)
        EAL.save_loaded_asset(mi)
    log('ground uses the lawn and asphalt scans')


def trees():
    # Crowns and bushes: the leaf cluster of scanned leaves (tools/make-leaf-atlas.py), three shades of green.
    for key, tint in (('MI_HK_LeavesA', (1.0, 1.0, 1.0)), ('MI_HK_LeavesB', (0.9, 1.0, 0.82)), ('MI_HK_LeavesBush', (0.8, 0.92, 0.78))):
        mi = unreal.load_asset(f'{MAT_DIR}/Foliage/{key}')
        if not mi:
            log('missing', key); continue
        MEL.set_material_instance_texture_parameter_value(mi, 'BaseMap', unreal.load_asset(f'{LIB_DIR}/T_Leaves_D'))
        MEL.set_material_instance_texture_parameter_value(mi, 'NormalMap', unreal.load_asset(f'{LIB_DIR}/T_Leaves_N'))
        MEL.set_material_instance_vector_parameter_value(mi, 'Color', unreal.LinearColor(*tint, 1))
        MEL.update_material_instance(mi)
        EAL.save_loaded_asset(mi)
    mi = unreal.load_asset(f'{MAT_DIR}/Foliage/MI_HK_Bark')
    if mi:
        MEL.set_material_instance_texture_parameter_value(mi, 'BaseMap', unreal.load_asset(f'{LIB_DIR}/T_Bark_D'))
        MEL.set_material_instance_texture_parameter_value(mi, 'NormalMap', unreal.load_asset(f'{LIB_DIR}/T_Bark_N'))
        MEL.set_material_instance_vector_parameter_value(mi, 'Color', unreal.LinearColor(1.1, 1.1, 1.1, 1))
        MEL.update_material_instance(mi)
        EAL.save_loaded_asset(mi)
        log('bark scan on the trees')


def brick_photo():
    """The web's own brick (lib.brick: the stair towers and the flat's two outer walls) in the same salmon brick as
    the blocks: 2.4 m web tile over the 2.01 m scan, tinted like M_HK_Building does for OSM #cb9c89."""
    g = Graph('M_HK_BrickPhoto')
    uv = g.mul(g.node(unreal.MaterialExpressionTextureCoordinate), g.mask(g.vector('UVScale', (1, 1, 0, 0)), 'rg'))
    d = g.tex('Albedo', f'{LIB_DIR}/T_Brick_D', uv, 'color')
    g.out(g.mul(d, g.vector('Color', (1, 1, 1, 1)), 'RGB', ''), unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(g.tex('Normal', f'{LIB_DIR}/T_Brick_N', uv, 'normal'), unreal.MaterialProperty.MP_NORMAL, 'RGB')
    g.out(g.tex('Rough', f'{LIB_DIR}/T_Brick_R', uv, 'mask'), unreal.MaterialProperty.MP_ROUGHNESS, 'R')
    return g.finish()


RAILING_HLSL = r'''
// Steel railing of the balconies: u = metres along the panel, v = height (0..1). Top and bottom rails, bars every
// 12 cm, two-sided and alpha-tested.
float v = frac(UV.y + 1e-4);
float rail = step(v, 0.05) + step(0.93, v);
float bar = step(frac(UV.x / 0.12), 0.16);
return saturate(rail + bar);
'''


def detail_master():
    """Plain tiling PBR for the building details: texture x Color, uvs in metres x UVScale, Metallic, RoughScale."""
    g = Graph('M_HK_DetailPBR')
    uv = g.mul(g.node(unreal.MaterialExpressionTextureCoordinate), g.mask(g.vector('UVScale', (1, 1, 0, 0)), 'rg'))
    d = g.tex('Albedo', f'{LIB_DIR}/T_Concrete_D', uv, 'color')
    g.out(g.mul(d, g.vector('Color', (1, 1, 1, 1)), 'RGB', ''), unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(g.tex('Normal', f'{LIB_DIR}/T_Concrete_N', uv, 'normal'), unreal.MaterialProperty.MP_NORMAL, 'RGB')
    g.out(g.mul(g.tex('Rough', f'{LIB_DIR}/T_Concrete_R', uv, 'mask'), g.scalar('RoughScale', 1.0), 'R', ''), unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(g.scalar('Metallic', 0.0), unreal.MaterialProperty.MP_METALLIC)
    return g.finish()


def railing_master():
    g = Graph('M_HK_Railing')
    g.m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
    g.m.set_editor_property('two_sided', True)
    g.m.set_editor_property('opacity_mask_clip_value', 0.5)
    bars = g.custom(RAILING_HLSL, F1, [('UV', g.node(unreal.MaterialExpressionTextureCoordinate), '')])
    g.out(bars, unreal.MaterialProperty.MP_OPACITY_MASK)
    g.out(g.vector('Color', (0.025, 0.025, 0.027, 1)), unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(g.scalar('Metallic', 0.6), unreal.MaterialProperty.MP_METALLIC)
    g.out(g.scalar('Roughness', 0.45), unreal.MaterialProperty.MP_ROUGHNESS)
    return g.finish()


def details():
    """Balconies, downpipes and chimneys (tools/blender/build_details.py), spawned by AHKGame::SpawnTrees."""
    dm, rm = detail_master(), railing_master()
    mis = {}
    for name, tex, scale, color, metal, rough in (
            ('MI_HK_DetailConcrete', 'Concrete', 1 / 2.71, (1.35, 1.45, 1.9), 0.0, 1.0),
            ('MI_HK_DetailPanel', 'Plaster', 1 / 2.23, (1.15, 1.25, 1.25), 0.0, 0.8),
            ('MI_HK_DetailBrick', 'Brick', 1 / 2.01, SALMON_TINT, 0.0, 1.0),
            ('MI_HK_DetailMetal', 'Concrete', 1 / 2.71, (1.2, 1.3, 1.6), 0.7, 0.5),
            ('MI_HK_DetailGranite', 'Concrete', 1 / 2.71, (1.25, 1.35, 1.75), 0.0, 1.0),   # granite grey (the granite scan has tile joints)
            ('MI_HK_PaintWhite', 'Plaster', 1 / 2.23, (1.55, 1.6, 1.65), 0.0, 0.75),
            ('MI_HK_PaintYellow', 'Plaster', 1 / 2.23, (1.75, 1.3, 0.22), 0.0, 0.75)):
        path = f'{MAT_DIR}/Details/{name}'
        if not EAL.does_asset_exist(path):
            AT.create_asset(name, f'{MAT_DIR}/Details', unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        mi = unreal.load_asset(path)
        MEL.set_material_instance_parent(mi, dm)
        for k, kind in (('Albedo', 'D'), ('Normal', 'N'), ('Rough', 'R')):
            MEL.set_material_instance_texture_parameter_value(mi, k, unreal.load_asset(f'{LIB_DIR}/T_{tex}_{kind}'))
        MEL.set_material_instance_vector_parameter_value(mi, 'UVScale', unreal.LinearColor(scale, scale, 0, 0))
        MEL.set_material_instance_vector_parameter_value(mi, 'Color', unreal.LinearColor(*color, 1))
        MEL.set_material_instance_scalar_parameter_value(mi, 'Metallic', metal)
        MEL.set_material_instance_scalar_parameter_value(mi, 'RoughScale', rough)
        MEL.update_material_instance(mi)
        EAL.save_loaded_asset(mi)
        mis[name] = mi
    mis['MI_HK_Railing'] = rm
    slots = {'SM_Balcony': ['MI_HK_DetailConcrete'],
             'SM_BalconyRail': ['MI_HK_Railing'],
             'SM_BalconyPanel': ['MI_HK_DetailConcrete', 'MI_HK_Railing', 'MI_HK_DetailPanel'],
             'SM_Downpipe': ['MI_HK_DetailMetal'],
             'SM_Gutter': ['MI_HK_DetailMetal'],
             'SM_Chimney': ['MI_HK_DetailBrick', 'MI_HK_DetailConcrete'],
             'SM_Kerb': ['MI_HK_DetailGranite'],
             'SM_RoadDash': ['MI_HK_PaintYellow'],
             'SM_ZebraStripe': ['MI_HK_PaintWhite']}
    for asset, names in slots.items():
        t = unreal.AssetImportTask()
        t.filename = os.path.join(ROOT, 'tools', 'out', 'fbx', f'details_{asset}.fbx')
        t.destination_path = '/Game/Haarklous/Meshes/details'
        t.destination_name = asset
        t.automated = True; t.replace_existing = True; t.save = False
        ui = unreal.FbxImportUI()
        ui.set_editor_property('import_mesh', True)
        ui.set_editor_property('import_as_skeletal', False)
        ui.set_editor_property('import_materials', False)
        ui.set_editor_property('import_textures', False)
        sd = ui.get_editor_property('static_mesh_import_data')
        sd.set_editor_property('combine_meshes', True)
        sd.set_editor_property('generate_lightmap_u_vs', True)
        sd.set_editor_property('auto_generate_collision', False)
        t.options = ui
        AT.import_asset_tasks([t])
        sm = unreal.load_asset(f'/Game/Haarklous/Meshes/details/{asset}')
        if not sm:
            log('missing detail mesh', asset); continue
        for k, nm in enumerate(names):
            sm.set_material(k, mis[nm])
        EAL.save_loaded_asset(sm)
    log('building details imported')


SALMON_TINT = tuple(0.85 * a / b for a, b in zip((0.5972, 0.3325, 0.2502), BRICK_AVG))

import_all()
reparent('MI_brick', brick_photo(), vectors={'UVScale': (2.4 / 2.01, 2.4 / 2.01, 0, 0), 'Color': (*SALMON_TINT, 1)})
building = building_master()
reparent('MI_facade', building)
# The web's plaster buildings (walls from the ground up, no windows, 1.6 m plaster uvs): the same facades, their
# own colour from the building map, windows from the first storey up (facade uv = 1.6 / 12 u, storeys counted
# from 1.425 m above the ground like the brick blocks).
reparent('MI_plaster_d9d2c3', building, vectors={'FacadeUVScale': (1.6 / 12, 1.6 / 2.85, 0, 0), 'FacadeUVOffset': (0, 1.5 - 1.6 / 2.85, 0, 0)})
reparent('MI_plinth', plinth_master())
reparent('MI_roof', roof_master())
ground()
trees()
details()
grass_cards()
