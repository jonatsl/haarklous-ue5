/* Furnishing for the realistic view: a generic living room and the bathroom as shown in the FINN listing
 * (white 15 cm wall tiles, wall-hung toilet in front of a tiled cistern box, white two-drawer vanity,
 * mirror cabinet with a light bar, chrome thermostatic shower riser, ceiling curtain rail, washing machine).
 * Positions use the model's interior faces (metres): living x 0-2.703, z 0.16-4.719;
 * bathroom x 3.462-5.653, z 4.899-6.769, recess x 3.462-4.223 to z 7.144. Furniture sizes are typical, not measured.
 * Furnishings.create({T, lib}) -> {living(ctx), bathroom(ctx)}; ctx = {parent, collider(w,h,d,x,y,z,rotY,name), part(mesh,name)}.
 */
(function (root, factory) {
  root.Furnishings = factory();
})(typeof window === 'undefined' ? globalThis : window, function () {
  'use strict';
  function create({T, lib}) {
    function roundedBox(w, h, d, r) {
      r = Math.min(r, w / 2 - .001, h / 2 - .001, d / 2 - .001);
      const shape = new T.Shape();shape.moveTo(-w / 2 + r, -h / 2);shape.lineTo(w / 2 - r, -h / 2);shape.quadraticCurveTo(w / 2, -h / 2, w / 2, -h / 2 + r);shape.lineTo(w / 2, h / 2 - r);
      shape.quadraticCurveTo(w / 2, h / 2, w / 2 - r, h / 2);shape.lineTo(-w / 2 + r, h / 2);shape.quadraticCurveTo(-w / 2, h / 2, -w / 2, h / 2 - r);shape.lineTo(-w / 2, -h / 2 + r);shape.quadraticCurveTo(-w / 2, -h / 2, -w / 2 + r, -h / 2);
      const g = new T.ExtrudeGeometry(shape, {depth: Math.max(.001, d - 2 * r), bevelEnabled: true, bevelThickness: r, bevelSize: r * .98, bevelSegments: 4, curveSegments: 6});
      g.translate(0, 0, -(d - 2 * r) / 2);g.computeVertexNormals();return g;
    }
    function maker(ctx) {
      const mesh = (geometry, material, x, y, z, name, parent = ctx.parent) => {const m = new T.Mesh(geometry, material);m.position.set(x, y, z);m.castShadow = m.receiveShadow = true;parent.add(m);ctx.part(m, name);return m;};
      const box = (w, h, d, x, y, z, material, name, parent) => {const m = mesh(new T.BoxGeometry(w, h, d), material, x, y, z, name, parent);lib.boxUV(m);return m;};
      const soft = (w, h, d, r, x, y, z, material, name, parent) => mesh(roundedBox(w, h, d, r), material, x, y, z, name, parent);
      const cyl = (rt, rb, h, x, y, z, material, name, parent, seg = 24) => mesh(new T.CylinderGeometry(rt, rb, h, seg), material, x, y, z, name, parent);
      const group = (x, z, rotY = 0) => {const g = new T.Group();g.position.set(x, 0, z);g.rotation.y = rotY;ctx.parent.add(g);return g;};
      return {mesh, box, soft, cyl, group};
    }
    const artTexture = (() => {
      const c = document.createElement('canvas');c.width = 512;c.height = 340;const x = c.getContext('2d');
      const g = x.createLinearGradient(0, 0, 0, 340);g.addColorStop(0, '#d9cbb0');g.addColorStop(1, '#b9a587');x.fillStyle = g;x.fillRect(0, 0, 512, 340);
      x.fillStyle = '#2f4a5e';x.beginPath();x.moveTo(0, 260);for (let i = 0; i <= 512; i += 16) x.lineTo(i, 200 + Math.sin(i * .02) * 30 + Math.sin(i * .07) * 10);x.lineTo(512, 340);x.lineTo(0, 340);x.fill();
      x.fillStyle = '#c4573a';x.beginPath();x.arc(360, 110, 46, 0, 7);x.fill();x.fillStyle = '#56718a';x.globalAlpha = .7;x.beginPath();x.moveTo(0, 290);for (let i = 0; i <= 512; i += 16) x.lineTo(i, 250 + Math.sin(i * .015 + 2) * 25);x.lineTo(512, 340);x.lineTo(0, 340);x.fill();
      const t = new T.CanvasTexture(c);t.colorSpace = T.SRGBColorSpace;return t;
    })();
    // A row of books from a0 to a1 along x (or along z when alongZ), standing at height y.
    function books(m, parent, a0, a1, y, depth, fixed, alongZ = false) {
      const colors = [0x8c2f28, 0x2f4d6b, 0xd8cdb2, 0x3d5a3a, 0x1f1f22, 0xc9a24a, 0x6b4a7a, 0xe6e1d6];let a = a0;
      while (a < a1 - .03) {
        const w = .02 + lib.rand() * .03, h = .17 + lib.rand() * .09, d = depth * (.75 + lib.rand() * .2), mat = lib.paint(colors[Math.floor(lib.rand() * colors.length)], .7);
        if (a + w > a1) break;
        if (alongZ) m.box(d, h, w, fixed, y + h / 2, a + w / 2, mat, 'Book', parent);else m.box(w, h, d, a + w / 2, y + h / 2, fixed, mat, 'Book', parent);
        a += w + .002;
      }
    }

    function living(ctx) {
      const m = maker(ctx), C = ctx.collider;
      const fabric = lib.fabric(0x7a7f84), cushion = lib.fabric(0x9aa0a4), oak = lib.wood(0xb88a5c, .5), black = lib.metal(0x1d1e20, .6), white = lib.paint(0xf1efe9, .4);
      // Three-seat sofa against the long left wall, facing the TV.
      const sofa = m.group(.47, 2.6, -Math.PI / 2);
      m.soft(2.1, .42, .9, .05, 0, .21, 0, fabric, 'Sofa base', sofa);
      m.soft(2.1, .48, .22, .08, 0, .62, .34, fabric, 'Sofa back', sofa);
      for (const sx of [-1.0, 1.0]) m.soft(.18, .26, .88, .07, sx, .55, 0, fabric, 'Sofa arm', sofa);
      for (const sx of [-.6, 0, .6]) {m.soft(.62, .14, .66, .06, sx, .49, -.08, cushion, 'Seat cushion', sofa);m.soft(.58, .4, .16, .07, sx, .72, .2, cushion, 'Back cushion', sofa);}
      m.soft(.42, .4, .14, .06, -.72, .7, .1, lib.fabric(0xb8823f), 'Throw pillow', sofa).rotation.z = .25;
      for (const sx of [-.95, .95]) for (const sz of [-.33, .33]) m.cyl(.02, .015, .06, sx, .03, sz, black, 'Sofa leg', sofa, 10);
      m.soft(.9, .03, .6, .012, .55, .57, -.05, lib.fabric(0xc9bfae), 'Throw blanket', sofa).rotation.z = -.05;
      C(.9, .82, 2.1, .45, .41, 2.6, 0, 'Sofa');
      // Rug and coffee table.
      m.box(1.4, .012, 1.9, 1.45, .006, 2.6, lib.rug([0xb9ae99, 0x5c5f63, 0x8a4b3a]), 'Rug').castShadow = false;
      m.box(.55, .03, 1.0, 1.45, .41, 2.6, oak, 'Coffee table top');
      for (const dx of [-.23, .23]) for (const dz of [-.45, .45]) m.box(.035, .395, .035, 1.45 + dx, .2, 2.6 + dz, black, 'Coffee table leg');
      m.box(.5, .02, .95, 1.45, .14, 2.6, oak, 'Coffee table shelf');
      m.cyl(.06, .05, .1, 1.45, .48, 2.35, lib.ceramic(), 'Mug');m.box(.24, .03, .32, 1.45, .44, 2.85, lib.paint(0x9b3d2e, .6), 'Magazine');
      C(.55, .43, 1.0, 1.45, .215, 2.6, 0, 'Coffee table');
      // TV bench with television on the opposite wall.
      m.box(.42, .46, 1.6, 2.49, .25, 2.6, white, 'TV bench');
      for (const dz of [-.4, .4]) m.box(.004, .38, .76, 2.278, .25, 2.6 + dz, lib.paint(0xe4e1da, .3), 'TV bench door');
      m.box(.42, .02, 1.6, 2.49, .02, 2.6, black, 'TV bench plinth');
      m.box(.06, .72, 1.24, 2.62, 1.12, 2.6, black, 'Television');
      m.box(.008, .68, 1.2, 2.588, 1.12, 2.6, new T.MeshStandardMaterial({color: 0x050608, roughness: .08, metalness: .4}), 'Television screen');
      m.box(.08, .1, .5, 2.42, .53, 2.6, lib.paint(0x2a2b2e, .5), 'Soundbar');
      books(m, ctx.parent, 2.35, 2.62, .48, .3, 1.95);
      C(.42, .46, 1.6, 2.49, .23, 2.6, 0, 'TV bench');
      // Armchair angled in the window corner.
      const chair = m.group(.55, .75, -Math.PI / 2 - .55), leather = lib.leather(0x6a442c);
      m.soft(.78, .22, .78, .05, 0, .36, 0, leather, 'Armchair seat', chair);m.soft(.78, .56, .16, .06, 0, .66, .32, leather, 'Armchair back', chair);
      for (const sx of [-.36, .36]) m.soft(.1, .22, .7, .04, sx, .55, 0, leather, 'Armchair arm', chair);
      for (const sx of [-.32, .32]) for (const sz of [-.3, .3]) m.cyl(.018, .014, .25, sx, .125, sz, oak, 'Armchair leg', chair, 10);
      C(.8, .8, .8, .55, .4, .75, -.55, 'Armchair');
      // Floor lamp, side table, bookshelf, plant, wall art.
      m.cyl(.15, .15, .02, .25, .01, 1.42, black, 'Floor lamp base');m.cyl(.012, .012, 1.5, .25, .76, 1.42, black, 'Floor lamp pole');
      const shade = m.cyl(.19, .23, .26, .25, 1.55, 1.42, new T.MeshStandardMaterial({color: 0xf3ead8, emissive: 0xffe2b0, emissiveIntensity: .35, roughness: .9, side: T.DoubleSide}), 'Floor lamp shade', undefined, 28);shade.castShadow = false;
      C(.3, 1.7, .3, .25, .85, 1.42, 0, 'Floor lamp');
      m.cyl(.2, .2, .025, .25, .55, 3.9, oak, 'Side table top');m.cyl(.02, .02, .54, .25, .27, 3.9, black, 'Side table stem');m.cyl(.14, .14, .015, .25, .008, 3.9, black, 'Side table foot');
      C(.4, .56, .4, .25, .28, 3.9, 0, 'Side table');
      const shelfX = .17;
      for (const z of [4.2, 4.68]) m.box(.32, 1.9, .02, shelfX, .95, z, oak, 'Bookshelf side');
      for (let i = 0; i < 5; i++) {m.box(.32, .02, .46, shelfX, .02 + i * .46, 4.44, oak, 'Bookshelf shelf');if (i < 4) books(m, ctx.parent, 4.22, 4.66, .03 + i * .46, .26, shelfX, true);}
      C(.33, 1.9, .5, shelfX, .95, 4.44, 0, 'Bookshelf');
      const pot = lib.paint(0xd8d2c4, .6);m.cyl(.17, .13, .34, 2.45, .17, 1.3, pot, 'Plant pot');
      // Fiddle-leaf fig: broad leaves on short stalks spiralling up a single stem.
      const leaf = new T.MeshStandardMaterial({color: 0x2f5a24, roughness: .5, side: T.DoubleSide}), leafShape = new T.Shape();
      leafShape.moveTo(0, 0);leafShape.quadraticCurveTo(.07, .06, .06, .16);leafShape.quadraticCurveTo(.03, .22, 0, .23);leafShape.quadraticCurveTo(-.03, .22, -.06, .16);leafShape.quadraticCurveTo(-.07, .06, 0, 0);
      const leafGeo = new T.ShapeGeometry(leafShape, 8);
      for (let i = 0; i < 26; i++) {
        const a = i * 2.39, h = .5 + i * .033, l = m.mesh(leafGeo, leaf, 2.45 + Math.cos(a) * .05, h, 1.3 + Math.sin(a) * .05, 'Plant leaf');
        l.rotation.set(-.9 + (i % 3) * .25, -a + Math.PI / 2, .3 * Math.sin(i));l.scale.setScalar(.85 + (i % 4) * .12);
      }
      m.cyl(.012, .016, 1.0, 2.45, .82, 1.3, lib.wood(0x5a4630), 'Plant stem');
      C(.36, 1.4, .36, 2.45, .7, 1.3, 0, 'Plant');
      m.box(.03, .62, .92, .016, 1.55, 2.6, black, 'Picture frame');
      // The print keeps its own 0-1 UVs (no metre mapping) so the whole artwork shows.
      m.mesh(new T.BoxGeometry(.004, .56, .86), new T.MeshStandardMaterial({map: artTexture, roughness: .6}), .034, 1.55, 2.6, 'Picture print').rotation.y = 0;
      // Curtains and rod over the window and balcony door.
      const curtain = lib.fabric(0x2c3e5c);curtain.side = T.DoubleSide;
      m.cyl(.012, .012, 1.9, 1.78, 2.2, .2, black, 'Curtain rod').rotation.z = Math.PI / 2;
      for (const [x0, x1] of [[.92, 1.16], [2.63, 2.7]]) for (let k = 0; k < (x1 - x0) / .04; k++) {const fold = m.box(.05, 2.05, .02, x0 + .02 + k * .04, 1.18, .2 + (k % 2) * .03, curtain, 'Curtain');fold.rotation.y = (k % 2 ? .6 : -.6);}
      // Panel heater under the window (typical Norwegian electric oven), wall sockets and a light switch.
      const heaterMat = lib.paint(0xf2f1ec, .35);
      m.box(.7, .4, .06, 1.525, .38, .2, heaterMat, 'Panel heater');
      for (let i = 0; i < 9; i++) m.box(.6, .006, .02, 1.525, .22 + i * .036, .235, lib.paint(0xdedcd5, .5), 'Heater grille');
      for (const x of [1.25, 1.8]) m.box(.03, .14, .03, x, .1, .19, heaterMat, 'Heater leg');
      C(.7, .5, .1, 1.525, .3, .2, 0, 'Panel heater');
      const socketMat = lib.paint(0xf6f6f2, .3), dark = lib.paint(0x2a2a2a, .5);
      const socket = (x, y, z, ry) => {const g = m.group(x, z, ry);m.box(.08, .08, .012, 0, y, .006, socketMat, 'Wall socket', g);for (const dx of [-.012, .012]) m.cyl(.004, .004, .004, dx, y, .013, dark, 'Socket pin hole', g, 8).rotation.x = Math.PI / 2;};
      socket(.0, .3, 3.85, Math.PI / 2);socket(2.703, .3, 1.75, -Math.PI / 2);socket(.0, .3, 1.15, Math.PI / 2);
      const sw = m.group(2.703, 4.45, -Math.PI / 2);m.box(.08, .08, .012, 0, 1.05, .006, socketMat, 'Light switch', sw);m.box(.03, .045, .008, 0, 1.05, .015, socketMat, 'Switch rocker', sw);
      // Pendant lamp.
      m.cyl(.004, .004, .55, 1.35, 2.2, 2.6, black, 'Pendant cord');
      m.mesh(new T.SphereGeometry(.17, 24, 12, 0, Math.PI * 2, 0, Math.PI / 2), new T.MeshStandardMaterial({color: 0x2b2d2f, roughness: .4, metalness: .5, side: T.DoubleSide}), 1.35, 1.78, 2.6, 'Pendant shade');
    }

    function bathroom(ctx) {
      const m = maker(ctx), C = ctx.collider, white = lib.ceramic(), chrome = lib.chrome(), gloss = lib.paint(0xf4f4f1, .18), tiles = lib.wallTiles();
      // Wall-hung toilet in front of a tiled cistern box (photos 17 and 19), next to the door.
      const tx = 3.86, wallZ = 4.899;
      m.box(.56, 1.05, .2, tx, .525, wallZ + .1, tiles, 'Tiled cistern box');
      m.box(.24, .16, .01, tx, .92, wallZ + .205, lib.paint(0xfafafa, .25), 'Flush plate');
      const bowl = m.mesh(new T.SphereGeometry(.2, 32, 18, 0, Math.PI * 2, Math.PI / 2, Math.PI / 2), white, tx, .42, wallZ + .47, 'Toilet bowl');bowl.scale.set(.92, .9, 1.35);bowl.rotation.x = Math.PI;
      m.soft(.36, .32, .3, .08, tx, .28, wallZ + .36, white, 'Toilet body');
      m.soft(.37, .03, .5, .015, tx, .44, wallZ + .47, white, 'Toilet seat');
      m.soft(.36, .025, .46, .012, tx, .47, wallZ + .45, white, 'Toilet lid');
      m.cyl(.06, .06, .11, tx - .36, .78, wallZ + .3, chrome, 'Toilet roll').rotation.z = Math.PI / 2;
      C(.42, .5, .55, tx, .25, wallZ + .3, 0, 'Toilet');C(.56, 1.05, .2, tx, .525, wallZ + .1, 0, 'Cistern box');
      // Vanity with basin, mirror cabinet and light bar.
      const vx = 4.45, vw = .6;
      m.box(vw, .5, .44, vx, .55, wallZ + .22, gloss, 'Vanity cabinet');
      for (const y of [.43, .67]) {m.box(vw - .01, .22, .01, vx, y, wallZ + .445, gloss, 'Vanity drawer');m.box(.2, .012, .015, vx, y + .07, wallZ + .455, lib.paint(0xdedede, .3), 'Drawer grip');}
      m.soft(vw + .02, .06, .46, .02, vx, .83, wallZ + .23, white, 'Basin top');
      const basin = m.mesh(new T.CylinderGeometry(.2, .14, .05, 32), new T.MeshStandardMaterial({color: 0xe9eaea, roughness: .1}), vx, .845, wallZ + .25, 'Basin bowl');basin.scale.z = .75;
      m.cyl(.018, .022, .2, vx, .96, wallZ + .07, chrome, 'Basin mixer');m.box(.025, .02, .12, vx, 1.05, wallZ + .12, chrome, 'Basin spout');m.box(.08, .012, .02, vx + .05, 1.04, wallZ + .07, chrome, 'Mixer lever');
      C(vw, .86, .46, vx, .43, wallZ + .23, 0, 'Vanity');
      m.box(.72, .7, .14, vx, 1.62, wallZ + .07, gloss, 'Mirror cabinet');
      m.box(.7, .68, .006, vx, 1.62, wallZ + .143, new T.MeshStandardMaterial({color: 0xd9e2e6, roughness: .02, metalness: 1}), 'Mirror doors');
      const bar = m.box(.6, .035, .06, vx, 2.03, wallZ + .1, new T.MeshStandardMaterial({color: 0xffffff, emissive: 0xfff4e2, emissiveIntensity: 2.2}), 'Mirror light bar');bar.castShadow = false;
      // Round shaving mirror on an arm (left of the cabinet).
      m.cyl(.008, .008, .25, vx - .48, 1.45, wallZ + .12, chrome, 'Mirror arm').rotation.x = Math.PI / 2;
      const round = m.cyl(.11, .11, .015, vx - .48, 1.45, wallZ + .25, new T.MeshStandardMaterial({color: 0xd9e2e6, roughness: .02, metalness: 1}), 'Shaving mirror', undefined, 32);round.rotation.x = Math.PI / 2;
      // Shower zone at the far (east) end: thermostatic riser on the end wall, rail and curtain.
      const ex = 5.653, sz = 5.62;
      m.box(.3, .06, .06, ex - .05, 1.05, sz, chrome, 'Thermostatic mixer');for (const dz of [-.13, .13]) m.cyl(.03, .03, .05, ex - .06, 1.05, sz + dz, chrome, 'Mixer knob').rotation.z = Math.PI / 2;
      m.cyl(.012, .012, 1.0, ex - .05, 1.6, sz, chrome, 'Riser rail');m.box(.03, .05, .05, ex - .05, 1.5, sz, chrome, 'Slide holder');
      m.cyl(.008, .008, .32, ex - .2, 2.1, sz, chrome, 'Overhead arm').rotation.z = Math.PI / 2;
      m.cyl(.11, .11, .015, ex - .34, 2.08, sz, chrome, 'Rain shower head', undefined, 32);
      m.cyl(.03, .02, .14, ex - .09, 1.5, sz + .02, chrome, 'Hand shower');
      const hose = new T.TubeGeometry(new T.CatmullRomCurve3([new T.Vector3(ex - .06, 1.03, sz + .02), new T.Vector3(ex - .1, .6, sz + .08), new T.Vector3(ex - .12, .45, sz + .02), new T.Vector3(ex - .09, .75, sz - .03), new T.Vector3(ex - .09, 1.44, sz + .02)]), 40, .007, 8);
      m.mesh(hose, chrome, 0, 0, 0, 'Shower hose');
      const basket = m.box(.22, .1, .1, 5.3, 1.3, 6.769 - .05, lib.metal(0xcfd3d6, .25), 'Shower basket');basket.material.wireframe = false;
      m.cyl(.06, .06, .006, 5.32, .004, 5.8, lib.metal(0x9aa0a4, .3), 'Floor drain grate', undefined, 4).rotation.y = Math.PI / 4;
      // Ceiling curtain rail across the room, with the curtain gathered at the wall.
      const railY = 2.28, rx = 4.88;
      m.cyl(.01, .01, 6.769 - 4.95, rx, railY, (4.95 + 6.769) / 2, chrome, 'Curtain rail').rotation.x = Math.PI / 2;
      for (const z of [5.0, 6.72]) m.cyl(.006, .006, 2.48 - railY, rx, (railY + 2.48) / 2, z, chrome, 'Rail ceiling bracket');
      const curtainMat = new T.MeshStandardMaterial({color: 0xf2f2ee, roughness: .85, transparent: true, opacity: .85, side: T.DoubleSide});
      for (let k = 0; k < 7; k++) {const f = m.box(.004, 1.85, .09, rx + (k % 2 ? .03 : -.03), railY - .95, 6.42 + k * .045, curtainMat, 'Shower curtain');f.rotation.y = (k % 2 ? .5 : -.5);f.castShadow = false;}
      // Washing machine in the recess by the door (plan and photo 17).
      m.box(.6, .85, .6, 3.85, .425, 6.84, lib.paint(0xf7f7f5, .3), 'Washing machine');
      m.box(.56, .012, .5, 3.85, .856, 6.84, lib.paint(0xeeeeec, .2), 'Washing machine lid');
      m.box(.5, .06, .012, 3.85, .78, 6.535, lib.paint(0x3c78c2, .3), 'Control strip');
      C(.6, .85, .6, 3.85, .425, 6.84, 0, 'Washing machine');
      // Towel hooks and towel by the door, ceiling downlights.
      for (const z of [6.1, 6.35]) m.cyl(.008, .008, .05, 3.47, 1.6, z, chrome, 'Towel hook').rotation.z = Math.PI / 2;
      m.soft(.04, .7, .4, .015, 3.49, 1.25, 6.22, lib.fabric(0xd9d4c8), 'Towel');
      const spot = new T.MeshStandardMaterial({color: 0xffffff, emissive: 0xfff5e6, emissiveIntensity: 3});
      for (const [x, z] of [[4.0, 5.6], [5.1, 5.9]]) {const s = m.cyl(.04, .04, .01, x, 2.475, z, spot, 'Downlight');s.castShadow = false;}
    }
    return {living, bathroom};
  }
  return {create};
});
