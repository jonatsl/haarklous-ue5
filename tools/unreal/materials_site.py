"""Materials and imports for the construction site (new feature, 28 Oct 2026). Run headless with
tools/unreal/run-python.ps1 after tools/blender/build_machines.py.

  M_HK_Dirt       the soil mesh (AHKSiteActor): topsoil (Poly Haven farm_soil), subsoil where digging went deeper
                  than 30 cm (excavated_soil_wall, vertex colour green), loose soil darker and rougher (red)
  M_HK_SitePaint  machine paint (Color) on a fine metal-plate texture, with grime toward the ground (vertex colour red
                  from build_machines.py) in the soil's colour
  M_HK_SitePBR    plain tiling PBR (texture x Color, roughness, metallic) for steel, rubber, concrete, containers ...
  M_HK_SiteGlow   lamps and beacons (emissive); M_HK_SiteGlass; M_HK_SiteMesh (masked fence mesh)
MI_Site_<slot> instances match the slot names of the Blender meshes. Then the machines (tools/out/fbx/site_*.fbx) and
the props and loads (site.fbx) are imported into /Game/Haarklous/Meshes/<rig>/ with those materials.
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
FBX = os.path.join(ROOT, 'tools', 'out', 'fbx')
TDIR = '/Game/Haarklous/Textures/Lib'
MDIR = '/Game/Haarklous/Materials'
SDIR = '/Game/Haarklous/Materials/Site'
RIGS = ['site_dozer', 'site_excavator', 'site_loader', 'site_hauler', 'site_tipper', 'site_crane', 'site_tower']


def log(*a):
    print('HK:', *a)


def tex(src, name, kind):
    t = unreal.AssetImportTask()
    t.filename = src; t.destination_path = TDIR; t.destination_name = name
    t.automated = True; t.replace_existing = True; t.save = False
    AT.import_asset_tasks([t])
    a = unreal.load_asset(f'{TDIR}/{name}')
    if kind == 'N':
        a.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_NORMALMAP)
        a.set_editor_property('srgb', False); a.set_editor_property('flip_green_channel', True)
    elif kind == 'R':
        a.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_MASKS); a.set_editor_property('srgb', False)
    a.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_WORLD)
    EAL.save_loaded_asset(a)
    return a


def import_textures():
    PH = {'Soil': 'farm_soil', 'Subsoil': 'excavated_soil_wall', 'Gravel': 'gravelly_sand', 'Plate': 'metal_plate',
          'Corrugated': 'corrugated_iron_02', 'Rust': 'rusty_metal_02'}
    for key, a in PH.items():
        for kind, suf in (('D', 'diff_2k.jpg'), ('N', 'nor_gl_2k.jpg'), ('R', 'rough_2k.jpg')):
            tex(os.path.join(LIB, a, f'{a}_{suf}'), f'T_{key}_{kind}', kind)
    tex(os.path.join(TEX, 'T_SiteSign.png'), 'T_SiteSign', 'D')
    tex(os.path.join(TEX, 'T_SiteBanner.png'), 'T_SiteBanner', 'D')
    log('site textures imported')


class G:
    def __init__(self, name):
        if EAL.does_asset_exist(f'{MDIR}/{name}'):
            EAL.delete_asset(f'{MDIR}/{name}')
        self.m = AT.create_asset(name, MDIR, unreal.Material, unreal.MaterialFactoryNew())
        self.y = 0

    def n(self, cls):
        self.y += 140
        return MEL.create_material_expression(self.m, cls, -1200, self.y)

    def c(self, a, ao, b, bi):
        MEL.connect_material_expressions(a, ao, b, bi)

    def out(self, a, prop, ao=''):
        MEL.connect_material_property(a, ao, prop)

    def scalar(self, name, v):
        e = self.n(unreal.MaterialExpressionScalarParameter); e.set_editor_property('parameter_name', name); e.set_editor_property('default_value', v); return e

    def vector(self, name, v):
        e = self.n(unreal.MaterialExpressionVectorParameter); e.set_editor_property('parameter_name', name); e.set_editor_property('default_value', unreal.LinearColor(*v)); return e

    def tex(self, name, path, uv, kind):
        e = self.n(unreal.MaterialExpressionTextureSampleParameter2D)
        e.set_editor_property('parameter_name', name)
        e.set_editor_property('texture', unreal.load_asset(path))
        e.set_editor_property('sampler_type', {'color': unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, 'normal': unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL,
                                               'mask': unreal.MaterialSamplerType.SAMPLERTYPE_MASKS}[kind])
        e.set_editor_property('sampler_source', unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS)
        if uv is not None:
            self.c(uv, '', e, 'UVs')
        return e

    def mul(self, a, b, ao='', bo=''):
        e = self.n(unreal.MaterialExpressionMultiply); self.c(a, ao, e, 'A'); self.c(b, bo, e, 'B'); return e

    def lerp(self, a, b, t, ao='', bo='', to=''):
        e = self.n(unreal.MaterialExpressionLinearInterpolate); self.c(a, ao, e, 'A'); self.c(b, bo, e, 'B'); self.c(t, to, e, 'Alpha'); return e

    def uv(self, scale_name, scale):
        tc = self.n(unreal.MaterialExpressionTextureCoordinate)
        return self.mul(tc, self.scalar(scale_name, scale))

    def finish(self, instanced=True):
        self.m.set_editor_property('used_with_instanced_static_meshes', instanced)
        MEL.recompile_material(self.m)
        EAL.save_loaded_asset(self.m)
        log('created', self.m.get_name())
        return self.m


def dirt_material():
    g = G('M_HK_Dirt')
    vc = g.n(unreal.MaterialExpressionVertexColor)

    def mask(node, chans, out=''):
        m = g.n(unreal.MaterialExpressionComponentMask)
        for ch in 'rgba':
            m.set_editor_property(ch, ch in chans)
        g.c(node, out, m, '')
        return m

    def add(a, b):
        e = g.n(unreal.MaterialExpressionAdd); g.c(a, '', e, 'A'); g.c(b, '', e, 'B'); return e

    def soil_and_sub(uv_soil, uv_sub, tag, rough=True):
        """Topsoil and subsoil (where digging went deeper than 30 cm, vertex colour green) at the given uvs."""
        sd = g.tex(f'SoilD{tag}', f'{TDIR}/T_Soil_D', uv_soil, 'color')
        bd = g.tex(f'SubD{tag}', f'{TDIR}/T_Subsoil_D', uv_sub, 'color')
        col = g.lerp(g.mul(sd, g.vector('SoilTint', (.95, .9, .85, 1)), 'RGB', ''), g.mul(bd, g.vector('SubTint', (1.0, .95, .9, 1)), 'RGB', ''), vc, '', '', 'G')
        return col
    # Top projection: the mesh uv (local metres / 2.5).
    uv = g.uv('SoilScale', 1.0)
    uv2 = g.uv('SubScale', .8)
    top = soil_and_sub(uv, uv2, '')
    sn = g.tex('SoilN', f'{TDIR}/T_Soil_N', uv, 'normal'); sr = g.tex('SoilR', f'{TDIR}/T_Soil_R', uv, 'mask')
    bn = g.tex('SubN', f'{TDIR}/T_Subsoil_N', uv2, 'normal'); br = g.tex('SubR', f'{TDIR}/T_Subsoil_R', uv2, 'mask')
    top_n = g.lerp(sn, bn, vc, 'RGB', 'RGB', 'G')
    top_r = g.lerp(sr, br, vc, 'R', 'R', 'G')
    # Side projections on the world's x and y axes (cm -> 2.5 m tiles) for pit walls and heap flanks, so steep faces
    # are not smeared; blended by the normal (tri-planar, weights |n|^4).
    wp = g.n(unreal.MaterialExpressionWorldPosition)
    k = g.scalar('SideScale', 1 / 250.0)
    uvx = g.mul(mask(wp, 'gb'), k)
    uvy = g.mul(mask(wp, 'rb'), k)
    side_x = soil_and_sub(uvx, uvx, 'X')
    side_y = soil_and_sub(uvy, uvy, 'Y')
    nx = g.tex('SubNX', f'{TDIR}/T_Subsoil_N', uvx, 'normal'); ny = g.tex('SubNY', f'{TDIR}/T_Subsoil_N', uvy, 'normal')
    nws = g.n(unreal.MaterialExpressionVertexNormalWS)
    an = g.n(unreal.MaterialExpressionAbs); g.c(nws, '', an, '')
    w2 = g.mul(an, an); w4 = g.mul(w2, w2)
    one = g.n(unreal.MaterialExpressionConstant3Vector); one.set_editor_property('constant', unreal.LinearColor(1, 1, 1, 1))
    tot = g.n(unreal.MaterialExpressionDotProduct); g.c(w4, '', tot, 'A'); g.c(one, '', tot, 'B')
    w = g.n(unreal.MaterialExpressionDivide); g.c(w4, '', w, 'A'); g.c(tot, '', w, 'B')
    wx, wy, wz = mask(w, 'r'), mask(w, 'g'), mask(w, 'b')
    col = add(add(g.mul(top, wz), g.mul(side_x, wx)), g.mul(side_y, wy))
    loose = g.mul(col, g.scalar('LooseDark', .78))
    col = g.lerp(col, loose, vc, '', '', 'R')
    g.out(col, unreal.MaterialProperty.MP_BASE_COLOR)
    nrm = add(add(g.mul(top_n, wz), g.mul(nx, wx, 'RGB', '')), g.mul(ny, wy, 'RGB', ''))
    g.out(nrm, unreal.MaterialProperty.MP_NORMAL)
    g.out(g.lerp(g.scalar('SideRough', .92), top_r, wz), unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(g.scalar('Specular', .3), unreal.MaterialProperty.MP_SPECULAR)
    return g.finish(False)


def paint_material():
    g = G('M_HK_SitePaint')
    uv = g.uv('PlateScale', 1 / 1.5)
    pd = g.tex('PlateD', f'{TDIR}/T_Plate_D', uv, 'color')
    pn = g.tex('PlateN', f'{TDIR}/T_Plate_N', uv, 'normal')
    color = g.vector('Color', (.75, .45, .02, 1))
    # Paint over the plate: its luminance varies the paint a little (scratches and streaks).
    lum = g.n(unreal.MaterialExpressionDesaturation); g.c(pd, 'RGB', lum, '')
    paint = g.mul(color, g.lerp(g.scalar('PlateMin', .82), g.scalar('PlateMax', 1.12), lum, '', '', ''), '', '')
    vc = g.n(unreal.MaterialExpressionVertexColor)
    grime_amt = g.mul(vc, g.scalar('Grime', .85), 'R', '')
    soil = g.tex('SoilD', f'{TDIR}/T_Soil_D', g.uv('SoilScale', .5), 'color')
    dirty = g.mul(soil, g.vector('GrimeTint', (.75, .68, .6, 1)), 'RGB', '')
    g.out(g.lerp(paint, dirty, grime_amt), unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(pn, unreal.MaterialProperty.MP_NORMAL, 'RGB')
    g.out(g.lerp(g.scalar('Roughness', .42), g.scalar('GrimeRough', .9), grime_amt), unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(g.scalar('Metallic', 0.0), unreal.MaterialProperty.MP_METALLIC)
    return g.finish()


def pbr_material():
    g = G('M_HK_SitePBR')
    uv = g.uv('UVScale', 1 / 1.5)
    d = g.tex('Albedo', f'{TDIR}/T_Plate_D', uv, 'color')
    vc = g.n(unreal.MaterialExpressionVertexColor)
    soil = g.tex('SoilD', f'{TDIR}/T_Soil_D', g.uv('SoilScale', .5), 'color')
    base = g.mul(d, g.vector('Color', (1, 1, 1, 1)), 'RGB', '')
    grime_amt = g.mul(vc, g.scalar('Grime', .0), 'R', '')
    g.out(g.lerp(base, g.mul(soil, g.vector('GrimeTint', (.75, .68, .6, 1)), 'RGB', ''), grime_amt), unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(g.tex('Normal', f'{TDIR}/T_Plate_N', uv, 'normal'), unreal.MaterialProperty.MP_NORMAL, 'RGB')
    g.out(g.mul(g.tex('Rough', f'{TDIR}/T_Plate_R', uv, 'mask'), g.scalar('RoughScale', 1.0), 'R', ''), unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(g.scalar('Metallic', 0.0), unreal.MaterialProperty.MP_METALLIC)
    return g.finish()


def glow_material():
    g = G('M_HK_SiteGlow')
    c = g.vector('Color', (1, .9, .7, 1))
    g.out(c, unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(g.mul(c, g.scalar('Glow', 6.0)), unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    g.out(g.scalar('Roughness', .2), unreal.MaterialProperty.MP_ROUGHNESS)
    return g.finish()


def glass_material():
    g = G('M_HK_SiteGlass')
    g.out(g.vector('Color', (.02, .025, .03, 1)), unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(g.scalar('Roughness', .05), unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(g.scalar('Specular', .9), unreal.MaterialProperty.MP_SPECULAR)
    return g.finish()


def mesh_material():
    """Heras fence mesh: a wire grid (10 x 25 cm) cut out of a quad with an opacity mask, galvanised."""
    g = G('M_HK_SiteMesh')
    g.m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
    g.m.set_editor_property('two_sided', True)
    wp = g.n(unreal.MaterialExpressionWorldPosition)
    c = g.n(unreal.MaterialExpressionCustom)
    c.set_editor_property('code', 'float2 g = abs(frac(float2(P.x + P.y, P.z) / float2(10.0, 25.0)) - 0.5);\nreturn step(0.44, max(g.x * 1.0, g.y));')
    c.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    ci = unreal.CustomInput(); ci.set_editor_property('input_name', 'P'); c.set_editor_property('inputs', [ci])
    g.c(wp, '', c, 'P')
    g.out(c, unreal.MaterialProperty.MP_OPACITY_MASK)
    g.out(g.vector('Color', (.55, .56, .55, 1)), unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(g.scalar('Metallic', .8), unreal.MaterialProperty.MP_METALLIC)
    g.out(g.scalar('Roughness', .45), unreal.MaterialProperty.MP_ROUGHNESS)
    return g.finish()


def image_material():
    g = G('M_HK_SiteImage')
    tc = g.n(unreal.MaterialExpressionTextureCoordinate)
    d = g.tex('Image', f'{TDIR}/T_SiteSign', None, 'color')
    # Box-projected metres -> the panel: scale/offset per instance (the parameters' x and y; uv is a float2).
    def rg(node):
        m = g.n(unreal.MaterialExpressionComponentMask); m.set_editor_property('r', True); m.set_editor_property('g', True)
        g.c(node, '', m, '')
        return m
    uv = g.n(unreal.MaterialExpressionAdd)
    g.c(g.mul(tc, rg(g.vector('UVScale', (1, 1, 0, 0))), '', ''), '', uv, 'A'); g.c(rg(g.vector('UVOffset', (0, 0, 0, 0))), '', uv, 'B')
    g.c(uv, '', d, 'UVs')
    g.out(d, unreal.MaterialProperty.MP_BASE_COLOR, 'RGB')
    g.out(g.scalar('Roughness', .5), unreal.MaterialProperty.MP_ROUGHNESS)
    return g.finish()


def mi(name, parent, vectors=None, scalars=None, textures=None):
    path = f'{SDIR}/{name}'
    if not EAL.does_asset_exist(path):
        AT.create_asset(name, SDIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    m = unreal.load_asset(path)
    MEL.set_material_instance_parent(m, parent)
    MEL.clear_all_material_instance_parameters(m)
    for k, v in (vectors or {}).items():
        MEL.set_material_instance_vector_parameter_value(m, k, unreal.LinearColor(*v))
    for k, v in (scalars or {}).items():
        MEL.set_material_instance_scalar_parameter_value(m, k, v)
    for k, v in (textures or {}).items():
        MEL.set_material_instance_texture_parameter_value(m, k, unreal.load_asset(f'{TDIR}/{v}'))
    MEL.update_material_instance(m)
    EAL.save_loaded_asset(m)
    return m


def instances(paint, pbr, glow, glass, mesh, image):
    lin = lambda r, g, b: tuple(((c / 255.0) / 12.92 if c / 255.0 <= .04045 else ((c / 255.0 + .055) / 1.055) ** 2.4) for c in (r, g, b))
    out = {}
    out['MI_Site_Yellow'] = mi('MI_Site_Yellow', paint, {'Color': (*lin(236, 168, 0), 1)})           # machine yellow
    out['MI_Site_White'] = mi('MI_Site_White', paint, {'Color': (*lin(232, 232, 228), 1)}, {'Grime': .6})
    out['MI_Site_Blue'] = mi('MI_Site_Blue', paint, {'Color': (*lin(22, 52, 120), 1)}, {'Grime': .3})
    out['MI_Site_Red'] = mi('MI_Site_Red', paint, {'Color': (*lin(150, 30, 22), 1)}, {'Grime': .5, 'Roughness': .6})
    out['MI_Site_Dark'] = mi('MI_Site_Dark', paint, {'Color': (*lin(30, 30, 30), 1)}, {'Roughness': .6})
    out['MI_Site_Steel'] = mi('MI_Site_Steel', pbr, {'Color': (.55, .55, .55, 1)}, {'Metallic': .9, 'RoughScale': .9, 'Grime': .6}, {'Albedo': 'T_Rust_D', 'Normal': 'T_Rust_N', 'Rough': 'T_Rust_R'})
    out['MI_Site_Chrome'] = mi('MI_Site_Chrome', pbr, {'Color': (.9, .9, .9, 1)}, {'Metallic': 1.0, 'RoughScale': .25})
    out['MI_Site_Rubber'] = mi('MI_Site_Rubber', pbr, {'Color': (.08, .08, .08, 1)}, {'RoughScale': 1.3, 'Grime': .8})
    out['MI_Site_Galv'] = mi('MI_Site_Galv', pbr, {'Color': (.75, .76, .75, 1)}, {'Metallic': .85, 'RoughScale': .7})
    out['MI_Site_Concrete'] = mi('MI_Site_Concrete', pbr, {'Color': (1.25, 1.25, 1.2, 1)}, {'RoughScale': 1.2},
                                 {'Albedo': 'T_Concrete_D', 'Normal': 'T_Concrete_N', 'Rough': 'T_Concrete_R'})
    out['MI_Site_Container'] = mi('MI_Site_Container', pbr, {'Color': (.35, .55, 1.1, 1), 'UVScale': (.5, .5, 0, 0)}, {'Metallic': .3},
                                  {'Albedo': 'T_Corrugated_D', 'Normal': 'T_Corrugated_N', 'Rough': 'T_Corrugated_R'})
    out['MI_Site_Container2'] = mi('MI_Site_Container2', pbr, {'Color': (1.4, 1.4, 1.35, 1)}, {'Metallic': .3},
                                   {'Albedo': 'T_Corrugated_D', 'Normal': 'T_Corrugated_N', 'Rough': 'T_Corrugated_R'})
    out['MI_Site_Door'] = mi('MI_Site_Door', paint, {'Color': (*lin(90, 90, 92), 1)}, {'Grime': .3})
    out['MI_Site_Toilet'] = mi('MI_Site_Toilet', paint, {'Color': (*lin(30, 110, 60), 1)}, {'Grime': .2, 'Roughness': .5})
    out['MI_Site_Toilet2'] = mi('MI_Site_Toilet2', paint, {'Color': (*lin(220, 220, 215), 1)}, {'Grime': .2})
    out['MI_Site_Skip'] = mi('MI_Site_Skip', pbr, {'Color': (.9, .45, .08, 1)}, {'Grime': .7}, {'Albedo': 'T_Rust_D', 'Normal': 'T_Rust_N', 'Rough': 'T_Rust_R'})
    out['MI_Site_Wood'] = mi('MI_Site_Wood', paint, {'Color': (*lin(150, 115, 75), 1)}, {'Roughness': .85, 'Grime': .4})
    out['MI_Site_Brick'] = mi('MI_Site_Brick', pbr, {'Color': (1.0, .75, .65, 1), 'UVScale': (.5, .5, 0, 0)}, {},
                              {'Albedo': 'T_Brick_D', 'Normal': 'T_Brick_N', 'Rough': 'T_Brick_R'})
    out['MI_Site_Soil'] = mi('MI_Site_Soil', pbr, {'Color': (.85, .8, .75, 1), 'UVScale': (.6, .6, 0, 0)}, {'RoughScale': 1.1},
                             {'Albedo': 'T_Soil_D', 'Normal': 'T_Soil_N', 'Rough': 'T_Soil_R'})
    out['MI_Site_Light'] = mi('MI_Site_Light', glow, {'Color': (1, .95, .85, 1)}, {'Glow': 3.0})
    out['MI_Site_Beacon'] = mi('MI_Site_Beacon', glow, {'Color': (1, .45, .02, 1)}, {'Glow': 4.0})
    out['MI_Site_Glass'] = mi('MI_Site_Glass', glass)
    out['MI_Site_Mesh'] = mi('MI_Site_Mesh', mesh)
    out['MI_Site_Banner'] = mi('MI_Site_Banner', image, {'UVScale': (1 / 3.3, -1 / .9, 0, 0), 'UVOffset': (.5, .5 + 1.2 / .9, 0, 0)}, {},
                               {'Image': 'T_SiteBanner'})
    out['MI_Site_Sign'] = mi('MI_Site_Sign', image, {'UVScale': (1.0, -1 / .7, 0, 0), 'UVOffset': (.5, .5 + 1.8 / .7, 0, 0)}, {},
                             {'Image': 'T_SiteSign'})
    return out


def import_fbx(fbx, dest, mats):
    t = unreal.AssetImportTask()
    t.filename = fbx; t.destination_path = dest
    t.automated = True; t.replace_existing = True; t.save = False
    ui = unreal.FbxImportUI()
    ui.set_editor_property('import_mesh', True); ui.set_editor_property('import_as_skeletal', False)
    ui.set_editor_property('import_materials', False); ui.set_editor_property('import_textures', False)
    sd = ui.get_editor_property('static_mesh_import_data')
    sd.set_editor_property('combine_meshes', False)
    sd.set_editor_property('generate_lightmap_u_vs', False)
    sd.set_editor_property('auto_generate_collision', False)
    sd.set_editor_property('vertex_color_import_option', unreal.VertexColorImportOption.REPLACE)
    sd.set_editor_property('normal_import_method', unreal.FBXNormalImportMethod.FBXNIM_COMPUTE_NORMALS)
    t.options = ui
    AT.import_asset_tasks([t])
    n = 0
    for p in EAL.list_assets(dest, recursive=False):
        sm = unreal.load_asset(p)
        if not isinstance(sm, unreal.StaticMesh):
            continue
        for i, sl in enumerate(sm.get_editor_property('static_materials')):
            key = str(sl.get_editor_property('material_slot_name'))
            if key in mats:
                sm.set_material(i, mats[key])
        EAL.save_loaded_asset(sm)
        n += 1
    return n


def main():
    import_textures()
    paint, pbr, glow, glass, mesh, image = paint_material(), pbr_material(), glow_material(), glass_material(), mesh_material(), image_material()
    dirt_material()
    mats = instances(paint, pbr, glow, glass, mesh, image)
    for rig in RIGS:
        log(rig, import_fbx(os.path.join(FBX, rig + '.fbx'), f'/Game/Haarklous/Meshes/{rig}', mats), 'meshes')
    log('site', import_fbx(os.path.join(FBX, 'site.fbx'), '/Game/Haarklous/Meshes/site', mats), 'meshes')


main()
