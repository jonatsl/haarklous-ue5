const test = require('node:test');
const assert = require('node:assert/strict');
globalThis.window = globalThis;
require('../surroundings-data.js');
require('../site-data.js');
const S = require('../game-site.js');

// The game's terrain functions (as surroundings.js computes them), without three.js.
const D = window.SURROUNDINGS, th = D.geo.theta, c = Math.cos(th), s = Math.sin(th);
const toWorld = (x, z) => ({x: x * c + z * s + D.geo.x, z: -x * s + z * c + D.geo.z});
const toLocal = (x, z) => {const dx = x - D.geo.x, dz = z - D.geo.z;return {x: dx * c - dz * s, s: dx * s + dz * c};};
const N = D.terrain.n, half = D.terrain.half, step = D.terrain.step, buf = Buffer.from(D.terrain.dm, 'base64'), grid = new Int16Array(buf.buffer, buf.byteOffset, buf.length / 2);
const rel = (x, z) => {const fi = Math.min(N - 1.001, Math.max(0, (x + half) / step)), fj = Math.min(N - 1.001, Math.max(0, (z + half) / step)), i = Math.floor(fi), j = Math.floor(fj), u = fi - i, v = fj - j;return (grid[j * N + i] * (1 - u) * (1 - v) + grid[j * N + i + 1] * u * (1 - v) + grid[(j + 1) * N + i] * (1 - u) * v + grid[(j + 1) * N + i + 1] * u * v) / 10;};
const heightWorld = (x, z) => {const l = toLocal(x, z);return D.ground + rel(l.x, l.s);};
const field = () => S.fieldFor(window.SITE, {toWorld, toLocal, heightWorld});

test('the soil meets the terrain at the fence and holds the pit and the stockpiles', () => {
  const f = field(), r = window.SITE.rect;
  for (const [x, z] of [[r.x0 + 1, r.s0 + 1], [r.x0 + r.w - 1, r.s0 + r.d / 2], [r.x0 + r.w / 2, r.s0 + r.d - 1]]) {
    const w = toWorld(x, z);assert.ok(Math.abs(f.surface(w.x, w.z) - heightWorld(w.x, w.z)) < .3, `edge at local (${x}, ${z})`);
  }
  const p = window.SITE.pit, pw = toWorld(p.x, p.s);
  assert.ok(f.surface(pw.x, pw.z) < window.SITE.padLevel - 2, 'the foundation pit is 2.6 m below the pad');
  assert.equal(f.surface(0, 0), null, 'outside the site there is no soil');
});

test('digging, carrying and dumping conserve the soil, and loose soil settles at 34 degrees', () => {
  const f = field(), v0 = f.volume(), m = window.SITE.machines[0], fwd = {x: 1, z: 0};
  let carried = 0;
  for (let k = 0; k < 20; k++) {const y = f.surface(m.x + 3, m.z);carried += f.cut({c: {x: m.x + 3, z: m.z}, f: fwd, halfL: .4, halfW: .7, bottom: y - .3}, .2);}
  assert.ok(carried > 1, `dug ${carried.toFixed(2)} m3`);
  assert.ok(Math.abs(f.volume() + carried - v0) < 1e-3, 'what is cut is what the bucket carries');
  f.deposit({c: {x: m.x + 8, z: m.z + 2}, f: fwd, halfL: .3, halfW: .3, bottom: 0}, carried);
  for (let k = 0; k < 600; k++) f.step(1 / 30);
  assert.ok(Math.abs(f.volume() - v0) < 1e-2, `volume after the heap settled: ${(f.volume() - v0).toFixed(4)}`);
  // No step between neighbours is steeper than the angle of repose where the soil is loose.
  let steep = 0;
  for (let j = 0; j < f.ns; j++) for (let i = 0; i + 1 < f.nx; i++) {const k = j * f.nx + i, d = Math.abs(f.H[k] - f.H[k + 1]), hi = f.H[k] > f.H[k + 1] ? k : k + 1;if (d > f.cell * .6745 + .02 && f.H[hi] - f.Firm[hi] > .02) steep++;}
  assert.equal(steep, 0);
});

test('nothing can be dug below rock', () => {
  const f = field(), m = window.SITE.machines[0];let total = 0;
  for (let k = 0; k < 200; k++) total += f.cut({c: {x: m.x + 3, z: m.z}, f: {x: 0, z: 1}, halfL: .3, halfW: .3, bottom: -1000}, 5);
  const l = toLocal(m.x + 3, m.z), i = Math.round((l.x - f.x0) / f.cell), j = Math.round((l.s - f.s0) / f.cell), k = j * f.nx + i;
  assert.ok(f.H[k] >= f.Rock[k] - 1e-4 && total > 0);
});
