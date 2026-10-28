"""The urban paintball / reball course (new feature, 28 Oct 2026), built headless from Content/Data/paintball.json
(tools/make-paintball.py):

    blender -b --factory-startup -P tools/blender/build_paintball.py

Rig 'paintball' (Content/Data/rigs/paintball.json, tools/out/fbx/paintball.fbx): its root sits at the play area's
centre on the yard floor, turned by the map's theta (HKPaintball.cpp places it); children in course coordinates
(u along local x, v along local s, y up):
  floor   the paved yard (asphalt) with white boundary lines and the two base zones
  walls   concrete retaining walls along the yard's edge, from the ground outside to a parapet above the floor
  net     5 m posts and netting round the play area
  cover   containers, roofless building shells, walls with windows, jersey barriers, a car wreck, cable spools,
          sandbags, barrels, pallets, tyres and the two flag stands (point-symmetric for both teams)
Separate meshes (tools/out/fbx/paintball_parts.fbx): the lobby furniture placed on the lawn by the game
(SM_PB_Shelter, SM_PB_Bench, SM_PB_Rack, SM_PB_Board, SM_PB_Sign), the flags (SM_PB_Flag_Blue / _Red), the marker
(SM_PB_Marker, origin at the grip, barrel along -z), the goggles (SM_PB_Mask), team armbands (SM_PB_Band_Blue / _Red),
a paintball (SM_PB_Ball_Blue / _Red) and a paint splat (SM_PB_Splat_Blue / _Red).
Material slots MI_Site_* (tools/unreal/materials_site.py) and MI_PB_* (tools/unreal/materials_paintball.py).
"""
import json
import math
import os
import sys

import bpy
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from site_geometry import Mesh, box, cyl, prism, lathe_x, tyre, rot_x, rot_y, rot_z  # noqa: E402
from build_machines import Rig, build_object, OUT  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
PB = json.load(open(os.path.join(ROOT, 'unreal', 'Haarklous', 'Content', 'Data', 'paintball.json'), encoding='utf-8'))

ASPH, CONC, PLAS, BRICK, RUST, WOOD, RUB, GALV, NET, DARK = ('MI_PB_Asphalt', 'MI_Site_Concrete', 'MI_PB_Plaster',
                                                            'MI_Site_Brick', 'MI_Site_Steel', 'MI_Site_Wood',
                                                            'MI_Site_Rubber', 'MI_Site_Galv', 'MI_PB_Net', 'MI_Site_Dark')
LINE, BLUE, RED, SAND, BARREL, CONT_R, CONT_G, CONT_B = ('MI_PB_Line', 'MI_PB_Blue', 'MI_PB_Red', 'MI_PB_Sandbag',
                                                         'MI_PB_Barrel', 'MI_PB_ContainerRed', 'MI_PB_ContainerGreen',
                                                         'MI_Site_Container')


def placed(m, piece_mesh, u, v, yaw):
    m.add(piece_mesh.transformed((u, 0, v), rot_y(yaw)))


# ---------------------------------------------------------------------------------------------------------------------
# Cover kit (each piece in its own frame: x along w, z along d, y up from the floor)
def jersey(m, length=3.0, mat=CONC):
    prof = [(-.3, 0), (.3, 0), (.3, .08), (.13, .32), (.1, .85), (-.1, .85), (-.13, .32), (-.3, .08)]
    prism(m, prof, -length / 2, length / 2, mat)


def container(m, mat):
    W, D, H = 6.06, 2.44, 2.6
    box(m, (0, H / 2, 0), (W, H, D), mat)
    for k in range(18):                                     # corrugation ribs on the long sides
        x = -2.8 + k * .33
        for sz in (-1, 1):
            box(m, (x, H / 2, sz * (D / 2 + .015)), (.12, H - .2, .03), mat)
    for sx in (-1, 1):                                      # corner posts and door bars
        for sz in (-1, 1):
            box(m, (sx * (W / 2 - .06), H / 2, sz * (D / 2 - .06)), (.14, H, .14), DARK)
    for z in (-.6, -.2, .2, .6):
        cyl(m, (W / 2 + .03, .2, z), (W / 2 + .03, H - .2, z), .025, GALV, seg=6)


def shell(m):
    # Boxes like tools/make-paintball.py's collision, plaster outside, a brick plinth band.
    t, h = .25, 2.7
    parts = [(-2.0, -3.0, 3.0, t, 0, h), (2.8, -3.0, 1.4, t, 0, h), (1.55, -3.0, 1.1, t, 2.1, .6),
             (2.0, 3.0, 3.0, t, 0, h), (-2.8, 3.0, 1.4, t, 0, h), (-1.55, 3.0, 1.1, t, 2.1, .6)]
    for sx in (-3.5, 3.5):
        parts += [(sx, -1.75, t, 2.5, 0, h), (sx, 1.75, t, 2.5, 0, h), (sx, 0, t, 1.0, 0, .9), (sx, 0, t, 1.0, 1.7, 1.0)]
    for x, z, w, d, y0, hh in parts:
        box(m, (x, y0 + hh / 2, z), (w, hh, d), PLAS)
        if y0 == 0:
            box(m, (x, .2, z), (w + .04, .4, d + .04), BRICK)
    for x, z, w, d in ((0, -3.0, 7.25, t), (0, 3.0, 7.25, t), (-3.5, 0, t, 6.25), (3.5, 0, t, 6.25)):
        box(m, (x, h + .04, z), (w + .06, .08, d + .06), CONC)          # coping


def wall(m):
    for x, z, w, d, y0, h in ((-1.0, 0, 1.0, .25, 0, 2.2), (1.0, 0, 1.0, .25, 0, 2.2), (0, 0, 1.0, .25, 0, .9), (0, 0, 1.0, .25, 1.6, .6)):
        box(m, (x, y0 + h / 2, z), (w, h, d), CONC)
    box(m, (0, .05, 0), (3.3, .1, .6), CONC)                            # footing


def car(m):
    box(m, (0, .55, 0), (4.2, .65, 1.78), RUST, bevel=.08)
    box(m, (-.1, .95, 0), (4.0, .2, 1.74), RUST, bevel=.05)
    box(m, (.2, 1.2, 0), (2.1, .5, 1.55), RUST, bevel=.1)               # cabin (glass long gone)
    box(m, (.2, 1.2, 0), (1.9, .38, 1.6), DARK)
    for sx in (-1.35, 1.35):
        for sz in (-.8, .8):
            m.add(_tyre(.33, .2).transformed((sx, .26, sz), rot_y(math.pi / 2)))
    box(m, (-2.1, .5, 0), (.08, .25, 1.6), GALV)                         # bumper


def _tyre(r, w):
    t = Mesh()
    tyre(t, (0, 0, 0), r, w, RUB, DARK, rim_dish=.03)
    return t


def spool(m):
    for sx in (-.45, .45):
        cyl(m, (sx - .04, .8, 0), (sx + .04, .8, 0), .8, WOOD, seg=24)
    cyl(m, (-.42, .8, 0), (.42, .8, 0), .35, WOOD, seg=16)
    cyl(m, (-.4, .8, 0), (.4, .8, 0), .5, DARK, seg=16)                 # cable left on it


def sandbags(m):
    for row in range(4):
        n = 5 if row % 2 == 0 else 4
        for k in range(n):
            x = -1.2 + k * .6 + (0 if row % 2 == 0 else .3)
            box(m, (x, .12 + row * .24, 0), (.58, .22, .62 - row * .04), SAND, bevel=.08)


def barrels(m):
    for (x, z), mat in zip(((0, 0), (.7, .2), (.3, .75)), (BARREL, RUST, BARREL)):
        cyl(m, (x, 0, z), (x, .88, z), .29, mat, seg=16)
        for y in (.3, .6):
            cyl(m, (x, y - .015, z), (x, y + .015, z), .3, mat, seg=16)


def pallets(m):
    for k in range(9):
        y = .07 + k * .145
        box(m, (0, y + .05, 0), (1.2, .03, 1.0), WOOD)
        for z in (-.45, 0, .45):
            box(m, (0, y, z), (1.2, .1, .1), WOOD)


def tires(m):
    for (x, n) in ((0, 4), (.8, 3)):
        for k in range(n):
            m.add(_tyre(.36, .24).transformed((x, .13 + k * .25, 0), rot_z(math.pi / 2)))


KIT = {'barrier': lambda m: jersey(m), 'container': None, 'shell': shell, 'wall': wall, 'car': car, 'spool': spool,
       'sandbags': sandbags, 'barrels': barrels, 'pallets': pallets, 'tires': tires}


def flag_stand(m, u, v, mat):
    cyl(m, (u, 0, v), (u, .25, v), .45, CONC, seg=20)
    cyl(m, (u, .25, v), (u, .27, v), .46, mat, seg=20)


# ---------------------------------------------------------------------------------------------------------------------
def course():
    r = Rig('paintball')
    root = r.node('root')
    rect, play = PB['rect'], PB['play']
    cx, cs = PB['centre']['x'], PB['centre']['s']
    u0, u1 = rect['x0'] - cx, rect['x0'] + rect['w'] - cx
    v0, v1 = rect['s0'] - cs, rect['s0'] + rect['d'] - cs
    # Floor: asphalt, painted boundary of the play area and the base lines.
    f = Mesh()
    f.quad((u0, 0, v1), (u1, 0, v1), (u1, 0, v0), (u0, 0, v0), ASPH)
    pu, pv = play['w'] / 2, play['d'] / 2
    for (a, b) in (((-pu, -pv), (pu, -pv)), ((pu, -pv), (pu, pv)), ((pu, pv), (-pu, pv)), ((-pu, pv), (-pu, -pv))):
        L = math.hypot(b[0] - a[0], b[1] - a[1])
        box(f, ((a[0] + b[0]) / 2, .004, (a[1] + b[1]) / 2), (L + .12, .008, .12), LINE, rot=rot_y(-math.atan2(b[1] - a[1], b[0] - a[0])))
    for sgn, mat in ((-1, BLUE), (1, RED)):
        z = PB['bases'][0]['zone'] if sgn < 0 else PB['bases'][1]['zone']
        f.quad((z - .06, .006, pv), (z + .06, .006, pv), (z + .06, .006, -pv), (z - .06, .006, -pv), mat)
    r.node('floor', root, mesh=f)
    # Retaining walls along the yard edge (0.3 m thick, 1 m segments), a gap for the gate in the south wall.
    w = Mesh()
    g0, g1 = PB['gate'][0] - cx, PB['gate'][1] - cx
    fl = PB['floor']
    for name, pts in PB['edges'].items():
        for (ua, va, ha), (ub, vb, hb) in zip(pts[:-1], pts[1:]):
            um, vm, h = (ua + ub) / 2, (va + vb) / 2, (ha + hb) / 2 - fl
            if name == 'south' and g0 - .2 < um < g1 + .2:
                continue
            bottom = min(0.0, h) - .4
            top = max(0.0, h) + (1.05 if h < .8 else .12)
            horiz = name in ('north', 'south')
            size = (1.02, top - bottom, .3) if horiz else (.3, top - bottom, 1.02)
            box(w, (um, (top + bottom) / 2, vm), size, CONC)
            box(w, (um, top + .04, vm), (1.04 if horiz else .36, .08, .36 if horiz else 1.04), CONC)
    for gx in (g0 - .2, g1 + .2):
        box(w, (gx, .9, v1), (.4, 1.8, .4), CONC)
    r.node('walls', root, mesh=w)
    # Net: posts every 5 m, netting 5 m high round the play area.
    n = Mesh()
    corners = [(-pu, -pv), (pu, -pv), (pu, pv), (-pu, pv), (-pu, -pv)]
    for (a, b) in zip(corners[:-1], corners[1:]):
        L = math.hypot(b[0] - a[0], b[1] - a[1]); k = int(round(L / 5))
        for i in range(k):
            p = (a[0] + (b[0] - a[0]) * i / k, a[1] + (b[1] - a[1]) * i / k)
            cyl(n, (p[0], 0, p[1]), (p[0], 5.0, p[1]), .05, GALV, seg=8)
        n.quad((a[0], .05, a[1]), (b[0], .05, b[1]), (b[0], 5.0, b[1]), (a[0], 5.0, a[1]), NET)
        n.quad((b[0], .05, b[1]), (a[0], .05, a[1]), (a[0], 5.0, a[1]), (b[0], 5.0, b[1]), NET)
        cyl(n, (a[0], 5.0, a[1]), (b[0], 5.0, b[1]), .025, GALV, seg=6)
    r.node('net', root, mesh=n)
    # Cover.
    c = Mesh()
    colours = [CONT_R, CONT_G, CONT_B]
    nc = 0
    for p in PB['pieces']:
        pm = Mesh()
        if p['kind'] == 'container':
            container(pm, colours[nc % 3]); nc += 1
        else:
            KIT[p['kind']](pm)
        placed(c, pm, p['u'], p['v'], p['yaw'])
    for b, mat in zip(PB['bases'], (BLUE, RED)):
        flag_stand(c, b['flag'][0], b['flag'][1], mat)
    r.node('cover', root, mesh=c)
    r.save()


# ---------------------------------------------------------------------------------------------------------------------
def parts():
    meshes = {}
    # Lobby shelter: four posts, a sloped corrugated roof, a back wall with the course board.
    s = Mesh()
    for x in (-3, 3):
        for z in (-1.5, 1.5):
            cyl(s, (x, 0, z), (x, 2.6 if z < 0 else 2.3, z), .06, GALV, seg=8)
    roof = rot_x(math.atan2(.3, 3.2))
    box(s, (0, 2.5, 0), (6.6, .05, 3.4), 'MI_Site_Container2', rot=roof)
    box(s, (0, 1.25, 1.5), (6.0, 2.3, .05), 'MI_Site_Container')
    meshes['SM_PB_Shelter'] = s
    b = Mesh()
    for x in (-.8, .8):
        box(b, (x, .22, 0), (.06, .44, .4), GALV)
    box(b, (0, .45, 0), (2.0, .05, .4), WOOD)
    meshes['SM_PB_Bench'] = b
    rk = Mesh()                                          # marker rack with six markers
    box(rk, (0, .9, 0), (2.2, .06, .3), WOOD); box(rk, (0, .3, 0), (2.2, .06, .3), WOOD)
    for x in (-1.05, 1.05):
        box(rk, (x, .6, 0), (.06, 1.2, .3), WOOD)
    for k in range(6):
        rk.add(marker_mesh().transformed((-.85 + k * .34, .62, 0), rot_x(math.pi / 2)))
    meshes['SM_PB_Rack'] = rk
    bd = Mesh()                                          # score / rules board
    for x in (-1.3, 1.3):
        cyl(bd, (x, 0, 0), (x, 2.6, 0), .05, GALV, seg=8)
    box(bd, (0, 1.75, -.03), (2.7, 1.5, .04), DARK)                           # the board faces +z
    bd.quad((-1.3, 1.05, -.005), (1.3, 1.05, -.005), (1.3, 2.45, -.005), (-1.3, 2.45, -.005), 'MI_PB_Board')
    meshes['SM_PB_Board'] = bd
    sg = Mesh()
    cyl(sg, (0, 0, 0), (0, 2.1, 0), .04, GALV, seg=8)
    box(sg, (0, 1.65, .04), (.8, .6, .02), GALV)                               # plate (z .03 to .05), the print in front of it
    sg.quad((-.4, 1.35, .053), (.4, 1.35, .053), (.4, 1.95, .053), (-.4, 1.95, .053), 'MI_PB_Sign')
    meshes['SM_PB_Sign'] = sg
    # Flags (pole and cloth; the origin at the foot of the pole).
    for team, mat in (('Blue', BLUE), ('Red', RED)):
        fm = Mesh()
        cyl(fm, (0, 0, 0), (0, 2.3, 0), .025, GALV, seg=8)
        for k in range(6):                                  # a cloth with a slight wave
            x0, x1 = .03 + k * .14, .03 + (k + 1) * .14
            z0, z1 = .04 * math.sin(k * 1.1), .04 * math.sin((k + 1) * 1.1)
            fm.quad((x0, 1.6, z0), (x1, 1.6, z1), (x1, 2.25, z1), (x0, 2.25, z0), mat)
            fm.quad((x1, 1.6, z1), (x0, 1.6, z0), (x0, 2.25, z0), (x1, 2.25, z1), mat)
        meshes[f'SM_PB_Flag_{team}'] = fm
        bandm = Mesh()
        cyl(bandm, (-.035, 0, 0), (.035, 0, 0), .068, mat, seg=16, caps=False)
        cyl(bandm, (.035, 0, 0), (-.035, 0, 0), .066, mat, seg=16, caps=False)
        meshes[f'SM_PB_Band_{team}'] = bandm
        ball = Mesh()
        lathe_x(ball, [(.0087, 0), (.006, .0062), (0, .0087), (-.006, .0062), (-.0087, 0)], f'MI_PB_Ball_{team}', seg=10)   # outward faces
        meshes[f'SM_PB_Ball_{team}'] = ball
        sp = Mesh()
        sp.quad((-.09, .0, .09), (.09, .0, .09), (.09, .0, -.09), (-.09, .0, -.09), f'MI_PB_Splat_{team}')
        meshes[f'SM_PB_Splat_{team}'] = sp
    meshes['SM_PB_Marker'] = marker_mesh()
    meshes['SM_PB_Mask'] = mask_mesh()
    bpy.ops.wm.read_factory_settings(use_empty=True)
    for key, m in meshes.items():
        build_object(key, m, 0)
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.export_scene.fbx(filepath=os.path.join(OUT, 'paintball_parts.fbx'), use_selection=True, apply_unit_scale=True,
                             apply_scale_options='FBX_SCALE_NONE', object_types={'MESH'}, use_mesh_modifiers=True,
                             mesh_smooth_type='FACE', colors_type='LINEAR', add_leaf_bones=False, axis_forward='-Z', axis_up='Y')
    print(f'HK: paintball parts: {len(meshes)} meshes')


def marker_mesh():
    """A paintball marker: origin at the grip (the hand), barrel along -z, hopper on top, air tank as the stock."""
    m = Mesh()
    box(m, (0, .07, -.05), (.045, .1, .26), 'MI_PB_Marker', bevel=.012)          # body
    cyl(m, (0, .085, -.18), (0, .085, -.48), .016, 'MI_PB_Marker', seg=12)      # barrel
    cyl(m, (0, .085, -.48), (0, .085, -.5), .019, DARK, seg=12)
    box(m, (0, -.03, 0), (.035, .12, .05), DARK, rot=rot_x(-.25), bevel=.01)    # grip
    box(m, (0, -.02, -.1), (.03, .1, .045), DARK, rot=rot_x(-.15), bevel=.01)   # front grip (feed neck side)
    box(m, (0, .0, -.035), (.012, .04, .07), GALV)                              # trigger guard
    cyl(m, (0, .12, -.07), (0, .15, -.07), .018, DARK, seg=10)                  # feed neck
    hop = Mesh()
    lathe_x(hop, [(.1, 0), (.08, .05), (0, .065), (-.08, .05), (-.09, 0)], 'MI_PB_Hopper', seg=16)   # outward faces
    m.add(hop.transformed((0, .2, -.08), rot_y(math.pi / 2)))
    cyl(m, (0, .03, .1), (0, .03, .33), .04, 'MI_PB_Tank', seg=14)             # air tank (stock)
    cyl(m, (0, .03, .33), (0, .03, .35), .03, GALV, seg=10)
    return m


def mask_mesh():
    """Paintball goggles for an avatar's head (origin at the head node; the face toward -z)."""
    m = Mesh()
    # Face plate: a curved band round the front of the head, lens across the eyes, a strap round the back.
    seg = 9
    for k in range(seg):
        a0, a1 = -1.15 + 2.3 * k / seg, -1.15 + 2.3 * (k + 1) / seg
        p = lambda a, y, r: (r * math.sin(a), y, -r * math.cos(a))
        for (y0, r0, y1, r1, mat) in ((.02, .112, .12, .118, 'MI_PB_Marker'), (.12, .118, .19, .114, 'MI_PB_Lens'), (.19, .114, .215, .108, 'MI_PB_Marker')):
            m.quad(p(a1, y0, r0), p(a0, y0, r0), p(a0, y1, r1), p(a1, y1, r1), mat)      # outside
            m.quad(p(a0, y0, r0), p(a1, y0, r0), p(a1, y1, r1), p(a0, y1, r1), mat)      # inside
    for k in range(10):
        a0, a1 = 1.15 + (2 * math.pi - 2.3) * k / 10, 1.15 + (2 * math.pi - 2.3) * (k + 1) / 10
        p = lambda a, y: (.105 * math.sin(a), y, -.105 * math.cos(a))
        m.quad(p(a1, .15), p(a0, .15), p(a0, .18), p(a1, .18), DARK)
        m.quad(p(a0, .15), p(a1, .15), p(a1, .18), p(a0, .18), DARK)
    return m


course()
parts()
