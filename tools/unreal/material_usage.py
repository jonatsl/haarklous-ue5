"""Usage flags on every base material of the game (run headless with tools/unreal/run-python.ps1, after the other
material scripts). The editor sets these by itself when a material is first used in a new way, but a game started
with -game cannot: lightmapped level meshes then fall back to the grey default material ("missing usage flag
StaticLighting"). Opaque and masked materials get static lighting and instanced meshes."""
import unreal

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
FLAGS = ('used_with_static_lighting', 'used_with_instanced_static_meshes')

changed = 0
for path in EAL.list_assets('/Game/Haarklous', recursive=True, include_folder=False):
    data = EAL.find_asset_data(path)
    if str(data.asset_class_path.asset_name) != 'Material':
        continue
    m = unreal.load_asset(path)
    if m.get_editor_property('material_domain') != unreal.MaterialDomain.MD_SURFACE:
        continue
    todo = [f for f in FLAGS if not m.get_editor_property(f)]
    if not todo:
        continue
    for f in todo:
        m.set_editor_property(f, True)
    MEL.recompile_material(m)
    EAL.save_loaded_asset(m)
    changed += 1
    print('HK: usage flags', ', '.join(todo), '->', m.get_name())
print(f'HK: {changed} materials updated')
