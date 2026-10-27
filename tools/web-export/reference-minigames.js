/* Runs inside haarklous-game.html (tools/web-export/export.mjs --only=tests --script=reference-minigames.js).
 * Plays the web version's own minigames and firepit with scripted inputs and a fixed time step, and records what they do
 * every step into unreal/Haarklous/Content/Data/tests/minigames.json. HKMinigamesTests.cpp replays the same inputs on
 * the C++ port and compares.
 * - Fresh instances (GameMinigames.create / GameFire.create) with a recording audio and HUD; the page's own are untouched.
 * - Math.random is replaced by the Park-Miller generator (seed per scenario) for the game code. three.js also calls
 *   Math.random for every new object's UUID; those calls are recognised by their source line and get the real generator,
 *   so the game code sees exactly the sequence the C++ test uses.
 * - performance.now() and Date.now() are fake clocks advanced by the fixed step.
 * The driver (where to aim, how long to hold R2) is only here; the C++ test replays the recorded controls.
 */
(() => {
  'use strict';
  const G = window.haarklousGame, T = window.THREE, W = G.world, files = window.__hkFiles, log = window.__hkLog, lib = window.__hkLib;
  const DT = 1 / 60;
  const n12 = v => typeof v === 'number' ? (Number.isFinite(v) ? +v.toPrecision(12) : (Number.isNaN(v) ? null : v > 0 ? 1e9 : -1e9)) : v;
  const A = a => Array.from(a, n12);

  // ---------- Deterministic randomness and clocks ----------
  const realRandom = Math.random, realPerf = performance.now, realDate = Date.now;
  const callerFrame = () => {const keep = Error.prepareStackTrace;Error.prepareStackTrace = (e, s) => s;const s = new Error().stack;Error.prepareStackTrace = keep;return s[2];};
  let uuidLine = -1, seed = 1, gameCalls = 0, uuidCalls = 0;
  Math.random = () => {const c = callerFrame();uuidLine = c ? c.getLineNumber() : -1;return realRandom();};
  new T.Object3D();
  Math.random = () => {const c = callerFrame();if (c && c.getLineNumber() === uuidLine) {uuidCalls++;return realRandom();}gameCalls++;return (seed = (seed * 16807) % 2147483647) / 2147483647;};
  let nowMs = 5000;
  performance.now = () => nowMs;
  // The driver's own choices (never Math.random).
  let dseed = 99;const drand = () => (dseed = (dseed * 16807) % 2147483647) / 2147483647;

  const sfx = [];let step = 0;
  const audio = {sfx: (name, o) => sfx.push([step, name, o && o.gain !== undefined ? n12(o.gain) : 1])};
  let hudDg = null, hudPq = null;
  const hud = {discgolf: s => {hudDg = s;}, petanque: s => {hudPq = s;}};
  const mini = window.GameMinigames.create({T, lib, audio, heightAt: W.heightWorld, toWorld: W.toWorld, data: window.SURROUNDINGS, hud, trees: W.trees});
  const heightAt = W.heightWorld;
  // game-main.js updateMinigame(): R2 held charges, letting go releases.
  function mainInput(g, held, c) {
    if (held) g.charge = Math.min(1, (g.charge || 0) + DT / 1.2);
    const release = !held && (g.charge || 0) > 0, power = g.charge || 0;if (release) g.charge = 0;
    return {aimYaw: c.yaw, aimPitch: c.pitch, charge: g.charge || 0, power, release, alt: c.alt, hyzer: c.hyzer, cancel: c.cancel, drunk: c.drunk};
  }
  const res = r => {
    if (!r) return null;
    const o = {};
    if (r.teleport) o.teleport = A([r.teleport.x, r.teleport.z, r.teleport.yaw]);
    if (r.stand) o.stand = A([r.stand.x, r.stand.y, r.stand.z]);
    if (r.face !== undefined) o.face = n12(r.face);
    if (r.done) o.done = r.done;
    if (r.camera) o.camera = A([r.camera.x, r.camera.y, r.camera.z, r.camera.lookAt.x, r.camera.lookAt.y, r.camera.lookAt.z]);
    if (r.lock) o.lock = true;
    return o;
  };
  // Controls are stored only when they change: [step, held, yaw, pitch, alt, hyzer, cancel, drunk].
  function controlLog() {
    const list = [];let last = '';
    return {list, put(c) {const row = [c.held ? 1 : 0, n12(c.yaw), n12(c.pitch), c.alt ? 1 : 0, n12(c.hyzer), c.cancel ? 1 : 0, n12(c.drunk)], key = row.join();if (key !== last) {list.push([step, ...row]);last = key;}}};
  }
  const ctlValue = c => ({...c, yaw: n12(c.yaw), pitch: n12(c.pitch), hyzer: n12(c.hyzer), drunk: n12(c.drunk)});

  // ======================= DISC GOLF =======================
  function discGolfRun() {
    const g = mini.discgolf, disc = g.group.children.find(o => o.isMesh && o.geometry.type === 'CylinderGeometry' && Math.abs(o.geometry.parameters.radiusTop - .105) < 1e-9);
    seed = 20261027;gameCalls = 0;sfx.length = 0;step = 0;nowMs = 5000;
    const out = {seed, holes: g.holes.map(h => ({n: h.n, par: h.par, tee: A([h.tee.x, h.tee.y, h.tee.z]), basket: A([h.basket.x, h.basket.y, h.basket.z]), length: n12(h.length)})), near: [], steps: []};
    for (const h of g.holes) for (const [dx, dy, dz] of [[0, 0, 0], [3.9, 0, 0], [0, 0, -4.1], [1, 2.9, 1], [1, 3.1, 1], [-20, 0, 5]]) {const p = {x: h.tee.x + dx, y: h.tee.y + dy, z: h.tee.z + dz};out.near.push([A([p.x, p.y, p.z]), g.near(p)]);}
    // Start next to hole 1 and play both holes with an aiming driver that predicts the flight (no trees, no scatter).
    const h0 = g.holes[0], sp = {x: h0.tee.x + .6, z: h0.tee.z - .4, y: h0.tee.y};
    const r0 = g.start(sp);out.start = {p: A([sp.x, sp.y, sp.z]), r: A([r0.x, r0.z, r0.yaw])};
    out.nearActive = g.near({x: h0.tee.x, y: h0.tee.y, z: h0.tee.z});
    let throws = 0;
    const predict = (lie, yaw, pitch, power, putt, hyzer, h) => {
      const speed0 = putt ? 3 + power * 9 : 9 + power * 20, pt = pitch + (putt ? .06 : .09);
      const d = new T.Vector3(-Math.sin(yaw) * Math.cos(pt), Math.sin(pt), -Math.cos(yaw) * Math.cos(pt));
      const pos = new T.Vector3(lie.x, lie.y + 1.2, lie.z).addScaledVector(d, .5), v = d.clone().multiplyScalar(speed0);let bank = hyzer * .55;
      for (let i = 0; i < 12 * 240; i++) {
        const dt = DT / 4, speed = v.length();
        if (speed > .3) {
          const k = .0135, lift = .085 * speed * speed * .195 * (putt ? .55 : 1), up = new T.Vector3(0, 1, 0), dir = v.clone().normalize(), side = new T.Vector3().crossVectors(dir, up).normalize();
          bank += ((speed > 18 ? -.22 : 0) + (speed < 15 ? .55 * (1 - speed / 15) : 0)) * dt * (putt ? .3 : 1);
          const ld = up.clone().multiplyScalar(Math.cos(bank)).addScaledVector(side, Math.sin(bank));
          v.addScaledVector(dir, -k * speed * speed * dt).addScaledVector(ld, lift * dt).y -= 9.81 * dt;
        }
        pos.addScaledVector(v, dt);
        const rb = Math.hypot(pos.x - h.basket.x, pos.z - h.basket.z), by = pos.y - h.basket.y;
        if (rb < .33 && by > .55 && by < 1.4 && speed < 13) return {in: true, x: pos.x, z: pos.z};
        if (pos.y < heightAt(pos.x, pos.z) + .015) return {in: false, x: pos.x, z: pos.z};
      }
      return {in: false, x: pos.x, z: pos.z};
    };
    const choose = () => {
      const h = g.holes[g.hole], lie = g.lie, dist = Math.hypot(h.basket.x - lie.x, h.basket.z - lie.z), putt = dist < 11;
      const yaw = Math.atan2(-(h.basket.x - lie.x), -(h.basket.z - lie.z));
      const hyzer = throws === 1 ? .35 : throws === 4 ? -.3 : 0, drunk = throws === 2 ? .25 : 0;
      let best = null;
      for (const pitch of putt ? [-.06, -.03, 0, .03, .06, .1] : [.02, .06, .1]) for (let f = 3; f <= 72; f++) {
        const power = Math.min(1, f * DT / 1.2), p = predict(lie, yaw, pitch, power, putt, hyzer, h), miss = Math.hypot(p.x - h.basket.x, p.z - h.basket.z);
        const score = p.in ? -1 : miss;if (!best || score < best.score) best = {score, frames: f, pitch};
        if (p.in) break;
      }
      throws++;
      return {yaw, pitch: best.pitch, frames: best.frames, hyzer, drunk};
    };
    // The main loop for one scenario: the driver plans a throw whenever the disc is idle (null: press circle).
    const play = (sc, choose, limit) => {
      const ctl = controlLog();let c = {held: false, yaw: sc.startYaw, pitch: 0, alt: false, hyzer: 0, cancel: false, drunk: 0}, plan = null, done = null, lastMsg = null;sc.steps = [];sc.lies = [];
      for (; step < limit && !done;) {
        const D = g.disc, busy = D.flying || (disc.visible && !D.rest && D.vel.lengthSq() > 0) || g.pending;
        if (!busy && !plan) {plan = choose();if (!plan) c.cancel = true;}
        if (plan) {c.yaw = plan.yaw;c.pitch = plan.pitch;c.hyzer = plan.hyzer;c.drunk = plan.drunk;if (plan.frames > 0) {c.held = true;plan.frames--;} else {c.held = false;plan = null;}}
        ctl.put(c);
        const cv = ctlValue(c), input = mainInput(g, c.held, {yaw: cv.yaw, pitch: cv.pitch, alt: false, hyzer: cv.hyzer, cancel: c.cancel, drunk: cv.drunk});
        const r = g.update(DT, input, null);
        if (r && r.teleport) c.yaw = r.teleport.yaw;
        if (r && r.done) done = r.done;
        sc.steps.push({
          r: res(r), pos: A([D.pos.x, D.pos.y, D.pos.z]), vel: A([D.vel.x, D.vel.y, D.vel.z]), bank: n12(D.bank), time: n12(D.time || 0), fl: D.flying ? 1 : 0, rest: D.rest ? 1 : 0,
          hole: g.hole, strokes: g.strokes.slice(), lie: g.lie ? A([g.lie.x, g.lie.y, g.lie.z]) : null, msg: g.message === lastMsg ? undefined : (lastMsg = g.message), pend: g.pending ? [g.pending.next ? 1 : 0, n12(g.pending.t)] : null,
          disc: [disc.visible ? 1 : 0, ...A([disc.position.x, disc.position.y, disc.position.z, disc.rotation.x, disc.rotation.y, disc.rotation.z])], charge: n12(g.charge || 0),
          hud: hudDg ? {hole: hudDg.hole, holes: hudDg.holes, par: hudDg.par, strokes: hudDg.strokes, distance: hudDg.distance, charge: n12(hudDg.charge), message: hudDg.message === g.message ? undefined : hudDg.message} : null, active: g.active ? 1 : 0
        });
        step++;nowMs += DT * 1000;
      }
      sc.controls = ctl.list;sc.sfx = sfx.slice();sc.done = done;sc.randomCalls = gameCalls;
    };
    out.startYaw = r0.yaw;play(out, choose, 20000);
    // Extra throws for branches a normal round may not reach: a fast disc into the basket (chains bounce, pole), trees.
    {
      const ex = out.extra = {seed: 31337};seed = ex.seed;gameCalls = 0;sfx.length = 0;step = 0;nowMs = 5000;
      const h = g.holes[0], rs = g.start({x: h.tee.x, z: h.tee.z, y: h.tee.y});ex.startYaw = rs.yaw;
      const ux = (h.tee.x - h.basket.x) / h.length, uz = (h.tee.z - h.basket.z) / h.length;
      const trees = W.trees.map(t => {const w = W.toWorld(t.x, t.s);return {x: w.x, z: w.z, h: t.h || 11};}).sort((p, q) => Math.hypot(p.x - h.basket.x, p.z - h.basket.z) - Math.hypot(q.x - h.basket.x, q.z - h.basket.z));
      const tr = trees[0], ty = heightAt(tr.x, tr.z);
      const at = (x, z) => ({x, z, y: heightAt(x, z)});
      const setups = [
        ...[0, .2, -.2, .1, -.1, 0].map(s => ({lie: at(h.basket.x + ux * 11.5 - uz * s, h.basket.z + uz * 11.5 + ux * s), to: {x: h.basket.x, y: h.basket.y + .95, z: h.basket.z}, frames: 72})),
        {lie: at(h.basket.x + ux * 4, h.basket.z + uz * 4), to: {x: h.basket.x, y: h.basket.y + .3, z: h.basket.z}, frames: 72},
        {lie: at(h.basket.x + ux * 3, h.basket.z + uz * 3), to: {x: h.basket.x, y: h.basket.y + .3, z: h.basket.z}, frames: 72},
        {lie: at(tr.x + 6, tr.z + 2), to: {x: tr.x, y: ty + 1.4, z: tr.z}, frames: 36},
        {lie: at(tr.x - 7, tr.z + 1), to: {x: tr.x, y: ty + tr.h * .62, z: tr.z}, frames: 48},
        {lie: at(tr.x + 1, tr.z - 7), to: {x: tr.x, y: ty + tr.h * .6, z: tr.z}, frames: 60}
      ];
      let k = 0;
      play(ex, () => {
        const su = setups[k++];if (!su) return null;
        g.lie = su.lie;ex.lies.push([step, ...A([su.lie.x, su.lie.y, su.lie.z])]);
        const dx = su.to.x - su.lie.x, dz = su.to.z - su.lie.z, dist = Math.hypot(dx, dz), putt = Math.hypot(h.basket.x - su.lie.x, h.basket.z - su.lie.z) < 11;
        return {yaw: Math.atan2(-dx, -dz), pitch: Math.atan2(su.to.y - (su.lie.y + 1.2), dist) - (putt ? .06 : .09), frames: su.frames, hyzer: 0, drunk: 0};
      }, 8000);
      ex.tree = A([tr.x, tr.z, tr.h]);
      log.push(`disc golf extra: ${ex.steps.length} steps, sounds ${ex.sfx.map(x => x[1] + (x[2] !== 1 ? '/' + x[2] : '')).join(' ')}, done: ${ex.done}`);
    }
    // Cancel: start again at hole 2, throw nothing, press circle.
    {
      seed = 777;sfx.length = 0;const h1 = g.holes[1], r = g.start({x: h1.tee.x, z: h1.tee.z, y: h1.tee.y});
      const rc = g.update(DT, {aimYaw: 0, aimPitch: 0, charge: 0, power: 0, release: false, alt: false, hyzer: 0, cancel: true, drunk: 0}, null), after = g.update(DT, {cancel: false}, null);
      out.cancel = {start: A([r.x, r.z, r.yaw]), r: res(rc), after: after === null ? null : res(after), active: g.active};
    }
    log.push(`disc golf: ${out.steps.length} steps, ${throws} throws, ${out.sfx.length} sounds, ${out.randomCalls} random numbers, done: ${out.done}`);
    return out;
  }

  // ======================= PETANQUE =======================
  function petanqueRun() {
    const g = mini.petanque, grp = g.group;
    seed = 4242;gameCalls = 0;sfx.length = 0;step = 0;nowMs = 5000;hudPq = null;
    const P = window.SURROUNDINGS.petanque, centre = W.toWorld(P.x, P.s), a1 = W.toWorld(P.x + Math.cos(P.rot), P.s + Math.sin(P.rot)), along = {x: a1.x - centre.x, z: a1.z - centre.z}, across = {x: -along.z, z: along.x};
    const courtPoint = (u, v) => ({x: centre.x + along.x * u + across.x * v, z: centre.z + along.z * u + across.z * v});
    const npc = grp.children.find(o => o.isGroup), armR = npc.children.filter(o => o.isGroup)[1];
    const out = {seed, near: [], steps: []};
    for (const [u, v] of [[0, 0], [7.4, 1.9], [10.4, 0], [0, 4.9], [0, 5.1], [-10.6, 0], [3, -4]]) {const p = courtPoint(u, v);out.near.push([A([p.x, 0, p.z]), g.near({x: p.x, y: 0, z: p.z})]);}
    const sp = courtPoint(-8, 1), r0 = g.start({x: sp.x, z: sp.z, y: 0});
    out.start = {p: A([sp.x, 0, sp.z]), r: A([r0.x, r0.z, r0.yaw])};out.nearActive = g.near({x: centre.x, y: 0, z: centre.z});
    const balls = () => grp.children.filter(o => o.isMesh && o.geometry.type === 'SphereGeometry' && (Math.abs(o.geometry.parameters.radius - .015) < 1e-9 || Math.abs(o.geometry.parameters.radius - .037) < 1e-9)).slice(-7);
    const ctl = controlLog();
    let c = {held: false, yaw: r0.yaw, pitch: 0, alt: false, hyzer: 0, cancel: false, drunk: 0}, plan = null, lastR = null, throws = 0, done = null, wonAt = -1, prevKey = '';
    const yawFor = (du, dv) => {const fx = along.x * du + across.x * dv, fz = along.z * du + across.z * dv;return Math.atan2(-fx, -fz);};
    const choose = () => {
      const d = g.debug(), circle = d.circle, jack = d.jack;throws++;
      if (d.aimingFor === 'jack') {
        const dir = circle.u < 0 ? 1 : -1, want = throws === 1 ? 4.2 : throws === 3 ? 7.5 : 6.8 + drand() * 2.2, side = throws === 5 ? .45 : (drand() - .5) * .08;
        return {yaw: yawFor(dir, side), frames: Math.round((want - 1.5) / 11 * 1.2 / DT), alt: false, drunk: 0};
      }
      const du = jack.u - circle.u, dv = jack.v - circle.v, dj = Math.hypot(du, dv);
      // Shoot Arne's best boule now and then; one boule thrown far to the side (out of the court).
      const npcBest = d.boules.npc.filter(b => b.alive).map(b => ({b, d: Math.hypot(b.u - jack.u, b.v - jack.v)})).sort((x, y) => x.d - y.d)[0];
      if (throws === 7) return {yaw: yawFor(du / dj * .3, 1), frames: 30, alt: false, drunk: 0};
      if (npcBest && npcBest.d < .5 && hudPq && hudPq.hold === 'npc' && hudPq.you >= 2 && drand() < .5) {
        const bu = npcBest.b.u - circle.u, bv = npcBest.b.v - circle.v, bd = Math.hypot(bu, bv);
        return {yaw: yawFor(bu / bd, bv / bd), frames: Math.max(2, Math.round(Math.min(1, (bd - 1.5) / 11) * 1.2 / DT)), alt: true, drunk: 0};
      }
      const want = dj + .31 + (drand() - .5) * .3;
      return {yaw: yawFor(du / dj, dv / dj), frames: Math.max(2, Math.round(Math.min(1, (want - 1.5) / 11) * 1.2 / DT)), alt: false, drunk: throws % 5 === 0 ? .4 : 0};
    };
    for (; step < 60000 && !done;) {
      if (lastR && lastR.stand && !plan && wonAt < 0) plan = choose();
      if (plan) {c.yaw = plan.yaw;c.pitch = -.12;c.alt = plan.alt;c.drunk = plan.drunk;if (plan.frames > 0) {c.held = true;plan.frames--;} else {c.held = false;plan = null;}}
      else if (c.alt && !c.held) c.alt = false;
      // After the win: wait a while to see what the game does, then quit with circle.
      c.cancel = wonAt >= 0 && step - wonAt > 150;
      ctl.put(c);
      const cv = ctlValue(c), input = mainInput(g, c.held, {yaw: cv.yaw, pitch: cv.pitch, alt: c.alt, hyzer: 0, cancel: c.cancel, drunk: cv.drunk});
      const r = g.update(DT, input, null);
      lastR = r;
      if (r && r.face !== undefined) {c.yaw = r.face;c.pitch = -.12;}
      if (r && r.done) done = r.done;
      if (wonAt < 0 && /wins? \d/.test(g.message)) wonAt = step;
      const d = g.debug(), bl = balls();
      const key = [d.phase, d.current, d.aimingFor, g.message, g.mene, g.score.you, g.score.npc].join('|');
      const event = key !== prevKey || (sfx.length && sfx[sfx.length - 1][0] === step) || (r && (r.face !== undefined || r.done || !r.stand !== !(out.steps.length && out.steps[out.steps.length - 1].stand)));
      prevKey = key;
      const rec = {s: step, r: res(r)};
      if (event || step % 12 === 0 || done) {
        rec.full = 1;
        rec.balls = bl.map(m => [m.visible ? 1 : 0, ...A([m.position.x, m.position.y, m.position.z])]);
        rec.dbg = {phase: d.phase, current: d.current, aim: d.aimingFor, circle: d.circle ? A([d.circle.u, d.circle.v]) : null, jack: d.jack ? [n12(d.jack.u), n12(d.jack.v), d.jack.alive ? 1 : 0, d.jack.dead ? 1 : 0] : null,
          you: d.boules ? d.boules.you.map(b => [b.u, b.v, b.alive ? 1 : 0, b.dead ? 1 : 0]) : null, npc: d.boules ? d.boules.npc.map(b => [b.u, b.v, b.alive ? 1 : 0, b.dead ? 1 : 0]) : null};
        rec.score = [g.score.you, g.score.npc];rec.mene = g.mene;rec.msg = g.message;rec.turnId = g.turnId || 0;rec.faced = g.faced === undefined ? -1 : g.faced;
        rec.finish = g.finish === undefined ? null : n12(g.finish);rec.nextFirst = g.nextFirst || null;
        rec.npc = A([npc.position.x, npc.position.y, npc.position.z, npc.rotation.y, armR.rotation.x]);
        rec.hud = hudPq && g.active ? {score: [hudPq.score.you, hudPq.score.npc], mene: hudPq.mene, you: hudPq.you, npc: hudPq.npc, turn: hudPq.turn, aim: hudPq.aim, hold: hudPq.hold, margin: n12(hudPq.margin), message: hudPq.message === g.message ? undefined : hudPq.message, charge: n12(hudPq.charge), shoot: !!hudPq.shoot,
          markers: hudPq.markers.map(m => [m.label, m.owner, m.best ? 1 : 0])} : null;
      }
      rec.stand = r && r.stand ? 1 : 0;
      out.steps.push(rec);
      step++;nowMs += DT * 1000;
    }
    out.controls = ctl.list;out.sfx = sfx.slice();out.done = done;out.wonAt = wonAt;out.randomCalls = gameCalls;out.throws = throws;
    out.afterStop = {npc: A([npc.position.x, npc.position.y, npc.position.z]), active: g.active, visible: balls().map(m => m.visible ? 1 : 0)};
    log.push(`petanque: ${out.steps.length} steps, ${throws} player throws, mènes ${g.mene}, score ${g.score.you}-${g.score.npc}, won at step ${wonAt}, ${out.sfx.length} sounds, ${gameCalls} random numbers, done: ${done}`);
    return out;
  }

  // ======================= FIRE =======================
  function fireRun() {
    seed = 1234567;gameCalls = 0;
    const F = window.GameFire.create({T, lib, heightAt: W.heightWorld, x: -2.7, z: 34.7}), grp = F.group;
    const kids = grp.children.slice(), flames = kids.filter(o => o.isMesh && o.material && o.material.type === 'ShaderMaterial'), points = kids.filter(o => o.isPoints), embers = points[0], smoke = points[1], light = kids.find(o => o.isPointLight);
    const charredMat = kids.find(o => o.isMesh && o.geometry.type === 'CylinderGeometry' && Math.abs(o.geometry.parameters.radiusTop - .045) < 1e-9).material;
    const out = {layout: {center: A([F.center.x, F.center.y, F.center.z]), spots: F.spots.map(s => [s.id, ...A([s.x, s.y, s.z, s.yaw])]), woodpile: A([F.woodpile.x, F.woodpile.z]), crate: A([F.crate.x, F.crate.z]),
      obstacles: F.obstacles.map(o => ({points: o.points.map(p => A([p.x, p.z])), minY: n12(o.minY), maxY: n12(o.maxY), kind: o.kind})), DECAY_S: F.DECAY_S, MAX: F.MAX}, seed, steps: [], throws: []};
    out.initial = {flames: flames.map(f => [n12(f.userData.k), ...A(f.userData.off), n12(f.material.uniforms.time.value)])};
    const level = i => i < 60 ? 0 : i < 150 ? 2 : i < 240 ? 4.5 : i < 330 ? 6 : i < 380 ? 7.5 : i < 420 ? -1 : i < 470 ? .05 : i < 560 ? 1 : i < 700 ? 3 + Math.sin(i * .05) * 2 : 5.5 - (i - 700) / 60;
    const throwsAt = {30: {x: -1, y: -8.3, z: 37}, 200: {x: 0, z: 37.5}, 205: {x: -4.5, y: -8.4, z: 33}, 206: {x: -2, y: -8, z: 30}, 600: {x: 1, y: -8.5, z: 34}};
    const logs = () => grp.children.filter(o => o.isMesh && o.geometry.type === 'CylinderGeometry' && Math.abs(o.geometry.parameters.radiusTop - .07) < 1e-9);
    for (let i = 0; i < 900; i++) {
      if (throwsAt[i]) {const f = throwsAt[i];F.throwLog(f);out.throws.push([i, ...A([f.x, f.y === undefined ? 0 : f.y, f.z]), f.y === undefined ? 0 : 1]);}
      const a = i * .013, cam = {position: {x: -2.7 + Math.cos(a) * 9, y: -8, z: 34.7 + Math.sin(a) * 9}}, L = level(i);
      F.update(DT, L, cam);
      const rec = {level: n12(L), cam: A([cam.position.x, cam.position.y, cam.position.z]), light: A([light.intensity, light.distance]), charred: n12(charredMat.emissiveIntensity),
        flames: flames.map(f => [f.visible ? 1 : 0, ...A([f.scale.x, f.scale.y, f.position.x, f.position.y, f.position.z, f.rotation.y, f.material.uniforms.time.value, f.material.uniforms.power.value])]),
        embers: [embers.visible ? 1 : 0, n12(embers.material.size)], smoke: n12(smoke.material.opacity), logs: logs().map(l => A([l.position.x, l.position.y, l.position.z, l.rotation.x, l.rotation.y, l.rotation.z]))};
      if (i % 15 === 0 || i === 899) {rec.ep = A(embers.geometry.attributes.position.array);rec.sp = A(smoke.geometry.attributes.position.array);}
      out.steps.push(rec);
    }
    out.randomCalls = gameCalls;
    log.push(`fire: ${out.steps.length} steps, ${out.throws.length} logs thrown, ${gameCalls} random numbers`);
    return out;
  }

  // Fire level shared by the lobby (game-multiplayer.js, offline: Date.now()).
  function fireLevelRun() {
    const mp = G.mp, t0 = realDate() + 1e9;let fake = t0;Date.now = () => fake;
    const ops = [];
    const at = (dt, op) => {fake = t0 + dt;if (op === 'add') mp.addWood();ops.push([op, dt, n12(mp.fireLevel())]);};
    at(0, 'level');at(0, 'add');at(1000, 'add');at(2000, 'add');at(45000, 'level');at(60000, 'add');for (let k = 0; k < 6; k++) at(61000 + k * 500, 'add');
    for (let s = 0; s <= 700; s += 25) at(64000 + s * 1000, 'level');
    at(800000, 'add');at(800000 + 90000, 'level');at(800000 + 89999, 'level');
    Date.now = realDate;
    log.push(`fire level: ${ops.length} operations`);
    return {t0: n12(t0), ops, decayS: mp && 90, max: 6};
  }

  try {
    const result = {dt: DT, uuidLine, discgolf: discGolfRun(), petanque: petanqueRun(), fire: fireRun(), fireLevel: fireLevelRun()};
    log.push(`three.js UUID calls kept off the game generator: ${uuidCalls} (source line ${uuidLine})`);
    files.minigames = {json: JSON.stringify(result), bin: null};
  } finally {
    Math.random = realRandom;performance.now = realPerf;Date.now = realDate;
    delete performance.now;
    window.__hkDone = true;
  }
})();
