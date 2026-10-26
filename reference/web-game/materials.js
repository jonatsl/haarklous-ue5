/* Procedural physically based materials for the realistic first-person view.
 * Every texture is painted on a canvas at start-up, so the viewer stays offline and self-contained.
 * RealMaterials.create(THREE, renderer) -> library:
 *   lib.plaster(color), lib.woodFloor(), lib.wallTiles(), lib.floorTiles(), lib.brick(), lib.facade(), lib.plinth(),
 *   lib.roof(), lib.terrazzo(), lib.concrete(color), lib.fabric(color), lib.leather(color), lib.wood(color),
 *   lib.paint(color, roughness), lib.metal(color, roughness), lib.chrome(), lib.ceramic(), lib.glass(), lib.steelPlate(),
 *   lib.polymer(color), lib.glove(), lib.sleeve(), lib.leaves(color), lib.bark(), lib.rug(colors)
 *   lib.boxUV(mesh) writes UVs in metres from each face's local position, offset by the mesh position in its parent,
 *   so neighbouring boxes of one wall continue the same pattern. Materials set texture.repeat = 1 / tile size.
 *   lib.environment(scene) builds a soft daylight environment map (PMREM) for reflections.
 */
(function (root, factory) {
  root.RealMaterials = factory();
})(typeof window === 'undefined' ? globalThis : window, function () {
  'use strict';
  function create(T, renderer) {
    const cache = new Map(), anisotropy = Math.min(8, renderer.capabilities.getMaxAnisotropy ? renderer.capabilities.getMaxAnisotropy() : 4);
    // Deterministic noise so textures look the same on every load.
    let seed = 1234567;
    const rand = () => {seed = (seed * 16807) % 2147483647;return seed / 2147483647;};
    const canvas = (w, h = w) => {const c = document.createElement('canvas');c.width = w;c.height = h;return c;};
    const memo = (key, make) => {if (!cache.has(key)) cache.set(key, make());return cache.get(key);};
    function texture(c, tile, color = true) {
      const t = new T.CanvasTexture(c);t.wrapS = t.wrapT = T.RepeatWrapping;t.anisotropy = anisotropy;
      if (color) t.colorSpace = T.SRGBColorSpace;
      t.repeat.set(1 / tile[0], 1 / tile[1]);t.needsUpdate = true;return t;
    }
    // Normal map from a greyscale height canvas (Sobel), strength in pixels of relief.
    function normalFrom(height, strength = 2) {
      const w = height.width, h = height.height, src = height.getContext('2d').getImageData(0, 0, w, h).data, out = canvas(w, h), ctx = out.getContext('2d'), img = ctx.createImageData(w, h);
      const at = (x, y) => src[(((y + h) % h) * w + ((x + w) % w)) * 4] / 255;
      for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
        const dx = (at(x + 1, y) - at(x - 1, y)) * strength, dy = (at(x, y + 1) - at(x, y - 1)) * strength, len = Math.hypot(dx, dy, 1), i = (y * w + x) * 4;
        img.data[i] = (-dx / len * .5 + .5) * 255;img.data[i + 1] = (dy / len * .5 + .5) * 255;img.data[i + 2] = (1 / len * .5 + .5) * 255;img.data[i + 3] = 255;
      }
      ctx.putImageData(img, 0, 0);return out;
    }
    function speckle(ctx, w, h, count, size, colors, alpha) {
      for (let i = 0; i < count; i++) {ctx.globalAlpha = alpha * rand();ctx.fillStyle = colors[Math.floor(rand() * colors.length)];const s = size * (.4 + rand());ctx.fillRect(rand() * w, rand() * h, s, s);}
      ctx.globalAlpha = 1;
    }
    function blurInto(c, radius) {const out = canvas(c.width, c.height), ctx = out.getContext('2d');ctx.filter = `blur(${radius}px)`;for (const dx of [-c.width, 0, c.width]) for (const dy of [-c.height, 0, c.height]) ctx.drawImage(c, dx, dy);ctx.filter = 'none';return out;}
    // Low-frequency mottling (tileable) in [0,1] greys.
    function cloud(size, cells, radius) {
      const c = canvas(size), ctx = c.getContext('2d');ctx.fillStyle = '#808080';ctx.fillRect(0, 0, size, size);
      for (let i = 0; i < cells; i++) {const v = Math.floor(100 + rand() * 56);ctx.fillStyle = `rgb(${v},${v},${v})`;ctx.globalAlpha = .5;ctx.beginPath();ctx.arc(rand() * size, rand() * size, radius * (.5 + rand()), 0, 7);ctx.fill();}
      ctx.globalAlpha = 1;return blurInto(c, radius * .8);
    }
    const hex = n => '#' + new T.Color(n).getHexString();
    function shade(color, f) {const c = new T.Color(color);c.multiplyScalar(f);return '#' + c.getHexString();}
    function standard(opts) {return new T.MeshStandardMaterial(opts);}

    // ---------- Surfaces ----------
    const plasterSet = () => memo('plaster', () => {
      // Near-white with faint roller mottling, so the paint colour multiplies almost unchanged.
      const size = 512, c = canvas(size), ctx = c.getContext('2d');ctx.fillStyle = '#f4f4f4';ctx.fillRect(0, 0, size, size);
      for (let i = 0; i < 220; i++) {const v = 232 + Math.floor(rand() * 23);ctx.fillStyle = `rgb(${v},${v},${v})`;ctx.globalAlpha = .35;ctx.beginPath();ctx.arc(rand() * size, rand() * size, 14 + rand() * 26, 0, 7);ctx.fill();}
      ctx.globalAlpha = 1;const soft = blurInto(c, 14), out = canvas(size), o = out.getContext('2d');o.drawImage(soft, 0, 0);
      speckle(o, size, size, 7000, 1.4, ['#dcdcdc', '#ffffff'], .3);
      const h = cloud(size, 400, 6);return {map: texture(out, [1.6, 1.6]), normal: texture(normalFrom(h, 1.1), [1.6, 1.6], false)};
    });
    function plaster(color = 0xf1eee8) {
      const s = plasterSet();
      return standard({color: new T.Color(color), map: s.map, normalMap: s.normal, normalScale: new T.Vector2(.25, .25), roughness: .93, metalness: 0});
    }
    const woodSet = () => memo('woodFloor', () => {
      const w = 1024, h = 1024, c = canvas(w, h), ctx = c.getContext('2d'), height = canvas(w, h), hx = height.getContext('2d'), rough = canvas(w, h), rx = rough.getContext('2d');
      const rows = 8, rowH = h / rows; hx.fillStyle = '#c0c0c0';hx.fillRect(0, 0, w, h);rx.fillStyle = '#909090';rx.fillRect(0, 0, w, h);
      for (let r = 0; r < rows; r++) {
        let x = -rand() * w * .6;
        while (x < w) {
          const len = w * (.45 + rand() * .35), tone = .82 + rand() * .3, base = new T.Color(0xb08457).multiplyScalar(tone);
          for (const shift of [0, w, -w]) {
            const x0 = x + shift;
            if (x0 > w || x0 + len < 0) continue;
            const grad = ctx.createLinearGradient(0, r * rowH, 0, (r + 1) * rowH);grad.addColorStop(0, '#' + base.clone().multiplyScalar(1.04).getHexString());grad.addColorStop(1, '#' + base.clone().multiplyScalar(.94).getHexString());
            ctx.fillStyle = grad;ctx.fillRect(x0, r * rowH, len, rowH);
            // Grain: long wavy streaks and a few knots.
            ctx.save();ctx.beginPath();ctx.rect(x0, r * rowH, len, rowH);ctx.clip();
            for (let g = 0; g < 26; g++) {
              const y = r * rowH + rand() * rowH, amp = 1 + rand() * 3, freq = .004 + rand() * .01, dark = rand() < .6;
              ctx.strokeStyle = dark ? 'rgba(70,40,15,.18)' : 'rgba(255,230,190,.12)';ctx.lineWidth = .6 + rand() * 1.6;ctx.beginPath();
              for (let xx = x0; xx <= x0 + len; xx += 8) ctx.lineTo(xx, y + Math.sin(xx * freq + g) * amp);ctx.stroke();
            }
            if (rand() < .35) {const kx = x0 + rand() * len, ky = r * rowH + rand() * rowH;ctx.fillStyle = 'rgba(60,32,12,.35)';ctx.beginPath();ctx.ellipse(kx, ky, 6 + rand() * 6, 3 + rand() * 2, 0, 0, 7);ctx.fill();}
            ctx.restore();
            hx.fillStyle = '#404040';hx.fillRect(x0 - 1, r * rowH, 2.5, rowH);
            rx.fillStyle = `rgb(${120 + rand() * 30 | 0},0,0)`;
          }
          x += len;
        }
        hx.fillStyle = '#404040';hx.fillRect(0, r * rowH - 1, w, 2.5);ctx.fillStyle = 'rgba(40,22,8,.55)';ctx.fillRect(0, r * rowH - 1, w, 2);
      }
      speckle(rx, w, h, 4000, 3, ['#707070', '#a8a8a8'], .5);
      const tile = [1.6, 1.52];return {map: texture(c, tile), normal: texture(normalFrom(blurInto(height, .8), 3), tile, false), rough: texture(rough, tile, false)};
    });
    function woodFloor() {const s = woodSet();return standard({map: s.map, normalMap: s.normal, normalScale: new T.Vector2(.6, .6), roughnessMap: s.rough, roughness: .62, metalness: 0});}
    function tileSet(key, {count, color, grout, size, diagonal = false, variance = .05, groutPx = 3}) {
      return memo(key, () => {
        const px = 512, c = canvas(px), ctx = c.getContext('2d'), height = canvas(px), hx = height.getContext('2d'), rough = canvas(px), rx = rough.getContext('2d');
        ctx.fillStyle = hex(grout);ctx.fillRect(0, 0, px, px);hx.fillStyle = '#303030';hx.fillRect(0, 0, px, px);rx.fillStyle = '#e8e8e8';rx.fillRect(0, 0, px, px);
        const cell = px / count;
        const draw = (cx, cy) => {
          const tone = 1 - variance / 2 + rand() * variance;
          for (const [ox, oy] of [[0, 0], [px, 0], [-px, 0], [0, px], [0, -px]]) {
            ctx.fillStyle = shade(color, tone);ctx.fillRect(cx + ox + groutPx / 2, cy + oy + groutPx / 2, cell - groutPx, cell - groutPx);
            hx.fillStyle = '#d0d0d0';hx.fillRect(cx + ox + groutPx / 2 + 1, cy + oy + groutPx / 2 + 1, cell - groutPx - 2, cell - groutPx - 2);
            rx.fillStyle = '#262626';rx.fillRect(cx + ox + groutPx / 2, cy + oy + groutPx / 2, cell - groutPx, cell - groutPx);
          }
        };
        if (diagonal) {
          // Diamonds on a lattice with one diagonal per cell (plus the centred row) tile seamlessly.
          const diamond = (k, cx, cy, r, fill) => {k.fillStyle = fill;k.beginPath();k.moveTo(cx, cy - r);k.lineTo(cx + r, cy);k.lineTo(cx, cy + r);k.lineTo(cx - r, cy);k.closePath();k.fill();};
          for (let i = -1; i <= count; i++) for (let j = -1; j <= count; j++) for (const [ox, oy] of [[0, 0], [cell / 2, cell / 2]]) {
            const cx = i * cell + ox, cy = j * cell + oy, r = cell / 2 - groutPx * .75, tone = 1 - variance / 2 + rand() * variance;
            diamond(ctx, cx, cy, r, shade(color, tone));diamond(hx, cx, cy, r - 1, '#d0d0d0');diamond(rx, cx, cy, r, '#262626');
          }
        } else for (let i = 0; i < count; i++) for (let j = 0; j < count; j++) draw(i * cell, j * cell);
        speckle(ctx, px, px, 1500, 1.2, ['#ffffff', '#909090'], .12);
        const tile = diagonal ? [size * Math.SQRT2 * count, size * Math.SQRT2 * count] : [size * count, size * count];
        return {map: texture(c, tile), normal: texture(normalFrom(blurInto(height, .6), 2.2), tile, false), rough: texture(rough, tile, false)};
      });
    }
    // Finn listing bathroom: white 15 x 15 cm glazed wall tiles, light grey grout.
    function wallTiles() {const s = tileSet('wallTiles', {count: 8, color: 0xe2e2df, grout: 0xb9bab6, size: .15, variance: .025});return standard({map: s.map, normalMap: s.normal, normalScale: new T.Vector2(.8, .8), roughnessMap: s.rough, roughness: 1, metalness: 0});}
    // Small light-grey floor tiles laid on the diagonal.
    function floorTiles() {const s = tileSet('floorTiles', {count: 10, color: 0xc9c8c3, grout: 0x8f908c, size: .10, diagonal: true, variance: .07, groutPx: 4});return standard({map: s.map, normalMap: s.normal, normalScale: new T.Vector2(.7, .7), roughnessMap: s.rough, roughness: 1, metalness: 0});}
    function brickPainter(ctx, w, h, hx, opts = {}) {
      const course = opts.course || 16, brickW = opts.brickW || 46, mortar = opts.mortar || 2.5;
      ctx.fillStyle = '#b7aa9b';ctx.fillRect(0, 0, w, h);if (hx) {hx.fillStyle = '#383838';hx.fillRect(0, 0, w, h);}
      const palette = opts.palette || [0x8f4f3b, 0x9b5a43, 0xa4634a, 0x87483a, 0x97563f, 0x7c4334, 0xab6c50];
      for (let r = 0, y = 0; y < h; r++, y += course) {
        const offset = (r % 2) * brickW / 2;
        for (let x = -brickW; x < w + brickW; x += brickW) {
          const c = new T.Color(palette[Math.floor(rand() * palette.length)]).multiplyScalar(.9 + rand() * .2);
          ctx.fillStyle = '#' + c.getHexString();ctx.fillRect(x + offset + mortar / 2, y + mortar / 2, brickW - mortar, course - mortar);
          ctx.fillStyle = `rgba(0,0,0,${rand() * .12})`;ctx.fillRect(x + offset + mortar / 2, y + course * .55, brickW - mortar, course * .45 - mortar / 2);
          if (hx) {hx.fillStyle = `rgb(${190 + rand() * 40 | 0},0,0)`;hx.fillStyle = '#c8c8c8';hx.fillRect(x + offset + mortar / 2 + .5, y + mortar / 2 + .5, brickW - mortar - 1, course - mortar - 1);}
        }
      }
      speckle(ctx, w, h, w * h / 60, 1.5, ['#000000', '#ffffff', '#5a2a1a'], .18);
    }
    const brickSet = () => memo('brick', () => {
      const px = 512, c = canvas(px), height = canvas(px);brickPainter(c.getContext('2d'), px, px, height.getContext('2d'), {course: 16, brickW: 64, mortar: 3});
      // 512 px = 32 courses of 7.5 cm (2.4 m) and 8 bricks of 30 cm per row.
      const tile = [2.4, 2.4];return {map: texture(c, tile), normal: texture(normalFrom(blurInto(height, .7), 2.5), tile, false)};
    });
    function brick() {const s = brickSet();return standard({map: s.map, normalMap: s.normal, normalScale: new T.Vector2(.9, .9), roughness: .92, metalness: 0});}
    // Facade bays for surrounding buildings: four 3 m bays x one 2.85 m storey per texture.
    const facadeSet = () => memo('facade', () => {
      const w = 1024, h = 244, c = canvas(w, h), ctx = c.getContext('2d'), height = canvas(w, h), hx = height.getContext('2d');
      brickPainter(ctx, w, h, hx, {course: 6.4, brickW: 25, mortar: 1.3});
      const bay = w / 4, mpp = 12 / w;
      for (let i = 0; i < 4; i++) {
        const ww = 1.25 / mpp, x = i * bay + (bay - ww) / 2;
        const top = h * (1 - (0.85 + 1.45) / 2.85), hh = h * 1.45 / 2.85;
        ctx.fillStyle = '#d9d4c8';ctx.fillRect(x - 4, top + hh, ww + 8, 5); // sill
        ctx.fillStyle = '#4b3324';ctx.fillRect(x, top, ww, hh); // brown frame
        const g = ctx.createLinearGradient(0, top, 0, top + hh);g.addColorStop(0, '#9fb4c4');g.addColorStop(.45, '#5d6f7d');g.addColorStop(1, '#2c343a');
        ctx.fillStyle = g;ctx.fillRect(x + 5, top + 5, ww / 2 - 7, hh - 10);ctx.fillRect(x + ww / 2 + 2, top + 5, ww / 2 - 7, hh - 10);
        ctx.fillStyle = '#efece4';for (const xx of [x + 3, x + ww / 2 - 2, x + ww / 2 + 1, x + ww - 5]) ctx.fillRect(xx, top + 3, 2, hh - 6);ctx.fillRect(x + 3, top + 3, ww - 6, 2);ctx.fillRect(x + 3, top + hh - 5, ww - 6, 2);
        if (rand() < .55) {ctx.fillStyle = ['#e9e1cf', '#c9c3b5', '#b9433b', '#4c6a8a'][Math.floor(rand() * 4)];ctx.globalAlpha = .75;ctx.fillRect(x + 6, top + 6, ww * (.12 + rand() * .2), hh - 12);ctx.fillRect(x + ww - 6 - ww * .18, top + 6, ww * .18, hh - 12);ctx.globalAlpha = 1;}
        hx.fillStyle = '#101010';hx.fillRect(x, top, ww, hh);hx.fillStyle = '#e0e0e0';hx.fillRect(x - 4, top + hh, ww + 8, 5);
      }
      return {map: texture(c, [12, 2.85]), normal: texture(normalFrom(blurInto(height, .6), 2), [12, 2.85], false)};
    });
    function facade() {const s = facadeSet();return standard({map: s.map, normalMap: s.normal, normalScale: new T.Vector2(.7, .7), roughness: .85, metalness: 0});}
    const plinthSet = () => memo('plinth', () => {
      const w = 1024, h = 122, c = canvas(w, h), ctx = c.getContext('2d');brickPainter(ctx, w, h, null, {course: 6.4, brickW: 25, mortar: 1.3, palette: [0x7c4334, 0x87483a, 0x8f4f3b]});
      for (let i = 0; i < 4; i++) {const x = i * 256 + 98;ctx.fillStyle = '#3a3a36';ctx.fillRect(x, 30, 60, 34);ctx.fillStyle = '#8e9aa3';ctx.fillRect(x + 4, 34, 52, 26);}
      ctx.fillStyle = '#6d6a64';ctx.fillRect(0, h - 22, w, 22);
      return {map: texture(c, [12, 1.425])};
    });
    function plinth() {return standard({map: plinthSet().map, roughness: .9});}
    const roofSet = () => memo('roof', () => {
      const px = 256, c = canvas(px), ctx = c.getContext('2d');ctx.fillStyle = '#8c9998';ctx.fillRect(0, 0, px, px);
      for (let x = 0; x < px; x += 16) {ctx.fillStyle = '#a7b3b1';ctx.fillRect(x, 0, 2, px);ctx.fillStyle = '#6f7a79';ctx.fillRect(x + 2, 0, 1, px);}
      speckle(ctx, px, px, 2500, 2, ['#5d6463', '#b8c2c0'], .25);return {map: texture(c, [8, 8])};
    });
    function roof() {return standard({map: roofSet().map, roughness: .55, metalness: .35});}
    const terrazzoSet = () => memo('terrazzo', () => {
      const px = 512, c = canvas(px), ctx = c.getContext('2d');ctx.fillStyle = '#a49c90';ctx.fillRect(0, 0, px, px);
      speckle(ctx, px, px, 14000, 3, ['#ece6da', '#5e5a54', '#c4a27c', '#7d776e', '#d8d2c6'], .9);return {map: texture(c, [1.2, 1.2])};
    });
    function terrazzo() {return standard({map: terrazzoSet().map, roughness: .35, metalness: 0});}
    const concreteSet = () => memo('concrete', () => {
      const px = 512, c = canvas(px), ctx = c.getContext('2d');ctx.drawImage(cloud(px, 300, 26), 0, 0);speckle(ctx, px, px, 12000, 1.5, ['#505050', '#b0b0b0'], .3);
      return {map: texture(c, [2.5, 2.5]), normal: texture(normalFrom(blurInto(c, 1), 1.2), [2.5, 2.5], false)};
    });
    function concrete(color = 0xb5b2aa) {const s = concreteSet();return standard({color: new T.Color(color).multiplyScalar(1.5), map: s.map, normalMap: s.normal, normalScale: new T.Vector2(.4, .4), roughness: .95});}
    const weaveSet = () => memo('weave', () => {
      const px = 256, c = canvas(px), ctx = c.getContext('2d');ctx.fillStyle = '#a0a0a0';ctx.fillRect(0, 0, px, px);
      for (let y = 0; y < px; y += 2) {ctx.fillStyle = `rgba(0,0,0,${.05 + rand() * .07})`;ctx.fillRect(0, y, px, 1);}
      for (let x = 0; x < px; x += 2) {ctx.fillStyle = `rgba(255,255,255,${.03 + rand() * .06})`;ctx.fillRect(x, 0, 1, px);}
      speckle(ctx, px, px, 3000, 1.4, ['#000000', '#ffffff'], .12);
      return {map: texture(c, [.12, .12]), normal: texture(normalFrom(c, 1.4), [.12, .12], false)};
    });
    function fabric(color) {const s = weaveSet();return standard({color: new T.Color(color).multiplyScalar(1.45), map: s.map, normalMap: s.normal, normalScale: new T.Vector2(.5, .5), roughness: .97, metalness: 0});}
    const grainSet = () => memo('grain', () => {
      const px = 512, c = canvas(px), ctx = c.getContext('2d');ctx.fillStyle = '#9a9a9a';ctx.fillRect(0, 0, px, px);
      for (let g = 0; g < 120; g++) {const y = rand() * px, amp = 2 + rand() * 6, f = .006 + rand() * .01;ctx.strokeStyle = rand() < .6 ? 'rgba(40,40,40,.16)' : 'rgba(255,255,255,.12)';ctx.lineWidth = .6 + rand() * 2;ctx.beginPath();for (let x = 0; x <= px; x += 6) ctx.lineTo(x, y + Math.sin(x * f + g) * amp);ctx.stroke();}
      return {map: texture(c, [.9, .9])};
    });
    function wood(color = 0x9b6b43, roughness = .55) {return standard({color: new T.Color(color).multiplyScalar(1.55), map: grainSet().map, roughness, metalness: 0});}
    function leather(color = 0x5a3b2a) {const s = concreteSet();return standard({color: new T.Color(color).multiplyScalar(1.5), map: s.map, normalMap: s.normal, normalScale: new T.Vector2(.6, .6), roughness: .55});}
    const paint = (color, roughness = .45) => standard({color, roughness, metalness: 0});
    const metal = (color = 0x8a8f94, roughness = .35) => standard({color, roughness, metalness: .9});
    const chrome = () => standard({color: 0xe8ecef, roughness: .08, metalness: 1});
    const ceramic = () => standard({color: 0xf7f7f5, roughness: .12, metalness: 0});
    const glass = () => new T.MeshPhysicalMaterial({color: 0xdfeef2, transparent: true, opacity: .16, roughness: .03, metalness: 0, reflectivity: .6, depthWrite: false, side: T.DoubleSide});
    const steelPlateSet = () => memo('steelPlate', () => {
      const px = 256, c = canvas(px), ctx = c.getContext('2d'), h = canvas(px), hx = h.getContext('2d');ctx.fillStyle = '#8b8f92';ctx.fillRect(0, 0, px, px);hx.fillStyle = '#404040';hx.fillRect(0, 0, px, px);
      for (let y = 0; y < px; y += 32) for (let x = 0; x < px; x += 32) for (const [ox, oy, a] of [[8, 8, .6], [24, 24, -.6]]) {
        for (const k of [ctx, hx]) {k.save();k.translate(x + ox, y + oy);k.rotate(a);k.fillStyle = k === ctx ? '#a5a9ab' : '#e0e0e0';k.fillRect(-9, -2.5, 18, 5);k.restore();}
      }
      speckle(ctx, px, px, 2500, 2, ['#5d6264', '#b9bdbf'], .3);
      return {map: texture(c, [.3, .3]), normal: texture(normalFrom(blurInto(h, .8), 3), [.3, .3], false)};
    });
    function steelPlate() {const s = steelPlateSet();return standard({map: s.map, normalMap: s.normal, roughness: .45, metalness: .7});}
    const polymer = (color = 0x26282b) => {const s = concreteSet();return standard({color: new T.Color(color).multiplyScalar(1.4), map: s.map, normalMap: s.normal, normalScale: new T.Vector2(.25, .25), roughness: .62, metalness: 0});};
    function glove() {const s = weaveSet();return standard({color: 0x4a4e52, map: s.map, normalMap: s.normal, normalScale: new T.Vector2(.9, .9), roughness: .62, metalness: 0});}
    function sleeve() {const s = weaveSet();return standard({color: 0x34405a, map: s.map, normalMap: s.normal, normalScale: new T.Vector2(1, 1), roughness: .95, metalness: 0});}
    const leafSet = () => memo('leaves', () => {
      const px = 256, c = canvas(px), ctx = c.getContext('2d');ctx.fillStyle = '#7d7d7d';ctx.fillRect(0, 0, px, px);
      for (let i = 0; i < 2600; i++) {const v = 70 + rand() * 140 | 0;ctx.fillStyle = `rgb(${v},${v},${v})`;ctx.beginPath();ctx.ellipse(rand() * px, rand() * px, 2 + rand() * 4, 1 + rand() * 2, rand() * 3, 0, 7);ctx.fill();}
      return {map: texture(c, [1.5, 1.5]), normal: texture(normalFrom(blurInto(c, .5), 3), [1.5, 1.5], false)};
    });
    function leaves(color = 0x4f6b32) {const s = leafSet();return standard({color: new T.Color(color).multiplyScalar(1.6), map: s.map, normalMap: s.normal, roughness: .85, metalness: 0});}
    function bark() {const s = concreteSet();return standard({color: 0x8a7b6a, map: s.map, normalMap: s.normal, normalScale: new T.Vector2(1.2, 1.2), roughness: .95});}
    function rug(colors = [0x8e2f2b, 0xd8c7a6, 0x2f3b56]) {
      return memo('rug' + colors.join(), () => {
        const px = 512, c = canvas(px), ctx = c.getContext('2d');ctx.fillStyle = hex(colors[0]);ctx.fillRect(0, 0, px, px);
        ctx.strokeStyle = hex(colors[1]);ctx.lineWidth = 14;ctx.strokeRect(26, 26, px - 52, px - 52);ctx.lineWidth = 4;ctx.strokeRect(54, 54, px - 108, px - 108);
        for (let i = 0; i < 9; i++) for (let j = 0; j < 9; j++) {ctx.fillStyle = hex(colors[(i + j) % 2 ? 1 : 2]);ctx.save();ctx.translate(90 + i * 41.5, 90 + j * 41.5);ctx.rotate(Math.PI / 4);ctx.fillRect(-9, -9, 18, 18);ctx.restore();}
        speckle(ctx, px, px, 30000, 1.3, ['#000000', '#ffffff'], .1);
        const t = texture(c, [1, 1]);return standard({map: t, roughness: 1, metalness: 0});
      });
    }

    // ---------- UV helper ----------
    // Writes UVs in metres per face, using the face's dominant normal axis, including the mesh's
    // offset within its parent. Patterns then continue across a wall split into several boxes.
    function boxUV(mesh, axisRotation = false) {
      const g = mesh.geometry, pos = g.attributes.position, nor = g.attributes.normal, uv = g.attributes.uv;
      if (!pos || !nor || !uv) return mesh;
      const o = mesh.position;
      for (let i = 0; i < pos.count; i++) {
        const x = pos.getX(i) + o.x, y = pos.getY(i) + o.y, z = pos.getZ(i) + o.z, nx = Math.abs(nor.getX(i)), ny = Math.abs(nor.getY(i)), nz = Math.abs(nor.getZ(i));
        if (ny >= nx && ny >= nz) uv.setXY(i, axisRotation ? z : x, axisRotation ? x : z);
        else if (nx >= nz) uv.setXY(i, z, y);
        else uv.setXY(i, x, y);
      }
      uv.needsUpdate = true;return mesh;
    }

    // ---------- Static geometry merging (performance) ----------
    // Bakes every static mesh below `group` into one mesh per material (draw calls drop from hundreds to tens).
    // Meshes for which keep(mesh) is true, or under an object with userData.noMerge, stay as they are.
    function mergeStatic(group, keep = () => false) {
      group.updateMatrixWorld(true);
      const inv = new T.Matrix4().copy(group.matrixWorld).invert(), buckets = new Map(), removed = [];
      const skip = o => {for (let p = o; p && p !== group; p = p.parent) if (p.userData.noMerge || !p.visible) return true;return false;};
      group.traverse(o => {
        if (!o.isMesh || o.isInstancedMesh || o.isSkinnedMesh || keep(o) || skip(o)) return;
        const g = (o.geometry.index ? o.geometry.toNonIndexed() : o.geometry.clone()).applyMatrix4(new T.Matrix4().multiplyMatrices(inv, o.matrixWorld));
        const parts = Array.isArray(o.material) ? (g.groups.length ? g.groups : [{start: 0, count: g.attributes.position.count, materialIndex: 0}]).map(gr => [o.material[gr.materialIndex], gr.start, gr.count]) : [[o.material, 0, g.attributes.position.count]];
        for (const [mat, start, count] of parts) {
          if (!mat) continue;
          if (!buckets.has(mat)) buckets.set(mat, {pos: [], nor: [], uv: [], cast: o.castShadow});
          const b = buckets.get(mat), P = g.attributes.position, N = g.attributes.normal, U = g.attributes.uv;
          for (let i = start; i < start + count; i++) {b.pos.push(P.getX(i), P.getY(i), P.getZ(i));if (N) b.nor.push(N.getX(i), N.getY(i), N.getZ(i));else b.nor.push(0, 1, 0);if (U) b.uv.push(U.getX(i), U.getY(i));else b.uv.push(0, 0);}
          b.cast = b.cast || o.castShadow;
        }
        g.dispose();removed.push(o);
      });
      removed.forEach(o => o.parent && o.parent.remove(o));
      buckets.forEach((b, mat) => {
        const g = new T.BufferGeometry();g.setAttribute('position', new T.Float32BufferAttribute(b.pos, 3));g.setAttribute('normal', new T.Float32BufferAttribute(b.nor, 3));g.setAttribute('uv', new T.Float32BufferAttribute(b.uv, 2));g.computeBoundingSphere();
        const m = new T.Mesh(g, mat);m.castShadow = b.cast && !mat.transparent;m.receiveShadow = true;m.userData.merged = true;group.add(m);
      });
      return buckets.size;
    }

    // ---------- Environment ----------
    // A soft daylit room: bright sky-coloured panels above and to one side, warm floor bounce.
    function environment() {
      return memo('environment', () => {
        const pm = new T.PMREMGenerator(renderer), env = new T.Scene();
        const room = new T.Mesh(new T.BoxGeometry(20, 10, 20), new T.MeshBasicMaterial({color: 0x9aa3a8, side: T.BackSide}));env.add(room);
        const panel = (w, h, x, y, z, rx, ry, color, intensity) => {const m = new T.Mesh(new T.PlaneGeometry(w, h), new T.MeshBasicMaterial({color: new T.Color(color).multiplyScalar(intensity), side: T.DoubleSide}));m.position.set(x, y, z);m.rotation.set(rx, ry, 0);env.add(m);};
        panel(18, 18, 0, 4.9, 0, Math.PI / 2, 0, 0xdfeaf4, 2.2);
        panel(10, 5, 0, 2, -9.9, 0, 0, 0xcfe3f5, 3.4);
        panel(6, 4, 9.9, 2, 3, 0, -Math.PI / 2, 0xfff1d8, 2.6);
        panel(18, 18, 0, -4.9, 0, -Math.PI / 2, 0, 0x8a7d6c, .9);
        const target = pm.fromScene(env, .02);pm.dispose();return target.texture;
      });
    }
    return {plaster, woodFloor, wallTiles, floorTiles, brick, facade, plinth, roof, terrazzo, concrete, fabric, leather, wood, paint, metal, chrome, ceramic, glass, steelPlate, polymer, glove, sleeve, leaves, bark, rug, boxUV, environment, mergeStatic, rand};
  }
  return {create};
});
