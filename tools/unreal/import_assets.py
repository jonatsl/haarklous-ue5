"""Import the Blender-built assets into the Unreal project (run headless with tools/unreal/run-python.ps1).

    powershell -File tools\\unreal\\run-python.ps1 tools\\unreal\\import_assets.py -ScriptArgs "car apartment"

1. Textures: tools/out/web-export/textures/<id>.png -> /Game/Haarklous/Textures/Web/T_Web_<id> (only those the materials use)
2. Master material /Game/Haarklous/Materials/M_HK_Master (parameters for colour, maps, roughness, metalness,
   emissive, opacity, vertex colour) and one material instance per web material key (/Game/Haarklous/Materials/Web/MI_*).
   Better hand-tuned materials (tools/unreal/materials_better.py) override these later by key.
3. Meshes: tools/out/fbx/<rig>.fbx -> /Game/Haarklous/Meshes/<rig>/SM_*, one static mesh per Blender object,
   lightmap UVs generated, material slots set from their names (= material keys). Meshes the game aims at (doors,
   drinks, locked doors, apartment and stairwell walls) get complex collision for line traces.
"""
import json
import os

import unreal

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
EXPORT = os.path.join(ROOT, 'tools', 'out', 'web-export')
PLAN = os.path.join(ROOT, 'tools', 'out', 'build-plan')
FBX = os.path.join(ROOT, 'tools', 'out', 'fbx')
ARGS = [a for a in os.environ.get('HK_ARGS', '').split() if a]
AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
MAT_DIR = '/Game/Haarklous/Materials'
TRACE_RIGS = ('apartment', 'stair_')   # meshes used for shooting / interaction traces


def log(*a):
    print('HK:', *a)


def srgb_to_linear(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def hex_color(h, a=1.0):
    if h is None:
        return unreal.LinearColor(1, 1, 1, a)
    r, g, b = ((h >> 16) & 255) / 255.0, ((h >> 8) & 255) / 255.0, (h & 255) / 255.0
    return unreal.LinearColor(srgb_to_linear(r), srgb_to_linear(g), srgb_to_linear(b), a)


# ---------- Master material ----------
def master_material():
    path = f'{MAT_DIR}/M_HK_Master'
    if EAL.does_asset_exist(path):
        return unreal.load_asset(path)
    mat = AT.create_asset('M_HK_Master', MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_OPAQUE)
    y = 0

    def node(cls, x, yy):
        return MEL.create_material_expression(mat, cls, x, yy)

    def vparam(name, default, x, yy):
        n = node(unreal.MaterialExpressionVectorParameter, x, yy); n.set_editor_property('parameter_name', name); n.set_editor_property('default_value', default); return n

    def sparam(name, default, x, yy):
        n = node(unreal.MaterialExpressionScalarParameter, x, yy); n.set_editor_property('parameter_name', name); n.set_editor_property('default_value', default); return n

    def tparam(name, x, yy, normal=False):
        n = node(unreal.MaterialExpressionTextureSampleParameter2D, x, yy); n.set_editor_property('parameter_name', name)
        default = '/Engine/EngineMaterials/DefaultNormal' if normal else '/Engine/EngineResources/WhiteSquareTexture'
        n.set_editor_property('texture', unreal.load_asset(default))
        n.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL if normal else unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
        return n

    def mul(a, b, x, yy, ai='', bi=''):
        n = node(unreal.MaterialExpressionMultiply, x, yy); MEL.connect_material_expressions(a, ai, n, 'A'); MEL.connect_material_expressions(b, bi, n, 'B'); return n

    def lerp(a, b, alpha, x, yy, ai='', bi='', ali=''):
        n = node(unreal.MaterialExpressionLinearInterpolate, x, yy)
        MEL.connect_material_expressions(a, ai, n, 'A'); MEL.connect_material_expressions(b, bi, n, 'B'); MEL.connect_material_expressions(alpha, ali, n, 'Alpha'); return n

    # Base colour = Color * BaseMap * lerp(1, VertexColor, UseVertexColor)
    color = vparam('Color', unreal.LinearColor(1, 1, 1, 1), -900, -300)
    base = tparam('BaseMap', -900, -100)
    vc = node(unreal.MaterialExpressionVertexColor, -900, 100)
    usevc = sparam('UseVertexColor', 0.0, -900, 250)
    one = node(unreal.MaterialExpressionConstant, -700, 250); one.set_editor_property('r', 1.0)
    vcl = lerp(one, vc, usevc, -600, 150)
    c1 = mul(color, base, -500, -250, '', 'RGB')
    c2 = mul(c1, vcl, -300, -200)
    MEL.connect_material_property(c2, '', unreal.MaterialProperty.MP_BASE_COLOR)
    # Normal map with strength.
    nmap = tparam('NormalMap', -900, 400, normal=True)
    nstr = sparam('NormalStrength', 1.0, -900, 600)
    flat = node(unreal.MaterialExpressionConstant3Vector, -700, 650); flat.set_editor_property('constant', unreal.LinearColor(0, 0, 1, 1))
    nl = lerp(flat, nmap, nstr, -500, 500, '', 'RGB')
    MEL.connect_material_property(nl, '', unreal.MaterialProperty.MP_NORMAL)
    # Roughness = Roughness * RoughMap.g, Metallic, Specular.
    rough = sparam('Roughness', 0.8, -900, 800)
    rmap = tparam('RoughMap', -900, 900)
    r2 = mul(rough, rmap, -500, 850, '', 'G')
    MEL.connect_material_property(r2, '', unreal.MaterialProperty.MP_ROUGHNESS)
    metal = sparam('Metallic', 0.0, -500, 1000)
    MEL.connect_material_property(metal, '', unreal.MaterialProperty.MP_METALLIC)
    # Emissive = EmissiveColor * EmissiveIntensity
    em = vparam('EmissiveColor', unreal.LinearColor(0, 0, 0, 1), -900, 1150)
    emi = sparam('EmissiveIntensity', 1.0, -900, 1300)
    e2 = mul(em, emi, -500, 1200)
    MEL.connect_material_property(e2, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    # Opacity (translucent instances) and opacity mask (alpha-tested leaves) from Opacity * BaseMap alpha.
    op = sparam('Opacity', 1.0, -900, 1450)
    o2 = mul(op, base, -500, 1450, '', 'A')
    MEL.connect_material_property(o2, '', unreal.MaterialProperty.MP_OPACITY)
    MEL.connect_material_property(o2, '', unreal.MaterialProperty.MP_OPACITY_MASK)
    mat.set_editor_property('used_with_instanced_static_meshes', True)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    log('created', path)
    return mat


# ---------- Textures ----------
def import_texture(tex_id, normal=False, srgb=True):
    name = f'T_Web_{tex_id}'
    path = f'{MAT_DIR}/../Textures/Web/{name}'.replace('Materials/../', '')
    if EAL.does_asset_exist(path):
        return unreal.load_asset(path)
    src = os.path.join(EXPORT, 'textures', f'{tex_id}.png')
    if not os.path.exists(src):
        return None
    task = unreal.AssetImportTask()
    task.filename = src; task.destination_path = '/Game/Haarklous/Textures/Web'; task.destination_name = name
    task.automated = True; task.replace_existing = True; task.save = False
    AT.import_asset_tasks([task])
    t = unreal.load_asset(path)
    if t:
        if normal:
            t.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_NORMALMAP)
            t.set_editor_property('srgb', False)
        elif not srgb:
            t.set_editor_property('srgb', False)
            t.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_MASKS)
        t.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_WORLD)
        EAL.save_loaded_asset(t)
    return t


# ---------- Material instances (one per web material key) ----------
def material_instances(master, wanted_keys):
    keys = {k['key']: k for k in json.load(open(os.path.join(PLAN, 'material-keys.json'), encoding='utf-8'))}
    textures = json.load(open(os.path.join(EXPORT, 'materials.json'), encoding='utf-8'))['textures']
    made = {}
    for key in sorted(wanted_keys):
        path = f'{MAT_DIR}/Web/{key}'
        if EAL.does_asset_exist(path):
            made[key] = unreal.load_asset(path)
            continue
        m = keys.get(key)
        mi = AT.create_asset(key, f'{MAT_DIR}/Web', unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        MEL.set_material_instance_parent(mi, master)
        if m:
            opacity = m.get('opacity', 1.0) if m.get('transparent') else 1.0
            MEL.set_material_instance_vector_parameter_value(mi, 'Color', hex_color(m.get('color'), 1.0))
            MEL.set_material_instance_scalar_parameter_value(mi, 'Roughness', float(m.get('roughness', 0.8) if m.get('roughness') is not None else 0.8))
            MEL.set_material_instance_scalar_parameter_value(mi, 'Metallic', float(m.get('metalness', 0.0) or 0.0))
            MEL.set_material_instance_scalar_parameter_value(mi, 'Opacity', float(opacity))
            MEL.set_material_instance_scalar_parameter_value(mi, 'UseVertexColor', 1.0 if m.get('vertexColors') else 0.0)
            if m.get('emissive') and m.get('emissive') != 0:
                MEL.set_material_instance_vector_parameter_value(mi, 'EmissiveColor', hex_color(m['emissive']))
                MEL.set_material_instance_scalar_parameter_value(mi, 'EmissiveIntensity', float(m.get('emissiveIntensity', 1.0) or 1.0))
            if m.get('map') is not None:
                t = import_texture(m['map'], srgb=textures[m['map']].get('srgb', True))
                if t:
                    MEL.set_material_instance_texture_parameter_value(mi, 'BaseMap', t)
            if m.get('normalMap') is not None:
                t = import_texture(m['normalMap'], normal=True)
                if t:
                    MEL.set_material_instance_texture_parameter_value(mi, 'NormalMap', t)
                    ns = m.get('normalScale') or [1, 1]
                    MEL.set_material_instance_scalar_parameter_value(mi, 'NormalStrength', float(min(2.0, abs(ns[0]))))
            if m.get('roughnessMap') is not None:
                t = import_texture(m['roughnessMap'], srgb=False)
                if t:
                    MEL.set_material_instance_texture_parameter_value(mi, 'RoughMap', t)
            ov = unreal.MaterialInstanceBasePropertyOverrides()
            if m.get('side') == 2:
                ov.set_editor_property('override_two_sided', True); ov.set_editor_property('two_sided', True)
            if m.get('transparent') and opacity < 0.999:
                ov.set_editor_property('override_blend_mode', True); ov.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
            elif m.get('alphaTest', 0) and m.get('alphaTest', 0) > 0:
                ov.set_editor_property('override_blend_mode', True); ov.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
                ov.set_editor_property('override_opacity_mask_clip_value', True); ov.set_editor_property('opacity_mask_clip_value', float(m['alphaTest']))
            mi.set_editor_property('base_property_overrides', ov)
        EAL.save_loaded_asset(mi)
        made[key] = mi
    return made


# ---------- Meshes ----------
def import_rig(rig, mats):
    fbx = os.path.join(FBX, rig + '.fbx')
    manifest = json.load(open(os.path.join(FBX, rig + '.manifest.json'), encoding='utf-8'))
    dest = f'/Game/Haarklous/Meshes/{rig}'
    task = unreal.AssetImportTask()
    task.filename = fbx; task.destination_path = dest; task.automated = True; task.replace_existing = True; task.save = False
    ui = unreal.FbxImportUI()
    ui.import_mesh = True; ui.import_materials = False; ui.import_textures = False; ui.import_as_skeletal = False; ui.import_animations = False
    smd = ui.static_mesh_import_data
    smd.combine_meshes = False
    smd.auto_generate_collision = False
    smd.generate_lightmap_u_vs = True
    smd.remove_degenerates = True
    smd.build_nanite = False
    smd.normal_import_method = unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS
    smd.vertex_color_import_option = unreal.VertexColorImportOption.REPLACE
    task.options = ui
    AT.import_asset_tasks([task])
    count = 0
    for p in task.imported_object_paths:
        sm = unreal.load_asset(p)
        if not isinstance(sm, unreal.StaticMesh):
            continue
        static_mats = sm.get_editor_property('static_materials')
        for i, sl in enumerate(static_mats):
            key = str(sl.get_editor_property('material_slot_name'))
            mi = mats.get(key) or mats.get(key.rsplit('_', 1)[0] if key.rsplit('_', 1)[-1].isdigit() and key.rsplit('_', 1)[0] in mats else key)
            if mi:
                sm.set_material(i, mi)
        name = sm.get_name()
        trace = rig.startswith(TRACE_RIGS) or '__p' in name or rig.startswith('apartment')
        body = sm.get_editor_property('body_setup')
        if body:
            body.set_editor_property('collision_trace_flag', unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE if trace else unreal.CollisionTraceFlag.CTF_USE_DEFAULT)
        # Lightmap resolution by size (the city tiles are large and simple).
        bb = sm.get_bounding_box()
        size = max(bb.max.x - bb.min.x, bb.max.y - bb.min.y, bb.max.z - bb.min.z) / 100.0
        sm.set_editor_property('light_map_resolution', 64 if size < 3 else 128 if size < 20 else 256)
        EAL.save_loaded_asset(sm)
        count += 1
    log(f'{rig}: {count} meshes imported into {dest}')


def main():
    master = master_material()
    rigs = ARGS or sorted(f[:-4] for f in os.listdir(FBX) if f.endswith('.fbx'))
    wanted = set()
    for rig in rigs:
        plan = json.load(open(os.path.join(PLAN, rig + '.json'), encoding='utf-8'))
        for a in plan['assets']:
            wanted.update(a['slotKeys'])
    mats = material_instances(master, wanted)
    log(f'{len(mats)} material instances ready')
    for rig in rigs:
        import_rig(rig, mats)


main()
