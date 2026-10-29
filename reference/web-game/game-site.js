/* The construction site (ported from the Unreal version, where the user asked for it on 28 Oct 2026): a fenced
 * 64 x 64 m site in the north of Torshovdalen with soil that can be dug, carried and dumped, and seven machines that
 * drive like the real ones: a Cat D6-class dozer, a 30 t excavator, a wheel loader, an articulated hauler, an 8x4
 * tipper, an all-terrain mobile crane and a tower crane, plus crane loads (brick pallets, steel beams, a concrete skip).
 * Layout and soil shape: site-data.js (from the Unreal project's tools/make-site.py). Models: site-models.js.
 *
 * Soil (DirtField, pure logic, also used by the Node tests): a height field of 0.25 m cells over the site in the map's
 * local frame. H is the surface, Firm the top of the compacted ground (soil above it is loose and slumps to its angle of
 * repose, 34 degrees), Rock the lowest diggable level, Base the terrain there. Tools cut soil above an oriented
 * rectangle's bottom and deposit it elsewhere; soil is conserved (what is cut is carried until it is put down).
 *
 * GameSite.DirtField(data, {yLocal, toLocal, toWorld}); GameSite.create({T, lib, audio, VH, models, data, world,
 *   groundAt, surfaceAt, onSpill}) -> {field, group, machines, loads, obstacles, update(dt)}
 */
(function (root, factory) {
  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.GameSite = api;
})(typeof window === 'undefined' ? globalThis : window, function () {
  'use strict';
  const TAN_REPOSE = .6745, TILE = 32, G = 9.81, SOIL_DENSITY = 1700;
  const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
  const sign = v => v > 0 ? 1 : v < 0 ? -1 : 0;
  // The sticks already have the controller's dead zone; this only drops noise so fine control stays possible.
  const dz = (v, d = .03) => !v || Math.abs(v) < d ? 0 : v;
  function int16s(b64) {
    const bin = typeof atob === 'function' ? atob(b64) : Buffer.from(b64, 'base64').toString('binary'), bytes = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
    return new Int16Array(bytes.buffer);
  }

  // ---------- Soil ----------
  class DirtField {
    constructor(data, {yLocal, toLocal, toWorld}) {
      this.toLocal = toLocal;this.toWorld = toWorld;
      const r = data.rect;this.x0 = r.x0;this.s0 = r.s0;this.w = r.w;this.d = r.d;this.cell = data.cell || .25;this.nx = data.nx;this.ns = data.ns;
      const n = this.nx * this.ns, off = int16s(data.offsets), firm = int16s(data.firm), rock = (data.rock ?? 450) / 100;
      this.H = new Float32Array(n);this.Firm = new Float32Array(n);this.Rock = new Float32Array(n);this.Base = new Float32Array(n);
      for (let j = 0; j < this.ns; j++) for (let i = 0; i < this.nx; i++) {
        const k = j * this.nx + i, b = yLocal(this.x0 + i * this.cell, this.s0 + j * this.cell);
        this.Base[k] = b;this.H[k] = b + off[k] / 100;this.Firm[k] = Math.min(this.H[k], b + firm[k] / 100);this.Rock[k] = Math.min(b, b + firm[k] / 100) - rock;
      }
      this.machines = data.machines || [];this.props = data.props || [];this.loads = data.loads || [];this.tracks = data.tracks || (data.track ? [data.track] : []);
      this.gateC = data.gate ? data.gate.centre : 0;this.gateW = data.gate ? data.gate.width : 0;
      this.ai0 = 0;this.aj0 = 0;this.ai1 = -1;this.aj1 = -1;this.settled = 0;this.dirty = new Set();
      for (let t = 0; t < this.tilesX * this.tilesS; t++) this.dirty.add(t);
      // Let the stockpiles settle once before the game starts.
      this.touch(0, 0, this.nx - 1, this.ns - 1);
      for (let k = 0; k < 40; k++) this.step(1 / 30);
    }
    get tilesX() {return Math.floor((this.nx - 2) / TILE) + 1;}
    get tilesS() {return Math.floor((this.ns - 2) / TILE) + 1;}
    nodeWorld(i, j) {return this.toWorld(this.x0 + i * this.cell, this.s0 + j * this.cell);}
    contains(x, z, margin = 0) {const l = this.toLocal(x, z);return l.x >= this.x0 + margin && l.x <= this.x0 + this.w - margin && l.s >= this.s0 + margin && l.s <= this.s0 + this.d - margin;}
    surface(x, z) {
      const l = this.toLocal(x, z), fi = (l.x - this.x0) / this.cell, fj = (l.s - this.s0) / this.cell;
      if (!(fi >= 0 && fj >= 0 && fi <= this.nx - 1 && fj <= this.ns - 1)) return null;
      const i = Math.min(this.nx - 2, Math.floor(fi)), j = Math.min(this.ns - 2, Math.floor(fj)), u = fi - i, v = fj - j, k = j * this.nx + i, H = this.H;
      return H[k] * (1 - u) * (1 - v) + H[k + 1] * u * (1 - v) + H[k + this.nx] * (1 - u) * v + H[k + this.nx + 1] * u * v;
    }
    looseAt(x, z) {
      const l = this.toLocal(x, z), i = Math.round((l.x - this.x0) / this.cell), j = Math.round((l.s - this.s0) / this.cell);
      if (i < 0 || j < 0 || i >= this.nx || j >= this.ns) return 0;return this.H[j * this.nx + i] - this.Firm[j * this.nx + i];
    }
    // A tool: {c: {x, z} centre, f: {x, z} unit forward, halfL, halfW, bottom (height of the cutting edge)}.
    nodes(t) {
      const out = [], R = {x: -t.f.z, z: t.f.x};let li0 = 1e9, lj0 = 1e9, li1 = -1e9, lj1 = -1e9;
      for (const [a, b] of [[1, 1], [1, -1], [-1, 1], [-1, -1]]) {
        const l = this.toLocal(t.c.x + t.f.x * t.halfL * a + R.x * t.halfW * b, t.c.z + t.f.z * t.halfL * a + R.z * t.halfW * b);
        li0 = Math.min(li0, (l.x - this.x0) / this.cell);li1 = Math.max(li1, (l.x - this.x0) / this.cell);lj0 = Math.min(lj0, (l.s - this.s0) / this.cell);lj1 = Math.max(lj1, (l.s - this.s0) / this.cell);
      }
      const I0 = Math.max(1, Math.floor(li0)), I1 = Math.min(this.nx - 2, Math.ceil(li1)), J0 = Math.max(1, Math.floor(lj0)), J1 = Math.min(this.ns - 2, Math.ceil(lj1));
      for (let j = J0; j <= J1; j++) for (let i = I0; i <= I1; i++) {
        const p = this.nodeWorld(i, j), dx = p.x - t.c.x, dz2 = p.z - t.c.z;
        if (Math.abs(dx * t.f.x + dz2 * t.f.z) <= t.halfL && Math.abs(dx * R.x + dz2 * R.z) <= t.halfW) out.push(j * this.nx + i);
      }
      return out;
    }
    cut(t, maxVolume) {
      const K = maxVolume > 0 ? this.nodes(t) : [];if (!K.length) return 0;
      const A = this.cell * this.cell, H = this.H;let avail = 0;
      for (const k of K) avail += Math.max(0, H[k] - Math.max(t.bottom, this.Rock[k])) * A;
      if (avail <= 1e-6) return 0;
      const scale = Math.min(1, maxVolume / avail);let i0 = this.nx, j0 = this.ns, i1 = 0, j1 = 0;
      for (const k of K) {
        const take = Math.max(0, H[k] - Math.max(t.bottom, this.Rock[k])) * scale;if (take <= 0) continue;
        H[k] -= take;this.Firm[k] = Math.min(this.Firm[k], H[k]);
        i0 = Math.min(i0, k % this.nx);i1 = Math.max(i1, k % this.nx);j0 = Math.min(j0, Math.floor(k / this.nx));j1 = Math.max(j1, Math.floor(k / this.nx));
      }
      if (i1 >= i0) this.touch(i0, j0, i1, j1);
      return avail * scale;
    }
    volumeAbove(t) {let v = 0;for (const k of this.nodes(t)) v += Math.max(0, this.H[k] - Math.max(t.bottom, this.Rock[k]));return v * this.cell * this.cell;}
    deposit(t, volume) {
      const K = volume > 0 ? this.nodes(t) : [];if (!K.length) return 0;
      const dh = volume / (K.length * this.cell * this.cell);let i0 = this.nx, j0 = this.ns, i1 = 0, j1 = 0;
      for (const k of K) {this.H[k] += dh;i0 = Math.min(i0, k % this.nx);i1 = Math.max(i1, k % this.nx);j0 = Math.min(j0, Math.floor(k / this.nx));j1 = Math.max(j1, Math.floor(k / this.nx));}
      this.touch(i0, j0, i1, j1);return volume;
    }
    markTiles(I0, J0, I1, J1) {
      for (let tj = Math.max(0, Math.floor((J0 - 1) / TILE)); tj <= Math.min(this.tilesS - 1, Math.floor(J1 / TILE)); tj++)
        for (let ti = Math.max(0, Math.floor((I0 - 1) / TILE)); ti <= Math.min(this.tilesX - 1, Math.floor(I1 / TILE)); ti++) this.dirty.add(tj * this.tilesX + ti);
    }
    touch(I0, J0, I1, J1) {
      const P = 8;// loose soil runs a little beyond what the tool touched
      I0 = Math.max(0, I0 - P);J0 = Math.max(0, J0 - P);I1 = Math.min(this.nx - 1, I1 + P);J1 = Math.min(this.ns - 1, J1 + P);
      if (this.ai1 < this.ai0) {this.ai0 = I0;this.aj0 = J0;this.ai1 = I1;this.aj1 = J1;}
      else {this.ai0 = Math.min(this.ai0, I0);this.aj0 = Math.min(this.aj0, J0);this.ai1 = Math.max(this.ai1, I1);this.aj1 = Math.max(this.aj1, J1);}
      this.settled = 0;this.markTiles(I0, J0, I1, J1);
    }
    // Loose soil slides to its angle of repose: neighbours may differ by cell * tan(34 deg); a steeper step moves soil from
    // the higher node (only its loose part) to the lower one. Two sweeps a frame over the active region.
    step(dt) {
      if (this.ai1 < this.ai0) return;
      const lim = this.cell * TAN_REPOSE, H = this.H, F = this.Firm, nx = this.nx, ns = this.ns;
      let moved = 0, I0 = nx, J0 = ns, I1 = -1, J1 = -1;
      for (let sweep = 0; sweep < 2; sweep++)
        for (let j = Math.max(0, this.aj0); j <= Math.min(ns - 1, this.aj1); j++)
          for (let i = Math.max(0, this.ai0); i <= Math.min(nx - 1, this.ai1); i++) {
            const k = j * nx + i;
            for (let q = 0; q < 2; q++) {
              const n = q === 0 ? (i + 1 < nx ? k + 1 : -1) : (j + 1 < ns ? k + nx : -1);if (n < 0) continue;
              const d = H[k] - H[n];if (Math.abs(d) <= lim) continue;
              const hi = d > 0 ? k : n, lo = d > 0 ? n : k, m = Math.min((Math.abs(d) - lim) * .25, H[hi] - F[hi]);if (m <= 1e-5) continue;
              H[hi] -= m;H[lo] += m;moved += m;
              const a = hi % nx, b = Math.floor(hi / nx);
              I0 = Math.min(I0, a, n % nx);I1 = Math.max(I1, a, n % nx);J0 = Math.min(J0, b, Math.floor(n / nx));J1 = Math.max(J1, b, Math.floor(n / nx));
            }
          }
      if (I1 >= I0) this.markTiles(I0, J0, I1, J1);
      if (moved < 1e-3) {this.settled += dt;if (this.settled > .5) {this.ai0 = 0;this.aj0 = 0;this.ai1 = -1;this.aj1 = -1;}} else this.settled = 0;
    }
    volume() {let v = 0;for (let k = 0; k < this.H.length; k++) v += this.H[k] - this.Rock[k];return v * this.cell * this.cell;}
  }

  // ---------- The site in the game ----------
  // Fence panels block walking and driving (a thin box along each panel); the gate is a gap in them.
  function fenceObstacles(field, ground) {
    const out = [];
    for (const p of field.props) {
      if (p.kind !== 'fence') continue;
      const L = (p.len || 3.5) / 2, t = .08, c = Math.cos(p.yaw), s = Math.sin(p.yaw), gy = ground(p.x, p.z);
      out.push({points: [[-L, -t], [L, -t], [L, t], [-L, t]].map(([a, b]) => ({x: p.x + a * c + b * s, z: p.z - a * s + b * c})), minY: gy - 1.5, maxY: gy + 2.1, kind: 'fence'});
    }
    return out;
  }
  const fieldFor = (data, world) => new DirtField(data, {yLocal: (x, s) => {const p = world.toWorld(x, s);return world.heightWorld(p.x, p.z);}, toLocal: world.toLocal, toWorld: world.toWorld});
  // `field` may be made first (fieldFor) so the game's ground height already knows the soil when the machines are placed.
  function create({T, lib, audio, VH, models, data, world, groundAt, surfaceAt, onSpill, field = fieldFor(data, world)}) {
    const group = new T.Group();group.name = 'Construction site';
    const V3 = (x = 0, y = 0, z = 0) => new T.Vector3(x, y, z);
    const sfx = (n, gain) => audio.sfx(n, gain === undefined ? {} : {gain});
    const ground = (x, z) => groundAt(x, z);

    // ---- Soil mesh: 32 x 32-cell tiles, only changed tiles rebuilt (a few a frame), plus a skirt along the fence. ----
    const soilTex = lib.concrete(0xffffff);
    const soilMat = new T.MeshStandardMaterial({vertexColors: true, map: soilTex.map, normalMap: soilTex.normalMap, roughness: 1, metalness: 0});
    const cT = Math.cos(world.data ? world.data.geo.theta : 0), sT = Math.sin(world.data ? world.data.geo.theta : 0);
    const EX = {x: cT, z: -sT}, ES = {x: sT, z: cT};// world directions of local +x and +s
    const COL = {firm: [.5, .37, .24], loose: [.36, .25, .15], deep: [.64, .46, .29]};
    const tiles = [];
    function buildTile(ti, tj) {
      const F = field, i0 = ti * TILE, j0 = tj * TILE, i1 = Math.min(F.nx - 1, i0 + TILE), j1 = Math.min(F.ns - 1, j0 + TILE), cw = i1 - i0 + 1, ch = j1 - j0 + 1;
      let tile = tiles[tj * F.tilesX + ti];
      if (!tile) {
        const g = new T.BufferGeometry(), n = cw * ch;
        g.setAttribute('position', new T.BufferAttribute(new Float32Array(n * 3), 3));g.setAttribute('normal', new T.BufferAttribute(new Float32Array(n * 3), 3));
        g.setAttribute('color', new T.BufferAttribute(new Float32Array(n * 3), 3));g.setAttribute('uv', new T.BufferAttribute(new Float32Array(n * 2), 2));
        const idx = [];for (let j = 0; j < ch - 1; j++) for (let i = 0; i < cw - 1; i++) {const a = j * cw + i, b = a + 1, c = a + cw, d = c + 1;idx.push(a, c, b, b, c, d);}
        g.setIndex(idx);
        const m = new T.Mesh(g, soilMat);m.receiveShadow = true;m.castShadow = true;m.name = 'Soil';group.add(m);
        tile = tiles[tj * F.tilesX + ti] = {mesh: m, g, cw, ch, i0, j0};
      }
      const P = tile.g.attributes.position.array, N = tile.g.attributes.normal.array, C = tile.g.attributes.color.array, U = tile.g.attributes.uv.array, H = F.H;
      for (let j = j0; j <= j1; j++) for (let i = i0; i <= i1; i++) {
        const k = j * F.nx + i, o = (j - j0) * cw + (i - i0), w = F.nodeWorld(i, j);
        P[o * 3] = w.x;P[o * 3 + 1] = H[k];P[o * 3 + 2] = w.z;
        const gx = (H[j * F.nx + Math.min(F.nx - 1, i + 1)] - H[j * F.nx + Math.max(0, i - 1)]) / (2 * F.cell), gs = (H[Math.min(F.ns - 1, j + 1) * F.nx + i] - H[Math.max(0, j - 1) * F.nx + i]) / (2 * F.cell);
        let nx = -EX.x * gx - ES.x * gs, ny = 1, nz = -EX.z * gx - ES.z * gs;const l = Math.hypot(nx, ny, nz);nx /= l;ny /= l;nz /= l;
        N[o * 3] = nx;N[o * 3 + 1] = ny;N[o * 3 + 2] = nz;
        U[o * 2] = (F.x0 + i * F.cell) / 2.5;U[o * 2 + 1] = (F.s0 + j * F.cell) / 2.5;
        const loose = clamp((H[k] - F.Firm[k]) / .3, 0, 1), deep = clamp((F.Base[k] - H[k] - .3) / 2, 0, 1);
        for (let c = 0; c < 3; c++) C[o * 3 + c] = (COL.firm[c] + (COL.loose[c] - COL.firm[c]) * loose) * (1 - deep) + COL.deep[c] * deep;
      }
      for (const a of ['position', 'normal', 'color', 'uv']) tile.g.attributes[a].needsUpdate = true;
      tile.g.computeBoundingSphere();tile.g.computeBoundingBox();
    }
    for (let tj = 0; tj < field.tilesS; tj++) for (let ti = 0; ti < field.tilesX; ti++) buildTile(ti, tj);
    field.dirty.clear();
    {
      // Skirt: hides the step down to the coarse far terrain along the fence.
      const pos = [], col = [], idx = [];
      const edge = (ia, ja, di, dj, count) => {
        const start = pos.length / 3;
        for (let k = 0; k <= count; k++) {const i = ia + di * k, j = ja + dj * k, w = field.nodeWorld(i, j), y = field.H[j * field.nx + i];pos.push(w.x, y, w.z, w.x, y - 1.6, w.z);col.push(...COL.deep, ...COL.deep);}
        for (let k = 0; k < count; k++) {const a = start + k * 2, b = a + 2;idx.push(a, a + 1, b, b, a + 1, b + 1);}
      };
      edge(0, 0, 1, 0, field.nx - 1);edge(0, field.ns - 1, 1, 0, field.nx - 1);edge(0, 0, 0, 1, field.ns - 1);edge(field.nx - 1, 0, 0, 1, field.ns - 1);
      const g = new T.BufferGeometry();g.setAttribute('position', new T.Float32BufferAttribute(pos, 3));g.setAttribute('color', new T.Float32BufferAttribute(col, 3));g.setIndex(idx);g.computeVertexNormals();
      const m = new T.Mesh(g, new T.MeshStandardMaterial({vertexColors: true, roughness: 1, side: T.DoubleSide}));m.receiveShadow = true;group.add(m);
    }

    // ---- Furniture: fence panels (walk and vehicle obstacles; the gate is a gap), offices, toilet, signs, pallets, lights. ----
    const obstacles = fenceObstacles(field, ground);
    for (const p of field.props) {
      const g = models.prop(p.kind, p.len || 3.5);if (!g) continue;
      g.position.set(p.x, ground(p.x, p.z) - .02, p.z);g.rotation.y = p.yaw;group.add(g);
    }
    for (const tr of field.tracks) if (tr.length && models.track) {const m = models.track(tr);if (m) group.add(m);}

    // ---- Machines ----
    const machines = [], loads = [];
    const tool = (c, f, halfL, halfW, bottom) => {const l = Math.hypot(f.x, f.z) || 1;return {c: {x: c.x, z: c.z}, f: {x: f.x / l, z: f.z / l}, halfL, halfW, bottom};};
    const worldDir = (node, local) => node.localToWorld(local.clone()).sub(node.localToWorld(V3())).normalize();
    const wpos = node => node.getWorldPosition(V3());
    function poseRams(rootObj, rams) {
      rootObj.updateMatrixWorld(true);const inv = rootObj.matrixWorld.clone().invert();
      for (const r of rams) {
        const pa = r.pa.localToWorld(r.a.clone()).applyMatrix4(inv), pb = r.pb.localToWorld(r.b.clone()).applyMatrix4(inv), d = pb.clone().sub(pa), len = d.length();d.normalize();
        const q = new T.Quaternion().setFromUnitVectors(V3(0, 1, 0), d);
        r.barrel.scale.set(1, len * .58, 1);r.barrel.position.copy(pa).addScaledVector(d, len * .29);r.barrel.quaternion.copy(q);
        r.rod.scale.set(1, len * .5, 1);r.rod.position.copy(pb).addScaledVector(d, -len * .25);r.rod.quaternion.copy(q);
      }
    }
    // A hook hanging from `top` at world point h: hook and wire are nodes under the rig root.
    function poseHook(rootObj, hookNode, wire, top, h) {
      rootObj.updateMatrixWorld(true);const inv = rootObj.matrixWorld.clone().invert(), lh = h.clone().applyMatrix4(inv), lt = top.clone().applyMatrix4(inv);
      hookNode.position.copy(lh);hookNode.quaternion.copy(rootObj.getWorldQuaternion(new T.Quaternion()).invert());
      const a = lh.clone().add(V3(0, .45, 0)), d = lt.clone().sub(a), L = Math.max(.01, d.length());
      wire.position.copy(a);wire.quaternion.setFromUnitVectors(V3(0, 1, 0), d.normalize());wire.scale.set(1, L, 1);
    }

    class SiteMachine extends VH.Vehicle {
      constructor(x, z, yaw, radius, length, rig, kind, name) {
        super(x, z, yaw, radius, length);
        this.kind = kind;this.name = name;this.verb = 'Climb into the';this.site = true;this.cullDistance = 220;
        const r = models.rig(rig);this.rig = r;this.root = r.group;this.group.add(r.group);this.body = r.group;
        this.payload = 0;this.capacity = 0;this.massKg = 20000;this.powerKw = 120;this.vMax = 10;this.vRev = 4;this.grip = .65;this.steer = 0;this.gears = 4;this.tracked = false;
        this.engineLoad = 0;this.seatNode = null;this.seatOffset = V3();this.halfL = 2;this.halfW = 1;this.wheelAngle = 0;this.gear = 1;this.rpm = 0;this.terrainRoll = 0;
        this.camPitch = 0;this.camYaw = 0;this.rightStickTool = false;this.help = '';this.engine = null;
        machines.push(this);
      }
      N(name) {const n = this.rig.nodes[name];if (!n) throw new Error(`Site model ${this.kind}: no node ${name}`);return n;}
      ram(pa, a, pb, b, i) {return {pa, a, pb, b, barrel: this.N(`rams[${i}].barrel`), rod: this.N(`rams[${i}].rod`)};}
      acceptSoil() {return 0;}
      // Soil leaving a bucket or body at world point `at`: into a truck under it, onto the site's soil, or a heap outside.
      pourSoil(at, volume, spread = .8) {
        if (volume <= 0) return;
        for (const m of machines) if (m !== this) {volume -= m.acceptSoil(at, volume);if (volume <= 1e-6) return;}
        if (field.contains(at.x, at.z, .5)) {field.deposit(tool(at, this.forward(), spread, spread, 0), volume);return;}
        if (onSpill) onSpill(at.x, at.z, volume);
      }
      seat() {this.group.updateMatrixWorld(true);const p = (this.seatNode || this.root).localToWorld(this.seatOffset.clone());return {x: p.x, y: p.y, z: p.z, yaw: this.yaw, pitch: this.pitch, roll: this.terrainRoll || 0};}
      hipAnchor(seat) {return seat === 'd' ? {obj: this.seatNode || this.root, pos: this.seatOffset.clone().add(V3(0, -.62, .18)), stance: 'drive'} : null;}
      enter() {this.active = true;audio.sfx('carDoor');audio.sfx('starter');this.engine = audio.engine('diesel');}
      exit() {this.active = false;if (this.engine) this.engine.stop();this.engine = null;audio.sfx('carDoor');return this.exitPoint(-1);}
      gearText() {return this.tracked ? ` · ${this.speed < -.05 ? 'R' : 'F'}` : ` · gear ${this.speed < -.05 ? 'R' : ''}${Math.max(1, this.gear)} · ${Math.round(this.rpm / 50) * 50} rpm`;}
      hud() {return `${Math.round(Math.abs(this.speed) * 3.6)} km/h${this.gearText()}`;}
      groundResistance() {
        // Rolling resistance coefficients: loose soil, grass, gravel, asphalt.
        if (field.contains(this.x, this.z)) return field.looseAt(this.x, this.z) > .15 ? .12 : .07;
        const s = surfaceAt(this.x, this.z);return s === 'asphalt' ? .015 : s === 'gravel' ? .035 : s === 'sand' ? .1 : .05;
      }
      // Wheeled machine: power-limited traction up to the grip limit; brakes; rolling resistance; the slope.
      drive(dt, fwd, back, extra = 0) {
        const M = this.massKg + this.payload * SOIL_DENSITY, v = this.speed, av = Math.abs(v), pull = Math.min(this.grip * M * G, this.powerKw * 1000 / Math.max(av, 1.2));
        let a = -G * Math.sin(this.pitch);
        if (fwd > .02 && v > -.3) a += fwd * pull / M;
        else if (back > .02 && v < .3) a -= back * pull / M * .7;
        if (back > .02 && v > .3) a -= back * .5 * G;
        if (fwd > .02 && v < -.3) a += fwd * .5 * G;
        const resist = (this.groundResistance() + extra) * G + (fwd < .02 ? .25 : 0);
        if (av > .03) a -= sign(v) * Math.min(resist, av / dt);
        this.speed = clamp(v + a * dt, -this.vRev, this.vMax);
        if (fwd < .02 && back < .02 && Math.abs(this.speed) < .15) this.speed = 0;// parked: the brakes hold
        const frac = Math.abs(this.speed) / this.vMax;this.gear = clamp(1 + Math.floor(frac * this.gears * .999), 1, this.gears);
        this.rpm = 700 + (frac * this.gears - (this.gear - 1)) * 1300 + Math.max(fwd, back) * 250;this.engineLoad = Math.max(fwd, back);
      }
      // Tracks: the levers set the speed; the pump slows the machine on a climb.
      hydrostatic(dt, target, accel) {
        const up = Math.max(0, Math.sin(this.pitch) * sign(target));target *= 1 - Math.min(.6, up * 1.6);
        this.speed += clamp(target - this.speed, -accel * 1.6 * dt, accel * dt);
        if (Math.abs(target) < .01 && Math.abs(this.speed) < .05) this.speed = 0;
        this.rpm = 900 + Math.abs(target) / Math.max(.1, this.vMax) * 1200;
      }
      // The machine rests on the higher of its centre and the average of its ends (it bridges small holes).
      settleBig(dt) {
        const f = this.forward(), rx = -f.z, rz = f.x, hf = ground(this.x + f.x * this.halfL, this.z + f.z * this.halfL), hb = ground(this.x - f.x * this.halfL, this.z - f.z * this.halfL);
        const hl = ground(this.x - rx * this.halfW, this.z - rz * this.halfW), hr = ground(this.x + rx * this.halfW, this.z + rz * this.halfW), hc = ground(this.x, this.z);
        this.y = Math.max((hf + hb) / 2, hc - .15);const k = Math.min(1, dt * 4);
        this.pitch += (Math.atan2(hf - hb, 2 * this.halfL) - this.pitch) * k;this.terrainRoll += (Math.atan2(hl - hr, 2 * this.halfW) - this.terrainRoll) * k;this.roll = this.terrainRoll;
      }
      engineSound(rpm, load) {if (this.engine) this.engine.set(rpm, load);}
      spinWheels(list, r, dt) {this.wheelAngle -= this.speed / r * dt;for (const w of list) w.rotation.x = this.wheelAngle;}
    }

    // Cat D6 class crawler dozer: hydrostatic tracks, a semi-U blade that cuts, pushes and spreads soil.
    class Dozer extends SiteMachine {
      constructor(x, z, yaw) {
        super(x, z, yaw, 1.6, 5.2, 'site_dozer', 'dozer', 'Dozer');
        Object.assign(this, {width: 3.6, height: 3.2, massKg: 21000, vMax: 2.9, vRev: 3.3, tracked: true, halfL: 1.3, halfW: 1.0, lift: .1, tilt: 0, trackPhase: 0, ahead: 0, capacity: 4.0});
        this.arm = this.N('bladeArm');this.blade = this.N('blade');this.edge = this.N('bladeEdge');this.tracks = [this.N('tracks[0]'), this.N('tracks[1]')];
        // Lift cylinders: from beside the radiator down to the back of the blade.
        this.rams = [0, 1].map(i => {const sx = i ? 1 : -1;return this.ram(this.root, V3(sx * .8, 1.6, -1.6), this.blade, V3(sx * .8, .8, .3), i);});
        this.seatNode = this.root;this.seatOffset = V3(0, 2.5, .9);this.camPitch = -.12;this.rightStickTool = true;
        this.help = 'Dozer · R2 forward, L2 reverse, left stick steer · right stick back / forward: blade up / down, ←→ tilt · R3 / V view · □ get out';
        this.pose();this.place();
      }
      pose() {this.arm.rotation.x = this.lift;this.blade.rotation.z = this.tilt;this.edge.rotation.z = this.tilt;poseRams(this.root, this.rams);}
      update(dt, input = {}) {
        let target = 0;
        if (this.active) {
          this.lift = clamp(this.lift + dz(input.ry) * .35 * dt, -.16, .5);// stick back raises the blade
          this.tilt = clamp(this.tilt + dz(input.rx) * .25 * dt, -.14, .14);
          target = (input.throttle || 0) * this.vMax - (input.brake || 0) * this.vRev;this.steer = input.steer || 0;
        } else this.steer = 0;
        this.pose();this.group.updateMatrixWorld(true);
        // Blade below the surface while moving forward: it cuts what is above its edge and pushes it into a heap in front.
        const e = wpos(this.edge), f = this.forward(), surf = ground(e.x, e.z);let resist = 0;this.ahead = 0;
        if (field.contains(e.x, e.z, 1.5)) {
          this.ahead = field.volumeAbove(tool({x: e.x + f.x * .55, z: e.z + f.z * .55}, f, .55, 1.75, e.y + .05));
          if (this.speed > .05 && e.y < surf + .03) {
            const step = Math.max(.12, this.speed * dt);
            const cut = field.cut(tool({x: e.x - f.x * step / 2, z: e.z - f.z * step / 2}, f, step / 2 + .1, 1.78, e.y), 2.5 * dt + this.speed * dt * 1.8);
            if (cut > 0) field.deposit(tool({x: e.x + f.x * .4, z: e.z + f.z * .4}, f, .3, 1.7, 0), cut);
          }
          resist = Math.min(.55, this.ahead / 9) + (e.y < surf - .25 ? .4 : 0);
        }
        const turn = Math.abs(this.steer);
        this.hydrostatic(dt, target * (1 - resist) * (1 - turn * .35), 1.0);
        this.yaw -= this.steer * (.42 + .18 * Math.abs(this.speed) / this.vMax) * dt;
        this.move(dt);this.settleBig(dt);this.trackPhase += dt;
        const vl = this.speed + this.steer * .6, vr = this.speed - this.steer * .6;
        this.tracks[0].position.z = (vl * this.trackPhase) % .2;this.tracks[1].position.z = (vr * this.trackPhase) % .2;
        this.place();
        this.engineSound(this.active ? this.rpm + resist * 400 : 0, this.active ? Math.max(resist, Math.abs(target) / this.vMax) : 0);
        this.payload = this.ahead;
      }
      netExtra() {return [this.lift, this.tilt];}
      netApply(a) {this.lift = a[0] || 0;this.tilt = a[1] || 0;this.pose();}
      hud() {return `${(Math.abs(this.speed) * 3.6).toFixed(1)} km/h${this.gearText()} · blade ${(this.lift * 1.9).toFixed(1)} m · pushing ${this.ahead.toFixed(1)} m³`;}
    }

    // 30 t crawler excavator: swing, boom, stick and bucket (ISO controls like the street excavator), digs the site soil.
    class SiteExcavator extends SiteMachine {
      constructor(x, z, yaw) {
        super(x, z, yaw, 1.6, 4.9, 'site_excavator', 'excavator30', 'Excavator (30 t)');
        Object.assign(this, {width: 3.2, height: 3.2, massKg: 30000, vMax: 1.5, vRev: 1.5, tracked: true, halfL: 1.9, halfW: 1.3, swing: 0, boom: .35, stickA: -1.35, bucketA: -1.2, vl: 0, vr: 0, trackPhase: 0, capacity: 1.9});
        this.tracks = [this.N('tracks[0]'), this.N('tracks[1]')];this.upper = this.N('upper');this.cab = this.N('cab');
        this.boomPivot = this.N('boomPivot');this.stickPivot = this.N('stickPivot');this.bucketPivot = this.N('bucketPivot');this.tip = this.N('tip');this.dirt = this.N('dirt');
        this.rams = [this.ram(this.upper, V3(.15, .45, -.55), this.boomPivot, V3(0, -.25, -2.3), 0), this.ram(this.boomPivot, V3(0, .75, -2.4), this.stickPivot, V3(0, .55, .55), 1), this.ram(this.stickPivot, V3(0, .3, -.55), this.bucketPivot, V3(0, .3, .2), 2)];
        this.seatNode = this.cab;this.seatOffset = V3(0, 1.3, .2);this.camPitch = -.3;this.camYaw = -.12;this.rightStickTool = true;
        this.help = 'Excavator · tracks: L1/L2 left, R1/R2 right · left stick: swing / stick · right stick: boom / bucket · R3 / V view · □ get out';
        this.lastTip = V3();this.pose();this.place();
      }
      pose() {
        this.upper.rotation.y = this.swing;this.boomPivot.rotation.x = this.boom;this.stickPivot.rotation.x = this.stickA;this.bucketPivot.rotation.x = this.bucketA;
        poseRams(this.root, this.rams);this.dirt.visible = this.payload > .1;const s = Math.max(.15, Math.min(1, this.payload / this.capacity));this.dirt.scale.set(s, s, s);
      }
      seat() {const s = super.seat();s.yaw = this.yaw + this.swing;return s;}
      update(dt, input = {}) {
        let flow = 0, swingIn = 0, stickIn = 0, boomIn = 0, bucketIn = 0;
        if (this.active) {
          const tl = (input.l1 ? 1 : 0) - (input.l2 || 0), tr = (input.r1 ? 1 : 0) - (input.r2 || 0), k = Math.min(1, dt * 1.6);
          this.vl += (tl * this.vMax - this.vl) * k;this.vr += (tr * this.vMax - this.vr) * k;
          swingIn = dz(input.lx);stickIn = dz(input.ly);boomIn = dz(input.ry);bucketIn = dz(input.rx);
          this.swing -= swingIn * .65 * dt;
          this.stickA = clamp(this.stickA - stickIn * .55 * dt, -2.65, -.3);
          this.boom = clamp(this.boom + boomIn * .38 * dt, -.95, 1.05);
          this.bucketA = clamp(this.bucketA + bucketIn * .95 * dt, -2.7, .9);
          flow = Math.min(1, Math.abs(swingIn) + Math.abs(stickIn) + Math.abs(boomIn) + Math.abs(bucketIn));
        } else {this.vl = this.vr = 0;}
        this.speed = (this.vl + this.vr) / 2;this.yaw += (this.vl - this.vr) / 2.6 * dt;
        this.pose();this.group.updateMatrixWorld(true);
        const tipW = wpos(this.tip), moved = tipW.clone().sub(this.lastTip);this.lastTip.copy(tipW);
        // Digging: teeth below the soil while the bucket curls, the stick pulls in (stick back) or the boom breaks out.
        const gy = ground(tipW.x, tipW.z), mv = Math.hypot(moved.x, moved.z);
        if (field.contains(tipW.x, tipW.z, .8) && tipW.y < gy - .02 && this.payload < this.capacity && (bucketIn < -.15 || stickIn > .15 || boomIn > .15)) {
          const dir = mv > .002 ? {x: moved.x, z: moved.z} : {x: -Math.sin(this.yaw + this.swing), z: -Math.cos(this.yaw + this.swing)};
          const cut = field.cut(tool(tipW, dir, .3 + mv, .72, tipW.y), Math.min(this.capacity - this.payload, 1.4 * dt));
          this.payload += cut;if (cut > 0 && Math.random() < dt * 5) sfx('scrape', .45);
        }
        // Deep in the ground the boom cannot go further down.
        if (tipW.y < gy - .5) this.boom = Math.min(1.05, this.boom + .3 * dt);
        // Spilling: the bucket's opening turned below the horizontal pours the soil out (not while it is in the ground).
        if (this.payload > 0 && tipW.y > gy + .1) {
          const mouth = worldDir(this.bucketPivot, V3(0, -.91, -.42)), rate = 4.0 * clamp((-.12 - mouth.y) / .6, 0, 1);
          if (rate > 0) {const out = Math.min(this.payload, rate * dt);this.payload -= out;this.pourSoil(this.bucketPivot.localToWorld(V3(0, .35, -.8)), out, .7);if (Math.random() < dt * 3) sfx('dump', .4);}
        }
        this.move(dt);this.settleBig(dt);this.trackPhase += dt;
        this.tracks[0].position.z = (this.vl * this.trackPhase) % .17;this.tracks[1].position.z = (this.vr * this.trackPhase) % .17;
        this.place();this.pose();
        if (this.engine) {const l = Math.max(flow, Math.abs(this.speed) / this.vMax);this.engine.set(950 + 1150 * l, l);this.engine.hydraulics(flow);}
      }
      netExtra() {return [this.swing, this.boom, this.stickA, this.bucketA, this.payload];}
      netApply(a) {[this.swing, this.boom, this.stickA, this.bucketA, this.payload] = a.map(v => v || 0);this.pose();}
      hud() {return `${(Math.abs(this.speed) * 3.6).toFixed(1)} km/h · bucket ${this.payload.toFixed(1)} / ${this.capacity.toFixed(1)} m³`;}
    }

    // Two-unit articulated steering (wheel loader, hauler): the front unit's axle leads, the rear follows the hinge.
    class Articulation {
      constructor(la, lb) {this.la = la;this.lb = lb;this.pa = {x: 0, z: 0};this.pb = {x: 0, z: 0};this.yawA = 0;this.yawB = 0;this.phi = 0;}
      init(hx, hz, yawRear, phi) {
        this.phi = phi;this.yawB = yawRear;this.yawA = yawRear + phi;
        this.pa = {x: hx - Math.sin(this.yawA) * this.la, z: hz - Math.cos(this.yawA) * this.la};this.pb = {x: hx + Math.sin(this.yawB) * this.lb, z: hz + Math.cos(this.yawB) * this.lb};
      }
      hinge() {return {x: this.pa.x + Math.sin(this.yawA) * this.la, z: this.pa.z + Math.cos(this.yawA) * this.la};}
      step(dist, phiNow) {
        this.pa.x += -Math.sin(this.yawA) * dist;this.pa.z += -Math.cos(this.yawA) * dist;
        const h = this.hinge();let dx = h.x - this.pb.x, dzz = h.z - this.pb.z;const l = Math.hypot(dx, dzz);
        if (l > 1e-6) {dx /= l;dzz /= l;this.yawB = Math.atan2(-dx, -dzz);}
        this.pb = {x: h.x + Math.sin(this.yawB) * this.lb, z: h.z + Math.cos(this.yawB) * this.lb};
        this.phi = phiNow;this.yawA = this.yawB + this.phi;
        this.pa = {x: h.x - Math.sin(this.yawA) * this.la, z: h.z - Math.cos(this.yawA) * this.la};
      }
    }

    // Volvo L120 class wheel loader: articulated, lift arm and self-levelling bucket, scoops soil from piles.
    class WheelLoader extends SiteMachine {
      constructor(x, z, yaw) {
        super(x, z, yaw, 1.35, 8.2, 'site_loader', 'loader', 'Wheel loader');
        Object.assign(this, {width: 2.9, height: 3.4, massKg: 20500, powerKw: 200, vMax: 10.5, vRev: 6, gears: 4, halfL: 1.6, halfW: 1.0, liftA: 0, tiltA: .15, spin: 0, capacity: 3.6});
        this.front = this.N('front');this.arm = this.N('liftArm');this.bucket = this.N('bucket');this.lip = this.N('lip');this.dirt = this.N('dirt');
        this.wheels = ['rwheels[0]', 'rwheels[1]', 'fwheels[0]', 'fwheels[1]'].map(n => this.N(n));
        this.art = new Articulation(1.6, 1.6);this.art.init(this.x, this.z, this.yaw, 0);
        this.rams = [this.ram(this.front, V3(-.62, .75, -1.9), this.arm, V3(-.62, -.95, -1.0), 0), this.ram(this.front, V3(.62, .75, -1.9), this.arm, V3(.62, -.95, -1.0), 1), this.ram(this.front, V3(0, 2.05, -.95), this.bucket, V3(0, .75, .3), 2)];
        this.seatNode = this.root;this.seatOffset = V3(0, 3.15, .3);this.camPitch = -.36;this.rightStickTool = true;// high, at the front glass, looking down: the bucket's edge and corners show
        this.help = 'Wheel loader · R2 throttle, L2 brake / reverse, left stick steer · right stick back / forward: lift / lower, ← roll back, → dump · R3 / V view · □ get out';
        this.pose();this.place();
      }
      pose() {
        this.front.rotation.y = this.art.phi;this.arm.rotation.x = this.liftA;this.bucket.rotation.x = this.tiltA - this.liftA;// the linkage keeps the bucket's angle as the arm rises
        poseRams(this.root, this.rams);this.dirt.visible = this.payload > .15;const s = Math.max(.2, Math.min(1, this.payload / this.capacity));this.dirt.scale.set(1, s, s);
      }
      update(dt, input = {}) {
        let fwd = 0, back = 0, phiT = this.art.phi;
        if (this.active) {
          fwd = input.throttle || 0;back = input.brake || 0;
          phiT = clamp(this.art.phi - (input.steer || 0) * .55 * dt, -.7, .7);
          this.liftA = clamp(this.liftA + dz(input.ry) * .32 * dt, -.08, 1.05);// stick back lifts
          this.tiltA = clamp(this.tiltA - dz(input.rx) * .65 * dt, -1.1, .8);
        }
        this.pose();this.group.updateMatrixWorld(true);
        const L = wpos(this.lip), F = {x: -Math.sin(this.art.yawA), z: -Math.cos(this.art.yawA)}, gl = ground(L.x, L.z);let resist = 0;
        // Scooping: the lip in the soil while driving forward fills the bucket (and the pile pushes back).
        if (field.contains(L.x, L.z, .8) && L.y < gl - .04 && this.speed > .1 && this.payload < this.capacity) {
          const cut = field.cut(tool({x: L.x + F.x * .25, z: L.z + F.z * .25}, F, .3 + this.speed * dt, 1.42, L.y), Math.min(this.capacity - this.payload, (1 + this.speed) * dt * 1.2));
          this.payload += cut;resist = .25 + Math.min(.5, (gl - L.y) * .8);if (cut > 0 && Math.random() < dt * 4) sfx('scrape', .4);
        }
        // Spilling: the bucket's floor tipped more than about 27 degrees below the horizontal.
        if (this.payload > 0) {
          const floor = worldDir(this.bucket, V3(0, 0, -1)), rate = 5.0 * clamp((-.45 - floor.y) / .5, 0, 1);
          if (rate > 0) {const out = Math.min(this.payload, rate * dt);this.payload -= out;this.pourSoil(this.bucket.localToWorld(V3(0, -.1, -1.2)), out, 1.0);if (Math.random() < dt * 3) sfx('dump', .4);}
        }
        this.drive(dt, fwd, back, resist);
        // Articulated motion of the two frames; the root is the rear frame at the hinge.
        this.art.step(this.speed * dt, phiT);const h = this.art.hinge(), ox = this.x, oz = this.z, oyaw = this.yaw;
        this.x = h.x;this.z = h.z;this.yaw = this.art.yawB;
        if (Math.abs(this.speed) > .001 && !this.clear(this.x, this.z)) {this.x = ox;this.z = oz;this.yaw = oyaw;this.art.init(this.x, this.z, this.yaw, this.art.phi);if (Math.abs(this.speed) > 2.5) sfx('hit');this.speed = 0;}
        this.settleBig(dt);this.spin += this.speed * dt;this.spinWheels(this.wheels, .8, dt);this.place();
        this.engineSound(this.active ? this.rpm + resist * 300 : 0, this.active ? Math.max(this.engineLoad, resist) : 0);
      }
      netExtra() {return [this.art.phi, this.liftA, this.tiltA, this.payload];}
      netApply(a) {this.art.phi = a[0] || 0;this.liftA = a[1] || 0;this.tiltA = a[2] || 0;this.payload = a[3] || 0;this.art.init(this.x, this.z, this.yaw, this.art.phi);this.pose();}
      hud() {return `${Math.round(Math.abs(this.speed) * 3.6)} km/h${this.gearText()} · bucket ${this.payload.toFixed(1)} / ${this.capacity.toFixed(1)} m³`;}
    }

    // Dump trucks: the articulated hauler (Volvo A30 class) and the 8x4 tipper.
    class DumpTruck extends SiteMachine {
      constructor(x, z, yaw, articulated) {
        super(x, z, yaw, 1.4, articulated ? 10.5 : 9.2, articulated ? 'site_hauler' : 'site_tipper', articulated ? 'hauler' : 'tipper', articulated ? 'Articulated hauler' : 'Tipper truck');
        const A = this.art_ = articulated;this.verb = 'Drive the';
        Object.assign(this, {width: A ? 2.95 : 2.55, height: 3.4, massKg: A ? 23500 : 14500, powerKw: A ? 265 : 330, vMax: A ? 15 : 22, vRev: 4, gears: A ? 6 : 12, grip: A ? .6 : .55, capacity: A ? 17.5 : 16.0, tip: 0, steerA: 0, spin: 0, wheelBase: 4.2});
        this.bed = this.N('body');this.loadNode = this.N('load');this.rams = [];
        if (A) {
          this.trailer = this.N('trailer');this.wheels = ['fwheels[0]', 'fwheels[1]', 'rwheels[0]', 'rwheels[1]', 'rwheels[2]', 'rwheels[3]'].map(n => this.N(n));
          this.art = new Articulation(2.1, 3.1);this.art.init(this.x, this.z, this.yaw, 0);
          this.seatNode = this.root;this.seatOffset = V3(0, 3.2, -1.65);this.halfL = 3.0;this.halfW = 1.1;
          this.rams.push(this.ram(this.trailer, V3(0, 1.0, 1.4), this.bed, V3(0, .1, -3.0), 0), this.ram(this.trailer, V3(0, 1.0, 1.5), this.bed, V3(0, .1, -2.9), 1));
          this.help = 'Articulated hauler · R2 throttle, L2 brake / reverse, left stick steer · right stick back: tip the body, forward: lower it · R3 / V view · □ get out';
        } else {
          this.wheels = [0, 1, 2, 3].map(i => this.N(`swheels[${i}]`)).concat([0, 1, 2, 3].map(i => this.N(`rwheels[${i}]`)));this.steers = [this.N('steer[0]'), this.N('steer[1]')];
          this.seatNode = this.root;this.seatOffset = V3(-.55, 2.75, -5.45);this.halfL = 3.0;this.halfW = 1.0;this.wheelBase = 4.2;
          this.rams.push(this.ram(this.root, V3(0, 1.2, -3.85), this.bed, V3(0, .05, -5.3), 0));
          this.help = 'Tipper truck · R2 throttle, L2 brake / reverse, left stick steer · right stick back: tip the body, forward: lower it · R3 / V view · □ get out';
        }
        this.rightStickTool = true;this.camPitch = -.05;this.pose();this.place();
      }
      pose() {
        if (this.art_) this.trailer.rotation.y = -this.art.phi;else for (const s of this.steers) s.rotation.y = -this.steerA;
        this.bed.rotation.x = this.tip;this.loadNode.visible = this.payload > .2;const s = Math.max(.15, Math.min(1, this.payload / this.capacity));this.loadNode.scale.set(1, s, .4 + .6 * s);
        poseRams(this.root, this.rams);
      }
      acceptSoil(at, volume) {
        if (this.payload >= this.capacity || this.tip > .1) return 0;
        this.group.updateMatrixWorld(true);const L = this.bed.worldToLocal(at.clone()), len = this.art_ ? 5.0 : 5.5, wd = this.art_ ? 2.95 : 2.5;
        if (L.z > .3 || L.z < -len - .2 || Math.abs(L.x) > wd / 2 + .2 || L.y < -.5 || L.y > 6) return 0;
        const take = Math.min(volume, this.capacity - this.payload);this.payload += take;return take;
      }
      update(dt, input = {}) {
        let fwd = 0, back = 0;
        if (this.active) {fwd = input.throttle || 0;back = input.brake || 0;const ry = dz(input.ry);this.tip = clamp(this.tip + ry * (ry > 0 ? .22 : .3) * dt, 0, this.art_ ? 1.2 : .95);}// stick back tips
        // Tipping: past about 25 degrees the soil slides out at the back.
        if (this.payload > 0 && this.tip > .42) {const out = Math.min(this.payload, (this.tip - .42) * 6 * dt);this.payload -= out;this.group.updateMatrixWorld(true);this.pourSoil(this.bed.localToWorld(V3(0, 0, 1.2)), out, 1.1);}
        if (this.tip > .05) fwd *= .25;// a raised body: only creep
        this.drive(dt, fwd, back);
        const ox = this.x, oz = this.z, oyaw = this.yaw;
        if (this.art_) {
          const phiT = this.active ? clamp(this.art.phi - (input.steer || 0) * .5 * dt, -.75, .75) : this.art.phi;
          this.art.step(this.speed * dt, phiT);const h = this.art.hinge();this.x = h.x;this.z = h.z;this.yaw = this.art.yawA;
        } else {
          const max = .6 * Math.max(.3, 1 - Math.abs(this.speed) / 25);
          this.steerA += ((this.active ? input.steer || 0 : 0) * max - this.steerA) * Math.min(1, dt * 2.5);
          this.yaw -= this.speed * Math.tan(this.steerA) / this.wheelBase * dt;const f = this.forward();this.x += f.x * this.speed * dt;this.z += f.z * this.speed * dt;
        }
        if (Math.abs(this.speed) > .001 && !this.clear(this.x, this.z)) {
          this.x = ox;this.z = oz;this.yaw = oyaw;if (this.art_) this.art.init(this.x, this.z, this.yaw - this.art.phi, this.art.phi);
          if (Math.abs(this.speed) > 2.5) sfx('hit');this.speed = 0;
        }
        this.settleBig(dt);this.spinWheels(this.wheels, this.art_ ? .78 : .52, dt);this.place();this.pose();
        this.engineSound(this.active ? this.rpm : 0, this.active ? this.engineLoad : 0);
        if (this.engine) this.engine.hydraulics(this.active && Math.abs(input.ry || 0) > .12 ? 1 : 0);
      }
      netExtra() {return [this.art_ ? this.art.phi : this.steerA, this.tip, this.payload];}
      netApply(a) {if (this.art_) {this.art.phi = a[0] || 0;this.art.init(this.x, this.z, this.yaw - this.art.phi, this.art.phi);} else this.steerA = a[0] || 0;this.tip = a[1] || 0;this.payload = a[2] || 0;this.pose();}
      hud() {return `${Math.round(Math.abs(this.speed) * 3.6)} km/h${this.gearText()} · load ${this.payload.toFixed(1)} / ${this.capacity.toFixed(1)} m³${this.tip > .03 ? ` · body ${Math.round(this.tip * 180 / Math.PI)}°` : ''}`;}
    }

    // A hook on a wire: a damped pendulum under a moving top point; carries a site load.
    class Hook {
      constructor(wire) {this.p = V3();this.vel = V3();this.wire = wire;this.load = null;}
      step(dt, top) {
        const prev = this.p.clone();this.vel.y -= G * dt;this.p.addScaledVector(this.vel, dt);
        const d = this.p.clone().sub(top), L = d.length();if (L > this.wire) this.p.copy(top).addScaledVector(d.normalize(), this.wire);
        const below = this.load ? this.load.height + .6 : .55, gy = ground(this.p.x, this.p.z) + below;
        if (this.p.y < gy) {this.p.y = gy;this.vel.set(0, 0, 0);}
        this.vel.copy(this.p).sub(prev).multiplyScalar(1 / Math.max(dt, 1e-4) * .992);
        if (this.load) {this.load.p.copy(this.p).sub(V3(0, below, 0));this.load.hooked = true;}
      }
      // ✕: pick up the nearest load under the hook, or set the load down.
      toggle() {
        if (this.load) {this.load.hooked = false;this.load.p.y = ground(this.load.p.x, this.load.p.z);this.load = null;sfx('thud', .5);return true;}
        let best = null, bd = 1.6;
        for (const l of loads) {if (l.hooked) continue;const d = l.p.clone().add(V3(0, l.height, 0)).distanceTo(this.p);if (d < bd) {bd = d;best = l;}}
        if (!best) return false;this.load = best;sfx('carDoor', .4);return true;
      }
    }

    // Three-axle all-terrain crane: drives like a truck; with the outriggers down (L1) it slews, luffs, telescopes and hoists.
    class MobileCrane extends SiteMachine {
      constructor(x, z, yaw) {
        super(x, z, yaw, 1.3, 11.4, 'site_crane', 'crane', 'Mobile crane');this.verb = 'Drive the';
        Object.assign(this, {width: 2.55, height: 3.8, massKg: 36000, powerKw: 330, vMax: 19, vRev: 3, gears: 12, halfL: 2.6, halfW: 1.1, steerA: 0, spin: 0, slew: 0, luff: 0, ext: 0, deploy: 0, deployTarget: 0, prevL1: false});
        this.hook = new Hook(6);
        this.sup = this.N('super');this.boomBase = this.N('boomBase');this.tipNode = this.N('tip');this.hookNode = this.N('hook');this.wireNode = this.N('wire');
        this.tele = [0, 1, 2].map(i => this.N(`tele[${i}]`));this.outriggers = [0, 1, 2, 3].map(i => this.N(`out[${i}]`));
        this.wheels = [0, 1, 2, 3].map(i => this.N(`swheels[${i}]`)).concat([this.N('rwheels[0]'), this.N('rwheels[1]')]);this.steers = [this.N('steer[0]'), this.N('steer[1]')];
        this.rams = [this.ram(this.sup, V3(0, .45, -.6), this.boomBase, V3(0, -.4, -3.6), 0)];
        this.seatNode = this.root;this.seatOffset = V3(-.65, 2.6, -5.0);
        this.help = 'Mobile crane · drive: R2 / L2 / left stick · L1 outriggers down / up · crane: left stick ←→ slew, ↑↓ telescope out / in, right stick back / forward boom up / down, R2 / L2 hoist up / down, ✕ hook or set down a load · R3 / V view · □ get out';
        this.pose();this.place();this.group.updateMatrixWorld(true);this.hook.p.copy(wpos(this.tipNode)).sub(V3(0, this.hook.wire, 0));
      }
      pose() {
        this.sup.rotation.y = this.slew;this.boomBase.rotation.x = this.luff;
        for (let i = 0; i < 3; i++) this.tele[i].position.z = (i === 0 ? -.3 : -.2) - this.ext / 3;
        for (let i = 0; i < 4; i++) {const sx = i % 2 === 0 ? -1 : 1;this.outriggers[i].position.x = sx * (.9 + 2 * Math.min(1, this.deploy * 1.6));this.outriggers[i].position.y = 1.05 - .35 * clamp((this.deploy - .62) / .38, 0, 1);}
        for (const s of this.steers) s.rotation.y = -this.steerA;
        poseRams(this.root, this.rams);
      }
      seat() {
        if (this.deploy > .99) {
          const n = this.seatNode, o = this.seatOffset;this.seatNode = this.sup;this.seatOffset = V3(-1.05, 1.45, -.45);
          const s = super.seat();s.yaw = this.yaw + this.slew;s.pitch = this.pitch + this.luff * .45;this.seatNode = n;this.seatOffset = o;return s;
        }
        return super.seat();
      }
      update(dt, input = {}) {
        let fwd = 0, back = 0;const craneMode = this.deploy > .99;this.rightStickTool = this.deploy > .01;
        if (this.active) {
          // L1 (pressed): outriggers down when stopped, up again (the boom folds down first).
          if (input.l1 && !this.prevL1 && Math.abs(this.speed) < .3) this.deployTarget = this.deployTarget > .5 ? 0 : 1;
          this.prevL1 = !!input.l1;
          if (craneMode) {
            this.slew -= dz(input.lx) * .22 * dt;this.ext = clamp(this.ext - dz(input.ly) * .7 * dt, 0, 23);
            this.luff = clamp(this.luff + dz(input.ry) * .1 * dt, 0, 1.36);// stick back raises the boom
            this.hook.wire = clamp(this.hook.wire - (input.r2 || 0) * 1.2 * dt + (input.l2 || 0) * 1.2 * dt, 1, 48);
            if (input.cross) this.hook.toggle();
          } else if (this.deploy < .01) {fwd = input.throttle || 0;back = input.brake || 0;}
        }
        // Outriggers: out and down over about 4 s; before they go up the boom is lowered and retracted.
        if (this.deployTarget < .5 && (this.luff > .01 || this.ext > .05)) {this.luff = Math.max(0, this.luff - .15 * dt);this.ext = Math.max(0, this.ext - 2 * dt);this.slew *= Math.max(0, 1 - dt);}
        else this.deploy = clamp(this.deploy + (this.deployTarget > .5 ? 1 : -1) * dt / 4, 0, 1);
        this.drive(dt, fwd, back);
        const max = .55 * Math.max(.3, 1 - Math.abs(this.speed) / 20);
        this.steerA += ((this.active && this.deploy < .01 ? input.steer || 0 : 0) * max - this.steerA) * Math.min(1, dt * 2.5);
        this.yaw -= this.speed * Math.tan(this.steerA) / 4.6 * dt;
        this.move(dt);this.settleBig(dt);
        if (this.deploy > .9) {this.pitch *= Math.max(0, 1 - dt * 2);this.terrainRoll *= Math.max(0, 1 - dt * 2);this.roll = this.terrainRoll;}// levelled on the outriggers
        this.spinWheels(this.wheels, .62, dt);this.place();this.pose();this.group.updateMatrixWorld(true);
        const tipW = wpos(this.tipNode);this.hook.step(dt, tipW);poseHook(this.root, this.hookNode, this.wireNode, tipW, this.hook.p);
        this.engineSound(this.active ? (craneMode ? 1100 : this.rpm) : 0, this.active ? (craneMode ? .3 : this.engineLoad) : 0);
      }
      netExtra() {return [this.steerA, this.slew, this.luff, this.ext, this.deploy, this.hook.wire];}
      netApply(a) {[this.steerA, this.slew, this.luff, this.ext, this.deploy] = a.map(v => v || 0);this.hook.wire = Math.max(1, a[5] || 0);this.pose();}
      hud() {
        if (this.deploy < .01) return super.hud();
        if (this.deploy < .99) return `Outriggers ${Math.round(this.deploy * 100)} %`;
        const boomLen = 10.6 + this.ext, reach = boomLen * Math.cos(this.luff);
        return `Boom ${boomLen.toFixed(1)} m at ${Math.round(this.luff * 180 / Math.PI)}° · radius ${reach.toFixed(1)} m · hook ${this.hook.wire.toFixed(1)} m${this.hook.load ? ` · load ${(this.hook.load.mass / 1000).toFixed(1)} t` : ''}`;
      }
    }

    // Tower crane: 30 m mast, 40 m jib; climb in at the foot of the mast.
    class TowerCrane extends SiteMachine {
      constructor(x, z, yaw) {
        super(x, z, yaw, 1.2, 2.2, 'site_tower', 'tower', 'Tower crane');
        Object.assign(this, {width: 2.2, height: 32, massKg: 1e9, slew: 0, slewV: 0, trolley: 14, cullDistance: 900});
        this.slewNode = this.N('slew');this.trolleyNode = this.N('trolley');this.hookNode = this.N('hook');this.wireNode = this.N('wire');
        this.seatNode = this.slewNode;this.seatOffset = V3(-1.4, 1.35, -1.75);this.camPitch = -.45;this.rightStickTool = true;
        this.help = 'Tower crane · left stick ←→ slew, ↑↓ trolley out / in · right stick back / forward hoist up / down · ✕ hook or set down a load · R3 / V view · □ climb down';
        this.hook = new Hook(20);this.pose();this.place();this.group.updateMatrixWorld(true);this.hook.p.copy(wpos(this.trolleyNode)).sub(V3(0, this.hook.wire, 0));
      }
      pose() {this.slewNode.rotation.y = this.slew;this.trolleyNode.position.z = -this.trolley;}
      seat() {const s = super.seat();s.yaw = this.yaw + this.slew;s.pitch = 0;s.roll = 0;return s;}
      enter() {this.active = true;audio.sfx('carDoor');this.engine = audio.engine('diesel');}
      exit() {this.active = false;if (this.engine) this.engine.stop();this.engine = null;return {x: this.x + 2.2, z: this.z + 2.2};}
      footprint() {return {points: [[1, 1], [1, -1], [-1, -1], [-1, 1]].map(([a, b]) => ({x: this.x + a * .9, z: this.z + b * .9})), minY: this.y - .2, maxY: this.y + 30, kind: 'vehicle'};}
      update(dt, input = {}) {
        if (this.active) {
          // Slewing has inertia: the jib speeds up and slows down gently (about 0.7 rpm at full stick).
          this.slewV += (-dz(input.lx) * .075 - this.slewV) * Math.min(1, dt * .8);
          this.trolley = clamp(this.trolley - dz(input.ly) * .9 * dt, 3.5, 38.5);
          this.hook.wire = clamp(this.hook.wire - dz(input.ry) * 1.1 * dt, 1.5, 31);// stick back hoists up
          if (input.cross) this.hook.toggle();
        } else this.slewV *= Math.max(0, 1 - dt);
        this.slew += this.slewV * dt;this.speed = 0;this.pose();this.place();this.group.updateMatrixWorld(true);
        const top = wpos(this.trolleyNode).sub(V3(0, .35, 0));this.hook.step(dt, top);poseHook(this.root, this.hookNode, this.wireNode, top, this.hook.p);
        if (this.engine) this.engine.set(this.active ? 800 + Math.abs(this.slewV) * 4000 : 0, .1);
      }
      netExtra() {return [this.slew, this.trolley, this.hook.wire];}
      netApply(a) {this.slew = a[0] || 0;this.trolley = Math.max(3.5, a[1] || 0);this.hook.wire = Math.max(1.5, a[2] || 0);this.pose();}
      hud() {return `Radius ${this.trolley.toFixed(1)} m · hook ${this.hook.wire.toFixed(1)} m below the jib${this.hook.load ? ` · load ${(this.hook.load.mass / 1000).toFixed(1)} t` : ''}`;}
    }

    // The machines in site-data order (excavator, hauler, tipper, dozer, loader, crane, tower).
    for (const p of field.machines) {
      const make = {dozer: () => new Dozer(p.x, p.z, p.yaw), excavator: () => new SiteExcavator(p.x, p.z, p.yaw), loader: () => new WheelLoader(p.x, p.z, p.yaw), hauler: () => new DumpTruck(p.x, p.z, p.yaw, true),
        tipper: () => new DumpTruck(p.x, p.z, p.yaw, false), crane: () => new MobileCrane(p.x, p.z, p.yaw), tower: () => new TowerCrane(p.x, p.z, p.yaw)}[p.kind];
      if (make) {const m = make();m.settleBig(1);m.place();m.pose();}
    }
    // Crane loads: brick pallets, a bundle of steel beams, a concrete skip.
    for (const p of field.loads) {
      const mesh = models.load(p.kind) || new T.Group();group.add(mesh);
      loads.push({kind: p.kind, yaw: p.yaw, p: V3(p.x, ground(p.x, p.z), p.z), mass: p.kind === 'beams' ? 2400 : p.kind === 'skip' ? 900 : 1100, height: p.kind === 'beams' ? .65 : p.kind === 'skip' ? 1.5 : 1.15, hooked: false, mesh});
    }
    function syncLoads() {for (const l of loads) {l.mesh.position.copy(l.p);l.mesh.rotation.y = l.yaw;}}
    syncLoads();
    // Rebuild a few changed soil tiles a frame (digging touches one or two; the slumping after a dump may touch more).
    function update(dt) {
      field.step(dt);
      let budget = 6;for (const t of [...field.dirty]) {if (budget-- <= 0) break;buildTile(t % field.tilesX, Math.floor(t / field.tilesX));field.dirty.delete(t);}
      syncLoads();
    }
    return {field, group, machines, loads, obstacles, update, contains: (x, z, m) => field.contains(x, z, m), surface: (x, z) => field.surface(x, z)};
  }
  return {DirtField, fieldFor, fenceObstacles, create};
});
