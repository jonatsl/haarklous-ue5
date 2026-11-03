"""Repair mesh material slots whose material is missing (run headless with tools/unreal/run-python.ps1).

The web material instances are named by their export number (MI_<type>_<colour>_<id>); a new web export can move the
numbers (29 Oct 2026: +1 to +3), and import_assets.py then makes them again. A mesh imported over an existing one
keeps its old slot names, so a slot named after a number that no longer exists got no material and drew Unreal's
default grey grid (the "tiles" on the fridge and kitchen fronts, found 3 Nov 2026). The right key is the plan's key
of the same type and colour (MI_Standard_f2f2ee_168 -> MI_Standard_f2f2ee_171) when there is exactly one, else the
plan's i-th distinct key for slot i. This sets that material and the slot's name. Geometry is not touched, so the
baked lighting stays valid. (FIX_SLOTS: (asset, slot) -> the slot's
old name, to redo a slot that was set wrongly.)
"""
import json
import os

import unreal

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
PLAN = os.path.join(ROOT, 'tools', 'out', 'build-plan')
EAL = unreal.EditorAssetLibrary
WEB = '/Game/Haarklous/Materials/Web'
FIX_SLOTS = {}
fixed = 0
for f in sorted(os.listdir(PLAN)):
    rig = f[:-5]
    if not f.endswith('.json') or rig == 'material-keys':
        continue
    for a in json.load(open(os.path.join(PLAN, f), encoding='utf-8'))['assets']:
        sm = unreal.load_asset(f'/Game/Haarklous/Meshes/{rig}/{a["asset"]}')
        if not isinstance(sm, unreal.StaticMesh):
            continue
        keys = list(dict.fromkeys(a['slotKeys']))
        slots = list(sm.get_editor_property('static_materials'))
        changed = False
        for i, sl in enumerate(slots):
            name = str(sl.get_editor_property('material_slot_name'))
            if FIX_SLOTS.get((a['asset'], i)):
                name = FIX_SLOTS[(a['asset'], i)]
            elif sl.get_editor_property('material_interface') is not None or i >= len(keys):
                continue
            prefix = name.rsplit('_', 1)[0] + '_'
            same = [k for k in keys if k.startswith(prefix)]
            key = same[0] if len(same) == 1 else keys[i]
            mi = unreal.load_asset(f'{WEB}/{key}')
            if not mi:
                continue
            print('HK:', rig, a['asset'], 'slot', i, name, '->', key)
            sl.set_editor_property('material_interface', mi)
            sl.set_editor_property('material_slot_name', unreal.Name(key))
            changed = True
            fixed += 1
        if changed:
            sm.set_editor_property('static_materials', slots)
            EAL.save_loaded_asset(sm)
print(f'HK: {fixed} material slots repaired')
