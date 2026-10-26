/* Walkable stairwell: street door on the half landing, dogleg stair to the 4th-floor landing.
 * Tower coordinates (metres): x along the facade (apartment entrance wall W05 at x 1.405-1.665),
 * z from the back wall (5.14) to the facade (10.32) and the stair-window bay (11.12).
 * Storeys are 2.85 m; the 4th-floor landing is y = 0 and the street is y = -9.975 (half landing 0.5).
 * Each landing has one flat door on the left (x = -1.2) and one on the right. In our own building the right
 * door on the 4th floor is the apartment's real entrance; in other buildings every flat door is locked.
 * A typical 1930s Oslo stair (not surveyed). The same tower is reused for other enterable buildings.
 * Stairwell.create({T, lib, own=true, id='own', wallTop=3.35})
 *   -> {group, floors(), obstacles(), doors, inspectables, lampAt(position, feetY), ground, levelY, landingStart}
 */
(function (root, factory) {
  root.Stairwell = factory();
})(typeof window === 'undefined' ? globalThis : window, function () {
  'use strict';
  const STOREY = 2.85, HALF = STOREY / 2, TOP = 4;
  const X0 = -1.2, X1 = 1.405, XA = 1.665, XL = -1.45, ZB = 5.14, ZE = 4.9, ZL = 7.2, ZH = 9.09, ZF = 10.32, ZW = 11.0, ZO = 11.12;
  const WELL0 = 0.0, WELL1 = .205, RISERS = 8, RUN = (ZH - ZL), GRADE = HALF / RUN;
  const DOOR = {z0: 5.399, z1: 6.249};
  const STREET = {x0: .3, x1: 1.2};
  const levelY = n => (n - TOP) * STOREY;
  const GROUND = levelY(1) - HALF;
  const rect = (x0, z0, x1, z1) => [{x: x0, z: z0}, {x: x1, z: z0}, {x: x1, z: z1}, {x: x0, z: z1}];

  function floors(own = true) {
    const list = [];
    for (let n = 1; n <= TOP; n++) list.push({name: `Landing ${n}`, points: rect(X0, ZB, own && n === TOP ? XA : X1, ZL), y: levelY(n), ceilingHeight: 2.6});
    for (let n = 0; n < TOP; n++) {
      list.push({name: `Half landing ${n}.5`, points: rect(X0, ZH, X1, ZW), y: levelY(n) + HALF, ceilingHeight: 2.6});
      // Second flight: half landing up to the next storey, rising toward the building (−z).
      list.push({name: `Flight ${n}.5-${n + 1}`, points: rect(X0, ZL, WELL0, ZH), y: levelY(n) + HALF, slope: {x: 0, z: -GRADE, origin: {x: 0, z: ZH}}, ceilingHeight: 2.35});
      // First flight: storey up to the next half landing, rising toward the facade (+z).
      if (n >= 1) list.push({name: `Flight ${n}-${n}.5`, points: rect(WELL1, ZL, X1, ZH), y: levelY(n), slope: {x: 0, z: GRADE, origin: {x: 0, z: ZL}}, ceilingHeight: 2.35});
    }
    list.push({name: 'Street door threshold', points: rect(STREET.x0, ZW, STREET.x1, ZO + .02), y: GROUND, ceilingHeight: 2.1});
    return list;
  }
  // Walls as solid volumes (the outdoor terrain floor continues underneath the tower), plus the solid
  // ground under landing 1 and the first flights so nobody walks under the stairs.
  function obstacles(own = true, wallTop = 3.35) {
    const o = (x0, z0, x1, z1, minY, maxY) => ({points: rect(x0, z0, x1, z1), minY, maxY});
    const bottom = GROUND - 1, list = [
      o(XL, ZE, X0, ZO, bottom, wallTop),
      o(XL, ZE, XA, ZB, bottom, own ? -.16 : wallTop), o(XL, ZE, 0, ZB, -.16, wallTop),
      o(XL, ZW, STREET.x0, ZO, bottom, wallTop), o(STREET.x1, ZW, XA, ZO, bottom, wallTop), o(STREET.x0, ZW, STREET.x1, ZO, GROUND + 2.1, wallTop),
      o(X0, ZE, X1, ZL, bottom, GROUND + 1.2), o(WELL1, ZL, X1, ZH, bottom, GROUND + 1.3), o(X0, ZL, WELL1, ZH - .9, bottom, GROUND + .4),
    ];
    if (own) list.push(o(X1, ZE, XA, ZO, bottom, -.16), o(X1, 10.0, XA, ZO, -.16, wallTop), o(X1, ZE, XA, ZO, 2.62, wallTop), o(0, ZE, XA, ZB, 2.62, wallTop));
    else list.push(o(X1, ZE, XA, ZO, bottom, wallTop));
    return list;
  }

  function create({T, lib, own = true, id = 'own', wallTop = 3.35}) {
    const group = new T.Group();group.name = own ? 'Stairwell (Haarklous plass 17A)' : `Stairwell ${id}`;
    const inspectables = [], doors = [];
    const wallPaint = lib.plaster(0xe7e0cf), dado = lib.paint(0x52685a, .35), trim = lib.paint(0xefece4, .4), stone = lib.terrazzo(), brick = lib.brick();
    const veneer = lib.wood(0x7a4a2a, .45), brass = lib.metal(0xc8a24a, .3), steel = lib.metal(0x2d3033, .5), handrailWood = lib.wood(0x5a3a22, .4), glass = lib.glass();
    const panelWood = lib.wood(0x6d4126, .45), matFabric = lib.fabric(0x3c3631), mailMetal = lib.metal(0x6f7d74, .45);
    const mesh = (geometry, material, x, y, z, parent = group) => {const m = new T.Mesh(geometry, material);m.position.set(x, y, z);m.castShadow = m.receiveShadow = true;parent.add(m);return m;};
    const box = (w, h, d, x, y, z, material, parent = group) => {if (w <= 0 || h <= 0 || d <= 0) return null;const m = mesh(new T.BoxGeometry(w, h, d), material, x, y, z, parent);lib.boxUV(m);return m;};
    const span = (x0, x1, y0, y1, z0, z1, material, parent) => box(x1 - x0, y1 - y0, z1 - z0, (x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2, material, parent);
    // Stone on top and sides, painted plaster underneath (BoxGeometry face order +x,-x,+y,-y,+z,-z).
    const stoneSlab = [stone, stone, stone, wallPaint, stone, stone];
    const wallMats = outsideFace => {const m = [wallPaint, wallPaint, wallPaint, wallPaint, wallPaint, wallPaint];if (outsideFace !== undefined) m[outsideFace] = brick;return m;};
    const bottom = GROUND - .6, top = STOREY - .25;

    // ---- Walls (inner face plaster, outer faces brick on the street side), up to the eaves.
    // Side walls stop at the facade boxes (z = ZW): no wall face may coincide with another (z-fighting flicker).
    span(XL, X0, bottom, wallTop, ZE, ZW, wallMats(1));
    if (own) {span(X1, XA, bottom, -.16, ZE, ZW, wallMats(0));span(X1, XA, 2.62, wallTop, ZE, ZW, wallMats(0));span(X1, XA, -.16, 2.62, 10.0, ZW, wallMats(0));}
    else span(X1, XA, bottom, wallTop, ZE, ZW, wallMats(0));
    span(XL, 0, bottom, wallTop, ZE, ZB, wallMats());
    if (own) {span(0, XA, bottom, -.16, ZE, ZB, wallMats());span(0, XA, 2.62, wallTop, ZE, ZB, wallMats());}
    else span(0, XA, bottom, wallTop, ZE, ZB, wallMats());
    // Facade of the stair bay: one tall window per half landing, the street door at the bottom.
    const facade = (x0, x1, y0, y1) => span(x0, x1, y0, y1, ZW, ZO, wallMats(4));
    const winX0 = -.85, winX1 = 1.05;
    let y = bottom;
    for (let n = 0; n < TOP; n++) {
      const half = levelY(n) + HALF, sill = n === 0 ? half + 2.3 : half + .95, head = n === 0 ? half + 2.9 : half + 2.35;
      if (n === 0) {
        facade(XL, STREET.x0, y, sill);facade(STREET.x1, XA, y, sill);facade(STREET.x0, STREET.x1, half + 2.1, half + 2.3);facade(STREET.x0, STREET.x1, y, half - .2);
        y = sill;
      }
      facade(XL, XA, y, sill);facade(XL, winX0, sill, head);facade(winX1, XA, sill, head);
      span(winX0, winX1, sill - .04, sill, ZW - .08, ZO + .03, trim);span(winX0, winX1, head - .06, head, ZW + .02, ZW + .08, trim);
      for (const x of [winX0, (winX0 + winX1) / 2 - .03, winX1 - .06]) span(x, x + .06, sill, head, ZW + .02, ZW + .08, trim);
      span(winX0, winX1, (sill + head) / 2 - .025, (sill + head) / 2 + .025, ZW + .02, ZW + .08, trim);
      const pane = span(winX0 + .06, winX1 - .06, sill, head - .06, ZW + .045, ZW + .055, glass);if (pane) pane.castShadow = false;
      y = head;
    }
    facade(XL, XA, y, wallTop);
    // Ceiling over the top landing (with a loft hatch) and the bay's own roof slab at the eaves.
    span(XL, XA, top, top + .25, ZE, ZF, wallMats());
    span(XL - .1, XA + .1, wallTop, wallTop + .12, ZF - .1, ZO + .25, lib.roof());
    span(-.6, .3, top - .02, top, 5.6, 6.6, trim);

    // ---- Landings, half landings, flights.
    const slab = (x0, x1, z0, z1, yTop) => span(x0, x1, yTop - .22, yTop, z0, z1, stoneSlab);
    for (let n = 1; n <= TOP; n++) slab(X0, own && n === TOP ? XA : X1, ZB, ZL, levelY(n));
    for (let n = 0; n < TOP; n++) slab(X0, X1, ZH, ZW, levelY(n) + HALF);
    span(STREET.x0, STREET.x1, GROUND - .2, GROUND, ZW, ZO + .02, stone);
    const tread = RUN / (RISERS - 1), rise = HALF / RISERS;
    function flight(x0, x1, baseY, upward) {
      for (let k = 0; k < RISERS - 1; k++) {
        const za = upward ? ZL + k * tread : ZH - (k + 1) * tread, zb = za + tread, topY = baseY + (k + 1) * rise;
        span(x0, x1, topY - rise - .06, topY, za, zb, stoneSlab);
        span(x0, x1, topY - .012, topY, upward ? za - .012 : zb, upward ? za : zb + .012, stone);
      }
      const length = Math.hypot(RUN, HALF), soffit = new T.Mesh(new T.BoxGeometry(x1 - x0, .14, length), wallPaint);
      soffit.position.set((x0 + x1) / 2, baseY + HALF / 2 - .2, (ZL + ZH) / 2);soffit.rotation.x = upward ? -Math.atan2(HALF, RUN) : Math.atan2(HALF, RUN);soffit.castShadow = soffit.receiveShadow = true;group.add(soffit);
    }
    for (let n = 0; n < TOP; n++) {flight(X0, WELL0, levelY(n) + HALF, false);if (n >= 1) flight(WELL1, X1, levelY(n), true);}
    function rail(xa, za, ya, xb, zb, yb) {
      const len = Math.hypot(xb - xa, zb - za), count = Math.max(2, Math.round(len / .12));
      for (let i = 0; i <= count; i++) {const t = i / count;box(.016, .9, .016, xa + (xb - xa) * t, ya + (yb - ya) * t + .45, za + (zb - za) * t, steel);}
      const bar = new T.Mesh(new T.CylinderGeometry(.028, .028, Math.hypot(len, yb - ya), 10), handrailWood);
      bar.position.set((xa + xb) / 2, (ya + yb) / 2 + .92, (za + zb) / 2);
      bar.quaternion.setFromUnitVectors(new T.Vector3(0, 1, 0), new T.Vector3(xb - xa, yb - ya, zb - za).normalize());bar.castShadow = true;group.add(bar);
    }
    for (let n = 0; n < TOP; n++) {const half = levelY(n) + HALF;rail(WELL0 + .05, ZH, half, WELL0 + .05, ZL, half + HALF);if (n >= 1) rail(WELL1 - .03, ZL, levelY(n), WELL1 - .03, ZH, half);}
    rail(WELL1 - .03, ZL - .02, 0, X1, ZL - .02, 0);
    rail(WELL1 - .03, ZH + .02, GROUND, X1, ZH + .02, GROUND);

    // ---- Flat doors (locked; inspectable for the "locked" message).
    const names = ['Hansen', 'Berg', 'Nilsen', 'Larsen', 'Dahl', 'Haugen', 'Lie', 'Bakke', 'Moen'];
    let nameIndex = id.length;
    function flatDoor(n, side) {
      const yb = levelY(n), x = side < 0 ? X0 : X1, face = side < 0 ? 1 : -1, g = new T.Group();g.position.set(x + face * .005, yb, (DOOR.z0 + DOOR.z1) / 2);group.add(g);
      const w = DOOR.z1 - DOOR.z0, frame = .06;
      for (const dz of [-w / 2 - frame / 2, w / 2 + frame / 2]) box(.04, 2.1, frame, face * .02, 1.05, dz, trim, g);
      box(.04, frame, w + frame * 2, face * .02, 2.1 + frame / 2, 0, trim, g);
      for (let p = 0; p < 3; p++) box(.004, .5, w - .2, face * .054, .45 + p * .58, 0, panelWood, g);
      const handle = new T.Mesh(new T.CylinderGeometry(.011, .011, .13, 10), brass);handle.rotation.x = Math.PI / 2;handle.position.set(face * .08, 1.02, -w / 2 + .09);g.add(handle);
      box(.012, .14, .045, face * .056, 1.0, -w / 2 + .09, brass, g);box(.006, .045, .12, face * .056, 1.48, 0, brass, g);
      // The leaf stays separate (not merged) so aiming at it can report "locked".
      const leaf = new T.Mesh(new T.BoxGeometry(.045, 2.04, w - .01), veneer);leaf.position.set(face * .03, 1.02, 0);leaf.castShadow = leaf.receiveShadow = true;lib.boxUV(leaf);g.add(leaf);
      leaf.userData = {lockedDoor: true, partName: `Flat door, floor ${n} · ${names[nameIndex++ % names.length]}`, keep: true};inspectables.push(leaf);
      box(.5, .012, .75, side < 0 ? X0 + .3 : X1 - .3, yb + .006, (DOOR.z0 + DOOR.z1) / 2, matFabric);
    }
    for (let n = 1; n <= TOP; n++) {flatDoor(n, -1);if (n < TOP || !own) flatDoor(n, 1);}

    // Street door: glazed timber door, hinged at x .3, opening into the stair hall.
    const streetHinge = new T.Group();streetHinge.position.set(STREET.x0 + .04, GROUND, ZW + .02);streetHinge.userData.noMerge = true;group.add(streetHinge);
    const sw = STREET.x1 - STREET.x0 - .08, streetLeaf = box(sw, 2.05, .055, sw / 2, 1.04, 0, lib.wood(0x6b3f22, .4), streetHinge);
    const streetGlass = box(sw - .24, 1.2, .012, sw / 2, 1.3, .0, glass, streetHinge);streetGlass.castShadow = false;
    box(.03, .22, .03, sw - .09, 1.05, .05, brass, streetHinge);box(.03, .22, .03, sw - .09, 1.05, -.05, brass, streetHinge);
    const doorId = `${id}-street-door`;streetLeaf.userData.doorId = streetGlass.userData.doorId = doorId;inspectables.push(streetLeaf, streetGlass);
    doors.push({id: doorId, name: 'Street door', hinge: streetHinge, leaf: streetLeaf, open: false, openAngle: Math.PI / 2 * .95, sound: 'street'});
    span(STREET.x0 - .35, STREET.x1 + .35, GROUND + 2.55, GROUND + 2.62, ZO, ZO + .7, lib.concrete(0x9a9890));
    if (own) box(.22, .16, .02, (STREET.x0 + STREET.x1) / 2, GROUND + 2.75, ZO + .012, lib.paint(0x1f3b5a, .5));
    for (let i = 0; i < 4; i++) for (let j = 0; j < 2; j++) {box(.06, .3, .34, X0 + .03, GROUND + 1.1 + j * .32, 9.35 + i * .36, mailMetal);box(.004, .03, .2, X0 + .062, GROUND + 1.22 + j * .32, 9.35 + i * .36, steel);}
    box(.02, .7, .9, X0 + .01, GROUND + 1.4, 9.1, lib.paint(0xc9b48a, .8));
    for (let n = 1; n <= TOP; n++) {
      const yb = levelY(n);
      span(X0, X0 + .006, yb, yb + 1.1, ZB, ZL, dado);span(XL, own && n === TOP ? 0 : X1, yb, yb + 1.1, ZB, ZB + .006, dado);
      if (n < TOP || !own) span(X1 - .006, X1, yb, yb + 1.1, ZB, ZL, dado);
    }
    for (let n = 0; n < TOP; n++) {const yh = levelY(n) + HALF;span(X0, X0 + .006, yh, yh + 1.1, ZH, ZW, dado);span(X1 - .006, X1, yh, yh + 1.1, ZH, ZW, dado);}
    const globe = new T.MeshStandardMaterial({color: 0xfff6e6, emissive: 0xfff1d6, emissiveIntensity: 1.4, roughness: .3});
    for (let n = 1; n <= TOP; n++) mesh(new T.SphereGeometry(.11, 14, 10), globe, .1, levelY(n) + 2.45, 6.2);
    for (let n = 0; n < TOP; n++) mesh(new T.SphereGeometry(.11, 14, 10), globe, .1, levelY(n) + HALF + 2.45, 10.0);
    mesh(new T.SphereGeometry(.09, 12, 10), globe, (STREET.x0 + STREET.x1) / 2 + .7, GROUND + 2.35, ZO + .12);

    // Bake static parts into a few meshes; doors and flat-door leaves stay separate.
    lib.mergeStatic(group, o => o.userData.keep || o.userData.doorId);

    // Interior light position for a walker at (x, z) with feet height (tower coordinates), or null outside.
    function lampAt(position, feetY) {
      const inside = position.x > XL && position.x < XA && position.z > ZE && position.z < ZO && feetY > GROUND - .5 && feetY < 2.7;
      if (!inside) return null;
      const level = Math.round((feetY - levelY(TOP)) / HALF) * HALF + levelY(TOP);
      return {x: .1, y: level + 2.3, z: position.z < 8.2 ? 6.2 : 10.0};
    }
    return {group, floors: () => floors(own), obstacles: () => obstacles(own, wallTop), doors, inspectables, lampAt, ground: GROUND, levelY,
      landingStart: {x: .25, z: 5.83, y: 0, yaw: -Math.PI / 2}, doorOutside: {x: (STREET.x0 + STREET.x1) / 2, z: ZO + 1.2}};
  }
  return {create, floors, obstacles, STOREY, GROUND: (1 - TOP) * STOREY - STOREY / 2, X0, X1, XA, ZB, ZL, ZH, ZW, ZO};
});
