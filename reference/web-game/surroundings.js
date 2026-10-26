/* The neighbourhood of Haarklous plass 17A for the open-world game, built from surroundings-data.js
 * (OpenStreetMap ODbL, Kartverket DTM CC BY 4.0). Terrain from real height data; buildings stand on it; trees,
 * bushes, courtyard dressing, street lamps; sky and the hills around Oslo. Ground texture: an offline map
 * painting; when online and allowed, Esri World Imagery is fetched live and painted onto the terrain.
 * Surroundings.create({T, lib, data, renderer, onStatus}) -> {
 *   near, far, setImagery(on), setSun(dir), attribution,
 *   heightWorld(x, z) -> y, toLocal(x, z) -> {x, s}, toWorld(x, s) -> {x, z},
 *   terrainFloor() (walk-collision floor), obstacles() (buildings, trees, bushes, hedges, in model coordinates),
 *   surfaceAt(x, z) -> 'grass'|'asphalt'|'gravel'|'sand'|'none', trafficDistance(x, z), greenery(x, z) -> 0..1 }
 * Coordinates: model (world) metres as in the apartment model; "local" = metres east (x) and south (s) of the address.
 */
(function (root, factory) {
  root.Surroundings = factory();
})(typeof window === 'undefined' ? globalThis : window, function () {
  'use strict';
  const NEAR_LIMIT = 280, PLAY_HALF = 540;
  function create({T, lib, data, renderer, onStatus = () => {}}) {
    const near = new T.Group(), far = new T.Group();near.name = 'Surroundings (near)';far.name = 'Surroundings (far)';
    const ground = data.ground, theta = data.geo.theta, cT = Math.cos(theta), sT = Math.sin(theta);
    const geoFrame = parent => {const g = new T.Group();g.rotation.y = theta;g.position.set(data.geo.x, 0, data.geo.z);parent.add(g);return g;};
    const geoNear = geoFrame(near), geoFar = geoFrame(far);
    const toWorld = (x, s) => ({x: x * cT + s * sT + data.geo.x, z: -x * sT + s * cT + data.geo.z});
    const toLocal = (x, z) => {const dx = x - data.geo.x, dz = z - data.geo.z;return {x: dx * cT - dz * sT, s: dx * sT + dz * cT};};
    const pairs = flat => {const out = [];for (let i = 0; i < flat.length; i += 2) out.push([flat[i], flat[i + 1]]);return out;};
    const signedArea = ring => ring.reduce((s, p, i) => {const q = ring[(i + 1) % ring.length];return s + p[0] * q[1] - q[0] * p[1];}, 0) / 2;

    // ---------- Terrain (relative heights in decimetres, 4 m grid, local frame) ----------
    const ter = data.terrain, N = ter.n, half = ter.half, step = ter.step;
    const grid = (() => {const bin = atob(ter.dm), bytes = new Uint8Array(bin.length);for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);const raw = new Int16Array(bytes.buffer), f = new Float32Array(raw.length);for (let i = 0; i < raw.length; i++) f[i] = raw[i] / 10;return f;})();
    function relLocal(x, s) {
      const fi = Math.min(N - 1.001, Math.max(0, (x + half) / step)), fj = Math.min(N - 1.001, Math.max(0, (s + half) / step)), i = Math.floor(fi), j = Math.floor(fj), u = fi - i, v = fj - j;
      return grid[j * N + i] * (1 - u) * (1 - v) + grid[j * N + i + 1] * u * (1 - v) + grid[(j + 1) * N + i] * (1 - u) * v + grid[(j + 1) * N + i + 1] * u * v;
    }
    const yLocal = (x, s) => ground + relLocal(x, s);
    const heightWorld = (x, z) => {const l = toLocal(x, z);return yLocal(l.x, l.s);};
    function terrainMesh(parent, extent, res, canvas, name, drop = 0) {
      const count = Math.round(2 * extent / res) + 1, pos = new Float32Array(count * count * 3), uv = new Float32Array(count * count * 2), index = [];
      for (let j = 0; j < count; j++) for (let i = 0; i < count; i++) {
        const x = -extent + i * res, s = -extent + j * res, k = j * count + i;
        pos[k * 3] = x;pos[k * 3 + 1] = yLocal(x, s) - drop;pos[k * 3 + 2] = s;uv[k * 2] = (x + extent) / (2 * extent);uv[k * 2 + 1] = 1 - (s + extent) / (2 * extent);
      }
      for (let j = 0; j < count - 1; j++) for (let i = 0; i < count - 1; i++) {const a = j * count + i, b = a + 1, c = a + count, d = c + 1;index.push(a, c, b, b, c, d);}
      const g = new T.BufferGeometry();g.setAttribute('position', new T.BufferAttribute(pos, 3));g.setAttribute('uv', new T.BufferAttribute(uv, 2));g.setIndex(index);g.computeVertexNormals();
      const tex = new T.CanvasTexture(canvas);tex.colorSpace = T.SRGBColorSpace;tex.anisotropy = 8;
      const m = new T.Mesh(g, new T.MeshStandardMaterial({map: tex, color: 0xa6a6a6, roughness: .97, metalness: 0, envMapIntensity: .35}));
      m.receiveShadow = true;m.name = name;parent.add(m);return {mesh: m, tex};
    }

    // ---------- Map painting (offline ground texture) and surface classes ----------
    const colors = {grass: '#5d7a3a', wood: '#3f5a2c', playground: '#a8946a', pitch: '#4d7944', parking: '#6c6d6f', yard: '#9a958a', flowerbed: '#5e5136', water: '#4a6a80'};
    const roadOrder = ['footway', 'path', 'cycleway', 'steps', 'pedestrian', 'service', 'track', 'living_street', 'unclassified', 'residential', 'tertiary', 'secondary', 'primary', 'trunk_link', 'trunk', 'tram'];
    const isPath = k => ['footway', 'path', 'steps', 'cycleway', 'pedestrian', 'track'].includes(k);
    function paint(size, ext, classes = false) {
      const c = document.createElement('canvas');c.width = c.height = size;const ctx = c.getContext('2d'), s = size / (2 * ext), P = (x, y) => [(x + ext) * s, (y + ext) * s];
      const col = (kind, normal) => classes ? ({grass: '#010000', asphalt: '#020000', gravel: '#030000', sand: '#040000', none: '#050000'})[kind] : normal;
      ctx.fillStyle = col('grass', '#62783f');ctx.fillRect(0, 0, size, size);
      const ring = (flat, color) => {ctx.fillStyle = color;ctx.beginPath();pairs(flat).forEach((p, i) => {const [x, y] = P(p[0], p[1]);i ? ctx.lineTo(x, y) : ctx.moveTo(x, y);});ctx.closePath();ctx.fill();};
      for (const a of data.areas) if (a.k === 'grass' || a.k === 'wood') ring(a.o, col('grass', colors[a.k]));
      for (const a of data.areas) if (a.k !== 'grass' && a.k !== 'wood') ring(a.o, col(a.k === 'parking' ? 'asphalt' : a.k === 'playground' ? 'sand' : a.k === 'yard' ? 'gravel' : 'grass', colors[a.k] || '#888'));
      for (const r of data.roads.slice().sort((a, b) => roadOrder.indexOf(a.k) - roadOrder.indexOf(b.k))) {
        ctx.strokeStyle = col(isPath(r.k) ? 'gravel' : 'asphalt', r.k === 'tram' ? '#5a5650' : isPath(r.k) ? '#a7a296' : '#4d4f52');ctx.lineWidth = r.w * s;ctx.lineCap = ctx.lineJoin = 'round';
        ctx.beginPath();pairs(r.p).forEach((p, i) => {const [x, y] = P(p[0], p[1]);i ? ctx.lineTo(x, y) : ctx.moveTo(x, y);});ctx.stroke();
      }
      const yard = data.courtyard;
      if (yard) {ctx.lineCap = 'round';ctx.strokeStyle = col('gravel', '#b4a78f');ctx.lineWidth = yard.pathWidth * s;ctx.beginPath();pairs(yard.path).forEach((p, i) => {const [x, y] = P(p[0], p[1]);i ? ctx.lineTo(x, y) : ctx.moveTo(x, y);});ctx.stroke();if (yard.playground) ring(yard.playground.o, col('sand', '#c9b48a'));}
      if (data.petanque) {const p = data.petanque, cx = Math.cos(p.rot), sx = Math.sin(p.rot);ring([[-1, -1], [1, -1], [1, 1], [-1, 1]].flatMap(([a, b]) => [p.x + cx * a * (p.length / 2 + .3) - sx * b * (p.width / 2 + .3), p.s + sx * a * (p.length / 2 + .3) + cx * b * (p.width / 2 + .3)]), col('gravel', '#c2b49a'));}
      for (const h of data.discgolf || []) {const [tx, ts] = h.tee;ctx.fillStyle = col('gravel', '#59544c');ctx.beginPath();const [px, py] = P(tx, ts);ctx.arc(px, py, 1.4 * s, 0, 7);ctx.fill();}
      for (const b of data.buildings) ring(b.r.o, col('none', '#5a5652'));
      if (!classes) {const img = ctx.getImageData(0, 0, size, size), d = img.data;for (let i = 0; i < d.length; i += 4) {const n = (lib.rand() - .5) * 26;d[i] += n;d[i + 1] += n;d[i + 2] += n;}ctx.putImageData(img, 0, 0);}
      return c;
    }
    const NEAR_EXT = 300, FAR_EXT = 600;
    const nearPaint = paint(4096, NEAR_EXT), nearBase = document.createElement('canvas');nearBase.width = nearBase.height = 4096;nearBase.getContext('2d').drawImage(nearPaint, 0, 0);
    const farPaint = paint(2048, FAR_EXT), farBase = document.createElement('canvas');farBase.width = farBase.height = 2048;farBase.getContext('2d').drawImage(farPaint, 0, 0);
    const classCanvas = paint(1024, PLAY_HALF, true), classData = classCanvas.getContext('2d').getImageData(0, 0, 1024, 1024).data;
    const surfaceNames = ['grass', 'grass', 'asphalt', 'gravel', 'sand', 'none'];
    function surfaceAt(x, z) {const l = toLocal(x, z), i = Math.floor((l.x + PLAY_HALF) / (2 * PLAY_HALF) * 1024), j = Math.floor((l.s + PLAY_HALF) / (2 * PLAY_HALF) * 1024);if (i < 0 || j < 0 || i > 1023 || j > 1023) return 'grass';return surfaceNames[classData[(j * 1024 + i) * 4]] || 'grass';}
    const nearTerrain = terrainMesh(geoNear, NEAR_EXT, 4, nearPaint, 'Terrain (near)');
    const farTerrain = terrainMesh(geoFar, FAR_EXT, 8, farPaint, 'Terrain (district)', .5);
    // Beyond the mapped district: a generic Oslo townscape tile (blocks of roofs, streets, gardens and parks), repeated about every
    // 1.5 km, so the land seen from the aircraft is not one flat colour. The inner Oslofjord lies about 3 km to the south.
    const horizonCanvas = document.createElement('canvas');horizonCanvas.width = horizonCanvas.height = 512;
    {
      const c = horizonCanvas.getContext('2d');c.fillStyle = '#7f8574';c.fillRect(0, 0, 512, 512);
      const roofs = ['#8d8c88', '#9b6c57', '#6f7071', '#aeaca4', '#7b5d4e', '#848a7a', '#a39b8c'];
      for (let k = 0; k < 22; k++) {const x = lib.rand() * 512, y = lib.rand() * 512, r = 18 + lib.rand() * 45;c.fillStyle = lib.rand() < .7 ? '#5f7c4b' : '#4f6a40';c.beginPath();c.ellipse(x, y, r, r * (.5 + lib.rand() * .6), lib.rand() * 3, 0, 7);c.fill();}
      for (let gy = 0; gy < 512; gy += 26) for (let gx = 0; gx < 512; gx += 30) {
        if (lib.rand() < .16) continue;const w = 8 + lib.rand() * 16, h = 6 + lib.rand() * 14;
        c.fillStyle = roofs[Math.floor(lib.rand() * roofs.length)];c.fillRect(gx + 3 + lib.rand() * (24 - w), gy + 3 + lib.rand() * (20 - h), w, h);
        if (lib.rand() < .5) {c.fillStyle = '#6b8456';c.fillRect(gx + 2 + lib.rand() * 20, gy + 2 + lib.rand() * 16, 6, 5);}
      }
      c.strokeStyle = '#5d5f60';c.lineWidth = 2.2;for (let k = 0; k < 512; k += 30) {c.beginPath();c.moveTo(k, 0);c.lineTo(k, 512);c.stroke();}for (let k = 0; k < 512; k += 26) {c.beginPath();c.moveTo(0, k);c.lineTo(512, k);c.stroke();}
      const img = c.getImageData(0, 0, 512, 512), d = img.data;for (let i = 0; i < d.length; i += 4) {const n = (lib.rand() - .5) * 18;d[i] += n;d[i + 1] += n;d[i + 2] += n;}c.putImageData(img, 0, 0);
    }
    const horizonTex = new T.CanvasTexture(horizonCanvas);horizonTex.colorSpace = T.SRGBColorSpace;horizonTex.wrapS = horizonTex.wrapT = T.RepeatWrapping;horizonTex.repeat.set(12, 12);horizonTex.anisotropy = 8;
    const horizon = new T.Mesh(new T.PlaneGeometry(18000, 18000), new T.MeshStandardMaterial({map: horizonTex, color: 0xb4b4b4, roughness: 1}));
    horizon.rotation.x = -Math.PI / 2;horizon.position.y = ground + relLocal(600, 600) - 6;geoFar.add(horizon);
    const fjord = new T.Mesh(new T.PlaneGeometry(7000, 6000), new T.MeshStandardMaterial({color: 0x3e5d70, roughness: .18, metalness: .15, polygonOffset: true, polygonOffsetFactor: -2, polygonOffsetUnits: -2}));
    fjord.rotation.x = -Math.PI / 2;fjord.position.set(-600, horizon.position.y + 2, 6200);geoFar.add(fjord);

    // ---------- Buildings ----------
    function builder() {return {pos: [], nor: [], uv: [], col: [], index: []};}
    function finish(b) {
      if (!b.pos.length) return null;
      const g = new T.BufferGeometry();g.setAttribute('position', new T.Float32BufferAttribute(b.pos, 3));g.setAttribute('normal', new T.Float32BufferAttribute(b.nor, 3));
      g.setAttribute('uv', new T.Float32BufferAttribute(b.uv, 2));if (b.col.length) g.setAttribute('color', new T.Float32BufferAttribute(b.col, 3));g.setIndex(b.index);g.computeBoundingSphere();return g;
    }
    function quad(b, a, c, y0, y1, u0, u1, v0, v1, color) {
      const dx = c[0] - a[0], dz = c[1] - a[1], len = Math.hypot(dx, dz) || 1, nx = -dz / len, nz = dx / len, base = b.pos.length / 3;
      b.pos.push(a[0], y0, a[1], c[0], y0, c[1], c[0], y1, c[1], a[0], y1, a[1]);
      for (let i = 0; i < 4; i++) b.nor.push(nx, 0, nz);
      b.uv.push(u0, v0, u1, v0, u1, v1, u0, v1);if (color) for (let i = 0; i < 4; i++) b.col.push(color.r, color.g, color.b);
      b.index.push(base, base + 1, base + 2, base, base + 2, base + 3);
    }
    function cap(b, ring, holes, y, color) {
      const contour = ring.map(p => new T.Vector2(p[0], p[1])), hs = (holes || []).map(h => h.map(p => new T.Vector2(p[0], p[1])));
      const tris = T.ShapeUtils.triangulateShape(contour, hs), all = contour.concat(...hs), base = b.pos.length / 3;
      for (const p of all) {b.pos.push(p.x, y, p.y);b.nor.push(0, 1, 0);b.uv.push(p.x, p.y);if (color) b.col.push(color.r, color.g, color.b);}
      for (const t of tris) b.index.push(base + t[0], base + t[2], base + t[1]);
    }
    function walls(b, ring, y0, y1, vBase, color) {
      const r = signedArea(ring) > 0 ? ring.slice().reverse() : ring;let u = 0;
      for (let i = 0; i < r.length; i++) {const a = r[i], c = r[(i + 1) % r.length], len = Math.hypot(c[0] - a[0], c[1] - a[1]);if (len < .02) continue;quad(b, a, c, y0, y1, u, u + len, y0 - vBase, y1 - vBase, color);u += len;}
    }
    function hipRoof(b, corners, eave) {
      let p = corners.map(c => new T.Vector2(c[0], c[1]));
      const centre = p.reduce((s, v) => s.add(v), new T.Vector2()).multiplyScalar(.25);
      p = p.map(v => v.clone().add(v.clone().sub(centre).normalize().multiplyScalar(.35 * Math.SQRT2)));
      let [a, bb, c, d] = p;if (a.distanceTo(bb) < bb.distanceTo(c)) [a, bb, c, d] = [bb, c, d, a];
      const width = bb.distanceTo(c), rise = width / 2 * Math.tan(27 * Math.PI / 180), axis = bb.clone().sub(a).normalize();
      const r1 = a.clone().add(d).multiplyScalar(.5).addScaledVector(axis, width / 2), r2 = bb.clone().add(c).multiplyScalar(.5).addScaledVector(axis, -width / 2), top = eave + rise;
      for (const f of [[a, bb, r2, r1], [c, d, r1, r2], [d, a, r1], [bb, c, r2]]) {
        const pts = f.map((v, i) => new T.Vector3(v.x, i < 2 ? eave : top, v.y)), n = new T.Vector3().subVectors(pts[1], pts[0]).cross(new T.Vector3().subVectors(pts[pts.length - 1], pts[0])).normalize();
        if (n.y < 0) {pts.reverse();n.negate();}
        const base = b.pos.length / 3;for (const v of pts) {b.pos.push(v.x, v.y, v.z);b.nor.push(n.x, n.y, n.z);b.uv.push(v.x, v.z);}
        b.index.push(base, base + 1, base + 2);if (pts.length === 4) b.index.push(base, base + 2, base + 3);
      }
    }
    const sets = {near: {facade: builder(), plinth: builder(), plaster: builder(), roof: builder(), shed: builder()}, far: {facade: builder(), plinth: builder(), plaster: builder(), roof: builder(), shed: builder()}};
    const plasterColors = [0xe3dccb, 0xd8cfbd, 0xcdd2cb, 0xe6e0d4, 0xd9c7a8];
    data.buildings.forEach((b, i) => {
      const ring = pairs(b.r.o), cx = ring.reduce((s, p) => s + p[0], 0) / ring.length, cs = ring.reduce((s, p) => s + p[1], 0) / ring.length;
      const set = Math.hypot(cx, cs) < NEAR_LIMIT ? sets.near : sets.far, g0 = ground + (b.g || 0), bottom = ground + (b.gm ?? b.g ?? 0) - .6, top = g0 + b.h;
      b.base = g0;b.top = top;b.bottom = bottom;
      if (b.s === 'brick') {
        walls(set.plinth, ring, bottom, Math.min(g0 + data.plinth, top), g0);
        if (top > g0 + data.plinth) walls(set.facade, ring, g0 + data.plinth, top, g0 + data.plinth);
        if (b.k) hipRoof(set.roof, pairs(b.k), top);else cap(set.roof, ring, null, top);
      } else {
        const color = new T.Color(b.s === 'shed' ? 0x8d7f6d : b.c || plasterColors[i % plasterColors.length]);
        walls(b.s === 'shed' ? set.shed : set.plaster, ring, bottom, top, g0, color);cap(set.roof, ring, null, top);
      }
    });
    const own = data.own, ownSet = {facade: builder(), plinth: builder(), roof: builder()}, plinthTop = ground + data.plinth;
    const ownBottom = ground + Math.min(...pairs(data.ownLocal).map(p => relLocal(p[0], p[1]))) - .6;
    for (const part of own.lower) {walls(ownSet.plinth, pairs(part.o), ownBottom, plinthTop, ground);walls(ownSet.facade, pairs(part.o), plinthTop, -.16, plinthTop);}
    for (const part of own.band) walls(ownSet.facade, pairs(part.o), -.16, own.bandTop, plinthTop);
    for (const part of own.upper) {walls(ownSet.facade, pairs(part.o), own.bandTop, own.top, plinthTop);cap(ownSet.roof, pairs(part.o), part.h.map(pairs), own.top);}
    for (const r of own.roof) hipRoof(ownSet.roof, pairs(r), own.top);
    const facadeMat = lib.facade(), plinthMat = lib.plinth(), roofMat = lib.roof(), plasterMat = lib.plaster(0xd9d2c3), shedMat = lib.wood(0x7a6a58, .8);
    plasterMat.vertexColors = shedMat.vertexColors = true;plasterMat.color.setScalar(.85);shedMat.color.set(0xffffff);
    roofMat.side = T.DoubleSide;facadeMat.color.setScalar(.78);plinthMat.color.setScalar(.75);
    const add = (parent, geometry, material, shadows = true) => {if (!geometry) return null;const m = new T.Mesh(geometry, material);m.castShadow = m.receiveShadow = shadows;parent.add(m);return m;};
    for (const [name, set] of Object.entries(sets)) {
      const parent = name === 'near' ? geoNear : geoFar, shadows = name === 'near';
      add(parent, finish(set.facade), facadeMat, shadows);add(parent, finish(set.plinth), plinthMat, shadows);add(parent, finish(set.roof), roofMat, shadows);
      add(parent, finish(set.plaster), plasterMat, shadows);add(parent, finish(set.shed), shedMat, shadows);
    }
    const ownGroup = new T.Group();ownGroup.name = 'Haarklous plass 17 (own building)';near.add(ownGroup);
    add(ownGroup, finish(ownSet.facade), facadeMat);add(ownGroup, finish(ownSet.plinth), plinthMat);add(ownGroup, finish(ownSet.roof), roofMat);

    // ---------- Aerial imagery painted onto the terrain textures ----------
    const KX = 111320 * Math.cos(data.origin.lat * Math.PI / 180);
    const tileUrl = (z, x, y) => `https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/${z}/${y}/${x}`;
    const lonToX = (lon, z) => (lon + 180) / 360 * 2 ** z, latToY = (lat, z) => {const r = lat * Math.PI / 180;return (1 - Math.log(Math.tan(r) + 1 / Math.cos(r)) / Math.PI) / 2 * 2 ** z;};
    const xToLon = (x, z) => x / 2 ** z * 360 - 180, yToLat = (y, z) => {const n = Math.PI - 2 * Math.PI * y / 2 ** z;return 180 / Math.PI * Math.atan(Math.sinh(n));};
    let imageryToken = 0, imageryState = 'off';
    function loadTiles(zoom, ext, token) {
      const lat = data.origin.lat, lon = data.origin.lon, dLon = ext / KX, dLat = ext / 111320;
      const x0 = Math.floor(lonToX(lon - dLon, zoom)), x1 = Math.floor(lonToX(lon + dLon, zoom)), y0 = Math.floor(latToY(lat + dLat, zoom)), y1 = Math.floor(latToY(lat - dLat, zoom));
      const c = document.createElement('canvas');c.width = (x1 - x0 + 1) * 256;c.height = (y1 - y0 + 1) * 256;const ctx = c.getContext('2d');
      const bounds = {west: (xToLon(x0, zoom) - lon) * KX, east: (xToLon(x1 + 1, zoom) - lon) * KX, north: (yToLat(y0, zoom) - lat) * 111320, south: (yToLat(y1 + 1, zoom) - lat) * 111320};
      let pending = (x1 - x0 + 1) * (y1 - y0 + 1), loaded = 0, failed = 0;
      return new Promise(resolve => {
        for (let i = x0; i <= x1; i++) for (let j = y0; j <= y1; j++) {
          const img = new Image();img.crossOrigin = 'anonymous';img.referrerPolicy = 'no-referrer';
          const done = ok => {if (ok && token === imageryToken) {ctx.drawImage(img, (i - x0) * 256, (j - y0) * 256);loaded++;} else failed++;if (--pending === 0) resolve({canvas: c, bounds, loaded, failed});};
          img.onload = () => done(true);img.onerror = () => done(false);img.src = tileUrl(zoom, i, j);
        }
      });
    }
    // Draw an imagery mosaic into a terrain canvas covering [-ext, ext] in local metres.
    function drawInto(target, ext, mosaic, lift) {
      const ctx = target.getContext('2d'), k = target.width / (2 * ext), b = mosaic.bounds;
      const tmp = document.createElement('canvas');tmp.width = mosaic.canvas.width;tmp.height = mosaic.canvas.height;const t = tmp.getContext('2d');t.drawImage(mosaic.canvas, 0, 0);
      if (lift) {const img = t.getImageData(0, 0, tmp.width, tmp.height), d = img.data, lut = new Uint8ClampedArray(256);for (let i = 0; i < 256; i++) lut[i] = 255 * Math.pow(i / 255, .58);for (let i = 0; i < d.length; i += 4) {d[i] = lut[d[i]];d[i + 1] = lut[d[i + 1]];d[i + 2] = lut[d[i + 2]];}t.putImageData(img, 0, 0);}
      ctx.drawImage(tmp, (b.west + ext) * k, (-b.north + ext) * k, (b.east - b.west) * k, (b.north - b.south) * k);
    }
    // Oblique photo: roofs lean onto the ground beside each building; cover those zones with the map painting.
    function maskLean(target, base, ext) {
      const size = target.width, k = size / (2 * ext), P = (x, s) => [(x + ext) * k, (s + ext) * k], lean = data.imageryLean || [0, 0];
      const mask = document.createElement('canvas');mask.width = mask.height = size;const m = mask.getContext('2d');m.fillStyle = '#000';
      const poly = pts => {m.beginPath();pts.forEach((p, i) => {const [u, v] = P(p[0], p[1]);i ? m.lineTo(u, v) : m.moveTo(u, v);});m.closePath();m.fill();};
      const sweep = (flat, h) => {const ring = pairs(flat), dx = lean[0] * h, dz = lean[1] * h;poly(ring);poly(ring.map(p => [p[0] + dx, p[1] + dz]));ring.forEach((a, i) => {const b = ring[(i + 1) % ring.length];poly([a, b, [b[0] + dx, b[1] + dz], [a[0] + dx, a[1] + dz]]);});};
      for (const b of data.buildings) {const o = b.r.o;if (Math.abs(o[0]) < ext + 60 && Math.abs(o[1]) < ext + 60) sweep(o, b.h + 1.5);}
      sweep(data.ownLocal, own.top - ground + 1.5);
      const fill = document.createElement('canvas');fill.width = fill.height = size;const f = fill.getContext('2d');f.drawImage(base, 0, 0);f.globalCompositeOperation = 'destination-in';f.filter = 'blur(2px)';f.drawImage(mask, 0, 0);
      target.getContext('2d').drawImage(fill, 0, 0);
    }
    // Deep shadows in the photo carry no ground detail: blend them toward the map painting (kept slightly shaded).
    function fillShadows(target, base) {
      const w = target.width, h = target.height, tc = target.getContext('2d'), img = tc.getImageData(0, 0, w, h), d = img.data, b = base.getContext('2d').getImageData(0, 0, w, h).data;
      for (let i = 0; i < d.length; i += 4) {
        const lum = .3 * d[i] + .59 * d[i + 1] + .11 * d[i + 2];if (lum >= 70) continue;
        const t = Math.min(1, (70 - lum) / 45) * .85;d[i] += (b[i] * .8 - d[i]) * t;d[i + 1] += (b[i + 1] * .8 - d[i + 1]) * t;d[i + 2] += (b[i + 2] * .8 - d[i + 2]) * t;
      }
      tc.putImageData(img, 0, 0);
    }
    function restore(target, base) {const c = target.getContext('2d');c.drawImage(base, 0, 0);}
    async function setImagery(on) {
      const token = ++imageryToken;
      if (!on) {restore(nearPaint, nearBase);restore(farPaint, farBase);nearTerrain.tex.needsUpdate = farTerrain.tex.needsUpdate = true;imageryState = 'off';onStatus('Map-based ground (aerial imagery off).');return;}
      if (typeof navigator !== 'undefined' && navigator.onLine === false) {imageryState = 'offline';onStatus('Offline: map-based ground. Aerial imagery loads when online.');return;}
      imageryState = 'loading';onStatus('Loading aerial imagery…');
      const [nearTiles, farTiles, horizonTiles] = await Promise.all([loadTiles(18, NEAR_EXT, token), loadTiles(15, FAR_EXT, token), loadTiles(12, 9000, token)]);
      if (token !== imageryToken) return;
      if (nearTiles.loaded <= nearTiles.failed) {imageryState = 'failed';onStatus('Aerial imagery could not load; map-based ground.');return;}
      drawInto(nearPaint, NEAR_EXT, nearTiles, true);fillShadows(nearPaint, nearBase);maskLean(nearPaint, nearBase, NEAR_EXT);
      drawInto(farPaint, FAR_EXT, farTiles, true);fillShadows(farPaint, farBase);
      drawInto(horizonCanvas, 9000, horizonTiles, true);
      nearTerrain.tex.needsUpdate = farTerrain.tex.needsUpdate = horizonTex.needsUpdate = true;imageryState = 'on';onStatus('Aerial imagery: Esri World Imagery (loaded live, not stored).');
    }

    // ---------- Trees and bushes ----------
    function leafCardTexture(tones) {
      const c = document.createElement('canvas');c.width = c.height = 256;const x = c.getContext('2d');
      x.strokeStyle = 'rgba(70,55,40,.9)';x.lineWidth = 2;x.beginPath();x.moveTo(128, 250);x.lineTo(128, 60);for (let k = 0; k < 6; k++) {x.moveTo(128, 220 - k * 28);x.lineTo(128 + (k % 2 ? 60 : -60), 170 - k * 28);}x.stroke();
      for (let k = 0; k < 170; k++) {const a = lib.rand() * Math.PI * 2, r = Math.sqrt(lib.rand()) * 110, px = 128 + Math.cos(a) * r, py = 120 + Math.sin(a) * r * .9;x.fillStyle = tones[Math.floor(lib.rand() * tones.length)];x.save();x.translate(px, py);x.rotate(lib.rand() * Math.PI);x.beginPath();x.ellipse(0, 0, 7 + lib.rand() * 6, 4 + lib.rand() * 3, 0, 0, Math.PI * 2);x.fill();x.restore();}
      const t = new T.CanvasTexture(c);t.colorSpace = T.SRGBColorSpace;t.anisotropy = 4;return t;
    }
    function crownGeometry(cards, lift = .55) {
      const pos = [], nor = [], uv = [], idx = [], q = new T.Quaternion(), e = new T.Euler();
      for (let k = 0; k < cards; k++) {
        const dir = new T.Vector3(lib.rand() * 2 - 1, lib.rand() * 1.6 - .5, lib.rand() * 2 - 1).normalize(), centre = dir.clone().multiplyScalar(.35 + lib.rand() * .55);centre.y += lift;
        e.set(lib.rand() * Math.PI, lib.rand() * Math.PI, lib.rand() * Math.PI);q.setFromEuler(e);const size = .55 + lib.rand() * .35, base = pos.length / 3;
        for (const [u, v] of [[-1, -1], [1, -1], [1, 1], [-1, 1]]) {const p = new T.Vector3(u * size / 2, v * size / 2, 0).applyQuaternion(q).add(centre);pos.push(p.x, p.y, p.z);const n = p.clone().sub(new T.Vector3(0, lift, 0)).normalize();nor.push(n.x, n.y, n.z);uv.push((u + 1) / 2, (v + 1) / 2);}
        idx.push(base, base + 1, base + 2, base, base + 2, base + 3);
      }
      const g = new T.BufferGeometry();g.setAttribute('position', new T.Float32BufferAttribute(pos, 3));g.setAttribute('normal', new T.Float32BufferAttribute(nor, 3));g.setAttribute('uv', new T.Float32BufferAttribute(uv, 2));g.setIndex(idx);return g;
    }
    const leafMat = tones => new T.MeshStandardMaterial({map: leafCardTexture(tones), alphaTest: .45, side: T.DoubleSide, roughness: .8, metalness: 0});
    const leafMats = [leafMat(['#3f5f2a', '#4f7232', '#5d8238', '#36522a', '#6a8c3e']), leafMat(['#46622c', '#557a35', '#3a5526', '#678a3b', '#4b6a2f'])], bushMat = leafMat(['#34502a', '#40602c', '#2e4724', '#4b6b31']), barkMat = lib.bark();
    const crownGeo = [crownGeometry(70), crownGeometry(70)], bushGeo = crownGeometry(40, .35), trunkGeo = new T.CylinderGeometry(.11, .17, 1, 8);trunkGeo.translate(0, .5, 0);
    function plantTrees(parent, list, castShadow) {
      if (!list.length) return;
      const trunks = new T.InstancedMesh(trunkGeo, barkMat, list.length), crowns = crownGeo.map((g, i) => new T.InstancedMesh(g, leafMats[i], list.length));
      const counts = [0, 0], m = new T.Matrix4(), q = new T.Quaternion(), s = new T.Vector3(), p = new T.Vector3();
      list.forEach((t, i) => {
        const h = t.h || 9 + ((i * 7919) % 70) / 10, crown = h * .62, trunkH = h * .42, r = h * .3, y = yLocal(t.x, t.s);
        q.setFromAxisAngle(new T.Vector3(0, 1, 0), (i * 2.399) % 6.283);
        m.compose(p.set(t.x, y, t.s), q, s.set(1 + h / 25, trunkH, 1 + h / 25));trunks.setMatrixAt(i, m);
        const k = i % 2;m.compose(p.set(t.x, y + trunkH * .95, t.s), q, s.set(r * .95, crown * .5, r * .95));crowns[k].setMatrixAt(counts[k]++, m);
      });
      crowns.forEach((c, k) => {c.count = counts[k];c.castShadow = castShadow;c.receiveShadow = true;c.instanceMatrix.needsUpdate = true;parent.add(c);});
      trunks.castShadow = castShadow;trunks.receiveShadow = true;trunks.instanceMatrix.needsUpdate = true;parent.add(trunks);
    }
    const trees = {near: [], far: []}, allTrees = [];
    for (let i = 0; i < data.trees.length; i += 3) {const t = {x: data.trees[i], s: data.trees[i + 1], h: data.trees[i + 2]};allTrees.push(t);(Math.hypot(t.x, t.s) < NEAR_LIMIT ? trees.near : trees.far).push(t);}
    const yard = data.courtyard;
    if (yard) for (let i = 0; i < yard.trees.length; i += 3) {const t = {x: yard.trees[i], s: yard.trees[i + 1], h: yard.trees[i + 2]};allTrees.push(t);trees.near.push(t);}
    plantTrees(geoNear, trees.near, true);plantTrees(geoFar, trees.far, false);
    const bushes = [];for (let i = 0; i < data.bushes.length; i += 3) bushes.push({x: data.bushes[i], s: data.bushes[i + 1], r: data.bushes[i + 2]});
    {
      const inst = new T.InstancedMesh(bushGeo, bushMat, bushes.length), m = new T.Matrix4(), q = new T.Quaternion(), s = new T.Vector3(), p = new T.Vector3();
      bushes.forEach((b, i) => {q.setFromAxisAngle(new T.Vector3(0, 1, 0), i * 1.7);m.compose(p.set(b.x, yLocal(b.x, b.s) - .1, b.s), q, s.set(b.r, b.r * .8, b.r));inst.setMatrixAt(i, m);});
      inst.castShadow = true;inst.receiveShadow = true;geoNear.add(inst);
    }
    const poleMat = lib.metal(0x3b3f42, .5);
    for (let i = 0; i < data.lamps.length; i += 2) {
      const x = data.lamps[i], s = data.lamps[i + 1], y = yLocal(x, s), pole = new T.Mesh(new T.CylinderGeometry(.05, .07, 5, 8), poleMat);pole.position.set(x, y + 2.5, s);pole.castShadow = true;geoNear.add(pole);
      const head = new T.Mesh(new T.CylinderGeometry(.25, .12, .25, 10), poleMat);head.position.set(x, y + 5, s);geoNear.add(head);
    }
    const hedgeObstacles = [];
    if (yard) {
      const hedgeMat = lib.leaves(0x3d5a26), wood = lib.wood(0x8a5a32, .6), red = lib.paint(0xc0392b, .4), yellow = lib.paint(0xe2b13c, .35), steelM = lib.metal(0x7d8589, .4);
      const put = (geo, mat, x, y, z, ry = 0) => {const m = new T.Mesh(geo, mat);m.position.set(x, y, z);m.rotation.y = ry;m.castShadow = m.receiveShadow = true;geoNear.add(m);return m;};
      for (const [x0, z0, x1, z1] of yard.hedges) {const len = Math.hypot(x1 - x0, z1 - z0), cx = (x0 + x1) / 2, cz = (z0 + z1) / 2;put(new T.BoxGeometry(len, 1.0, .75), hedgeMat, cx, yLocal(cx, cz) + .5, cz, -Math.atan2(z1 - z0, x1 - x0));hedgeObstacles.push({a: [x0, z0], b: [x1, z1], w: .75});}
      if (yard.playground) {
        const ring = pairs(yard.playground.o), cx = ring.reduce((a, p) => a + p[0], 0) / ring.length, cz = ring.reduce((a, p) => a + p[1], 0) / ring.length;
        ring.forEach((a, i) => {const b = ring[(i + 1) % ring.length], len = Math.hypot(b[0] - a[0], b[1] - a[1]);put(new T.BoxGeometry(len, .8, .6), hedgeMat, (a[0] + b[0]) / 2, yLocal((a[0] + b[0]) / 2, (a[1] + b[1]) / 2) + .4, (a[1] + b[1]) / 2, -Math.atan2(b[1] - a[1], b[0] - a[0]));});
        const g = new T.Group();g.position.set(cx, yLocal(cx, cz), cz);g.rotation.y = Math.atan2(-yard.axis[1], yard.axis[0]);geoNear.add(g);
        const part = (geo, mat, x, y, z, rx = 0, ry = 0, rz = 0) => {const m = new T.Mesh(geo, mat);m.position.set(x, y, z);m.rotation.set(rx, ry, rz);m.castShadow = m.receiveShadow = true;g.add(m);return m;};
        for (const [x, z] of [[-1.6, -.6], [-.6, -.6], [-1.6, .4], [-.6, .4]]) part(new T.BoxGeometry(.1, 2.2, .1), wood, x, 1.1, z);
        part(new T.BoxGeometry(1.1, .08, 1.1), wood, -1.1, 1.2, -.1);part(new T.ConeGeometry(.75, .7, 4), red, -1.1, 2.55, -.1, 0, Math.PI / 4);part(new T.BoxGeometry(.55, .05, 2.3), yellow, -1.1, .65, 1.45, -.5);
        for (const x of [1.0, 3.0]) {part(new T.CylinderGeometry(.05, .05, 2.3, 8), steelM, x, 1.1, -1.2, 0, 0, .18);part(new T.CylinderGeometry(.05, .05, 2.3, 8), steelM, x, 1.1, .2, 0, 0, -.18);}
        part(new T.CylinderGeometry(.05, .05, 2.2, 8), steelM, 2.0, 2.2, -.5, 0, 0, Math.PI / 2);
        for (const x of [1.6, 2.4]) {part(new T.BoxGeometry(.45, .04, .18), lib.paint(0x2b2b2b, .6), x, .45, -.5);part(new T.CylinderGeometry(.008, .008, 1.7, 4), steelM, x, 1.32, -.5);}
        for (const [w, d, x, z] of [[2.4, .15, 0, 2.4], [2.4, .15, 0, 4.6], [.15, 2.3, -1.2, 3.5], [.15, 2.3, 1.2, 3.5]]) part(new T.BoxGeometry(w, .3, d), wood, x, .15, z);
      }
      for (const [x, z, r] of yard.benches) {
        const b = new T.Group();b.position.set(x, yLocal(x, z), z);b.rotation.y = -r;geoNear.add(b);
        for (const dz of [-.18, 0, .18]) {const s = new T.Mesh(new T.BoxGeometry(1.6, .04, .12), wood);s.position.set(0, .45, dz);s.castShadow = true;b.add(s);}
        const back = new T.Mesh(new T.BoxGeometry(1.6, .35, .04), wood);back.position.set(0, .75, .3);back.rotation.x = -.15;b.add(back);
        for (const dx of [-.7, .7]) {const leg = new T.Mesh(new T.BoxGeometry(.06, .45, .5), steelM);leg.position.set(dx, .22, .05);b.add(leg);}
      }
    }
    lib.mergeStatic(geoNear, o => o.isInstancedMesh || o.name.startsWith('Terrain'));

    // ---------- Sky and hills ----------
    const sunDirection = new T.Vector3(0, 1, 0);
    const sky = new T.Mesh(new T.SphereGeometry(20000, 48, 24), new T.ShaderMaterial({
      side: T.BackSide, depthWrite: false, fog: false, uniforms: {sun: {value: sunDirection}},
      vertexShader: 'varying vec3 vDir; void main(){ vDir = normalize(position); gl_Position = projectionMatrix * modelViewMatrix * vec4(position,1.0); }',
      fragmentShader: `uniform vec3 sun; varying vec3 vDir;
        float h(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}
        float n(vec2 p){vec2 i=floor(p),f=fract(p);f=f*f*(3.0-2.0*f);return mix(mix(h(i),h(i+vec2(1,0)),f.x),mix(h(i+vec2(0,1)),h(i+vec2(1,1)),f.x),f.y);}
        float fbm(vec2 p){float v=0.0,a=.5;for(int i=0;i<5;i++){v+=a*n(p);p*=2.03;a*=.5;}return v;}
        void main(){vec3 d=normalize(vDir);float up=clamp(d.y,0.0,1.0);vec3 col=mix(vec3(.70,.80,.89),vec3(.22,.42,.78),pow(up,.55));
          float s=max(dot(d,normalize(sun)),0.0);col+=vec3(1.0,.9,.7)*pow(s,700.0)*6.0+vec3(1.0,.85,.6)*pow(s,12.0)*.25;
          if(d.y>0.0){vec2 uv=d.xz/(d.y+.12)*1.6;float c=smoothstep(.5,.78,fbm(uv+vec2(3.1,1.7)));col=mix(col,vec3(1.0)*(.92+.08*fbm(uv*3.0)),c*.85*smoothstep(0.0,.18,d.y));}
          if(d.y<0.0) col=vec3(.70,.80,.89)*.92;gl_FragColor=vec4(col,1.0);
          #include <tonemapping_fragment>
          #include <colorspace_fragment>
        }`
    }));
    sky.frustumCulled = false;far.add(sky);
    const hills = [[30, 3200, 280, 25], [55, 6500, 260, 30], [305, 6200, 360, 25], [335, 5000, 300, 22], [150, 3600, 90, 30], [180, 9000, 180, 40], [250, 12000, 150, 50], [95, 9000, 190, 40]];
    {
      const segments = 360, rows = 6, pos = [], index = [];
      for (let j = 0; j <= rows; j++) for (let i = 0; i <= segments; i++) {
        const bearing = i / segments * 360, t = j / rows;let height = 18 + 40 * Math.sin(bearing * .07) * Math.sin(bearing * .19);const distance = 2600 + t * 9000;
        for (const [b, dist, hgt, wid] of hills) {const d = Math.min(Math.abs(bearing - b), 360 - Math.abs(bearing - b)), w = Math.exp(-(d * d) / (2 * wid * wid));height += hgt * w * Math.max(0, 1 - Math.abs(distance - dist) / (dist * .9));}
        height *= Math.min(1, t * 2.4);const r = bearing * Math.PI / 180;pos.push(Math.sin(r) * distance, ground - 8 + height, -Math.cos(r) * distance);
      }
      for (let j = 0; j < rows; j++) for (let i = 0; i < segments; i++) {const a = j * (segments + 1) + i, b = a + segments + 1;index.push(a, b, a + 1, a + 1, b, b + 1);}
      const g = new T.BufferGeometry();g.setAttribute('position', new T.Float32BufferAttribute(pos, 3));g.setIndex(index);g.computeVertexNormals();
      geoFar.add(new T.Mesh(g, new T.MeshStandardMaterial({color: 0x3f5236, roughness: 1, side: T.DoubleSide})));
    }
    const farSun = new T.DirectionalLight(0xfff1dc, 2.4), farSky = new T.HemisphereLight(0xcfe0ee, 0x5a5a4a, 1.3);far.add(farSun, farSky);
    function setSun(direction) {sunDirection.copy(direction).normalize();farSun.position.copy(sunDirection).multiplyScalar(1000);}

    // ---------- Collision and queries (model coordinates) ----------
    const ringWorld = flat => pairs(flat).map(([x, s]) => toWorld(x, s));
    function obstacles() {
      const list = [];
      data.buildings.forEach(b => {
        const o = b.r.o;if (Math.hypot(o[0], o[1]) > PLAY_HALF + 40) return;
        const rings = b.enter !== undefined ? data.enterable[b.enter].collide.map(r => r.o) : [o];
        for (const r of rings) list.push({points: ringWorld(r), minY: b.bottom - .5, maxY: b.top + 4, kind: 'building'});
      });
      const ownRings = (parts, minY, maxY) => parts.forEach(p => list.push({points: pairs(p.o).map(([x, z]) => ({x, z})), minY, maxY, kind: 'building'}));
      ownRings(own.lower, ownBottom - .5, -.16);ownRings(own.band, -.16, own.bandTop);ownRings(own.upper, own.bandTop, own.top + 4);
      const square = (x, s, r, h, kind) => {const pts = [[x - r, s - r], [x + r, s - r], [x + r, s + r], [x - r, s + r]].map(([a, b]) => toWorld(a, b)), y = yLocal(x, s);list.push({points: pts, minY: y - .5, maxY: y + h, kind});};
      allTrees.forEach(t => {if (Math.hypot(t.x, t.s) < PLAY_HALF) square(t.x, t.s, .28, 6, 'tree');});
      bushes.forEach(b => square(b.x, b.s, .45 * b.r, 1.2, 'bush'));
      hedgeObstacles.forEach(h => {const dx = h.b[0] - h.a[0], dz = h.b[1] - h.a[1], L = Math.hypot(dx, dz), nx = -dz / L * h.w / 2, nz = dx / L * h.w / 2;const pts = [[h.a[0] + nx, h.a[1] + nz], [h.b[0] + nx, h.b[1] + nz], [h.b[0] - nx, h.b[1] - nz], [h.a[0] - nx, h.a[1] - nz]].map(([a, b]) => toWorld(a, b)), y = yLocal(h.a[0], h.a[1]);list.push({points: pts, minY: y - .5, maxY: y + 1, kind: 'hedge'});});
      return list;
    }
    function terrainFloor() {
      return {name: 'Neighbourhood', points: [[-PLAY_HALF, -PLAY_HALF], [PLAY_HALF, -PLAY_HALF], [PLAY_HALF, PLAY_HALF], [-PLAY_HALF, PLAY_HALF]].map(([x, s]) => toWorld(x, s)), height: p => heightWorld(p.x, p.z), ceilingHeight: Infinity, surfaceKind: 'terrain'};
    }
    const traffic = data.roads.filter(r => ['residential', 'tertiary', 'secondary', 'primary', 'trunk', 'trunk_link'].includes(r.k)).flatMap(r => {const p = pairs(r.p), w = {residential: .35, tertiary: .7, secondary: 1, primary: 1.4, trunk: 2, trunk_link: 1.2}[r.k];return p.slice(1).map((b, i) => ({a: p[i], b, w}));});
    function trafficDistance(x, z) {
      const l = toLocal(x, z);let best = 999;
      for (const sgm of traffic) {const dx = sgm.b[0] - sgm.a[0], dz = sgm.b[1] - sgm.a[1], L2 = dx * dx + dz * dz || 1, t = Math.max(0, Math.min(1, ((l.x - sgm.a[0]) * dx + (l.s - sgm.a[1]) * dz) / L2));const d = Math.hypot(l.x - sgm.a[0] - t * dx, l.s - sgm.a[1] - t * dz) / sgm.w;if (d < best) best = d;}
      return best;
    }
    function greenery(x, z) {const l = toLocal(x, z);let n = 0;for (const t of allTrees) if (Math.abs(t.x - l.x) < 40 && Math.abs(t.s - l.s) < 40) n++;for (const b of bushes) if (Math.abs(b.x - l.x) < 25 && Math.abs(b.s - l.s) < 25) n += .3;return Math.min(1, n / 18);}

    return {near, far, setImagery, setSun, get imageryState() {return imageryState;}, attribution: data.attribution, ground, heightWorld, toLocal, toWorld, terrainFloor, obstacles, surfaceAt, trafficDistance, greenery, data, playHalf: PLAY_HALF, trees: allTrees};
  }
  return {create};
});
