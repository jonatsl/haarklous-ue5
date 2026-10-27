"""The photo-real look of the lighting and the camera (27 Oct 2026), shared by build_level.py (new level) and
apply_look.py (changes the existing level without baking the lighting again).

Aimed at the Battlefield 4 look the user asked for, within what this laptop can run: a stronger sun against a
softer sky (more contrast), light shafts through trees and between buildings, screen-space contact shadows (grass,
window reveals, kerbs), bloom and lens flares off bright sky and glass, screen-space reflections in windows and car
paint, tighter ambient occlusion, cool shadows and warm highlights with a filmic curve, a touch of colour fringing
and vignette. The render switches that pay for it are set per quality level in AHKGame::ApplyQuality.
"""
import unreal


def log(*a):
    print('HK:', *a)


def _set(obj, pairs, what):
    for k, v in pairs:
        try:
            obj.set_editor_property(k, v)
        except Exception as e:
            log(what, k, e)


def sun(lc):
    _set(lc, [('intensity', 4.2),                                        # web 3.4; with less sky light: more contrast
              ('light_color', unreal.Color(255, 240, 218, 255)),          # a little warmer than the web's 0xfff3e0
              ('light_source_angle', 0.9),
              ('enable_light_shaft_bloom', True), ('bloom_scale', 0.12), ('bloom_threshold', 4.0),
              ('bloom_tint', unreal.Color(255, 236, 205, 255)),
              ('contact_shadow_length', 0.04)], 'sun')


def sky_light(sc):
    _set(sc, [('intensity', 1.2)], 'sky light')


def post(s):
    _set(s, [('override_auto_exposure_bias', True), ('auto_exposure_bias', 0.4),
             ('override_bloom_intensity', True), ('bloom_intensity', 0.55),
             ('override_lens_flare_intensity', True), ('lens_flare_intensity', 0.22),
             ('override_lens_flare_bokeh_size', True), ('lens_flare_bokeh_size', 2.5),
             ('override_lens_flare_threshold', True), ('lens_flare_threshold', 6.0),
             ('override_screen_space_reflection_intensity', True), ('screen_space_reflection_intensity', 100.0),
             ('override_screen_space_reflection_quality', True), ('screen_space_reflection_quality', 50.0),
             ('override_screen_space_reflection_max_roughness', True), ('screen_space_reflection_max_roughness', 0.45),
             ('override_ambient_occlusion_intensity', True), ('ambient_occlusion_intensity', 0.85),
             ('override_ambient_occlusion_radius', True), ('ambient_occlusion_radius', 110.0),
             ('override_ambient_occlusion_power', True), ('ambient_occlusion_power', 1.6),
             ('override_color_saturation', True), ('color_saturation', unreal.Vector4(0.92, 0.92, 0.92, 1.0)),
             ('override_color_contrast', True), ('color_contrast', unreal.Vector4(1.1, 1.1, 1.1, 1.0)),
             ('override_color_gain_shadows', True), ('color_gain_shadows', unreal.Vector4(0.95, 0.99, 1.05, 1.0)),
             ('override_color_gain_highlights', True), ('color_gain_highlights', unreal.Vector4(1.04, 1.01, 0.95, 1.0)),
             ('override_film_slope', True), ('film_slope', 0.9),
             ('override_film_toe', True), ('film_toe', 0.57),
             ('override_white_temp', True), ('white_temp', 6100.0),
             ('override_scene_fringe_intensity', True), ('scene_fringe_intensity', 0.35),
             ('override_vignette_intensity', True), ('vignette_intensity', 0.32)], 'post')
    return s
