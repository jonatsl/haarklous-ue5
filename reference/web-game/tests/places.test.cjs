const test = require('node:test');
const assert = require('node:assert/strict');
globalThis.window = globalThis;
require('../surroundings-data.js');
require('../site-data.js');
require('../paintball-data.js');
require('../game-sculpture.js');
const D = window.SURROUNDINGS, SITE = window.SITE, PB = window.PAINTBALL;

// HODET N.N. at its real place (OSM node 2965223021) with its steps on the east side: a box round both, local metres.
const SCULPTURE = {x0: 9.1 - 3, s0: 275.5 - 3, x1: 9.1 + 10, s1: 275.5 + 4};
const box = r => ({x0: r.x0, s0: r.s0, x1: r.x0 + r.w, s1: r.s0 + r.d});
const places = {site: box(SITE.rect), yard: box(PB.rect), lobby: box(PB.lobby), sculpture: SCULPTURE};
const pairs = f => {const o = [];for (let i = 0; i < f.length; i += 2) o.push([f[i], f[i + 1]]);return o;};
const overlap = (a, b) => a.x0 < b.x1 && b.x0 < a.x1 && a.s0 < b.s1 && b.s0 < a.s1;
const inside = (r, [x, s], m = 0) => x > r.x0 - m && x < r.x1 + m && s > r.s0 - m && s < r.s1 + m;
// Does the segment a-b come within m metres of the box? (Sampled every 0.5 m; boxes are tens of metres.)
const segNear = (r, a, b, m) => {const L = Math.hypot(b[0] - a[0], b[1] - a[1]), n = Math.max(1, Math.ceil(L / .5));for (let k = 0; k <= n; k++) if (inside(r, [a[0] + (b[0] - a[0]) * k / n, a[1] + (b[1] - a[1]) * k / n], m)) return true;return false;};

test('the sculpture stands at its real place (OSM node 2965223021)', () => {
  const lat = 59.9339293, lon = 10.7768206, x = (lon - D.origin.lon) * 111320 * Math.cos(D.origin.lat * Math.PI / 180), s = -(lat - D.origin.lat) * 111320;
  assert.ok(Math.abs(x - 9.1) < .1 && Math.abs(s - 275.5) < .1, `${x.toFixed(2)}, ${s.toFixed(2)}`);
});

test('construction site, paintball yard, lobby and sculpture do not overlap each other', () => {
  const k = Object.keys(places);
  for (let i = 0; i < k.length; i++) for (let j = i + 1; j < k.length; j++) if (!(k[i] === 'yard' && k[j] === 'lobby')) assert.ok(!overlap(places[k[i]], places[k[j]]), `${k[i]} overlaps ${k[j]}`);
});

test('no building and no car road crosses the site, the paintball course or the sculpture', () => {
  const carRoads = ['primary', 'secondary', 'tertiary', 'residential', 'service', 'unclassified', 'living_street', 'trunk', 'tram'];
  for (const [name, r] of Object.entries(places)) {
    D.buildings.forEach((b, i) => {
      const ring = pairs(b.r.o);
      assert.ok(!ring.some((p, k) => segNear(r, p, ring[(k + 1) % ring.length], 0)), `building ${i} crosses the ${name}`);
    });
    D.roads.filter(rd => carRoads.includes(rd.k)).forEach(rd => {
      const p = pairs(rd.p);
      for (let k = 0; k + 1 < p.length; k++) assert.ok(!segNear(r, p[k], p[k + 1], rd.w / 2), `a ${rd.k} road crosses the ${name}`);
    });
  }
});

test("the doll's face: symmetric, the nose stands out most, nothing on the back of the head", () => {
  const {headShape} = window.GameSculpture;
  for (const [x, y, z] of [[.3, -.1, .9], [.5, -.4, .7], [.2, .5, .8]]) assert.ok(Math.abs(headShape(x, y, z) - headShape(-x, y, z)) < 1e-12);
  const nose = headShape(0, -.3, Math.sqrt(1 - .09));
  for (let u = -1.2; u <= 1.2; u += .1) for (let v = -.9; v <= .9; v += .1) {const c = Math.sqrt(Math.max(0, 1 - v * v));assert.ok(headShape(c * Math.sin(u), v, c * Math.cos(u)) <= nose + 1e-9);}
  assert.equal(headShape(0, 0, -1), 0);
});
