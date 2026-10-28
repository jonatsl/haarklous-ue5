"""Imports the realistic people from tools/blender/build_people.py (asked for by the user on 28 Oct 2026).

  powershell -File tools\\unreal\\run-python.ps1 tools\\unreal\\import_people.py

Reads tools/out/fbx/people/people.json: textures (MakeHuman CC0 skins, clothes, hair) go to /Game/Haarklous/People/Textures,
materials to /Game/Haarklous/People/Materials:
  M_HK_PersonSkin   skin texture, soft specular
  M_HK_PersonCloth  cloth texture (masked by its alpha) and normal map; TintAmount > 0 recolours it (its brightness
                    pattern times Gain times Tint)
  M_HK_PersonHair   hair, brows and lashes: masked by the texture's alpha, two-sided
  M_HK_PersonEye    the eyes, glossy
  M_HK_PersonPlain  plain colour (balaclava, armband, pistol)
The players (avatar_0..4) become skeletal meshes /Game/Haarklous/People/<name>/SK_<name> (AHKRigActor drives their bones
from the avatar rig); the mission figures replace the 'body' meshes of their rigs (/Game/Haarklous/Meshes/<rig>/).
"""
import json
import os

import unreal

AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
SRC = os.path.join(ROOT, 'tools', 'out', 'fbx', 'people')
PDIR = '/Game/Haarklous/People'
TDIR = PDIR + '/Textures'
MDIR = PDIR + '/Materials'


def log(*a):
    print('HK:', *a)


TEX = {}


def tex(path, kind, max_size):
    key = (path, kind)
    if key in TEX:
        return TEX[key]
    base = os.path.splitext(os.path.basename(path))[0]
    folder = os.path.basename(os.path.dirname(path))
    name = ('T_' + folder + '_' + base).replace('-', '_').replace(' ', '_').replace('.', '_')[:60] + ('_N' if kind == 'N' else '')
    t = unreal.AssetImportTask()
    t.filename = path; t.destination_path = TDIR; t.destination_name = name
    t.automated = True; t.replace_existing = True; t.save = False
    AT.import_asset_tasks([t])
    a = unreal.load_asset(f'{TDIR}/{name}')
    if kind == 'N':
        a.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_NORMALMAP)
        a.set_editor_property('srgb', False); a.set_editor_property('flip_green_channel', True)
    a.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_CHARACTER)
    a.set_editor_property('max_texture_size', max_size)
    EAL.save_loaded_asset(a)
    TEX[key] = a
    return a


class G:
    """A small material graph builder (like tools/unreal/materials_site.py)."""
    def __init__(self, name, blend=None, two_sided=False):
        path = f'{MDIR}/{name}'
        if EAL.does_asset_exist(path):
            EAL.delete_asset(path)
        self.m = AT.create_asset(name, MDIR, unreal.Material, unreal.MaterialFactoryNew())
        self.m.set_editor_property('used_with_skeletal_mesh', True)
        if blend:
            self.m.set_editor_property('blend_mode', blend)
        self.m.set_editor_property('two_sided', two_sided)
        self.y = 0

    def n(self, cls):
        self.y += 140
        return MEL.create_material_expression(self.m, cls, -1000, self.y)

    def c(self, a, ao, b, bi):
        MEL.connect_material_expressions(a, ao, b, bi)

    def out(self, a, prop, ao=''):
        MEL.connect_material_property(a, ao, prop)

    def scalar(self, name, v):
        e = self.n(unreal.MaterialExpressionScalarParameter); e.set_editor_property('parameter_name', name); e.set_editor_property('default_value', v); return e

    def vector(self, name, v):
        e = self.n(unreal.MaterialExpressionVectorParameter); e.set_editor_property('parameter_name', name); e.set_editor_property('default_value', unreal.LinearColor(*v)); return e

    def tex(self, name, texture, kind='color'):
        e = self.n(unreal.MaterialExpressionTextureSampleParameter2D)
        e.set_editor_property('parameter_name', name)
        e.set_editor_property('texture', texture)
        e.set_editor_property('sampler_type', {'color': unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, 'normal': unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL}[kind])
        return e

    def mul(self, a, b, ao='', bo=''):
        e = self.n(unreal.MaterialExpressionMultiply); self.c(a, ao, e, 'A'); self.c(b, bo, e, 'B'); return e

    def lerp(self, a, b, t, ao='', bo='', to=''):
        e = self.n(unreal.MaterialExpressionLinearInterpolate); self.c(a, ao, e, 'A'); self.c(b, bo, e, 'B'); self.c(t, to, e, 'Alpha'); return e

    def finish(self):
        MEL.recompile_material(self.m)
        EAL.save_loaded_asset(self.m)
        log('created', self.m.get_name())
        return self.m


def flat_normal():
    return unreal.load_asset('/Engine/EngineMaterials/DefaultNormal')


def white():
    return unreal.load_asset('/Engine/EngineResources/WhiteSquareTexture')


def parents():
    out = {}
    g = G('M_HK_PersonSkin')
    d = g.tex('Diffuse', white())
    g.out(d, unreal.MaterialProperty.MP_BASE_COLOR, 'RGB')
    g.out(g.scalar('Roughness', .52), unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(g.scalar('Specular', .38), unreal.MaterialProperty.MP_SPECULAR)
    out['skin'] = g.finish()
    g = G('M_HK_PersonCloth', unreal.BlendMode.BLEND_MASKED)   # some garments cut their hems with the texture's alpha
    d = g.tex('Diffuse', white())
    g.out(d, unreal.MaterialProperty.MP_OPACITY_MASK, 'A')
    lum = g.n(unreal.MaterialExpressionDesaturation); g.c(d, 'RGB', lum, '')
    tinted = g.mul(g.mul(lum, g.scalar('Gain', 1.0)), g.vector('Tint', (1, 1, 1, 1)), '', 'RGB')
    g.out(g.lerp(d, tinted, g.scalar('TintAmount', 0.0), 'RGB', '', ''), unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(g.tex('Normal', flat_normal(), 'normal'), unreal.MaterialProperty.MP_NORMAL, 'RGB')
    g.out(g.scalar('Roughness', .85), unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(g.scalar('Specular', .3), unreal.MaterialProperty.MP_SPECULAR)
    out['cloth'] = g.finish()
    g = G('M_HK_PersonHair', unreal.BlendMode.BLEND_MASKED, two_sided=True)
    d = g.tex('Diffuse', white())
    g.out(d, unreal.MaterialProperty.MP_BASE_COLOR, 'RGB')
    g.out(d, unreal.MaterialProperty.MP_OPACITY_MASK, 'A')
    g.out(g.scalar('Roughness', .55), unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(g.scalar('Specular', .35), unreal.MaterialProperty.MP_SPECULAR)
    g.m.set_editor_property('opacity_mask_clip_value', .4)
    out['hair'] = g.finish()
    g = G('M_HK_PersonEye')
    d = g.tex('Diffuse', white())
    g.out(d, unreal.MaterialProperty.MP_BASE_COLOR, 'RGB')
    g.out(g.scalar('Roughness', .08), unreal.MaterialProperty.MP_ROUGHNESS)
    out['eye'] = g.finish()
    g = G('M_HK_PersonPlain')
    g.out(g.vector('Color', (.1, .1, .1, 1)), unreal.MaterialProperty.MP_BASE_COLOR, 'RGB')
    g.out(g.scalar('Roughness', .9), unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(g.scalar('Metallic', 0.0), unreal.MaterialProperty.MP_METALLIC)
    out['plain'] = g.finish()
    return out


def instance(name, parent, vectors=None, scalars=None, textures=None):
    path = f'{MDIR}/{name}'
    if not EAL.does_asset_exist(path):
        AT.create_asset(name, MDIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    m = unreal.load_asset(path)
    MEL.set_material_instance_parent(m, parent)
    MEL.clear_all_material_instance_parameters(m)
    for k, v in (vectors or {}).items():
        MEL.set_material_instance_vector_parameter_value(m, k, unreal.LinearColor(*v, 1))
    for k, v in (scalars or {}).items():
        MEL.set_material_instance_scalar_parameter_value(m, k, v)
    for k, v in (textures or {}).items():
        MEL.set_material_instance_texture_parameter_value(m, k, v)
    EAL.save_loaded_asset(m)
    return m


def materials(entries, par):
    out = {}
    for key, e in entries.items():
        kind = e['kind']
        if kind == 'skin':
            out[key] = instance(key, par['skin'], textures={'Diffuse': tex(e['diffuse'], 'D', 2048)})
        elif kind in ('hair', 'brow', 'lash'):
            out[key] = instance(key, par['hair'], textures={'Diffuse': tex(e['diffuse'], 'D', 1024)})
        elif kind == 'eye':
            out[key] = instance(key, par['eye'], textures={'Diffuse': tex(e['diffuse'], 'D', 512)})
        elif 'diffuse' in e:
            t = {'Diffuse': tex(e['diffuse'], 'D', 1024)}
            if 'normal' in e:
                t['Normal'] = tex(e['normal'], 'N', 1024)
            s, v = {}, {}
            if 'tint' in e:
                s = {'TintAmount': 1.0, 'Gain': e['gain']}
                v = {'Tint': e['tint']}
            out[key] = instance(key, par['cloth'], v, s, t)
        else:
            out[key] = instance(key, par['plain'], {'Color': e['color']}, {'Roughness': e.get('rough', .8), 'Metallic': 1.0 if kind == 'metal' else 0.0})
    return out


def import_fbx(fbx, dest, name, skeletal):
    t = unreal.AssetImportTask()
    t.filename = fbx; t.destination_path = dest; t.destination_name = name
    t.automated = True; t.replace_existing = True; t.save = False
    ui = unreal.FbxImportUI()
    ui.set_editor_property('import_mesh', True)
    ui.set_editor_property('import_as_skeletal', skeletal)
    ui.set_editor_property('import_materials', False); ui.set_editor_property('import_textures', False)
    ui.set_editor_property('import_animations', False)
    if skeletal:
        ui.set_editor_property('mesh_type_to_import', unreal.FBXImportType.FBXIT_SKELETAL_MESH)
        ui.set_editor_property('create_physics_asset', False)
        sd = ui.get_editor_property('skeletal_mesh_import_data')
        sd.set_editor_property('import_morph_targets', False)
        sd.set_editor_property('normal_import_method', unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
        sd.set_editor_property('use_t0_as_ref_pose', False)
    else:
        ui.set_editor_property('mesh_type_to_import', unreal.FBXImportType.FBXIT_STATIC_MESH)
        sd = ui.get_editor_property('static_mesh_import_data')
        sd.set_editor_property('combine_meshes', True)
        sd.set_editor_property('generate_lightmap_u_vs', False)
        sd.set_editor_property('auto_generate_collision', False)
        sd.set_editor_property('normal_import_method', unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
    t.options = ui
    AT.import_asset_tasks([t])
    return [unreal.load_asset(p) for p in t.get_editor_property('imported_object_paths')]


def assign(mesh, mats):
    if isinstance(mesh, unreal.SkeletalMesh):
        # The list holds copies of the structs: build new ones and set the whole list.
        slots = []
        for sm in mesh.get_editor_property('materials'):
            key = str(sm.get_editor_property('material_slot_name'))
            slots.append(unreal.SkeletalMaterial(material_interface=mats.get(key, sm.get_editor_property('material_interface')),
                                                 material_slot_name=sm.get_editor_property('material_slot_name'),
                                                 uv_channel_data=sm.get_editor_property('uv_channel_data')))
        mesh.set_editor_property('materials', slots)
        log('materials', [str(m.get_editor_property('material_interface').get_name()) for m in mesh.get_editor_property('materials')])
    else:
        for i, sl in enumerate(mesh.get_editor_property('static_materials')):
            key = str(sl.get_editor_property('material_slot_name'))
            if key in mats:
                mesh.set_material(i, mats[key])
    EAL.save_loaded_asset(mesh)


def main():
    manifest = json.load(open(os.path.join(SRC, 'people.json')))
    par = parents()
    for name, p in manifest.items():
        mats = materials(p['materials'], par)
        fbx = os.path.join(SRC, name + '.fbx')
        if p['static']:
            got = import_fbx(fbx, f'/Game/Haarklous/Meshes/{name}', f'SM_{name}__body', False)
        else:
            got = import_fbx(fbx, f'{PDIR}/{name}', f'SK_{name}', True)
        for a in got:
            if isinstance(a, (unreal.SkeletalMesh, unreal.StaticMesh)):
                assign(a, mats)
                log(name, a.get_path_name())
            elif a is not None:
                EAL.save_loaded_asset(a)


main()
