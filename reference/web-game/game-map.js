/* Map overlay (touchpad / M): the neighbourhood from OpenStreetMap with street names, your apartment, the
 * hostage-rescue start, the disc golf holes and the petanque court, and you (arrow). Vehicles are not shown.
 * Left stick / drag pans, right stick / wheel zooms, ○ / M / Esc closes.
 * GameMap.create({data, toLocal, toWorld, root}) -> {open, toggle(), draw(player), pan(dx, dy), zoom(f)}
 */
(function (root, factory) {
  root.GameMap = factory();
})(typeof window === 'undefined' ? globalThis : window, function () {
  'use strict';
  function create({data, toLocal, toWorld, root, poi}) {
    const el = document.createElement('div');el.id = 'mapOverlay';el.hidden = true;
    el.innerHTML = '<canvas></canvas><div class="map-legend"><b>Map</b><span><i class="lg home"></i>Your apartment (Haarklous plass 17A)</span><span><i class="lg case"></i>Hostage rescue</span><span><i class="lg disc"></i>Disc golf</span><span><i class="lg petanque"></i>Petanque</span><span><i class="lg you"></i>You</span><small>Left stick / drag: pan · Right stick / wheel: zoom · ○ / M: close</small><small class="attr">© OpenStreetMap contributors</small></div>';
    root.append(el);
    const canvas = el.querySelector('canvas'), ctx = canvas.getContext('2d');
    const map = {open: false, cx: 0, cs: 0, scale: 2.2};
    const pairs = flat => {const out = [];for (let i = 0; i < flat.length; i += 2) out.push([flat[i], flat[i + 1]]);return out;};
    function draw(player) {
      if (!map.open) return;
      const w = canvas.width = el.clientWidth * devicePixelRatio, h = canvas.height = el.clientHeight * devicePixelRatio, k = map.scale * devicePixelRatio;
      const P = (x, s) => [w / 2 + (x - map.cx) * k, h / 2 + (s - map.cs) * k];
      ctx.fillStyle = '#e8e4d8';ctx.fillRect(0, 0, w, h);
      const poly = (flat, fill, stroke) => {ctx.beginPath();pairs(flat).forEach(([x, s], i) => {const [u, v] = P(x, s);i ? ctx.lineTo(u, v) : ctx.moveTo(u, v);});ctx.closePath();if (fill) {ctx.fillStyle = fill;ctx.fill();}if (stroke) {ctx.strokeStyle = stroke;ctx.lineWidth = 1;ctx.stroke();}};
      for (const a of data.areas) poly(a.o, {grass: '#b9d39b', wood: '#9cbf83', playground: '#e8d9a8', pitch: '#a8cf8f', parking: '#d6d3cc', yard: '#ddd6c4', flowerbed: '#c9d6a0', water: '#a8c8e0'}[a.k] || '#ddd');
      const order = ['footway', 'path', 'cycleway', 'steps', 'pedestrian', 'service', 'track', 'living_street', 'unclassified', 'residential', 'tertiary', 'secondary', 'primary', 'trunk_link', 'trunk', 'tram'];
      for (const r of data.roads.slice().sort((a, b) => order.indexOf(a.k) - order.indexOf(b.k))) {
        const path = ['footway', 'path', 'steps', 'cycleway', 'track', 'pedestrian'].includes(r.k);
        ctx.strokeStyle = path ? '#c9a77b' : r.k === 'tram' ? '#888' : '#ffffff';ctx.lineWidth = Math.max(1, (path ? 1.4 : r.w) * k * .9);ctx.setLineDash(path ? [4 * devicePixelRatio, 3 * devicePixelRatio] : []);
        ctx.lineCap = 'round';ctx.beginPath();pairs(r.p).forEach(([x, s], i) => {const [u, v] = P(x, s);i ? ctx.lineTo(u, v) : ctx.moveTo(u, v);});ctx.stroke();
        if (!path && r.k !== 'tram') {ctx.strokeStyle = '#bfb9ad';ctx.lineWidth = 1;ctx.stroke();}
      }
      ctx.setLineDash([]);
      for (const b of data.buildings) poly(b.r.o, b.s === 'brick' ? '#c9a08a' : '#c8c2b8', '#a8826e');
      poly(data.ownLocal, '#e0703c', '#7a2e10');
      // Street names (one label per named street, at its longest segment).
      ctx.font = `${12 * devicePixelRatio}px system-ui, sans-serif`;ctx.fillStyle = '#4a4a4a';ctx.textAlign = 'center';
      const named = new Map();for (const r of data.roads) if (r.n && !['footway', 'path', 'steps', 'cycleway'].includes(r.k)) {const p = pairs(r.p);for (let i = 1; i < p.length; i++) {const L = Math.hypot(p[i][0] - p[i - 1][0], p[i][1] - p[i - 1][1]);if (!named.has(r.n) || named.get(r.n).L < L) named.set(r.n, {L, a: p[i - 1], b: p[i]});}}
      for (const [name, s] of named) {const [u1, v1] = P(...s.a), [u2, v2] = P(...s.b);if (Math.hypot(u2 - u1, v2 - v1) < ctx.measureText(name).width * .6) continue;let ang = Math.atan2(v2 - v1, u2 - u1);if (ang > Math.PI / 2) ang -= Math.PI;if (ang < -Math.PI / 2) ang += Math.PI;ctx.save();ctx.translate((u1 + u2) / 2, (v1 + v2) / 2);ctx.rotate(ang);ctx.fillText(name, 0, -3 * devicePixelRatio);ctx.restore();}
      // Points of interest.
      const icon = (x, s, color, label, glyph) => {const [u, v] = P(x, s), r = 11 * devicePixelRatio;ctx.fillStyle = color;ctx.beginPath();ctx.arc(u, v, r, 0, 7);ctx.fill();ctx.strokeStyle = '#fff';ctx.lineWidth = 2 * devicePixelRatio;ctx.stroke();ctx.fillStyle = '#fff';ctx.font = `bold ${12 * devicePixelRatio}px system-ui`;ctx.textBaseline = 'middle';ctx.fillText(glyph, u, v + .5);ctx.textBaseline = 'alphabetic';if (label) {ctx.fillStyle = '#222';ctx.font = `bold ${12 * devicePixelRatio}px system-ui`;ctx.fillText(label, u, v - r - 5 * devicePixelRatio);}};
      for (const p of poi()) icon(p.x, p.s, p.color, p.label, p.glyph);
      if (player) {
        const l = toLocal(player.x, player.z), [u, v] = P(l.x, l.s), r = 9 * devicePixelRatio, a = player.headingLocal;
        ctx.save();ctx.translate(u, v);ctx.rotate(a);ctx.fillStyle = '#1565c0';ctx.beginPath();ctx.moveTo(0, -r * 1.4);ctx.lineTo(r, r);ctx.lineTo(0, r * .45);ctx.lineTo(-r, r);ctx.closePath();ctx.fill();ctx.strokeStyle = '#fff';ctx.lineWidth = 2;ctx.stroke();ctx.restore();
      }
      // North arrow and scale bar.
      ctx.fillStyle = '#222';ctx.font = `bold ${14 * devicePixelRatio}px system-ui`;ctx.fillText('N ↑', w - 40 * devicePixelRatio, 30 * devicePixelRatio);
      const bar = 100 * k;ctx.fillRect(20 * devicePixelRatio, h - 30 * devicePixelRatio, bar, 4 * devicePixelRatio);ctx.fillText('100 m', 20 * devicePixelRatio + bar / 2, h - 38 * devicePixelRatio);
    }
    map.toggle = player => {map.open = !map.open;el.hidden = !map.open;if (map.open && player) {const l = toLocal(player.x, player.z);map.cx = l.x;map.cs = l.s;}draw(player);};
    map.pan = (dx, ds) => {map.cx += dx / map.scale;map.cs += ds / map.scale;};
    map.zoom = f => {map.scale = Math.max(.5, Math.min(12, map.scale * f));};
    map.draw = draw;
    let drag = null;
    canvas.addEventListener('pointerdown', e => {drag = {x: e.clientX, y: e.clientY};canvas.setPointerCapture(e.pointerId);});
    canvas.addEventListener('pointermove', e => {if (!drag) return;map.pan(-(e.clientX - drag.x), -(e.clientY - drag.y));drag = {x: e.clientX, y: e.clientY};});
    canvas.addEventListener('pointerup', () => {drag = null;});
    canvas.addEventListener('wheel', e => {e.preventDefault();map.zoom(Math.exp(-e.deltaY * .0015));}, {passive: false});
    return map;
  }
  return {create};
});
