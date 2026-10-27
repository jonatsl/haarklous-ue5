/* Runs inside haarklous-game.html (tools/web-export/export.mjs): reference answers from the web version's own code,
 * written to unreal/Haarklous/Content/Data/tests. The C++ automation tests (HKTests.cpp) load these and check that the
 * Unreal port gives the same results (collision, terrain heights, surfaces, vehicle placement).
 */
(() => {
  'use strict';
  const G = window.haarklousGame, C = window.WalkCollision, W = G.world, ww = G.walkWorld, files = window.__hkFiles, log = window.__hkLog;
  let seed = 4242;const rnd = () => (seed = (seed * 16807) % 2147483647) / 2147483647, between = (a, b) => a + (b - a) * rnd();
  const BIG = 1e9, num = v => v === Infinity ? BIG : v === -Infinity ? -BIG : v;
  const box = pts => ({x0: Math.min(...pts.map(p => p.x)), x1: Math.max(...pts.map(p => p.x)), z0: Math.min(...pts.map(p => p.z)), z1: Math.max(...pts.map(p => p.z))});
  const aptBox = box(G.apt.floors.flatMap(f => f.points)), stairBox = box(G.towers[0].stair.floors().flatMap(f => f.points));
  const stairYs = [...new Set(G.towers[0].stair.floors().map(f => f.y ?? 0))];
  // Sample points where the queries are interesting: the flat, the own stairwell, the street and the neighbourhood.
  function samplePoint() {
    const r = rnd();
    if (r < .35) return {x: between(aptBox.x0 - .5, aptBox.x1 + .5), z: between(aptBox.z0 - .5, aptBox.z1 + .5), y: rnd() < .8 ? 0 : between(-.4, .4)};
    if (r < .55) return {x: between(stairBox.x0 - .5, stairBox.x1 + .5), z: between(stairBox.z0 - .5, stairBox.z1 + .5), y: stairYs[Math.floor(rnd() * stairYs.length)] + between(-.3, .6)};
    if (r < .8) {const x = between(-40, 60), z = between(-30, 60);return {x, z, y: W.heightWorld(x, z) + between(-.2, .3)};}
    const x = between(-500, 500), z = between(-500, 500);return {x, z, y: W.heightWorld(x, z) + between(-.2, .3)};
  }
  const out = {dynamic: (ww.dynamic || []).map(o => ({points: o.points.map(p => [num(p.x), num(p.z)]), minY: num(o.minY ?? -Infinity), maxY: num(o.maxY ?? Infinity)})), heights: [], support: [], move: [], hits: [], spawn: [], surface: [], traffic: [], green: [], local: []};
  for (let i = 0; i < 3000; i++) {const x = between(-650, 650), z = between(-650, 650);out.heights.push([num(x), num(z), num(W.heightWorld(x, z))]);}
  for (let i = 0; i < 3000; i++) {
    const p = samplePoint(), s = C.supportAt(ww, p, p.y), eye = [1.65, 1.05, .32][i % 3];
    out.support.push({p: [num(p.x), num(p.z), num(p.y)], eye, support: s ? num(s.y) : null, stand: C.canStand(ww, p, eye, p.y), ceiling: num(C.ceilingAt(ww, p, p.y)), clamp: (() => {const c = C.clampEyeHeight(ww, p, eye, .3, p.y);return c === null ? null : num(c);})()});
  }
  let moves = 0;
  for (let tries = 0; moves < 2500 && tries < 60000; tries++) {
    const p = samplePoint(), eye = [1.65, 1.65, 1.05, .32][moves % 4], s = C.supportAt(ww, p, p.y);
    if (!s || !C.canStand(ww, p, eye, s.y)) continue;
    const a = rnd() * Math.PI * 2, L = rnd() < .7 ? between(.02, .25) : between(.25, 3), d = {x: Math.cos(a) * L, z: Math.sin(a) * L};
    const r = C.move(ww, {x: p.x, z: p.z}, d, eye, s.y);
    out.move.push({p: [num(p.x), num(p.z), num(s.y)], d: [num(d.x), num(d.z)], eye, r: [num(r.x), num(r.z), num(r.y)]});moves++;
  }
  for (let i = 0; i < 2000; i++) {const p = samplePoint(), y0 = p.y + between(-1, 3), y1 = y0 + between(.05, 2);out.hits.push({p: [num(p.x), num(p.z)], y0: num(y0), y1: num(y1), hit: C.hitsSolid(ww, p, y0, y1)});}
  for (let i = 0; i < 150; i++) {const p = samplePoint(), r = C.findSafeSpawn(ww, p, 1.65, {y: p.y, maxDistance: 6});out.spawn.push({p: [num(p.x), num(p.z), num(p.y)], r: r ? [num(r.x), num(r.z), num(r.y)] : null});}
  for (let i = 0; i < 3000; i++) {const x = between(-560, 560), z = between(-560, 560);out.surface.push([num(x), num(z), W.surfaceAt(x, z)]);}
  for (let i = 0; i < 400; i++) {const x = between(-400, 400), z = between(-400, 400);out.traffic.push([num(x), num(z), num(W.trafficDistance(x, z))]);out.green.push([num(x), num(z), num(W.greenery(x, z))]);}
  for (let i = 0; i < 200; i++) {const x = between(-600, 600), z = between(-600, 600), l = W.toLocal(x, z), w = W.toWorld(l.x, l.s);out.local.push([num(x), num(z), num(l.x), num(l.s), num(w.x), num(w.z)]);}
  files.collision = {json: JSON.stringify(out), bin: null};
  log.push(`reference tests: ${out.support.length} support, ${out.move.length} moves, ${out.hits.length} hits, ${out.spawn.length} spawns, ${out.heights.length} heights`);
  window.__hkDone = true;
})();
