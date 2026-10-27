"""A clump of grass blades for the 3D grass near the camera (realism pass), built headless:

    blender -b --factory-startup -P tools/blender/build_grass.py

28 short, tapered, slightly bent blades within 0.2 m, lawn height 7-16 cm, both sides as real triangles (3 per
side). Normals point straight up like the ground's, so the blades are lit exactly like the lawn under them (a
two-sided material would flip them on the back side and turn half of every blade dark). Vertex colour red = height
along the blade (0 at the root, 1 at the tip), used by M_HK_Grass for colour and wind.
Writes tools/out/fbx/grass.fbx (object SM_GrassClump, metres, z up).
"""
import math
import os

import bpy
import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
OUT = os.path.join(ROOT, 'tools', 'out', 'fbx')

rng = np.random.default_rng(7)
verts, faces, heights, norms = [], [], [], []
for b in range(28):
    r = 0.2 * math.sqrt(rng.uniform())
    a = rng.uniform(0, 2 * math.pi)
    base = np.array([r * math.cos(a), r * math.sin(a), 0.0])
    h = rng.uniform(0.07, 0.16)
    w = rng.uniform(0.01, 0.018)
    face = rng.uniform(0, 2 * math.pi)
    side = np.array([math.cos(face), math.sin(face), 0.0])
    lean_dir = np.array([math.cos(face + math.pi / 2), math.sin(face + math.pi / 2), 0.0]) * (1 if rng.uniform() < 0.5 else -1)
    lean = rng.uniform(0.01, 0.05)
    nrm = np.array([0.0, 0.0, 1.0])
    first = len(verts)
    for t in (0.0, 0.5):
        c = base + np.array([0, 0, h * t]) + lean_dir * lean * t * t
        half = w / 2 * (1 - 0.55 * t)
        verts += [c - side * half, c + side * half]
        heights += [t, t]
        norms += [nrm, nrm]
    verts.append(base + np.array([0, 0, h]) + lean_dir * lean)
    heights.append(1.0)
    norms.append(nrm)
    f = first
    faces += [(f, f + 1, f + 3), (f, f + 3, f + 2), (f + 2, f + 3, f + 4)]

# The back side: its own copies of the vertices, reversed winding, the same upward normals.
nv = len(verts)
verts += [v.copy() for v in verts]
heights += heights[:]
norms += norms[:]
faces += [(c + nv, b + nv, a + nv) for (a, b, c) in faces]

bpy.ops.wm.read_factory_settings(use_empty=True)
mesh = bpy.data.meshes.new('SM_GrassClump')
mesh.from_pydata([tuple(v) for v in verts], [], faces)
mesh.update()
col = mesh.color_attributes.new('Col', 'FLOAT_COLOR', 'CORNER')
for poly in mesh.polygons:
    for li in poly.loop_indices:
        t = heights[mesh.loops[li].vertex_index]
        col.data[li].color = (t, t, t, 1.0)
uv = mesh.uv_layers.new(name='UVMap')
for poly in mesh.polygons:
    for li in poly.loop_indices:
        uv.data[li].uv = (0.5, heights[mesh.loops[li].vertex_index])
mesh.normals_split_custom_set([tuple(norms[mesh.loops[li].vertex_index]) for li in range(len(mesh.loops))])
mat = bpy.data.materials.new('M_HK_Grass')
mesh.materials.append(mat)
obj = bpy.data.objects.new('SM_GrassClump', mesh)
bpy.context.scene.collection.objects.link(obj)
os.makedirs(OUT, exist_ok=True)
bpy.ops.object.select_all(action='SELECT')
bpy.ops.export_scene.fbx(filepath=os.path.join(OUT, 'grass.fbx'), use_selection=True, apply_unit_scale=True,
                         apply_scale_options='FBX_SCALE_NONE', object_types={'MESH'}, mesh_smooth_type='OFF',
                         colors_type='LINEAR', add_leaf_bones=False, axis_forward='-Z', axis_up='Y')
print(f'HK: grass clump {len(faces)} triangles -> {os.path.join(OUT, "grass.fbx")}')
