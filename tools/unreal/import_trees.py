"""Put the crowns of tools/blender/build_trees.py over the old crown meshes (run headless with
tools/unreal/run-python.ps1). Same asset names and folders, so AHKGame::SpawnTrees places them as before."""
import json
import os

import unreal

AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
SRC = os.path.join(ROOT, 'tools', 'out', 'fbx', 'trees')

done = 0
for m in json.load(open(os.path.join(SRC, 'trees.json'), encoding='utf-8')):
    t = unreal.AssetImportTask()
    t.filename = os.path.join(SRC, m['asset'] + '.fbx')
    t.destination_path = f"/Game/Haarklous/Meshes/{m['rig']}"
    t.destination_name = m['asset']
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
    sd.set_editor_property('normal_import_method', unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
    t.options = ui
    AT.import_asset_tasks([t])
    sm = unreal.load_asset(f"/Game/Haarklous/Meshes/{m['rig']}/{m['asset']}")
    if not sm:
        print('HK: missing', m['asset']); continue
    sm.set_material(0, unreal.load_asset(f"/Game/Haarklous/Materials/Foliage/{m['kind']}"))
    sm.set_material(1, unreal.load_asset('/Game/Haarklous/Materials/Foliage/MI_HK_Bark'))
    EAL.save_loaded_asset(sm)
    done += 1
print(f'HK: {done} tree crowns replaced')
