/* Extra export (runs after exporter.js): surface masks for the Unreal ground material, painted from the same map data
 * and in the same order as surroundings.js paint(): R = asphalt (streets, parking), G = gravel (paths, yards, petanque,
 * tee pads), B = sand (playgrounds); black = grass. Near: 4096 px over +-300 m, far: 2048 px over +-600 m (local frame,
 * like the web's terrain textures, so they map onto the terrain meshes' UVs).
 */
(() => {
  'use strict';
  const X = window.__hkExport, D = window.haarklousGame.world.data;
  const pairs = flat => {const out = [];for (let i = 0; i < flat.length; i += 2) out.push([flat[i], flat[i + 1]]);return out;};
  const roadOrder = ['footway', 'path', 'cycleway', 'steps', 'pedestrian', 'service', 'track', 'living_street', 'unclassified', 'residential', 'tertiary', 'secondary', 'primary', 'trunk_link', 'trunk', 'tram'];
  const isPath = k => ['footway', 'path', 'steps', 'cycleway', 'pedestrian', 'track'].includes(k);
  const C = {grass: '#000000', asphalt: '#ff0000', gravel: '#00ff00', sand: '#0000ff', none: '#ff0000'};
  function mask(size, ext) {
    const c = document.createElement('canvas');c.width = c.height = size;const ctx = c.getContext('2d'), s = size / (2 * ext), P = (x, y) => [(x + ext) * s, (y + ext) * s];
    ctx.fillStyle = C.grass;ctx.fillRect(0, 0, size, size);
    const ring = (flat, color) => {ctx.fillStyle = color;ctx.beginPath();pairs(flat).forEach((p, i) => {const [x, y] = P(p[0], p[1]);i ? ctx.lineTo(x, y) : ctx.moveTo(x, y);});ctx.closePath();ctx.fill();};
    for (const a of D.areas) if (a.k !== 'grass' && a.k !== 'wood') ring(a.o, a.k === 'parking' ? C.asphalt : a.k === 'playground' ? C.sand : a.k === 'yard' ? C.gravel : C.grass);
    for (const r of D.roads.slice().sort((a, b) => roadOrder.indexOf(a.k) - roadOrder.indexOf(b.k))) {
      ctx.strokeStyle = isPath(r.k) ? C.gravel : C.asphalt;ctx.lineWidth = r.w * s;ctx.lineCap = ctx.lineJoin = 'round';
      ctx.beginPath();pairs(r.p).forEach((p, i) => {const [x, y] = P(p[0], p[1]);i ? ctx.lineTo(x, y) : ctx.moveTo(x, y);});ctx.stroke();
    }
    const yard = D.courtyard;
    if (yard) {ctx.lineCap = 'round';ctx.strokeStyle = C.gravel;ctx.lineWidth = yard.pathWidth * s;ctx.beginPath();pairs(yard.path).forEach((p, i) => {const [x, y] = P(p[0], p[1]);i ? ctx.lineTo(x, y) : ctx.moveTo(x, y);});ctx.stroke();if (yard.playground) ring(yard.playground.o, C.sand);}
    if (D.petanque) {const p = D.petanque, cx = Math.cos(p.rot), sx = Math.sin(p.rot);ring([[-1, -1], [1, -1], [1, 1], [-1, 1]].flatMap(([a, b]) => [p.x + cx * a * (p.length / 2 + .3) - sx * b * (p.width / 2 + .3), p.s + sx * a * (p.length / 2 + .3) + cx * b * (p.width / 2 + .3)]), C.gravel);}
    for (const h of D.discgolf || []) {const [tx, ts] = h.tee;ctx.fillStyle = C.gravel;ctx.beginPath();const [px, py] = P(tx, ts);ctx.arc(px, py, 1.4 * s, 0, 7);ctx.fill();}
    for (const b of D.buildings) ring(b.r.o, C.none);
    return c.toDataURL('image/png');
  }
  X.files.tex_ground_near = {json: mask(4096, 300), bin: null};
  X.files.tex_ground_far = {json: mask(2048, 600), bin: null};
  X.log.push('ground masks: near 4096 px / +-300 m, far 2048 px / +-600 m');
})();
