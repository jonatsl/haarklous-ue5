/* Grocery shops at their real places (OpenStreetMap nodes, Overpass 29 Oct 2026): Joker Torshov (node 1506608498, about 110 m
 * from the flat) and Kiwi Torshov (node 2476542841, about 270 m). Each shop is cut into the ground floor of the building it is in:
 * the building outline gets a notch, the shop fills it (glass front with sliding doors and the brand sign, shelves, fridges,
 * a till with a cashier, a bottle-return machine) and the facade above the shop is rebuilt so the building looks whole.
 * Money is in Norwegian kroner. The wallet (money, empty bottles, the bag of bought items) is saved in the browser.
 * GameShops.applyNotches(data)  – call before Surroundings.create (changes the two building outlines).
 * GameShops.create({T, lib, world, audio}) -> {group, shops, floors, obstacles, hitboxes, wallet, priceOf, beerAllowed,
 *   shopAt(p, feet), atTill(shop, p), update(dt, player, camera)}
 */
(function (root, factory) {
  root.GameShops = factory();
})(typeof window === 'undefined' ? globalThis : window, function () {
  'use strict';
  // building / edge = the building outline in surroundings-data.js and the wall the shop front is on; t0..t1 = part of that wall.
  const SHOPS = [
    {id: 'joker', name: 'Joker Torshov', brand: 'JOKER', osm: 1506608498, building: 888, edge: 6, t0: .08, t1: .92, depth: 10, sign: {bg: '#ffd200', fg: '#d0021b'}, shirt: 0xd0021b, hours: 'Åpent man–fre 07–24 · lør 08–24 · søn 09–24', priceFactor: 1},
    {id: 'kiwi', name: 'Kiwi Torshov', brand: 'KIWI', osm: 2476542841, building: 156, edge: 20, t0: .04, t1: .40, depth: 10, sign: {bg: '#2e9a3f', fg: '#ffffff'}, shirt: 0x2e9a3f, hours: 'Åpent man–lør 07–23', priceFactor: .95}
  ];
  // Prices in kroner (Joker; Kiwi is about 5 % cheaper). where: fridge (back wall), shelf (gondolas), floor (by the door).
  const PRODUCTS = [
    {id: 'pils', name: 'Ringnes Pils 0,5 l', price: 39.9, beer: true, color: 0x3a6e2e, where: 'fridge', use: 'beer'},
    {id: 'solo', name: 'Solo 0,5 l', price: 29.9, color: 0xf7941d, where: 'fridge', use: 'soda'},
    {id: 'cola', name: 'Coca-Cola 0,5 l', price: 34.9, color: 0xc8102e, where: 'fridge', use: 'soda'},
    {id: 'redbull', name: 'Red Bull 0,25 l', price: 29.9, color: 0x1e3f8f, where: 'fridge', use: 'energy'},
    {id: 'melk', name: 'Tine Lettmelk 1 l', price: 24.9, color: 0xf2f2f2, where: 'fridge', use: 'food', sober: 15},
    {id: 'grandiosa', name: 'Grandiosa', price: 69.9, color: 0x1f4f9c, where: 'shelf', use: 'food', sober: 45},
    {id: 'kvikklunsj', name: 'Kvikk Lunsj', price: 24.9, color: 0xe8c31a, where: 'shelf', use: 'food', sober: 15},
    {id: 'banan', name: 'Banan', price: 6.9, color: 0xf3d23c, where: 'shelf', use: 'food', sober: 10},
    {id: 'brod', name: 'Kneippbrød', price: 39.9, color: 0x9b6a3a, where: 'shelf', use: 'food', sober: 20},
    {id: 'kaffe', name: 'Friele Frokostkaffe', price: 74.9, color: 0xb3262e, where: 'shelf', use: 'food', sober: 30},
    {id: 'polser', name: 'Grillpølser', price: 49.9, color: 0xc7463a, where: 'shelf', use: 'food', sober: 25},
    {id: 'ved', name: 'Sekk med ved', price: 89.9, color: 0x7a5a3a, where: 'floor', use: 'wood'}
  ];
  const PANT = 2;// kroner per empty bottle or can (the Norwegian deposit for 0.5 l and smaller; every drink sold here is 0.5 l or less)
  const kr = v => `${v.toFixed(2).replace('.', ',')} kr`;
  const priceOf = (shop, product) => shop.priceFactor === 1 ? product.price : Math.round(product.price * shop.priceFactor - .9) + .9;
  // Norwegian law: grocery shops may sell beer Mon–Fri 08–20 and Sat 08–18, not on Sundays.
  function beerAllowed(d) {const day = d.getDay(), h = d.getHours() + d.getMinutes() / 60;if (day === 0) return false;return h >= 8 && h < (day === 6 ? 18 : 20);}

  // ---------- The notch: a rectangle W × depth cut into the building outline behind part of one wall ----------
  const insidePoly = (p, ring) => {let r = false;for (let i = 0, j = ring.length - 1; i < ring.length; j = i++) {const a = ring[i], b = ring[j];if ((a[1] > p[1]) !== (b[1] > p[1]) && p[0] < (b[0] - a[0]) * (p[1] - a[1]) / (b[1] - a[1]) + a[0]) r = !r;}return r;};
  function notchFor(ring, edge, t0, t1, depthWanted) {
    const a = ring[edge], b = ring[(edge + 1) % ring.length], dx = b[0] - a[0], ds = b[1] - a[1], L = Math.hypot(dx, ds), u = [dx / L, ds / L];
    const mid = [(a[0] + b[0]) / 2, (a[1] + b[1]) / 2];let n = [-u[1], u[0]];if (!insidePoly([mid[0] + n[0] * .5, mid[1] + n[1] * .5], ring)) n = [-n[0], -n[1]];
    const P0 = [a[0] + dx * t0, a[1] + ds * t0], P1 = [a[0] + dx * t1, a[1] + ds * t1], W = L * (t1 - t0);
    const at = (fu, fv, d) => [P0[0] + u[0] * W * fu + n[0] * d * fv, P0[1] + u[1] * W * fu + n[1] * d * fv];
    const fits = d => {
      for (let i = 0; i <= 12; i++) for (let j = 1; j <= 10; j++) if (!insidePoly(at(.02 + .96 * i / 12, j / 10 * (1 - .3 / d), d), ring)) return false;
      for (const v of ring) {if (v === a || v === b) continue;const pu = (v[0] - P0[0]) * u[0] + (v[1] - P0[1]) * u[1], pv = (v[0] - P0[0]) * n[0] + (v[1] - P0[1]) * n[1];if (pu > -.3 && pu < W + .3 && pv > -.01 && pv < d + .3) return false;}
      return true;
    };
    let depth = depthWanted;while (depth > 5 && !fits(depth)) depth -= .5;
    const Q0 = at(0, 1, depth), Q1 = at(1, 1, depth);
    return {ring: [...ring.slice(0, edge + 1), P0, Q0, Q1, P1, ...ring.slice(edge + 1)], P0, P1, u, n, W, depth};
  }
  function applyNotches(data) {
    for (const s of SHOPS) {
      const b = data.buildings[s.building];if (!b || s.local) continue;
      const ring = [];for (let i = 0; i < b.r.o.length; i += 2) ring.push([b.r.o[i], b.r.o[i + 1]]);
      const nt = notchFor(ring, s.edge, s.t0, s.t1, s.depth);b.r.o = nt.ring.flat();s.local = nt;
    }
    return SHOPS;
  }

  // ---------- Wallet: money (kr), empty bottles, bag of items; saved in the browser ----------
  function makeWallet() {
    const KEY = 'haarklous-wallet-v1';let st = {money: 500, pant: 0, bag: {}};
    try {const s = JSON.parse(localStorage.getItem(KEY) || 'null');if (s && Number.isFinite(s.money)) st = {money: s.money, pant: s.pant | 0, bag: s.bag || {}};} catch {}
    const save = () => {try {localStorage.setItem(KEY, JSON.stringify(st));} catch {}};
    const round = v => Math.round(v * 100) / 100;
    return {
      get money() {return st.money;}, get pant() {return st.pant;}, get bag() {return st.bag;},
      earn(v) {if (!(v > 0)) return;st.money = round(st.money + v);save();},
      pay(v) {if (!(v >= 0) || st.money + 1e-9 < v) return false;st.money = round(st.money - v);save();return true;},
      addPant(n = 1) {st.pant += n;save();}, cashPant() {const n = st.pant;st.pant = 0;st.money = round(st.money + n * PANT);save();return n;},
      add(id, n = 1) {st.bag[id] = (st.bag[id] || 0) + n;save();}, take(id) {if (!st.bag[id]) return false;if (--st.bag[id] <= 0) delete st.bag[id];save();return true;},
      reset(money = 500) {st = {money, pant: 0, bag: {}};save();}
    };
  }

  // ---------- The shop buildings ----------
  function create({T, lib, world, audio}) {
    const group = new T.Group();group.name = 'Shops';
    const floors = [], obstacles = [], hitboxes = [], built = [];
    const avatars = window.GameAvatars ? window.GameAvatars.create({T, lib}) : null;
    const facadeMat = lib.facade(), plinthMat = lib.plinth(), roofMat = lib.roof();facadeMat.color.setScalar(.78);plinthMat.color.setScalar(.75);
    const tileM = lib.floorTiles(), wallM = lib.plaster(0xf2f1ec), ceilM = new T.MeshStandardMaterial({color: 0xf4f4f2, roughness: .9});
    const aluM = lib.metal(0x5b6066, .35), glassM = new T.MeshPhysicalMaterial({color: 0xbfd3dc, transparent: true, opacity: .22, roughness: .04, metalness: .1, side: T.DoubleSide, depthWrite: false});
    const shelfM = lib.metal(0xd9dbdd, .45), darkM = lib.polymer(0x232528), fridgeM = new T.MeshStandardMaterial({color: 0xe9ecee, roughness: .4, metalness: .2});
    const lightM = new T.MeshStandardMaterial({color: 0xffffff, emissive: 0xfff6e8, emissiveIntensity: 1.6}), fridgeLightM = new T.MeshStandardMaterial({color: 0xdfefff, emissive: 0xcfe6ff, emissiveIntensity: 1.2});
    const hitM = new T.MeshBasicMaterial({visible: false}), sackM = lib.fabric(0x8a6a44);
    const box = (parent, w, h, d, x, y, z, mat) => {const m = new T.Mesh(new T.BoxGeometry(w, h, d), mat);m.position.set(x, y, z);m.castShadow = m.receiveShadow = true;parent.add(m);return m;};
    function textTexture(w, h, draw) {const c = document.createElement('canvas');c.width = w;c.height = h;draw(c.getContext('2d'), w, h);const t = new T.CanvasTexture(c);t.colorSpace = T.SRGBColorSpace;t.anisotropy = 4;return t;}
    const productTex = new Map();
    for (const s of SHOPS) {
      if (!s.local) continue;
      const L = s.local, D = L.depth, W = L.W, top = world.data.buildings[s.building].top;
      // Store frame: x = along the front, z = into the shop, y up from the shop floor.
      const w0 = world.toWorld(...L.P0), w1 = world.toWorld(...L.P1), wn = world.toWorld(L.P0[0] + L.n[0], L.P0[1] + L.n[1]);
      let ux = (w1.x - w0.x) / W, uz = (w1.z - w0.z) / W, origin = w0;const nx0 = wn.x - w0.x, nz0 = wn.z - w0.z;
      if (ux * nz0 - uz * nx0 < 0) {origin = w1;ux = -ux;uz = -uz;}
      const vx = -uz, vz = ux, toW = (a, b) => ({x: origin.x + ux * a + vx * b, z: origin.z + uz * a + vz * b});
      let maxH = -1e9;for (let i = 0; i <= 10; i++) for (let j = 0; j <= 10; j++) {const p = toW(W * i / 10, D * j / 10);maxH = Math.max(maxH, world.heightWorld(p.x, p.z));}
      const floorY = maxH + .05, H = 3.3, storeTop = 3.62, uDoor = W - 2.0;
      const g = new T.Group();g.position.set(origin.x, floorY, origin.z);g.rotation.y = Math.atan2(-uz, ux);group.add(g);
      const rect = (a0, a1, b0, b1, minY, maxY, kind = 'shop') => ({points: [toW(a0, b0), toW(a1, b0), toW(a1, b1), toW(a0, b1)], minY: floorY + minY, maxY: floorY + maxY, kind});
      // Floor, ceiling, wall linings (inset from the building's notch walls so nothing is coplanar).
      box(g, W, .04, D, W / 2, -.02, D / 2, tileM);box(g, W, .04, D, W / 2, H + .02, D / 2, ceilM);
      box(g, .02, H, D - .08, .04, H / 2, D / 2, wallM);box(g, .02, H, D - .08, W - .04, H / 2, D / 2, wallM);box(g, W - .08, H, .02, W / 2, H / 2, D - .04, wallM);
      for (let i = 0; i < 3; i++) for (let j = 0; j < 2; j++) box(g, 1.2, .04, .3, W * (i + 1) / 4, H - .01, D * (j + 1) / 3, lightM);
      const light = new T.PointLight(0xfff4e6, 0, 18, 1.5);light.position.set(W / 2, H - .4, D / 2);g.add(light);
      // Facade above the shop (same brick as the building) and, on a flat roof, the roof over the notch.
      const fillH = top - floorY - storeTop;
      if (fillH > .2) {
        const geo = new T.PlaneGeometry(W, fillH), uv = geo.attributes.uv, vBase = (world.data.buildings[s.building].base + world.data.plinth) - floorY;
        for (let i = 0; i < uv.count; i++) uv.setXY(i, uv.getX(i) * W, storeTop - vBase + uv.getY(i) * fillH);
        const f = new T.Mesh(geo, facadeMat);f.rotation.y = Math.PI;f.position.set(W / 2, storeTop + fillH / 2, 0);f.castShadow = f.receiveShadow = true;g.add(f);
        if (!world.data.buildings[s.building].k) box(g, W, .06, D, W / 2, top - floorY + .03, D / 2, roofMat);
      }
      // Shop front: aluminium frame, glass, sliding doors, the brand sign and the opening hours on the glass.
      box(g, W, .12, .14, W / 2, .06, 0, aluM);box(g, W, .1, .14, W / 2, 2.9, 0, aluM);
      for (const x of [0, uDoor - .95, uDoor + .95, W]) box(g, .1, 2.95, .14, Math.min(W - .05, Math.max(.05, x)), 1.47, 0, aluM);
      for (const [a, b] of [[0, uDoor - .95], [uDoor + .95, W]]) {if (b - a < .2) continue;const p = new T.Mesh(new T.PlaneGeometry(b - a, 2.75), glassM);p.position.set((a + b) / 2, 1.5, 0);g.add(p);}
      const doors = [-1, 1].map(sd => {const d = new T.Group();d.position.set(uDoor + sd * .475, 0, .03);g.add(d);const pane = new T.Mesh(new T.PlaneGeometry(.95, 2.35), glassM);pane.position.y = 1.2;d.add(pane);box(d, .95, .06, .05, 0, 2.37, 0, aluM);box(d, .05, 2.35, .05, -sd * .45, 1.18, 0, aluM);return {g: d, sd};});
      const signTex = textTexture(1024, 160, (c, w, h) => {c.fillStyle = s.sign.bg;c.fillRect(0, 0, w, h);c.fillStyle = s.sign.fg;c.font = 'bold 118px Arial Black, Arial, sans-serif';c.textAlign = 'center';c.textBaseline = 'middle';c.fillText(s.brand, w / 2, h / 2 + 6);});
      const signMats = [darkM, darkM, darkM, darkM, darkM, new T.MeshStandardMaterial({map: signTex, emissive: 0xffffff, emissiveMap: signTex, emissiveIntensity: .35, roughness: .5})];
      const sign = new T.Mesh(new T.BoxGeometry(W, .62, .12), signMats);sign.position.set(W / 2, 3.3, -.06);sign.castShadow = true;g.add(sign);
      const hoursTex = textTexture(512, 256, (c, w, h) => {c.fillStyle = 'rgba(255,255,255,.92)';c.fillRect(0, 0, w, h);c.fillStyle = '#16191d';c.textAlign = 'center';c.font = 'bold 44px Arial, sans-serif';c.fillText(s.name, w / 2, 70);c.font = '30px Arial, sans-serif';const parts = s.hours.replace('Åpent ', '').split(' · ');c.fillText('Åpent', w / 2, 120);parts.forEach((p, i) => c.fillText(p, w / 2, 160 + i * 36));});
      const hours = new T.Mesh(new T.PlaneGeometry(.55, .28), new T.MeshBasicMaterial({map: hoursTex}));hours.rotation.y = Math.PI;hours.position.set(uDoor - 1.4, 1.45, -.012);g.add(hours);
      // Outside: a ramp up to the door if the shop floor is higher than the ground in front of it.
      // The slab's far end meets the ground where it ends (walk floors only join within 5 cm).
      let rl = 1.2, rise = 0;for (let it = 0; it < 3; it++) {const e = toW(uDoor, -rl);rise = floorY - world.heightWorld(e.x, e.z);rl = Math.max(1.2, Math.abs(rise) / .22);}
      {const e = toW(uDoor, -rl);rise = floorY - world.heightWorld(e.x, e.z);}
      if (rise > .06) {
        const pos = new Float32Array([uDoor - 1.1, 0, 0, uDoor + 1.1, 0, 0, uDoor + 1.1, -rise, -rl, uDoor - 1.1, -rise, -rl, uDoor - 1.1, -rise - .4, 0, uDoor + 1.1, -rise - .4, 0]);
        const rg = new T.BufferGeometry();rg.setAttribute('position', new T.BufferAttribute(pos, 3));rg.setIndex([0, 2, 1, 0, 3, 2, 0, 4, 3, 1, 2, 5]);rg.computeVertexNormals();
        const ramp = new T.Mesh(rg, lib.concrete(0x9c9a94));ramp.material.side = T.DoubleSide;ramp.receiveShadow = true;g.add(ramp);
      }
      {
        const o = toW(uDoor, 0), grad = rise / rl;floors.push({name: `${s.name} entrance`, points: [toW(uDoor - 1.1, 0), toW(uDoor + 1.1, 0), toW(uDoor + 1.1, -rl), toW(uDoor - 1.1, -rl)], y: floorY, slope: {x: vx * grad, z: vz * grad, origin: o}, surfaceKind: 'stone', ceilingHeight: Infinity});
      }
      floors.push({name: s.name, points: [toW(0, 0), toW(W, 0), toW(W, D), toW(0, D)], y: floorY, ceilingHeight: H, surfaceKind: 'tile'});
      // Front glass is solid apart from the door gap.
      obstacles.push(rect(0, uDoor - .95, -.08, .08, -.5, storeTop), rect(uDoor + .95, W, -.08, .08, -.5, storeTop));
      // Interior: fridges along the back wall, gondola shelves, the till, the bottle-return machine and firewood sacks.
      const spots = [], facings = [];
      const fridgeProducts = PRODUCTS.filter(p => p.where === 'fridge'), shelfProducts = PRODUCTS.filter(p => p.where === 'shelf');
      const fz0 = D - .85;box(g, W - .7, 2.1, .04, W / 2, 1.05, D - .12, fridgeM);box(g, W - .7, .16, .74, W / 2, 2.02, D - .47, fridgeM);box(g, W - .7, .22, .74, W / 2, .11, D - .47, fridgeM);
      for (const x of [.37, W - .37]) box(g, .04, 2.1, .74, x, 1.05, D - .47, fridgeM);for (let lv = 0; lv < 4; lv++) box(g, W - .78, .02, .62, W / 2, .27 + lv * .45, D - .47, shelfM);
      box(g, W - .7, .05, .05, W / 2, 1.92, fz0 + .04, fridgeLightM);
      const fg = new T.Mesh(new T.PlaneGeometry(W - .8, 1.85), glassM);fg.position.set(W / 2, 1.05, fz0 - .02);g.add(fg);
      obstacles.push(rect(.35, W - .35, fz0 - .05, D, -.2, 2.2));
      const segW = (W - .9) / fridgeProducts.length;
      fridgeProducts.forEach((p, i) => {const cx = .45 + segW * (i + .5);for (let lv = 0; lv < 4; lv++) for (let k = 0; k < Math.floor(segW / .13); k++) facings.push([p, cx - segW / 2 + .08 + k * .13, .28 + lv * .45, fz0 + .3, .1, .22, .1]);spots.push({p, x: cx, y: 1.0, z: fz0 - .1, w: segW - .05, h: 1.8, d: .3, tag: [cx, 2.0, fz0 - .03]});box(g, .03, 1.9, .05, .45 + segW * i, 1.05, fz0 - .03, aluM);});
      const nG = Math.max(2, Math.min(3, Math.floor((W - 1.6) / 2.6))), gz0 = 3.4, gz1 = D - 2.0, gl = gz1 - gz0;
      for (let i = 0; i < nG; i++) {
        const gx = 1.3 + i * (W - 2.8) / Math.max(1, nG - 1);
        box(g, .9, .1, gl, gx, .05, gz0 + gl / 2, shelfM);box(g, .05, 1.6, gl, gx, .85, gz0 + gl / 2, shelfM);
        for (const sd of [-1, 1]) {for (let lv = 0; lv < 4; lv++) box(g, .42, .03, gl, gx + sd * .24, .18 + lv * .42, gz0 + gl / 2, shelfM);}
        obstacles.push(rect(gx - .48, gx + .48, gz0, gz1, -.2, 1.7));
        [-1, 1].forEach((sd, k) => {
          const p = shelfProducts[(i * 2 + k) % shelfProducts.length], fx = gx + sd * .3;
          for (let lv = 0; lv < 4; lv++) for (let m = 0; m < Math.floor(gl / .16); m++) facings.push([p, fx, .195 + lv * .42, gz0 + .1 + m * .16, .16, .22, .13]);
          spots.push({p, x: gx + sd * .52, y: .9, z: gz0 + gl / 2, w: .25, h: 1.6, d: gl, tag: [gx + sd * .465, 1.42, gz0 + gl / 2], side: sd});
        });
      }
      // Firewood sacks by the left wall near the front.
      const ved = PRODUCTS.find(p => p.where === 'floor');
      for (let k = 0; k < 5; k++) {const sk = box(g, .42, .34, .3, .35 + (k % 2) * .05, .17 + Math.floor(k / 2) * .32, 2.2 + (k % 3) * .32, sackM);sk.rotation.y = k * .3;}
      spots.push({p: ved, x: .5, y: .5, z: 2.5, w: .8, h: 1.0, d: 1.2, tag: [.82, .9, 2.5], side: 1});obstacles.push(rect(.05, .8, 1.95, 3.05, -.2, 1.1));
      // Till: counter with belt, register and card terminal; the cashier stands between the counter and the window.
      box(g, 2.4, .9, .65, 2.4, .45, 1.32, lib.wood(0x6b4b33, .6));box(g, 1.6, .04, .45, 2.0, .92, 1.32, darkM);box(g, .4, .28, .34, 3.2, 1.06, 1.2, darkM);box(g, .14, .1, .2, 2.95, .98, 1.6, darkM);
      const screen = box(g, .3, .2, .02, 3.2, 1.3, 1.05, new T.MeshStandardMaterial({color: 0x0b1a10, emissive: 0x1c5a2a, emissiveIntensity: .9}));screen.rotation.x = -.2;
      obstacles.push(rect(1.18, 3.62, 1.0, 1.65, -.2, 1.0));
      const tillHit = new T.Mesh(new T.BoxGeometry(2.4, .6, .8), hitM);tillHit.position.set(2.4, 1.1, 1.4);tillHit.userData = {shopTill: s, noMerge: true};g.add(tillHit);hitboxes.push(tillHit);
      // Bottle-return machine ("pant") against the right wall.
      const mach = new T.Group();mach.position.set(W - .38, 0, 1.55);mach.rotation.y = -Math.PI / 2;g.add(mach);
      box(mach, .6, 1.75, .55, 0, .875, 0, lib.metal(0x9ea3a8, .4));const hole = new T.Mesh(new T.CircleGeometry(.09, 20), darkM);hole.position.set(0, 1.05, .28);mach.add(hole);
      const pantTex = textTexture(256, 128, (c, w, h) => {c.fillStyle = '#0f5a2a';c.fillRect(0, 0, w, h);c.fillStyle = '#fff';c.font = 'bold 64px Arial, sans-serif';c.textAlign = 'center';c.textBaseline = 'middle';c.fillText('PANT', w / 2, h / 2);});
      const pl = new T.Mesh(new T.PlaneGeometry(.45, .22), new T.MeshStandardMaterial({map: pantTex, emissive: 0xffffff, emissiveMap: pantTex, emissiveIntensity: .4}));pl.position.set(0, 1.45, .28);mach.add(pl);
      obstacles.push(rect(W - .7, W - .05, 1.25, 1.85, -.2, 1.8));
      const pantHit = new T.Mesh(new T.BoxGeometry(.7, 1.6, .7), hitM);pantHit.position.set(W - .38, 1.0, 1.55);pantHit.userData = {shopPant: s, noMerge: true};g.add(pantHit);hitboxes.push(pantHit);
      // Product facings (one instanced mesh) and price tags.
      const fGeo = new T.BoxGeometry(1, 1, 1), inst = new T.InstancedMesh(fGeo, new T.MeshStandardMaterial({roughness: .6}), facings.length), mtx = new T.Matrix4(), col = new T.Color();
      facings.forEach(([p, x, y, z, w, h, d], i) => {mtx.makeScale(w, h, d).setPosition(x, y + h / 2, z);inst.setMatrixAt(i, mtx);col.setHex(p.color).offsetHSL(0, 0, (Math.sin(i * 12.9898) * .5) * .08);inst.setColorAt(i, col);});
      inst.castShadow = true;inst.receiveShadow = true;g.add(inst);
      for (const sp of spots) {
        const price = priceOf(s, sp.p);
        const tex = textTexture(256, 96, (c, w, h) => {c.fillStyle = '#ffe14d';c.fillRect(0, 0, w, h);c.fillStyle = '#111';c.textAlign = 'center';c.font = 'bold 30px Arial, sans-serif';c.fillText(sp.p.name.length > 16 ? sp.p.name.slice(0, 15) + '…' : sp.p.name, w / 2, 36);c.font = 'bold 38px Arial, sans-serif';c.fillText(kr(price), w / 2, 82);});
        const tag = new T.Mesh(new T.PlaneGeometry(.24, .09), new T.MeshBasicMaterial({map: tex}));tag.position.set(...sp.tag);
        if (sp.side === -1) tag.rotation.y = -Math.PI / 2;else if (sp.side === 1) tag.rotation.y = Math.PI / 2;else tag.rotation.y = Math.PI;
        tag.userData.noMerge = true;g.add(tag);
        const hit = new T.Mesh(new T.BoxGeometry(sp.side ? sp.w : sp.w, sp.h, sp.side ? sp.d : sp.d), hitM);hit.position.set(sp.x, sp.y, sp.z);hit.userData = {shopItem: {shop: s, product: sp.p}, noMerge: true};g.add(hit);hitboxes.push(hit);
      }
      // The cashier.
      let cashier = null;
      if (avatars) {cashier = avatars.avatar(0, 'Kasserer', s.shirt);cashier.group.position.set(2.4, 0, .5);cashier.group.rotation.y = Math.PI;g.add(cashier.group);cashier.pose({stance: 'stand'});}
      [...doors.map(d => d.g), light].forEach(o => {o.userData.noMerge = true;});if (cashier) cashier.group.userData.noMerge = true;
      lib.mergeStatic(g, o => o.isInstancedMesh || !!o.userData.shopItem || !!o.userData.shopTill || !!o.userData.shopPant);
      built.push({...s, shop: s, g, floorY, W, D, uDoor, toW, doors, light, cashier, open: 0, ux, uz, origin, vx, vz, greeted: false});
      s.floorY = floorY;s.W = W;s.D = D;s.toLocal = (x, z) => {const dx = x - origin.x, dz = z - origin.z;return {a: dx * ux + dz * uz, b: dx * vx + dz * vz};};
      s.say = text => {if (cashier) cashier.say(text);};
    }
    // Which shop is the player standing in (on its floor)?
    function shopAt(p, feet) {for (const s of SHOPS) {if (!s.toLocal) continue;const l = s.toLocal(p.x, p.z);if (l.a > 0 && l.a < s.W && l.b > 0 && l.b < s.D && Math.abs(feet - s.floorY) < 1.4) return s;}return null;}
    const atTill = (s, p) => {const l = s.toLocal(p.x, p.z);return Math.hypot(l.a - 2.4, l.b - 2.25) < 1.3;};
    let tt = 0;
    function update(dt, player, camera) {
      tt += dt;
      for (const b of built) {
        const l = b.shop.toLocal(player.x, player.z), near = Math.hypot(l.a - b.W / 2, l.b - b.D / 2) < 30 && Math.abs(player.feet - b.floorY) < 6;
        b.light.intensity = near ? 26 : 0;
        // Sliding doors open when someone is within 2.5 m of them.
        const want = Math.hypot(l.a - b.uDoor, l.b) < 2.5 && Math.abs(player.feet - b.floorY) < 1.6 ? 1 : 0;
        if (want && !b.open) audio && audio.sfx('shopDoor');
        b.open += (want - b.open) * Math.min(1, dt * 5);for (const d of b.doors) d.g.position.x = b.uDoor + d.sd * (.475 + b.open * .9);
        if (b.cashier) {
          const inside = l.a > 0 && l.a < b.W && l.b > 0 && l.b < b.D, face = inside ? Math.atan2(-(l.a - 2.4), -(l.b - .5)) : Math.PI;
          let d = face - Math.PI;d = Math.atan2(Math.sin(d), Math.cos(d));const yaw = Math.PI + Math.max(-1.1, Math.min(1.1, d));
          b.cashier.group.rotation.y += (yaw - b.cashier.group.rotation.y) * Math.min(1, dt * 3);b.cashier.pose({stance: 'stand', speed: 0, dt});b.cashier.update(dt, camera);
        }
      }
    }
    return {group, shops: SHOPS.filter(s => s.local), floors, obstacles, hitboxes, wallet: makeWallet(), priceOf, beerAllowed, shopAt, atTill, update, kr, PANT, products: PRODUCTS};
  }
  return {SHOPS, PRODUCTS, PANT, applyNotches, notchFor, create, beerAllowed, priceOf, makeWallet};
});
