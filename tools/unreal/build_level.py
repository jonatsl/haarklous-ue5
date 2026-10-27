"""Build the level /Game/Maps/Torshov from the imported assets (run headless with tools/unreal/run-python.ps1).

Places every level-static mesh (the city tiles, the far landscape, the apartment shell, the nine stair towers) at the
web game's own transforms, and sets up the lighting for this laptop: one stationary sun (web sunDir, cascaded shadow
maps), a sky atmosphere and stationary sky light, height fog, a post-process volume with fixed exposure, static lamps
in the apartment (web values) and in every stairwell, and a Lightmass importance volume. Moving things (vehicles,
doors, drinks, trees, people) are spawned by the C++ game at run time.
Actors made here carry the tag HKGen and are replaced on every run.
"""
import json
import math
import os

import unreal

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
DATA = os.path.join(ROOT, 'unreal', 'Haarklous', 'Content', 'Data')
FBX = os.path.join(ROOT, 'tools', 'out', 'fbx')
LEVEL = '/Game/Maps/Torshov'
TAG = 'HKGen'
LES = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
EAL = unreal.EditorAssetLibrary


def log(*a):
    print('HK:', *a)


def ue_loc(p):
    return unreal.Vector(p[0] * 100.0, p[2] * 100.0, p[1] * 100.0)


def ue_transform(pos, quat, scale):
    q = unreal.Quat(-quat[0], -quat[2], -quat[1], quat[3])
    return unreal.Transform(ue_loc(pos), q.rotator(), unreal.Vector(scale[0], scale[2], scale[1]))


def srgb_hex(h):
    return unreal.LinearColor(((h >> 16) & 255) / 255.0, ((h >> 8) & 255) / 255.0, (h & 255) / 255.0, 1.0)


def spawn(cls, label, transform=None, folder='Generated'):
    a = EAS.spawn_actor_from_class(cls, transform.translation if transform else unreal.Vector(0, 0, 0))
    if transform:
        a.set_actor_transform(transform, False, False)
    a.set_actor_label(label)
    a.tags = [unreal.Name(TAG)]
    a.set_folder_path(unreal.Name(folder))
    return a


def open_level():
    if EAL.does_asset_exist(LEVEL):
        LES.load_level(LEVEL)
    else:
        LES.new_level(LEVEL)
    for a in EAS.get_all_level_actors():
        if TAG in [str(t) for t in a.tags]:
            EAS.destroy_actor(a)


def place_static():
    count = 0
    for rig in sorted(os.listdir(os.path.join(DATA, 'rigs'))):
        rig = rig[:-5]
        man_path = os.path.join(FBX, rig + '.manifest.json')
        if not os.path.exists(man_path):
            continue
        desc = json.load(open(os.path.join(DATA, 'rigs', rig + '.json'), encoding='utf-8'))
        if not desc.get('levelStatic'):
            continue
        root = desc['nodes'][0]
        xf = ue_transform(root['pos'], root['quat'], root['scale'])
        man = json.load(open(man_path, encoding='utf-8'))
        for a in man['assets']:
            if not a.get('levelStatic'):
                continue
            sm = unreal.load_asset(f'/Game/Haarklous/Meshes/{rig}/{a["asset"]}')
            if not sm:
                log('missing mesh', a['asset'])
                continue
            act = spawn(unreal.StaticMeshActor, a['asset'], xf, f'Generated/{rig}')
            comp = act.static_mesh_component
            far = rig == 'world_far'
            # The far landscape (district beyond 300 m, hills up to 11 km) is lit without lightmaps, so Lightmass only
            # works on the walkable neighbourhood.
            comp.set_mobility(unreal.ComponentMobility.MOVABLE if far else unreal.ComponentMobility.STATIC)
            comp.set_static_mesh(sm)
            if far:
                comp.set_editor_property('cast_shadow', False)
            count += 1
    log(f'{count} static mesh actors placed')


def lights():
    game = json.load(open(os.path.join(DATA, 'game.json'), encoding='utf-8'))
    sd = game['meta']['sunDir']
    # The sun shines along -sunDir. A directional light points along its actor X axis.
    d = unreal.Vector(-sd[0], -sd[2], -sd[1])
    yaw = math.degrees(math.atan2(d.y, d.x))
    pitch = math.degrees(math.asin(max(-1.0, min(1.0, d.z))))
    sun = spawn(unreal.DirectionalLight, 'Sun', unreal.Transform(unreal.Vector(0, 0, 5000), unreal.Rotator(0.0, pitch, yaw), unreal.Vector(1, 1, 1)), 'Generated/Lighting')
    lc = sun.light_component
    lc.set_mobility(unreal.ComponentMobility.STATIONARY)
    lc.set_editor_property('intensity', 3.4)                    # web: DirectionalLight(0xfff3e0, 3.4)
    lc.set_editor_property('light_color', unreal.Color(255, 243, 224, 255))
    lc.set_editor_property('atmosphere_sun_light', True)
    lc.set_editor_property('dynamic_shadow_distance_stationary_light', 6000.0)
    lc.set_editor_property('dynamic_shadow_cascades', 2)
    lc.set_editor_property('cascade_distribution_exponent', 3.0)
    lc.set_editor_property('shadow_bias', 0.4)
    atm = spawn(unreal.SkyAtmosphere, 'Sky atmosphere', None, 'Generated/Lighting')
    sky = spawn(unreal.SkyLight, 'Sky light', unreal.Transform(unreal.Vector(0, 0, 3000), unreal.Rotator(0, 0, 0), unreal.Vector(1, 1, 1)), 'Generated/Lighting')
    sc = sky.light_component
    sc.set_mobility(unreal.ComponentMobility.STATIONARY)
    sc.set_editor_property('intensity', 1.0)
    sc.set_editor_property('real_time_capture', False)
    sc.set_editor_property('lower_hemisphere_is_black', False)
    sc.set_editor_property('lower_hemisphere_color', unreal.LinearColor(0.42, 0.40, 0.33, 1))   # web hemisphere ground 0xb9b09c
    fog = spawn(unreal.ExponentialHeightFog, 'Height fog', unreal.Transform(unreal.Vector(0, 0, -1000), unreal.Rotator(0, 0, 0), unreal.Vector(1, 1, 1)), 'Generated/Lighting')
    fc = fog.component
    fc.set_editor_property('fog_density', 0.012)
    fc.set_editor_property('fog_height_falloff', 0.05)
    fc.set_editor_property('fog_inscattering_luminance', srgb_hex(0xc6d3dc))   # web fog colour
    fc.set_editor_property('start_distance', 3000.0)
    pp = spawn(unreal.PostProcessVolume, 'Post process', None, 'Generated/Lighting')
    pp.set_editor_property('unbound', True)
    s = pp.settings
    for k, v in [('override_auto_exposure_method', True), ('auto_exposure_method', unreal.AutoExposureMethod.AEM_MANUAL),
                 ('override_auto_exposure_apply_physical_camera_exposure', True), ('auto_exposure_apply_physical_camera_exposure', False),
                 ('override_auto_exposure_bias', True), ('auto_exposure_bias', 0.3),
                 ('override_bloom_intensity', True), ('bloom_intensity', 0.35),
                 ('override_ambient_occlusion_intensity', True), ('ambient_occlusion_intensity', 0.55),
                 ('override_ambient_occlusion_radius', True), ('ambient_occlusion_radius', 120.0),
                 ('override_vignette_intensity', True), ('vignette_intensity', 0.25),
                 ('override_motion_blur_amount', True), ('motion_blur_amount', 0.0),
                 ('override_lens_flare_intensity', True), ('lens_flare_intensity', 0.0)]:
        try:
            s.set_editor_property(k, v)
        except Exception as e:
            log('post process setting', k, e)
    pp.settings = s
    # Lightmass importance volume over the walkable neighbourhood (+-540 m) and the apartment block.
    lv = spawn(unreal.LightmassImportanceVolume, 'Lightmass importance', unreal.Transform(unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0), unreal.Vector(1, 1, 1)), 'Generated/Lighting')
    lv.set_actor_scale3d(unreal.Vector(550.0 * 2 / 2, 550.0 * 2 / 2, 60.0 / 2))
    # Apartment lamps (web: PointLight(color, intensity in candela, distance, decay)).
    for i, l in enumerate(game['apartment']['lights']):
        pl = spawn(unreal.PointLight, f'Apartment lamp {i}', unreal.Transform(ue_loc(l['pos']), unreal.Rotator(0, 0, 0), unreal.Vector(1, 1, 1)), 'Generated/Lighting')
        c = pl.point_light_component
        c.set_mobility(unreal.ComponentMobility.STATIC)
        c.set_editor_property('intensity_units', unreal.LightUnits.CANDELAS)
        c.set_editor_property('intensity', float(l['intensity']) * 1.6)
        c.set_editor_property('light_color', unreal.Color((l['color'] >> 16) & 255, (l['color'] >> 8) & 255, l['color'] & 255, 255))
        c.set_editor_property('attenuation_radius', float(l['distance']) * 100.0)
        c.set_editor_property('source_radius', 6.0)
    # Stairwell lamps: the web moves one lamp (5.5 cd, 7 m) to the landing you are on; baked lighting lights them all.
    st = game['stair']
    level_y = lambda n: (n - 4) * st['STOREY']
    half = st['STOREY'] / 2
    lamps = [(0.1, level_y(n) + 2.45 - 0.12, 6.2) for n in range(1, 5)] + [(0.1, level_y(n) + half + 2.45 - 0.12, 10.0) for n in range(0, 4)]
    for ti, t in enumerate(game['towers']):
        M = t['matrix']
        for k, (x, y, z) in enumerate(lamps):
            wx = M[0] * x + M[4] * y + M[8] * z + M[12]
            wy = M[1] * x + M[5] * y + M[9] * z + M[13]
            wz = M[2] * x + M[6] * y + M[10] * z + M[14]
            pl = spawn(unreal.PointLight, f'Stair lamp {ti}.{k}', unreal.Transform(ue_loc([wx, wy, wz]), unreal.Rotator(0, 0, 0), unreal.Vector(1, 1, 1)), 'Generated/Lighting/Stairs')
            c = pl.point_light_component
            c.set_mobility(unreal.ComponentMobility.STATIC)
            c.set_editor_property('intensity_units', unreal.LightUnits.CANDELAS)
            c.set_editor_property('intensity', 5.5)
            c.set_editor_property('light_color', unreal.Color(255, 233, 200, 255))
            c.set_editor_property('attenuation_radius', 700.0)
            c.set_editor_property('source_radius', 11.0)
    log('lighting placed')


def world_settings():
    """Lightmass: three bounces, sky colour from the web's hemisphere light, a volumetric lightmap for moving things."""
    ws = unreal.EditorLevelLibrary.get_editor_world().get_world_settings()
    lm = ws.get_editor_property('lightmass_settings')
    for k, v in [('num_indirect_lighting_bounces', 3), ('num_sky_lighting_bounces', 1), ('static_lighting_level_scale', 1.0),
                 ('indirect_lighting_quality', 1.0), ('indirect_lighting_smoothness', 1.0), ('use_ambient_occlusion', True),
                 ('direct_illumination_occlusion_fraction', 0.4), ('indirect_illumination_occlusion_fraction', 0.8),
                 ('volume_lighting_method', unreal.VolumeLightingMethod.VLM_VOLUMETRIC_LIGHTMAP), ('volumetric_lightmap_detail_cell_size', 400.0)]:
        try:
            lm.set_editor_property(k, v)
        except Exception as e:
            log('lightmass setting', k, e)
    ws.set_editor_property('lightmass_settings', lm)


def main():
    open_level()
    place_static()
    lights()
    world_settings()
    LES.save_current_level()
    log('saved', LEVEL)


main()
