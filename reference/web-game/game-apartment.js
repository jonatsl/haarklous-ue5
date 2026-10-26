/* The apartment (your kitchen + bedroom design) for the open-world game.
 * Built from apartment-data.js in model coordinates (metres, 4th-floor floor at y = 0). Static parts are merged
 * for speed; doors stay separate. Returns walkable floors, solid obstacles, doors, interactables (drinks) and lights.
 * GameApartment.build({T, lib, data}) -> {group, floors, obstacles, doors, drinks, lights, rooms, spawnAreas, xy}
 */
(function (root, factory) {
  root.GameApartment = factory();
})(typeof window === 'undefined' ? globalThis : window, function () {
  'use strict';
  function build({T, lib, data}) {
    const scale = data.calibration.imageUnitsPerMetre, origin = data.calibration.origin;
    const xy = p => [(p[0] - origin[0]) / scale, (p[1] - origin[1]) / scale];
    const heights = Object.fromEntries(data.rooms.map(r => [r.id, data.measuredCeilingHeights?.[r.id]?.value ?? data.ceilingHeight]));
    const group = new T.Group();group.name = 'Apartment';
    const solids = [], doors = [], drinks = [];
    const R = m => {m.polygonOffset = true;m.polygonOffsetFactor = 1;m.polygonOffsetUnits = 1;return m;};
    const roomPaint = {living: 0xeeebe4, hall: 0xb9785c, kitchen: 0xeeeae2, bedroom: 0xebe6dd, store: 0xe6e1d6};
    const facadeWalls = new Set(['W01', 'W08']);
    const inPoly = (p, ps) => {let r = false;for (let i = 0, j = ps.length - 1; i < ps.length; j = i++) {const a = ps[i], b = ps[j];if ((a[1] > p[1]) !== (b[1] > p[1]) && p[0] < (b[0] - a[0]) * (p[1] - a[1]) / (b[1] - a[1]) + a[0]) r = !r;}return r;};
    const roomAt = (x, z) => data.rooms.find(r => inPoly([x, z], r.polygon.map(xy)))?.id;
    function box(parent, w, h, d, x, y, z, mat, solid = true) {
      if (w <= 0 || h <= 0 || d <= 0) return null;
      const mesh = new T.Mesh(new T.BoxGeometry(w, h, d), typeof mat === 'function' ? mat(x, y, z) : mat);
      mesh.position.set(x, y, z);mesh.receiveShadow = true;mesh.castShadow = !(mesh.material.transparent);lib.boxUV(mesh);parent.add(mesh);
      if (solid) solids.push(mesh);return mesh;
    }
    function slab(parent, points, thickness, elevation, mat) {
      const shape = new T.Shape();points.forEach((p, i) => {const [x, z] = xy(p);i ? shape.lineTo(x, -z) : shape.moveTo(x, -z);});shape.closePath();
      const mesh = new T.Mesh(new T.ExtrudeGeometry(shape, {depth: thickness, bevelEnabled: false}), mat);
      mesh.rotation.x = -Math.PI / 2;mesh.position.y = elevation;mesh.receiveShadow = true;mesh.castShadow = true;parent.add(mesh);return mesh;
    }
    function wallSections(w) {
      const length = Math.hypot(w.b[0] - w.a[0], w.b[1] - w.a[1]);let start = 0;
      return data.wallRooms[w.id].map(run => {const end = run.end ?? length;const s = {start: start / scale, end: end / scale, height: Math.max(...run.rooms.map(id => heights[id]))};start = end;return s;});
    }
    // ---- Shading helpers (ambient-occlusion stand-ins).
    const gradTex = (() => {const c = document.createElement('canvas');c.width = 4;c.height = 128;const g = c.getContext('2d'), gr = g.createLinearGradient(0, 0, 0, 128);gr.addColorStop(0, 'rgba(0,0,0,1)');gr.addColorStop(.35, 'rgba(0,0,0,.45)');gr.addColorStop(1, 'rgba(0,0,0,0)');g.fillStyle = gr;g.fillRect(0, 0, 4, 128);return new T.CanvasTexture(c);})();
    const blobTex = (() => {const c = document.createElement('canvas');c.width = c.height = 128;const g = c.getContext('2d');g.filter = 'blur(14px)';g.fillStyle = '#000';g.fillRect(26, 26, 76, 76);return new T.CanvasTexture(c);})();
    const shade = (opacity, map) => new T.MeshBasicMaterial({map, color: 0x000000, transparent: true, opacity, depthWrite: false, polygonOffset: true, polygonOffsetFactor: -2, polygonOffsetUnits: -2});
    const aoWall = shade(.2, gradTex), aoFloor = shade(.24, gradTex), aoBlob = shade(.42, blobTex), skirting = R(lib.paint(0xf4f3ee, .4));
    const sideRoom = (g, x, z) => {const t = g.rotation.y;return roomAt(g.position.x + x * Math.cos(t) + z * Math.sin(t), g.position.z - x * Math.sin(t) + z * Math.cos(t));};
    const wallMats = new Map();
    function faceMaterials(w, g, x, z, depth) {
      const pick = room => {const key = room || (facadeWalls.has(w.id) ? 'brick' : 'party');if (!wallMats.has(key)) wallMats.set(key, R(key === 'bath' ? lib.wallTiles() : key === 'brick' ? lib.brick() : lib.plaster(roomPaint[key] ?? 0xe8e4dc)));return wallMats.get(key);};
      const ra = sideRoom(g, x, z + depth / 2 + .07), rb = sideRoom(g, x, z - depth / 2 - .07), a = pick(ra), b = pick(rb), inner = ra || !rb ? a : b;
      return [inner, inner, inner, inner, a, b];
    }
    function trims(w, g, left, right, base, cap, offset) {
      const len = right - left, mid = (left + right) / 2;
      for (const sign of [1, -1]) {
        const face = offset + sign * w.thickness / 2, room = sideRoom(g, mid, face + sign * .07);if (!room) continue;
        const flip = sign < 0 ? Math.PI : 0;
        if (base < .001) {
          if (room !== 'bath') {const sk = new T.Mesh(new T.BoxGeometry(len, .07, .014), skirting);sk.position.set(mid, .035, face + sign * .007);sk.receiveShadow = true;g.add(sk);}
          const wa = new T.Mesh(new T.PlaneGeometry(len, .32), aoWall);wa.position.set(mid, .16, face + sign * .003);wa.rotation.set(0, flip, Math.PI);g.add(wa);
          const fa = new T.Mesh(new T.PlaneGeometry(len, .26), aoFloor);fa.rotation.order = 'YXZ';fa.rotation.set(-Math.PI / 2, flip, 0);fa.position.set(mid, .002, face + sign * .13);g.add(fa);
        }
        if (cap >= heights[room] - .001) {const t = new T.Mesh(new T.PlaneGeometry(len, .4), aoWall);t.position.set(mid, heights[room] - .2, face + sign * .003);t.rotation.set(0, flip, 0);g.add(t);}
      }
    }
    const contactShadow = (w, d, x, z, rot = 0) => {const m = new T.Mesh(new T.PlaneGeometry(w + .35, d + .35), aoBlob);m.rotation.order = 'YXZ';m.rotation.set(-Math.PI / 2, rot, 0);m.position.set(x, .003, z);group.add(m);};

    // ---- Shell: floors, walls with openings, doors.
    slab(group, data.outline, .15, -.16, R(lib.concrete()));
    const oak = R(lib.woodFloor()), tiles = R(lib.floorTiles());
    data.rooms.forEach(r => slab(group, r.polygon, .008, -.008, r.id === 'bath' ? tiles : oak));
    const trim = R(lib.paint(0xf4f3ee, .38)), glass = lib.glass();
    data.walls.forEach(w => {
      const sections = wallSections(w), a = xy(w.a), b = xy(w.b), length = Math.hypot(b[0] - a[0], b[1] - a[1]);
      const g = new T.Group();g.position.set(a[0], 0, a[1]);g.rotation.y = -Math.atan2(b[1] - a[1], b[0] - a[0]);group.add(g);
      const offset = w.offset || 0, mat = (x, y, z) => faceMaterials(w, g, x, z, w.thickness);
      const openings = (w.openings || []).map(o => ({...o, start: o.start / scale, end: o.end / scale})).sort((p, q) => p.start - q.start);
      const part = (start, end, base, top = Infinity) => sections.forEach(s => {const left = Math.max(start, s.start), right = Math.min(end, s.end), cap = Math.min(top, s.height);if (right > left && cap > base) {box(g, right - left, cap - base, w.thickness, (left + right) / 2, (base + cap) / 2, offset, mat);trims(w, g, left, right, base, cap, offset);}});
      const cuts = [...new Set([0, length, ...openings.flatMap(o => [o.start, o.end])])].sort((p, q) => p - q);
      for (let i = 0; i < cuts.length - 1; i++) {
        const left = cuts[i], right = cuts[i + 1], mid = (left + right) / 2;
        const voids = openings.filter(o => mid > o.start && mid < o.end).sort((p, q) => p.sill - q.sill);
        let base = 0;for (const o of voids) {if (o.sill > base) part(left, right, base, o.sill);base = Math.max(base, o.top);}part(left, right, base);
      }
      openings.forEach((o, index) => {
        const width = o.end - o.start, mid = (o.start + o.end) / 2, oh = o.top - o.sill;
        if (o.transom) {
          const f = .025;for (const x of [o.start + f / 2, o.end - f / 2]) box(g, f, oh, w.thickness + .012, x, o.sill + oh / 2, offset, trim);
          for (const y of [o.sill + f / 2, o.top - f / 2]) box(g, width - 2 * f, f, w.thickness + .012, mid, y, offset, trim);
          box(g, width - 2 * f, oh - 2 * f, .012, mid, o.sill + oh / 2, offset, glass);return;
        }
        if (o.type === 'window' && data.features?.kitchenWindow?.wall === w.id) {kitchenWindow(g, w, o);return;}
        box(g, .035, oh, w.thickness + .025, o.start + (o.type === 'sliding' ? -.0175 : .0175), o.sill + oh / 2, offset, trim);
        box(g, .035, oh, w.thickness + .025, o.end + (o.type === 'sliding' ? .0175 : -.0175), o.sill + oh / 2, offset, trim);
        box(g, width, .045, w.thickness + .025, mid, o.top + (o.type === 'sliding' ? .0225 : -.0225), offset, trim);
        if (o.type === 'window') {
          box(g, width, .04, w.thickness + .10, mid, o.sill - .02, offset, trim);
          if (o.trimBottom !== undefined) box(g, width, o.sill - o.trimBottom, .025, mid, (o.sill + o.trimBottom) / 2, -.0125, trim);
          box(g, width - .07, oh - .045, .012, mid, o.sill + oh / 2, offset, glass);
          box(g, .035, oh, .055, mid, o.sill + oh / 2, offset, trim);
        } else if (o.type === 'sliding') slidingDoor(g, w, o, width, oh);
        else hingedDoor(g, w, o, index, width, oh, offset, mid);
      });
    });
    function kitchenWindow(g, w, o) {
      const f = data.features.kitchenWindow, width = o.end - o.start, mid = (o.start + o.end) / 2, offset = w.offset || 0;
      const iw = f.insideWidthCm / 100, ih = f.insideHeightCm / 100, side = (width - iw) / 2, top = o.top - o.sill - ih;
      const is = o.start + side, ie = o.end - side, it = o.sill + ih, thin = f.thinTrimCm / 100, div = f.dividerCm / 100, ls = f.leftSpanCm / 100;
      box(g, side, o.top - o.sill, w.thickness + .025, o.start + side / 2, (o.top + o.sill) / 2, offset, trim);
      box(g, side, o.top - o.sill, w.thickness + .025, o.end - side / 2, (o.top + o.sill) / 2, offset, trim);
      box(g, iw, top, w.thickness + .025, mid, o.top - top / 2, offset, trim);
      const bb = f.trimBottomCm / 100, bh = f.boardCm / 100, pb = bb + bh, ph = o.sill - pb;
      box(g, width, bh, .025, mid, bb + bh / 2, offset - w.thickness / 2 - .0125, trim);box(g, width, ph, w.thickness + .10, mid, pb + ph / 2, offset, trim);
      const de = ie - ls, ds = de - div;
      box(g, thin, ih, .055, is + thin / 2, o.sill + ih / 2, offset, trim);box(g, thin, ih, .055, ie - thin / 2, o.sill + ih / 2, offset, trim);box(g, div, ih, .055, (ds + de) / 2, o.sill + ih / 2, offset, trim);
      for (const [p, q] of [[is + thin, ds], [de, ie - thin]]) {box(g, q - p, thin, .055, (p + q) / 2, o.sill + thin / 2, offset, trim);box(g, q - p, thin, .055, (p + q) / 2, it - thin / 2, offset, trim);box(g, q - p, ih - 2 * thin, .012, (p + q) / 2, o.sill + ih / 2, offset, glass);}
    }
    function slidingDoor(g, w, o, width, height) {
      const id = `${w.id}-slide`, hinge = new T.Group(), side = o.slideSide ?? -1, depth = .032, closedX = (o.start + o.end) / 2, travel = (o.slideDirection ?? -1) * (width + .03);
      hinge.position.set(closedX, 0, (w.offset || 0) + side * (w.thickness / 2 + depth / 2 + Math.max(.0175, o.slideClearance ?? .035)));hinge.userData.noMerge = true;g.add(hinge);
      const leaf = box(hinge, width + .04, height - .025, depth, 0, o.sill + (height - .025) / 2, 0, R(lib.paint(0xdcd9cb, .4)), false);
      box(hinge, .018, .17, .02, (o.slideDirection ?? -1) * -(width / 2 - .08), 1.0, side * .025, R(lib.metal(0x303d39, .4)), false);
      leaf.userData.doorId = id;doors.push({id, name: o.name || 'Bedroom sliding door', motion: 'slide', hinge, leaf, closedX, travel, open: false});
      box(g, width + Math.abs(travel) + .08, .045, .025, closedX + travel / 2, o.top + .035, hinge.position.z, trim, false);
    }
    function hingedDoor(g, w, o, index, width, oh, offset, mid) {
      const endHinge = o.hinge === 'end', dir = endHinge ? -1 : 1, swing = o.swing ?? -1, hinge = new T.Group(), id = `${w.id}-${index}`;
      const lw = width - .078, lh = oh - .070, ld = .032, side = -swing * dir;
      hinge.position.set(endHinge ? o.end - .039 : o.start + .039, 0, offset + side * (w.thickness / 2 + ld / 2 + .004));hinge.userData.noMerge = true;g.add(hinge);
      const mat = w.id === 'W05' ? R(lib.wood(0x7a4a2a, .45)) : R(lib.paint(0xf3f2ed, .35));
      const leaf = box(hinge, lw, lh, ld, dir * lw / 2, o.sill + .022 + lh / 2, 0, mat, false);
      if (w.id === 'W01') {
        leaf.visible = false;const cx = dir * lw / 2, cy = o.sill + .022 + lh / 2, rail = .09;
        for (const sx of [-1, 1]) box(hinge, rail, lh, ld, cx + sx * (lw / 2 - rail / 2), cy, 0, mat, false);
        box(hinge, lw - 2 * rail, rail, ld, cx, o.sill + .022 + lh - rail / 2, 0, mat, false);box(hinge, lw - 2 * rail, .38, ld, cx, o.sill + .022 + .19, 0, mat, false);
        box(hinge, lw - 2 * rail, lh - rail - .38, .012, cx, o.sill + .022 + .38 + (lh - rail - .38) / 2, 0, glass, false);
      }
      const handle = new T.Mesh(new T.CylinderGeometry(.011, .011, .12, 10), R(lib.metal(0xbfc3c6, .3)));handle.rotation.z = Math.PI / 2;handle.position.set(dir * (lw - .07), o.sill + 1.02, side * (ld / 2 + .03));hinge.add(handle);
      hinge.traverse(m => {if (m.isMesh) m.userData.doorId = id;});
      doors.push({id, name: w.id === 'W01' ? 'Balcony door' : w.id === 'W05' ? 'Entrance door' : w.id === 'W09' ? 'Bathroom door' : 'Door', hinge, leaf, open: false, openAngle: swing * Math.PI / 2, sound: w.id === 'W05' ? 'entrance' : 'room'});
      box(g, width, .014, w.thickness + .02, mid, .015, offset, R(lib.paint(0x8d998f, .5)), false);
    }

    // ---- Your kitchen and bedroom design (unchanged), with realistic finishes.
    const sage = R(lib.paint(0x66786d, .42)), wood = R(lib.wood(0xbb946e, .45)), dark = R(lib.paint(0x26302c, .4)), white = R(lib.fabric(0xf2ede2)), steel = R(lib.metal(0x9aa8a9, .28));
    for (const f of data.proposal?.furniture || []) {
      if (f.ceilingRoom) f.height = Math.max(.01, heights[f.ceilingRoom] - (f.base ?? 0));
      const g = new T.Group();g.position.set(f.x, 0, f.z);g.rotation.y = f.rotation || 0;group.add(g);
      if (!(f.base > 0)) contactShadow(f.width, f.depth, f.x, f.z, f.rotation || 0);
      const w = f.width, d = f.depth, h = f.height, base = f.base ?? 0, front = d / 2, body = f.color ? R(lib.paint(f.color, .42)) : sage;
      const add = (ww, hh, dd, x, y, z, m, solid = true, parent = g) => box(parent, ww, hh, dd, x, y, z, m, solid);
      const panel = (x, y, pw, ph, z = front - .012, m = body) => {
        add(pw, ph, .02, x, y, z, m);const rim = .045;
        for (const xx of [x - pw / 2 + rim / 2, x + pw / 2 - rim / 2]) add(rim, ph, .014, xx, y, z + .014, m, false);
        for (const yy of [y - ph / 2 + rim / 2, y + ph / 2 - rim / 2]) add(pw - rim * 2, rim, .014, x, yy, z + .014, m, false);
        add(.09, .012, .026, x, y + ph * .32, z + .034, steel, false);
      };
      if (f.kind === 'bed') {
        add(w, .22, d, 0, .20, 0, wood);add(w - .03, .22, d - .04, 0, .42, 0, white);add(w, .68, .045, 0, .40, -d / 2 + .023, wood);
        add(w - .06, .045, d * .63, 0, .55, d * .15, R(lib.fabric(0xb8bfb0)), false);for (const xx of [-w * .25, w * .25]) add(w * .41, .095, .42, xx, .58, -d * .32, white, false);
      } else if (f.kind === 'wardrobe' && f.sliding === false) {
        add(w, h, d - .025, 0, base + h / 2, -.0125, body);for (let i = 0; i < 2; i++) panel((i ? 1 : -1) * w / 4, base + h / 2, w / 2 - .006, h - .01);
      } else if (f.kind === 'wardrobe') {
        const thick = .025, pw = (w - .06) / 2 + .008, ph = h - .08;
        add(w, h, .025, 0, base + h / 2, -d / 2 + .0125, body);for (const x of [-w / 2 + thick / 2, w / 2 - thick / 2]) add(thick, h, d, x, base + h / 2, 0, body);
        for (const y of [base + .02, base + h - .02]) add(w, .04, d, 0, y, 0, body);add(w - .05, .025, d - .08, 0, base + h * .57, -.02, wood);
        for (let i = 0; i < 2; i++) {
          const hinge = new T.Group(), closedX = (i ? 1 : -1) * (w - .06) / 4;hinge.position.x = closedX;hinge.userData.noMerge = true;g.add(hinge);
          const leaf = add(pw, ph, .019, 0, base + h / 2, front - .02 - i * .025, body, false, hinge);add(.012, .35, .018, (i ? -1 : 1) * (pw / 2 - .05), base + h * .53, front - .004 - i * .025, steel, false, hinge);
          const id = `${f.id}-panel-${i}`;hinge.traverse(m => {if (m.isMesh) m.userData.doorId = id;});
          doors.push({id, name: `Wardrobe ${i ? 'right' : 'left'} panel`, motion: 'slide', hinge, leaf, closedX, travel: (i ? -1 : 1) * (w - .06) / 2, open: false, sound: 'slide'});
        }
      } else if (f.kind === 'hood') {
        add(w, h * .30, d, 0, base + h * .15, 0, dark);add(w * .44, h * .72, d * .52, 0, base + h * .64, -d * .20, dark);
      } else if (f.kind === 'upper') {
        add(w, h, d - .025, 0, base + h / 2, -.0125, body);const count = w > .7 ? 2 : 1;for (let i = 0; i < count; i++) panel(-w / 2 + w / count * (i + .5), base + h / 2, w / count - .006, h - .01);
      } else if (f.kind === 'fridge') {
        add(w, h, d - .024, 0, base + h / 2, -.012, body);panel(0, base + h * .25, w - .01, h * .49);panel(0, base + h * .75, w - .01, h * .49);add(w - .08, .045, .014, 0, base + .065, front + .018, dark, false);
      } else {
        const top = .035, plinth = .10, bh = h - plinth - top, notch = f.pipeNotch, cut = notch?.width || 0, fw = w - cut, fx = cut / 2;
        add(fw - .07, plinth, d - .09, fx, base + plinth / 2, -.03, dark);add(fw, bh, d - .025, fx, base + plinth + bh / 2, -.0125, body);add(fw + .004, top, d + .012, fx, base + h - top / 2, .002, wood);
        if (notch) {const tail = d - notch.depth;add(cut, bh, tail, -w / 2 + cut / 2, base + plinth + bh / 2, -notch.depth / 2, body);add(cut, top, tail, -w / 2 + cut / 2, base + h - top / 2, -notch.depth / 2, wood);}
        if (f.kind === 'oven') {
          add(w - .055, bh - .025, .022, 0, base + plinth + bh / 2, front, dark);add(w - .12, bh * .55, .012, 0, base + plinth + bh * .45, front + .016, new T.MeshStandardMaterial({color: 0x1f2628, metalness: .3, roughness: .08}), false);
          add(w - .10, .018, .04, 0, base + h - .17, front + .035, steel, false);add(w - .055, .008, d - .08, 0, base + h + .003, 0, dark, false);
          for (const xx of [-w * .24, w * .24]) for (const zz of [-d * .23, d * .23]) add(w * .23, .004, d * .23, xx, base + h + .009, zz, new T.MeshStandardMaterial({color: 0x2a2e2d, roughness: .15}), false);
        } else if (f.kind === 'dishwasher') {panel(0, base + plinth + bh / 2, w - .01, bh - .012);add(w - .04, .035, .008, 0, base + h - .09, front + .027, dark, false);}
        else {
          const count = f.kind === 'base' ? 3 : 1;for (let i = 0; i < count; i++) panel(fx, base + plinth + bh / count * (i + .5), fw - .01, bh / count - .007);
          if (f.kind === 'sink') {add(fw * .68, .012, d * .66, fx, base + h + .009, 0, steel, false);add(fw * .56, .014, d * .52, fx, base + h + .018, 0, R(lib.metal(0x8d9799, .22)), false);add(.025, .24, .025, fx, base + h + .13, -d * .35, steel, false);add(.025, .025, .18, fx, base + h + .25, -d * .23, steel, false);}
        }
        if (f.kind === 'prep' || f.id === 'prep') f.worktop = base + h;
      }
    }
    // ---- Living room and FINN bathroom furnishing.
    const furnish = window.Furnishings.create({T, lib}), colliderMat = new T.MeshBasicMaterial({visible: false});
    const ctxF = {parent: group, part: () => {}, collider: (w, h, d, x, y, z, rot) => {if (y - h / 2 < .05) contactShadow(w, d, x, z, rot);const c = new T.Mesh(new T.BoxGeometry(w, h, d), colliderMat);c.position.set(x, y, z);c.rotation.y = rot;c.visible = false;group.add(c);solids.push(c);}};
    furnish.living(ctxF);furnish.bathroom(ctxF);
    const pipe = data.features?.pipe;
    if (pipe) {const [x, z] = xy(pipe.position), h = heights[pipe.room], m = new T.Mesh(new T.CylinderGeometry(pipe.diameter / 2, pipe.diameter / 2, h, 16), R(lib.paint(0xb38b68, .5)));m.position.set(x, h / 2, z);m.castShadow = m.receiveShadow = true;group.add(m);solids.push(m);}
    // Balcony: chequered steel deck and dark railing (FINN photo 7).
    const balTop = 108 - .78 * scale;
    slab(group, [[578, balTop], [648, balTop], [648, 108], [578, 108]], .10, -.03, R(lib.steelPlate()));
    const railMat = R(lib.metal(0x2f3437, .55)), bx = xy([578, balTop]), be = xy([648, 108]);
    [[bx[0], bx[1], be[0], bx[1]], [bx[0], bx[1], bx[0], be[1]], [be[0], bx[1], be[0], be[1]]].forEach(r => {
      const dx = r[2] - r[0], dz = r[3] - r[1], len = Math.hypot(dx, dz), g = new T.Group();g.position.set(r[0], 0, r[1]);g.rotation.y = -Math.atan2(dz, dx);group.add(g);
      box(g, len, .04, .04, len / 2, 1.04, 0, railMat);box(g, len, .025, .025, len / 2, .14, 0, railMat, false);
      for (let i = 0; i <= Math.ceil(len / .13); i++) box(g, .018, .92, .018, len * i / Math.ceil(len / .13), .58, 0, railMat, false);
    });
    // Ceilings (and steps between rooms of different height).
    const ceilingPaint = R(lib.plaster(0xf7f6f2));
    data.rooms.forEach(r => slab(group, r.polygon, .13, heights[r.id], ceilingPaint));
    data.ceilingTransitions.forEach(t => {const lo = Math.min(...t.rooms.map(id => heights[id])), hi = Math.max(...t.rooms.map(id => heights[id]));if (hi - lo < .001) return;const a = xy(t.a), b = xy(t.b), g = new T.Group();g.position.set(a[0], 0, a[1]);g.rotation.y = -Math.atan2(b[1] - a[1], b[0] - a[0]);group.add(g);const L = Math.hypot(b[0] - a[0], b[1] - a[1]);box(g, L, hi - lo, .10, L / 2, (hi + lo) / 2, 0, ceilingPaint, false);});

    // ---- Drinks (interactable): six beers on the kitchen worktop beside the fridge, a bottle of aquavit on the TV bench.
    const kitchenTop = (data.proposal?.furniture || []).find(f => f.id === 'shallow-1') || (data.proposal?.furniture || []).find(f => f.kind === 'base');
    const beerGlass = new T.MeshStandardMaterial({color: 0x5a3410, roughness: .15, metalness: .1, transparent: true, opacity: .92}), label = new T.MeshStandardMaterial({color: 0xd9c27a, roughness: .5}), cap = R(lib.metal(0xc0c4c7, .3));
    function bottle(x, y, z, kind) {
      const g = new T.Group();g.position.set(x, y, z);g.userData.noMerge = true;group.add(g);
      if (kind === 'beer') {
        const body = new T.Mesh(new T.CylinderGeometry(.031, .031, .16, 14), beerGlass);body.position.y = .08;
        const neck = new T.Mesh(new T.CylinderGeometry(.013, .028, .07, 12), beerGlass);neck.position.y = .195;
        const lab = new T.Mesh(new T.CylinderGeometry(.0315, .0315, .07, 14, 1, true), label);lab.position.y = .08;
        const c = new T.Mesh(new T.CylinderGeometry(.014, .014, .012, 10), cap);c.position.y = .235;g.add(body, neck, lab, c);
      } else {
        const glassM = new T.MeshStandardMaterial({color: 0xe8e2c8, roughness: .05, transparent: true, opacity: .55});
        const body = new T.Mesh(new T.CylinderGeometry(.045, .045, .22, 16), glassM);body.position.y = .11;
        const liquid = new T.Mesh(new T.CylinderGeometry(.041, .041, .16, 16), new T.MeshStandardMaterial({color: 0xd9a54a, roughness: .1, transparent: true, opacity: .8}));liquid.position.y = .085;
        const neck = new T.Mesh(new T.CylinderGeometry(.015, .04, .07, 12), glassM);neck.position.y = .255;const c = new T.Mesh(new T.CylinderGeometry(.017, .017, .03, 10), R(lib.paint(0x1d2b22, .4)));c.position.y = .3;
        const lab = new T.Mesh(new T.BoxGeometry(.06, .08, .002), new T.MeshStandardMaterial({color: 0xf0e7cf, roughness: .6}));lab.position.set(0, .12, .046);g.add(body, liquid, neck, c, lab);g.userData.liquid = liquid;
      }
      g.traverse(m => {if (m.isMesh) m.castShadow = true;});return g;
    }
    if (kitchenTop) {
      const yTop = (kitchenTop.base ?? 0) + kitchenTop.height + .002;
      for (let i = 0; i < 6; i++) {const g = bottle(kitchenTop.x - .14 + (i % 3) * .07, yTop, kitchenTop.z - .04 + Math.floor(i / 3) * .07, 'beer');drinks.push({kind: 'beer', object: g, name: 'Beer'});}
    }
    const aquavit = bottle(2.42, .48, 3.3, 'liquor');drinks.push({kind: 'liquor', object: aquavit, name: 'Aquavit', sips: 12});

    // ---- Lights for the windowless rooms.
    const lights = [new T.PointLight(0xfff3e2, 3, 6, 1.5), new T.PointLight(0xffecd2, 4.5, 5, 1.6)];
    lights[0].position.set(4.55, 2.3, 5.85);lights[1].position.set(2.55, 2.3, 5.75);lights.forEach(l => group.add(l));

    // Walkable floors and solid obstacles (computed before merging), then bake the static geometry.
    const pts = poly => poly.map(p => {const [x, z] = xy(p);return {x, z};});
    const floors = data.rooms.map(r => ({points: pts(r.polygon), ceilingHeight: heights[r.id], name: r.name, surfaceKind: r.id === 'bath' ? 'tile' : 'wood'}));
    floors.push({points: pts([[578, balTop], [648, balTop], [648, 108], [578, 108]]), ceilingHeight: Infinity, name: 'Balcony', surfaceKind: 'metal'});
    group.updateMatrixWorld(true);
    const obstacles = solids.filter(m => !m.parent || !isInDoor(m)).map(meshObstacle);
    function isInDoor(m) {for (let p = m.parent; p; p = p.parent) if (p.userData.noMerge) return true;return false;}
    function meshObstacle(mesh) {
      const p = mesh.geometry.parameters, h = p.height, w = p.width ?? p.radiusTop * 2, d = p.depth ?? p.radiusTop * 2;
      const points = [[-w / 2, -d / 2], [w / 2, -d / 2], [w / 2, d / 2], [-w / 2, d / 2]].map(([x, z]) => {const v = mesh.localToWorld(new T.Vector3(x, 0, z));return {x: v.x, z: v.z};});
      const c = mesh.getWorldPosition(new T.Vector3());return {points, minY: c.y - h / 2, maxY: c.y + h / 2};
    }
    lib.mergeStatic(group, m => m.userData.doorId || m.userData.keep);
    const spawnAreas = [...data.rooms.map(r => ({name: r.name, points: pts(r.polygon), ceilingHeight: heights[r.id]})), {name: 'Balcony', points: pts([[578, balTop], [648, balTop], [648, 108], [578, 108]]), ceilingHeight: Infinity}];
    return {group, floors, obstacles, doors, drinks, lights, heights, spawnAreas, xy, meshObstacle, rooms: data.rooms};
  }
  return {build};
});
