"""Construction-site machines, crane loads and site furniture (new feature, 28 Oct 2026), built headless:

    blender -b --factory-startup -P tools/blender/build_machines.py

Each machine is a rig like the web vehicles' (Content/Data/rigs/<rig>.json: a node tree in the web frame, metres,
y up, forward -z, origin on the ground) whose nodes the C++ code (HKSiteVehicles) moves: tracks, blade arm and
blade, upper / boom / stick / bucket, front and rear frames, wheels, lift arm, tipping bodies, crane superstructure,
telescopic boom sections, outriggers, trolley, hook. Sizes follow real machines of each class:
  site_dozer      Cat D6 class dozer: 3.0 m tracks at 1.98 m gauge, 3.6 x 1.25 m semi-U blade
  site_excavator  30 t excavator: 4.65 m tracks at 2.6 m gauge, 6.15 m boom, 3.0 m stick, 1.9 m3 bucket
  site_loader     Volvo L120 class wheel loader: articulated, 3.2 m wheelbase, 1.6 m tyres, 3.6 m3 bucket
  site_hauler     Volvo A30 class articulated hauler: 6 wheels, 17.5 m3 tipping body
  site_tipper     8x4 tipper truck: two steered front axles, 16 m3 tipping body
  site_crane      3-axle all-terrain crane: telescopic boom 10.6-34 m, four outriggers
  site_tower      tower crane: 30 m mast, 40 m jib, trolley and hook
Meshes: tools/out/fbx/<rig>.fbx (objects SM_<rig>__<node>), props and loads in tools/out/fbx/site.fbx (SM_Site_<kind>,
SM_Load_<kind>). Material slots are MI_Site_* (made by tools/unreal/materials_site.py).
"""
import json
import math
import os
import sys

import bpy
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from site_geometry import Mesh, box, cyl, prism, lathe_x, tyre, track, lattice, rot_x, rot_y, rot_z  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
OUT = os.path.join(ROOT, 'tools', 'out', 'fbx')
RIGS = os.path.join(ROOT, 'unreal', 'Haarklous', 'Content', 'Data', 'rigs')

Y, DK, ST, CH, RB, GL, LT, OR, WH, BL, RD, DIRT = ('MI_Site_Yellow', 'MI_Site_Dark', 'MI_Site_Steel', 'MI_Site_Chrome', 'MI_Site_Rubber',
                                                  'MI_Site_Glass', 'MI_Site_Light', 'MI_Site_Beacon', 'MI_Site_White', 'MI_Site_Blue',
                                                  'MI_Site_Red', 'MI_Site_Soil')


# ---------------------------------------------------------------------------------------------------------------------
class Rig:
    def __init__(self, name):
        self.name, self.nodes, self.meshes = name, [], {}

    def node(self, name, parent=None, pos=(0, 0, 0), mesh=None, quat=(0, 0, 0, 1), visible=True, ground=0.0):
        idx = len(self.nodes)
        d = {'name': name, 'parent': -1 if parent is None else parent, 'pos': [float(v) for v in pos], 'quat': list(quat),
             'scale': [1, 1, 1], 'visible': visible}
        if mesh is not None and len(mesh.P):
            key = f"SM_{self.name}__{name.replace('[', '_').replace(']', '').replace('.', '_')}"
            d['mesh'] = key
            self.meshes[key] = (mesh, ground)
        self.nodes.append(d)
        return idx

    def save(self):
        os.makedirs(RIGS, exist_ok=True)
        json.dump({'rig': self.name, 'source': 'site', 'levelStatic': False, 'nodes': self.nodes, 'lights': []},
                  open(os.path.join(RIGS, self.name + '.json'), 'w'), indent=0)
        export(self.name, self.meshes)
        print(f'HK: {self.name}: {len(self.nodes)} nodes, {sum(len(m.P) for m, _ in self.meshes.values())} triangles')


def to_blender(v):
    return np.stack([v[:, 0], -v[:, 2], v[:, 1]], axis=1)


def build_object(name, mesh, ground):
    P = np.array([[p for p in t] for t in mesh.P], float)        # T x 3 x 3 web frame
    T = len(P)
    slots = sorted(set(mesh.M))
    verts = to_blender(P.reshape(-1, 3))
    me = bpy.data.meshes.new(name)
    me.from_pydata(verts.tolist(), [], [(3 * i, 3 * i + 1, 3 * i + 2) for i in range(T)])
    me.update()
    me.polygons.foreach_set('material_index', np.array([slots.index(k) for k in mesh.M], dtype=np.int32))
    # Box-projected uvs in metres (along the face's dominant axis), grime from the height above the ground.
    nrm = np.cross(P[:, 1] - P[:, 0], P[:, 2] - P[:, 0])
    ax = np.argmax(np.abs(nrm), axis=1)
    uv = np.zeros((T, 3, 2))
    for k, (u, v) in enumerate(((2, 1), (0, 2), (0, 1))):
        sel = ax == k
        uv[sel, :, 0] = P[sel][:, :, u]; uv[sel, :, 1] = P[sel][:, :, v]
    uvl = me.uv_layers.new(name='UVMap')
    uvl.data.foreach_set('uv', uv.reshape(-1, 2).astype(np.float32).ravel())
    h = P[:, :, 1] + ground
    rng = np.random.default_rng(abs(hash(name)) % 2**31)
    noise = 0.5 + 0.5 * np.sin(P[:, :, 0] * 3.1 + P[:, :, 2] * 2.3) * np.sin(P[:, :, 2] * 1.7 + P[:, :, 1] * 4.1)
    grime = np.clip(1.0 - (h - 0.2) / 1.6, 0, 1) * 0.75 + noise * 0.25
    col = np.zeros((T, 3, 4)); col[:, :, 0] = np.clip(grime, 0, 1); col[:, :, 3] = 1
    ca = me.color_attributes.new('Col', 'FLOAT_COLOR', 'CORNER')
    ca.data.foreach_set('color', col.reshape(-1, 4).astype(np.float32).ravel())
    for p in me.polygons:
        p.use_smooth = False
    for k in slots:
        me.materials.append(bpy.data.materials.get(k) or bpy.data.materials.new(k))
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    # Smooth shading where faces meet at less than 35 degrees (cylinders, tyres), sharp edges elsewhere.
    ob.modifiers.new('weld', 'WELD').merge_threshold = 1e-4
    bpy.context.view_layer.objects.active = ob
    try:
        bpy.ops.object.shade_auto_smooth(angle=math.radians(35))
    except Exception:
        for p in me.polygons:
            p.use_smooth = True
    return ob


def export(name, meshes):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    for key, (mesh, ground) in meshes.items():
        build_object(key, mesh, ground)
    os.makedirs(OUT, exist_ok=True)
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.export_scene.fbx(filepath=os.path.join(OUT, name + '.fbx'), use_selection=True, apply_unit_scale=True,
                             apply_scale_options='FBX_SCALE_NONE', object_types={'MESH'}, use_mesh_modifiers=True,
                             mesh_smooth_type='FACE', colors_type='LINEAR', add_leaf_bones=False, axis_forward='-Z', axis_up='Y')


def ram_meshes(r_barrel=.09, r_rod=.055, barrel=DK):
    b = Mesh(); cyl(b, (0, -.5, 0), (0, .5, 0), r_barrel, barrel, seg=10)
    r = Mesh(); cyl(r, (0, -.5, 0), (0, .5, 0), r_rod, CH, seg=8)
    return b, r


def add_rams(rig, parent, count):
    for i in range(count):
        b, r = ram_meshes()
        rig.node(f'rams[{i}].barrel', parent, mesh=b)
        rig.node(f'rams[{i}].rod', parent, mesh=r)


def beacon(m, c):
    cyl(m, c, (c[0], c[1] + .05, c[2]), .07, DK, seg=10)
    cyl(m, (c[0], c[1] + .05, c[2]), (c[0], c[1] + .2, c[2]), .065, OR, seg=10, r2=.05)


def work_light(m, c, facing=-1):
    box(m, c, (.2, .14, .1), DK, bevel=.02)
    box(m, (c[0], c[1], c[2] + facing * .052), (.16, .1, .01), LT)


def cab_box(m, c, w, h, d, frame=Y, roof=Y, post=.06, door_side=-1):
    """A machine cab: four posts, glass all round, a roof and a floor."""
    cx, cy, cz = c
    for sx in (-1, 1):
        for sz in (-1, 1):
            box(m, (cx + sx * (w / 2 - post / 2), cy + h / 2, cz + sz * (d / 2 - post / 2)), (post, h, post), frame)
    box(m, (cx, cy + h + .05, cz), (w + .1, .1, d + .1), roof, bevel=.03)
    box(m, (cx, cy + .04, cz), (w, .08, d), DK)
    g = .02
    box(m, (cx, cy + h / 2, cz - d / 2 + g), (w - post * 2, h - .08, .015), GL)
    box(m, (cx, cy + h / 2, cz + d / 2 - g), (w - post * 2, h - .08, .015), GL)
    for sx in (-1, 1):
        box(m, (cx + sx * (w / 2 - g), cy + h / 2, cz), (.015, h - .08, d - post * 2), GL)
    # seat and controls seen through the glass
    box(m, (cx, cy + .45, cz + d * .15), (.5, .1, .5), DK)
    box(m, (cx, cy + .8, cz + d * .38), (.5, .6, .1), DK)
    return m


# ---------------------------------------------------------------------------------------------------------------------
def dozer():
    r = Rig('site_dozer')
    root = r.node('root')
    b = Mesh()
    box(b, (0, .78, .05), (1.55, .5, 3.1), DK, bevel=.05)                       # main frame
    # Engine hood sloping down to the front (a "visibility" hood) so the operator sees the blade over it; the exhaust
    # and the pre-cleaner stand at its sides, out of the middle of the view.
    prism(b, [(-1.9, .95), (-.2, .95), (-.2, 1.85), (-1.62, 1.55), (-1.9, 1.42)], -.66, .66, Y)   # engine hood
    box(b, (0, 1.2, -1.92), (1.15, .44, .06), DK)                                # radiator grille
    for k in range(5):
        box(b, (0, 1.02 + k * .085, -1.955), (1.05, .025, .02), ST)
    cyl(b, (.56, 1.66, -1.15), (.56, 2.45, -1.15), .075, DK, seg=10)             # exhaust
    cyl(b, (-.56, 1.68, -1.05), (-.56, 2.0, -1.05), .11, DK, seg=12)              # pre-cleaner
    for sx in (-1, 1):
        box(b, (sx * 1.03, 1.06, .2), (.62, .06, 3.0), Y, bevel=.02)            # fenders
        box(b, (sx * 1.3, .86, .2), (.06, .4, 3.0), Y)
        box(b, (sx * .72, 1.25, 1.25), (.12, .5, .9), DK)                         # tanks
        box(b, (sx * 1.18, 1.25, .95), (.2, .05, .4), ST)                          # step
    cab_box(b, (0, 1.32, .86), 1.36, 1.62, 1.24)
    box(b, (0, 1.12, 1.68), (1.35, .55, .45), DK, bevel=.06)                      # rear counterweight / ripper frame
    for sx in (-1, 1):
        work_light(b, (sx * .55, 3.02, .22), -1)
        work_light(b, (sx * .55, 3.02, 1.47), 1)
    beacon(b, (.5, 3.04, 1.3))
    cyl(b, (-.74, 1.2, .3), (-.74, 2.3, .3), .025, Y, seg=6)                        # grab rail (beside the door)
    r.node('body', root, mesh=b, ground=0)
    for i, sx in enumerate((-1, 1)):
        t = Mesh(); track(t, 3.05, .9, .56, ST, DK, 0, front_r=.36, rear_r=.42, shoes=42)
        r.node(f'tracks[{i}]', root, (sx * .99, 0, 0), mesh=t)
    # Blade on push arms outside the tracks: arm pivot on the track frame, blade at the arms' front ends.
    arm = r.node('bladeArm', root, (0, .62, -.45))
    a = Mesh()
    for sx in (-1, 1):
        box(a, (sx * 1.42, -.22, -.98), (.16, .26, 2.05), Y, rot=rot_x(.2), bevel=.03)
        cyl(a, (sx * 1.34, 0, 0), (sx * 1.5, 0, 0), .11, DK, seg=10)
    r.meshes.update({})
    r.nodes[arm]['mesh'] = None
    arm_mesh_node = r.node('bladeArmMesh', arm, mesh=a)
    bl = Mesh()
    front = [(0, 0), (.13, .3), (.19, .6), (.15, .9), (.03, 1.1), (-.12, 1.25)]
    back = [(z + .12, y) for z, y in front]
    for k in range(len(front) - 1):
        (z0, y0), (z1, y1) = front[k], front[k + 1]
        bl.quad((1.8, y0, z0), (-1.8, y0, z0), (-1.8, y1, z1), (1.8, y1, z1), Y)
        (z0, y0), (z1, y1) = back[k], back[k + 1]
        bl.quad((-1.8, y0, z0), (1.8, y0, z0), (1.8, y1, z1), (-1.8, y1, z1), DK)
    for sx in (-1, 1):
        pts = [(sx * 1.8, y, z) for z, y in front] + [(sx * 1.8, y, z) for z, y in reversed(back)]
        bl.poly(pts if sx > 0 else pts[::-1], Y)
        box(bl, (sx * 1.82, .55, .1), (.04, 1.1, .45), Y)                         # end bits
    box(bl, (0, .05, .03), (3.6, .1, .08), ST)                                   # cutting edge
    for k in range(5):
        box(bl, (-1.4 + k * .7, .62, .3), (.06, 1.0, .2), DK)                     # back ribs
    r.node('blade', arm, (0, -.62, -1.95), mesh=bl, ground=0)
    r.node('bladeEdge', arm, (0, -.62, -1.95))
    add_rams(r, root, 2)
    r.save()


def excavator():
    r = Rig('site_excavator')
    root = r.node('root')
    b = Mesh()
    box(b, (0, .78, 0), (1.35, .45, 1.9), DK, bevel=.06)
    cyl(b, (0, .98, 0), (0, 1.06, 0), 1.0, DK, seg=24)
    r.node('body', root, mesh=b)
    for i, sx in enumerate((-1, 1)):
        t = Mesh(); track(t, 4.65, .95, .6, ST, DK, 0, front_r=.38, rear_r=.38, shoes=56)
        r.node(f'tracks[{i}]', root, (sx * 1.3, 0, 0), mesh=t)
    upper = r.node('upper', root, (0, 1.06, 0))
    u = Mesh()
    box(u, (.12, .16, .7), (2.95, .32, 4.3), DK, bevel=.05)                       # deck
    box(u, (0, .78, 2.45), (2.9, 1.15, .95), Y, bevel=.22)                         # counterweight
    box(u, (0, .3, 2.45), (2.8, .25, .9), DK, bevel=.06)
    box(u, (.55, 1.0, 1.25), (1.8, .55, 1.5), Y, bevel=.08)                        # engine hood
    for k in range(6):
        box(u, (.55, 1.28, .7 + k * .2), (1.5, .02, .08), DK)                       # louvres
    box(u, (1.05, .8, -.2), (.85, 1.0, 1.75), Y, bevel=.06)                         # right-hand tanks / boxes
    box(u, (.15, .62, -.9), (.85, .9, .9), DK, bevel=.05)                          # boom foot
    cyl(u, (1.0, 1.25, 1.0), (1.0, 1.85, 1.0), .07, DK, seg=10)                     # exhaust
    for z in (-1.0, .4):
        cyl(u, (1.45, .32, z), (1.45, 1.35, z), .025, Y, seg=6)
    cyl(u, (1.45, 1.35, -1.0), (1.45, 1.35, .4), .025, Y, seg=6)                   # hand rail
    beacon(u, (.6, 1.3, 1.9))
    r.node('upperMesh', upper, mesh=u)
    cab = r.node('cab', upper, (-.85, .32, -.5))
    c = Mesh(); cab_box(c, (0, 0, 0), 1.0, 1.7, 1.75)
    work_light(c, (.3, 1.82, -.85), -1)
    r.node('cabMesh', cab, mesh=c)
    for i in range(2):
        r.node(f'levers[{i}]', cab, ((-1 if i == 0 else 1) * .3, .7, .25))
    boom = r.node('boomPivot', upper, (.15, 1.0, -1.15))
    bm = Mesh()
    path = [(0, 0, 0), (0, .55, -1.5), (0, .95, -3.0), (0, .62, -4.8), (0, 0, -6.15)]
    sizes = [(.62, .82), (.6, .8), (.56, .74), (.5, .64)]
    for k in range(4):
        p0, p1 = np.array(path[k]), np.array(path[k + 1])
        d = p1 - p0; L = np.linalg.norm(d)
        ang = math.atan2(d[1], -d[2])
        box(bm, (p0 + p1) / 2, (sizes[k][0], sizes[k][1], L + .25), Y, rot=rot_x(ang), bevel=.06)
    cyl(bm, (-.36, 0, 0), (.36, 0, 0), .16, DK, seg=12)
    cyl(bm, (-.3, 0, -6.15), (.3, 0, -6.15), .13, DK, seg=12)
    r.node('boomMesh', boom, mesh=bm)
    stick = r.node('stickPivot', boom, (0, 0, -6.15))
    sm = Mesh()
    box(sm, (0, .05, -1.35), (.42, .58, 3.3), Y, bevel=.05)
    box(sm, (0, .32, .3), (.4, .5, .7), Y, bevel=.05, rot=rot_x(-.4))
    cyl(sm, (-.26, 0, -3.0), (.26, 0, -3.0), .11, DK, seg=12)
    r.node('stickMesh', stick, mesh=sm)
    bucket = r.node('bucketPivot', stick, (0, 0, -3.0))
    bk = Mesh()
    # Backhoe bucket: the shell bulges to the stick's +y side, so with the teeth pointing down the opening (local -y)
    # faces the machine, and curling (bucket angle down) turns it up.
    wrap = [(.05, -.25), (.32, .2), (.3, .65), (.05, 1.0), (-.4, 1.2), (-.9, 1.15), (-1.3, .9), (-1.45, .45)]
    W = .72
    for k in range(len(wrap) - 1):
        (z0, y0), (z1, y1) = wrap[k], wrap[k + 1]
        bk.quad((-W, y0, z0), (W, y0, z0), (W, y1, z1), (-W, y1, z1), Y)
        bk.quad((W - .02, y0 - .02, z0 - .02), (-W + .02, y0 - .02, z0 - .02), (-W + .02, y1 - .02, z1 - .02), (W - .02, y1 - .02, z1 - .02), ST)
    for sx in (-1, 1):
        pts = [(sx * W, y, z) for z, y in wrap] + [(sx * W, -.1, -.4)]
        bk.poly(pts if sx < 0 else pts[::-1], Y)
    for k in range(5):
        x = -.56 + k * .28
        box(bk, (x, .42, -1.56), (.1, .1, .3), ST, rot=rot_x(-.5), bevel=.02)
    cyl(bk, (-.4, 0, 0), (.4, 0, 0), .1, DK, seg=10)
    r.node('bucketMesh', bucket, mesh=bk)
    r.node('tip', bucket, (0, .5, -1.62))
    d = Mesh(); box(d, (0, 0, 0), (1.2, .5, .9), DIRT, bevel=.18)
    r.node('dirt', bucket, (0, .55, -.55), mesh=d, visible=False)
    add_rams(r, root, 3)
    r.save()


def loader():
    r = Rig('site_loader')
    root = r.node('root')
    rear = Mesh()
    box(rear, (0, .95, 1.55), (1.3, .6, 2.7), DK, bevel=.05)
    box(rear, (0, 1.72, 2.35), (1.55, .95, 1.85), Y, bevel=.14)                   # engine hood
    for k in range(5):
        box(rear, (0, 2.2, 1.8 + k * .25), (1.3, .02, .09), DK)
    box(rear, (0, 1.15, 3.35), (2.25, .95, .5), Y, bevel=.18)                     # counterweight
    for sx in (-1, 1):
        box(rear, (sx * .85, 1.3, 3.61), (.22, .12, .03), RD)                       # tail lights
        box(rear, (sx * 1.0, 1.72, 1.6), (.72, .06, 1.25), Y, bevel=.03)            # fenders
        box(rear, (sx * 1.0, 1.15, 2.05), (.72, .5, .06), Y)
    cab_box(rear, (0, 1.55, .6), 1.5, 1.75, 1.6)                                  # a high cab: the operator sees the bucket
    for sx in (-1, 1):
        work_light(rear, (sx * .62, 3.4, -.18), -1)
    beacon(rear, (.55, 3.4, 1.3))
    cyl(rear, (.45, 2.2, 2.6), (.45, 2.8, 2.6), .07, DK, seg=10)
    for k in range(4):
        box(rear, (-.95, .45 + k * .32, .2), (.35, .04, .2), ST)                    # ladder
    r.node('rear', root, mesh=rear)
    for i, sx in enumerate((-1, 1)):
        t = Mesh(); tyre(t, (0, 0, 0), .8, .6, RB, Y, rim_dish=.08 * sx)
        r.node(f'rwheels[{i}]', root, (sx * 1.0, .8, 1.6), mesh=t)
    front = r.node('front', root)
    f = Mesh()
    box(f, (0, .95, -1.1), (1.15, .6, 2.1), DK, bevel=.05)
    box(f, (0, 1.4, -.45), (.9, .5, .6), Y, bevel=.05)
    for sx in (-1, 1):
        box(f, (sx * 1.0, 1.66, -1.5), (.72, .05, 1.0), Y, bevel=.02)             # short, low fenders: the bucket shows over them
        work_light(f, (sx * .65, 1.7, -1.95), -1)
    r.node('frontMesh', front, mesh=f)
    for i, sx in enumerate((-1, 1)):
        t = Mesh(); tyre(t, (0, 0, 0), .8, .6, RB, Y, rim_dish=.08 * sx)
        r.node(f'fwheels[{i}]', front, (sx * 1.0, .8, -1.6), mesh=t)
    arm = r.node('liftArm', front, (0, 1.85, -.7))
    am = Mesh()
    for sx in (-1, 1):
        box(am, (sx * .62, -.72, -1.18), (.2, .42, 2.75), Y, rot=rot_x(-math.atan2(1.45, 2.35) + .08), bevel=.04)
    box(am, (0, -.95, -1.45), (1.24, .28, .28), Y, bevel=.04)                      # cross tube
    r.node('liftArmMesh', arm, mesh=am)
    bucket = r.node('bucket', arm, (0, -1.45, -2.35))
    bk = Mesh()
    prof = [(-1.25, -.35), (.1, -.42), (.28, -.2), (.3, .3), (.18, .8), (-.25, .9)]
    W = 1.45
    for k in range(len(prof) - 1):
        (z0, y0), (z1, y1) = prof[k], prof[k + 1]
        bk.quad((-W, y0, z0), (W, y0, z0), (W, y1, z1), (-W, y1, z1), Y)
        bk.quad((W - .02, y0 + .03, z0 - .02), (-W + .02, y0 + .03, z0 - .02), (-W + .02, y1 + .03, z1 - .02), (W - .02, y1 + .03, z1 - .02), ST)
    for sx in (-1, 1):
        pts = [(sx * W, y, z) for z, y in prof]
        bk.poly(pts if sx < 0 else pts[::-1], Y)
    box(bk, (0, -.37, -1.27), (2.9, .06, .12), ST)                                 # cutting edge
    r.node('bucketMesh', bucket, mesh=bk)
    r.node('lip', bucket, (0, -.36, -1.3))
    d = Mesh(); box(d, (0, 0, 0), (2.6, .55, 1.1), DIRT, bevel=.2)
    r.node('dirt', bucket, (0, -.1, -.55), mesh=d, visible=False)
    add_rams(r, root, 3)
    r.save()


def truck_body(m, length, width, height, mat):
    """An open dump body from z = -length to 0 (hinge at the rear end), floor at y = 0, rising at the back."""
    L, W, H = length, width / 2, height
    floor = [(-L, 0), (-.6, 0), (0, .35)]
    for sx in (-1, 1):
        side = [(sx * W, 0, -L), (sx * W, H, -L - .1), (sx * W, H, 0), (sx * W, .35, 0), (sx * W, 0, -.6)]
        m.poly(side if sx > 0 else side[::-1], mat)
        m.poly([(sx * (W - .05), 0, -L), (sx * (W - .05), .35, 0), (sx * (W - .05), H - .05, 0), (sx * (W - .05), H - .05, -L)] if sx < 0 else
               [(sx * (W - .05), H - .05, -L), (sx * (W - .05), H - .05, 0), (sx * (W - .05), .35, 0), (sx * (W - .05), 0, -L)], ST)
    for k in range(len(floor) - 1):
        (z0, y0), (z1, y1) = floor[k], floor[k + 1]
        m.quad((W, y0, z0), (-W, y0, z0), (-W, y1, z1), (W, y1, z1), ST)
        m.quad((-W, y0 - .08, z0), (W, y0 - .08, z0), (W, y1 - .08, z1), (-W, y1 - .08, z1), mat)
    m.quad((-W, 0, -L), (W, 0, -L), (W, H + .25, -L - .25), (-W, H + .25, -L - .25), mat)        # front wall / cab guard
    m.quad((W, 0.02, -L + .05), (-W, .02, -L + .05), (-W, H + .2, -L - .18), (W, H + .2, -L - .18), ST)
    box(m, (0, H + .3, -L - .45), (width, .06, .6), mat)                                             # canopy
    for k in range(4):
        box(m, (0, -.14, -L + .4 + k * (L - .8) / 3), (width - .1, .12, .12), DK)                    # floor ribs
    return m


def hauler():
    r = Rig('site_hauler')
    root = r.node('root')
    t = Mesh()
    box(t, (0, 1.0, -2.2), (1.3, .6, 2.6), DK, bevel=.05)
    box(t, (0, 1.65, -3.25), (2.3, 1.0, 1.4), Y, bevel=.16)                        # engine hood (front)
    for k in range(6):
        box(t, (0, 1.35 + k * .1, -3.96), (1.6, .03, .02), DK)
    box(t, (0, .95, -3.95), (2.5, .3, .25), DK, bevel=.05)                          # bumper
    for sx in (-1, 1):
        box(t, (sx * .95, 1.35, -3.96), (.25, .14, .03), LT)
        box(t, (sx * 1.08, 1.75, -2.1), (.75, .06, 1.6), Y, bevel=.03)
    cab_box(t, (0, 2.15, -1.75), 1.9, 1.5, 1.5)
    beacon(t, (.6, 3.75, -1.2))
    cyl(t, (.9, 2.1, -2.6), (.9, 2.9, -2.6), .08, DK, seg=10)
    for sx in (-1, 1):
        box(t, (sx * 1.0, 3.0, -2.6), (.04, .3, .2), DK)                             # mirrors
    r.node('tractor', root, mesh=t)
    for i, sx in enumerate((-1, 1)):
        w = Mesh(); tyre(w, (0, 0, 0), .78, .62, RB, Y, rim_dish=.08 * sx)
        r.node(f'fwheels[{i}]', root, (sx * 1.1, .78, -2.1), mesh=w)
    trailer = r.node('trailer', root)
    tr = Mesh()
    box(tr, (0, .95, 2.6), (1.15, .5, 4.6), DK, bevel=.05)
    cyl(tr, (0, 1.0, .1), (0, 1.0, .6), .25, DK, seg=12)                           # articulation joint
    for sx in (-1, 1):
        box(tr, (sx * .85, .8, 3.12), (.18, .3, 2.0), DK)                            # bogie beams
    r.node('trailerMesh', trailer, mesh=tr)
    for i, (sx, z) in enumerate(((-1, 2.3), (1, 2.3), (-1, 3.95), (1, 3.95))):
        w = Mesh(); tyre(w, (0, 0, 0), .78, .62, RB, Y, rim_dish=.08 * sx)
        r.node(f'rwheels[{i}]', trailer, (sx * 1.1, .78, z), mesh=w)
    body = r.node('body', trailer, (0, 1.6, 5.05))
    bm = Mesh(); truck_body(bm, 5.0, 2.95, 1.55, Y)
    r.node('bodyMesh', body, mesh=bm)
    ld = Mesh(); box(ld, (0, 0, 0), (2.7, 1.0, 4.4), DIRT, bevel=.35)
    r.node('load', body, (0, .45, -2.5), mesh=ld, visible=False)
    add_rams(r, root, 2)
    r.save()


def tipper():
    r = Rig('site_tipper')
    root = r.node('root')
    c = Mesh()
    for sx in (-1, 1):
        box(c, (sx * .45, .95, -2.2), (.12, .3, 7.6), DK)                            # frame rails
        box(c, (sx * 1.15, .9, -1.5), (.06, .35, 1.6), DK)                           # side guard
        box(c, (sx * .9, .85, -2.75), (.35, .5, .8), ST)                              # tanks
    cab = Mesh()
    box(cab, (0, 2.1, -5.55), (2.48, 2.3, 2.0), WH, bevel=.12)                      # cab
    box(cab, (0, 2.55, -6.56), (2.25, 1.0, .02), GL)                                 # windscreen
    for sx in (-1, 1):
        box(cab, (sx * 1.245, 2.5, -5.9), (.02, .8, .9), GL)
        box(cab, (sx * 1.5, 2.62, -6.5), (.03, .34, .18), DK)                         # mirrors (out on their arms, small in the view)
        box(cab, (sx * 1.37, 2.62, -6.45), (.24, .03, .03), DK)
        box(cab, (sx * .85, 1.05, -6.6), (.35, .15, .05), LT)
    box(cab, (0, 1.4, -6.58), (1.7, .7, .04), DK)                                    # grille
    box(cab, (0, .85, -6.62), (2.5, .35, .2), DK, bevel=.04)                         # bumper
    box(cab, (0, 3.32, -6.4), (2.3, .12, .3), WH)                                    # sun visor
    for sx in (-1, 1):
        beacon(cab, (sx * .7, 3.27, -5.2))
    box(cab, (0, 1.2, -4.95), (2.48, .12, .9), BL)                                   # stripe
    c.add(cab)
    r.node('chassis', root, mesh=c)
    for i, z in enumerate((-4.9, -3.5)):
        st = r.node(f'steer[{i}]', root, (0, .52, z))
        for k, sx in enumerate((-1, 1)):
            w = Mesh(); tyre(w, (0, 0, 0), .52, .38, RB, ST, rim_r=.3, lugs=18, rim_dish=.06 * sx)
            r.node(f'swheels[{i * 2 + k}]', st, (sx * 1.05, 0, 0), mesh=w)
    for i, z in enumerate((-.68, .68)):
        for k, sx in enumerate((-1, 1)):
            w = Mesh()
            tyre(w, (-.2 * sx * -1 * 0, 0, 0), .52, .38, RB, ST, rim_r=.3, lugs=18, rim_dish=.06 * sx)
            tyre(w, (-.42 * sx, 0, 0), .52, .38, RB, ST, rim_r=.3, lugs=18, rim_dish=-.06 * sx)
            r.node(f'rwheels[{i * 2 + k}]', root, (sx * 1.0, .52, z), mesh=w)
    body = r.node('body', root, (0, 1.32, 1.6))
    bm = Mesh(); truck_body(bm, 5.5, 2.5, 1.35, WH)
    r.node('bodyMesh', body, mesh=bm)
    ld = Mesh(); box(ld, (0, 0, 0), (2.3, .9, 5.0), DIRT, bevel=.3)
    r.node('load', body, (0, .4, -2.8), mesh=ld, visible=False)
    add_rams(r, root, 1)
    r.save()


def crane():
    r = Rig('site_crane')
    root = r.node('root')
    c = Mesh()
    box(c, (0, 1.3, -.6), (2.5, .7, 9.8), Y, bevel=.1)                               # carrier deck
    box(c, (0, .85, -.6), (1.2, .4, 9.6), DK)
    for sx in (-1, 1):
        for z in (-2.6, -1.0, 1.9):
            box(c, (sx * 1.15, 1.05, z), (.3, .5, 1.5), DK)                           # wheel arches
    cab = Mesh()
    box(cab, (-.65, 2.05, -5.0), (1.1, 1.7, 1.8), Y, bevel=.1)                        # driver cab (left)
    box(cab, (-.65, 2.4, -5.91), (.95, .8, .02), GL)
    box(cab, (-1.21, 2.4, -5.0), (.02, .7, 1.2), GL)
    box(cab, (0, .95, -5.95), (2.5, .3, .25), DK, bevel=.05)
    for sx in (-1, 1):
        box(cab, (sx * .9, 1.15, -5.97), (.3, .14, .03), LT)
    c.add(cab)
    for z in (-4.0, 3.2):
        box(c, (0, 1.05, z), (2.4, .45, .55), DK)                                      # outrigger boxes
    r.node('carrier', root, mesh=c)
    axles = (-3.4, -2.0, 1.9)
    for i, z in enumerate(axles[:2]):
        st = r.node(f'steer[{i}]', root, (0, .62, z))
        for k, sx in enumerate((-1, 1)):
            w = Mesh(); tyre(w, (0, 0, 0), .62, .5, RB, Y, rim_r=.38, lugs=20, rim_dish=.06 * sx)
            r.node(f'swheels[{i * 2 + k}]', st, (sx * 1.05, 0, 0), mesh=w)
    for k, sx in enumerate((-1, 1)):
        w = Mesh(); tyre(w, (0, 0, 0), .62, .5, RB, Y, rim_r=.38, lugs=20, rim_dish=.06 * sx)
        r.node(f'rwheels[{k}]', root, (sx * 1.05, .62, axles[2]), mesh=w)
    for i, (sx, z) in enumerate(((-1, -4.0), (1, -4.0), (-1, 3.2), (1, 3.2))):
        o = Mesh()
        box(o, (sx * .2, 0, 0), (1.6, .35, .4), Y, bevel=.03)
        box(o, (sx * 1.05, -.45, 0), (.25, 1.0, .25), DK)
        cyl(o, (sx * 1.05, -.95, 0), (sx * 1.05, -1.0, 0), .3, DK, seg=12)
        r.node(f'out[{i}]', root, (sx * .9, 1.05, z), mesh=o)
    sup = r.node('super', root, (0, 1.65, .9))
    s = Mesh()
    cyl(s, (0, 0, 0), (0, .25, 0), 1.1, DK, seg=24)
    box(s, (0, .65, .9), (2.3, .8, 2.6), Y, bevel=.12)                                # turntable house
    box(s, (0, .75, 2.35), (2.4, 1.0, .7), DK, bevel=.1)                               # counterweight
    cyl(s, (-.5, 1.0, 1.5), (.5, 1.0, 1.5), .35, DK, seg=16)                          # winch
    cab_box(s, (-1.05, .25, -.4), .9, 1.5, 1.6, roof=GL)                              # roof window: the boom and hook overhead
    r.node('superMesh', sup, mesh=s)
    boom = r.node('boomBase', sup, (0, 1.0, 1.4))
    bm = Mesh()
    box(bm, (0, 0, -4.9), (.95, 1.1, 11.2), Y, bevel=.12)
    cyl(bm, (-.5, 0, 0), (.5, 0, 0), .2, DK, seg=12)
    r.node('boomBaseMesh', boom, mesh=bm)
    parent = boom
    tele_sizes = ((.8, .92), (.66, .76), (.52, .6))
    for i, (w, h) in enumerate(tele_sizes):
        tm = Mesh(); box(tm, (0, 0, -4.9), (w, h, 10.0), Y, bevel=.08)
        parent = r.node(f'tele[{i}]', parent, (0, 0, -.3 if i == 0 else -.2), mesh=tm)
    head = Mesh()
    box(head, (0, -.1, -10.0), (.6, .7, .6), DK, bevel=.05)
    cyl(head, (-.25, -.3, -10.2), (.25, -.3, -10.2), .3, DK, seg=14)
    r.node('head', parent, (0, 0, 0), mesh=head)
    r.node('tip', parent, (0, -.6, -10.2))
    hk = Mesh()
    box(hk, (0, .15, 0), (.4, .5, .25), Y, bevel=.06)
    cyl(hk, (0, -.1, 0), (0, -.4, 0), .06, ST, seg=8)
    box(hk, (0, -.48, .06), (.06, .16, .16), ST)
    r.node('hook', root, mesh=hk)
    w = Mesh(); cyl(w, (0, 0, 0), (0, 1, 0), .02, DK, seg=5, caps=False)
    r.node('wire', root, mesh=w)
    add_rams(r, root, 1)
    r.save()


def tower():
    r = Rig('site_tower')
    root = r.node('root')
    m = Mesh()
    box(m, (0, .3, 0), (6.0, .6, 6.0), 'MI_Site_Concrete')
    for k in range(10):
        lattice(m, (0, .6 + k * 3.0, 0), (0, .6 + (k + 1) * 3.0, 0), 1.6, 1.6, Y, panels=2, chord=.07, brace=.035)
    for k in range(30):
        box(m, (.7, .9 + k, .95), (.4, .03, .03), ST)                                 # ladder rungs
    r.node('base', root, mesh=m)
    sl = r.node('slew', root, (0, 30.6, 0))
    s = Mesh()
    cyl(s, (0, 0, 0), (0, .5, 0), 1.0, DK, seg=20)
    lattice(s, (0, .5, 0), (0, 6.5, 0), 1.4, 1.4, Y, panels=3, chord=.07, brace=.035)   # tower head
    lattice(s, (0, .9, -.6), (0, .9, -40.0), 1.3, 1.45, Y, panels=26, chord=.06, brace=.03)   # jib
    lattice(s, (0, .9, .6), (0, .9, 12.0), 1.6, 1.0, Y, panels=8, chord=.07, brace=.035)     # counter-jib
    for k in range(4):
        box(s, (0, .1 + k * .02, 10.0 + k * .02), (2.2, 2.2 - k * .3, .4), 'MI_Site_Concrete')  # ballast
    box(s, (0, 1.0, 7.0), (1.6, .9, 1.4), DK)                                          # hoist winch
    for z in (-14.0, -30.0):
        cyl(s, (0, 6.5, 0), (0, 1.6, z), .04, ST, seg=5)                                # tie bars
    cyl(s, (0, 6.5, 0), (0, 1.4, 11.5), .05, ST, seg=5)
    cab_box(s, (-1.4, -.3, -1.3), 1.3, 2.0, 1.6)
    box(s, (0, 1.75, -40.2), (.8, .2, .4), RD)
    r.node('slewMesh', sl, mesh=s)
    tr = Mesh()
    box(tr, (0, -.05, 0), (1.4, .25, 1.2), DK, bevel=.04)
    cyl(tr, (-.3, -.25, 0), (.3, -.25, 0), .18, ST, seg=12)
    r.node('trolley', sl, (0, .25, -10.0), mesh=tr)
    hk = Mesh()
    box(hk, (0, .2, 0), (.5, .55, .3), Y, bevel=.06)
    cyl(hk, (0, -.1, 0), (0, -.45, 0), .07, ST, seg=8)
    box(hk, (0, -.52, .07), (.07, .18, .18), ST)
    r.node('hook', root, mesh=hk)
    w = Mesh(); cyl(w, (0, 0, 0), (0, 1, 0), .02, DK, seg=5, caps=False)
    r.node('wire', root, mesh=w)
    r.save()


# ---------------------------------------------------------------------------------------------------------------------
def props():
    """Site furniture (SM_Site_<kind>) and crane loads (SM_Load_<kind>), exported together to site.fbx."""
    meshes = {}
    f = Mesh()                                   # Heras fence panel, 3.5 m, on two concrete feet
    for sx in (-1, 1):
        cyl(f, (sx * 1.74, .05, 0), (sx * 1.74, 2.0, 0), .025, 'MI_Site_Galv', seg=6)
        box(f, (sx * 1.75, .07, 0), (.25, .14, .62), 'MI_Site_Concrete', bevel=.02)
    for y in (.05, 1.98):
        cyl(f, (-1.74, y, 0), (1.74, y, 0), .022, 'MI_Site_Galv', seg=6)
    f.quad((-1.72, .06, 0), (1.72, .06, 0), (1.72, 1.97, 0), (-1.72, 1.97, 0), 'MI_Site_Mesh')
    f.quad((1.72, .06, .001), (-1.72, .06, .001), (-1.72, 1.97, .001), (1.72, 1.97, .001), 'MI_Site_Mesh')
    # Banner printed on the outside only (+z faces away from the site): image quad, plain back.
    f.quad((-1.65, .75, .0045), (1.65, .75, .0045), (1.65, 1.65, .0045), (-1.65, 1.65, .0045), 'MI_Site_Banner')
    f.quad((1.65, .75, .0035), (-1.65, .75, .0035), (-1.65, 1.65, .0035), (1.65, 1.65, .0035), DK)
    meshes['SM_Site_fence'] = f
    def container(color, wc=False):
        c = Mesh()
        box(c, (0, 1.35, 0), (6.06, 2.6, 2.44), color, bevel=.04)
        for k in range(14):
            box(c, (-2.6 + k * .4, 1.35, 1.225), (.12, 2.3, .03), color)
        for sx in (-1, 1):
            box(c, (sx * 3.03, 1.35, 0), (.06, 2.7, 2.5), DK)
        box(c, (-1.2, 1.6, 1.24), (1.4, .9, .02), GL)
        box(c, (1.6, 1.6, 1.24), (1.0, .9, .02), GL)
        box(c, (.3, 1.05, 1.24), (.9, 2.0, .03), 'MI_Site_Door')
        box(c, (.3, .15, 1.7), (1.3, .3, .9), 'MI_Site_Concrete')                     # steps
        for sx in (-1, 1):
            for sz in (-1, 1):
                box(c, (sx * 2.7, .02, sz * 1.0), (.4, .04, .4), 'MI_Site_Concrete')
        if wc:
            box(c, (-2.0, 1.9, 1.24), (.5, .4, .02), GL)
        return c
    meshes['SM_Site_office'] = container('MI_Site_Container')
    meshes['SM_Site_office_wc'] = container('MI_Site_Container2', wc=True)
    t = Mesh(); box(t, (0, 1.15, 0), (1.15, 2.3, 1.2), 'MI_Site_Toilet', bevel=.06)
    box(t, (0, 1.05, .61), (.8, 1.9, .03), 'MI_Site_Toilet2')
    meshes['SM_Site_toilet'] = t
    s = Mesh(); cyl(s, (0, 0, 0), (0, 2.3, 0), .04, 'MI_Site_Galv', seg=8)
    box(s, (0, 1.8, .05), (1.0, .7, .02), 'MI_Site_Galv')                         # plate, the print faces out (+z)
    s.quad((-.5, 1.45, .0605), (.5, 1.45, .0605), (.5, 2.15, .0605), (-.5, 2.15, .0605), 'MI_Site_Sign')
    box(s, (0, .1, 0), (.6, .2, .6), 'MI_Site_Concrete')
    meshes['SM_Site_sign'] = s
    p = Mesh()
    for k in range(2):
        box(p, (0, .07 + k * 1.15, 0), (1.2, .14, 1.0), 'MI_Site_Wood')
        box(p, (0, .64 + k * 1.15, 0), (1.15, 1.0, .95), 'MI_Site_Brick')
    meshes['SM_Site_pallets'] = p
    pp = Mesh()
    for k, (x, y) in enumerate(((-.62, .6), (.62, .6), (0, 1.66))):
        lathe_x(pp, [(-1.2, .6), (1.2, .6), (1.2, .48), (-1.2, .48)], 'MI_Site_Concrete', seg=20, centre=(0, y, x))
    meshes['SM_Site_pipes'] = pp
    sk = Mesh(); prism(sk, [(-1.9, 0), (1.9, 0), (2.2, 1.5), (-2.2, 1.5)], -.9, .9, 'MI_Site_Skip')
    meshes['SM_Site_skip'] = sk
    lt = Mesh()
    box(lt, (0, .5, 0), (1.3, 1.0, 2.2), Y, bevel=.06)
    cyl(lt, (0, 1.0, 0), (0, 7.5, 0), .07, 'MI_Site_Galv', seg=8)
    for sx in (-1, 1):
        box(lt, (sx * .35, 7.6, 0), (.5, .35, .2), DK)
        box(lt, (sx * .35, 7.6, -.11), (.42, .28, .02), LT)
    meshes['SM_Site_light'] = lt
    # Crane loads (origin at the lifting point's foot, i.e. the load's bottom centre).
    bricks = Mesh(); box(bricks, (0, .07, 0), (1.2, .14, 1.0), 'MI_Site_Wood'); box(bricks, (0, .62, 0), (1.15, .95, .95), 'MI_Site_Brick')
    meshes['SM_Load_bricks'] = bricks
    beams = Mesh()
    for k in range(6):
        x = -.5 + (k % 3) * .5; y = .15 + (k // 3) * .32
        box(beams, (x, y, 0), (.3, .3, 6.0), RD)
    box(beams, (0, .02, -2.4), (1.6, .04, .1), 'MI_Site_Wood'); box(beams, (0, .02, 2.4), (1.6, .04, .1), 'MI_Site_Wood')
    meshes['SM_Load_beams'] = beams
    sk2 = Mesh(); cyl(sk2, (0, .2, 0), (0, 1.5, 0), .55, 'MI_Site_Skip', seg=16, r2=.85)
    cyl(sk2, (0, 0, 0), (0, .2, 0), .3, DK, seg=10)
    meshes['SM_Load_skip'] = sk2
    # The gravel aprons through the gates, from the park path outside to inside (world coordinates, ground and soil
    # heights from site.json).
    site = json.load(open(os.path.join(ROOT, 'unreal', 'Haarklous', 'Content', 'Data', 'site.json')))
    tr = Mesh()
    for line in site.get('tracks', [site['track']] if 'track' in site else []):
        P = [np.array(p, float) for p in line]
        def side(i):   # the track's half-width vector at point i (across the mean direction there, so the strip joins up)
            a, b = P[max(0, i - 1)], P[min(len(P) - 1, i + 1)]
            d = np.array([b[0] - a[0], b[2] - a[2]]); d /= max(1e-6, np.linalg.norm(d))
            return np.array([-d[1], 0, d[0]]) * 2.25
        for i in range(len(P) - 1):
            a, b, na, nb = P[i], P[i + 1], side(i), side(i + 1)
            la, ra, lb, rb = a + na, a - na, b + nb, b - nb
            if np.cross(ra - la, lb - la)[1] > 0:   # faces up
                tr.quad(la, ra, rb, lb, 'MI_Site_Gravel')
            else:
                tr.quad(la, lb, rb, ra, 'MI_Site_Gravel')
    if len(tr.P):
        meshes['SM_Site_track'] = tr
    bpy.ops.wm.read_factory_settings(use_empty=True)
    for k, m in meshes.items():
        build_object(k, m, 0)
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.export_scene.fbx(filepath=os.path.join(OUT, 'site.fbx'), use_selection=True, apply_unit_scale=True,
                             apply_scale_options='FBX_SCALE_NONE', object_types={'MESH'}, use_mesh_modifiers=True,
                             mesh_smooth_type='FACE', colors_type='LINEAR', add_leaf_bones=False, axis_forward='-Z', axis_up='Y')
    print(f'HK: site props: {len(meshes)} meshes')


if __name__ == '__main__':      # tools/blender/build_paintball.py imports the helpers above
    WANT = [a for a in (sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else [])]
    for fn in (dozer, excavator, loader, hauler, tipper, crane, tower, props):
        if not WANT or fn.__name__ in WANT:
            fn()
