const test = require('node:test');
const assert = require('node:assert/strict');
globalThis.window = globalThis;
require('../surroundings-data.js');
require('../muselunden-data.js');
const D = window.SURROUNDINGS, M = window.MUSELUNDEN;
const pairs = f => {const o = [];for (let i = 0; i < f.length; i += 2) o.push([f[i], f[i + 1]]);return o;};
const inside = (ring, [x, s]) => {let c = false;for (let i = 0, j = ring.length - 1; i < ring.length; j = i++) {const [xi, si] = ring[i], [xj, sj] = ring[j];if ((si > s) !== (sj > s) && x < (xj - xi) * (s - si) / (sj - si) + xi) c = !c;}return c;};
const segDist = ([x, s], [ax, as], [bx, bs]) => {const dx = bx - ax, ds = bs - as, L = dx * dx + ds * ds || 1, t = Math.max(0, Math.min(1, ((x - ax) * dx + (s - as) * ds) / L));return Math.hypot(x - ax - t * dx, s - as - t * ds);};
const ringDist = (ring, p) => Math.min(...ring.map((a, i) => segDist(p, a, ring[(i + 1) % ring.length])));

test('Muselunden: 12 holes of the 2022 layout, par 36, the UDisc lengths', () => {
  const udisc = [106.3, 53, 68, 63, 35, 50, 65, 64, 76, 77, 52, 47];
  assert.equal(M.holes.length, 12);assert.equal(M.par, 36);
  M.holes.forEach((h, i) => {
    assert.equal(h.n, i + 1);assert.equal(h.par, 3);
    assert.ok(Math.abs(Math.hypot(h.basket[0] - h.tee[0], h.basket[1] - h.tee[1]) - udisc[i]) < .15, `hole ${h.n}`);
  });
});

test('every tee and basket is in the course (OSM 785654764), in the walkable world, not in a building or on a car road', () => {
  const course = pairs(M.outline), carRoads = ['primary', 'secondary', 'tertiary', 'residential', 'service', 'unclassified', 'trunk', 'tram'];
  const half = D.terrain.half - 40;// the walkable square (surroundings.js PLAY_HALF = 760) inside the terrain data (±800)
  for (const h of M.holes) for (const [what, p] of [['tee', h.tee], ['basket', h.basket]]) {
    const name = `hole ${h.n} ${what}`;
    assert.ok(inside(course, p) || ringDist(course, p) < 10, `${name} is ${ringDist(course, p).toFixed(1)} m outside the course`);
    assert.ok(Math.abs(p[0]) < half && Math.abs(p[1]) < half, `${name} is outside the world`);
    D.buildings.forEach((b, i) => assert.ok(!inside(pairs(b.r.o), p), `${name} is in building ${i}`));
    for (const r of D.roads.filter(r => carRoads.includes(r.k))) {const q = pairs(r.p);for (let k = 0; k + 1 < q.length; k++) assert.ok(segDist(p, q[k], q[k + 1]) > r.w / 2 + 1, `${name} is on a ${r.k} road`);}
  }
});
