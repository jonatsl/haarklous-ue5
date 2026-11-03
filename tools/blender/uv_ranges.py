"""The largest |uv| of every mesh in tools/out/fbx (3 Nov 2026), for tools/unreal/full_precision_uvs.py.

Unreal keeps mesh uvs as half floats unless a mesh is set to full precision. A half float has 11 significant bits, so a
uv of 150 snaps to steps of 1/8: meshes laid out in world metres (the near world's tiles, the 3D facades, the
stairwells, the site's gravel track) had their textures bent and smeared between vertices. Writes
tools/out/uv_ranges.json: {"<fbx name>/<mesh name>": largest |uv|} for every mesh above 16.

    blender -b --factory-startup -P tools/blender/uv_ranges.py
"""
import glob
import json
import os

import bpy
import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
FBX = os.path.join(ROOT, 'tools', 'out', 'fbx')

out = {}
for f in sorted(glob.glob(os.path.join(FBX, '*.fbx'))):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=f)
    for o in bpy.data.objects:
        if o.type != 'MESH' or not o.data.uv_layers:
            continue
        uv = np.zeros(len(o.data.loops) * 2, dtype=np.float32)
        o.data.uv_layers[0].data.foreach_get('uv', uv)
        if len(uv) and np.abs(uv).max() > 16:
            out[f'{os.path.splitext(os.path.basename(f))[0]}/{o.name}'] = round(float(np.abs(uv).max()), 1)
json.dump(out, open(os.path.join(ROOT, 'tools', 'out', 'uv_ranges.json'), 'w'), indent=0, sort_keys=True)
print(f'HK: {len(out)} meshes with uvs beyond 16')
