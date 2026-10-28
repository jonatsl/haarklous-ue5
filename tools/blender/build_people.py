"""Realistic people (asked for by the user on 28 Oct 2026): MakeHuman figures (CC0 assets) built with the MPFB 2 add-on.

Run with the project's private Blender profile (MPFB and the asset packs live there, the user's own Blender is untouched):
    set BLENDER_USER_RESOURCES=<repo>\\tools\\out\\blender-user
    blender -b -P tools/blender/build_people.py [-- avatar_0 mission_hostile ...]
tools/fetch-library.py downloads MPFB and the packs; tools/setup-people.ps1 installs them into that profile.

Players (avatar_0..4, one per player slot): a skinned mesh with MPFB's game_engine skeleton (Unreal mannequin bone
names), in the web avatar's rest pose: arms and legs straight down (the web rig's joints at zero rotation; the arms 6
degrees out so the hands clear the hips). The game turns the bones like the web rig's joints (AHKRigActor). The
jacket (a knit sweater) is tinted with the slot's jacket colour (game-avatars.js SLOT_COLORS), the trousers with the web's
dark trouser colour, so players still tell each other apart.
Mission figures (mission_hostage, mission_hostile): static meshes in the web mannequins' poses (case-game.js): the
hostage kneels with the hands on the head; the armed suspect aims a pistol with both hands (muzzle where the web's muzzle
flash is), wears a balaclava and the red armband. They replace the 'body' meshes of those rigs.

Output: tools/out/fbx/people/<name>.fbx and tools/out/fbx/people/people.json (materials: texture files, tint colours)
for tools/unreal/import_people.py.
"""
import bpy
import bmesh
import json
import math
import os
import sys

import numpy as np
from mathutils import Matrix, Vector

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
OUT = os.path.join(ROOT, 'tools', 'out', 'fbx', 'people')

from bl_ext.user_default.mpfb.services.humanservice import HumanService  # noqa: E402

SLOT = ['c0392b', '2471a3', '1e8449', 'd68910', '7d3c98']    # game-avatars.js SLOT_COLORS (jackets)
TROUSERS = '2b3140'                                           # the web avatar's trousers
RACES = {'caucasian': (0, 1, 0), 'african': (1, 0, 0), 'asian': (0, 0, 1)}

PEOPLE = {
    'avatar_0': dict(gender=1.0, age=.5, race='caucasian', muscle=.6, weight=.5, height=.6, skin='young_caucasian_male',
                     hair='short02', brows='eyebrow001', top=SLOT[0], boots='toigo_ankle_boots_male'),
    'avatar_1': dict(gender=0.0, age=.5, race='caucasian', muscle=.5, weight=.45, height=.5, skin='young_caucasian_female',
                     hair='ponytail01', brows='eyebrow010', top=SLOT[1], boots='toigo_ankle_boots_female'),
    'avatar_2': dict(gender=1.0, age=.45, race='african', muscle=.65, weight=.5, height=.65, skin='young_african_male',
                     hair='short04', brows='eyebrow004', top=SLOT[2], boots='toigo_ankle_boots_male'),
    'avatar_3': dict(gender=0.0, age=.5, race='asian', muscle=.5, weight=.45, height=.4, skin='young_asian_female',
                     hair='bob02', brows='eyebrow011', top=SLOT[3], boots='toigo_ankle_boots_female'),
    'avatar_4': dict(gender=1.0, age=.68, race='caucasian', muscle=.5, weight=.6, height=.55, skin='middleage_caucasian_male',
                     hair='short01', brows='eyebrow002', top=SLOT[4], boots='toigo_ankle_boots_male'),
    # case-game.js: hostage shirt 7fa7d6, jeans 3d5a80, hair 4a3426; suspect clothes 2a2e35, mask 141518, band b3261e.
    'mission_hostage': dict(gender=1.0, age=.5, race='caucasian', muscle=.5, weight=.5, height=.5, skin='young_caucasian_male2',
                            hair='short03', brows='eyebrow002', top='7fa7d6', top_garment='namuhekam_male_polo_shirt',
                            trousers='3d5a80', boots='toigo_ankle_boots_male', static=True),
    'mission_hostile': dict(gender=1.0, age=.5, race='caucasian', muscle=.7, weight=.55, height=.55, skin='young_caucasian_male',
                            hair='', brows='', top='2a2e35', trousers='2a2e35', boots='toigo_ankle_boots_male', static=True),
}

MANIFEST = {}


def log(*a):
    print('HK:', *a)


def lin(hexstr):
    """sRGB hex -> linear colour (Unreal material parameters are linear)."""
    c = [int(hexstr[i:i + 2], 16) / 255 for i in (0, 2, 4)]
    return [round(x / 12.92 if x <= .04045 else ((x + .055) / 1.055) ** 2.4, 4) for x in c]


def clear():
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o, do_unlink=True)
    for coll in (bpy.data.meshes, bpy.data.armatures, bpy.data.materials, bpy.data.images):
        for b in list(coll):
            if b.users == 0:
                coll.remove(b)


def textures_of(mat):
    out = {}
    if mat and mat.use_nodes:
        for n in mat.node_tree.nodes:
            if n.type == 'TEX_IMAGE' and n.image:
                if n.name.startswith('diffuse'):
                    out['diffuse'] = bpy.path.abspath(n.image.filepath)
                elif n.name.startswith('normal') or n.name.startswith('bump'):
                    out['normal'] = bpy.path.abspath(n.image.filepath)
    return out


def mean_luminance(path):
    img = bpy.data.images.load(path, check_existing=True)
    px = np.array(img.pixels[:], dtype=np.float32).reshape(-1, 4)
    a = px[:, 3] > .5
    rgb = px[a, :3] if a.any() else px[:, :3]
    return float((rgb @ np.array([.2126, .7152, .0722])).mean())


def build(name, p):
    clear()
    info = HumanService._create_default_human_info_dict()
    r = RACES[p['race']]
    info['phenotype'].update(gender=p['gender'], age=p['age'], muscle=p['muscle'], weight=p['weight'], height=p['height'])
    info['phenotype']['race'] = {'african': r[0], 'caucasian': r[1], 'asian': r[2]}
    info['rig'] = 'game_engine'
    info['eyes'] = 'low-poly.mhclo'
    info['eyelashes'] = 'eyelashes01.mhclo'
    info['eyebrows'] = p['brows'] + '.mhclo' if p['brows'] else ''
    info['hair'] = p['hair'] + '.mhclo' if p['hair'] else ''
    top = p.get('top_garment', 'toigo_fisherman_sweater')
    info['clothes'] = [top + '.mhclo', 'toigo_wool_pants.mhclo', p['boots'] + '.mhclo']
    info['skin_mhmat'] = p['skin'] + '.mhmat'
    info['skin_material_type'] = 'MAKESKIN'
    info['clothes_material_type'] = 'MAKESKIN'
    info['alternative_materials'] = {}
    st = HumanService.get_default_deserialization_settings()
    st['subdiv_levels'] = 0
    HumanService.deserialize_from_dict(info, st)
    arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
    meshes = [o for o in bpy.data.objects if o.type == 'MESH' and o.parent == arm]
    # Materials: one per part, named for Unreal, with what the importer needs.
    mats = {}
    for o in meshes:
        part = o.name.split('.', 1)[1] if '.' in o.name else 'body'
        kind = 'skin' if part in ('Human', 'body') or o.name == 'Human' else \
            'hair' if part == p['hair'] else 'brow' if part == p['brows'] else 'lash' if part.startswith('eyelashes') else \
            'eye' if part == 'low-poly' else 'cloth'
        if o.name == 'Human':
            kind, part = 'skin', 'skin'
        t = textures_of(o.material_slots[0].material if o.material_slots else None)
        tint = p['top'] if part == top else p.get('trousers', TROUSERS) if part == 'toigo_wool_pants' else None
        key = f'MP_{name}_{part}'.replace('-', '_')
        entry = {'kind': kind, **t}
        if tint and 'diffuse' in t:
            entry['tint'] = lin(tint)
            entry['gain'] = round(1 / max(.03, mean_luminance(t['diffuse'])), 3)   # keeps the cloth's shading, averages to the tint
        m = bpy.data.materials.new(key)
        o.data.materials.clear()
        o.data.materials.append(m)
        mats[key] = entry
    return arm, meshes, mats


def update():
    bpy.context.view_layer.update()


def point(arm, bone, direction):
    """Turn a pose bone (armature space) so that it points along direction."""
    update()
    pb = arm.pose.bones[bone]
    head = pb.head.copy()
    q = (pb.tail - pb.head).normalized().rotation_difference(Vector(direction).normalized())
    pb.matrix = Matrix.Translation(head) @ q.to_matrix().to_4x4() @ Matrix.Translation(-head) @ pb.matrix
    update()


def two_bone(arm, upper, lower, target, pole):
    """Point the two bones so that the end of lower reaches target, the middle joint bending toward pole."""
    update()
    pu, pl = arm.pose.bones[upper], arm.pose.bones[lower]
    s = pu.head.copy()
    a, b = (pu.tail - pu.head).length, (pl.tail - pl.head).length
    t = Vector(target)
    d = t - s
    L = min(d.length, a + b - 1e-4)
    dn = d.normalized()
    ca = (a * a + L * L - b * b) / (2 * a * L)
    ang = math.acos(max(-1.0, min(1.0, ca)))
    pv = Vector(pole) - s
    pv = (pv - dn * pv.dot(dn)).normalized()
    e = s + dn * (a * math.cos(ang)) + pv * (a * math.sin(ang))
    point(arm, upper, e - s)
    point(arm, lower, t - arm.pose.bones[lower].head)


def curl(arm, side, angles):
    """Bend the four fingers' joints toward the palm (radians for joints 1, 2, 3), the way the rest hand already curves."""
    update()
    pb = arm.pose.bones
    k = (pb[f'pinky_01_{side}'].head - pb[f'index_01_{side}'].head).normalized()
    d1 = (pb[f'middle_01_{side}'].tail - pb[f'middle_01_{side}'].head).normalized()
    d3 = (pb[f'middle_03_{side}'].tail - pb[f'middle_03_{side}'].head).normalized()
    sgn = 1 if d1.cross(d3).dot(k) >= 0 else -1
    for f in ('index', 'middle', 'ring', 'pinky'):
        for j, ang in enumerate(angles, 1):
            b = pb[f'{f}_0{j}_{side}']
            head = b.head.copy()
            b.matrix = Matrix.Translation(head) @ Matrix.Rotation(sgn * ang, 4, k) @ Matrix.Translation(-head) @ b.matrix
            update()


def W(x, y, z):
    """Web frame (x right, y up, the figure facing -z, its left at -x) -> this Blender figure's frame (facing -Y, its
    left at +X)."""
    return Vector((-x, z, y))


def rest_pose(arm):
    """The web avatar's joints at zero rotation: arms (6 degrees out) and legs straight down."""
    out = math.radians(6)
    for side, sx in (('l', 1), ('r', -1)):
        for b in ('upperarm', 'lowerarm', 'hand'):
            point(arm, f'{b}_{side}', (sx * math.sin(out), 0, -math.cos(out)))
        for b in ('thigh', 'calf'):
            point(arm, f'{b}_{side}', (0, 0, -1))
        curl(arm, side, (.3, .45, .3))      # relaxed hands, not the model's flat open hands


def bake(arm, meshes, keep_armature=True):
    """Apply the pose to the meshes (and the masks that hide the body under the clothes), then make it the rest pose."""
    bpy.ops.object.select_all(action='DESELECT')
    for o in meshes:
        bpy.context.view_layer.objects.active = o
        o.select_set(True)
        bpy.ops.object.convert(target='MESH')
        o.select_set(False)
    if keep_armature:
        bpy.context.view_layer.objects.active = arm
        arm.select_set(True)
        bpy.ops.object.mode_set(mode='POSE')
        bpy.ops.pose.armature_apply(selected=False)
        bpy.ops.object.mode_set(mode='OBJECT')
        arm.select_set(False)
        for o in meshes:
            m = o.modifiers.new('Armature', 'ARMATURE')
            m.object = arm


def add_part(name, bm, mat_key, mats, entry):
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    o = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(o)
    m = bpy.data.materials.new(mat_key)
    me.materials.append(m)
    mats[mat_key] = entry
    return o


def hostile_extras(arm, meshes, mats):
    """Balaclava (the head and neck skin pushed out 6 mm, an eye slit), the red armband, the pistol."""
    body = next(o for o in meshes if o.name == 'Human')
    update()
    head_g = body.vertex_groups.get('head')
    neck_g = body.vertex_groups.get('neck_01')
    eyes = next((o for o in meshes if 'low-poly' in o.name), None)
    bm = bmesh.new()
    bm.from_mesh(body.data)
    dl = bm.verts.layers.deform.active
    centres = []
    if eyes:
        pts = [(eyes.matrix_world @ v.co) for v in eyes.data.vertices]
        for sx in (1, -1):
            side = [q for q in pts if q.x * sx > 0]
            centres.append(sum(side, Vector()) / len(side))
    front = min(c.y for c in centres) if centres else 0
    kill = []
    for v in bm.verts:
        w = v[dl].get(head_g.index, 0) + (v[dl].get(neck_g.index, 0) if neck_g else 0)
        if w < .35:
            kill.append(v)
        elif v.co.y < front + .02 and any(((v.co.x - c.x) / .03) ** 2 + ((v.co.z - c.z) / .016) ** 2 < 1 for c in centres):
            kill.append(v)          # the eye opening (case-game.js: a 15 x 3.5 cm strip of skin)
    bmesh.ops.delete(bm, geom=kill, context='VERTS')
    bm.normal_update()
    for v in bm.verts:
        v.co += v.normal * .006
    meshes.append(add_part('Balaclava', bm, 'MP_mission_hostile_mask', mats, {'kind': 'cloth', 'color': lin('141518'), 'rough': .95}))
    # Armband on the right upper arm (the figure's right is -X here), at the middle of the bone.
    pb = arm.pose.bones['upperarm_r']
    c, ax = (pb.head + pb.tail) / 2, (pb.tail - pb.head).normalized()
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=False, segments=16, radius1=.068, radius2=.066, depth=.075)
    q = Vector((0, 0, 1)).rotation_difference(ax)
    bmesh.ops.transform(bm, matrix=Matrix.Translation(c) @ q.to_matrix().to_4x4(), verts=bm.verts)
    meshes.append(add_part('Armband', bm, 'MP_mission_hostile_band', mats, {'kind': 'cloth', 'color': lin('b3261e'), 'rough': .8}))
    # Pistol between the hands: slide and barrel forward to just short of the web's muzzle flash (z = -.67).
    bm = bmesh.new()
    for (cx, cy, cz), (sx, sy, sz), tilt in (((0, 1.36, -.52), (.032, .045, .17), 0), ((0, 1.295, -.455), (.03, .1, .045), .3),
                                              ((0, 1.335, -.48), (.026, .02, .07), 0)):
        g = bmesh.ops.create_cube(bm, size=1)
        vs = g['verts']
        bmesh.ops.scale(bm, vec=(sx, sz, sy), verts=vs)
        bmesh.ops.rotate(bm, cent=(0, 0, 0), matrix=Matrix.Rotation(tilt, 3, 'X'), verts=vs)
        bmesh.ops.translate(bm, vec=W(cx, cy, cz), verts=vs)
    meshes.append(add_part('Pistol', bm, 'MP_mission_hostile_gun', mats, {'kind': 'metal', 'color': lin('1a1a1a'), 'rough': .4}))


def pose_hostile(arm):
    # case-game.js: both arms forward holding the pistol at 1.31 m, 0.43 m in front of the chest.
    rest_pose(arm)
    two_bone(arm, 'upperarm_l', 'lowerarm_l', W(-.025, 1.3, -.42), W(-.6, .9, -.1))
    two_bone(arm, 'upperarm_r', 'lowerarm_r', W(.02, 1.31, -.45), W(.6, .9, -.1))
    for side in ('l', 'r'):
        pb = arm.pose.bones[f'lowerarm_{side}']
        point(arm, f'hand_{side}', pb.tail - pb.head)
        curl(arm, side, (.9, 1.1, .7))      # round the pistol's grip


def pose_hostage(arm):
    # Kneeling on both shins, upright, hands on top of the head, elbows out to the sides.
    update()
    hip = arm.pose.bones['thigh_l'].head.z
    pel = arm.pose.bones['pelvis']
    drop = hip - .56
    pel.matrix = Matrix.Translation((0, 0, -drop)) @ pel.matrix
    update()
    for side, sx in (('l', -1), ('r', 1)):
        two_bone(arm, f'thigh_{side}', f'calf_{side}', W(sx * .12, .07, .33), W(sx * .12, .1, -1.0))
        point(arm, f'foot_{side}', W(0, -.25, 1) - W(0, 0, 0))
        two_bone(arm, f'upperarm_{side}', f'lowerarm_{side}', W(sx * .05, 1.36, -.03), W(sx * .8, 1.3, .05))
        pb = arm.pose.bones[f'lowerarm_{"l" if sx < 0 else "r"}']
        point(arm, f'hand_{side}', pb.tail - pb.head)


def export(name, objs, path):
    bpy.ops.object.select_all(action='DESELECT')
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, object_types={'ARMATURE', 'MESH'}, add_leaf_bones=False,
                             use_armature_deform_only=True, bake_anim=False, apply_unit_scale=True,
                             apply_scale_options='FBX_SCALE_NONE', axis_forward='-Z', axis_up='Y', mesh_smooth_type='FACE',
                             armature_nodetype='NULL', primary_bone_axis='Y', secondary_bone_axis='X')


def inflate(o, d):
    """Push a garment out along its normals (the top over the trousers' waistband, which otherwise pokes through)."""
    bm = bmesh.new()
    bm.from_mesh(o.data)
    bm.normal_update()
    for v in bm.verts:
        v.co += v.normal * d
    bm.to_mesh(o.data)
    bm.free()


def make(name):
    p = PEOPLE[name]
    arm, meshes, mats = build(name, p)
    top = p.get('top_garment', 'toigo_fisherman_sweater')
    if p.get('static'):
        (pose_hostile if name == 'mission_hostile' else pose_hostage)(arm)
        if name == 'mission_hostile':
            for o in [o for o in meshes if 'eyebrow' in o.name]:
                meshes.remove(o); bpy.data.objects.remove(o, do_unlink=True)
            bake(arm, meshes, keep_armature=False)
            hostile_extras(arm, meshes, mats)
        else:
            bake(arm, meshes, keep_armature=False)
        for o in meshes:
            if o.name.endswith(top):
                inflate(o, .007)
        # One mesh named like the rig's 'body' node mesh, turned to face web -z (Blender +Y in tools' to_blender).
        bpy.data.objects.remove(arm, do_unlink=True)
        bpy.ops.object.select_all(action='DESELECT')
        for o in meshes:
            o.select_set(True)
        bpy.context.view_layer.objects.active = meshes[0]
        bpy.ops.object.join()
        body = bpy.context.view_layer.objects.active
        body.name = f'SM_{name}__body'
        body.matrix_world = Matrix.Rotation(math.pi, 4, 'Z') @ body.matrix_world
        bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
        export(name, [body], os.path.join(OUT, name + '.fbx'))
        log(name, 'static,', len(body.data.polygons), 'faces')
    else:
        rest_pose(arm)
        bake(arm, meshes)
        for o in meshes:
            if o.name.endswith(top):
                inflate(o, .007)
        arm.name = f'SK_{name}'
        export(name, [arm] + meshes, os.path.join(OUT, name + '.fbx'))
        log(name, 'skinned,', sum(len(o.data.polygons) for o in meshes), 'faces,', len(arm.data.bones), 'bones')
    MANIFEST[name] = {'static': bool(p.get('static')), 'materials': mats}


os.makedirs(OUT, exist_ok=True)
WANT = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else list(PEOPLE)
mpath = os.path.join(OUT, 'people.json')
if os.path.exists(mpath):
    MANIFEST.update(json.load(open(mpath)))
for n in WANT:
    make(n)
json.dump(MANIFEST, open(mpath, 'w'), indent=1)
