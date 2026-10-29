const test = require('node:test');
const assert = require('node:assert/strict');
// A tiny localStorage so the wallet can save and load like in the browser.
const store = {};
globalThis.localStorage = {getItem: k => (k in store ? store[k] : null), setItem: (k, v) => {store[k] = String(v);}};
globalThis.window = globalThis;
require('../surroundings-data.js');
require('../game-shops.js');
const S = globalThis.GameShops;

test('beer is sold Mon–Fri 08–20 and Sat 08–18, never on Sunday', () => {
  const at = iso => S.beerAllowed(new Date(iso));
  assert.equal(at('2026-10-29T07:59:00'), false);// Thursday before 08
  assert.equal(at('2026-10-29T08:00:00'), true);
  assert.equal(at('2026-10-29T19:59:00'), true);
  assert.equal(at('2026-10-29T20:00:00'), false);
  assert.equal(at('2026-10-31T17:59:00'), true);// Saturday
  assert.equal(at('2026-10-31T18:00:00'), false);
  assert.equal(at('2026-11-01T12:00:00'), false);// Sunday
});

test('Kiwi is a little cheaper than Joker and prices still end in ,90', () => {
  const joker = S.SHOPS.find(s => s.id === 'joker'), kiwi = S.SHOPS.find(s => s.id === 'kiwi');
  for (const p of S.PRODUCTS) {
    const j = S.priceOf(joker, p), k = S.priceOf(kiwi, p);
    assert.equal(j, p.price);assert.ok(k <= j, `${p.id}: ${k} > ${j}`);
    assert.equal(Math.round(k * 100) % 100, 90, `${p.id}: ${k}`);
  }
});

test('the wallet pays, refuses what you cannot afford, counts bottles and keeps the bag', () => {
  for (const k in store) delete store[k];
  const w = S.makeWallet();
  assert.equal(w.money, 500);
  assert.equal(w.pay(196.6), true);assert.equal(w.money, 303.4);
  assert.equal(w.pay(400), false);assert.equal(w.money, 303.4);
  assert.equal(w.pay(NaN), false);assert.equal(w.pay(undefined), false);assert.equal(w.money, 303.4);
  w.earn(NaN);w.earn(-5);assert.equal(w.money, 303.4);
  w.addPant(3);assert.equal(w.cashPant(), 3);assert.equal(w.pant, 0);assert.equal(w.money, 303.4 + 3 * S.PANT);
  w.add('pils', 2);assert.equal(w.take('pils'), true);assert.deepEqual(w.bag, {pils: 1});
  assert.equal(w.take('kaffe'), false);
  // Saved in the browser: a new wallet loads the same state; a broken save starts over at 500 kr.
  const again = S.makeWallet();assert.equal(again.money, w.money);assert.deepEqual(again.bag, {pils: 1});
  store['haarklous-wallet-v1'] = '{"money":null,"pant":0,"bag":{}}';assert.equal(S.makeWallet().money, 500);
});

test('each shop is cut into its real building as a notch the size of a small supermarket', () => {
  const D = JSON.parse(JSON.stringify(globalThis.SURROUNDINGS));
  const before = S.SHOPS.map(s => D.buildings[s.building].r.o.length);
  S.applyNotches(D);
  const ringOf = o => {const r = [];for (let i = 0; i < o.length; i += 2) r.push([o[i], o[i + 1]]);return r;};
  const crosses = (a, b, c, d) => {const o = (p, q, r) => Math.sign((q[0] - p[0]) * (r[1] - p[1]) - (q[1] - p[1]) * (r[0] - p[0]));return o(a, b, c) * o(a, b, d) < 0 && o(c, d, a) * o(c, d, b) < 0;};
  S.SHOPS.forEach((s, i) => {
    const nt = s.local, ring = ringOf(D.buildings[s.building].r.o);
    assert.equal(ring.length, before[i] / 2 + 4, `${s.id}: four corners added`);
    assert.ok(nt.W >= 8 && nt.W <= 14, `${s.id}: shop front ${nt.W.toFixed(1)} m`);
    assert.ok(nt.depth >= 8, `${s.id}: shop depth ${nt.depth} m`);
    // The new outline must not cross itself (the walls and the collision outline follow it).
    for (let a = 0; a < ring.length; a++) for (let b = a + 2; b < ring.length; b++) {
      if (a === 0 && b === ring.length - 1) continue;
      assert.ok(!crosses(ring[a], ring[(a + 1) % ring.length], ring[b], ring[(b + 1) % ring.length]), `${s.id}: edges ${a} and ${b} cross`);
    }
  });
  // Real places: Joker about 110 m and Kiwi about 270 m from the flat (local map origin).
  const dist = s => Math.hypot((s.local.P0[0] + s.local.P1[0]) / 2, (s.local.P0[1] + s.local.P1[1]) / 2);
  const joker = S.SHOPS.find(s => s.id === 'joker'), kiwi = S.SHOPS.find(s => s.id === 'kiwi');
  assert.ok(dist(joker) > 80 && dist(joker) < 150, `Joker ${dist(joker).toFixed(0)} m`);
  assert.ok(dist(kiwi) > 220 && dist(kiwi) < 320, `Kiwi ${dist(kiwi).toFixed(0)} m`);
});
