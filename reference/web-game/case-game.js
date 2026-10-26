/* First-person "case" game: pistol, visible arms, randomised hostiles and a possible hostage.
 * Pure helpers (Node-testable):
 *   createRng(seed) -> () => [0,1)            deterministic mulberry32 generator
 *   planCase(rng, sampler, options) -> {hostileCount, hasHostage, actors:[{id,role,x,z,area,facing}]}
 *     sampler(rng) -> {x,z,area}|null returns a free standing spot inside the apartment/balcony.
 *   outcome(actors) -> 'fail' (any hostage down) | 'success' (all hostiles down) | null
 *   applyHit(actor, head) -> actor with health reduced; hostiles take 2 body hits or 1 head hit.
 *   hostileHitChance(distance, playerSpeed, stance) -> 0..1 chance that one hostile shot hits the player (stand/crouch/prone).
 *   damagePlayer(player, amount) -> player with health reduced; outcome(actors, player) fails when the player is down.
 * mount(ctx) builds the Three.js scene parts and HUD; app.js forwards input, frames and rendering.
 * Distances are metres; the world uses x/z on the floor and y up, like app.js.
 */
(function (root, factory) {
  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.CaseGame = api;
})(typeof window === 'undefined' ? globalThis : window, function () {
  'use strict';
  const MAGAZINE = 15, RESERVE = 45, RELOAD_SECONDS = 1.45, HOSTILE_HEALTH = 2;
  // Return fire: four hits put the player down. A hostile needs line of sight and must face the player;
  // it waits a reaction time before its first shot, then fires at a steady interval.
  const PLAYER_HEALTH = 100, HOSTILE_DAMAGE = 25, HOSTILE_REACTION = [.75, 1.2], HOSTILE_INTERVAL = [1.0, 1.6];

  function createRng(seed) {
    let a = (seed >>> 0) || 1;
    return () => {
      a = (a + 0x6D2B79F5) | 0;
      let t = Math.imul(a ^ (a >>> 15), 1 | a);
      t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
      return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
    };
  }
  // Hostage rescue: one hostage in the apartment, 3–5 hostiles split between the apartment and the stairwell.
  // sampler(rng, zone) returns a free spot {x, z, y, area} in 'apartment' or 'stair'.
  function planCase(rng, sampler, {minSeparation = 1.1, hostiles = [3, 5], maxTries = 600} = {}) {
    const hostileCount = hostiles[0] + Math.floor(rng() * (hostiles[1] - hostiles[0] + 1)), hasHostage = true;
    const inStair = Math.max(1, Math.min(hostileCount - 1, 1 + Math.floor(rng() * 2)));
    const roles = [['hostage', 'apartment'], ...Array.from({length: hostileCount}, (_, i) => ['hostile', i < inStair ? 'stair' : 'apartment'])], actors = [];
    for (const [role, zone] of roles) {
      let spot = null;
      for (let i = 0; i < maxTries && !spot; i++) {
        const s = sampler(rng, zone);
        if (s && actors.every(a => Math.hypot(a.x - s.x, a.z - s.z) >= minSeparation || Math.abs((a.y || 0) - (s.y || 0)) > 1.5)) spot = s;
      }
      if (!spot) throw new Error(`No free position for ${role}`);
      actors.push({id: `${role}-${actors.length}`, role, zone, x: spot.x, z: spot.z, y: spot.y || 0, area: spot.area, facing: rng() * Math.PI * 2,
        health: role === 'hostile' ? HOSTILE_HEALTH : 1, alive: true});
    }
    return {hostileCount, hasHostage, actors};
  }
  function applyHit(actor, head) {
    if (!actor.alive) return actor;
    actor.health = Math.max(0, actor.health - (head ? actor.health : 1));
    actor.alive = actor.health > 0;
    return actor;
  }
  // About 75% close up, falling with distance; moving (especially running) makes the player harder to hit.
  // Crouching and lying prone present a smaller target.
  const STANCE_PENALTY = {stand: 0, crouch: .12, prone: .22};
  function hostileHitChance(distance, playerSpeed = 0, stance = 'stand') {
    const base = .75 - .05 * Math.max(0, distance - 1.5), moving = Math.min(1, Math.max(0, playerSpeed) / 3.2) * .3;
    return Math.max(stance === 'stand' ? .12 : .08, Math.min(.85, base - moving - (STANCE_PENALTY[stance] || 0)));
  }
  function damagePlayer(player, amount = HOSTILE_DAMAGE) {
    if (!player.alive) return player;
    player.health = Math.max(0, player.health - amount);
    player.alive = player.health > 0;
    return player;
  }
  function outcome(actors, player) {
    if (actors.some(a => a.role === 'hostage' && !a.alive)) return 'fail';
    if (player && !player.alive) return 'fail';
    if (actors.filter(a => a.role === 'hostile').every(a => !a.alive)) return 'success';
    return null;
  }
  function insidePolygon(p, points) {
    let inside = false;
    for (let i = 0, j = points.length - 1; i < points.length; j = i++) {
      const a = points[i], b = points[j];
      if ((a.z > p.z) !== (b.z > p.z) && p.x < (b.x - a.x) * (p.z - a.z) / (b.z - a.z) + a.x) inside = !inside;
    }
    return inside;
  }
  const polygonArea = points => Math.abs(points.reduce((s, p, i) => {const q = points[(i + 1) % points.length]; return s + p.x * q.z - q.x * p.z;}, 0)) / 2;

  function mount(ctx) {
    const T = ctx.T, {scene, renderer, viewport, collision} = ctx;
    let active = false, state = 'off', plan = null, seed = 0, caseId = '', elapsed = 0, shots = 0, hits = 0;
    let mag = MAGAZINE, reserve = RESERVE, reload = 0, aimT = 0, padAim = 0, mouseAim = false, triggerHeld = false;
    let recoil = 0, flashT = 0, bobPhase = 0, lastPos = null, hitMarkerT = 0, frame = null, lastTime = 0, losTimer = 0, audio = null;
    let player = {health: PLAYER_HEALTH, alive: true}, playerSpeed = 0, damageT = 0, damageFrom = null, damageDirT = 0, enemyFlashT = 0, invulnerable = false;
    const between = ([a, b]) => a + Math.random() * (b - a);
    const actors = new Map(), decals = [], raycaster = new T.Raycaster();
    const mat = (color, opts = {}) => new T.MeshStandardMaterial({color, roughness: .8, ...opts});

    // The walkable stairwell (stairwell.js) is part of the first-person world; the case starts on its 4th-floor landing.
    // Constant light count avoids shader recompiles; the flash light is dark until a shot.
    const flashLight = new T.PointLight(0xffd28a, 0, 7, 2);scene.add(flashLight);
    const enemyFlashLight = new T.PointLight(0xffc070, 0, 6, 2);scene.add(enemyFlashLight);

    // ---- Mannequins. Root at feet; inner "body" tips over when neutralised.
    function limb(parent, w, h, d, x, y, z, m, id, head = false) {
      const mesh = new T.Mesh(new T.BoxGeometry(w, h, d), m);mesh.position.set(x, y, z);mesh.castShadow = true;
      mesh.userData.actorId = id;mesh.userData.head = head;parent.add(mesh);return mesh;
    }
    function buildActor(a) {
      const root = new T.Group(), body = new T.Group();root.add(body);root.position.set(a.x, a.y || 0, a.z);root.rotation.y = a.facing;
      const meshes = [];let muzzleFlash = null;
      const add = (...args) => {const m = limb(body, ...args, a.id);meshes.push(m);return m;};
      const addHead = (r, x, y, z, m) => {const h = new T.Mesh(new T.SphereGeometry(r, 18, 14), m);h.position.set(x, y, z);h.castShadow = true;h.userData.actorId = a.id;h.userData.head = true;body.add(h);meshes.push(h);return h;};
      if (a.role === 'hostile') {
        const cloth = mat(0x2a2e35), mask = mat(0x141518), skin = mat(0xc99a7c), band = mat(0xb3261e), gun = mat(0x1a1a1a, {roughness: .4, metalness: .5});
        add(.15, .86, .17, -.11, .43, 0, cloth);add(.15, .86, .17, .11, .43, 0, cloth);
        add(.44, .62, .25, 0, 1.17, 0, cloth);add(.2, .1, .2, 0, 1.5, 0, cloth);
        addHead(.115, 0, 1.64, 0, mask);add(.15, .035, .02, 0, 1.66, -.105, skin);
        // Both arms forward holding a pistol; the red armband marks the armed suspect.
        add(.11, .11, .42, -.17, 1.33, -.2, cloth);add(.11, .11, .42, .17, 1.33, -.2, cloth);
        add(.115, .07, .115, .17, 1.33, -.08, band);
        add(.08, .08, .08, 0, 1.31, -.43, skin);add(.035, .05, .17, 0, 1.36, -.52, gun);
        // Muzzle flash for return fire; transparent, so it is never shot or treated as a sight blocker.
        muzzleFlash = new T.Mesh(new T.ConeGeometry(.035, .12, 10, 1, true), new T.MeshBasicMaterial({color: 0xffc56b, transparent: true, opacity: .9, blending: T.AdditiveBlending, depthWrite: false, side: T.DoubleSide}));
        muzzleFlash.rotation.x = -Math.PI / 2;muzzleFlash.position.set(0, 1.36, -.67);muzzleFlash.visible = false;body.add(muzzleFlash);
      } else {
        const shirt = mat(0x7fa7d6), jeans = mat(0x3d5a80), skin = mat(0xe0b49a), hair = mat(0x4a3426);
        // Kneeling with hands on the head.
        add(.15, .14, .5, -.11, .07, .12, jeans);add(.15, .14, .5, .11, .07, .12, jeans);
        add(.15, .42, .17, -.11, .35, -.06, jeans);add(.15, .42, .17, .11, .35, -.06, jeans);
        add(.4, .56, .23, 0, .84, -.04, shirt);addHead(.11, 0, 1.25, -.04, skin);add(.2, .06, .2, 0, 1.34, -.02, hair);
        add(.09, .3, .09, -.26, 1.12, -.04, shirt);add(.09, .3, .09, .26, 1.12, -.04, shirt);
        add(.09, .09, .22, -.18, 1.3, -.04, skin);add(.09, .09, .22, .18, 1.3, -.04, skin);
      }
      scene.add(root);
      return {...a, root, body, meshes, muzzleFlash, fall: 0, hitFlash: 0, flashT: 0, engaged: false, fireIn: between(HOSTILE_REACTION)};
    }
    function clearActors() {
      actors.forEach(a => {scene.remove(a.root);a.root.traverse(o => {if (o.geometry) o.geometry.dispose();if (o.material) o.material.dispose();});});
      actors.clear();
    }

    // ---- Viewmodel: arms and pistol in their own scene, drawn over the world without clipping walls.
    const vmScene = new T.Scene(), vmCamera = new T.PerspectiveCamera(62, 1, .01, 5);
    vmScene.add(new T.HemisphereLight(0xf1f5ff, 0x4a4540, 2.1));
    const vmSun = new T.DirectionalLight(0xffffff, 2.2);vmSun.position.set(.9, .8, .3);vmScene.add(vmSun);const vmRim = new T.DirectionalLight(0xcfe0ff, 1.1);vmRim.position.set(-.8, .4, -.6);vmScene.add(vmRim);
    const gun = new T.Group(), slide = new T.Group();gun.add(slide);vmScene.add(gun);
    const flash = new T.Mesh(new T.ConeGeometry(.03, .09, 10, 1, true), new T.MeshBasicMaterial({color: 0xffc56b, transparent: true, opacity: .9, blending: T.AdditiveBlending, depthWrite: false, side: T.DoubleSide}));
    flash.rotation.x = -Math.PI / 2;flash.position.set(0, 0, -.215);flash.visible = false;gun.add(flash);
    if (ctx.lib && window.Viewmodel) window.Viewmodel.build({T, lib: ctx.lib, gun, slide});
    if (ctx.lib) vmScene.environment = ctx.lib.environment();
    const hip = {p: new T.Vector3(.12, -.075, -.44), r: new T.Euler(.02, .08, 0)}, ads = {p: new T.Vector3(0, -.029, -.36), r: new T.Euler(0, 0, 0)};

    // ---- HUD and case panels (inside the viewport so they show in fullscreen).
    const hud = document.createElement('div');hud.id = 'caseHud';hud.hidden = true;
    hud.innerHTML = '<div class="case-top"><div><b id="caseHudId"></b><span id="caseHudObjective">Neutralise all hostiles · Do not harm the hostage</span></div><b id="caseHudTime">00:00.0</b></div>'
      + '<div id="caseCrosshair"><i></i><i></i><i></i><i></i><span></span></div><div id="caseHitMarker">✕</div><div id="caseFeed" role="status" aria-live="polite"></div>'
      + '<div class="case-ammo"><b id="caseMag">15</b><span id="caseReserve">/ 45</span><small id="caseReload"></small></div>'
      + '<div class="case-health"><span>HEALTH</span><b id="caseHealth">100</b><i><u id="caseHealthBar"></u></i></div><div id="caseDamage"></div><div id="caseDamageDir"></div>';
    const panel = document.createElement('div');panel.id = 'casePanel';panel.hidden = true;panel.setAttribute('role', 'dialog');
    viewport.append(hud, panel);
    const $ = id => document.getElementById(id);
    function showPanel(html, buttons) {
      panel.innerHTML = html + '<div class="case-buttons">' + buttons.map(b => `<button data-case-action="${b.action}">${b.label}</button>`).join('') + '</div>';
      panel.hidden = false;panel.querySelectorAll('button').forEach(b => b.onclick = () => action(b.dataset.caseAction, true));
      panel.querySelectorAll('[data-sens]').forEach(input => {
        input.oninput = () => {ctx.sensitivity.set(input.dataset.sens, +input.value);syncSensitivity();};
        // Release focus after a mouse change so a focused field does not hold back controller input.
        input.onchange = () => input.blur();
      });
      viewport.dataset.casePanel = state;
    }
    function hidePanel() {panel.hidden = true;delete viewport.dataset.casePanel;}
    function sensitivityHtml() {
      const s = ctx.sensitivity.get(), row = (kind, label) => `<label>${label}<input type="range" data-sens="${kind}" min="${ctx.sensitivity.min}" max="${ctx.sensitivity.max}" step="0.05" value="${s[kind]}"><output data-sens-out="${kind}">${s[kind].toFixed(2)}×</output></label>`;
      return `<div class="case-sens"><span class="case-eyebrow">LOOK SENSITIVITY</span>${row('pad', 'Controller')}${row('mouse', 'Mouse')}<small>D-pad ←/→ changes controller sensitivity here.</small></div>`;
    }
    function syncSensitivity() {
      const s = ctx.sensitivity.get();
      panel.querySelectorAll('[data-sens]').forEach(input => {if (document.activeElement !== input) input.value = s[input.dataset.sens];});
      panel.querySelectorAll('[data-sens-out]').forEach(out => {out.textContent = `${s[out.dataset.sensOut].toFixed(2)}×`;});
    }
    let feedTimer = null;
    function feed(text, kind = '') {const el = $('caseFeed');el.textContent = text;el.className = kind;clearTimeout(feedTimer);feedTimer = setTimeout(() => {el.textContent = '';}, 1800);}
    const clock = t => `${String(Math.floor(t / 60)).padStart(2, '0')}:${(t % 60).toFixed(1).padStart(4, '0')}`;
    const controls = '<dl class="case-controls"><dt>Left stick</dt><dd>Move · click L3 to run</dd><dt>Right stick</dt><dd>Look</dd><dt>L2</dt><dd>Aim down sights</dd><dt>R2</dt><dd>Fire</dd><dt>□</dt><dd>Open / close the door in front, otherwise reload</dd><dt>✕</dt><dd>Jump</dd><dt>○</dt><dd>Tap: crouch · hold: lie prone</dd><dt>Options</dt><dd>Pause</dd><dt>Keyboard</dt><dd>Space jump · C crouch · Z prone · R reload · E door</dd><dt>Mouse</dt><dd>Right button aim · left fire</dd></dl>';

    // ---- Spawn sampling inside the apartment and balcony.
    function makeSampler() {
      // Square-root weighting spreads actors over small rooms and the balcony too, not mostly the living room.
      const areas = ctx.spawnAreas().map(a => ({...a, area: Math.max(1.2, Math.sqrt(polygonArea(a.points)))})), total = areas.reduce((s, a) => s + a.area, 0);
      const world = ctx.walkWorld(), spawnWorld = collision.createWorld({floors: areas.map(a => ({points: a.points, ceilingHeight: a.ceilingHeight})), obstacles: world.obstacles, radius: .24, clearance: .08});
      // Keep actors out of each interior door leaf's sweep, so those doors still open around them.
      // The balcony door swings over the whole 0.78 m balcony: there only its closed leaf is kept clear,
      // so someone on the balcony blocks that door and must be engaged through the window.
      const entrance = ctx.entrance(), keepOut = ctx.doors().flatMap(d => {
        const width = d.leaf.geometry.parameters.width, points = [];
        for (const t of d.name === 'Balcony door' ? [0] : [0, .2, .4, .6, .8, 1]) {
          if (d.motion === 'slide') d.hinge.position.x = d.closedX + d.travel * t;else d.hinge.rotation.y = d.openAngle * t;
          d.hinge.updateMatrixWorld(true);
          const a = d.leaf.localToWorld(new T.Vector3(-width / 2, 0, 0)), b = d.leaf.localToWorld(new T.Vector3(width / 2, 0, 0));
          points.push({a: {x: a.x, z: a.z}, b: {x: b.x, z: b.z}});
        }
        if (d.motion === 'slide') d.hinge.position.x = d.closedX;else d.hinge.rotation.y = 0;d.hinge.updateMatrixWorld(true);
        return points;
      });
      const segmentDistance = (p, {a, b}) => {const dx = b.x - a.x, dz = b.z - a.z, t = Math.max(0, Math.min(1, ((p.x - a.x) * dx + (p.z - a.z) * dz) / (dx * dx + dz * dz || 1)));return Math.hypot(p.x - a.x - t * dx, p.z - a.z - t * dz);};
      const free = p => Math.hypot(p.x - entrance.x, p.z - entrance.z) >= 1.7 && !keepOut.some(k => segmentDistance(p, k) < .32) && collision.canStand(spawnWorld, p, 1.75);
      // Pick a room first, then search inside it, so furnished rooms are not starved by rejections.
      const stairZones = ctx.stairSpawns ? ctx.stairSpawns() : [], walkWorld = ctx.walkWorld();
      const sampler = (rng, only) => {
        if (only === 'stair' && stairZones.length) {
          const zone = stairZones[Math.floor(rng() * stairZones.length)], xs = zone.points.map(p => p.x), zs = zone.points.map(p => p.z);
          for (let i = 0; i < 60; i++) {
            const p = {x: Math.min(...xs) + .35 + rng() * (Math.max(...xs) - Math.min(...xs) - .7), z: Math.min(...zs) + .35 + rng() * (Math.max(...zs) - Math.min(...zs) - .7)};
            if (insidePolygon(p, zone.points) && collision.canStand(walkWorld, p, 1.75, zone.y)) return {x: p.x, z: p.z, y: zone.y, area: zone.name};
          }
          return null;
        }
        if (only === 'apartment' || only === 'stair') only = undefined;
        let pick = rng() * total, area = areas[areas.length - 1];
        for (const a of areas) {if ((pick -= a.area) <= 0) {area = a;break;}}
        if (only) area = areas.find(a => a.name === only);
        const xs = area.points.map(p => p.x), zs = area.points.map(p => p.z);
        for (let i = 0; i < 80; i++) {
          const p = {x: Math.min(...xs) + rng() * (Math.max(...xs) - Math.min(...xs)), z: Math.min(...zs) + rng() * (Math.max(...zs) - Math.min(...zs))};
          if (insidePolygon(p, area.points) && free(p)) return {x: p.x, z: p.z, y: 0, area: area.name};
        }
        return null;
      };
      sampler.explain = p => ({entrance: Math.hypot(p.x - entrance.x, p.z - entrance.z), door: Math.min(...keepOut.map(k => segmentDistance(p, k))), stand: collision.canStand(spawnWorld, p, 1.75)});
      return sampler;
    }

    // ---- Case flow.
    function newCase() {
      clearActors();decals.splice(0).forEach(d => d.parent && d.parent.remove(d));
      seed = (Math.random() * 2 ** 32) >>> 0;caseId = `HP-${String(seed % 100000).padStart(5, '0')}`;
      ctx.closeAllDoors();ctx.rebuildWalkWorld();
      plan = planCase(createRng(seed), makeSampler());
      plan.actors.forEach(a => actors.set(a.id, buildActor(a)));
      player = {health: PLAYER_HEALTH, alive: true};damageT = 0;damageDirT = 0;damageFrom = null;enemyFlashT = 0;
      elapsed = 0;shots = 0;hits = 0;mag = MAGAZINE;reserve = RESERVE;reload = 0;recoil = 0;triggerHeld = false;mouseAim = false;
      ctx.rebuildWalkWorld();
      {const s0 = ctx.missionStart ? ctx.missionStart() : {x: .25, z: 5.83, y: 0, yaw: -Math.PI / 2};ctx.setPose(s0.x, s0.z, s0.yaw, 0, s0.y);}
      viewport.dataset.caseId = caseId;viewport.dataset.caseActors = JSON.stringify(plan.actors.map(({id, role, x, z, area}) => ({id, role, x: +x.toFixed(2), z: +z.toFixed(2), area})));
      updateHud();
    }
    function briefing() {
      state = 'briefing';
      showPanel(`<span class="case-eyebrow">CASE FILE · ${caseId}</span><h2>Hostage rescue · Haarklous plass 17A</h2>
        <p>You are outside the street door. A <b>hostage</b> is held in the <b>4th-floor apartment</b>. Intel reports <b>${plan.hostileCount} armed hostiles</b>: some in the stairwell, the rest in the apartment.</p>
        <p>The hostiles <b>shoot back</b> once they see you: four hits and you are down. Moving makes you harder to hit.</p>
        <p class="case-rules"><span>Success</span> every hostile neutralised. <span class="fail">Failure</span> the hostage is shot, or you are down.</p>${controls}${sensitivityHtml()}`,
      [{action: 'begin', label: '✕ / Enter · Begin mission'}, {action: 'quit', label: '○ / Esc · Leave'}]);
      updateHud();
    }
    function begin(viaGesture) {
      if (state !== 'briefing' && state !== 'paused') return;
      state = 'play';hidePanel();lastTime = 0;ctx.enterImmersive(viaGesture, true);updateHud();
    }
    function pause() {
      if (state !== 'play') return;
      state = 'paused';triggerHeld = false;mouseAim = false;
      showPanel(`<span class="case-eyebrow">CASE ${caseId} · PAUSED</span><h2>${clock(elapsed)} · health ${player.health}</h2>${sensitivityHtml()}${controls}`,
        [{action: 'resume', label: 'Options / ✕ · Resume'}, {action: 'restart', label: 'Touchpad / R · New case'}, {action: 'quit', label: '○ / Esc · Leave'}]);
    }
    function finish(result) {
      state = result;triggerHeld = false;
      const hostiles = plan.actors.filter(a => a.role === 'hostile'), down = hostiles.filter(a => !actors.get(a.id).alive).length;
      const hostageActor = plan.actors.find(a => a.role === 'hostage'), hostageDown = !!hostageActor && !actors.get(hostageActor.id).alive;
      const hostage = plan.hasHostage ? (hostageDown ? '<b class="fail">Shot</b>' : 'Safe') : 'None present';
      const title = result === 'success' ? 'Hostage rescued' : hostageDown ? 'Mission failed: hostage shot' : 'Mission failed: you were shot';
      showPanel(`<span class="case-eyebrow">CASE ${caseId} · ${result === 'success' ? 'CLOSED' : 'FAILED'}</span>
        <h2 class="${result}">${title}</h2>
        <dl class="case-stats"><dt>Time</dt><dd>${clock(elapsed)}</dd><dt>Hostiles</dt><dd>${down} / ${hostiles.length} neutralised</dd><dt>Hostage</dt><dd>${hostage}</dd><dt>Health</dt><dd>${player.health}</dd>
        <dt>Shots</dt><dd>${shots}</dd><dt>Accuracy</dt><dd>${shots ? Math.round(hits / shots * 100) : 0}%</dd></dl>`,
      [{action: 'restart', label: '✕ / Enter · New case'}, {action: 'quit', label: '○ / Esc · Leave'}]);
      viewport.dataset.caseResult = result;
    }
    function action(name, viaGesture = false) {
      if (name === 'begin' || name === 'resume') begin(viaGesture);
      else if (name === 'restart') {delete viewport.dataset.caseResult;newCase();briefing();if (viaGesture) ctx.enterImmersive(true);}
      else if (name === 'quit') stop();
    }
    function start(viaGesture = false) {
      if (!active) {
        active = true;viewport.dataset.case = 'on';hud.hidden = false;
        ctx.rebuildWalkWorld();{const s0 = ctx.missionStart ? ctx.missionStart() : {x: .25, z: 5.83, y: 0, yaw: -Math.PI / 2};ctx.setPose(s0.x, s0.z, s0.yaw, 0, s0.y);}ctx.enterWalk();
      }
      delete viewport.dataset.caseResult;newCase();briefing();ctx.enterImmersive(viaGesture);
      if (frame === null) frame = requestAnimationFrame(tick);
    }
    function stop() {
      if (!active) return;
      active = false;state = 'off';clearActors();decals.splice(0).forEach(d => d.parent && d.parent.remove(d));
      flashLight.intensity = 0;enemyFlashLight.intensity = 0;hud.hidden = true;hidePanel();
      delete viewport.dataset.case;delete viewport.dataset.caseResult;delete viewport.dataset.caseActors;
      if (frame !== null) cancelAnimationFrame(frame);frame = null;aimT = 0;padAim = 0;mouseAim = false;
      ctx.rebuildWalkWorld();ctx.leave();
    }

    // ---- Weapon.
    function sound(kind, distance = 1) {
      try {
        audio = audio || new (window.AudioContext || window.webkitAudioContext)();
        if (audio.state === 'suspended') audio.resume();
        // A hostile shot reuses the pistol sound, darker and quieter with distance.
        const enemy = kind === 'enemy';if (enemy) kind = 'shot';
        const now = audio.currentTime, length = kind === 'shot' ? .32 : .05, buffer = audio.createBuffer(1, audio.sampleRate * length, audio.sampleRate), data = buffer.getChannelData(0);
        for (let i = 0; i < data.length; i++) data[i] = (Math.random() * 2 - 1) * Math.pow(1 - i / data.length, kind === 'shot' ? 4 : 2);
        const src = audio.createBufferSource(), filter = audio.createBiquadFilter(), gain = audio.createGain();
        src.buffer = buffer;filter.type = 'lowpass';filter.frequency.setValueAtTime(enemy ? 1500 + 1200 / Math.max(1, distance) : kind === 'shot' ? 3200 : 5000, now);filter.frequency.exponentialRampToValueAtTime(kind === 'shot' ? 300 : 2000, now + length);
        gain.gain.setValueAtTime(enemy ? Math.min(.8, 2.2 / Math.max(1, distance)) : kind === 'shot' ? .9 : .25, now);src.connect(filter).connect(gain).connect(audio.destination);src.start(now);
        if (kind === 'shot') {const osc = audio.createOscillator(), g = audio.createGain();osc.frequency.setValueAtTime(140, now);osc.frequency.exponentialRampToValueAtTime(40, now + .18);g.gain.setValueAtTime(.7, now);g.gain.exponentialRampToValueAtTime(.001, now + .2);osc.connect(g).connect(audio.destination);osc.start(now);osc.stop(now + .2);}
      } catch {}
    }
    function startReload() {
      if (state !== 'play' || reload > 0 || mag === MAGAZINE || reserve === 0) return;
      reload = RELOAD_SECONDS;feed('Reloading…');sound('click');
    }
    function shootables() {
      const list = [...ctx.shootables()];
      actors.forEach(a => list.push(...a.meshes));
      return list.filter(m => !(m.material && m.material.transparent));
    }
    function fire() {
      if (state !== 'play' || reload > 0) return;
      if (mag === 0) {sound('click');feed(reserve ? 'Empty: press □ or R to reload' : 'Out of ammunition');if (reserve) startReload();return;}
      mag--;shots++;recoil = 1;flashT = .055;sound('shot');
      const camera = ctx.camera(), dir = camera.getWorldDirection(new T.Vector3());
      const spread = .032 * (1 - aimT) + .002, right = new T.Vector3().crossVectors(dir, camera.up).normalize(), up = new T.Vector3().crossVectors(right, dir).normalize();
      const angle = Math.random() * Math.PI * 2, radius = Math.sqrt(Math.random()) * spread;
      dir.addScaledVector(right, Math.cos(angle) * radius).addScaledVector(up, Math.sin(angle) * radius).normalize();
      raycaster.set(camera.position, dir);raycaster.far = 60;
      const hit = raycaster.intersectObjects(shootables().filter(visible), false)[0];
      flashLight.position.copy(camera.position).addScaledVector(dir, .6);
      if (hit && hit.object.userData.actorId) {
        const actor = actors.get(hit.object.userData.actorId);
        if (actor && actor.alive) {
          hits++;applyHit(actor, hit.object.userData.head);actor.hitFlash = .12;hitMarkerT = .25;
          // A hit hostile flinches before it can fire again.
          actor.fireIn = Math.max(actor.fireIn, .55);
          if (!actor.alive) {
            ctx.rebuildWalkWorld();
            if (actor.role === 'hostage') feed('HOSTAGE HIT', 'fail');else feed(hit.object.userData.head ? 'Hostile down · headshot' : 'Hostile down', 'ok');
            const result = outcome([...actors.values()], player);
            if (result) setTimeout(() => {if (active && state === 'play') finish(result);}, 650);
          } else feed('Hit');
        }
      } else if (hit) decal(hit);
      if (mag === 0 && reserve > 0) setTimeout(startReload, 260);
      updateHud();
    }
    const visible = o => {for (let p = o; p; p = p.parent) if (!p.visible) return false;return true;};
    function decal(hit) {
      if (!hit.face) return;
      const mark = new T.Mesh(new T.CircleGeometry(.011, 10), new T.MeshBasicMaterial({color: 0x1b1b1b, polygonOffset: true, polygonOffsetFactor: -4}));
      const normal = hit.face.normal.clone().transformDirection(hit.object.matrixWorld);
      mark.position.copy(hit.point).addScaledVector(normal, .002);mark.lookAt(mark.position.clone().add(normal));
      hit.object.attach(mark);decals.push(mark);
      if (decals.length > 80) {const old = decals.shift();old.parent && old.parent.remove(old);}
    }

    // ---- Return fire.
    const muzzlePos = new T.Vector3();
    function hostileFire(a) {
      a.flashT = .06;enemyFlashT = .06;
      const eye = ctx.camera().position;a.muzzleFlash.getWorldPosition(muzzlePos);enemyFlashLight.position.copy(muzzlePos);
      const dist = muzzlePos.distanceTo(eye);sound('enemy', dist);
      if (Math.random() < hostileHitChance(dist, playerSpeed, ctx.stance ? ctx.stance() : 'stand')) {
        damageFrom = {x: a.root.position.x, z: a.root.position.z};damageDirT = 1.4;damageT = .55;
        if (!invulnerable) damagePlayer(player);
        updateHud();
        if (!player.alive) {
          state = 'down';triggerHeld = false;mouseAim = false;feed('YOU ARE DOWN', 'fail');
          setTimeout(() => {if (active && state === 'down') finish('fail');}, 1100);
        } else feed(`You are hit · health ${player.health}`, 'fail');
        return true;
      }
      // A miss passes beside the player and marks whatever lies behind.
      const dir = tmp.copy(eye).sub(muzzlePos).normalize(), side = new T.Vector3(-dir.z, 0, dir.x).normalize();
      dir.addScaledVector(side, (Math.random() < .5 ? -1 : 1) * (.03 + Math.random() * .05));dir.y += (Math.random() - .5) * .08;dir.normalize();
      raycaster.set(muzzlePos, dir);raycaster.far = 30;
      const hit = raycaster.intersectObjects(shootables().filter(m => !m.userData.actorId).filter(visible), false)[0];
      if (hit) decal(hit);
      return false;
    }

    // ---- Per-frame update while the case is active.
    const tmp = new T.Vector3(), headPos = new T.Vector3();
    function hostileLooks(dt) {
      losTimer -= dt;if (losTimer > 0) return;losTimer = .2;
      const eye = ctx.camera().position, blockers = shootables().filter(m => !m.userData.actorId).filter(visible);
      actors.forEach(a => {
        if (a.role !== 'hostile' || !a.alive) return;
        headPos.set(a.root.position.x, a.root.position.y + 1.6, a.root.position.z);const dist = headPos.distanceTo(eye);
        if (dist > 9) {a.sees = false;return;}
        raycaster.set(headPos, tmp.copy(eye).sub(headPos).normalize());raycaster.far = dist;
        const block = raycaster.intersectObjects(blockers, false)[0];a.sees = !block || block.distance > dist - .25;
      });
    }
    function tick(time) {
      frame = active ? requestAnimationFrame(tick) : null;if (!active) return;
      const dt = lastTime ? Math.min((time - lastTime) / 1000, .05) : 0;lastTime = time;
      if (state === 'play') {
        elapsed += dt;
        if (reload > 0) {reload -= dt;if (reload <= 0) {const need = MAGAZINE - mag, take = Math.min(need, reserve);mag += take;reserve -= take;reload = 0;feed('Reloaded');updateHud();}}
        hostileLooks(dt);
      }
      const aimWanted = state === 'play' && reload <= 0 && (padAim > .3 || mouseAim);
      aimT += ((aimWanted ? 1 : 0) - aimT) * Math.min(1, dt * 11);
      const camera = ctx.camera(), fov = 65 - 22 * aimT;
      if (Math.abs(camera.fov - fov) > .01) {camera.fov = fov;camera.updateProjectionMatrix();}
      // Hostiles turn smoothly toward the player while they have line of sight.
      // Once facing the player, a hostile waits its reaction time and then keeps firing.
      actors.forEach(a => {
        if (a.alive && a.sees) {
          const eye = camera.position, want = Math.atan2(-(eye.x - a.root.position.x), -(eye.z - a.root.position.z));
          let d = want - a.root.rotation.y;d = Math.atan2(Math.sin(d), Math.cos(d));a.root.rotation.y += d * Math.min(1, dt * 4);
          if (!a.engaged) {a.engaged = true;a.fireIn = between(HOSTILE_REACTION);}
          if (state === 'play' && a.role === 'hostile' && Math.abs(d) < .3 && (a.fireIn -= dt) <= 0) {hostileFire(a);a.fireIn = between(HOSTILE_INTERVAL);}
        } else a.engaged = false;
        if (a.muzzleFlash) {a.flashT = Math.max(0, a.flashT - dt);a.muzzleFlash.visible = a.flashT > 0;a.muzzleFlash.rotation.y = Math.random() * 6;}
        if (!a.alive && a.fall < 1) {a.fall = Math.min(1, a.fall + dt * 2.6);const e = 1 - Math.pow(1 - a.fall, 3);a.body.rotation.x = e * Math.PI / 2 * .96;a.body.position.y = e * .08;}
        if (a.hitFlash > 0) {a.hitFlash -= dt;a.body.position.z = a.hitFlash > 0 ? .04 : 0;}
      });
      const pos = camera.position, moved = lastPos ? Math.hypot(pos.x - lastPos.x, pos.z - lastPos.z) / Math.max(dt, 1e-3) : 0;lastPos = {x: pos.x, z: pos.z};
      if (dt) playerSpeed += (Math.min(moved, 4) - playerSpeed) * Math.min(1, dt * 6);
      enemyFlashT = Math.max(0, enemyFlashT - dt);enemyFlashLight.intensity = enemyFlashT > 0 ? 14 : 0;
      damageT = Math.max(0, damageT - dt);damageDirT = Math.max(0, damageDirT - dt);
      // Red edge flash on each hit, plus a lasting tint at low health; the arrow points toward the shooter.
      $('caseDamage').style.opacity = String(Math.min(1, damageT * 1.8 + (player.alive ? (player.health <= 25 ? .35 : 0) : .9)));
      const arrow = $('caseDamageDir');
      if (damageFrom && damageDirT > 0) {
        const f = camera.getWorldDirection(tmp), vx = damageFrom.x - pos.x, vz = damageFrom.z - pos.z;
        const angle = Math.atan2(vx * -f.z + vz * f.x, vx * f.x + vz * f.z);
        arrow.style.transform = `translate(-50%, -50%) rotate(${angle}rad) translateY(-118px)`;arrow.style.opacity = String(Math.min(1, damageDirT));
      } else arrow.style.opacity = '0';
      bobPhase += dt * (5 + moved * 3.2);
      recoil = Math.max(0, recoil - dt * 9);flashT = Math.max(0, flashT - dt);hitMarkerT = Math.max(0, hitMarkerT - dt);
      flash.visible = flashT > 0;flash.rotation.z = Math.random() * 6;flashLight.intensity = flashT > 0 ? 22 : 0;
      const bob = Math.min(moved, 3) * (1 - aimT * .85), reloadPose = reload > 0 ? Math.sin(Math.PI * (1 - reload / RELOAD_SECONDS)) : 0;
      gun.position.lerpVectors(hip.p, ads.p, aimT);gun.position.x *= Math.min(1, vmCamera.aspect / 1.5) * (1 - aimT) + aimT;
      gun.position.x += Math.sin(bobPhase) * .004 * bob;gun.position.y += -Math.abs(Math.cos(bobPhase)) * .005 * bob - reloadPose * .09;
      gun.position.z += recoil * .03;
      gun.rotation.set(hip.r.x * (1 - aimT) + recoil * .16 - reloadPose * .5, hip.r.y * (1 - aimT), reloadPose * .55);
      slide.position.z = recoil > .55 ? .028 : recoil * .05;
      $('caseCrosshair').style.setProperty('--gap', `${4 + 14 * (1 - aimT) + recoil * 8}px`);
      $('caseCrosshair').style.opacity = String(Math.max(0, 1 - aimT * 1.6));
      $('caseHitMarker').style.opacity = String(hitMarkerT > 0 ? 1 : 0);
      $('caseHudTime').textContent = clock(elapsed);
      $('caseReload').textContent = reload > 0 ? 'RELOADING' : '';
      viewport.dataset.caseState = state;viewport.dataset.caseAim = aimT.toFixed(2);
      ctx.render();
    }
    function updateHud() {
      if (!plan) return;
      $('caseHudId').textContent = `CASE ${caseId}`;
      const down = plan.actors.filter(a => a.role === 'hostile' && !actors.get(a.id).alive).length;
      const total = plan.actors.filter(a => a.role === 'hostile').length, hostage = plan.actors.find(a => a.role === 'hostage');
      $('caseHudObjective').textContent = `Hostiles down: ${down} / ${total} · Hostage: ${hostage && !actors.get(hostage.id).alive ? 'HIT' : 'alive'}`;
      $('caseHealth').textContent = player.health;$('caseHealthBar').style.width = `${player.health}%`;$('caseHealth').classList.toggle('low', player.health <= 25);
      viewport.dataset.caseHealth = String(player.health);
      $('caseMag').textContent = mag;$('caseReserve').textContent = `/ ${reserve}`;$('caseMag').classList.toggle('low', mag <= 3);
    }
    function renderOverlay(camera) {
      if (!active || !ctx.isFirstPerson()) return;
      vmCamera.aspect = camera.aspect;vmCamera.updateProjectionMatrix();
      const auto = renderer.autoClear;renderer.autoClear = false;renderer.clearDepth();renderer.render(vmScene, vmCamera);renderer.autoClear = auto;
    }

    // ---- Input from app.js. Each returns true when the case consumed it.
    function padButton(name) {
      if (!active) return false;
      if ((state === 'briefing' || state === 'paused') && (name === 'left' || name === 'right')) {ctx.sensitivity.step('pad', name === 'left' ? -1 : 1);syncSensitivity();return true;}
      if (state === 'down') return true;
      if (state === 'briefing') {if (name === 'cross') begin(false);else if (name === 'circle') stop();return true;}
      if (state === 'paused') {if (name === 'options' || name === 'cross') begin(false);else if (name === 'touchpad') action('restart');else if (name === 'circle') stop();return true;}
      if (state === 'success' || state === 'fail') {if (name === 'cross' || name === 'options' || name === 'touchpad') action('restart');else if (name === 'circle') stop();return true;}
      if (name === 'options') {pause();return true;}
      // □ opens a door in reach, otherwise reloads.
      if (name === 'square') {if (!(ctx.useDoorAhead && ctx.useDoorAhead())) startReload();return true;}
      if (name === 'touchpad') {action('restart');return true;}
      // ✕ jump, ○ crouch / hold for prone and L3 run fall through; teleport, eye height and measuring are disabled.
      return !['cross', 'circle', 'l3'].includes(name);
    }
    function triggers(l2, r2) {
      if (!active) return;
      padAim = l2;
      if (r2 > .55 && !triggerHeld) {triggerHeld = true;fire();}else if (r2 < .35) triggerHeld = false;
    }
    function key(e) {
      if (!active) return false;
      if (e.key === 'Escape') {if (state === 'play') {pause();return true;}if (state === 'paused' || state === 'briefing' || state === 'success' || state === 'fail') {stop();return true;}}
      if (e.key === 'Enter' || e.key === ' ') {if (state === 'briefing' || state === 'paused') {begin(true);return true;}if (state === 'success' || state === 'fail') {action('restart', true);return true;}return e.key === 'Enter';}
      if (e.code === 'KeyR' && !e.repeat) {if (state === 'play') startReload();else if (state === 'paused' || state === 'success' || state === 'fail') action('restart', true);return true;}
      return state !== 'play' && e.code !== 'KeyE';
    }
    function mouse(button, down) {
      if (button === 2 && !down) mouseAim = false;
      if (!active || state !== 'play') return false;
      if (button === 0 && down) fire();
      if (button === 2) mouseAim = down;
      return true;
    }
    return {
      start, stop, pause, padButton, triggers, key, mouse, renderOverlay,
      get active() {return active;}, get state() {return state;},
      canMove: () => !active || state === 'play',
      lookScale: () => 1 - .55 * aimT,
      moveScale: () => (!active ? 1 : state === 'play' ? 1 - .45 * aimT : 0),
      say: text => {if (active && text) feed(text);},
      actorObstacles: () => [...actors.values()].filter(a => a.alive).map(a => {const r = .24, x = a.root.position.x, z = a.root.position.z, y = a.root.position.y;return {points: [{x: x - r, z: z - r}, {x: x + r, z: z - r}, {x: x + r, z: z + r}, {x: x - r, z: z + r}], minY: y, maxY: y + 1.8};}),
      floors: () => [],
      obstacles: () => {
        if (!active) return [];
        const list = [];
        actors.forEach(a => {if (!a.alive) return;const r = .24, x = a.root.position.x, z = a.root.position.z, points = [{x: x - r, z: z - r}, {x: x + r, z: z - r}, {x: x + r, z: z + r}, {x: x - r, z: z + r}];
          list.push({points, minY: a.root.position.y, maxY: a.root.position.y + 1.8});});
        return list;
      },
      // Test hooks: deterministic layouts and direct hits without aiming.
      debug: {get plan() {return plan;}, get actors() {return actors;}, get player() {return player;}, get invulnerable() {return invulnerable;}, set invulnerable(v) {invulnerable = !!v;}, hostileFire(id) {const a = actors.get(id);return a ? hostileFire(a) : null;}, fire, explain(x, z) {return makeSampler().explain({x, z});}, probeArea(name, n = 400) {const sampler = makeSampler(), rng = createRng(7);let ok = 0;for (let i = 0; i < n; i++) if (sampler(rng, name)) ok++;return ok;}, samplePlans(n) {const sampler = makeSampler();return Array.from({length: n}, (_, i) => planCase(createRng(i + 1), sampler));}, setPose(x, z, yaw, pitch = 0) {ctx.setPose(x, z, yaw, pitch);ctx.render();}, hitActor(id, head = false) {const a = actors.get(id);if (!a) return;const mesh = a.meshes.find(m => !!m.userData.head === head);const cam = ctx.camera(), target = mesh.getWorldPosition(new T.Vector3());cam.lookAt(target);cam.updateMatrixWorld();aimT = 1;fire();}}
    };
  }
  return {createRng, planCase, applyHit, hostileHitChance, damagePlayer, outcome, insidePolygon, mount, MAGAZINE, RESERVE, PLAYER_HEALTH, HOSTILE_DAMAGE};
});
