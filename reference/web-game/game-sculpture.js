/* HODET N.N. (Marianne Heske, 2014): the 7 m bronze doll's head at the north end of Torshovdalen (OSM node 2965223021),
 * a giant copy of a doll's head the artist found at a Paris flea market. It stands straight on the grass, faces south down
 * the valley, and has a few stone steps (a small amphitheatre) on its east side. Unreal issue jonatsl/haarklous-ue5#3.
 * GameSculpture.create({T, lib, world, at: {x, s}}) -> {group, obstacles, floors, at}
 *   at: the place in the local map frame (x east, s south); obstacles / floors in world coordinates for walk-collision.js.
 * GameSculpture.headShape(px, py, pz) -> metres to push a point of the unit sphere outward (the face; Node-tested).
 */
(function (root, factory) {
  root.GameSculpture = factory();
})(typeof window === 'undefined' ? globalThis : window, function () {
  'use strict';
  const HEIGHT = 7.0;
  // Measured on the photos (7 m in all): the shoulder plate is 2.85 m high and 3.7 m across, reaching further forward
  // than back; a short neck; the head is 4 m high. Half-sizes and centre heights in metres.
  const HEAD = {rx: 1.62, ry: 2.0, rz: 1.75, y: HEIGHT - 2.0}, NECK = {r: 1.05, y0: 2.6, y1: 3.8}, SHOULDER = {rx: 1.85, ry: 2.85, rz: 1.9, back: .8, forward: .3};
  const g2 = (du, dv, su, sv) => Math.exp(-(du / su) * (du / su) - (dv / sv) * (dv / sv));
  // The doll's face on the front (+z) of the unit sphere: u = angle round from the front, v = height (-1 chin … 1 crown).
  // Lowered eyelids, a small nose, full cheeks, a small mouth and a round chin, as in the photos.
  function headShape(px, py, pz) {
    if (pz <= 0) return 0;
    const u = Math.atan2(px, pz), v = py, au = Math.abs(u);
    return .30 * g2(u, v + .30, .09, .10) + .10 * g2(u, v + .12, .06, .16)        // nose and its bridge
      - .16 * g2(au - .36, v + .05, .17, .10) + .10 * g2(au - .36, v + .09, .12, .05) // eye sockets, lowered lids
      + .05 * g2(au - .33, v - .08, .22, .05)                                      // brows
      + .22 * g2(au - .48, v + .42, .24, .22)                                      // cheeks
      + .09 * g2(u, v + .58, .13, .05) - .05 * g2(u, v + .60, .11, .015)          // lips, mouth line
      + .08 * g2(u, v + .76, .16, .07);                                            // chin
  }
  function create({T, lib, world, at}) {
    const group = new T.Group();group.name = 'HODET N.N.';
    const bronze = lib.metal(0x4e463d, .5), stone = lib.concrete(0x8f877c);
    // Facing: the face (+z of the model) points south (+s on the map) whatever the map's rotation is.
    const w0 = world.toWorld(at.x, at.s), w1 = world.toWorld(at.x, at.s + 1), yaw = Math.atan2(w1.x - w0.x, w1.z - w0.z);
    const toW = (a, b) => ({x: w0.x + a * Math.cos(yaw) + b * Math.sin(yaw), z: w0.z - a * Math.sin(yaw) + b * Math.cos(yaw)});// model (a, b) -> world
    // Ground: the lowest point under the shoulder plate, so the plate never floats on the slope.
    let base = Infinity;for (let k = 0; k < 16; k++) {const a = k / 16 * Math.PI * 2, p = toW(Math.cos(a) * SHOULDER.rx, SHOULDER.forward + Math.sin(a) * SHOULDER.rz);base = Math.min(base, world.heightWorld(p.x, p.z));}
    const statue = new T.Group();statue.position.set(w0.x, base - .15, w0.z);statue.rotation.y = yaw;group.add(statue);
    const add = (geo, mat, parent = statue) => {const m = new T.Mesh(geo, mat);m.castShadow = m.receiveShadow = true;parent.add(m);return m;};
    // Head: a deformed sphere (egg shaped: the crown is the widest part, the back of the skull a little rounder).
    const head = new T.SphereGeometry(1, 72, 54), P = head.attributes.position;
    for (let i = 0; i < P.count; i++) {
      const x = P.getX(i), y = P.getY(i), z = P.getZ(i), narrow = 1 - .08 * Math.min(1, Math.max(0, (.2 - y) / 1.2)), back = z < 0 ? 1 + .06 * -z : 1;
      const d = headShape(x, y, z);
      P.setXYZ(i, x * HEAD.rx * narrow + x * d, y * HEAD.ry + y * d * .4, z * HEAD.rz * narrow * back + z * d);
    }
    head.computeVertexNormals();add(head, bronze).position.y = HEAD.y + .15;
    add(new T.CylinderGeometry(NECK.r * .95, NECK.r, NECK.y1 - NECK.y0, 40, 1, true), bronze).position.y = (NECK.y0 + NECK.y1) / 2 + .15;
    // Shoulder plate: a bell with flat shoulders (a superellipse turned round the neck), oval, flatter at the back and set
    // a little forward under the chin; its open bottom is under the ground.
    const prof = [];for (let k = 0; k <= 24; k++) {const t = k / 24 * Math.PI / 2;prof.push(new T.Vector2(Math.pow(Math.cos(t), 2 / 3.2), Math.pow(Math.sin(t), 2 / 3.2)));}
    const sh = new T.LatheGeometry(prof, 64), S = sh.attributes.position;// bottom to top, so the faces look outward
    for (let i = 0; i < S.count; i++) {const z = S.getZ(i);S.setXYZ(i, S.getX(i) * SHOULDER.rx, S.getY(i) * SHOULDER.ry, SHOULDER.forward + z * SHOULDER.rz * (z < 0 ? SHOULDER.back : 1));}
    sh.computeVertexNormals();add(sh, bronze);
    // The small amphitheatre: four rows of stone steps on the slope east of the head, facing it. Each step is 0.34 m
    // high (one walking step), so you can climb them and stand on them.
    const floors = [], obstacles = [];
    for (let row = 0; row < 4; row++) {
      const r = 6.2 + row * .95, n = 7;
      for (let k = 0; k < n; k++) {
        const a = (k - (n - 1) / 2) * .13, c = toW(Math.cos(a) * r, Math.sin(a) * r), len = r * .13 - .06, dep = .62;// model +x is east
        const ground = world.heightWorld(c.x, c.z), top = ground + .34, blk = add(new T.BoxGeometry(dep, .54, len), stone, group);
        blk.position.set(c.x, top - .27, c.z);blk.rotation.y = yaw - a;
        const ca = Math.cos(yaw - a), sa = Math.sin(yaw - a), corner = (u, w) => ({x: c.x + u * ca + w * sa, z: c.z - u * sa + w * ca});
        floors.push({name: 'HODET N.N. steps', points: [corner(-dep / 2, -len / 2), corner(dep / 2, -len / 2), corner(dep / 2, len / 2), corner(-dep / 2, len / 2)], y: top, surfaceKind: 'stone'});
      }
    }
    // The bronze is solid: a 12-sided ring round the shoulder plate, up to the crown.
    const ring = [];for (let k = 0; k < 12; k++) {const a = k / 12 * Math.PI * 2, s = Math.sin(a);ring.push(toW(Math.cos(a) * (SHOULDER.rx + .05), SHOULDER.forward + s * (SHOULDER.rz + .05) * (s < 0 ? SHOULDER.back : 1)));}
    obstacles.push({points: ring, minY: base - .5, maxY: base + HEIGHT, kind: 'sculpture'});
    group.updateMatrixWorld(true);
    return {group, obstacles, floors, at, yaw, base};
  }
  return {create, headShape, HEIGHT};
});
