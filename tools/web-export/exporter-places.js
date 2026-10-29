/* Extra geometry exports for the places the web game added on 29 Oct 2026 (runs after exporter.js, see export.mjs).
 * rigs.d/places.json maps them onto rigs:
 *   sculpture:  HODET N.N. and its stone steps (game-sculpture.js), static level geometry
 *   stash:      the three crates (game-stash.js); "item<i>.part<j>" = the parts the game hides while item i is used up
 *   shops:      Joker and Kiwi (game-shops.js), static level geometry; "shop<i>.door<k>" = the two sliding doors
 *               (the C++ code moves them like update() does). The cashiers are left out (the game puts its own people
 *               there) and the product facings (one instanced mesh with a colour per box) become one mesh with vertex
 *               colours.
 *   muselunden: the 12 Muselunden holes (game-minigames.js discGolf), with the disc as "disc" like mg_discgolf.
 * The invisible hit boxes are left out (their boxes are in game.json). Nothing in the page is changed for good.
 */
(() => {
  'use strict';
  const {exportRig, log} = window.__hkExport, G = window.haarklousGame, T = window.THREE;
  // Take objects out of their parents for one export, then put them back where they were.
  function without(list, fn) {
    const saved = list.map(o => [o, o.parent, o.parent ? o.parent.children.indexOf(o) : -1]);
    saved.forEach(([o, p]) => p && p.remove(o));
    try {fn();} finally {saved.slice().reverse().forEach(([o, p, i]) => {if (!p) return;p.add(o);p.children.splice(p.children.indexOf(o), 1);p.children.splice(i, 0, o);});}
  }

  exportRig('sculpture', G.sculpture.group);

  {
    const keys = new Map();
    G.stash.items.forEach((it, i) => it.parts.forEach((p, j) => keys.set(p, `item${i}.part${j}`)));
    without(G.stash.hitboxes, () => exportRig('stash', G.stash.group, keys));
  }

  {
    const root = G.shops.group, keys = new Map(), out = [], temps = [];
    root.children.forEach((g, i) => {
      const isDoor = o => o.isGroup && o.children[0] && o.children[0].isMesh && o.children[0].geometry.type === 'PlaneGeometry' && Math.abs(o.children[0].geometry.parameters.width - .95) < 1e-6;
      const doors = g.children.filter(isDoor);
      if (doors.length !== 2) throw new Error(`shop ${i}: ${doors.length} doors`);
      doors.forEach((d, k) => keys.set(d, `shop${i}.door${k}`));
      out.push(...g.children.filter(o => o.isGroup && o.userData.noMerge && !isDoor(o)));// the cashier
      for (const im of g.children.filter(o => o.isInstancedMesh)) {
        const src = im.geometry.index ? im.geometry.toNonIndexed() : im.geometry, P = src.attributes.position, N = src.attributes.normal, U = src.attributes.uv, n = P.count;
        const pos = new Float32Array(im.count * n * 3), nor = new Float32Array(im.count * n * 3), uv = new Float32Array(im.count * n * 2), col = new Float32Array(im.count * n * 3);
        const m = new T.Matrix4(), nm = new T.Matrix3(), v = new T.Vector3(), c = new T.Color();
        for (let k = 0; k < im.count; k++) {
          im.getMatrixAt(k, m);nm.getNormalMatrix(m);im.getColorAt(k, c);
          for (let q = 0; q < n; q++) {
            const o = (k * n + q) * 3;
            v.fromBufferAttribute(P, q).applyMatrix4(m);pos.set([v.x, v.y, v.z], o);
            v.fromBufferAttribute(N, q).applyMatrix3(nm).normalize();nor.set([v.x, v.y, v.z], o);
            col.set([c.r, c.g, c.b], o);uv.set([U.getX(q), U.getY(q)], (k * n + q) * 2);
          }
        }
        const geo = new T.BufferGeometry();geo.setAttribute('position', new T.BufferAttribute(pos, 3));geo.setAttribute('normal', new T.BufferAttribute(nor, 3));
        geo.setAttribute('uv', new T.BufferAttribute(uv, 2));geo.setAttribute('color', new T.BufferAttribute(col, 3));
        const mesh = new T.Mesh(geo, new T.MeshStandardMaterial({roughness: .6, vertexColors: true}));mesh.name = 'products';
        mesh.position.copy(im.position);mesh.quaternion.copy(im.quaternion);mesh.scale.copy(im.scale);mesh.castShadow = mesh.receiveShadow = true;
        temps.push([g, im, mesh]);
      }
    });
    temps.forEach(([g, im, mesh]) => {g.remove(im);g.add(mesh);});
    without([...out, ...G.shops.hitboxes], () => exportRig('shops', root, keys));
    temps.forEach(([g, im, mesh]) => {g.remove(mesh);g.add(im);mesh.geometry.dispose();});
  }

  if (G.mini.muselunden) {
    const g = G.mini.muselunden.group, disc = g.children.find(o => o.isMesh && o.geometry.type === 'CylinderGeometry' && Math.abs(o.geometry.parameters.radiusTop - .105) < 1e-9);
    if (!disc) throw new Error('muselunden: disc not found');
    exportRig('mg_muselunden', g, new Map([[disc, 'disc']]));
  }
  log.push('exporter-places: sculpture, stash, shops, muselunden');
})();
