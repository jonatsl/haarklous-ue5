/* Minigames in the open world: disc golf (two par-3 holes about 70 m in Torshovdalen) and petanque (singles, 3 boules,
 * game to 13 points, real scoring) against a CGI opponent who always waits at the court.
 * GameMinigames.create({T, lib, audio, heightAt, toWorld, data, hud}) -> {discgolf, petanque}
 * Each game: group (scene objects), near(player) -> prompt text or null, start(player), update(dt, input, view) -> camera override,
 *   active, stop(). Input: {aimYaw, aimPitch, charge(0..1 while held), release (bool), alt (L2 held), cancel}.
 */
(function (root, factory) {
  root.GameMinigames = factory();
})(typeof window === 'undefined' ? globalThis : window, function () {
  'use strict';
  function create({T, lib, audio, heightAt, toWorld, data, hud, trees}) {
    const V3 = (x, y, z) => new T.Vector3(x, y, z);
    const mesh = (parent, geo, mat, x = 0, y = 0, z = 0) => {const m = new T.Mesh(geo, mat);m.position.set(x, y, z);m.castShadow = m.receiveShadow = true;parent.add(m);return m;};
    const gauss = () => {let u = 0, v = 0;while (!u) u = Math.random();while (!v) v = Math.random();return Math.sqrt(-2 * Math.log(u)) * Math.cos(2 * Math.PI * v);};
    const treeWorld = (trees || []).map(t => {const w = toWorld(t.x, t.s);return {x: w.x, z: w.z, h: t.h || 11};});

    // ======================= DISC GOLF =======================
    function discGolf() {
      const group = new T.Group();group.name = 'Disc golf';
      const holes = (data.discgolf || []).map((h, i) => {
        const tee = toWorld(h.tee[0], h.tee[1]), basket = toWorld(h.basket[0], h.basket[1]);
        tee.y = heightAt(tee.x, tee.z);basket.y = heightAt(basket.x, basket.z);
        return {n: i + 1, par: h.par, tee, basket, length: Math.hypot(basket.x - tee.x, basket.z - tee.z)};
      });
      const galv = lib.metal(0xb9bec2, .35), yellow = lib.paint(0xf2c200, .4), chainM = lib.metal(0xd7dadc, .25), pad = lib.concrete(0x7b7d78), sign = lib.wood(0x6b4a2b, .6);
      for (const h of holes) {
        // Tee pad facing the basket, with a hole sign.
        const dir = Math.atan2(h.basket.x - h.tee.x, h.basket.z - h.tee.z);
        const t = mesh(group, new T.BoxGeometry(1.5, .08, 3), pad, h.tee.x, h.tee.y + .02, h.tee.z);t.rotation.y = dir;
        const post = mesh(group, new T.BoxGeometry(.1, 1.4, .1), sign, h.tee.x + Math.cos(dir) * 1.3, h.tee.y + .7, h.tee.z - Math.sin(dir) * 1.3);
        const c = document.createElement('canvas');c.width = 256;c.height = 160;const x = c.getContext('2d');x.fillStyle = '#1f4d2b';x.fillRect(0, 0, 256, 160);x.fillStyle = '#fff';x.font = 'bold 44px Arial';x.fillText(`HULL ${h.n}`, 20, 55);x.font = '30px Arial';x.fillText(`Par ${h.par} · ${Math.round(h.length)} m`, 20, 110);
        const tex = new T.CanvasTexture(c);tex.colorSpace = T.SRGBColorSpace;
        const board = mesh(group, new T.BoxGeometry(.6, .38, .03), new T.MeshStandardMaterial({map: tex, roughness: .6}), post.position.x, h.tee.y + 1.45, post.position.z);board.rotation.y = dir + Math.PI;
        // Basket: pole, tray, chains, top band.
        const b = new T.Group();b.position.set(h.basket.x, h.basket.y, h.basket.z);group.add(b);
        mesh(b, new T.CylinderGeometry(.025, .025, 1.6, 10), galv, 0, .8, 0);
        mesh(b, new T.CylinderGeometry(.3, .27, .2, 24, 1, true), galv, 0, .62, 0);mesh(b, new T.CylinderGeometry(.27, .27, .01, 24), galv, 0, .52, 0);
        mesh(b, new T.TorusGeometry(.29, .012, 8, 32), galv, 0, .72, 0).rotation.x = Math.PI / 2;
        mesh(b, new T.CylinderGeometry(.3, .3, .07, 24, 1, true), yellow, 0, 1.38, 0);
        for (let i = 0; i < 16; i++) {const a = i / 16 * Math.PI * 2, ch = mesh(b, new T.CylinderGeometry(.004, .004, .62, 4), chainM, Math.cos(a) * .17, 1.04, Math.sin(a) * .17);ch.rotation.set(Math.sin(a) * .2, 0, -Math.cos(a) * .2);}
        h.marker = b;
      }
      lib.mergeStatic(group);
      const discMat = new T.MeshPhysicalMaterial({color: 0xff4f9a, roughness: .35, clearcoat: .6});
      const disc = mesh(group, new T.CylinderGeometry(.105, .1, .022, 28), discMat);disc.visible = false;
      const game = {group, holes, active: false, hole: 0, strokes: [], lie: null, disc: {pos: V3(), vel: V3(), bank: 0, flying: false, rest: false}, charging: 0, followT: 0, message: ''};
      game.near = p => {
        if (game.active) return null;
        for (const h of holes) if (Math.hypot(p.x - h.tee.x, p.z - h.tee.z) < 4 && Math.abs(p.y - h.tee.y) < 3) return `□ Play disc golf · Hull ${h.n} (par 3, ${Math.round(h.length)} m)`;
        return null;
      };
      game.start = p => {
        const h = holes.reduce((best, x) => Math.hypot(p.x - x.tee.x, p.z - x.tee.z) < Math.hypot(p.x - best.tee.x, p.z - best.tee.z) ? x : best, holes[0]);
        game.active = true;game.hole = h.n - 1;game.strokes = holes.map(() => 0);game.lie = {x: h.tee.x, z: h.tee.z, y: h.tee.y};game.disc.flying = false;disc.visible = false;game.message = `Hull ${h.n} · Par 3 · Hold R2 (or left mouse) to throw, aim with the camera, left stick X for hyzer/anhyzer.`;
        return {x: h.tee.x, z: h.tee.z, yaw: Math.atan2(-(h.basket.x - h.tee.x), -(h.basket.z - h.tee.z))};
      };
      game.stop = () => {game.active = false;disc.visible = false;hud.discgolf(null);};
      function throwDisc(input) {
        const h = holes[game.hole], dist = Math.hypot(h.basket.x - game.lie.x, h.basket.z - game.lie.z), putt = dist < 11;
        const power = input.power, speed = putt ? 3 + power * 9 : 9 + power * 20, err = (putt ? .025 : .05) * (1 + (input.drunk || 0) * 2);
        const yaw = input.aimYaw + gauss() * err, pitch = input.aimPitch + (putt ? .06 : .09) + gauss() * err * .6;
        const d = V3(-Math.sin(yaw) * Math.cos(pitch), Math.sin(pitch), -Math.cos(yaw) * Math.cos(pitch));
        game.disc.pos.set(game.lie.x, game.lie.y + 1.2, game.lie.z).addScaledVector(d, .5);game.disc.vel.copy(d).multiplyScalar(speed);
        game.disc.bank = (input.hyzer || 0) * .55;game.disc.flying = true;game.disc.putt = putt;game.disc.time = 0;disc.visible = true;game.strokes[game.hole]++;audio.sfx('throw');game.followT = 0;
      }
      function step(dt) {
        const D = game.disc, v = D.vel, speed = v.length();D.time += dt;
        if (speed > .3 && D.flying) {
          // Drag and lift (mid-range driver: glide 4, turn -1, fade 1). Bank tilts lift sideways: turn at high speed, fade when slow.
          const k = .0135, lift = .085 * speed * speed * .195 * (D.putt ? .55 : 1);
          const up = V3(0, 1, 0), dir = v.clone().normalize(), side = V3().crossVectors(dir, up).normalize();
          D.bank += ((speed > 18 ? -.22 : 0) + (speed < 15 ? .55 * (1 - speed / 15) : 0)) * dt * (D.putt ? .3 : 1);
          const liftDir = up.clone().multiplyScalar(Math.cos(D.bank)).addScaledVector(side, Math.sin(D.bank));
          v.addScaledVector(dir, -k * speed * speed * dt).addScaledVector(liftDir, lift * dt).y -= 9.81 * dt;
        }
        D.pos.addScaledVector(v, dt);
        // Trees: trunk or crown hits knock the disc down.
        for (const t of treeWorld) {
          const dx = D.pos.x - t.x, dz = D.pos.z - t.z;if (Math.abs(dx) > 5 || Math.abs(dz) > 5) continue;
          const gy = heightAt(t.x, t.z), r = Math.hypot(dx, dz), crownY = gy + t.h * .62, crownR = t.h * .28;
          if ((r < .3 && D.pos.y < gy + t.h * .5) || (Math.hypot(r, (D.pos.y - crownY) * 1.3) < crownR && Math.random() < dt * 12)) {v.multiplyScalar(.25);v.x += gauss() * 1.5;v.z += gauss() * 1.5;audio.sfx('thud', {gain: .5});}
        }
        // Basket: chains catch a disc at the right height when it is not too fast.
        const h = holes[game.hole], bx = D.pos.x - h.basket.x, bz = D.pos.z - h.basket.z, by = D.pos.y - h.basket.y, rb = Math.hypot(bx, bz);
        if (rb < .33 && by > .55 && by < 1.4) {
          if (speed < 13 || Math.random() < .3) {audio.sfx('chains');D.flying = false;v.set(0, 0, 0);D.pos.set(h.basket.x, h.basket.y + .62, h.basket.z);return 'in';}
          v.multiplyScalar(-.25);audio.sfx('chains', {gain: .5});
        }
        if (rb < .05 + .025 && by > 0 && by < 1.6) {v.x *= -.4;v.z *= -.4;audio.sfx('click');}
        const g = heightAt(D.pos.x, D.pos.z);
        // Once down, the disc stays on the grass (also downhill) and skids to a stop.
        if (!D.flying || D.pos.y < g + .015) {
          D.pos.y = g + .015;
          if (D.flying) {D.flying = false;audio.sfx('thud', {gain: .4});v.y = 0;v.multiplyScalar(.35);}
          v.y = 0;const hs = Math.hypot(v.x, v.z), dec = 6 * dt;if (hs <= dec || D.time > 12) {v.set(0, 0, 0);return 'rest';}v.multiplyScalar((hs - dec) / hs);
        }
        if (D.time > 12) {v.set(0, 0, 0);D.flying = false;D.pos.y = g + .015;return 'rest';}
        return null;
      }
      game.update = (dt, input, view) => {
        if (!game.active) return null;
        const h = holes[game.hole], D = game.disc;
        if (input.cancel) {game.stop();return {done: 'Disc golf round ended.'};}
        if (D.flying || (disc.visible && !D.rest && D.vel.lengthSq() > 0)) {
          let r = null;for (let i = 0; i < 4 && !r; i++) r = step(dt / 4);
          disc.position.copy(D.pos);disc.rotation.set(Math.sin(D.bank) * .6, D.time * 40, 0);
          if (r === 'in') {
            const total = game.strokes[game.hole], diff = total - h.par, name = total === 1 ? 'Hole in one!' : diff <= -2 ? 'Eagle!' : diff === -1 ? 'Birdie!' : diff === 0 ? 'Par' : diff === 1 ? 'Bogey' : `+${diff}`;
            game.message = `Hull ${h.n}: ${total} kast · ${name}`;if (diff < 0) audio.sfx('cheer');
            game.pending = {next: true, t: 2.5};D.rest = true;
          } else if (r === 'rest') {D.rest = true;game.lie ={x: D.pos.x, z: D.pos.z, y: heightAt(D.pos.x, D.pos.z)};game.message = `${Math.round(Math.hypot(h.basket.x - D.pos.x, h.basket.z - D.pos.z))} m to the basket. Throw again from where the disc landed.`;game.pending = {lie: true, t: .8};}
          // Follow camera behind and above the disc.
          const back = V3(D.pos.x - game.lie.x, 0, D.pos.z - game.lie.z).normalize();
          hud.discgolf(game.state());
          return {camera: {x: D.pos.x - back.x * 4, y: D.pos.y + 1.8, z: D.pos.z - back.z * 4, lookAt: D.pos.clone()}};
        }
        if (game.pending) {
          game.pending.t -= dt;
          if (game.pending.t <= 0) {
            if (game.pending.next) {
              game.pending = null;disc.visible = false;D.rest = false;
              if (game.hole + 1 < holes.length) {game.hole++;const n = holes[game.hole];game.lie = {x: n.tee.x, z: n.tee.z, y: n.tee.y};game.message = `Hull ${n.n} · Par 3`;hud.discgolf(game.state());return {teleport: {x: n.tee.x, z: n.tee.z, yaw: Math.atan2(-(n.basket.x - n.tee.x), -(n.basket.z - n.tee.z))}};}
              const total = game.strokes.reduce((a, b) => a + b, 0), par = holes.reduce((a, b) => a + b.par, 0);game.stop();
              return {done: `Disc golf finished: ${total} throws (${total - par >= 0 ? '+' : ''}${total - par} vs par ${par}).`};
            }
            game.pending = null;D.rest = false;
            return {teleport: {x: game.lie.x, z: game.lie.z, yaw: Math.atan2(-(h.basket.x - game.lie.x), -(h.basket.z - game.lie.z))}};
          }
        }
        if (input.release && input.power > .03) throwDisc(input);
        hud.discgolf(game.state(input.charge));
        return {lock: true};
      };
      game.state = (charge = 0) => {const h = holes[game.hole];return {hole: h.n, holes: holes.length, par: h.par, strokes: game.strokes.slice(), distance: game.lie ? Math.round(Math.hypot(h.basket.x - game.lie.x, h.basket.z - game.lie.z)) : 0, charge, message: game.message};};
      return game;
    }

    // ======================= PETANQUE =======================
    function petanque() {
      const P = data.petanque, group = new T.Group();group.name = 'Petanque';
      const centre = toWorld(P.x, P.s), along = (() => {const a = toWorld(P.x + Math.cos(P.rot), P.s + Math.sin(P.rot));return {x: a.x - centre.x, z: a.z - centre.z};})();
      const across = {x: -along.z, z: along.x}, L = P.length, W = P.width;
      // The court is levelled at its highest corner so the gravel is never buried in the slope.
      const baseY = Math.max(...[[-1, -1], [1, -1], [1, 1], [-1, 1], [0, 0]].map(([a, b]) => {const x = centre.x + along.x * a * L / 2 + (-along.z) * b * W / 2, z = centre.z + along.z * a * L / 2 + along.x * b * W / 2;return heightAt(x, z);})) + .03;
      const courtPoint = (u, v) => ({x: centre.x + along.x * u + across.x * v, z: centre.z + along.z * u + across.z * v});
      const toCourt = (x, z) => {const dx = x - centre.x, dz = z - centre.z;return {u: dx * along.x + dz * along.z, v: dx * across.x + dz * across.z};};
      const ang = Math.atan2(along.x, along.z);
      const boards = lib.wood(0x7a5a3a, .7);
      for (const [u, v, w, d] of [[0, W / 2 + .05, L + .2, .1], [0, -W / 2 - .05, L + .2, .1], [L / 2 + .05, 0, .1, W], [-L / 2 - .05, 0, .1, W]]) {
        const p = courtPoint(u, v), b = mesh(group, new T.BoxGeometry(d, .12, w), boards, p.x, baseY + .04, p.z);b.rotation.y = ang;
      }
      const gravel = mesh(group, new T.BoxGeometry(W, .6, L), lib.concrete(0xb9ad97), centre.x, baseY - .3, centre.z);gravel.rotation.y = ang;
      // Opponent: a CGI player waiting at the court (always there).
      const npc = new T.Group();group.add(npc);
      const skin = new T.MeshStandardMaterial({color: 0xd9a989, roughness: .7}), shirt = lib.fabric(0x3a5f8a), trousers = lib.fabric(0x4a4a45), shoe = lib.leather(0x2a1d14), capM = lib.fabric(0xd8d2c0);
      const part = (geo, mat, x, y, z, parent = npc) => mesh(parent, geo, mat, x, y, z);
      for (const s of [-1, 1]) {part(new T.CapsuleGeometry(.075, .75, 6, 10), trousers, s * .11, .48, 0);part(new T.BoxGeometry(.11, .07, .26), shoe, s * .11, .04, -.04);}
      part(new T.CapsuleGeometry(.17, .42, 6, 12), shirt, 0, 1.18, 0).scale.set(1.15, 1, .75);
      const head = part(new T.SphereGeometry(.11, 18, 14), skin, 0, 1.68, 0);head.scale.set(.9, 1.05, 1);
      part(new T.CylinderGeometry(.12, .125, .07, 18), capM, 0, 1.78, 0);part(new T.BoxGeometry(.18, .015, .1), capM, 0, 1.75, -.11);
      const armL = new T.Group();armL.position.set(-.24, 1.42, 0);npc.add(armL);part(new T.CapsuleGeometry(.05, .55, 6, 10), shirt, 0, -.3, 0, armL);part(new T.SphereGeometry(.05, 10, 8), skin, 0, -.62, 0, armL);
      const armR = new T.Group();armR.position.set(.24, 1.42, 0);npc.add(armR);part(new T.CapsuleGeometry(.05, .55, 6, 10), shirt, 0, -.3, 0, armR);part(new T.SphereGeometry(.05, 10, 8), skin, 0, -.62, 0, armR);
      const npcHome = courtPoint(-L / 2 - 1.2, 1.4);npc.position.set(npcHome.x, heightAt(npcHome.x, npcHome.z), npcHome.z);npc.rotation.y = ang + Math.PI;
      const steelBoule = lib.metal(0x9aa0a4, .25), bronzeBoule = lib.metal(0x8a6a48, .3), jackMat = lib.paint(0xe8b400, .35);
      const balls = [];
      const makeBall = (owner, r) => {const m = mesh(group, new T.SphereGeometry(r, 20, 14), owner === 'jack' ? jackMat : owner === 'you' ? steelBoule : bronzeBoule);m.visible = false;return {owner, r, mesh: m, u: 0, v: 0, vu: 0, vv: 0, alive: false, flight: null};};
      const game = {group, active: false, score: {you: 0, npc: 0}, mene: 0, message: '', npcName: 'Arne'};
      let jack, boules, turn, circle, thrower, phase, aiTimer = 0, waiting = 0;
      game.near = p => {if (game.active) return null;const c = toCourt(p.x, p.z);return Math.abs(c.u) < L / 2 + 3 && Math.abs(c.v) < W / 2 + 3 ? `□ Play petanque against ${game.npcName} (first to 13)` : null;};
      function newMene(first) {
        game.mene++;balls.forEach(b => {b.mesh.visible = false;});balls.length = 0;
        jack = makeBall('jack', .015);jack.mesh.scale.setScalar(2);balls.push(jack);boules = {you: [0, 1, 2].map(() => makeBall('you', .037)), npc: [0, 1, 2].map(() => makeBall('npc', .037))};balls.push(...boules.you, ...boules.npc);
        thrower = first;phase = 'jack';circle = circle || {u: -L / 2 + 1.2, v: 0};game.message = `Mène ${game.mene}: ${thrower === 'you' ? 'you throw' : game.npcName + ' throws'} the jack (6–10 m).`;aiTimer = 1.6;
      }
      game.start = () => {game.active = true;game.score = {you: 0, npc: 0};game.mene = 0;circle = {u: -L / 2 + 1.2, v: 0};newMene(Math.random() < .5 ? 'you' : 'npc');const s = courtPoint(circle.u, circle.v);return {x: s.x, z: s.z, yaw: Math.atan2(-along.x, -along.z)};};
      game.stop = () => {game.active = false;balls.forEach(b => {b.mesh.visible = false;});hud.petanque(null);npc.position.set(npcHome.x, heightAt(npcHome.x, npcHome.z), npcHome.z);};
      // Throw: aim direction in court space, target distance from power; scatter grows with distance (and drink).
      function launch(ball, dirU, dirV, distance, shoot, drunk = 0) {
        const sigma = .05 * distance * (1 + drunk * 2) + .08, aimU = circle.u + dirU * distance + gauss() * sigma, aimV = circle.v + dirV * distance + gauss() * sigma * .8;
        ball.alive = true;ball.mesh.visible = true;ball.u = circle.u;ball.v = circle.v;
        const land = {u: aimU - dirU * (shoot ? 0 : .5), v: aimV - dirV * (shoot ? 0 : .5)}, flightTime = shoot ? .45 : .9 + distance * .04;
        ball.flight = {from: {u: circle.u, v: circle.v, y: 1.0}, to: land, t: 0, T: flightTime, peak: shoot ? .6 : 1.2 + distance * .25, roll: {u: dirU * (shoot ? 7 : 1.1), v: dirV * (shoot ? 7 : 1.1)}};
        audio.sfx('throw', {gain: .5});
      }
      function physics(dt) {
        let moving = false;
        for (const b of balls) {
          if (!b.alive) continue;
          if (b.flight) {
            const f = b.flight;f.t += dt;const k = Math.min(1, f.t / f.T);b.u = f.from.u + (f.to.u - f.from.u) * k;b.v = f.from.v + (f.to.v - f.from.v) * k;b.y = f.from.y * (1 - k) + 4 * f.peak * k * (1 - k);
            if (k >= 1) {b.flight = null;b.y = 0;b.vu = f.roll.u;b.vv = f.roll.v;audio.sfx(b.owner === 'jack' ? 'click' : 'gravel');}
            moving = true;continue;
          }
          const sp = Math.hypot(b.vu, b.vv);if (sp > 0) {moving = true;const dec = 3.2 * dt, s = Math.max(0, sp - dec) / sp;b.vu *= s;b.vv *= s;b.u += b.vu * dt;b.v += b.vv * dt;}
          // Out of the court (dead ball) if it leaves the boards.
          if (Math.abs(b.u) > L / 2 || Math.abs(b.v) > W / 2) {b.alive = false;b.dead = true;b.mesh.visible = false;}
        }
        // Collisions on the ground: elastic, mass-weighted (boule 700 g, jack 16 g).
        for (let i = 0; i < balls.length; i++) for (let j = i + 1; j < balls.length; j++) {
          const a = balls[i], b = balls[j];if (!a.alive || !b.alive || a.flight || b.flight) continue;
          const du = b.u - a.u, dv = b.v - a.v, d = Math.hypot(du, dv), min = a.r + b.r;
          if (d < min && d > 1e-6) {
            const nu = du / d, nv = dv / d, rel = (a.vu - b.vu) * nu + (a.vv - b.vv) * nv;
            const ma = a.owner === 'jack' ? .016 : .7, mb = b.owner === 'jack' ? .016 : .7;
            if (rel > 0) {const imp = 2 * rel / (ma + mb) * .9;a.vu -= imp * mb * nu;a.vv -= imp * mb * nv;b.vu += imp * ma * nu;b.vv += imp * ma * nv;audio.sfx('boule', {gain: Math.min(1, rel / 3)});}
            const push = (min - d) / 2;a.u -= nu * push;a.v -= nv * push;b.u += nu * push;b.v += nv * push;
          }
        }
        for (const b of balls) if (b.mesh.visible) {const p = courtPoint(b.u, b.v);b.mesh.position.set(p.x, baseY + .02 + b.r * (b.owner === 'jack' ? 2 : 1) + (b.y || 0), p.z);}
        return moving;
      }
      const dist = b => Math.hypot(b.u - jack.u, b.v - jack.v);
      function best(owner) {return boules[owner].filter(b => b.alive).reduce((m, b) => Math.min(m, dist(b)), Infinity);}
      function holding() {const y = best('you'), n = best('npc');return y === Infinity && n === Infinity ? null : y < n ? 'you' : 'npc';}
      const left = owner => boules[owner].filter(b => !b.alive && !b.dead).length;
      function nextThrower() {
        if (phase === 'jack') {phase = 'first';return thrower;}
        if (phase === 'first') {phase = 'play';const other = thrower === 'you' ? 'npc' : 'you';return other;}
        const hold = holding(), other = hold === 'you' ? 'npc' : 'you';
        if (left(other) > 0) return other;if (left(hold) > 0) return hold;return null;
      }
      function endMene() {
        const hold = holding();
        if (!hold) {game.message = 'No points this mène.';return 'npc';}
        const other = hold === 'you' ? 'npc' : 'you', ob = best(other), pts = boules[hold].filter(b => b.alive && dist(b) < ob).length;
        game.score[hold] += pts;game.message = `${hold === 'you' ? 'You score' : game.npcName + ' scores'} ${pts} point${pts > 1 ? 's' : ''}. ${game.score.you}–${game.score.npc}`;
        if (pts && hold === 'you') audio.sfx('cheer');
        circle = {u: Math.max(-L / 2 + 1, Math.min(L / 2 - 1, jack.u)), v: Math.max(-W / 2 + .6, Math.min(W / 2 - .6, jack.v))};
        return hold;
      }
      let current = null, aimingFor = null;
      function npcTurn() {
        // Opponent: points at the jack, or shoots your boule when you hold close and it has boules to spare.
        const ball = phase === 'jack' ? jack : boules.npc.find(b => !b.alive && !b.dead), dirSign = circle.u < 0 ? 1 : -1;
        if (phase === 'jack') {const d = 6.5 + Math.random() * 3;launch(jack, dirSign, (Math.random() - .5) * .15, d, false);return;}
        const yourBest = boules.you.filter(b => b.alive).sort((a, b) => dist(a) - dist(b))[0], shoot = yourBest && dist(yourBest) < .35 && left('npc') >= 2 && Math.random() < .6;
        const target = shoot ? yourBest : jack, du = target.u - circle.u, dv = target.v - circle.v, d = Math.hypot(du, dv);
        launch(ball, du / d, dv / d, d + (shoot ? 0 : .1), shoot);
        npcThrowAnim = .8;
      }
      let npcThrowAnim = 0;
      game.update = (dt, input, view) => {
        if (!game.active) return null;
        if (input.cancel) {game.stop();return {done: `Petanque stopped at ${game.score.you}–${game.score.npc}.`};}
        const moving = physics(dt);
        npcThrowAnim = Math.max(0, npcThrowAnim - dt);armR.rotation.x = npcThrowAnim > 0 ? -Math.sin((1 - npcThrowAnim / .8) * Math.PI) * 2.2 : Math.sin(performance.now() / 900) * .05;
        npc.rotation.y = ang + Math.PI + Math.sin(performance.now() / 1300) * .1;
        if (!moving) {
          if (waiting > 0) waiting -= dt;
          else if (!current) {
            if (phase === 'jack' && jack.alive && !jack.flight) {
              const jd = Math.hypot(jack.u - circle.u, jack.v - circle.v);
              if (jd < 6 || jd > 10 || jack.dead) {game.message = `Jack ${jack.dead ? 'out' : 'at ' + jd.toFixed(1) + ' m'}: thrown again (6–10 m).`;jack.alive = false;jack.dead = false;jack.mesh.visible = false;current = thrower;aimingFor = 'jack';}
              else {current = nextThrower();aimingFor = 'boule';}
            } else if (phase === 'jack') {current = thrower;aimingFor = 'jack';}
            else {current = nextThrower();aimingFor = 'boule';if (current === null) {const winner = endMene();if (game.score.you >= 13 || game.score.npc >= 13) {const won = game.score.you >= 13;game.message = `${won ? 'You win' : game.npcName + ' wins'} ${game.score.you}–${game.score.npc}!`;if (won) audio.sfx('cheer');game.finish = 4;} else {waiting = 2.5;game.nextFirst = winner;}}}
            if (current === 'npc') aiTimer = 1.4;
            game.turnId = (game.turnId || 0) + 1;
          }
        }
        if (game.finish !== undefined) {game.finish -= dt;if (game.finish <= 0) {const msg = game.message;delete game.finish;game.stop();return {done: msg};}}
        if (game.nextFirst && waiting <= 0 && current === null && !moving) {const f = game.nextFirst;game.nextFirst = null;newMene(f);current = null;}
        if (current === 'npc' && !moving) {aiTimer -= dt;if (aiTimer <= 0) {npcTurn();current = null;waiting = .3;}}
        if (current === 'you' && !moving && input.release && input.power > .03) {
          const ball = aimingFor === 'jack' ? jack : boules.you.find(b => !b.alive && !b.dead);
          if (ball) {
            if (ball === jack) {jack.mesh.visible = true;}
            const fwd = {x: -Math.sin(input.aimYaw), z: -Math.cos(input.aimYaw)}, du = fwd.x * along.x + fwd.z * along.z, dv = fwd.x * across.x + fwd.z * across.z, n = Math.hypot(du, dv) || 1;
            launch(ball, du / n, dv / n, 1.5 + input.power * 11, input.alt, input.drunk);current = null;waiting = .3;
          }
        }
        // HUD: score, mène, boules left, who holds the point and by how much, markers over boules.
        const hold = jack && jack.alive ? holding() : null, markers = balls.filter(b => b.alive && b.mesh.visible).map(b => ({pos: b.mesh.position.clone(), label: b.owner === 'jack' ? 'jack' : `${dist(b).toFixed(2)} m`, owner: b.owner, best: b.owner !== 'jack' && hold === b.owner && dist(b) === best(b.owner)}));
        hud.petanque({score: game.score, mene: game.mene, you: left('you'), npc: left('npc'), turn: current, aim: aimingFor, hold, margin: hold ? Math.abs(best('you') - best('npc')) : 0, message: game.message, charge: current === 'you' ? input.charge : 0, markers, npcName: game.npcName, shoot: input.alt});
        if (current === 'you' && !moving) {
          const s = courtPoint(circle.u, circle.v), r = {lock: true, stand: {x: s.x, z: s.z, y: baseY}};
          // Turn to face the jack (or down the court for the jack throw) when your turn starts.
          if (game.faced !== game.turnId) {game.faced = game.turnId;const t = aimingFor === 'jack' || !jack.alive ? courtPoint(circle.u < 0 ? L / 2 : -L / 2, 0) : jack.mesh.position;r.face = Math.atan2(-(t.x - s.x), -(t.z - s.z));}
          return r;
        }
        return {lock: true};
      };
      game.debug = () => ({jack: jack && {u: jack.u, v: jack.v, alive: jack.alive, dead: jack.dead}, boules: boules && Object.fromEntries(Object.entries(boules).map(([k, l]) => [k, l.map(b => ({u: +b.u.toFixed(2), v: +b.v.toFixed(2), alive: b.alive, dead: !!b.dead}))])), circle, phase, current, aimingFor});
      return game;
    }
    return {discgolf: discGolf(), petanque: petanque()};
  }
  return {create};
});
