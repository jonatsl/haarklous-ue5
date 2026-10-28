"""Real 3D facades of the blocks near home (photo-real pass, tools/blender/facade_model.py), built headless:

    blender -b --factory-startup -P tools/blender/build_facades.py

Writes tools/out/fbx/facades.fbx (one mesh per 50 m tile, SM_Facade3D_<i>_<j>, slots M_HK_Wall3D wall / glass /
dark frame / white frame / concrete sill, uv0 + uv1, vertex colours) and facades.manifest.json. Imported and placed
by tools/unreal/import_facades.py; build_assets.py drops the web's flat facade on the same walls.
"""
import json
import math
import os
import sys

import bpy
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import facade_model as fm  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
OUT = os.path.join(ROOT, 'tools', 'out', 'fbx')
TILE = 50.0


def to_blender(v):
    return np.stack([v[:, 0], -v[:, 2], v[:, 1]], axis=1)


def build(name, P, N, U0, U1, C, S):
    T = len(S)
    me = bpy.data.meshes.new(name)
    me.from_pydata(to_blender(P.reshape(-1, 3)).tolist(), [], [(3 * i, 3 * i + 1, 3 * i + 2) for i in range(T)])
    me.update()
    me.polygons.foreach_set('material_index', S.astype(np.int32))
    for uvn, U in (('UVMap', U0), ('UV1', U1)):
        uvl = me.uv_layers.new(name=uvn)
        uvl.data.foreach_set('uv', U.reshape(-1, 2).astype(np.float32).ravel())
    ca = me.color_attributes.new('Col', 'FLOAT_COLOR', 'CORNER')
    ca.data.foreach_set('color', C.reshape(-1, 4).astype(np.float32).ravel())
    me.normals_split_custom_set(to_blender(N.reshape(-1, 3)).tolist())
    for k in fm.SLOTS:
        me.materials.append(bpy.data.materials.get(k) or bpy.data.materials.new(k))
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    # Weld corners that are identical (position, normal, uvs) so the mesh is not a soup.
    bpy.context.view_layer.objects.active = ob
    return ob


bpy.ops.wm.read_factory_settings(use_empty=True)
walls = fm.covered_walls()
P, N, U0, U1, C, S = fm.shell(walls)
cx = P[:, :, 0].mean(axis=1); cz = P[:, :, 2].mean(axis=1)
ti, tj = np.floor(cx / TILE).astype(int), np.floor(cz / TILE).astype(int)
made = []
for (i, j) in sorted(set(zip(ti.tolist(), tj.tolist()))):
    sel = (ti == i) & (tj == j)
    name = f'SM_Facade3D_{i + 50}_{j + 50}'
    build(name, P[sel], N[sel], U0[sel], U1[sel], C[sel], S[sel])
    made.append({'asset': name, 'triangles': int(sel.sum())})
os.makedirs(OUT, exist_ok=True)
bpy.ops.object.select_all(action='SELECT')
bpy.ops.export_scene.fbx(filepath=os.path.join(OUT, 'facades.fbx'), use_selection=True, apply_unit_scale=True,
                         apply_scale_options='FBX_SCALE_NONE', object_types={'MESH'}, mesh_smooth_type='FACE',
                         colors_type='LINEAR', add_leaf_bones=False, axis_forward='-Z', axis_up='Y', use_tspace=True)
json.dump({'assets': made, 'walls': len(walls), 'buildings': len({w['b'] for w in walls})},
          open(os.path.join(OUT, 'facades.manifest.json'), 'w'), indent=1)
print(f'HK: 3D facades: {len(walls)} walls of {len({w["b"] for w in walls})} buildings, {len(S)} triangles, {len(made)} tiles')
