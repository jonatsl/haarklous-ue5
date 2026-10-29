/* Paintball / reball course models (ported from the UE5 project's tools/blender/build_paintball.py, 28 Oct 2026).
 * Data: window.PAINTBALL (paintball-data.js, from tools/make-paintball.py).
 * PaintballModels.create({T, lib, data}) ->
 *   course()      T.Group: the course rig root (floor + lines + base zones, retaining walls, net, cover, flag stands).
 *                 Children in course coordinates (u, y, v): u along the map's local x, v along local s, y = metres
 *                 ABOVE THE YARD FLOOR (0 = floor). Place it with
 *                 group.position.set(centre.world[0], data.floor, centre.world[1]); group.rotation.y = centre.theta.
 *                 Merged per material and per part (floor / walls / net / cover): about 22 meshes in all.
 *   shelter(), bench(), rack(), board(), sign()   lobby furniture, T.Group each, origin on the ground.
 *   flag(team)    T.Group, pole and cloth (0 blue, 1 red), origin at the foot of the pole.
 *   marker()      T.Group, origin at the grip, barrel along -z, hopper on top, air tank as the stock.
 *   mask()        goggles T.Group (origin at the head node, the face toward -z).
 *   band(team)    armband T.Group (a ring round the x axis).
 *   ball(team)    {geometry, material} for an InstancedMesh (17.4 mm, glowing team colour).
 *   splat(team)   {geometry, material}: paint splat decal, 1 m plane in XY facing +z (orient and scale it).
 *   COLORS        {blue, red} team colours.
 * PaintballModels.capTris(points) -> [[a, b, c], ...]: ear-clipping triangles for a simple polygon (Node-tested).
 * Shapes are built like the Python (triangle lists with a material per triangle, uvs box-projected in metres) and
 * turned into one BufferGeometry per material.
 */
(function (root, factory) {
  root.PaintballModels = factory();
})(typeof window === 'undefined' ? globalThis : window, function () {
  'use strict';
  const BLUE = 0x1e5fd7, RED = 0xcd2d1e;          // MI_PB_Blue / MI_PB_Red (sRGB 30,95,215 and 205,45,30)

  // ---------- small vector maths (arrays [x, y, z], 3x3 row-major matrices) ----------
  const add = (a, b) => [a[0] + b[0], a[1] + b[1], a[2] + b[2]], sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
  const mul = (a, s) => [a[0] * s, a[1] * s, a[2] * s], dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
  const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
  const unit = a => {const l = Math.hypot(a[0], a[1], a[2]);return l > 1e-12 ? mul(a, 1 / l) : [0, 0, 0];};
  const mv = (R, p) => [R[0][0] * p[0] + R[0][1] * p[1] + R[0][2] * p[2], R[1][0] * p[0] + R[1][1] * p[1] + R[1][2] * p[2], R[2][0] * p[0] + R[2][1] * p[1] + R[2][2] * p[2]];
  const rotX = a => {const c = Math.cos(a), s = Math.sin(a);return [[1, 0, 0], [0, c, -s], [0, s, c]];};
  const rotY = a => {const c = Math.cos(a), s = Math.sin(a);return [[c, 0, s], [0, 1, 0], [-s, 0, c]];};   // = three.js rotation.y
  const rotZ = a => {const c = Math.cos(a), s = Math.sin(a);return [[c, -s, 0], [s, c, 0], [0, 0, 1]];};

  // ---------- triangle-list mesh with a material key per triangle (site_geometry.py's Mesh) ----------
  function Mesh() {this.T = [];}
  Mesh.prototype.tri = function (a, b, c, m, n, uv) {this.T.push({p: [a, b, c], m, n: n || null, uv: uv || null});};
  Mesh.prototype.quad = function (a, b, c, d, m, uv) {this.tri(a, b, c, m, null, uv && [uv[0], uv[1], uv[2]]);this.tri(a, c, d, m, null, uv && [uv[0], uv[2], uv[3]]);};
  // Adds another mesh moved by rotation R then position pos (Python: m.add(other.transformed(pos, R))).
  Mesh.prototype.add = function (o, pos = [0, 0, 0], R = null) {
    for (const t of o.T) this.T.push({p: t.p.map(p => add(R ? mv(R, p) : p, pos)), m: t.m, n: t.n && (R ? t.n.map(n => mv(R, n)) : t.n), uv: t.uv});
    return this;
  };
  // Smooth normals for the triangles from index `from` on: area-weighted average over faces sharing a position (same
  // material) whose normals are within the crease angle.
  function smooth(m, from = 0, creaseCos = .3) {
    const fn = [], fu = [], at = new Map(), key = (p, mat) => mat + '|' + p.map(v => Math.round(v * 1e5)).join(',');
    for (let i = from; i < m.T.length; i++) {
      const t = m.T[i], n = cross(sub(t.p[1], t.p[0]), sub(t.p[2], t.p[0]));fn[i] = n;fu[i] = unit(n);
      for (const p of t.p) {const k = key(p, t.m);if (!at.has(k)) at.set(k, []);at.get(k).push(i);}
    }
    for (let i = from; i < m.T.length; i++) {
      const t = m.T[i];
      t.n = t.p.map(p => {let s = [0, 0, 0];for (const j of at.get(key(p, t.m))) if (dot(fu[j], fu[i]) > creaseCos) s = add(s, fn[j]);const u = unit(s);return u[0] || u[1] || u[2] ? u : fu[i];});
    }
  }

  // Box centred at c with full sizes; bevel > 0 chamfers every edge (one 45 degree cut); skip drops faces ('+x', '-y', ...).
  function box(m, c, size, mat, bevel = 0, R = null, skip = []) {
    const [hx, hy, hz] = size.map(s => s / 2), P = (x, y, z) => add(R ? mv(R, [x, y, z]) : [x, y, z], c);
    const b = Math.min(bevel, hx * .9, hy * .9, hz * .9);
    if (b <= 0) {
      const faces = {'+x': [[hx, -hy, hz], [hx, -hy, -hz], [hx, hy, -hz], [hx, hy, hz]], '-x': [[-hx, -hy, -hz], [-hx, -hy, hz], [-hx, hy, hz], [-hx, hy, -hz]],
        '+y': [[-hx, hy, hz], [hx, hy, hz], [hx, hy, -hz], [-hx, hy, -hz]], '-y': [[-hx, -hy, -hz], [hx, -hy, -hz], [hx, -hy, hz], [-hx, -hy, hz]],
        '+z': [[-hx, -hy, hz], [hx, -hy, hz], [hx, hy, hz], [-hx, hy, hz]], '-z': [[hx, -hy, -hz], [-hx, -hy, -hz], [-hx, hy, -hz], [hx, hy, -hz]]};
      for (const k in faces) if (!skip.includes(k)) m.quad(...faces[k].map(v => P(...v)), mat);
      return m;
    }
    const full = [hx, hy, hz], ext = [hx - b, hy - b, hz - b];
    const out = (pts, n) => {const w = R ? mv(R, n) : n;return dot(cross(sub(pts[1], pts[0]), sub(pts[2], pts[0])), w) < 0 ? pts.reverse() : pts;};
    for (let ax = 0; ax < 3; ax++) for (const sg of [1, -1]) {
      const [u, v] = [0, 1, 2].filter(a => a !== ax), pts = [];
      for (const [su, sv] of [[-1, -1], [1, -1], [1, 1], [-1, 1]]) {const q = [0, 0, 0];q[ax] = sg * full[ax];q[u] = su * ext[u];q[v] = sv * ext[v];pts.push(P(...q));}
      const n = [0, 0, 0];n[ax] = sg;m.quad(...out(pts, n), mat);
    }
    for (let a = 0; a < 3; a++) for (let bb = a + 1; bb < 3; bb++) for (const sa of [1, -1]) for (const sb of [1, -1]) {
      const w = 3 - a - bb, pts = [];
      for (const sw of [-1, 1]) {
        const q1 = [0, 0, 0], q2 = [0, 0, 0];q1[a] = sa * full[a];q1[bb] = sb * ext[bb];q1[w] = sw * ext[w];q2[a] = sa * ext[a];q2[bb] = sb * full[bb];q2[w] = sw * ext[w];
        pts.push(q1, q2);
      }
      const n = [0, 0, 0];n[a] = sa;n[bb] = sb;m.quad(...out([P(...pts[0]), P(...pts[1]), P(...pts[3]), P(...pts[2])], n), mat);
    }
    for (const sx of [1, -1]) for (const sy of [1, -1]) for (const sz of [1, -1]) {
      const t = out([P(sx * hx, sy * ext[1], sz * ext[2]), P(sx * ext[0], sy * hy, sz * ext[2]), P(sx * ext[0], sy * ext[1], sz * hz)], [sx, sy, sz]);
      m.tri(t[0], t[1], t[2], mat);
    }
    return m;
  }

  // Cylinder (or cone with r2) from a to b, smooth sides, flat caps.
  function cyl(m, a, b, r, mat, seg = 12, caps = true, r2 = r) {
    const d = unit(sub(b, a)), t = unit(cross(d, Math.abs(d[1]) < .9 ? [0, 1, 0] : [1, 0, 0])), s = cross(d, t);
    const O = [], A = [], B = [];
    for (let k = 0; k < seg; k++) {const ang = 2 * Math.PI * k / seg, o = add(mul(t, Math.cos(ang)), mul(s, Math.sin(ang)));O.push(o);A.push(add(a, mul(o, r)));B.push(add(b, mul(o, r2)));}
    // Faces wind outward when a -> b follows the right-hand rule; reversed (a band's inside), the normals turn inward too.
    const f = dot(cross(sub(A[1], A[0]), sub(B[1], A[0])), O[0]) < 0 ? -1 : 1, N = O.map(o => mul(o, f));
    for (let k = 0; k < seg; k++) {
      const k1 = (k + 1) % seg;
      m.tri(A[k], A[k1], B[k1], mat, [N[k], N[k1], N[k1]]);m.tri(A[k], B[k1], B[k], mat, [N[k], N[k1], N[k]]);
    }
    if (caps) for (let k = 1; k < seg - 1; k++) {m.tri(A[0], A[k + 1], A[k], mat);m.tri(B[0], B[k], B[k + 1], mat);}
    return m;
  }

  // Triangles filling a simple polygon (convex or not) by ear clipping: index triples in the polygon's own turning order.
  // A fan only works for convex shapes; on the concave jersey barrier it left a back-facing triangle in each end cap
  // (see-through, Unreal issue jonatsl/haarklous-ue5#1) and a sliver sticking out past the outline.
  function capTris(p) {
    let area = 0;p.forEach((a, i) => {const b = p[(i + 1) % p.length];area += a[0] * b[1] - b[0] * a[1];});
    const sg = area > 0 ? 1 : -1, cr = (a, b, c) => sg * ((b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]));
    const idx = p.map((_, i) => i), out = [];
    while (idx.length > 3) {
      let cut = -1;
      for (let i = 0; i < idx.length && cut < 0; i++) {
        const a = idx[(i + idx.length - 1) % idx.length], b = idx[i], c = idx[(i + 1) % idx.length];
        if (cr(p[a], p[b], p[c]) <= 1e-12) continue;// a concave (or straight) corner is not an ear
        if (!idx.some(j => j !== a && j !== b && j !== c && cr(p[a], p[b], p[j]) >= 0 && cr(p[b], p[c], p[j]) >= 0 && cr(p[c], p[a], p[j]) >= 0)) {out.push([a, b, c]);cut = i;}
      }
      if (cut < 0) break;idx.splice(cut, 1);
    }
    if (idx.length === 3) out.push(idx.slice());
    return out;
  }

  // A profile in the (z, y) plane extruded from x = x0 to x1 (convex or concave); faces always wind outward.
  function prism(m, profile, x0, x1, mat) {
    let area = 0;profile.forEach((p, i) => {const q = profile[(i + 1) % profile.length];area += p[0] * q[1] - q[0] * p[1];});
    const pts = area > 0 ? profile.slice().reverse() : profile, n = pts.length;
    const L = pts.map(p => [x0, p[1], p[0]]), R = pts.map(p => [x1, p[1], p[0]]);
    for (const [a, b, c] of capTris(pts)) {m.tri(R[a], R[b], R[c], mat);m.tri(L[a], L[c], L[b], mat);}
    for (let k = 0; k < n; k++) {const k1 = (k + 1) % n;m.quad(L[k], L[k1], R[k1], R[k], mat);}
    return m;
  }

  // Solid of revolution about the x axis: closed profile of (x, r); faces always wind outward, smooth shading.
  function lathe(m, profile, mat, seg = 24, centre = [0, 0, 0]) {
    let area = 0;profile.forEach((p, i) => {const q = profile[(i + 1) % profile.length];area += p[0] * q[1] - q[0] * p[1];});
    const pr = area < 0 ? profile.slice().reverse() : profile, n = pr.length, from = m.T.length, rings = [];
    for (let k = 0; k < seg; k++) {const a = 2 * Math.PI * k / seg;rings.push(pr.map(([x, r]) => add(centre, [x, r * Math.cos(a), r * Math.sin(a)])));}
    for (let k = 0; k < seg; k++) for (let i = 0; i < n; i++) {const k1 = (k + 1) % seg, i1 = (i + 1) % n;m.quad(rings[k][i], rings[k][i1], rings[k1][i1], rings[k1][i], mat);}
    smooth(m, from);
    return m;
  }

  // A tyre (axle along x) on a dished rim; the Python's tread lugs and wheel bolts are left out for the browser.
  function tyre(m, centre, radius, width, matT, matR, rimDish = .08) {
    const rr = radius * .58, hw = width / 2;
    lathe(m, [[-hw, rr * 1.02], [-hw - .01, (rr + radius) / 2], [-hw + .03, radius - .05], [-hw + .1, radius - .015], [hw - .1, radius - .015],
      [hw - .03, radius - .05], [hw + .01, (rr + radius) / 2], [hw, rr * 1.02]], matT, 18, centre);
    lathe(m, [[-hw + .05, rr], [hw - .05, rr], [hw - .05, rr * .9], [rimDish, rr * .85], [rimDish, rr * .25], [rimDish + .06, rr * .22],
      [rimDish + .06, .01], [-hw + .05, .01]], matR, 12, centre);
    return m;
  }

  function create({T, lib, data}) {
    data = data || (typeof window !== 'undefined' ? window.PAINTBALL : globalThis.PAINTBALL);
    const canvas = (w, h = w) => {const c = document.createElement('canvas');c.width = w;c.height = h;return c;};
    let seed = 11;const rnd = () => (seed = (seed * 16807) % 2147483647) / 2147483647;
    const std = (color, roughness, metalness = 0) => new T.MeshStandardMaterial({color, roughness, metalness});
    const lin = (r, g, b) => new T.Color().setRGB(r, g, b, T.LinearSRGBColorSpace);
    const tex = (c, srgb = true) => {const t = new T.CanvasTexture(c);if (srgb) t.colorSpace = T.SRGBColorSpace;t.anisotropy = 8;t.needsUpdate = true;return t;};
    // Text helper: centred at x, top at y (PIL's text origin), font size in px.
    const text = (g, s, x, y, size, color, bold = true, align = 'center') => {g.font = `${bold ? 'bold ' : ''}${size}px Arial, Helvetica, sans-serif`;g.fillStyle = color;g.textAlign = align;g.textBaseline = 'top';g.fillText(s, x, y);};

    // ---------- textures (tools/make-signs.py drawn on canvases) ----------
    function boardTexture() {
      const W = 1300, H = 700, k = .8, c = canvas(W * k, H * k), g = c.getContext('2d');g.scale(k, k);
      g.fillStyle = 'rgb(22,24,28)';g.fillRect(0, 0, W, H);
      for (let i = 0; i < 26; i++) {                               // paint marks on the board
        g.fillStyle = ['rgb(30,110,230)', 'rgb(220,60,30)', 'rgb(240,200,30)'][Math.floor(rnd() * 3)];
        const x = rnd() * W, y = rnd() * H, r = 8 + rnd() * 18;g.beginPath();g.arc(x, y, r, 0, 2 * Math.PI);g.fill();
      }
      g.fillStyle = 'rgb(15,16,19)';g.fillRect(0, 0, W, 210);
      text(g, 'HAARKLOUS', W / 2, 28, 110, 'rgb(245,245,240)');
      text(g, 'PAINTBALL  ·  REBALL', W / 2, 140, 52, 'rgb(250,200,20)');
      ['Maske på hele tiden i spilleområdet', 'Treff = ut: kula sprekker og merker deg', 'Rekyl og sprett teller ikke (paint)',
        'Halvautomatisk, maks 10 kuler i sekundet', 'Lag-deathmatch  ·  Capture the flag', 'Inntil 6 mot 6'].forEach((s, i) => {
        g.fillStyle = 'rgb(250,200,20)';g.beginPath();g.arc(119, 271 + i * 70, 9, 0, 2 * Math.PI);g.fill();
        text(g, s, 150, 245 + i * 70, 46, 'rgb(235,235,230)', false, 'left');
      });
      return tex(c);
    }
    function signTexture() {
      const W = 800, H = 600, k = .64, c = canvas(W * k, H * k), g = c.getContext('2d');g.scale(k, k);
      g.fillStyle = 'rgb(250,200,20)';g.fillRect(0, 0, W, H);
      g.strokeStyle = 'rgb(20,20,20)';g.lineWidth = 12;g.strokeRect(16, 16, W - 32, H - 32);
      // Goggles: a rounded frame, the nose gap, the strap.
      const rr = (x0, y0, x1, y1, r) => {g.beginPath();g.moveTo(x0 + r, y0);g.arcTo(x1, y0, x1, y1, r);g.arcTo(x1, y1, x0, y1, r);g.arcTo(x0, y1, x0, y0, r);g.arcTo(x0, y0, x1, y0, r);g.closePath();};
      g.lineWidth = 22;rr(211, 81, 589, 209, 49);g.stroke();
      g.fillStyle = 'rgb(250,200,20)';g.fillRect(380, 120, 40, 80);
      g.lineWidth = 18;g.beginPath();g.moveTo(200, 150);g.lineTo(120, 160);g.moveTo(600, 150);g.lineTo(680, 160);g.stroke();
      text(g, 'MASKE PÅ', W / 2, 270, 130, 'rgb(20,20,20)');
      text(g, 'I SPILLEOMRÅDET', W / 2, 430, 70, 'rgb(20,20,20)');
      return tex(c);
    }
    function splatTexture() {
      // White paint with alpha (tinted by the material colour): a core, lobes and flung drops, a little shade variation.
      const S = 256, c = canvas(S), g = c.getContext('2d');g.clearRect(0, 0, S, S);g.fillStyle = '#ffffff';g.strokeStyle = '#ffffff';g.lineCap = 'round';
      g.beginPath();g.arc(S / 2, S / 2, S * .2, 0, 2 * Math.PI);g.fill();
      for (let i = 0; i < 14; i++) {
        const a = rnd() * 2 * Math.PI, r = (.12 + rnd() * .26) * S, s = (.03 + rnd() * .07) * S, x = S / 2 + Math.cos(a) * r, y = S / 2 + Math.sin(a) * r;
        g.beginPath();g.arc(x, y, s, 0, 2 * Math.PI);g.fill();
        g.lineWidth = s * .9;g.beginPath();g.moveTo(S / 2, S / 2);g.lineTo(x, y);g.stroke();
      }
      g.globalCompositeOperation = 'source-atop';
      for (let i = 0; i < 40; i++) {const v = 205 + Math.floor(rnd() * 50);g.fillStyle = `rgb(${v},${v},${v})`;g.beginPath();g.arc(rnd() * S, rnd() * S, 4 + rnd() * 12, 0, 2 * Math.PI);g.fill();}
      g.globalCompositeOperation = 'source-over';
      return tex(c);
    }
    function netTexture() {
      // 8 x 8 cells per 0.6 m tile (7.5 cm mesh), dark cord on transparent.
      const S = 128, c = canvas(S), g = c.getContext('2d');g.clearRect(0, 0, S, S);g.fillStyle = 'rgba(28,32,30,1)';
      for (let i = 0; i < 8; i++) {g.fillRect(i * 16, 0, 2, S);g.fillRect(0, i * 16, S, 2);}
      const t = tex(c);t.wrapS = t.wrapT = T.RepeatWrapping;t.repeat.set(1 / .6, 1 / .6);return t;
    }

    // ---------- materials (MI_PB_* / MI_Site_* slots), made once ----------
    const MAKE = {
      asph: () => lib.concrete(0x2a2a2c), conc: () => lib.concrete(), plas: () => lib.plaster(0xece6da), brick: () => lib.brick(),
      rust: () => std(0x6c4532, .85, .35), wood: () => lib.wood(0x8a6a48, .8), rub: () => std(0x151515, .95), galv: () => lib.metal(0xbfc3c0, .4),
      dark: () => std(0x1e1e1e, .6), line: () => std(0xe1e1dc, .7), blue: () => std(BLUE, .6), red: () => std(RED, .6),
      sand: () => lib.fabric(0x6a5c42), barrel: () => std(0x23468c, .55),
      contR: () => std(0xa8452f, .6, .3), contG: () => std(0x3f7a4c, .6, .3), contB: () => std(0x3c5f9e, .6, .3), sheet: () => std(0xc5c6c2, .5, .4),
      net: () => new T.MeshStandardMaterial({map: netTexture(), transparent: true, depthWrite: false, side: T.DoubleSide, roughness: .9, metalness: 0}),
      marker: () => std(0x1a1a1e, .6), hopper: () => std(0x0f0f12, .5), tank: () => std(0x5a5c62, .3),
      lens: () => new T.MeshPhysicalMaterial({color: 0x56687a, transparent: true, opacity: .5, roughness: .05, metalness: 0, depthWrite: false}),
      board: () => new T.MeshStandardMaterial({map: boardTexture(), roughness: .7, metalness: 0}),
      sign: () => new T.MeshStandardMaterial({map: signTexture(), roughness: .6, metalness: 0})
    };
    const mats = {}, mat = key => mats[key] || (mats[key] = MAKE[key]());

    // ---------- triangle list -> one BufferGeometry per material ----------
    function geometry(list) {
      const P = [], N = [], U = [];
      for (const t of list) {
        const fn = cross(sub(t.p[1], t.p[0]), sub(t.p[2], t.p[0]));
        if (Math.hypot(fn[0], fn[1], fn[2]) < 1e-12) continue;          // degenerate (lathe poles, zero-length profile steps)
        const fu = unit(fn), ax = fu.map(Math.abs);
        t.p.forEach((p, i) => {
          P.push(p[0], p[1], p[2]);const n = t.n ? t.n[i] : fu;N.push(n[0], n[1], n[2]);
          if (t.uv) U.push(t.uv[i][0], t.uv[i][1]);
          else if (ax[1] >= ax[0] && ax[1] >= ax[2]) U.push(p[0], p[2]);      // box projection in metres (lib.boxUV)
          else if (ax[0] >= ax[2]) U.push(p[2], p[1]);
          else U.push(p[0], p[1]);
        });
      }
      const g = new T.BufferGeometry();
      g.setAttribute('position', new T.Float32BufferAttribute(P, 3));g.setAttribute('normal', new T.Float32BufferAttribute(N, 3));g.setAttribute('uv', new T.Float32BufferAttribute(U, 2));
      g.computeBoundingBox();g.computeBoundingSphere();return g;
    }
    function toGroup(m, name, cast = true) {
      const g = new T.Group(), buckets = new Map();g.name = name;
      for (const t of m.T) {if (!buckets.has(t.m)) buckets.set(t.m, []);buckets.get(t.m).push(t);}
      for (const [key, list] of buckets) {
        const material = mat(key), mesh = new T.Mesh(geometry(list), material);
        mesh.name = name + ':' + key;mesh.castShadow = cast && !material.transparent;mesh.receiveShadow = true;mesh.userData.merged = true;g.add(mesh);
      }
      return g;
    }

    // ---------- cover kit (each piece in its own frame: x along w, z along d, y up from the floor) ----------
    function jersey(m, length = 3.0) {prism(m, [[-.3, 0], [.3, 0], [.3, .08], [.13, .32], [.1, .85], [-.1, .85], [-.13, .32], [-.3, .08]], -length / 2, length / 2, 'conc');}
    function container(m, colour) {
      const W = 6.06, D = 2.44, H = 2.6;
      box(m, [0, H / 2, 0], [W, H, D], colour);
      for (let k = 0; k < 18; k++) for (const sz of [-1, 1]) box(m, [-2.8 + k * .33, H / 2, sz * (D / 2 + .015)], [.12, H - .2, .03], colour, 0, null, [sz > 0 ? '-z' : '+z']);   // ribs
      for (const sx of [-1, 1]) for (const sz of [-1, 1]) box(m, [sx * (W / 2 - .06), (H + .01) / 2, sz * (D / 2 - .06)], [.14, H + .01, .14], 'dark');   // posts 1 cm above the roof (no coplanar tops)
      for (const z of [-.6, -.2, .2, .6]) cyl(m, [W / 2 + .03, .2, z], [W / 2 + .03, H - .2, z], .025, 'galv', 6);   // door bars
    }
    function shell(m) {
      const t = .25, h = 2.7, parts = [[-2.0, -3.0, 3.0, t, 0, h], [2.8, -3.0, 1.4, t, 0, h], [1.55, -3.0, 1.1, t, 2.1, .6],
        [2.0, 3.0, 3.0, t, 0, h], [-2.8, 3.0, 1.4, t, 0, h], [-1.55, 3.0, 1.1, t, 2.1, .6]];
      for (const sx of [-3.5, 3.5]) parts.push([sx, -1.75, t, 2.5, 0, h], [sx, 1.75, t, 2.5, 0, h], [sx, 0, t, 1.0, 0, .9], [sx, 0, t, 1.0, 1.7, 1.0]);
      for (const [x, z, w, d, y0, hh] of parts) {
        box(m, [x, y0 + hh / 2, z], [w, hh, d], 'plas', 0, null, Math.abs(y0 + hh - h) < 1e-6 ? ['+y'] : []);   // tops under the coping
        if (y0 === 0) box(m, [x, .2, z], [w + .04, .4, d + .04], 'brick');
      }
      for (const [x, z, w, d] of [[0, -3.0, 7.25, t], [0, 3.0, 7.25, t], [-3.5, 0, t, 6.25], [3.5, 0, t, 6.25]]) box(m, [x, h + .04, z], [w + .06, .08, d + .06], 'conc');   // coping
    }
    function wall(m) {
      for (const [x, z, w, d, y0, h] of [[-1.0, 0, 1.0, .25, 0, 2.2], [1.0, 0, 1.0, .25, 0, 2.2], [0, 0, 1.0, .25, 0, .9], [0, 0, 1.0, .25, 1.6, .6]]) box(m, [x, y0 + h / 2, z], [w, h, d], 'conc');
      box(m, [0, .05, 0], [3.3, .1, .6], 'conc', 0, null, ['-y']);          // footing
    }
    function car(m) {
      box(m, [0, .55, 0], [4.2, .65, 1.78], 'rust', .08);
      box(m, [-.1, .95, 0], [4.0, .2, 1.74], 'rust', .05);
      box(m, [.2, 1.2, 0], [2.1, .5, 1.55], 'rust', .1);                   // cabin (glass long gone)
      box(m, [.2, 1.2, 0], [1.9, .38, 1.6], 'dark');
      const t = tyre(new Mesh(), [0, 0, 0], .33, .2, 'rub', 'dark', .03);
      for (const sx of [-1.35, 1.35]) for (const sz of [-.8, .8]) m.add(t, [sx, .26, sz], rotY(Math.PI / 2));
      box(m, [-2.1, .5, 0], [.08, .25, 1.6], 'galv');                      // bumper
    }
    function spool(m) {
      for (const sx of [-.45, .45]) cyl(m, [sx - .04, .8, 0], [sx + .04, .8, 0], .8, 'wood', 20);
      cyl(m, [-.42, .8, 0], [.42, .8, 0], .35, 'wood', 14);
      cyl(m, [-.4, .8, 0], [.4, .8, 0], .5, 'dark', 16);                     // cable left on it
    }
    function sandbags(m) {
      for (let row = 0; row < 4; row++) for (let k = 0; k < (row % 2 ? 4 : 5); k++) box(m, [-1.2 + k * .6 + (row % 2 ? .3 : 0), .12 + row * .24, 0], [.58, .22, .62 - row * .04], 'sand', .08);
    }
    function barrels(m) {
      [[0, 0, 'barrel'], [.7, .2, 'rust'], [.3, .75, 'barrel']].forEach(([x, z, mt]) => {
        cyl(m, [x, 0, z], [x, .88, z], .29, mt, 16);
        for (const y of [.3, .6]) cyl(m, [x, y - .015, z], [x, y + .015, z], .3, mt, 16);
      });
    }
    function pallets(m) {
      for (let k = 0; k < 9; k++) {
        const y = .07 + k * .145;box(m, [0, y + .05, 0], [1.2, .03, 1.0], 'wood');
        for (const z of [-.45, 0, .45]) box(m, [0, y, z], [1.2, .1, .1], 'wood');
      }
    }
    function tires(m) {
      const t = tyre(new Mesh(), [0, 0, 0], .36, .24, 'rub', 'dark');
      for (const [x, n] of [[0, 4], [.8, 3]]) for (let k = 0; k < n; k++) m.add(t, [x, .13 + k * .25, 0], rotZ(Math.PI / 2));
    }
    const KIT = {barrier: m => jersey(m), shell, wall, car, spool, sandbags, barrels, pallets, tires};
    const kits = {};                                                     // each kind built once, placed by transform
    const kit = kind => kits[kind] || (kits[kind] = (() => {const m = new Mesh();KIT[kind](m);return m;})());

    // ---------- the course rig ----------
    function course() {
      const rect = data.rect, play = data.play, cx = data.centre.x, cs = data.centre.s;
      const u0 = rect.x0 - cx, u1 = rect.x0 + rect.w - cx, v0 = rect.s0 - cs, v1 = rect.s0 + rect.d - cs;
      const rootG = new T.Group();rootG.name = 'paintball';
      // Floor: asphalt, painted boundary of the play area, the base lines (6-8 mm above the asphalt).
      const f = new Mesh();
      f.quad([u0, 0, v1], [u1, 0, v1], [u1, 0, v0], [u0, 0, v0], 'asph');
      const pu = play.w / 2, pv = play.d / 2, corners = [[-pu, -pv], [pu, -pv], [pu, pv], [-pu, pv], [-pu, -pv]];
      for (let i = 0; i < 4; i++) {
        const [a, b] = [corners[i], corners[i + 1]], L = Math.hypot(b[0] - a[0], b[1] - a[1]);
        box(f, [(a[0] + b[0]) / 2, .004, (a[1] + b[1]) / 2], [L + .12, .008, .12], 'line', 0, rotY(-Math.atan2(b[1] - a[1], b[0] - a[0])), ['-y']);
      }
      data.bases.forEach((base, i) => {const z = base.zone, q = pv - .06;f.quad([z - .06, .006, q], [z + .06, .006, q], [z + .06, .006, -q], [z - .06, .006, -q], i ? 'red' : 'blue');});
      rootG.add(toGroup(f, 'floor', false));
      // Retaining walls along the yard's edge (0.3 m thick, 1 m segments) with a coping; a gap for the gate in the south wall.
      const w = new Mesh(), g0 = data.gate[0] - cx, g1 = data.gate[1] - cx, fl = data.floor;
      for (const name of Object.keys(data.edges)) {
        const pts = data.edges[name], horiz = name === 'north' || name === 'south';
        for (let i = 0; i + 1 < pts.length; i++) {
          const [ua, va, ha] = pts[i], [ub, vb, hb] = pts[i + 1], um = (ua + ub) / 2, vm = (va + vb) / 2, h = (ha + hb) / 2 - fl;
          if (name === 'south' && g0 - .2 < um && um < g1 + .2) continue;
          const bottom = Math.min(0, h) - .4, top = Math.max(0, h) + (h < .8 ? 1.05 : .12);
          box(w, [um, (top + bottom) / 2, vm], horiz ? [1.02, top - bottom, .3] : [.3, top - bottom, 1.02], 'conc', 0, null, ['+y']);
          box(w, [um, top + .04, vm], [horiz ? 1.04 : .36, .08, horiz ? .36 : 1.04], 'conc');
        }
      }
      for (const gx of [g0 - .2, g1 + .2]) box(w, [gx, .9, v1], [.4, 1.8, .4], 'conc');
      rootG.add(toGroup(w, 'walls'));
      // Net: posts every 5 m, netting 5 m high round the play area (one double-sided plane per side), a top rail.
      const n = new Mesh();
      for (let i = 0; i < 4; i++) {
        const [a, b] = [corners[i], corners[i + 1]], L = Math.hypot(b[0] - a[0], b[1] - a[1]), k = Math.round(L / 5);
        for (let j = 0; j < k; j++) {const p = [a[0] + (b[0] - a[0]) * j / k, a[1] + (b[1] - a[1]) * j / k];cyl(n, [p[0], 0, p[1]], [p[0], 5.0, p[1]], .05, 'galv', 8);}
        n.quad([a[0], .05, a[1]], [b[0], .05, b[1]], [b[0], 5.0, b[1]], [a[0], 5.0, a[1]], 'net');
        cyl(n, [a[0], 5.0, a[1]], [b[0], 5.0, b[1]], .025, 'galv', 6);
      }
      rootG.add(toGroup(n, 'net'));
      // Cover (point-symmetric for both teams) and the two flag stands.
      const c = new Mesh(), colours = ['contR', 'contG', 'contB'];let nc = 0;
      for (const p of data.pieces) {
        let pm;
        if (p.kind === 'container') {pm = new Mesh();container(pm, colours[nc++ % 3]);}
        else pm = kit(p.kind);
        c.add(pm, [p.u, 0, p.v], rotY(p.yaw));
      }
      data.bases.forEach((base, i) => {
        const [u, v] = base.flag;
        cyl(c, [u, 0, v], [u, .25, v], .45, 'conc', 20);
        cyl(c, [u, .25, v], [u, .27, v], .46, i ? 'red' : 'blue', 20);
      });
      rootG.add(toGroup(c, 'cover'));
      return rootG;
    }

    // ---------- lobby furniture ----------
    function shelter() {
      const s = new Mesh();
      for (const x of [-3, 3]) for (const z of [-1.5, 1.5]) cyl(s, [x, 0, z], [x, z < 0 ? 2.6 : 2.3, z], .06, 'galv', 8);
      box(s, [0, 2.5, 0], [6.6, .05, 3.4], 'sheet', 0, rotX(Math.atan2(.3, 3.2)));   // sloped corrugated roof
      box(s, [0, 1.25, 1.5], [6.0, 2.3, .05], 'contB');                              // back wall
      return toGroup(s, 'pb-shelter');
    }
    function bench() {
      const b = new Mesh();
      for (const x of [-.8, .8]) box(b, [x, .22, 0], [.06, .44, .4], 'galv');
      box(b, [0, .45, 0], [2.0, .05, .4], 'wood');
      return toGroup(b, 'pb-bench');
    }
    function rack() {                                                    // marker rack with six markers, barrels up
      const r = new Mesh(), mk = markerMesh();
      box(r, [0, .9, 0], [2.2, .06, .3], 'wood');box(r, [0, .3, 0], [2.2, .06, .3], 'wood');
      for (const x of [-1.05, 1.05]) box(r, [x, .6, 0], [.06, 1.2, .3], 'wood');
      for (let k = 0; k < 6; k++) r.add(mk, [-.85 + k * .34, .62, 0], rotX(Math.PI / 2));
      return toGroup(r, 'pb-rack');
    }
    function board() {                                                   // score / rules board, faces +z
      const b = new Mesh();
      for (const x of [-1.3, 1.3]) cyl(b, [x, 0, 0], [x, 2.6, 0], .05, 'galv', 8);
      box(b, [0, 1.75, -.03], [2.7, 1.5, .04], 'dark');
      b.quad([-1.3, 1.05, -.004], [1.3, 1.05, -.004], [1.3, 2.45, -.004], [-1.3, 2.45, -.004], 'board', [[0, 0], [1, 0], [1, 1], [0, 1]]);   // 6 mm in front of the panel
      return toGroup(b, 'pb-board');
    }
    function sign() {                                                    // "goggles on" sign at the gate, faces +z
      const s = new Mesh();
      cyl(s, [0, 0, 0], [0, 2.1, 0], .04, 'galv', 8);
      box(s, [0, 1.65, .04], [.8, .6, .02], 'galv');                     // plate (z .03 to .05)
      s.quad([-.4, 1.35, .056], [.4, 1.35, .056], [.4, 1.95, .056], [-.4, 1.95, .056], 'sign', [[0, 0], [1, 0], [1, 1], [0, 1]]);   // 6 mm in front
      return toGroup(s, 'pb-sign');
    }

    // ---------- flags, gear ----------
    function flag(team) {
      const m = new Mesh(), col = team ? 'red' : 'blue';
      cyl(m, [0, 0, 0], [0, 2.3, 0], .025, 'galv', 8);
      for (let k = 0; k < 6; k++) {                                       // a cloth with a slight wave (both sides)
        const x0 = .03 + k * .14, x1 = .03 + (k + 1) * .14, z0 = .04 * Math.sin(k * 1.1), z1 = .04 * Math.sin((k + 1) * 1.1);
        m.quad([x0, 1.6, z0], [x1, 1.6, z1], [x1, 2.25, z1], [x0, 2.25, z0], col);
        m.quad([x1, 1.6, z1], [x0, 1.6, z0], [x0, 2.25, z0], [x1, 2.25, z1], col);
      }
      return toGroup(m, team ? 'pb-flag-red' : 'pb-flag-blue');
    }
    function band(team) {
      const m = new Mesh(), col = team ? 'red' : 'blue';
      cyl(m, [-.035, 0, 0], [.035, 0, 0], .068, col, 16, false);
      cyl(m, [.035, 0, 0], [-.035, 0, 0], .066, col, 16, false);         // the inside
      return toGroup(m, team ? 'pb-band-red' : 'pb-band-blue', false);
    }
    // Paintball marker: origin at the grip (the hand), barrel along -z, hopper on top, air tank as the stock.
    function markerMesh() {
      const m = new Mesh();
      box(m, [0, .07, -.05], [.045, .1, .26], 'marker', .012);           // body
      cyl(m, [0, .085, -.18], [0, .085, -.48], .016, 'marker', 12);       // barrel
      cyl(m, [0, .085, -.48], [0, .085, -.5], .019, 'dark', 12);
      box(m, [0, -.03, 0], [.035, .12, .05], 'dark', .01, rotX(-.25));    // grip
      box(m, [0, -.02, -.1], [.03, .1, .045], 'dark', .01, rotX(-.15));   // front grip
      box(m, [0, 0, -.035], [.012, .04, .07], 'galv');                    // trigger guard
      cyl(m, [0, .12, -.07], [0, .15, -.07], .018, 'dark', 10);           // feed neck
      const hop = lathe(new Mesh(), [[.1, 0], [.08, .05], [0, .065], [-.08, .05], [-.09, 0]], 'hopper', 16);
      m.add(hop, [0, .2, -.08], rotY(Math.PI / 2));
      cyl(m, [0, .03, .1], [0, .03, .33], .04, 'tank', 14);              // air tank (stock)
      cyl(m, [0, .03, .33], [0, .03, .35], .03, 'galv', 10);
      return m;
    }
    function marker() {return toGroup(markerMesh(), 'pb-marker', false);}
    // Goggles for an avatar's head (origin at the head node; the face toward -z): a curved face band, the lens, a strap.
    function mask() {
      const m = new Mesh(), seg = 9;
      for (let k = 0; k < seg; k++) {
        const a0 = -1.15 + 2.3 * k / seg, a1 = -1.15 + 2.3 * (k + 1) / seg, p = (a, y, r) => [r * Math.sin(a), y, -r * Math.cos(a)];
        for (const [y0, r0, y1, r1, mt] of [[.02, .112, .12, .118, 'marker'], [.12, .118, .19, .114, 'lens'], [.19, .114, .215, .108, 'marker']]) {
          m.quad(p(a1, y0, r0), p(a0, y0, r0), p(a0, y1, r1), p(a1, y1, r1), mt);     // outside
          m.quad(p(a0, y0, r0), p(a1, y0, r0), p(a1, y1, r1), p(a0, y1, r1), mt);     // inside
        }
      }
      for (let k = 0; k < 10; k++) {
        const a0 = 1.15 + (2 * Math.PI - 2.3) * k / 10, a1 = 1.15 + (2 * Math.PI - 2.3) * (k + 1) / 10, p = (a, y) => [.105 * Math.sin(a), y, -.105 * Math.cos(a)];
        m.quad(p(a1, .15), p(a0, .15), p(a0, .18), p(a1, .18), 'dark');
        m.quad(p(a0, .15), p(a1, .15), p(a1, .18), p(a0, .18), 'dark');
      }
      smooth(m, 0, .5);
      return toGroup(m, 'pb-mask', false);
    }

    // ---------- balls and splats (shared per team) ----------
    const balls = [], splats = [];
    function ball(team) {
      team = team ? 1 : 0;
      if (!balls[team]) {
        const glow = team ? lin(1.0, .35, .05) : lin(.15, .55, 1.0);
        balls[team] = {geometry: new T.SphereGeometry(.0087, 10, 6), material: new T.MeshStandardMaterial({color: glow, emissive: glow, emissiveIntensity: .8, roughness: .35, metalness: 0})};
      }
      return balls[team];
    }
    let splatMap = null;
    function splat(team) {
      team = team ? 1 : 0;
      if (!splats[team]) {
        splatMap = splatMap || splatTexture();
        splats[team] = {geometry: new T.PlaneGeometry(1, 1), material: new T.MeshStandardMaterial({color: team ? lin(.95, .3, .03) : lin(.08, .35, .95), map: splatMap,
          transparent: true, depthWrite: false, polygonOffset: true, polygonOffsetFactor: -4, polygonOffsetUnits: -4, roughness: .3, metalness: 0})};
      }
      return splats[team];
    }

    return {course, shelter, bench, rack, board, sign, flag, marker, mask, band, ball, splat, COLORS: {blue: BLUE, red: RED}};
  }
  return {create, capTris};
});
