"""Materials of the places the web game added on 29 Oct 2026 (run headless with tools/unreal/run-python.ps1, after
import_assets.py): HODET N.N.'s bronze (the web's lib.metal(0x4e463d, .5)) becomes a real dark bronze with a lighter
metal sheen, as on the photos; its stone steps (gabions in reality) keep the web's grey stone.
    powershell -File tools\\unreal\\run-python.ps1 tools\\unreal\\materials_places.py
"""
import unreal

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary


def log(*a):
    unreal.log('HK: ' + ' '.join(str(x) for x in a))


def tune(key, color=None, metallic=None, roughness=None):
    path = f'/Game/Haarklous/Materials/Web/{key}'
    mi = unreal.load_asset(path) if EAL.does_asset_exist(path) else None
    if not mi:
        log('missing', key)
        return
    if color:
        MEL.set_material_instance_vector_parameter_value(mi, 'Color', unreal.LinearColor(*color, 1))
    if metallic is not None:
        MEL.set_material_instance_scalar_parameter_value(mi, 'Metallic', metallic)
    if roughness is not None:
        MEL.set_material_instance_scalar_parameter_value(mi, 'Roughness', roughness)
    MEL.update_material_instance(mi)
    EAL.save_loaded_asset(mi)
    log('tuned', key)


# Cast bronze: dark brown-grey (linear) with a metal sheen; not as rough as the web's 0.5 so the sun catches the face.
tune('MI_metal_4e463d', color=(0.16, 0.115, 0.075), metallic=1.0, roughness=0.42)
