/* Construction-site machines, crane loads and site furniture (ported from the UE5 project's
 * tools/blender/build_machines.py + site_geometry.py, 28 Oct 2026).
 * Everything is built in the web frame (metres, x right, y up, forward = -z, origin on the ground) as triangle lists
 * with a material slot per triangle; each slot becomes one BufferGeometry, so a node costs one draw call per material.
 * SiteModels.create({T, lib}) ->
 *   rig(name)          -> {group, nodes}  name: site_dozer | site_excavator | site_loader | site_hauler | site_tipper |
 *                         site_crane | site_tower. group = the 'root' node; nodes maps every node name of the rig
 *                         (same names, parents, positions and visibility as Content/Data/rigs/site_*.json) to a T.Group.
 *   prop(kind, len)    -> T.Group  fence (len, default 3.5) | office | office_wc | toilet | sign | pallets | pipes | skip | light
 *   load(kind)         -> T.Group  bricks | beams | skip (crane loads, origin at the bottom centre)
 *   track(points)      -> T.Mesh   gravel strip (4.5 m wide) along [x, y, z] world points, or several strips in one
 *                         mesh when given a list of point lists (site.json 'tracks'); lifted 4 cm + polygon offset
 * Geometry is built once per kind and shared between instances; materials are created once.
 */
(function (root, factory) {
  root.SiteModels = factory();
})(typeof window === 'undefined' ? globalThis : window, function () {
  'use strict';
  function create({T, lib}) {
    // ---------- small vector maths (arrays [x, y, z], 3x3 matrices as rows) ----------
    const add = (a, b) => [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
    const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
    const scl = (a, s) => [a[0] * s, a[1] * s, a[2] * s];
    const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
    const len = a => Math.hypot(a[0], a[1], a[2]);
    const unit = a => scl(a, 1 / (len(a) || 1));
    const mv = (R, p) => [R[0][0] * p[0] + R[0][1] * p[1] + R[0][2] * p[2], R[1][0] * p[0] + R[1][1] * p[1] + R[1][2] * p[2], R[2][0] * p[0] + R[2][1] * p[1] + R[2][2] * p[2]];
    const I3 = [[1, 0, 0], [0, 1, 0], [0, 0, 1]];
    const rotX = a => {const c = Math.cos(a), s = Math.sin(a);return [[1, 0, 0], [0, c, -s], [0, s, c]];};
    const rotY = a => {const c = Math.cos(a), s = Math.sin(a);return [[c, 0, s], [0, 1, 0], [-s, 0, c]];};

    // ---------- triangle soup with a material slot per triangle ----------
    class Geo {
      constructor() {this.S = new Map();this.count = 0;}
      tri(a, b, c, mat) {
        let s = this.S.get(mat);if (!s) this.S.set(mat, s = []);
        s.push(a[0], a[1], a[2], b[0], b[1], b[2], c[0], c[1], c[2]);this.count++;
      }
      quad(a, b, c, d, mat) {this.tri(a, b, c, mat);this.tri(a, c, d, mat);}
      poly(pts, mat) {for (let k = 1; k < pts.length - 1; k++) this.tri(pts[0], pts[k], pts[k + 1], mat);}
      add(o, xf) {
        for (const [mat, s] of o.S) for (let i = 0; i < s.length; i += 9) {
          const p = [[s[i], s[i + 1], s[i + 2]], [s[i + 3], s[i + 4], s[i + 5]], [s[i + 6], s[i + 7], s[i + 8]]];
          this.tri(...(xf ? p.map(xf) : p), mat);
        }
        return this;
      }
      transformed(pos = [0, 0, 0], R = I3) {return new Geo().add(this, p => add(mv(R, p), pos));}
    }

    // Box centred at c with full sizes; bevel > 0 chamfers all edges (one 45 degree cut).
    function box(m, c, size, mat, {bevel = 0, rot = null} = {}) {
      const hx = size[0] / 2, hy = size[1] / 2, hz = size[2] / 2, R = rot || I3;
      const P = q => add(mv(R, q), c);
      const b = Math.min(bevel, hx * .9, hy * .9, hz * .9);
      if (b <= 0) {
        const faces = [[[hx, -hy, hz], [hx, -hy, -hz], [hx, hy, -hz], [hx, hy, hz]], [[-hx, -hy, -hz], [-hx, -hy, hz], [-hx, hy, hz], [-hx, hy, -hz]],
          [[-hx, hy, hz], [hx, hy, hz], [hx, hy, -hz], [-hx, hy, -hz]], [[-hx, -hy, -hz], [hx, -hy, -hz], [hx, -hy, hz], [-hx, -hy, hz]],
          [[-hx, -hy, hz], [hx, -hy, hz], [hx, hy, hz], [-hx, hy, hz]], [[hx, -hy, -hz], [-hx, -hy, -hz], [-hx, hy, -hz], [hx, hy, -hz]]];
        for (const f of faces) m.quad(...f.map(P), mat);
        return m;
      }
      const ext = [hx - b, hy - b, hz - b], full = [hx, hy, hz];
      const orient = (pts, n) => dot(cross(sub(pts[1], pts[0]), sub(pts[2], pts[0])), mv(R, n)) < 0 ? pts.reverse() : pts;
      for (let ax = 0; ax < 3; ax++) for (const sgn of [1, -1]) {
        const [u, v] = [0, 1, 2].filter(a => a !== ax), pts = [];
        for (const [su, sv] of [[-1, -1], [1, -1], [1, 1], [-1, 1]]) {const q = [0, 0, 0];q[ax] = sgn * full[ax];q[u] = su * ext[u];q[v] = sv * ext[v];pts.push(P(q));}
        const n = [0, 0, 0];n[ax] = sgn;m.quad(...orient(pts, n), mat);
      }
      for (let a = 0; a < 3; a++) for (let bb = a + 1; bb < 3; bb++) for (const sa of [1, -1]) for (const sb of [1, -1]) {
        const w = 3 - a - bb, pts = [];
        for (const sw of [-1, 1]) {
          const q1 = [0, 0, 0], q2 = [0, 0, 0];
          q1[a] = sa * full[a];q1[bb] = sb * ext[bb];q1[w] = sw * ext[w];
          q2[a] = sa * ext[a];q2[bb] = sb * full[bb];q2[w] = sw * ext[w];
          pts.push(q1, q2);
        }
        const n = [0, 0, 0];n[a] = sa;n[bb] = sb;
        m.quad(...orient([P(pts[0]), P(pts[1]), P(pts[3]), P(pts[2])], n), mat);
      }
      for (const sx of [1, -1]) for (const sy of [1, -1]) for (const sz of [1, -1]) {
        const t = [P([sx * hx, sy * ext[1], sz * ext[2]]), P([sx * ext[0], sy * hy, sz * ext[2]]), P([sx * ext[0], sy * ext[1], sz * hz])];
        m.tri(...orient(t, [sx, sy, sz]), mat);
      }
      return m;
    }

    // Cylinder (or cone with r2) from point a to b. Segment counts are capped at 16 for the web.
    function cyl(m, a, b, r, mat, {seg = 12, caps = true, r2 = r} = {}) {
      seg = Math.min(seg, 16);
      const d = unit(sub(b, a)), t = unit(cross(d, Math.abs(d[1]) < .9 ? [0, 1, 0] : [1, 0, 0])), s = cross(d, t), A = [], B = [];
      for (let k = 0; k < seg; k++) {
        const an = 2 * Math.PI * k / seg, dir = add(scl(t, Math.cos(an)), scl(s, Math.sin(an)));
        A.push(add(a, scl(dir, r)));B.push(add(b, scl(dir, r2)));
      }
      for (let k = 0; k < seg; k++) {const k1 = (k + 1) % seg;m.quad(A[k], A[k1], B[k1], B[k], mat);}
      if (caps) for (let k = 1; k < seg - 1; k++) {m.tri(A[0], A[k + 1], A[k], mat);m.tri(B[0], B[k], B[k + 1], mat);}
      return m;
    }

    // A convex profile in the (z, y) plane (counter-clockwise seen from +x) extruded from x = x0 to x1.
    function prism(m, profile, x0, x1, mat, sideMat = mat) {
      // The Python's winding is only outward for profiles running clockwise in (z, y); its dozer hood and skip run the
      // other way (inside out), so wind by the profile's orientation instead.
      const L = profile.map(p => [x0, p[1], p[0]]), R = profile.map(p => [x1, p[1], p[0]]), n = profile.length;
      let area = 0;for (let k = 0; k < n; k++) {const p = profile[k], q = profile[(k + 1) % n];area += p[0] * q[1] - q[0] * p[1];}
      const flip = (area > 0) === (x1 > x0);
      for (let k = 1; k < n - 1; k++) {
        if (flip) {m.tri(R[0], R[k + 1], R[k], sideMat);m.tri(L[0], L[k], L[k + 1], sideMat);} else {m.tri(R[0], R[k], R[k + 1], sideMat);m.tri(L[0], L[k + 1], L[k], sideMat);}
      }
      for (let k = 0; k < n; k++) {const k1 = (k + 1) % n;if (flip) m.quad(L[k], R[k], R[k1], L[k1], mat);else m.quad(L[k], L[k1], R[k1], R[k], mat);}
      return m;
    }

    // Solid of revolution about the x axis: profile = closed list of [x, r].
    function latheX(m, profile, mat, seg = 24, centre = [0, 0, 0]) {
      const rings = [];
      for (let k = 0; k < seg; k++) {const a = 2 * Math.PI * k / seg;rings.push(profile.map(([x, r]) => add(centre, [x, r * Math.cos(a), r * Math.sin(a)])));}
      // The Python wound every lathe inside out (all its profiles run clockwise in (x, r)); wind by the profile's
      // orientation so the faces point outwards and survive back-face culling.
      const n = profile.length;let area = 0;
      for (let i = 0; i < n; i++) {const p = profile[i], q = profile[(i + 1) % n];area += p[0] * q[1] - q[0] * p[1];}
      for (let k = 0; k < seg; k++) {
        const k1 = (k + 1) % seg;
        for (let i = 0; i < n; i++) {
          const i1 = (i + 1) % n;
          if (area < 0) m.quad(rings[k][i], rings[k1][i], rings[k1][i1], rings[k][i1], mat);else m.quad(rings[k][i], rings[k][i1], rings[k1][i1], rings[k1][i], mat);
        }
      }
      return m;
    }

    // An earthmover / truck tyre with tread lugs (axle along x) and a steel rim with bolts.
    function tyre(m, centre, radius, width, matTyre, matRim, {rimR = radius * .58, lugs = 22, rimDish = .08} = {}) {
      const hw = width / 2;
      latheX(m, [[-hw, rimR * 1.02], [-hw - .01, (rimR + radius) / 2], [-hw + .03, radius - .05], [-hw + .1, radius - .015],
        [hw - .1, radius - .015], [hw - .03, radius - .05], [hw + .01, (rimR + radius) / 2], [hw, rimR * 1.02]], matTyre, 24, centre);
      for (let k = 0; k < lugs; k++) for (const side of [-1, 1]) {
        const a = 2 * Math.PI * (k + (side > 0 ? .5 : 0)) / lugs, blk = new Geo();
        box(blk, [side * hw * .45, radius + .025, 0], [hw * .75, .05, 2 * Math.PI * radius / lugs * .45], matTyre, {rot: rotY(side * .45)});
        m.add(blk.transformed(centre, rotX(a)));
      }
      latheX(m, [[-hw + .05, rimR], [hw - .05, rimR], [hw - .05, rimR * .9], [rimDish, rimR * .85], [rimDish, rimR * .25],
        [rimDish + .06, rimR * .22], [rimDish + .06, .01], [-hw + .05, .01]], matRim, 16, centre);
      for (let k = 0; k < 10; k++) {
        const a = 2 * Math.PI * k / 10, p = add(centre, [rimDish + .07, Math.cos(a) * rimR * .33, Math.sin(a) * rimR * .33]);
        cyl(m, p, add(p, [.04, 0, 0]), .025, matRim, {seg: 6});
      }
      return m;
    }

    // A crawler track: a loop of shoes with grousers around a front idler and a rear sprocket, plus rollers.
    function track(m, length, height, width, matShoe, matFrame, centreX, {ground = 0, frontR = .36, rearR = .42, rearUp = 0, shoes = 44} = {}) {
      const zf = -length / 2 + frontR, zr = length / 2 - rearR, yf = ground + frontR, yr = ground + rearR + rearUp;
      const arc = (cz, cy, r, a0, a1, n) => Array.from({length: n}, (_, i) => {const a = a0 + (a1 - a0) * i / (n - 1);return [cz + r * Math.sin(a), cy - r * Math.cos(a)];});
      const path = [[zf, ground], [zr, ground], ...arc(zr, yr, rearR, 0, Math.PI, 30), [zf, yf + frontR], ...arc(zf, yf, frontR, Math.PI, 2 * Math.PI, 30)];
      const N = path.length, seglen = path.map((p, i) => Math.hypot(path[(i + 1) % N][0] - p[0], path[(i + 1) % N][1] - p[1]));
      const cum = [0];for (const s of seglen) cum.push(cum[cum.length - 1] + s);
      const total = cum[N];
      const at = s => {
        s = ((s % total) + total) % total;
        let i = cum.findIndex(c => c >= s) - 1;i = Math.max(0, Math.min(i, N - 1));
        const p0 = path[i], p1 = path[(i + 1) % N], L = Math.max(seglen[i], 1e-9), t = (s - cum[i]) / L;
        return {p: [p0[0] + (p1[0] - p0[0]) * t, p0[1] + (p1[1] - p0[1]) * t], d: [(p1[0] - p0[0]) / L, (p1[1] - p0[1]) / L]};
      };
      const pitch = total / shoes, cy = (yf + yr) / 2, cz = (zf + zr) / 2;
      const shoe = new Geo();
      box(shoe, [0, .025, 0], [width, .05, pitch * .92], matShoe);
      box(shoe, [0, .065, 0], [width - .01, .04, .05], matShoe);      // grouser (5 mm in from the shoe's sides: no coplanar faces)
      for (let k = 0; k < shoes; k++) {
        const {p: [z, y], d} = at(k * pitch);
        let perp = [0, d[0], -d[1]];
        if (dot(perp, [0, y - cy, z - cz]) < 0) perp = scl(perp, -1);
        const zc = cross([1, 0, 0], perp), Rm = [[1, perp[0], zc[0]], [0, perp[1], zc[1]], [0, perp[2], zc[2]]];
        m.add(shoe.transformed([centreX, y, z], Rm));
      }
      for (const [z, y, r, teeth] of [[zf, yf, frontR * .92, 0], [zr, yr, rearR * .9, 11]]) {
        latheX(m, [[-width * .35, r], [width * .35, r], [width * .35, .05], [-width * .35, .05]], matFrame, 16, [centreX, y, z]);
        for (let k = 0; k < teeth; k++) {
          const a = 2 * Math.PI * k / teeth;
          box(m, [centreX, y + Math.cos(a) * (r + .03), z + Math.sin(a) * (r + .03)], [width * .5, .08, .08], matFrame, {rot: rotX(-a)});
        }
      }
      for (let k = 0; k < 5; k++) {
        const z = zf + (zr - zf) * (k + .5) / 5;
        latheX(m, [[-width * .3, .13], [width * .3, .13], [width * .3, .03], [-width * .3, .03]], matFrame, 10, [centreX, ground + .2, z]);
      }
      box(m, [centreX, ground + .45 + rearUp / 2, (zf + zr) / 2], [width * .55, .38, (zr - zf) * .92], matFrame, {bevel: .04});
      return m;
    }

    // A box-section lattice girder from a to b (cross-section w x h, 'up' = +y unless the girder is vertical).
    function lattice(m, a, b, w, h, mat, {panels = 0, chord = .06, brace = .03, topOpen = false} = {}) {
      const dd = sub(b, a), L = len(dd), d = scl(dd, 1 / L);
      let up = Math.abs(d[1]) < .9 ? [0, 1, 0] : [0, 0, 1];
      const side = unit(cross(d, up));up = cross(side, d);
      const corners = [[-1, -1], [1, -1], [1, 1], [-1, 1]];
      panels = panels || Math.max(1, Math.round(L / Math.max(w, h, .8)));
      const off = c => add(scl(side, c[0] * w / 2), scl(up, c[1] * h / 2));
      for (const c of corners) cyl(m, add(a, off(c)), add(b, off(c)), chord, mat, {seg: 5, caps: false});
      for (let k = 0; k < panels; k++) {
        const p0 = add(a, scl(d, L * k / panels)), p1 = add(a, scl(d, L * (k + 1) / panels));
        for (let i = 0; i < 4; i++) {
          if (topOpen && i === 2) continue;
          const c0 = corners[i], c1 = corners[(i + 1) % 4], [q0, q1] = k % 2 === 0 ? [p0, p1] : [p1, p0];
          cyl(m, add(q0, off(c0)), add(q1, off(c1)), brace, mat, {seg: 4, caps: false});
        }
        for (let i = 0; i < 4; i++) cyl(m, add(p1, off(corners[i])), add(p1, off(corners[(i + 1) % 4])), brace, mat, {seg: 4, caps: false});
      }
      return m;
    }

    // ---------- material slots ----------
    const Y = 'Yellow', DK = 'Dark', ST = 'Steel', CH = 'Chrome', RB = 'Rubber', GL = 'Glass', LT = 'Light', OR = 'Beacon', WH = 'White',
      BL = 'Blue', RD = 'Red', DIRT = 'Soil', CONC = 'Concrete', GALV = 'Galv', WIN = 'Window';
    const has = name => lib && typeof lib[name] === 'function';
    const std = o => new T.MeshStandardMaterial(o);
    const paint = (c, r) => has('paint') ? lib.paint(c, r) : std({color: c, roughness: r, metalness: 0});
    const canvasOK = typeof document !== 'undefined' && typeof document.createElement === 'function';
    function canvasTexture(w, h, draw, repeat, offset) {
      const c = document.createElement('canvas');c.width = w;c.height = h;draw(c.getContext('2d'), w, h);
      const t = new T.CanvasTexture(c);t.colorSpace = T.SRGBColorSpace;t.anisotropy = 4;
      if (repeat) {t.wrapS = t.wrapT = T.RepeatWrapping;t.repeat.set(repeat[0], repeat[1]);}
      if (offset) t.offset.set(offset[0], offset[1]);
      return t;
    }
    const MAKERS = {
      Yellow: () => new T.MeshPhysicalMaterial({color: 0xf0b400, roughness: .45, metalness: .1, clearcoat: .4}),
      Dark: () => paint(0x1f2022, .6),
      Steel: () => has('metal') ? lib.metal(0x6c7074, .4) : std({color: 0x6c7074, roughness: .4, metalness: .9}),
      Chrome: () => has('chrome') ? lib.chrome() : std({color: 0xe8ecef, roughness: .08, metalness: 1}),
      Rubber: () => std({color: 0x161616, roughness: .9}),
      Glass: () => new T.MeshPhysicalMaterial({color: 0x3a4a52, transparent: true, opacity: .2, roughness: .05, metalness: 0, depthWrite: false, side: T.DoubleSide}),
      Window: () => std({color: 0x1d262c, roughness: .08, metalness: .4}),          // office container windows (no interior behind)
      Light: () => std({color: 0xffffff, emissive: 0xfff2d8, emissiveIntensity: 1.6, roughness: .3}),
      Beacon: () => std({color: 0xff8a10, emissive: 0xff7300, emissiveIntensity: 1.8, roughness: .3}),
      White: () => paint(0xe8e8e4, .45),
      Blue: () => paint(0x16347a, .45),
      Red: () => paint(0x96201a, .55),
      Soil: () => has('concrete') ? lib.concrete(0x6b5541) : std({color: 0x6b5541, roughness: 1}),
      Concrete: () => has('concrete') ? lib.concrete(0xa9a7a0) : std({color: 0xa9a7a0, roughness: .95}),
      Gravel: () => has('concrete') ? lib.concrete(0x8a8378) : std({color: 0x8a8378, roughness: 1}),
      Galv: () => has('metal') ? lib.metal(0xb4b8ba, .5) : std({color: 0xb4b8ba, roughness: .5, metalness: .85}),
      Container: () => paint(0x5d7489, .55),
      Container2: () => paint(0xe6e6e0, .55),
      Door: () => paint(0x5a5a5c, .5),
      Toilet: () => has('polymer') ? lib.polymer(0x1e6e3c) : paint(0x1e6e3c, .5),
      Toilet2: () => has('polymer') ? lib.polymer(0xdcdcd7) : paint(0xdcdcd7, .5),
      Skip: () => paint(0xd8772a, .65),
      Wood: () => has('wood') ? lib.wood(0x7a5c3c, .85) : paint(0x96734b, .85),
      Brick: () => has('brick') ? lib.brick() : paint(0xa0563f, .9),
      Mesh: () => canvasOK
        ? std({color: 0xc8ccce, roughness: .5, metalness: .6, alphaTest: .5, map: canvasTexture(64, 64, (g, w, h) => {
          g.clearRect(0, 0, w, h);g.fillStyle = '#ffffff';g.fillRect(0, 0, 4, h);g.fillRect(w / 2, 0, 4, h);g.fillRect(0, 0, w, 4);
        }, [1 / .2, 1 / .3])})
        : std({color: 0xc8ccce, roughness: .5, metalness: .6, transparent: true, opacity: .35, depthWrite: false}),
      Banner: () => canvasOK
        ? std({roughness: .8, map: canvasTexture(1024, 280, (g, w, h) => {
          g.fillStyle = '#1f5a36';g.fillRect(0, 0, w, h);g.fillStyle = '#f0b400';g.fillRect(0, h - 26, w, 26);g.fillRect(0, 0, w, 10);
          g.fillStyle = '#ffffff';g.textAlign = 'center';g.textBaseline = 'middle';g.font = 'bold 120px sans-serif';g.fillText('HAARKLOUS PLASS', w / 2, h * .42, w * .92);
          g.font = '52px sans-serif';g.fillText('Byggeplass – adgang forbudt', w / 2, h * .78, w * .92);
        }, [1 / 3.3, 1 / .9], [.5, -.75 / .9])})
        : paint(0x1f5a36, .8),
      Sign: () => canvasOK
        ? std({roughness: .6, map: canvasTexture(512, 360, (g, w, h) => {
          g.fillStyle = '#ffffff';g.fillRect(0, 0, w, h);g.strokeStyle = '#c0161c';g.lineWidth = 22;g.strokeRect(11, 11, w - 22, h - 22);
          g.fillStyle = '#111111';g.textAlign = 'center';g.textBaseline = 'middle';g.font = 'bold 74px sans-serif';g.fillText('BYGGEPLASS', w / 2, h * .36, w * .86);
          g.font = '40px sans-serif';g.fillText('Adgang forbudt for', w / 2, h * .6);g.fillText('uvedkommende', w / 2, h * .74);
        }, [1, 1 / .7], [.5, -1.45 / .7])})
        : paint(0xf2f2f2, .6)
    };
    const NO_SHADOW = new Set([GL, LT, OR, 'Mesh']);
    const mats = new Map();
    const mat = slot => {
      if (!mats.has(slot)) {const m = (MAKERS[slot] || (() => paint(0x8a8a8a, .6)))();m.name = 'Site_' + slot;mats.set(slot, m);}
      return mats.get(slot);
    };

    // ---------- Geo -> BufferGeometry (box-projected UVs in metres, normals smoothed below 40 degrees) ----------
    const COS_SMOOTH = Math.cos(40 * Math.PI / 180);   // 10-sided rams smooth, 45 degree chamfers stay sharp
    function bufferGeometry(arr) {
      const nt = arr.length / 9, area = new Float32Array(nt * 3), un = new Float32Array(nt * 3), uv = new Float32Array(nt * 6), nor = new Float32Array(arr.length);
      for (let t = 0; t < nt; t++) {
        const i = t * 9, ex = arr[i + 3] - arr[i], ey = arr[i + 4] - arr[i + 1], ez = arr[i + 5] - arr[i + 2], fx = arr[i + 6] - arr[i], fy = arr[i + 7] - arr[i + 1], fz = arr[i + 8] - arr[i + 2];
        const nx = ey * fz - ez * fy, ny = ez * fx - ex * fz, nz = ex * fy - ey * fx, l = Math.hypot(nx, ny, nz) || 1;
        area[t * 3] = nx;area[t * 3 + 1] = ny;area[t * 3 + 2] = nz;un[t * 3] = nx / l;un[t * 3 + 1] = ny / l;un[t * 3 + 2] = nz / l;
        const ax = Math.abs(nx), ay = Math.abs(ny), az = Math.abs(nz), [u, v] = ax >= ay && ax >= az ? [2, 1] : ay >= az ? [0, 2] : [0, 1];
        for (let j = 0; j < 3; j++) {uv[t * 6 + j * 2] = arr[i + j * 3 + u];uv[t * 6 + j * 2 + 1] = arr[i + j * 3 + v];}
      }
      const groups = new Map();
      for (let c = 0; c < nt * 3; c++) {
        const k = Math.round(arr[c * 3] * 2000) + ',' + Math.round(arr[c * 3 + 1] * 2000) + ',' + Math.round(arr[c * 3 + 2] * 2000);
        let g = groups.get(k);if (!g) groups.set(k, g = []);g.push(c);
      }
      for (const g of groups.values()) for (const c of g) {
        const t = (c / 3) | 0;let x = 0, y = 0, z = 0;
        for (const c2 of g) {
          const t2 = (c2 / 3) | 0;
          if (un[t * 3] * un[t2 * 3] + un[t * 3 + 1] * un[t2 * 3 + 1] + un[t * 3 + 2] * un[t2 * 3 + 2] > COS_SMOOTH) {x += area[t2 * 3];y += area[t2 * 3 + 1];z += area[t2 * 3 + 2];}
        }
        const l = Math.hypot(x, y, z);
        if (l > 1e-12) {nor[c * 3] = x / l;nor[c * 3 + 1] = y / l;nor[c * 3 + 2] = z / l;} else {nor[c * 3] = un[t * 3];nor[c * 3 + 1] = un[t * 3 + 1];nor[c * 3 + 2] = un[t * 3 + 2];}
      }
      const geo = new T.BufferGeometry();
      geo.setAttribute('position', new T.BufferAttribute(new Float32Array(arr), 3));
      geo.setAttribute('normal', new T.BufferAttribute(nor, 3));
      geo.setAttribute('uv', new T.BufferAttribute(uv, 2));
      geo.computeBoundingBox();geo.computeBoundingSphere();
      return geo;
    }
    // One T.Mesh per material slot under `group`; the BufferGeometries are built once per Geo and shared.
    function addMeshes(group, geo) {
      if (!geo.buffers) geo.buffers = [...geo.S].filter(([, s]) => s.length).map(([slot, s]) => [slot, bufferGeometry(s)]);
      for (const [slot, g] of geo.buffers) {
        const m = new T.Mesh(g, mat(slot));m.name = slot;
        m.castShadow = !NO_SHADOW.has(slot);m.receiveShadow = !NO_SHADOW.has(slot);group.add(m);
      }
      return group;
    }

    // ---------- rigs ----------
    class Rig {
      constructor(name) {this.name = name;this.nodes = [];}
      node(name, parent = null, pos = [0, 0, 0], mesh = null, visible = true) {
        this.nodes.push({name, parent, pos: pos.map(Number), mesh: mesh && mesh.count ? mesh : null, visible});
        return name;
      }
    }
    function addRams(r, parent, count) {
      for (let i = 0; i < count; i++) {
        const b = cyl(new Geo(), [0, -.5, 0], [0, .5, 0], .09, DK, {seg: 10}), rod = cyl(new Geo(), [0, -.5, 0], [0, .5, 0], .055, CH, {seg: 8});
        r.node(`rams[${i}].barrel`, parent, [0, 0, 0], b);
        r.node(`rams[${i}].rod`, parent, [0, 0, 0], rod);
      }
    }
    function beacon(m, c) {
      cyl(m, c, [c[0], c[1] + .05, c[2]], .07, DK, {seg: 10});
      cyl(m, [c[0], c[1] + .05, c[2]], [c[0], c[1] + .2, c[2]], .065, OR, {seg: 10, r2: .05});
    }
    function workLight(m, c, facing = -1) {
      box(m, c, [.2, .14, .1], DK, {bevel: .02});
      box(m, [c[0], c[1], c[2] + facing * .052], [.16, .1, .01], LT);
    }
    // A machine cab: four posts, glass all round, a roof and a floor (glass panes 1 cm narrower than the gap between
    // the posts, so their edges do not touch the posts' inner faces).
    function cabBox(m, c, w, h, d, {frame = Y, roof = Y, post = .06} = {}) {
      const [cx, cy, cz] = c, g = .02;
      for (const sx of [-1, 1]) for (const sz of [-1, 1]) box(m, [cx + sx * (w / 2 - post / 2), cy + h / 2, cz + sz * (d / 2 - post / 2)], [post, h, post], frame);
      box(m, [cx, cy + h + .05, cz], [w + .1, .1, d + .1], roof, {bevel: .03});
      box(m, [cx, cy + .045, cz], [w - .01, .08, d - .01], DK);         // floor, 5 mm inside the posts' outer faces and bottoms
      box(m, [cx, cy + h / 2, cz - d / 2 + g], [w - post * 2 - .01, h - .08, .015], GL);
      box(m, [cx, cy + h / 2, cz + d / 2 - g], [w - post * 2 - .01, h - .08, .015], GL);
      for (const sx of [-1, 1]) box(m, [cx + sx * (w / 2 - g), cy + h / 2, cz], [.015, h - .08, d - post * 2 - .01], GL);
      box(m, [cx, cy + .45, cz + d * .15], [.5, .1, .5], DK);           // seat and controls seen through the glass
      box(m, [cx, cy + .8, cz + d * .38], [.5, .6, .1], DK);
      return m;
    }

    function dozer() {
      const r = new Rig('site_dozer'), root = r.node('root'), b = new Geo();
      box(b, [0, .78, .05], [1.55, .5, 3.1], DK, {bevel: .05});                              // main frame
      prism(b, [[-1.9, .95], [-.2, .95], [-.2, 2.05], [-1.62, 2.02], [-1.9, 1.78]], -.66, .66, Y);   // engine hood
      box(b, [0, 1.4, -1.92], [1.15, .7, .06], DK);                                           // radiator grille
      for (let k = 0; k < 7; k++) box(b, [0, 1.12 + k * .085, -1.955], [1.05, .025, .02], ST);
      cyl(b, [.36, 2.0, -1.15], [.36, 2.8, -1.15], .075, DK, {seg: 10});                      // exhaust
      cyl(b, [-.34, 2.0, -1.05], [-.34, 2.35, -1.05], .11, DK, {seg: 12});                    // pre-cleaner
      for (const sx of [-1, 1]) {
        box(b, [sx * 1.03, 1.06, .2], [.62, .06, 3.0], Y, {bevel: .02});                     // fenders
        box(b, [sx * 1.3, .86, .2], [.06, .4, 2.99], Y);                                     // (5 mm shorter than the fenders)
        box(b, [sx * .72, 1.25, 1.245], [.12, .5, .89], DK);                                 // tanks (end 5 mm before the fenders' end)
        box(b, [sx * 1.18, 1.25, .95], [.2, .05, .4], ST);                                    // step
      }
      cabBox(b, [0, 1.32, .86], 1.36, 1.62, 1.24);
      box(b, [0, 1.12, 1.68], [1.35, .55, .45], DK, {bevel: .06});                            // rear counterweight / ripper frame
      for (const sx of [-1, 1]) {workLight(b, [sx * .55, 3.02, .22], -1);workLight(b, [sx * .55, 3.02, 1.47], 1);}
      beacon(b, [.5, 3.04, 1.3]);
      cyl(b, [-.62, 1.2, -.4], [-.62, 2.3, .2], .025, Y, {seg: 6});                          // grab rail
      r.node('body', root, [0, 0, 0], b);
      [-1, 1].forEach((sx, i) => r.node(`tracks[${i}]`, root, [sx * .99, 0, 0], track(new Geo(), 3.05, .9, .56, ST, DK, 0, {frontR: .36, rearR: .42, shoes: 42})));
      // Blade on push arms outside the tracks: arm pivot on the track frame, blade at the arms' front ends.
      const arm = r.node('bladeArm', root, [0, .62, -.45]), a = new Geo();
      for (const sx of [-1, 1]) {
        box(a, [sx * 1.42, -.22, -.98], [.16, .26, 2.05], Y, {rot: rotX(.2), bevel: .03});
        cyl(a, [sx * 1.34, 0, 0], [sx * 1.5, 0, 0], .11, DK, {seg: 10});
      }
      r.node('bladeArmMesh', arm, [0, 0, 0], a);
      const bl = new Geo(), front = [[0, 0], [.13, .3], [.19, .6], [.15, .9], [.03, 1.1], [-.12, 1.25]], back = front.map(([z, y]) => [z + .12, y]);
      for (let k = 0; k < front.length - 1; k++) {
        let [[z0, y0], [z1, y1]] = [front[k], front[k + 1]];
        bl.quad([1.8, y0, z0], [-1.8, y0, z0], [-1.8, y1, z1], [1.8, y1, z1], Y);
        [[z0, y0], [z1, y1]] = [back[k], back[k + 1]];
        bl.quad([-1.8, y0, z0], [1.8, y0, z0], [1.8, y1, z1], [-1.8, y1, z1], DK);
      }
      for (const sx of [-1, 1]) {
        // End plates: the curved band between the front and back skins as a strip (the Python fanned it, which
        // covered the concave side); each quad wound to face outwards (sx).
        for (let k = 0; k < front.length - 1; k++) {
          const q = [[sx * 1.8, front[k][1], front[k][0]], [sx * 1.8, front[k + 1][1], front[k + 1][0]], [sx * 1.8, back[k + 1][1], back[k + 1][0]], [sx * 1.8, back[k][1], back[k][0]]];
          bl.quad(...(cross(sub(q[1], q[0]), sub(q[2], q[0]))[0] * sx < 0 ? q.reverse() : q), Y);
        }
        box(bl, [sx * 1.82, .55, .1], [.04, 1.1, .45], Y);                                    // end bits
      }
      box(bl, [0, .05, .03], [3.59, .1, .08], ST);                                            // cutting edge (ends 5 mm inside the end plates)
      for (let k = 0; k < 5; k++) box(bl, [-1.4 + k * .7, .62, .3], [.06, 1.0, .2], DK);       // back ribs
      r.node('blade', arm, [0, -.62, -1.95], bl);
      r.node('bladeEdge', arm, [0, -.62, -1.95]);
      addRams(r, root, 2);
      return r;
    }

    function excavator() {
      const r = new Rig('site_excavator'), root = r.node('root'), b = new Geo();
      box(b, [0, .78, 0], [1.35, .45, 1.9], DK, {bevel: .06});
      cyl(b, [0, .98, 0], [0, 1.06, 0], 1.0, DK, {seg: 24});
      r.node('body', root, [0, 0, 0], b);
      [-1, 1].forEach((sx, i) => r.node(`tracks[${i}]`, root, [sx * 1.3, 0, 0], track(new Geo(), 4.65, .95, .6, ST, DK, 0, {frontR: .38, rearR: .38, shoes: 56})));
      const upper = r.node('upper', root, [0, 1.06, 0]), u = new Geo();
      box(u, [.12, .16, .7], [2.95, .32, 4.3], DK, {bevel: .05});                             // deck
      box(u, [0, .78, 2.45], [2.9, 1.15, .95], Y, {bevel: .22});                              // counterweight
      box(u, [0, .3, 2.45], [2.8, .25, .9], DK, {bevel: .06});
      box(u, [.55, 1.0, 1.25], [1.8, .55, 1.5], Y, {bevel: .08});                             // engine hood
      for (let k = 0; k < 6; k++) box(u, [.55, 1.28, .7 + k * .2], [1.5, .02, .08], DK);       // louvres
      box(u, [1.05, .8, -.2], [.85, 1.0, 1.75], Y, {bevel: .06});                             // right-hand tanks / boxes
      box(u, [.15, .62, -.9], [.85, .9, .9], DK, {bevel: .05});                               // boom foot
      cyl(u, [1.0, 1.25, 1.0], [1.0, 1.85, 1.0], .07, DK, {seg: 10});                         // exhaust
      for (const z of [-1.0, .4]) cyl(u, [1.45, .32, z], [1.45, 1.35, z], .025, Y, {seg: 6});
      cyl(u, [1.45, 1.35, -1.0], [1.45, 1.35, .4], .025, Y, {seg: 6});                        // hand rail
      beacon(u, [.6, 1.3, 1.9]);
      r.node('upperMesh', upper, [0, 0, 0], u);
      const cab = r.node('cab', upper, [-.85, .32, -.5]), c = cabBox(new Geo(), [0, 0, 0], 1.0, 1.7, 1.75);
      workLight(c, [.3, 1.82, -.85], -1);
      r.node('cabMesh', cab, [0, 0, 0], c);
      for (let i = 0; i < 2; i++) r.node(`levers[${i}]`, cab, [(i === 0 ? -1 : 1) * .3, .7, .25]);
      const boom = r.node('boomPivot', upper, [.15, 1.0, -1.15]), bm = new Geo();
      const path = [[0, 0, 0], [0, .55, -1.5], [0, .95, -3.0], [0, .62, -4.8], [0, 0, -6.15]], sizes = [[.62, .82], [.6, .8], [.56, .74], [.5, .64]];
      for (let k = 0; k < 4; k++) {
        const p0 = path[k], p1 = path[k + 1], d = sub(p1, p0), L = len(d), ang = Math.atan2(d[1], -d[2]);
        box(bm, scl(add(p0, p1), .5), [sizes[k][0], sizes[k][1], L + .25], Y, {rot: rotX(ang), bevel: .06});
      }
      cyl(bm, [-.36, 0, 0], [.36, 0, 0], .16, DK, {seg: 12});
      cyl(bm, [-.3, 0, -6.15], [.3, 0, -6.15], .13, DK, {seg: 12});
      r.node('boomMesh', boom, [0, 0, 0], bm);
      const stick = r.node('stickPivot', boom, [0, 0, -6.15]), sm = new Geo();
      box(sm, [0, .05, -1.35], [.42, .58, 3.3], Y, {bevel: .05});
      box(sm, [0, .32, .3], [.4, .5, .7], Y, {bevel: .05, rot: rotX(-.4)});
      cyl(sm, [-.26, 0, -3.0], [.26, 0, -3.0], .11, DK, {seg: 12});
      r.node('stickMesh', stick, [0, 0, 0], sm);
      const bucket = r.node('bucketPivot', stick, [0, 0, -3.0]), bk = new Geo();
      // Backhoe bucket: the shell bulges to the stick's +y side, so with the teeth pointing down the opening (local -y)
      // faces the machine, and curling (bucket angle down) turns it up.
      const wrap = [[.05, -.25], [.32, .2], [.3, .65], [.05, 1.0], [-.4, 1.2], [-.9, 1.15], [-1.3, .9], [-1.45, .45]], W = .72;
      for (let k = 0; k < wrap.length - 1; k++) {
        const [[z0, y0], [z1, y1]] = [wrap[k], wrap[k + 1]];
        bk.quad([-W, y0, z0], [W, y0, z0], [W, y1, z1], [-W, y1, z1], Y);
        bk.quad([W - .02, y0 - .02, z0 - .02], [-W + .02, y0 - .02, z0 - .02], [-W + .02, y1 - .02, z1 - .02], [W - .02, y1 - .02, z1 - .02], ST);
      }
      for (const sx of [-1, 1]) {
        const pts = wrap.map(([z, y]) => [sx * W, y, z]).concat([[sx * W, -.1, -.4]]);
        bk.poly(sx < 0 ? pts : pts.reverse(), Y);
      }
      for (let k = 0; k < 5; k++) box(bk, [-.56 + k * .28, .42, -1.56], [.1, .1, .3], ST, {rot: rotX(-.5), bevel: .02});
      cyl(bk, [-.4, 0, 0], [.4, 0, 0], .1, DK, {seg: 10});
      r.node('bucketMesh', bucket, [0, 0, 0], bk);
      r.node('tip', bucket, [0, .5, -1.62]);
      r.node('dirt', bucket, [0, .55, -.55], box(new Geo(), [0, 0, 0], [1.2, .5, .9], DIRT, {bevel: .18}), false);
      addRams(r, root, 3);
      return r;
    }

    function loader() {
      const r = new Rig('site_loader'), root = r.node('root'), rear = new Geo();
      box(rear, [0, .95, 1.55], [1.3, .6, 2.7], DK, {bevel: .05});
      box(rear, [0, 1.72, 2.35], [1.55, .95, 1.85], Y, {bevel: .14});                        // engine hood
      for (let k = 0; k < 5; k++) box(rear, [0, 2.2, 1.8 + k * .25], [1.3, .02, .09], DK);
      box(rear, [0, 1.15, 3.35], [2.25, .95, .5], Y, {bevel: .18});                          // counterweight
      for (const sx of [-1, 1]) {
        box(rear, [sx * .85, 1.3, 3.61], [.22, .12, .03], RD);                                // tail lights
        box(rear, [sx * 1.0, 1.72, 1.6], [.72, .06, 1.25], Y, {bevel: .03});                  // fenders
        box(rear, [sx * 1.0, 1.15, 2.05], [.72, .5, .06], Y);
      }
      cabBox(rear, [0, 1.55, .6], 1.5, 1.75, 1.6);                                          // a high cab: the operator sees the bucket
      for (const sx of [-1, 1]) workLight(rear, [sx * .62, 3.4, -.18], -1);
      beacon(rear, [.55, 3.4, 1.3]);
      cyl(rear, [.45, 2.2, 2.6], [.45, 2.8, 2.6], .07, DK, {seg: 10});
      for (let k = 0; k < 4; k++) box(rear, [-.95, .45 + k * .32, .2], [.35, .04, .2], ST);   // ladder
      r.node('rear', root, [0, 0, 0], rear);
      [-1, 1].forEach((sx, i) => r.node(`rwheels[${i}]`, root, [sx * 1.0, .8, 1.6], tyre(new Geo(), [0, 0, 0], .8, .6, RB, Y, {rimDish: .08 * sx})));
      const front = r.node('front', root), f = new Geo();
      box(f, [0, .95, -1.1], [1.15, .6, 2.1], DK, {bevel: .05});
      box(f, [0, 1.4, -.45], [.9, .5, .6], Y, {bevel: .05});
      // Short, low front fenders: the bucket shows over them from the seat (UE commit aa26d86).
      for (const sx of [-1, 1]) {box(f, [sx * 1.0, 1.66, -1.5], [.72, .05, 1.0], Y, {bevel: .02});workLight(f, [sx * .65, 1.7, -1.95], -1);}
      r.node('frontMesh', front, [0, 0, 0], f);
      [-1, 1].forEach((sx, i) => r.node(`fwheels[${i}]`, front, [sx * 1.0, .8, -1.6], tyre(new Geo(), [0, 0, 0], .8, .6, RB, Y, {rimDish: .08 * sx})));
      const arm = r.node('liftArm', front, [0, 1.85, -.7]), am = new Geo();
      for (const sx of [-1, 1]) box(am, [sx * .62, -.72, -1.18], [.2, .42, 2.75], Y, {rot: rotX(-Math.atan2(1.45, 2.35) + .08), bevel: .04});
      box(am, [0, -.95, -1.45], [1.24, .28, .28], Y, {bevel: .04});                          // cross tube
      r.node('liftArmMesh', arm, [0, 0, 0], am);
      const bucket = r.node('bucket', arm, [0, -1.45, -2.35]), bk = new Geo();
      const prof = [[-1.25, -.35], [.1, -.42], [.28, -.2], [.3, .3], [.18, .8], [-.25, .9]], W = 1.45;
      for (let k = 0; k < prof.length - 1; k++) {
        const [[z0, y0], [z1, y1]] = [prof[k], prof[k + 1]];
        bk.quad([-W, y0, z0], [W, y0, z0], [W, y1, z1], [-W, y1, z1], Y);
        bk.quad([W - .02, y0 + .03, z0 - .02], [-W + .02, y0 + .03, z0 - .02], [-W + .02, y1 + .03, z1 - .02], [W - .02, y1 + .03, z1 - .02], ST);
      }
      for (const sx of [-1, 1]) {const pts = prof.map(([z, y]) => [sx * W, y, z]);bk.poly(sx < 0 ? pts : pts.reverse(), Y);}
      box(bk, [0, -.37, -1.27], [2.89, .06, .12], ST);                                        // cutting edge (ends 5 mm inside the sides)
      r.node('bucketMesh', bucket, [0, 0, 0], bk);
      r.node('lip', bucket, [0, -.36, -1.3]);
      r.node('dirt', bucket, [0, -.1, -.55], box(new Geo(), [0, 0, 0], [2.6, .55, 1.1], DIRT, {bevel: .2}), false);
      addRams(r, root, 3);
      return r;
    }

    // An open dump body from z = -length to 0 (hinge at the rear end), floor at y = 0, rising at the back.
    function truckBody(m, length, width, height, mt) {
      const L = length, W = width / 2, H = height, floor = [[-L, 0], [-.6, 0], [0, .35]];
      for (const sx of [-1, 1]) {
        const side = [[sx * W, 0, -L], [sx * W, H, -L - .1], [sx * W, H, 0], [sx * W, .35, 0], [sx * W, 0, -.6]];
        m.poly(sx > 0 ? side : side.reverse(), mt);
        // Steel lining facing into the body (the Python wound both linings and the front wall below the wrong way
        // round, so from inside the body the walls were culled).
        const wi = sx * (W - .05), lining = [[wi, 0, -L], [wi, .35, 0], [wi, H - .05, 0], [wi, H - .05, -L]];
        m.poly(sx > 0 ? lining : lining.reverse(), ST);
      }
      for (let k = 0; k < floor.length - 1; k++) {
        const [[z0, y0], [z1, y1]] = [floor[k], floor[k + 1]];
        m.quad([W, y0, z0], [-W, y0, z0], [-W, y1, z1], [W, y1, z1], ST);
        m.quad([-W, y0 - .08, z0], [W, y0 - .08, z0], [W, y1 - .08, z1], [-W, y1 - .08, z1], mt);
      }
      m.quad([-W, H + .25, -L - .25], [W, H + .25, -L - .25], [W, 0, -L], [-W, 0, -L], mt);        // front wall / cab guard (faces forward)
      m.quad([W, H + .2, -L - .18], [-W, H + .2, -L - .18], [-W, .02, -L + .05], [W, .02, -L + .05], ST);   // its lining (faces into the body)
      box(m, [0, H + .3, -L - .45], [width, .06, .6], mt);                                      // canopy
      for (let k = 0; k < 4; k++) box(m, [0, -.14, -L + .4 + k * (L - .8) / 3], [width - .1, .12, .12], DK);   // floor ribs
      return m;
    }

    function hauler() {
      const r = new Rig('site_hauler'), root = r.node('root'), t = new Geo();
      box(t, [0, 1.0, -2.2], [1.3, .6, 2.6], DK, {bevel: .05});
      box(t, [0, 1.65, -3.25], [2.3, 1.0, 1.4], Y, {bevel: .16});                             // engine hood (front)
      for (let k = 0; k < 6; k++) box(t, [0, 1.35 + k * .1, -3.96], [1.6, .03, .02], DK);
      box(t, [0, .95, -3.95], [2.5, .3, .25], DK, {bevel: .05});                               // bumper
      for (const sx of [-1, 1]) {box(t, [sx * .95, 1.35, -3.96], [.25, .14, .03], LT);box(t, [sx * 1.08, 1.75, -2.1], [.75, .06, 1.6], Y, {bevel: .03});}
      cabBox(t, [0, 2.15, -1.75], 1.9, 1.5, 1.5);
      beacon(t, [.6, 3.75, -1.2]);
      cyl(t, [.9, 2.1, -2.6], [.9, 2.9, -2.6], .08, DK, {seg: 10});
      for (const sx of [-1, 1]) box(t, [sx * 1.0, 3.0, -2.6], [.04, .3, .2], DK);              // mirrors
      r.node('tractor', root, [0, 0, 0], t);
      [-1, 1].forEach((sx, i) => r.node(`fwheels[${i}]`, root, [sx * 1.1, .78, -2.1], tyre(new Geo(), [0, 0, 0], .78, .62, RB, Y, {rimDish: .08 * sx})));
      const trailer = r.node('trailer', root), tr = new Geo();
      box(tr, [0, .95, 2.6], [1.15, .5, 4.6], DK, {bevel: .05});
      cyl(tr, [0, 1.0, .1], [0, 1.0, .6], .25, DK, {seg: 12});                                 // articulation joint
      for (const sx of [-1, 1]) box(tr, [sx * .85, .8, 3.12], [.18, .3, 2.0], DK);             // bogie beams
      r.node('trailerMesh', trailer, [0, 0, 0], tr);
      [[-1, 2.3], [1, 2.3], [-1, 3.95], [1, 3.95]].forEach(([sx, z], i) => r.node(`rwheels[${i}]`, trailer, [sx * 1.1, .78, z], tyre(new Geo(), [0, 0, 0], .78, .62, RB, Y, {rimDish: .08 * sx})));
      const body = r.node('body', trailer, [0, 1.6, 5.05]);
      r.node('bodyMesh', body, [0, 0, 0], truckBody(new Geo(), 5.0, 2.95, 1.55, Y));
      r.node('load', body, [0, .45, -2.5], box(new Geo(), [0, 0, 0], [2.7, 1.0, 4.4], DIRT, {bevel: .35}), false);
      addRams(r, root, 2);
      return r;
    }

    function tipper() {
      const r = new Rig('site_tipper'), root = r.node('root'), c = new Geo();
      for (const sx of [-1, 1]) {
        box(c, [sx * .45, .95, -2.2], [.12, .3, 7.6], DK);                                     // frame rails
        box(c, [sx * 1.15, .9, -1.5], [.06, .35, 1.6], DK);                                    // side guard
        box(c, [sx * .9, .85, -2.75], [.35, .5, .8], ST);                                      // tanks
      }
      const cab = new Geo();
      box(cab, [0, 2.1, -5.55], [2.48, 2.3, 2.0], WH, {bevel: .12});                           // cab
      box(cab, [0, 2.55, -6.565], [2.25, 1.0, .02], GL);                                       // windscreen (5 mm proud of the cab front)
      for (const sx of [-1, 1]) {
        box(cab, [sx * 1.245, 2.5, -5.9], [.02, .8, .9], GL);
        box(cab, [sx * 1.5, 2.62, -6.5], [.03, .34, .18], DK);                                 // mirrors (out on their arms, small in the view)
        box(cab, [sx * 1.37, 2.62, -6.45], [.24, .03, .03], DK);
        box(cab, [sx * .85, 1.05, -6.6], [.35, .15, .05], LT);
      }
      box(cab, [0, 1.4, -6.58], [1.7, .7, .04], DK);                                           // grille
      box(cab, [0, .85, -6.62], [2.5, .35, .2], DK, {bevel: .04});                             // bumper
      box(cab, [0, 3.32, -6.4], [2.3, .12, .3], WH);                                           // sun visor
      for (const sx of [-1, 1]) beacon(cab, [sx * .7, 3.27, -5.2]);
      box(cab, [0, 1.2, -4.95], [2.5, .12, .9], BL);                                           // stripe (1 cm proud of the cab sides)
      c.add(cab);
      r.node('chassis', root, [0, 0, 0], c);
      const tyreT = (m, x, dish) => tyre(m, [x, 0, 0], .52, .38, RB, ST, {rimR: .3, lugs: 18, rimDish: dish});
      [-4.9, -3.5].forEach((z, i) => {
        const st = r.node(`steer[${i}]`, root, [0, .52, z]);
        [-1, 1].forEach((sx, k) => r.node(`swheels[${i * 2 + k}]`, st, [sx * 1.05, 0, 0], tyreT(new Geo(), 0, .06 * sx)));
      });
      [-.68, .68].forEach((z, i) => [-1, 1].forEach((sx, k) => {
        const w = new Geo();tyreT(w, 0, .06 * sx);tyreT(w, -.42 * sx, -.06 * sx);
        r.node(`rwheels[${i * 2 + k}]`, root, [sx * 1.0, .52, z], w);
      }));
      const body = r.node('body', root, [0, 1.32, 1.6]);
      r.node('bodyMesh', body, [0, 0, 0], truckBody(new Geo(), 5.5, 2.5, 1.35, WH));
      r.node('load', body, [0, .4, -2.8], box(new Geo(), [0, 0, 0], [2.3, .9, 5.0], DIRT, {bevel: .3}), false);
      addRams(r, root, 1);
      return r;
    }

    function crane() {
      const r = new Rig('site_crane'), root = r.node('root'), c = new Geo();
      box(c, [0, 1.3, -.6], [2.5, .7, 9.8], Y, {bevel: .1});                                   // carrier deck
      box(c, [0, .85, -.6], [1.2, .4, 9.6], DK);
      for (const sx of [-1, 1]) for (const z of [-2.6, -1.0, 1.9]) box(c, [sx * 1.15, 1.05, z], [.3, .5, 1.5], DK);   // wheel arches
      const cab = new Geo();
      box(cab, [-.65, 2.05, -5.0], [1.1, 1.7, 1.8], Y, {bevel: .1});                           // driver cab (left)
      box(cab, [-.65, 2.4, -5.915], [.95, .8, .02], GL);                                       // glass 5 mm proud of the cab faces
      box(cab, [-1.215, 2.4, -5.0], [.02, .7, 1.2], GL);
      box(cab, [0, .95, -5.95], [2.5, .3, .25], DK, {bevel: .05});
      for (const sx of [-1, 1]) box(cab, [sx * .9, 1.15, -5.97], [.3, .14, .03], LT);
      c.add(cab);
      for (const z of [-4.0, 3.2]) box(c, [0, 1.05, z], [2.4, .45, .55], DK);                  // outrigger boxes
      r.node('carrier', root, [0, 0, 0], c);
      const axles = [-3.4, -2.0, 1.9], tyreC = sx => tyre(new Geo(), [0, 0, 0], .62, .5, RB, Y, {rimR: .38, lugs: 20, rimDish: .06 * sx});
      axles.slice(0, 2).forEach((z, i) => {
        const st = r.node(`steer[${i}]`, root, [0, .62, z]);
        [-1, 1].forEach((sx, k) => r.node(`swheels[${i * 2 + k}]`, st, [sx * 1.05, 0, 0], tyreC(sx)));
      });
      [-1, 1].forEach((sx, k) => r.node(`rwheels[${k}]`, root, [sx * 1.05, .62, axles[2]], tyreC(sx)));
      [[-1, -4.0], [1, -4.0], [-1, 3.2], [1, 3.2]].forEach(([sx, z], i) => {
        const o = new Geo();
        box(o, [sx * .2, 0, 0], [1.6, .35, .4], Y, {bevel: .03});
        box(o, [sx * 1.05, -.45, 0], [.25, 1.0, .25], DK);
        cyl(o, [sx * 1.05, -.95, 0], [sx * 1.05, -1.0, 0], .3, DK, {seg: 12});
        r.node(`out[${i}]`, root, [sx * .9, 1.05, z], o);
      });
      const sup = r.node('super', root, [0, 1.65, .9]), s = new Geo();
      cyl(s, [0, 0, 0], [0, .25, 0], 1.1, DK, {seg: 24});
      box(s, [0, .65, .9], [2.3, .8, 2.6], Y, {bevel: .12});                                   // turntable house
      box(s, [0, .75, 2.35], [2.4, 1.0, .7], DK, {bevel: .1});                                 // counterweight
      cyl(s, [-.5, 1.0, 1.5], [.5, 1.0, 1.5], .35, DK, {seg: 16});                             // winch
      cabBox(s, [-1.05, .25, -.4], .9, 1.5, 1.6);
      r.node('superMesh', sup, [0, 0, 0], s);
      const boom = r.node('boomBase', sup, [0, 1.0, 1.4]), bm = new Geo();
      box(bm, [0, 0, -4.9], [.95, 1.1, 11.2], Y, {bevel: .12});
      cyl(bm, [-.51, 0, 0], [.51, 0, 0], .2, DK, {seg: 12});                                 // pin 1 cm proud of the winch drum's ends
      r.node('boomBaseMesh', boom, [0, 0, 0], bm);
      let parent = boom;
      [[.8, .92], [.66, .76], [.52, .6]].forEach(([w, h], i) => {
        parent = r.node(`tele[${i}]`, parent, [0, 0, i === 0 ? -.3 : -.2], box(new Geo(), [0, 0, -4.9], [w, h, 10.0], Y, {bevel: .08}));
      });
      const head = new Geo();
      box(head, [0, -.1, -10.0], [.6, .7, .6], DK, {bevel: .05});
      cyl(head, [-.25, -.3, -10.2], [.25, -.3, -10.2], .3, DK, {seg: 14});
      r.node('head', parent, [0, 0, 0], head);
      r.node('tip', parent, [0, -.6, -10.2]);
      const hk = new Geo();
      box(hk, [0, .15, 0], [.4, .5, .25], Y, {bevel: .06});
      cyl(hk, [0, -.1, 0], [0, -.4, 0], .06, ST, {seg: 8});
      box(hk, [0, -.48, .06], [.06, .16, .16], ST);
      r.node('hook', root, [0, 0, 0], hk);
      r.node('wire', root, [0, 0, 0], cyl(new Geo(), [0, 0, 0], [0, 1, 0], .02, DK, {seg: 5, caps: false}));
      addRams(r, root, 1);
      return r;
    }

    function tower() {
      const r = new Rig('site_tower'), root = r.node('root'), m = new Geo();
      box(m, [0, .3, 0], [6.0, .6, 6.0], CONC);
      for (let k = 0; k < 10; k++) lattice(m, [0, .6 + k * 3.0, 0], [0, .6 + (k + 1) * 3.0, 0], 1.6, 1.6, Y, {panels: 2, chord: .07, brace: .035});
      for (let k = 0; k < 30; k++) box(m, [.7, .9 + k, .95], [.4, .03, .03], ST);              // ladder rungs
      r.node('base', root, [0, 0, 0], m);
      const sl = r.node('slew', root, [0, 30.6, 0]), s = new Geo();
      cyl(s, [0, 0, 0], [0, .5, 0], 1.0, DK, {seg: 20});
      lattice(s, [0, .5, 0], [0, 6.5, 0], 1.4, 1.4, Y, {panels: 3, chord: .07, brace: .035});   // tower head
      lattice(s, [0, .9, -.6], [0, .9, -40.0], 1.3, 1.45, Y, {panels: 26, chord: .06, brace: .03});   // jib
      lattice(s, [0, .9, .6], [0, .9, 12.0], 1.6, 1.0, Y, {panels: 8, chord: .07, brace: .035});     // counter-jib
      // Ballast blocks: 1 cm narrower per block (the Python's were all 2.2 m wide, so their sides were coplanar).
      for (let k = 0; k < 4; k++) box(s, [0, .1 + k * .02, 10.0 + k * .02], [2.2 - k * .02, 2.2 - k * .3, .4], CONC);
      box(s, [0, 1.0, 7.0], [1.6, .9, 1.4], DK);                                               // hoist winch
      for (const z of [-14.0, -30.0]) cyl(s, [0, 6.5, 0], [0, 1.6, z], .04, ST, {seg: z < -20 ? 6 : 5});   // tie bars (different facets, no shared plane)
      cyl(s, [0, 6.5, 0], [0, 1.4, 11.5], .05, ST, {seg: 5});
      cabBox(s, [-1.4, -.3, -1.3], 1.3, 2.0, 1.6);
      box(s, [0, 1.75, -40.2], [.8, .2, .4], RD);
      r.node('slewMesh', sl, [0, 0, 0], s);
      const tr = new Geo();
      box(tr, [0, -.05, 0], [1.38, .25, 1.2], DK, {bevel: .04});                              // 1 cm off the jib's bottom chords
      cyl(tr, [-.3, -.25, 0], [.3, -.25, 0], .18, ST, {seg: 12});
      r.node('trolley', sl, [0, .25, -10.0], tr);
      const hk = new Geo();
      box(hk, [0, .2, 0], [.5, .55, .3], Y, {bevel: .06});
      cyl(hk, [0, -.1, 0], [0, -.45, 0], .07, ST, {seg: 8});
      box(hk, [0, -.52, .07], [.07, .18, .18], ST);
      r.node('hook', root, [0, 0, 0], hk);
      r.node('wire', root, [0, 0, 0], cyl(new Geo(), [0, 0, 0], [0, 1, 0], .02, DK, {seg: 5, caps: false}));
      return r;
    }

    // ---------- site furniture and crane loads ----------
    // Heras fence panel (3.5 m by default) on two concrete feet, banner on the outside (+z).
    function fence(L = 3.5) {
      const f = new Geo(), h = L / 2;
      for (const sx of [-1, 1]) {
        cyl(f, [sx * (h - .01), .05, 0], [sx * (h - .01), 2.02, 0], .025, GALV, {seg: 6});   // post tops 2 cm above the top rail
        box(f, [sx * h, .07, 0], [.25, .14, .62], CONC, {bevel: .02});
      }
      for (const y of [.05, 1.98]) cyl(f, [-(h - .01), y, 0], [h - .01, y, 0], .022, GALV, {seg: 6});
      f.quad([-(h - .03), .06, 0], [h - .03, .06, 0], [h - .03, 1.97, 0], [-(h - .03), 1.97, 0], 'Mesh');
      f.quad([h - .03, .06, .001], [-(h - .03), .06, .001], [-(h - .03), 1.97, .001], [h - .03, 1.97, .001], 'Mesh');
      if (L >= 2) {
        // Banner image quad facing out and its plain back, kept >= 5 mm off the mesh and each other.
        const b = Math.min(1.65, h - .1);
        f.quad([-b, .75, .012], [b, .75, .012], [b, 1.65, .012], [-b, 1.65, .012], 'Banner');
        f.quad([b, .75, .006], [-b, .75, .006], [-b, 1.65, .006], [b, 1.65, .006], DK);
      }
      return f;
    }
    function container(color, wc) {
      const c = new Geo();
      box(c, [0, 1.35, 0], [6.06, 2.6, 2.44], color, {bevel: .04});
      for (let k = 0; k < 14; k++) box(c, [-2.6 + k * .4, 1.35, 1.225], [.12, 2.3, .03], color);
      for (const sx of [-1, 1]) box(c, [sx * 3.03, 1.35, 0], [.06, 2.7, 2.5], DK);
      box(c, [-1.2, 1.6, 1.24], [1.4, .9, .02], WIN);
      box(c, [1.6, 1.6, 1.24], [1.0, .9, .02], WIN);
      box(c, [.3, 1.05, 1.24], [.9, 2.0, .03], 'Door');
      box(c, [.3, .15, 1.7], [1.3, .3, .9], CONC);                                             // steps
      for (const sx of [-1, 1]) for (const sz of [-1, 1]) box(c, [sx * 2.7, .02, sz * 1.0], [.4, .04, .4], CONC);
      if (wc) box(c, [-2.0, 1.9, 1.246], [.5, .4, .02], WIN);                                  // 6 mm proud of the overlapping office window
      return c;
    }
    const PROPS = {
      fence: L => fence(L),
      office: () => container('Container', false),
      office_wc: () => container('Container2', true),
      toilet: () => {
        const t = new Geo();box(t, [0, 1.15, 0], [1.15, 2.3, 1.2], 'Toilet', {bevel: .06});
        box(t, [0, 1.05, .61], [.8, 1.9, .03], 'Toilet2');return t;
      },
      sign: () => {
        const s = new Geo();cyl(s, [0, .01, 0], [0, 2.3, 0], .04, GALV, {seg: 8});
        box(s, [0, 1.8, .05], [1.0, .7, .02], GALV);                                          // plate, the print faces out (+z)
        s.quad([-.5, 1.45, .066], [.5, 1.45, .066], [.5, 2.15, .066], [-.5, 2.15, .066], 'Sign');   // 6 mm off the plate
        box(s, [0, .1, 0], [.6, .2, .6], CONC);return s;
      },
      pallets: () => {
        const p = new Geo();
        for (let k = 0; k < 2; k++) {box(p, [0, .07 + k * 1.15, 0], [1.2, .14, 1.0], 'Wood');box(p, [0, .64 + k * 1.15, 0], [1.15, 1.0, .95], 'Brick');}
        return p;
      },
      pipes: () => {
        const pp = new Geo();
        for (const [x, y] of [[-.62, .6], [.62, .6], [0, 1.66]]) latheX(pp, [[-1.2, .6], [1.2, .6], [1.2, .48], [-1.2, .48]], CONC, 16, [0, y, x]);
        return pp;
      },
      skip: () => prism(new Geo(), [[-1.9, 0], [1.9, 0], [2.2, 1.5], [-2.2, 1.5]], -.9, .9, 'Skip'),
      light: () => {
        const lt = new Geo();box(lt, [0, .5, 0], [1.3, 1.0, 2.2], Y, {bevel: .06});
        cyl(lt, [0, 1.0, 0], [0, 7.5, 0], .07, GALV, {seg: 8});
        for (const sx of [-1, 1]) {box(lt, [sx * .35, 7.6, 0], [.5, .35, .2], DK);box(lt, [sx * .35, 7.6, -.11], [.42, .28, .02], LT);}
        return lt;
      }
    };
    const LOADS = {
      bricks: () => {const b = new Geo();box(b, [0, .07, 0], [1.2, .14, 1.0], 'Wood');box(b, [0, .62, 0], [1.15, .95, .95], 'Brick');return b;},
      beams: () => {
        // Beams rest on the two bearers (raised 4 cm from the Python, whose beam and bearer bottoms were coplanar).
        const b = new Geo();
        for (let k = 0; k < 6; k++) box(b, [-.5 + (k % 3) * .5, .19 + Math.floor(k / 3) * .32, 0], [.3, .3, 6.0], RD);
        box(b, [0, .02, -2.4], [1.6, .04, .1], 'Wood');box(b, [0, .02, 2.4], [1.6, .04, .1], 'Wood');
        return b;
      },
      skip: () => {
        const s = new Geo();cyl(s, [0, .2, 0], [0, 1.5, 0], .55, 'Skip', {seg: 16, r2: .85});
        cyl(s, [0, 0, 0], [0, .2, 0], .3, DK, {seg: 10});return s;
      }
    };

    // ---------- public API ----------
    const BUILDERS = {site_dozer: dozer, site_excavator: excavator, site_loader: loader, site_hauler: hauler, site_tipper: tipper, site_crane: crane, site_tower: tower};
    const specs = new Map(), geos = new Map();
    function rig(name) {
      if (!BUILDERS[name]) throw new Error('SiteModels: unknown rig ' + name);
      if (!specs.has(name)) specs.set(name, BUILDERS[name]());
      const nodes = {};
      for (const n of specs.get(name).nodes) {
        const g = new T.Group();g.name = n.name;g.position.set(n.pos[0], n.pos[1], n.pos[2]);g.visible = n.visible;
        if (n.mesh) addMeshes(g, n.mesh);
        nodes[n.name] = g;
        if (n.parent !== null) nodes[n.parent].add(g);
      }
      return {group: nodes.root, nodes};
    }
    const cached = (key, make) => {if (!geos.has(key)) geos.set(key, make());return geos.get(key);};
    function prop(kind, L = 3.5) {
      if (!PROPS[kind]) throw new Error('SiteModels: unknown prop ' + kind);
      const key = kind === 'fence' ? 'fence:' + Math.round((L || 3.5) * 100) : kind;
      const g = addMeshes(new T.Group(), cached('prop:' + key, () => PROPS[kind](L || 3.5)));g.name = 'site_' + kind;return g;
    }
    function load(kind) {
      if (!LOADS[kind]) throw new Error('SiteModels: unknown load ' + kind);
      const g = addMeshes(new T.Group(), cached('load:' + kind, LOADS[kind]));g.name = 'load_' + kind;return g;
    }
    // The gravel access track: a 4.5 m strip along the points, lifted 4 cm and drawn with a polygon offset.
    let gravel = null;
    function trackMesh(points) {
      const tr = new Geo(), lines = points && points.length && Array.isArray(points[0][0]) ? points : [points || []];
      for (const line of lines) {
        const P = line.map(p => [p[0], p[1] + .04, p[2]]);
        const side = i => {
          const a = P[Math.max(0, i - 1)], b = P[Math.min(P.length - 1, i + 1)];
          let dx = b[0] - a[0], dz = b[2] - a[2];const l = Math.max(1e-6, Math.hypot(dx, dz));dx /= l;dz /= l;
          return [-dz * 2.25, 0, dx * 2.25];
        };
        for (let i = 0; i < P.length - 1; i++) {
          const a = P[i], b = P[i + 1], na = side(i), nb = side(i + 1), la = add(a, na), ra = sub(a, na), lb = add(b, nb), rb = sub(b, nb);
          if (cross(sub(ra, la), sub(lb, la))[1] > 0) tr.quad(la, ra, rb, lb, 'Gravel');else tr.quad(la, lb, rb, ra, 'Gravel');
        }
      }
      if (!gravel) {gravel = mat('Gravel').clone();gravel.polygonOffset = true;gravel.polygonOffsetFactor = -2;gravel.polygonOffsetUnits = -2;gravel.name = 'Site_Track';}
      const arr = tr.S.get('Gravel') || [];
      const m = new T.Mesh(arr.length ? bufferGeometry(arr) : new T.BufferGeometry(), gravel);
      m.name = 'site_track';m.receiveShadow = true;m.castShadow = false;
      return m;
    }
    return {rig, prop, load, track: trackMesh, rigs: Object.keys(BUILDERS), props: Object.keys(PROPS), loads: Object.keys(LOADS)};
  }
  return {create};
});
