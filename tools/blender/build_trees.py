"""Rebuild only the tree crowns (photo-real pass) without rebuilding the city tiles, headless:

    blender -b --factory-startup -P tools/blender/build_trees.py

For every crown in the tree lists (unreal/Haarklous/Content/Data/instances/world_near.json and world_far.json, kind
MI_HK_LeavesA / MI_HK_LeavesB, in the order build_assets.py made them) this writes
tools/out/fbx/trees/<asset>.fbx with tree_model.crown(): the same asset name, the same placements, slot 0 leaves and
slot 1 bark. tools/unreal/import_trees.py puts them over the old crowns. build_assets.py makes the same crowns when
the world rigs are rebuilt.
"""
import json
import os
import sys

import bpy
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tree_model  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
OUT = os.path.join(ROOT, 'tools', 'out', 'fbx', 'trees')


def to_blender(v):
    return np.stack([v[:, 0], -v[:, 2], v[:, 1]], axis=1)


def build(name, data, slots):
    T = len(data['slot'])
    pos = to_blender(data['pos'].reshape(-1, 3))
    me = bpy.data.meshes.new(name)
    me.from_pydata(pos.tolist(), [], [(3 * i, 3 * i + 1, 3 * i + 2) for i in range(T)])
    me.update()
    me.polygons.foreach_set('material_index', data['slot'].astype(np.int32))
    me.polygons.foreach_set('use_smooth', np.ones(T, dtype=bool))
    uvl = me.uv_layers.new(name='UVMap')
    uvl.data.foreach_set('uv', data['uv'].reshape(-1, 2).astype(np.float32).ravel())
    nor = to_blender(data['nor'].reshape(-1, 3))
    nor /= np.maximum(np.linalg.norm(nor, axis=1, keepdims=True), 1e-9)
    me.normals_split_custom_set(nor.tolist())
    for k in slots:
        me.materials.append(bpy.data.materials.get(k) or bpy.data.materials.new(k))
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    return ob


os.makedirs(OUT, exist_ok=True)
made = []
for rig in ('world_near', 'world_far'):
    lst = json.load(open(os.path.join(ROOT, 'unreal', 'Haarklous', 'Content', 'Data', 'instances', rig + '.json'), encoding='utf-8'))
    crown_index = 0
    for inst in lst['instances']:
        if inst['kind'] not in ('MI_HK_LeavesA', 'MI_HK_LeavesB'):
            continue
        bpy.ops.wm.read_factory_settings(use_empty=True)
        ob = build(inst['asset'], tree_model.crown(crown_index, far=rig == 'world_far'), [inst['kind'], 'MI_HK_Bark'])
        crown_index += 1
        bpy.ops.object.select_all(action='DESELECT')
        ob.select_set(True)
        bpy.ops.export_scene.fbx(filepath=os.path.join(OUT, inst['asset'] + '.fbx'), use_selection=True, apply_unit_scale=True,
                                 apply_scale_options='FBX_SCALE_NONE', object_types={'MESH'}, mesh_smooth_type='FACE',
                                 add_leaf_bones=False, axis_forward='-Z', axis_up='Y', use_tspace=False)
        made.append({'rig': rig, 'asset': inst['asset'], 'kind': inst['kind']})
json.dump(made, open(os.path.join(OUT, 'trees.json'), 'w'), indent=1)
print('HK: crowns', [(m['asset'], m['kind']) for m in made])
