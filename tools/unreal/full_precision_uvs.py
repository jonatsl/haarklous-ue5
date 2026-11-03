"""Full-precision uvs for the meshes whose uvs are large (3 Nov 2026). Run headless with tools/unreal/run-python.ps1
after the imports and tools/blender/uv_ranges.py.

Unreal stores uvs as half floats by default: at a uv of 150 (world metres on the near world's tiles, the 3D facades)
they snap to 1/8 of a texture, at 71 (the stairwells) to 1/16, so textures were bent between vertices and smeared into
streaks where rows of vertices snapped to the same value. Meshes listed in tools/out/uv_ranges.json above LIMIT get
full-precision uvs (4 bytes more per vertex and uv channel).
"""
import json
import os

import unreal

LIMIT = 32.0          # half-float step 1/32 of a texture from here on
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
EAL = unreal.EditorAssetLibrary


def log(*a):
    print('HK:', *a)


unreal.load_module('StaticMeshEditor')     # its subsystem is not loaded in a commandlet otherwise
SMES = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
ranges = json.load(open(os.path.join(ROOT, 'tools', 'out', 'uv_ranges.json')))
done, same, missing = 0, 0, []
for key, top in sorted(ranges.items()):
    if top <= LIMIT:
        continue
    rig, name = key.split('/', 1)
    path = f'/Game/Haarklous/Meshes/{rig}/{name}'
    sm = unreal.load_asset(path) if EAL.does_asset_exist(path) else None
    if not sm:
        missing.append(key)
        continue
    for lod in range(SMES.get_lod_count(sm)):
        b = SMES.get_lod_build_settings(sm, lod)
        if b.get_editor_property('use_full_precision_u_vs'):
            same += 1
            continue
        b.set_editor_property('use_full_precision_u_vs', True)
        SMES.set_lod_build_settings(sm, lod, b)
        done += 1
    EAL.save_loaded_asset(sm)
log(f'full-precision uvs: {done} mesh LODs set, {same} already set, {len(missing)} not imported {missing[:6]}')
