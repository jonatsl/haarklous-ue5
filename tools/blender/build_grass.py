"""A clump of grass cards for the 3D lawn near the camera (photo-real pass), built headless:

    blender -b --factory-startup -P tools/blender/build_grass.py

16 upright cards spread evenly (jittered grid) over a 0.6 m disc, each showing one of the real grass tufts of the
card atlas (tools/make-grass-atlas.py, from the CC0 Poly Haven grass_medium_01 maps), lawn height 5-11 cm, turned at
random and leaning back a little. Clumps placed 0.45 m apart overlap, so the lawn has no bare gaps. Both sides are
real triangles. Normals point straight up like the ground's, so the cards are lit exactly like the lawn under them
(a card's own normal would turn it dark or bright depending on the view).
Vertex colour: red = height on the card (0 root .. 1 top, for the root shade and wind), green = a random value per
card (colour variety). Writes tools/out/fbx/grass.fbx (object SM_GrassClump, metres, z up).
"""
import json
import math
import os

import bpy
import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
OUT = os.path.join(ROOT, 'tools', 'out', 'fbx')
TUFTS = json.load(open(os.path.join(ROOT, 'tools', 'out', 'textures', 'grass-cards.json'), encoding='utf-8'))['tufts']

rng = np.random.default_rng(11)
verts, faces, uvs, cols = [], [], [], []
spots = [(x, y) for x in np.linspace(-0.225, 0.225, 4) for y in np.linspace(-0.225, 0.225, 4)]
for c, (sx, sy) in enumerate(spots):
    t = TUFTS[int(rng.integers(len(TUFTS)))]
    base = np.array([sx + rng.uniform(-0.07, 0.07), sy + rng.uniform(-0.07, 0.07), -0.01])
    h = rng.uniform(0.05, 0.11)
    w = h * t['aspect']
    yaw = rng.uniform(0, 2 * math.pi)
    side = np.array([math.cos(yaw), math.sin(yaw), 0.0])
    back = np.array([-math.sin(yaw), math.cos(yaw), 0.0]) * rng.uniform(-0.25, 0.25)   # lean, in units of height
    up = np.array([0.0, 0.0, 1.0])
    g = rng.uniform()
    first = len(verts)
    # Two rows of vertices (root and top), three columns so the card can bend a little in the wind.
    for k, (s, v) in enumerate(((0, 0), (0.5, 0), (1, 0), (0, 1), (0.5, 1), (1, 1))):
        p = base + side * (s - 0.5) * w + (up + back) * h * v
        verts.append(p)
        uvs.append((t['u0'] + (t['u1'] - t['u0']) * s, 1.0 - (t['v1'] - (t['v1'] - t['v0']) * v)))
        cols.append((float(v), g))
    f = first
    faces += [(f, f + 1, f + 4), (f, f + 4, f + 3), (f + 1, f + 2, f + 5), (f + 1, f + 5, f + 4)]

# The back side: its own copies of the vertices, reversed winding, the same upward normals.
nv, nf = len(verts), len(faces)
verts += [v.copy() for v in verts]
uvs += uvs[:]
cols += cols[:]
faces += [(c + nv, b + nv, a + nv) for (a, b, c) in faces[:nf]]

bpy.ops.wm.read_factory_settings(use_empty=True)
mesh = bpy.data.meshes.new('SM_GrassClump')
mesh.from_pydata([tuple(v) for v in verts], [], faces)
mesh.update()
col = mesh.color_attributes.new('Col', 'FLOAT_COLOR', 'CORNER')
uv = mesh.uv_layers.new(name='UVMap')
for poly in mesh.polygons:
    for li in poly.loop_indices:
        vi = mesh.loops[li].vertex_index
        col.data[li].color = (cols[vi][0], cols[vi][1], 0.0, 1.0)
        uv.data[li].uv = uvs[vi]
mesh.normals_split_custom_set([(0.0, 0.0, 1.0)] * len(mesh.loops))
mesh.materials.append(bpy.data.materials.new('M_HK_GrassCards'))
obj = bpy.data.objects.new('SM_GrassClump', mesh)
bpy.context.scene.collection.objects.link(obj)
os.makedirs(OUT, exist_ok=True)
bpy.ops.object.select_all(action='SELECT')
bpy.ops.export_scene.fbx(filepath=os.path.join(OUT, 'grass.fbx'), use_selection=True, apply_unit_scale=True,
                         apply_scale_options='FBX_SCALE_NONE', object_types={'MESH'}, mesh_smooth_type='OFF',
                         colors_type='LINEAR', add_leaf_bones=False, axis_forward='-Z', axis_up='Y')
print(f'HK: grass clump {len(faces)} triangles -> {os.path.join(OUT, "grass.fbx")}')
