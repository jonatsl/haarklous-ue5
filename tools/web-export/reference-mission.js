/* Runs inside haarklous-game.html (node tools/web-export/export.mjs --only=tests --script=reference-mission.js):
 * reference answers from the web's own case-game.js for the C++ hostage-rescue port, written to
 * unreal/Haarklous/Content/Data/tests/mission.json and read by Private/Tests/HKMissionTests.cpp.
 *   - pure helpers: createRng, planCase (with the web test file's own samplers), hostileHitChance, applyHit,
 *     damagePlayer, outcome, insidePolygon
 *   - the real spawn sampler of the apartment and stairwell: plans for many seeds, explain() and probeArea()
 *   - scripted missions, replayable frame by frame: Math.random is a seeded generator, requestAnimationFrame and
 *     setTimeout are driven by this script at 64 fps, the game's own frame loop is held. Per frame it records the camera
 *     pose, the stance and every call into the mission (start, padButton, key, triggers, mouse ...), every raycast the
 *     mission makes (with the answer of the world geometry alone, which the C++ code gets from Unreal line traces), and
 *     the state after the frame (AI, health, ammunition, timers, HUD values, the viewmodel pose, flash lights).
 * Everything is put back afterwards (the mission is stopped, the player's pose restored, the frame loop resumes).
 */
(() => {
  'use strict';
  const G = window.haarklousGame, T = window.THREE, CG = window.CaseGame, cg = G.caseGame, WC = window.WalkCollision;
  const files = window.__hkFiles, log = window.__hkLog;
  const BIG = 1e9, num = v => v === Infinity ? BIG : v === -Infinity ? -BIG : v;
  const q = v => typeof v === 'number' && Number.isFinite(v) ? +v.toPrecision(12) : v;
  const pts = list => list.map(p => [p.x, p.z]);
  const planOut = p => ({hostileCount: p.hostileCount, hasHostage: p.hasHostage, actors: p.actors.map(a => [a.id, a.role, a.zone, a.x, a.z, a.y, a.area ?? '', a.facing, a.health])});
  const obstacleOut = o => ({points: pts(o.points), minY: num(o.minY ?? -Infinity), maxY: num(o.maxY ?? Infinity)});
  const out = {version: 1};

  // ---------- Pure helpers ----------
  out.rng = [0, 1, 2, 7, 42, 43, 4294967295, 2147483648, 123456789].map(seed => {const r = CG.createRng(seed);return [seed, Array.from({length: 24}, () => r())];});
  const zoned = (rng, zone) => zone === 'stair' ? {x: rng() * 2.6, z: 5 + rng() * 2, y: -2.85 * Math.floor(rng() * 4), area: 'Stair'} : {x: rng() * 6, z: rng() * 10, y: 0, area: 'Room'};
  const flat = rng => ({x: rng() * 6, z: rng() * 10, area: 'Room'});
  out.zonedPlans = [];for (let seed = 1; seed <= 300; seed++) out.zonedPlans.push(planOut(CG.planCase(CG.createRng(seed), zoned)));
  out.flatPlans = [];for (let seed = 1; seed <= 60; seed++) out.flatPlans.push(planOut(CG.planCase(CG.createRng(seed), flat)));
  out.optionPlans = [[7, 2, 1, 2, 600], [8, .5, 6, 8, 600], [9, 1.1, 2, 2, 50]].map(([seed, minSeparation, h0, h1, maxTries]) =>
    ({seed, minSeparation, hostiles: [h0, h1], maxTries, plan: planOut(CG.planCase(CG.createRng(seed), zoned, {minSeparation, hostiles: [h0, h1], maxTries}))}));
  try {CG.planCase(CG.createRng(1), () => null, {maxTries: 20});out.planError = '';} catch (e) {out.planError = e.message;}
  out.hitChance = [];
  for (const d of [0, .5, 1, 1.5, 2, 2.75, 3, 4.25, 6, 8, 9, 12, 15.4, 20, 40]) for (const s of [0, -1, .5, 1.6, 3.2, 4, 10]) for (const st of ['stand', 'crouch', 'prone', 'swim']) out.hitChance.push([d, s, st, CG.hostileHitChance(d, s, st)]);
  out.hitChanceDefault = [0, 1.5, 5, 30].map(d => [d, CG.hostileHitChance(d)]);
  out.applyHit = [];
  for (const [health, alive] of [[2, true], [1, true], [0, false], [2, false], [3, true]]) for (const head of [false, true]) {const a = {health, alive};CG.applyHit(a, head);out.applyHit.push([health, alive, head, a.health, a.alive]);}
  out.damage = [];
  for (const [health, alive] of [[100, true], [25, true], [10, true], [0, false], [50, false]]) for (const amount of [undefined, 25, 40, 0]) {const p = {health, alive};CG.damagePlayer(p, amount);out.damage.push([health, alive, amount ?? null, p.health, p.alive]);}
  {
    const cases = [[[['hostile', 1], ['hostile', 1], ['hostage', 1]], null], [[['hostile', 0], ['hostile', 1], ['hostage', 1]], null], [[['hostile', 0], ['hostile', 0], ['hostage', 1]], null],
      [[['hostile', 0], ['hostile', 0], ['hostage', 0]], null], [[['hostile', 1], ['hostage', 0]], null], [[['hostile', 1]], [100, 1]], [[['hostile', 1]], [0, 0]], [[['hostile', 0]], [0, 0]], [[['hostile', 0]], [25, 1]], [[['hostage', 1]], null], [[], null]];
    out.outcome = cases.map(([list, player]) => [list, player, CG.outcome(list.map(([role, alive]) => ({role, alive: !!alive})), player ? {health: player[0], alive: !!player[1]} : undefined)]);
  }
  {
    const L = [{x: 0, z: 0}, {x: 4, z: 0}, {x: 4, z: 1}, {x: 1, z: 1}, {x: 1, z: 4}, {x: 0, z: 4}];let s = 99;const r = () => (s = (s * 16807) % 2147483647) / 2147483647;
    out.inside = {polygon: pts(L), tests: Array.from({length: 200}, () => {const p = {x: r() * 5 - .5, z: r() * 5 - .5};return [p.x, p.z, CG.insidePolygon(p, L)];})};
  }

  // ---------- The real spawn sampler (apartment rooms, balcony, stairwell landings) ----------
  const ww = G.walkWorld, own = G.towers[0].stair;
  const entranceDoor = G.apt.doors.find(d => d.name === 'Entrance door'), entrance = entranceDoor ? entranceDoor.leaf.getWorldPosition(new T.Vector3()) : {x: 1.66, y: 0, z: 5.83};
  out.sampler = {
    spawnAreas: G.apt.spawnAreas.map(a => ({name: a.name, points: pts(a.points), ceilingHeight: num(a.ceilingHeight ?? Infinity)})),
    stairSpawns: own.floors().filter(f => !f.slope && f.name !== 'Street door threshold' && !/Half landing 0/.test(f.name)).map(f => ({name: f.name, points: pts(f.points), y: f.y})),
    entrance: [entrance.x, entrance.y, entrance.z], doors: G.apt.doors.map(d => d.name), dynamic: (ww.dynamic || []).map(obstacleOut)
  };
  out.samplePlans = cg.debug.samplePlans(100).map(planOut);
  {
    const xs = G.apt.floors.flatMap(f => f.points.map(p => p.x)), zs = G.apt.floors.flatMap(f => f.points.map(p => p.z));
    const x0 = Math.min(...xs), x1 = Math.max(...xs), z0 = Math.min(...zs), z1 = Math.max(...zs);out.explain = [];
    for (let i = 0; i <= 24; i++) for (let j = 0; j <= 24; j++) {const x = x0 + (x1 - x0) * i / 24, z = z0 + (z1 - z0) * j / 24, e = cg.debug.explain(x, z);out.explain.push([x, z, e.entrance, e.door, e.stand]);}
  }
  out.probe = G.apt.spawnAreas.map(a => [a.name, cg.debug.probeArea(a.name, 300)]);

  // ---------- Scripted missions ----------
  const P = G.player, keepPose = ['x', 'z', 'feet', 'yaw', 'pitch', 'stance', 'eye', 'eyeNow', 'jumpY', 'jumpV', 'run', 'mode'], savedPose = {};
  keepPose.forEach(k => {savedPose[k] = P[k];});
  const saved = {random: Math.random, raf: window.requestAnimationFrame, caf: window.cancelAnimationFrame, st: window.setTimeout, ct: window.clearTimeout,
    ac: window.AudioContext, wac: window.webkitAudioContext, ri: T.Raycaster.prototype.intersectObjects, sd: WC.setDynamic, umw: T.Object3D.prototype.updateMatrixWorld,
    cam: {p: G.camera.position.clone(), q: G.camera.quaternion.clone(), fov: G.camera.fov}};
  let rafQ = new Map(), rafId = 1, cancelled = new Set(), timers = [], timerId = 1, fakeNow = 1000, now = 1000, rays = [], inner = false, dynLog = [];
  window.requestAnimationFrame = cb => {const id = rafId++;rafQ.set(id, cb);return id;};
  window.cancelAnimationFrame = id => {rafQ.delete(id);cancelled.add(id);};
  window.setTimeout = (fn, ms = 0) => {const id = timerId++;timers.push({id, due: fakeNow + (+ms || 0), fn});return id;};
  window.clearTimeout = id => {timers = timers.filter(t => t.id !== id);};
  // case-game.js makes its sounds from Math.random noise: no audio while recording, so only the game logic draws numbers.
  window.AudioContext = function () {throw new Error('no audio while recording');};window.webkitAudioContext = undefined;
  // Every raycast made by case-game.js: the full answer and the answer of the world geometry alone. The game's frame loop
  // is held, so the only other raycast is game-main.js's door lookup inside the mission's □ (useDoorAhead), skipped here.
  let lookingForDoor = false;
  T.Raycaster.prototype.intersectObjects = function (objects, recursive = true, target = []) {
    const res = saved.ri.call(this, objects, recursive, target);
    if (!inner && !lookingForDoor) {
      inner = true;
      try {
        const world = objects.filter(o => !o.userData.actorId), wr = world.length === objects.length ? res : saved.ri.call(this, world, recursive, []), w = wr[0], f = res[0];
        rays.push({o: this.ray.origin.toArray(), d: this.ray.direction.toArray(), far: this.far, n: objects.length - world.length,
          w: w ? [w.distance, ...w.point.toArray(), ...(w.face ? w.face.normal.clone().transformDirection(w.object.matrixWorld).toArray() : [0, 1, 0])] : null,
          f: f ? [f.object.userData.actorId || '', f.object.userData.head ? 1 : 0, f.distance] : null});
      } finally {inner = false;}
    }
    return res;
  };
  WC.setDynamic = function (world, list) {if (world === ww) dynLog.push(list.map(obstacleOut));return saved.sd.apply(this, arguments);};
  const runTimers = () => {for (;;) {const due = timers.filter(t => t.due <= fakeNow).sort((a, b) => a.due - b.due || a.id - b.id);if (!due.length) return;const t = due[0];timers = timers.filter(x => x !== t);t.fn();}};
  const $ = id => document.getElementById(id);
  const lights = G.scene.children.filter(o => o.isPointLight), flashLight = lights.find(l => l.color.getHex() === 0xffd28a && l.distance === 7), enemyLight = lights.find(l => l.color.getHex() === 0xffc070 && l.distance === 6);
  let gun = null, slide = null, flash = null;
  function captureGun() {
    let vm = null;
    T.Object3D.prototype.updateMatrixWorld = function (force) {if (this.isScene && this !== G.scene && !vm && this.children.some(c => c.isHemisphereLight) && this.children.some(c => c.isGroup)) vm = this;return saved.umw.call(this, force);};
    try {cg.renderOverlay(G.camera);} finally {T.Object3D.prototype.updateMatrixWorld = saved.umw;}
    if (vm) {gun = vm.children.find(c => c.isGroup);slide = gun.children.find(c => c.isGroup);flash = gun.children.find(c => c.isMesh && c.geometry.type === 'ConeGeometry');}
  }
  const style = (id, k) => $(id).style.getPropertyValue(k);
  function snap() {
    const p = cg.debug.player, s = {st: cg.state, act: cg.active ? 1 : 0, hp: p.health, alive: p.alive ? 1 : 0};
    s.hud = [$('caseHudId').textContent, $('caseHudObjective').textContent, $('caseHudTime').textContent, $('caseMag').textContent, $('caseReserve').textContent, $('caseReload').textContent,
      $('caseFeed').textContent, $('caseFeed').className, $('caseHealth').textContent];
    s.hn = [parseFloat(style('caseCrosshair', '--gap') || '16'),parseFloat($('caseCrosshair').style.opacity || '1'), parseFloat($('caseHitMarker').style.opacity || '0'), parseFloat($('caseDamage').style.opacity || '0'),
      parseFloat($('caseDamageDir').style.opacity || '0'), (() => {const m = /rotate\(([-0-9.e+]+)rad\)/.exec($('caseDamageDir').style.transform);return m ? parseFloat(m[1]) : 0;})()].map(q);
    s.pan = $('casePanel').hidden ? '' : (document.getElementById('game').dataset.casePanel || '?');
    s.sens = [...document.querySelectorAll('#casePanel [data-sens-out]')].map(o => o.textContent).join('|');
    s.fov = G.camera.fov;
    s.gun = gun ? [gun.position.x, gun.position.y, gun.position.z, gun.rotation.x, gun.rotation.y, gun.rotation.z, slide.position.z, flash.visible ? 1 : 0, flash.rotation.z].map(q) : null;
    s.fl = [flashLight.intensity, ...flashLight.position.toArray(), enemyLight.intensity, ...enemyLight.position.toArray()].map(q);
    s.a = [...cg.debug.actors.values()].map(a => [a.id, a.alive ? 1 : 0, a.health, a.root.rotation.y, a.fall, a.hitFlash, a.flashT, a.engaged ? 1 : 0, a.fireIn, a.sees ? 1 : 0,
      a.body.position.y, a.body.position.z, a.body.rotation.x, a.muzzleFlash ? (a.muzzleFlash.visible ? 1 : 0) : -1, a.muzzleFlash ? a.muzzleFlash.rotation.y : 0].map(q));
    return s;
  }
  // One frame, in the order the C++ port runs it: inputs (with the camera of the previous frame update), the render
  // (actor world matrices), the mission tick, then due timers.
  let rec = null, lastPlan = null;
  function frame(script, dt = 15.625) {
    const due = [...rafQ.entries()];rafQ.clear();cancelled = new Set();
    const calls = [], doors = [];
    const api = {
      call(name, ...args) {
        const before = G.doors.map(d => !!d.anim);
        lookingForDoor = name === 'padButton' && args[0] === 'square';
        let r;
        try {r = cg[name](...args);} finally {lookingForDoor = false;}
        if (name === 'padButton' && args[0] === 'square') doors.push(G.doors.some((d, i) => !!d.anim !== before[i]));
        calls.push([name, args, r === undefined ? null : r]);return r;
      },
      invulnerable(v) {cg.debug.invulnerable = v;calls.push(['invulnerable', [v], null]);}
    };
    if (script) script(api);
    G.camera.updateMatrixWorld();
    const f = {dt, cam: [...G.camera.position.toArray(), ...G.camera.quaternion.toArray()], stance: P.stance, calls};
    if (doors.length) f.doors = doors;
    cg.debug.actors.forEach(a => a.root.updateMatrixWorld(true));
    now += dt;fakeNow = now;
    let ticked = 0;
    for (const [id, cb] of due) if (!cancelled.has(id)) {cb(now);ticked = 1;}
    runTimers();
    f.ticked = ticked;f.rays = rays.splice(0);f.s = snap();
    if (cg.debug.plan && cg.debug.plan !== lastPlan) {lastPlan = cg.debug.plan;f.plan = planOut(lastPlan);f.planDynamic = dynLog.length >= 2 ? dynLog[dynLog.length - 2] : null;}
    dynLog.length = 0;
    rec.frames.push(f);
    return f;
  }
  const wait = (n, script) => {for (let i = 0; i < n; i++) frame(script);};
  // Camera helpers (world geometry tests here are this script's own, not recorded).
  const worldMeshes = [];[G.apt.group, own.group].forEach(g => g.traverse(o => {if (o.isMesh && !o.material.transparent && o.visible) worldMeshes.push(o);}));
  const myRay = new T.Raycaster();
  function clear(a, b) {const d = b.clone().sub(a), L = d.length();myRay.set(a, d.normalize());myRay.far = L;const h = saved.ri.call(myRay, worldMeshes, false, [])[0];return !h || h.distance > L - .3;}
  const limbAt = (a, head) => {const m = a.meshes.find(x => !!x.userData.head === head) || a.meshes[0], e = m.matrixWorld.elements;return new T.Vector3(e[12], e[13], e[14]);};
  const torso = a => {const m = a.meshes[a.role === 'hostile' ? 2 : 4], e = m.matrixWorld.elements;return new T.Vector3(e[12], e[13], e[14]);};
  // Places to stand with a clear view of the actor's head and torso (nearest first; the actor's own facing first).
  function spotsNear(a, eyeH = 1.65) {
    const r = a.root.position, y = r.y, f = a.facing, list = [], aims = [limbAt(a, true), torso(a)];
    for (let dist = 1.2; dist < 6.5; dist += .35) for (let k = 0; k < 24; k++) {
      const ang = f + (k % 2 ? 1 : -1) * Math.ceil(k / 2) * Math.PI / 12, p = {x: r.x - Math.sin(ang) * dist, z: r.z - Math.cos(ang) * dist};
      if (!WC.canStand(ww, p, 1.65, y)) continue;
      const eye = new T.Vector3(p.x, y + eyeH, p.z);
      if (aims.every(t => clear(eye, t))) list.push(eye);
    }
    // Closed rooms (doors stay shut in these scripts): any point with a clear view, even inside furniture.
    for (let dist = .8; dist < 3 && !list.length; dist += .2) for (let k = 0; k < 24; k++) {
      const ang = f + (k % 2 ? 1 : -1) * Math.ceil(k / 2) * Math.PI / 12, eye = new T.Vector3(r.x - Math.sin(ang) * dist, y + eyeH, r.z - Math.cos(ang) * dist);
      if (aims.every(t => clear(eye, t))) list.push(eye);
    }
    if (!list.length) {log.push(`mission: no clear spot near ${a.id}`);list.push(new T.Vector3(r.x - Math.sin(f) * 2, y + eyeH, r.z - Math.cos(f) * 2));}
    return list;
  }
  const spotNear = (a, eyeH) => spotsNear(a, eyeH)[0];
  function look(eye, target, strafe = 0) {
    const d = target.clone().sub(eye), r = new T.Vector3(-d.z, 0, d.x).normalize();
    G.camera.position.copy(eye).addScaledVector(r, strafe);G.camera.lookAt(target);
  }
  const alive = role => [...cg.debug.actors.values()].filter(a => a.role === role && a.alive);
  // Math.random: case-game.js's own calls draw from the scenario's seeded generator. three.js also draws four numbers
  // for the uuid of every new mesh, geometry and material (the mannequins, bullet holes, the first overlay render);
  // those go to a second generator, so the C++ code, which makes no such objects, sees the same sequence.
  const threeLine = (() => {
    let frame = '';const R = Math.random;
    Math.random = function () {frame = new Error().stack.split('\n')[2] || '';return R();};
    try {new T.Object3D();} finally {Math.random = R;}
    // "at Pt (file:///.../haarklous-game.html?server=...:256:1234)": the script line is the second-to-last number.
    const m = /:(\d+):\d+\)?$/.exec(frame.trim());return m ? m[1] : null;
  })();
  log.push(`mission: three.js uuid generator on line ${threeLine}`);
  function scenario(name, seed, body) {
    const s = CG.createRng(seed), other = CG.createRng(seed + 77777);
    Math.random = () => {const m = /:(\d+):\d+\)?$/.exec((new Error().stack.split('\n')[2] || '').trim());return m && m[1] === threeLine ? other() : s();};
    let sens = {pad: 1, mouse: 1};try {Object.assign(sens, JSON.parse(localStorage.getItem('haarklous-look-sensitivity-v1') || 'null') || {});} catch {}
    rec = {name, seed, aspect: G.camera.aspect, fov0: G.camera.fov, sens: [sens.pad, sens.mouse], frames: []};
    rays = [];dynLog.length = 0;
    try {body();} finally {if (cg.active) frame(api => api.call('stop'));}
    out.scenarios.push(rec);
    log.push(`mission scenario ${name}: ${rec.frames.length} frames, ${rec.frames.reduce((n, f) => n + f.rays.length, 0)} raycasts, end ${rec.frames[rec.frames.length - 1].s.st}`);
  }
  out.scenarios = [];
  const shootLoop = (opts) => {
    // Engage the hostiles one by one from a spot with a clear view; fire at a steady rhythm (mouse, triggers), aim
    // down the sights part of the time, reload by R and by □, strafe a little so the player is harder to hit.
    let target = null, eye = null, k = 0, quiet = 0, spots = [], spot = 0, shotsHere = 0, health = 0, missed = 0;
    while (k < opts.maxFrames && quiet < 140) {
      if (cg.state !== 'play') quiet++;
      const next = opts.pick();
      if (next && next !== target) {target = next;spots = spotsNear(target);spot = 0;eye = spots[0];shotsHere = 0;health = target.health;missed = 0;}
      if (target && k % opts.every === 0 && cg.state === 'play') missed++;
      // No hit after four shots from here: try the next spot.
      if (target && target.health !== health) {health = target.health;shotsHere = 0;}
      if (target && k % opts.every === 0 && cg.state === 'play' && ++shotsHere > 4) {spot = (spot + 1) % spots.length;eye = spots[spot];shotsHere = 1;}
      frame(api => {
        if (target && eye) {
          const aimHead = opts.head(k);
          // The first shots at each target go wide on purpose (misses, bullet holes, time for return fire).
          const aim = aimHead ? limbAt(target, true) : torso(target);
          if (missed <= (opts.missFirst || 0)) {const d = aim.clone().sub(eye);aim.add(new T.Vector3(-d.z, 0, d.x).normalize().multiplyScalar(.75));}
          look(eye, aim, opts.strafe ? Math.sin(k / 18) * .35 : 0);
          if (opts.down && k % 300 === 150) {G.camera.lookAt(G.camera.position.clone().add(new T.Vector3(0, -3, .2)));api.call('padButton', 'square');}
        }
        if (cg.state === 'play') {
          const ph = k % opts.every;
          if (ph === 0) (Math.floor(k / opts.every) % 3 === 2 ? api.call('triggers', opts.l2(k), 1) : api.call('mouse', 0, true));
          if (ph === 1) (Math.floor(k / opts.every) % 3 === 2 ? api.call('triggers', opts.l2(k), 0) : api.call('mouse', 0, false));
          if (k === opts.adsOn) api.call('mouse', 2, true);
          if (k === opts.adsOff) api.call('mouse', 2, false);
          if (k === opts.reloadAt) api.call('key', {key: 'r', code: 'KeyR', repeat: false});
          if (opts.stance) P.stance = opts.stance(k);
        }
      });
      k++;
    }
  };
  try {
    // A: a straight fight (the hostiles shoot back for real).
    scenario('fight', 1001, () => {
      frame(api => {api.call('start', false);api.invulnerable(false);captureGun();rec.aspect = G.camera.aspect;});
      wait(30);
      frame(api => api.call('padButton', 'right'));wait(4);frame(api => api.call('padButton', 'left'));wait(6);
      frame(api => api.call('padButton', 'triangle'));wait(2);
      frame(api => api.call('padButton', 'cross'));
      shootLoop({maxFrames: 1500, every: 30, missFirst: 4, pick: () => alive('hostile').sort((a, b) => a.root.position.distanceTo(G.camera.position) - b.root.position.distanceTo(G.camera.position))[0] || null,
        head: k => Math.floor(k / 30) % 2 === 1, strafe: true, adsOn: 90, adsOff: 330, reloadAt: 400, l2: k => (k > 600 && k < 800 ? 1 : 0), down: true});
      frame(api => api.call('padButton', 'cross'));wait(20);
      frame(api => api.call('padButton', 'circle'));wait(5);
    });
    // B: invulnerable, crouched: clear the flat and the stairwell, then a second case where the hostage is shot.
    scenario('clear', 2002, () => {
      frame(api => api.call('start', false));wait(8);
      frame(api => api.call('key', {key: ' ', code: 'Space', repeat: false}));
      frame(api => api.invulnerable(true));
      shootLoop({maxFrames: 1600, every: 15, missFirst: 3, pick: () => alive('hostile')[0] || null, head: k => k % 3 === 0, strafe: false, adsOn: 40, adsOff: 900, reloadAt: 250,
        l2: k => (k > 1000 ? .8 : 0), down: true, stance: k => (k < 700 ? 'crouch' : k < 1000 ? 'prone' : 'stand')});
      frame(api => api.call('key', {key: 'Enter', code: 'Enter', repeat: false}));wait(6);
      frame(api => api.call('padButton', 'cross'));wait(10);
      frame(api => api.call('padButton', 'options'));wait(12);
      frame(api => api.call('padButton', 'left'));frame(api => api.call('padButton', 'right'));
      frame(api => api.call('key', {key: 'r', code: 'KeyR', repeat: false}));wait(6);
      frame(api => api.call('padButton', 'cross'));
      const hostage = [...cg.debug.actors.values()].find(a => a.role === 'hostage'), eye = spotNear(hostage, 1.65);
      let k = 0;
      while (k < 400 && cg.state === 'play') {frame(api => {look(eye, torso(hostage));if (k % 25 === 5) api.call('mouse', 0, true);if (k % 25 === 6) api.call('mouse', 0, false);});k++;}
      wait(90);
      frame(api => api.call('key', {key: 'Escape', code: 'Escape', repeat: false}));wait(4);
    });
    // C: stand in the open without shooting: the hostiles put the player down; pause and resume on the way.
    scenario('down', 3003, () => {
      frame(api => {api.call('start', false);api.invulnerable(false);});wait(5);
      frame(api => {api.call('key', {key: 'e', code: 'KeyE', repeat: false});api.call('key', {key: 'x', code: 'KeyX', repeat: false});api.call('mouse', 0, true);api.call('mouse', 0, false);});
      frame(api => api.call('padButton', 'cross'));
      const h = alive('hostile')[0], eye = spotNear(h);
      let k = 0;
      while (k < 1500 && (cg.state === 'play' || cg.state === 'paused')) {
        frame(api => {
          // Empty the magazine into the floor (the last shot clicks and reloads), then just stand there.
          if (k < 70) {G.camera.position.copy(eye);G.camera.lookAt(eye.clone().add(new T.Vector3(.3, -2, .4)));} else look(eye, limbAt(h, true), Math.sin(k / 30) * .25);
          if (k >= 4 && k < 60 && k % 3 === 1) api.call('mouse', 0, true);
          if (k >= 4 && k < 60 && k % 3 === 2) api.call('mouse', 0, false);
          if (k === 64) {api.call('key', {key: 'r', code: 'KeyR', repeat: true});api.call('mouse', 1, true);api.call('mouse', 1, false);}
          if (k === 120) api.call('padButton', 'options');
          if (k === 150) api.call('padButton', 'cross');
          if (k === 160) api.call('key', {key: 'Escape', code: 'Escape', repeat: false});
          if (k === 175) api.call('key', {key: 'Enter', code: 'Enter', repeat: false});
          if (k === 180) api.call('mouse', 2, true);
          if (k === 240) api.call('mouse', 2, false);
          if (k === 250) {api.call('triggers', .5, 0);}
          if (k === 300) {api.call('triggers', 0, 0);}
          P.stance = k < 260 ? 'stand' : k < 450 ? 'crouch' : 'prone';
        });
        k++;
      }
      wait(100);
      frame(api => api.call('padButton', 'touchpad'));wait(10);
      frame(api => api.call('padButton', 'circle'));wait(4);
    });
  } finally {
    if (cg.active) cg.stop();
    Math.random = saved.random;window.requestAnimationFrame = saved.raf;window.cancelAnimationFrame = saved.caf;window.setTimeout = saved.st;window.clearTimeout = saved.ct;
    window.AudioContext = saved.ac;window.webkitAudioContext = saved.wac;T.Raycaster.prototype.intersectObjects = saved.ri;WC.setDynamic = saved.sd;T.Object3D.prototype.updateMatrixWorld = saved.umw;
    keepPose.forEach(k => {P[k] = savedPose[k];});
    G.camera.position.copy(saved.cam.p);G.camera.quaternion.copy(saved.cam.q);G.camera.fov = saved.cam.fov;G.camera.updateProjectionMatrix();
    const n = $('notice');if (n) n.hidden = true;
  }
  files.mission = {json: JSON.stringify(out), bin: null};
  log.push(`reference mission: ${out.zonedPlans.length} zoned plans, ${out.samplePlans.length} world plans, ${out.explain.length} explain points, ${out.scenarios.length} scenarios`);
  window.__hkDone = true;
})();
