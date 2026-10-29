/* Haarklous plass — open-world game main loop.
 * Walk the neighbourhood, enter buildings, ride the Honda, drive cars and the excavator, play disc golf and petanque,
 * take photos, drink (carefully), and start the hostage-rescue mission at the street door of Haarklous plass 17A.
 * World coordinates are the apartment model's metres (4th-floor floor at y = 0, street at about y = -10).
 */
(() => {
  'use strict';
  const T = window.THREE, D = window.SURROUNDINGS, collision = window.WalkCollision, padReader = window.GamepadControls;
  const view = document.getElementById('game'), loading = document.getElementById('loading');
  const $ = id => document.getElementById(id);
  let caseGame = null;
  // ---------- Renderer, scenes, light ----------
  let renderer;
  try {renderer = new T.WebGLRenderer({antialias: true, powerPreference: 'high-performance'});}
  catch {loading.textContent = 'WebGL could not start. Use a browser with hardware acceleration.';return;}
  let pixelRatio = Math.min(window.devicePixelRatio || 1, 1.25);renderer.setPixelRatio(pixelRatio);
  renderer.shadowMap.enabled = true;renderer.shadowMap.type = T.PCFSoftShadowMap;renderer.outputColorSpace = T.SRGBColorSpace;renderer.toneMapping = T.ACESFilmicToneMapping;renderer.toneMappingExposure = 1.05;
  view.prepend(renderer.domElement);const canvas = renderer.domElement;canvas.tabIndex = 0;
  const lib = window.RealMaterials.create(T, renderer);
  const scene = new T.Scene(), farScene = new T.Scene();
  scene.fog = new T.FogExp2(0xc6d3dc, .0011);farScene.fog = new T.FogExp2(0xc6d3dc, .00011);scene.environment = lib.environment();
  const camera = new T.PerspectiveCamera(70, 1, .05, 450), farCamera = new T.PerspectiveCamera(70, 1, 15, 40000);
  const theta = D.geo.theta, sunDir = (() => {const b = 225 * Math.PI / 180, e = 38 * Math.PI / 180, x = Math.sin(b), z = -Math.cos(b);return new T.Vector3((x * Math.cos(theta) + z * Math.sin(theta)) * Math.cos(e), Math.sin(e), (-x * Math.sin(theta) + z * Math.cos(theta)) * Math.cos(e)).normalize();})();
  const sun = new T.DirectionalLight(0xfff3e0, 3.4);sun.castShadow = true;sun.shadow.mapSize.set(2048, 2048);sun.shadow.bias = -.0004;
  // normalBias removes the striped "shadow acne" that flickered on interior walls.
  sun.shadow.normalBias = .035;scene.add(sun, sun.target);
  const sky = new T.HemisphereLight(0xe3f4ff, 0xb9b09c, 1.7);scene.add(sky);
  const towerLamp = new T.PointLight(0xffe9c8, 0, 7, 1.6);scene.add(towerLamp);

  // ---------- World ----------
  const audio = window.GameAudio.create();
  // Soundtrack: a calm 2.5-minute open-world loop, and its own loop for disc golf, petanque and the hostage rescue.
  const music = window.GameMusic.create();
  // Joker and Kiwi are cut into the ground floors of their real buildings (this has to happen before the buildings are built).
  window.GameShops.applyNotches(D);
  // The construction site and the paintball yard (ported from the Unreal version): no trees or bushes grow in them.
  const SITE_DATA = window.SITE, PB_DATA = window.PAINTBALL;
  const holes = [SITE_DATA && {x0: SITE_DATA.rect.x0, s0: SITE_DATA.rect.s0, x1: SITE_DATA.rect.x0 + SITE_DATA.rect.w, s1: SITE_DATA.rect.s0 + SITE_DATA.rect.d, cutGround: true},
    PB_DATA && {x0: PB_DATA.rect.x0 - 1, s0: PB_DATA.rect.s0 - 1, x1: PB_DATA.rect.x0 + PB_DATA.rect.w + 1, s1: PB_DATA.rect.s0 + PB_DATA.rect.d + 1}].filter(Boolean);
  const world = window.Surroundings.create({T, lib, data: D, renderer, holes, onStatus: t => {const el = $('imageryStatus');if (el) el.textContent = t;}});
  scene.add(world.near);farScene.add(world.far);world.setSun(sunDir);
  const apt = window.GameApartment.build({T, lib, data: window.APARTMENT});scene.add(apt.group);
  // Stair towers: ours (model coordinates) and one in each enterable neighbouring building.
  const towers = [];
  const ownStair = window.Stairwell.create({T, lib, own: true, id: 'own', wallTop: D.own.top});scene.add(ownStair.group);ownStair.group.updateMatrixWorld(true);
  towers.push({stair: ownStair, matrix: ownStair.group.matrixWorld.clone(), own: true});
  D.enterable.forEach((e, i) => {
    const b = D.buildings[e.b], geo = new T.Group();geo.rotation.y = theta;geo.position.set(D.geo.x, 0, D.geo.z);scene.add(geo);
    const s = window.Stairwell.create({T, lib, own: false, id: 'b' + i, wallTop: b.top - e.y});s.group.position.set(e.x, e.y, e.s);s.group.rotation.y = e.rot;geo.add(s.group);geo.updateMatrixWorld(true);
    towers.push({stair: s, matrix: s.group.matrixWorld.clone(), building: b});
  });
  towers.forEach(t => {t.inverse = t.matrix.clone().invert();});
  const xf = (m, x, z) => {const v = new T.Vector3(x, 0, z).applyMatrix4(m);return {x: v.x, z: v.z};};
  function towerFloors(t) {
    const m = t.matrix, e = m.elements, dy = e[13];
    return t.stair.floors().map(f => {
      const out = {...f, points: f.points.map(p => xf(m, p.x, p.z)), y: (f.y ?? 0) + dy, surfaceKind: 'stone', tower: t};
      if (f.slope) {const o = xf(m, f.slope.origin.x, f.slope.origin.z), gx = f.slope.x, gz = f.slope.z;out.slope = {x: e[0] * gx + e[8] * gz, z: e[2] * gx + e[10] * gz, origin: o};}
      return out;
    });
  }
  const towerObstacles = t => t.stair.obstacles().map(o => ({points: o.points.map(p => xf(t.matrix, p.x, p.z)), minY: o.minY + t.matrix.elements[13], maxY: o.maxY + t.matrix.elements[13], kind: 'tower'}));
  // Firepit with five benches in the park in front of the building (the fire level is shared in multiplayer).
  const FIRE_POS = {x: -2.7, z: 34.7}, fire = window.GameFire.create({T, lib, heightAt: world.heightWorld, x: FIRE_POS.x, z: FIRE_POS.z});scene.add(fire.group);
  // Three hidden stashes (issue #2): the top landing of a neighbour's stairwell, the courtyard behind the building and the
  // bushes in the park north of the block. Each crate has lines to snort and a syringe: a random 30-second boost.
  const stash = (() => {
    const t = towers.filter(x => !x.own)[4], top = new T.Vector3(.1, 0, 5.38).applyMatrix4(t.matrix), e = t.matrix.elements;
    const at = (name, x, z, yaw) => ({name, x, z, y: world.heightWorld(x, z), yaw});
    return window.GameStash.create({T, lib, places: [{name: 'stairwell', x: top.x, y: top.y, z: top.z, yaw: Math.atan2(e[8], e[0])}, at('courtyard', -2.8, -41.7, .4), at('park', 104.5, 162.2, 1.2)]});
  })();scene.add(stash.group);
  const shops = window.GameShops.create({T, lib, world, audio});scene.add(shops.group);
  // HODET N.N., Marianne Heske's 7 m bronze doll's head at the north end of Torshovdalen (OSM node 2965223021, real place
  // local (9.1, 275.5)), facing south, with its stone steps on the east side.
  const SCULPTURE_AT = {x: 9.1, s: 275.5};
  const sculpture = window.GameSculpture.create({T, lib, world, at: SCULPTURE_AT});scene.add(sculpture.group);
  // Ground: the building site's soil (it changes as the machines dig), the paved paintball yard, the terrain elsewhere.
  const siteField = SITE_DATA && window.GameSite ? window.GameSite.fieldFor(SITE_DATA, world) : null;
  const pbCtx = {};// filled in below, once the player, camera and lobby exist
  const pbRules = PB_DATA && window.GamePaintball ? new window.GamePaintball.Rules(PB_DATA, pbCtx) : null;
  function groundAt(x, z) {
    if (siteField) {const y = siteField.surface(x, z);if (y !== null) return y;}
    if (pbRules) {const y = pbRules.ground(x, z);if (y !== null) return y;}
    return world.heightWorld(x, z);
  }
  const groundFloor = {...world.terrainFloor(), height: p => groundAt(p.x, p.z)};
  const extraObstacles = [...(siteField ? window.GameSite.fenceObstacles(siteField, groundAt) : []), ...(pbRules ? pbRules.obstacles : [])];
  const staticObstacles = [...apt.obstacles, ...towers.flatMap(towerObstacles), ...world.obstacles(), ...fire.obstacles, ...stash.obstacles, ...shops.obstacles, ...sculpture.obstacles, ...extraObstacles];
  const walkFloors = [...apt.floors, ...towers.flatMap(towerFloors), ...shops.floors, ...sculpture.floors, groundFloor];
  const walkWorld = collision.createWorld({floors: walkFloors, obstacles: staticObstacles, radius: .18, clearance: .08});
  const vehicleWorlds = new Map();
  let vehicleBlockers = null;// the other vehicles' footprints from the last updateDynamic()
  function worldFor(r) {
    const key = r.toFixed(2);
    if (!vehicleWorlds.has(key)) {
      const w = collision.createWorld({floors: [groundFloor], obstacles: [...world.obstacles(), ...towers.flatMap(towerObstacles), ...fire.obstacles, ...shops.obstacles, ...sculpture.obstacles, ...extraObstacles], radius: r, clearance: .08});
      // A world made during play (e.g. the bike's on its first ride) gets the other vehicles at once, not one update later.
      if (vehicleBlockers) collision.setDynamic(w, vehicleBlockers);
      vehicleWorlds.set(key, w);
    }
    return vehicleWorlds.get(key);
  }
  const doors = [...apt.doors, ...towers.flatMap(t => t.stair.doors)];
  const shootTargets = [];[apt.group, ownStair.group].forEach(g => g.traverse(o => {if (o.isMesh && !(o.material.transparent) && o.visible) shootTargets.push(o);}));
  const interactables = [];[apt.group, ...towers.map(t => t.stair.group)].forEach(g => g.traverse(o => {if (o.isMesh && (o.userData.doorId || o.userData.lockedDoor)) interactables.push(o);}));
  apt.drinks.forEach(d => d.object.traverse(o => {if (o.isMesh) {o.userData.drink = d;interactables.push(o);}}));
  interactables.push(...stash.hitboxes, ...shops.hitboxes);

  // ---------- Vehicles ----------
  const toWorld = world.toWorld, heightAt = groundAt;
  const localHeadingToYaw = h => {const dx = Math.sin(h), ds = Math.cos(h), wx = dx * Math.cos(theta) + ds * Math.sin(theta), wz = -dx * Math.sin(theta) + ds * Math.cos(theta);return Math.atan2(-wx, -wz);};
  const VH = window.GameVehicles.create({T, lib, audio, heightAt, collision, worldFor});
  const vehicles = [];
  D.vehicles.cars.forEach(c => {const p = toWorld(c.x, c.s);vehicles.push(new VH.Car(p.x, p.z, localHeadingToYaw(c.heading), c.model));});
  {const p = toWorld(D.vehicles.motorcycle.x, D.vehicles.motorcycle.s);vehicles.push(new VH.Motorcycle(p.x, p.z, Math.PI / 2 + .3));}
  {const p = toWorld(D.vehicles.excavator.x, D.vehicles.excavator.s);const ex = new VH.Excavator(p.x, p.z, -.4);vehicles.push(ex);}
  // Aircraft: the helicopter on the grass beside Haarklous plass, the sport plane at the north end of the long grass strip in
  // Torshovdalen (about 280 m clear, nose pointing down the strip).
  const AC = window.GameAircraft.create({T, lib, audio, heightAt, collision, worldFor});
  const AIR = {heli: {x: 26, z: 14, yaw: .6}, plane: {x: -116.5, z: 0, yaw: .5236}};
  const heli = new AC.Helicopter(AIR.heli.x, AIR.heli.z, AIR.heli.yaw), plane = new AC.Plane(AIR.plane.x, AIR.plane.z, AIR.plane.yaw);
  heli.onCrash = plane.onCrash = a => {if (player.vehicle === a) notice('Crashed! The aircraft is put back on its pad in a moment.', 3);};
  vehicles.push(heli, plane);
  const drone = new AC.Drone();
  // More parked cars and motorcycles along the streets within about 330 m. Generated deterministically (seeded random,
  // same checks), so in multiplayer every player has the same vehicles with the same numbers.
  {
    let seed = 1707;const rnd = () => (seed = (seed * 16807) % 2147483647) / 2147483647;
    const kinds = ['residential', 'tertiary', 'secondary', 'unclassified', 'service', 'living_street'], roads = D.roads.filter(r => kinds.includes(r.k));
    const segs = roads.flatMap((r, ri) => {const o = [];for (let k = 0; k + 3 < r.p.length; k += 2) o.push({ri, w: r.w, a: [r.p[k], r.p[k + 1]], b: [r.p[k + 2], r.p[k + 3]]});return o;});
    const segDist = (p, s) => {const dx = s.b[0] - s.a[0], ds = s.b[1] - s.a[1], L = dx * dx + ds * ds || 1, t = Math.max(0, Math.min(1, ((p[0] - s.a[0]) * dx + (p[1] - s.a[1]) * ds) / L));return Math.hypot(p[0] - s.a[0] - dx * t, p[1] - s.a[1] - ds * t);};
    const runway = {a: [AIR.plane.x, AIR.plane.z], b: [AIR.plane.x - Math.sin(AIR.plane.yaw) * 280, AIR.plane.z - Math.cos(AIR.plane.yaw) * 280]};
    const keep = [{x: AIR.heli.x, z: AIR.heli.z, r: 14}, {x: FIRE_POS.x, z: FIRE_POS.z, r: 9}, ...vehicles.map(v => ({x: v.x, z: v.z, r: 7}))];
    const w = worldFor(1.25), bikeColors = [0x16171a, 0x9e1b20, 0xa9afb5, 0x1f3a63, 0xe9e9e4, 0x56657c];let cars = 0, bikes = 0;
    for (const s of segs) {
      const dx = s.b[0] - s.a[0], ds = s.b[1] - s.a[1], L = Math.hypot(dx, ds);if (L < 20) continue;
      for (let t = 9 + rnd() * 10; t < L - 9 && cars + bikes < 32; t += 26 + rnd() * 16) {
        const side = rnd() < .5 ? 1 : -1, ux = dx / L, us = ds / L, off = s.w / 2 + 1.25;
        const lp = [s.a[0] + ux * t - us * off * side, s.a[1] + us * t + ux * off * side];if (Math.hypot(lp[0], lp[1]) > 330) continue;
        if (segs.some(o => o.ri !== s.ri && segDist(lp, o) < 10)) continue;
        const p = toWorld(lp[0], lp[1]), h = heightAt(p.x, p.z);
        if (keep.some(k => Math.hypot(k.x - p.x, k.z - p.z) < k.r) || segDist([p.x, p.z], runway) < 12) continue;
        const yaw = localHeadingToYaw(Math.atan2(ux * side, us * side)), f = {x: -Math.sin(yaw), z: -Math.cos(yaw)};
        if (Math.abs(heightAt(p.x + f.x * 2, p.z + f.z * 2) - heightAt(p.x - f.x * 2, p.z - f.z * 2)) > 1.1) continue;
        if (!collision.canStand(w, p, 1.4, h) || !collision.canStand(w, {x: p.x + f.x * 1.6, z: p.z + f.z * 1.6}, 1.4, h) || !collision.canStand(w, {x: p.x - f.x * 1.6, z: p.z - f.z * 1.6}, 1.4, h)) continue;
        const bike = bikes < 7 && (cars + bikes) % 4 === 3, v = bike ? new VH.Motorcycle(p.x, p.z, yaw + .25, bikeColors[bikes % bikeColors.length]) : new VH.Car(p.x, p.z, yaw, Math.floor(rnd() * 7));
        if (bike) bikes++;else cars++;vehicles.push(v);keep.push({x: p.x, z: p.z, r: 7});
      }
    }
  }
  const dirtPiles = [];
  const pile = (x, z, r) => {const m = new T.Mesh(new T.ConeGeometry(r, r * .7, 14), lib.concrete(0x6b5541));m.position.set(x, heightAt(x, z) + r * .3, z);m.castShadow = m.receiveShadow = true;scene.add(m);dirtPiles.push(m);};
  // The construction site's machines come after the street vehicles, so the older vehicle numbers stay the same online.
  // Soil a machine puts down outside the site makes a heap like the street excavator's (a cone of that volume).
  let spilled = 0;
  const site = siteField && window.SiteModels ? window.GameSite.create({T, lib, audio, VH, models: window.SiteModels.create({T, lib}), data: SITE_DATA, world, field: siteField, groundAt, surfaceAt: world.surfaceAt,
    onSpill: (x, z, vol) => {spilled += vol;if (spilled < .4) return;pile(x, z, Math.max(.4, Math.min(2.2, Math.cbrt(spilled * 3 / (Math.PI * .7)))));spilled = 0;}}) : null;
  if (site) {scene.add(site.group);vehicles.push(...site.machines);}
  vehicles.forEach(v => {v.spawn = v.netGet();});
  vehicles.forEach(v => scene.add(v.group));
  vehicles.filter(v => v.kind === 'excavator').forEach(ex => {
    const f = ex.forward();pile(ex.x + f.x * 6, ex.z + f.z * 6, 1.4);ex.onDump = (x, z) => pile(x, z, .55);
  });

  // ---------- HUD ----------
  const hud = {
    discgolf(s) {const el = $('discHud');if (!s) {el.hidden = true;return;}el.hidden = false;el.innerHTML = `<b>${s.course} · Hull ${s.hole}/${s.holes} · Par ${s.par}</b><span>Throws: ${s.strokes.map((n, i) => `H${i + 1} ${n}`).join(' · ')}</span><span>${s.distance} m to the basket</span><i class="meter"><u style="width:${Math.round(s.charge * 100)}%"></u></i><small>${s.message}</small><small>Hold R2 / left mouse, release to throw · left stick ←/→ hyzer · ○ quit</small>`;},
    petanque(s) {
      const el = $('petanqueHud'), marks = $('markers');if (!s) {el.hidden = true;marks.replaceChildren();return;}el.hidden = false;
      el.innerHTML = `<b>Petanque · You ${s.score.you} – ${s.score.npc} ${s.npcName}</b><span>Mène ${s.mene} · Boules left: you ${s.you}, ${s.npcName} ${s.npc}</span><span>${s.hold ? (s.hold === 'you' ? `You hold the point by ${s.margin.toFixed(2)} m` : `${s.npcName} holds the point by ${s.margin.toFixed(2)} m`) : 'Nobody holds the point'}</span><span>${s.turn === 'you' ? `Your turn: ${s.aim === 'jack' ? 'throw the jack (6–10 m)' : (s.shoot ? 'shoot (L2 held)' : 'point (hold L2 to shoot)')}` : s.turn === 'npc' ? `${s.npcName} is throwing…` : ''}</span><i class="meter"><u style="width:${Math.round(s.charge * 100)}%"></u></i><small>${s.message}</small><small>Hold R2 / left mouse for power, release to throw · ○ quit</small>`;
      marks.replaceChildren(...s.markers.map(m => {const v = m.pos.clone();v.y += .25;v.project(camera);const d = document.createElement('div');d.className = 'marker ' + m.owner + (m.best ? ' best' : '');d.textContent = m.label;d.style.left = `${(v.x * .5 + .5) * 100}%`;d.style.top = `${(-v.y * .5 + .5) * 100}%`;d.hidden = v.z > 1;return d;}));
    }
  };
  function notice(text, seconds = 3) {const el = $('notice');el.textContent = text;el.hidden = !text;clearTimeout(notice.t);if (text) notice.t = setTimeout(() => {el.hidden = true;}, seconds * 1000);}
  const prompt = text => {const el = $('prompt');if (el.textContent !== (text || '')) el.textContent = text || '';el.hidden = !text;};

  // ---------- Player ----------
  const player = {x: 0, z: 0, feet: D.ground, yaw: 0, pitch: 0, stance: 'stand', eye: 1.65, eyeNow: 1.65, jumpY: 0, jumpV: 0, run: false, stride: 0, mode: 'foot', vehicle: null, minigame: null};
  const stanceEye = s => s === 'prone' ? .32 : s === 'crouch' ? 1.05 : player.eye;
  const WALK_SPEED = 3.5;
  const stanceSpeed = () => player.stance === 'prone' ? .28 : player.stance === 'crouch' ? .55 : 1;
  function place(x, z, yaw, y) {
    const sp = collision.findSafeSpawn(walkWorld, {x, z}, stanceEye('stand'), {y: y ?? heightAt(x, z), maxDistance: 6});
    player.x = sp ? sp.x : x;player.z = sp ? sp.z : z;player.feet = sp ? sp.y : (y ?? heightAt(x, z));if (yaw !== undefined) player.yaw = yaw;player.pitch = 0;player.stance = 'stand';player.eyeNow = player.eye;player.jumpY = player.jumpV = 0;
  }
  // The hostage mission starts from its own marker on the grass beside the entrance, so the street door is a normal door.
  const MISSION_MARK = {x: 4.8, z: 14.2};
  {
    const y = world.heightWorld(MISSION_MARK.x, MISSION_MARK.z), g = new T.Group();g.position.set(MISSION_MARK.x, y, MISSION_MARK.z);scene.add(g);
    const ring = new T.Mesh(new T.TorusGeometry(1.1, .05, 8, 48), new T.MeshStandardMaterial({color: 0xb71c1c, emissive: 0xb71c1c, emissiveIntensity: .8}));ring.rotation.x = -Math.PI / 2;ring.position.y = .04;g.add(ring);
    const post = new T.Mesh(new T.CylinderGeometry(.04, .04, 1.4, 10), lib.metal(0x5c6166, .4));post.position.set(0, .7, -1.25);post.castShadow = true;g.add(post);
    const c = document.createElement('canvas');c.width = 512;c.height = 256;const x = c.getContext('2d');x.fillStyle = '#7f1010';x.fillRect(0, 0, 512, 256);x.strokeStyle = '#fff';x.lineWidth = 10;x.strokeRect(10, 10, 492, 236);
    x.fillStyle = '#fff';x.textAlign = 'center';x.font = 'bold 64px Segoe UI, Arial, sans-serif';x.fillText('HOSTAGE', 256, 100);x.fillText('RESCUE', 256, 168);x.font = '600 34px Segoe UI, Arial, sans-serif';x.fillText('stand in the ring · □ start', 256, 222);
    const tex = new T.CanvasTexture(c);tex.colorSpace = T.SRGBColorSpace;const sign = new T.Mesh(new T.PlaneGeometry(.9, .45), new T.MeshStandardMaterial({map: tex, side: T.DoubleSide, roughness: .6}));sign.position.set(0, 1.5, -1.25);sign.castShadow = true;g.add(sign);
    MISSION_MARK.ring = ring;
  }
  const missionStart = () => {const p = toWorld(...D.vehicles.missionStart);return {x: p.x, z: p.z, y: heightAt(p.x, p.z), yaw: Math.atan2(-(ownStair.doorOutside.x - p.x), -(ownStair.doorOutside.z - p.z))};};
  {const s = missionStart(), f = {x: Math.sin(s.yaw), z: Math.cos(s.yaw)};place(s.x + f.x * 4, s.z + f.z * 4, s.yaw);}
  function setStance(next) {
    if (next === player.stance) return;
    if (stanceEye(next) > stanceEye(player.stance) && !collision.canStand(walkWorld, player, stanceEye(next), player.feet)) {notice('No room to stand up here.');return;}
    player.stance = next;
  }
  const move = (dx, dz) => {const n = collision.move(walkWorld, player, {x: dx, z: dz}, stanceEye(player.stance), player.feet);player.x = n.x;player.z = n.z;player.feet = n.y;};
  function inApartment() {return player.feet > -.5 && player.feet < 2.5 && apt.floors.some(f => inside(player, f.points));}
  // Ambience: the whole flat (also doorways and wall gaps) counts as indoors; the balcony is open air.
  const aptBox = (() => {const pts = apt.floors.filter(f => !/balcony/i.test(f.id || f.name || '')).flatMap(f => f.points);return {x0: Math.min(...pts.map(p => p.x)) - .15, x1: Math.max(...pts.map(p => p.x)) + .15, z0: Math.min(...pts.map(p => p.z)) - .15, z1: Math.max(...pts.map(p => p.z)) + .15};})();
  function inFlat() {return player.feet > -.5 && player.feet < 2.5 && player.x > aptBox.x0 && player.x < aptBox.x1 && player.z > aptBox.z0 && player.z < aptBox.z1;}
  function towerAt(x, z, feet) {for (const t of towers) {const l = new T.Vector3(x, 0, z).applyMatrix4(t.inverse);if (l.x > -1.45 && l.x < 1.665 && l.z > 4.9 && l.z < 11.12 && feet > t.matrix.elements[13] - 10.5 && feet < t.matrix.elements[13] + 2.7) return {t, l};}return null;}
  function inside(p, pts) {let r = false;for (let i = 0, j = pts.length - 1; i < pts.length; j = i++) {const a = pts[i], b = pts[j];if ((a.z > p.z) !== (b.z > p.z) && p.x < (b.x - a.x) * (p.z - a.z) / (b.z - a.z) + a.x) r = !r;}return r;}
  function roomAt() {const r = apt.rooms.find(r => inside(player, r.polygon.map(apt.xy).map(([x, z]) => ({x, z}))));return r?.id;}

  // ---------- Doors (animated) and dynamic obstacles ----------
  const doorPolygon = d => apt.meshObstacle(d.leaf);
  function setDoor(d, t) {if (d.motion === 'slide') d.hinge.position.x = d.closedX + d.travel * t;else d.hinge.rotation.y = d.openAngle * t;d.hinge.updateMatrixWorld(true);}
  function polygonsOverlap(a, b) {for (const poly of [a, b]) for (let i = 0; i < poly.length; i++) {const p = poly[i], q = poly[(i + 1) % poly.length], nx = -(q.z - p.z), nz = q.x - p.x, aa = a.map(v => v.x * nx + v.z * nz), bb = b.map(v => v.x * nx + v.z * nz);if (Math.max(...aa) <= Math.min(...bb) + 1e-8 || Math.max(...bb) <= Math.min(...aa) + 1e-8) return false;}return true;}
  function toggleDoor(d) {
    if (d.anim) return;
    const target = d.open ? 0 : 1;setDoor(d, target);const poly = doorPolygon(d);setDoor(d, d.open ? 1 : 0);
    const me = [{x: player.x - .2, z: player.z - .2}, {x: player.x + .2, z: player.z - .2}, {x: player.x + .2, z: player.z + .2}, {x: player.x - .2, z: player.z + .2}];
    if (poly.maxY > player.feet && poly.minY < player.feet + 1.8 && polygonsOverlap(poly.points, me)) {notice('Step back from the door, then try again.');return;}
    if (caseGame?.active && caseGame.actorObstacles().some(o => polygonsOverlap(poly.points, o.points))) {notice('Someone is standing in the door’s path.');return;}
    d.anim = {from: d.open ? 1 : 0, to: target, t: 0};audio.sfx(target ? 'doorOpen' : 'doorClose', {reverb: towerAt(player.x, player.z, player.feet) ? .6 : .1});if (mp) mp.sendDoor(d.id, target === 1);
  }
  function closeAllDoors() {doors.forEach(d => {d.anim = null;d.open = false;setDoor(d, 0);});updateDynamic();}
  function updateDynamic() {
    const list = doors.filter(d => !d.anim).map(doorPolygon);
    vehicles.forEach(v => {if (v !== player.vehicle) list.push(...[].concat(v.footprint()));});
    if (caseGame?.active) list.push(...caseGame.actorObstacles());
    collision.setDynamic(walkWorld, list);
    // Other vehicles block the one being driven; it must never collide with its own (parked) footprint.
    const vlist = vehicles.filter(v => v !== player.vehicle).flatMap(v => [].concat(v.footprint()));
    vehicleBlockers = vlist;vehicleWorlds.forEach(w => collision.setDynamic(w, vlist));
  }
  updateDynamic();

  // ---------- Mission (hostage rescue) ----------
  const sensitivity = {pad: 1, mouse: 1};
  try {const saved = JSON.parse(localStorage.getItem('haarklous-look-sensitivity-v1') || 'null');if (saved) Object.assign(sensitivity, saved);} catch {}
  const setSens = (k, v) => {sensitivity[k] = Math.round(Math.min(3, Math.max(.25, v)) * 100) / 100;try {localStorage.setItem('haarklous-look-sensitivity-v1', JSON.stringify(sensitivity));} catch {}};
  if (window.CaseGame) {
    const stairSpawns = () => ownStair.floors().filter(f => !f.slope && f.name !== 'Street door threshold' && !/Half landing 0/.test(f.name)).map(f => ({name: f.name, points: f.points, y: f.y}));
    caseGame = window.CaseGame.mount({T, scene, renderer, viewport: view, collision, meshObstacle: apt.meshObstacle, closeAllDoors, rebuildWalkWorld: updateDynamic,
      camera: () => camera, walkWorld: () => walkWorld, doors: () => apt.doors, isFirstPerson: () => true, render: () => {}, shootables: () => shootTargets,
      lib, stance: () => player.stance, useDoorAhead: () => interact(true),
      sensitivity: {min: .25, max: 3, get: () => ({...sensitivity}), set: setSens, step: (k, d) => setSens(k, sensitivity[k] + d * .1)},
      spawnAreas: () => apt.spawnAreas, stairSpawns, missionStart,
      entrance: () => {const d = apt.doors.find(d => d.name === 'Entrance door');return d ? d.leaf.getWorldPosition(new T.Vector3()) : {x: 1.66, z: 5.83};},
      timeScale: () => worldScale(),
      onFinish: result => {if (result === 'success') earn(400, 'Reward for rescuing the hostage');},
      setPose: (x, z, yaw, tilt, y) => place(x, z, yaw, y), enterWalk: () => {}, enterImmersive: () => {},
      leave: () => {const yaw = player.yaw;place(player.x, player.z, yaw, player.feet);updateDynamic();notice(inApartment() ? 'Mission over – you are home. Make yourself comfortable.' : 'Mission over.', 4);}});
  }

  // ---------- Minigames, photo camera, map ----------
  const mini = window.GameMinigames.create({T, lib, audio, heightAt, toWorld, data: D, hud, trees: world.trees, reward: (v, why) => earn(v, why), muselunden: window.MUSELUNDEN});
  const discCourses = [mini.discgolf, mini.muselunden].filter(Boolean);
  scene.add(...discCourses.map(g => g.group), mini.petanque.group);
  const photo = window.GameCamera.create({T, lib, audio, root: view});
  const overlayScene = new T.Scene(), overlayCamera = new T.PerspectiveCamera(55, 1, .01, 5);overlayScene.add(new T.HemisphereLight(0xf1f5ff, 0x4a4540, 2.2));
  const ovSun = new T.DirectionalLight(0xffffff, 1.8);ovSun.position.set(.6, 1, .5);overlayScene.add(ovSun);overlayScene.environment = lib.environment();
  overlayScene.add(photo.model);photo.model.position.set(.08, -.13, -.32);photo.model.rotation.set(.1, -.15, 0);
  const homeLocal = world.toLocal(2.8, 5), caseLocal = D.vehicles.missionStart;let mp = null;
  const map = window.GameMap.create({data: D, toLocal: world.toLocal, toWorld, root: view, poi: () => [
    {x: homeLocal.x, s: homeLocal.s, color: '#e0703c', label: 'Home · 4th floor', glyph: '⌂'},
    {...world.toLocal(4.8, 14.2), color: '#b71c1c', label: 'Hostage rescue', glyph: '◎'},
    ...(D.discgolf || []).map((h, i) => ({x: h.tee[0], s: h.tee[1], color: '#6a1b9a', label: `Disc golf ${i + 1}`, glyph: '◉'})),
    ...(D.discgolf || []).map(h => ({x: h.basket[0], s: h.basket[1], color: '#9c4dcc', label: '', glyph: '⛳'})),
    ...(window.MUSELUNDEN ? window.MUSELUNDEN.holes.map(h => ({x: h.tee[0], s: h.tee[1], color: '#6a1b9a', label: h.n === 1 ? 'Muselunden disc golf 1' : `${h.n}`, glyph: '◉'})) : []),
    ...(window.MUSELUNDEN ? window.MUSELUNDEN.holes.map(h => ({x: h.basket[0], s: h.basket[1], color: '#9c4dcc', label: '', glyph: '⛳'})) : []),
    ...(D.petanque ? [{x: D.petanque.x, s: D.petanque.s, color: '#00838f', label: 'Petanque', glyph: '●'}] : []),
    {...world.toLocal(AIR.heli.x, AIR.heli.z), color: '#1565c0', label: 'Helipad', glyph: 'H'},
    {...world.toLocal(AIR.plane.x, AIR.plane.z), color: '#1565c0', label: 'Airstrip', glyph: '✈'},
    {...world.toLocal(FIRE_POS.x, FIRE_POS.z), color: '#e65100', label: 'Firepit', glyph: '🔥'},
    ...shops.shops.map(s => {const L = s.local;return {x: L.P0[0] + L.u[0] * L.W / 2 + L.n[0] * L.depth / 2, s: L.P0[1] + L.u[1] * L.W / 2 + L.n[1] * L.depth / 2, color: s.id === 'kiwi' ? '#2e9a3f' : '#d0021b', label: s.name, glyph: '🛒'};}),
    ...(SITE_DATA ? [{x: SITE_DATA.rect.x0 + SITE_DATA.rect.w / 2, s: SITE_DATA.rect.s0 + SITE_DATA.rect.d / 2, color: '#f9a825', label: 'Construction site', glyph: '⛏'}] : []),
    ...(PB_DATA ? [{x: PB_DATA.centre.x, s: PB_DATA.centre.s, color: '#2e7d32', label: 'Paintball', glyph: '✚'}] : []),
    {x: SCULPTURE_AT.x, s: SCULPTURE_AT.s, color: '#7a5c3e', label: 'HODET N.N.', glyph: '◉'},
    ...(mp ? [...mp.remotes.values()].filter(r => r.seen).map(r => ({...world.toLocal(r.pos.x, r.pos.z), color: window.GameAvatars.slotCss(r.slot), label: r.name, glyph: '●'})) : [])]});
  // Wristwatch (D-pad ↑) shares the first-person overlay with the photo camera.
  const watch = window.GameWatch.create({T, lib});overlayScene.add(watch.model);
  // Paintball course, lobby and games (the rules' view of the game; online the others' states come from the lobby).
  Object.assign(pbCtx, {
    serverNow: () => mp && mp.net.online ? mp.net.serverNow() : Date.now(), you: () => mp && mp.net.online ? mp.net.you : 0, yourName: () => mp ? mp.net.name : 'You', yourSlot: () => mp ? mp.net.slot : 0,
    others: () => mp ? [...mp.remotes.values()].filter(r => r.st && r.st.pb && r.seen).map(r => window.GamePaintball.memberFromJson(r.st.pb, {id: r.id, name: r.name, slot: r.slot, local: false, feet: {x: r.pos.x, y: r.pos.y, z: r.pos.z}, yaw: r.yaw, stance: r.st.st || 'stand'})).filter(Boolean) : [],
    feet: () => ({x: player.x, y: player.feet, z: player.z}), yaw: () => player.yaw, stance: () => player.stance,
    teleport: (x, z, yaw) => {if (player.mode === 'foot') place(x, z, yaw);}, cameraPos: () => camera.position.clone(), cameraFwd: () => camera.getWorldDirection(new T.Vector3()),
    ground: groundAt, notice: (t, sec) => notice(t, sec), sound: (n, g) => audio.sfx(n, {gain: g}), random: Math.random});
  const paintball = pbRules && window.PaintballModels ? window.GamePaintball.create({T, scene, overlay: overlayScene, models: window.PaintballModels.create({T, lib, data: PB_DATA}), data: PB_DATA, view, groundAt, ctx: pbCtx, rules: pbRules}) : null;
  const pbBusy = () => !!paintball && paintball.busy, pbArmed = () => !!paintball && paintball.armed;

  // ---------- Drinking ----------
  let drunk = 0;// seconds of drunkenness remaining
  const drunkLevel = () => Math.min(1, drunk / 90);
  function drink(d) {
    if (d.kind === 'beer') {d.object.visible = false;d.used = true;audio.sfx('bottleOpen');audio.sfx('gulp');drunk += 30;wallet.addPant(1);notice('You drink a beer. (+30 s drunk, +1 bottle for the deposit)');}
    else {if (d.sips <= 0) {notice('The bottle is empty.');return;}d.sips--;audio.sfx('gulp');drunk += 30;notice(`A sip of ${d.name}. (+30 s drunk) · ${d.sips} sips left`);const l = d.object.userData.liquid;if (l) l.scale.y = Math.max(.05, d.sips / 12);}
  }

  // ---------- Interaction (□ / E) ----------
  const ray = new T.Raycaster();
  let promptAction = null;
  function lookTarget(range = 2.6) {
    ray.setFromCamera(new T.Vector2(0, 0), camera);ray.far = range;
    const hit = ray.intersectObjects(interactables.filter(o => {for (let p = o; p; p = p.parent) if (!p.visible) return false;return true;}), false)[0];
    return hit ? hit.object : null;
  }
  function nearestVehicle() {
    let best = null, bd = 2.6;
    for (const v of vehicles) {const f = v.forward(), dx = player.x - v.x, dz = player.z - v.z, along = dx * f.x + dz * f.z, side = dx * -f.z + dz * f.x, d = Math.hypot(Math.max(0, Math.abs(along) - v.length / 2), Math.max(0, Math.abs(side) - v.width / 2));if (d < bd && Math.abs(player.feet - v.y) < 2) {bd = d;best = v;}}
    return best;
  }
  function findPrompt() {
    if (player.mode === 'vehicle') {const v = player.vehicle;return (v.aircraft ? v.grounded && v.speed < 3 : Math.abs(v.speed) < 3) ? {text: `□ ${v.kind === 'motorcycle' ? 'Get off' : 'Get out'}`, act: () => exitVehicle()} : null;}
    if (player.mode === 'passenger') {const v = player.vehicle;return (v.aircraft ? v.grounded && v.speed < 3 : Math.abs(v.speed) < 3) ? {text: `□ ${v.kind === 'motorcycle' ? 'Get off' : 'Get out'}`, act: () => exitPassenger()} : null;}
    if (player.mode === 'sit') return {text: '□ Stand up · R2 have a beer', act: () => standUp()};
    // During the mission only doors can be used (□ otherwise reloads).
    if (caseGame?.active) {const o = lookTarget();if (o?.userData.doorId) {const d = doors.find(d => d.id === o.userData.doorId);if (d) return {text: `□ ${d.open ? 'Close' : 'Open'} ${d.name.toLowerCase()}`, act: () => toggleDoor(d)};}if (o?.userData.lockedDoor) return {text: '□ Try the door', act: () => {audio.sfx('locked');notice('Locked.');}};return null;}
    // Doors first (the street door of 17A opens normally), then the mission marker beside the entrance.
    {const o = lookTarget();if (o?.userData.doorId) {const d = doors.find(d => d.id === o.userData.doorId);if (d) return {text: `□ ${d.open ? 'Close' : 'Open'} ${d.name.toLowerCase()}`, act: () => toggleDoor(d)};}}
    if (Math.hypot(player.x - MISSION_MARK.x, player.z - MISSION_MARK.z) < 1.6 && Math.abs(player.feet - world.heightWorld(MISSION_MARK.x, MISSION_MARK.z)) < 2) return {text: '□ Start mission: rescue the hostage (4th floor)', act: () => startMission()};
    const shp = shopPrompt();if (shp) return shp;
    const fp = firePrompt();if (fp) return fp;
    const sp = stashPrompt();if (sp) return sp;
    const v = nearestVehicle();
    if (v) {
      const i = vehicles.indexOf(v), drv = mp.occupant(i, 'd'), pas = mp.occupant(i, 'p'), canRide = !!v.hipAnchor('p');
      if (!drv) return {text: `□ ${v.verb ? v.verb : v.kind === 'motorcycle' ? 'Ride the' : v.kind === 'excavator' ? 'Climb into the' : v.aircraft ? 'Fly the' : 'Drive the'} ${v.name}`, act: () => tryDrive(v)};
      if (canRide && !pas) return {text: `□ Ride along with ${mp.nameOf(drv)} (${v.kind === 'motorcycle' ? 'pillion' : 'passenger'})`, act: () => tryRide(v)};
      return {text: `${mp.nameOf(drv)} is ${v.aircraft ? 'flying' : 'driving'} the ${v.name}`, act: () => {}};
    }
    for (const g of [...discCourses, mini.petanque]) {const t = g.near({x: player.x, z: player.z, y: player.feet});if (t) return {text: t, act: () => startMinigame(g)};}
    const o = lookTarget();
    if (o?.userData.drink && !o.userData.drink.used) {const d = o.userData.drink;return {text: d.kind === 'beer' ? '□ Drink a beer' : `□ Take a sip of ${d.name}`, act: () => drink(d)};}
    if (o?.userData.doorId) {const d = doors.find(d => d.id === o.userData.doorId);if (d) return {text: `□ ${d.open ? 'Close' : 'Open'} ${d.name.toLowerCase()}`, act: () => toggleDoor(d)};}
    if (o?.userData.lockedDoor) return {text: '□ Try the door', act: () => {audio.sfx('locked');notice('Locked. Someone lives here.');}};
    return null;
  }
  function interact(silent = false) {const p = findPrompt();if (p) {p.act();return true;}if (!silent) notice('Nothing to use here.', 1.5);return false;}

  // ---------- Vehicles: enter / exit ----------
  const AIR_HELP = {
    helicopter: 'Helicopter (Mode 2) · left stick ↑↓ collective (centre = hover), ←→ pedals · right stick pitch/roll · R3 / V chase camera · the rotor needs a few seconds to spool up',
    plane: 'Plane (Mode 2) · left stick ↑ to add throttle (it stays), ←→ rudder/nose wheel · right stick pitch/roll · pull back at about 85 km/h · L2 brakes · R3 / V view'};
  function setClip(near, far) {if (camera.near !== near || camera.far !== far) {camera.near = near;camera.far = far;camera.updateProjectionMatrix();}}
  function enterVehicle(v) {watch.hide();player.mode = 'vehicle';player.vehicle = v;player.siteView = 0;player.lookYaw = 0;player.lookPitch = 0;player.chase = false;player.chaseCam = null;v.enter();updateDynamic();if (v.aircraft) setClip(.12, 1600);notice(v.help ? v.help : v.aircraft ? AIR_HELP[v.kind] : v.kind === 'excavator' ? 'Tracks: L1/L2 left, R1/R2 right · Left stick: swing / stick · Right stick: boom / bucket · □ get out' : 'R2 throttle · L2 brake / reverse · Left stick steer · Right stick look · □ get off', v.aircraft || v.help ? 9 : 6);}
  function tryDrive(v) {
    const i = vehicles.indexOf(v);
    mp.claim(i, 'd').then(r => {if (player.mode !== 'foot') {if (r.ok) mp.release(i, 'd');return;}if (r.ok) enterVehicle(v);else notice(r.timeout ? 'No answer from the lobby – try again.' : `${mp.nameOf(r.by)} just took it.`, 3);});
  }
  function tryRide(v) {
    const i = vehicles.indexOf(v);
    mp.claim(i, 'p').then(r => {if (player.mode !== 'foot') {if (r.ok) mp.release(i, 'p');return;}if (r.ok) enterPassenger(v);else notice(r.timeout ? 'No answer from the lobby – try again.' : 'That seat is taken.', 3);});
  }
  let passengerEngine = null;
  function enterPassenger(v) {
    watch.hide();if (photo.active) photo.toggle();player.mode = 'passenger';player.vehicle = v;player.lookYaw = 0;player.lookPitch = 0;if (v.aircraft) setClip(.12, 1600);
    passengerEngine = audio.engine({motorcycle: 'bike', car: 'car', helicopter: 'heli', plane: 'plane'}[v.kind] || 'car');
    notice(`Riding along with ${mp.nameOf(mp.occupant(vehicles.indexOf(v), 'd'))} · look around with the right stick · □ get out when stopped`, 5);
  }
  function exitPassenger() {
    const v = player.vehicle, i = vehicles.indexOf(v);mp.release(i, 'p');if (passengerEngine) passengerEngine.stop();passengerEngine = null;
    player.mode = 'foot';player.vehicle = null;setClip(.05, 450);$('speedo').classList.remove('air');updateDynamic();
    const f = v.forward(), r = {x: -f.z, z: f.x}, d = (v.width || 1.8) / 2 + .7, p = v.aircraft ? {x: v.x + r.x * 1.8 - f.x * 1.8, z: v.z + r.z * 1.8 - f.z * 1.8} : {x: v.x + r.x * d, z: v.z + r.z * d};
    const sp = collision.findSafeSpawn(walkWorld, p, player.eye, {y: heightAt(p.x, p.z), maxDistance: 6});if (sp) {player.x = sp.x;player.z = sp.z;player.feet = sp.y;}player.yaw = v.yaw;player.pitch = 0;
  }
  // Firepit: throw logs, take a beer, sit on the benches.
  let beerT = 0;
  // ---------- Stash boosts (30 s, random): 3× running speed, 3× jump height or bullet time ----------
  const BOOSTS = {speed: '⚡ 3× running speed', jump: '🦘 3× jump height', slowmo: '⏳ Bullet time – the world slows down'};
  let boost = null;
  const boostIs = k => !!boost && boost.kind === k;
  const worldScale = () => boostIs('slowmo') ? .35 : 1;
  function stashPrompt() {
    if (player.mode !== 'foot') return null;
    const o = lookTarget(), it = o?.userData.stashItem || stash.nearest(player, player.feet);if (!it) return null;
    if (!it.ready) return {text: 'Nothing left here – come back later', act: () => {}};
    return {text: it.kind === 'line' ? '□ Snort a line' : '□ Use the needle', act: () => takeBoost(it)};
  }
  function takeBoost(it) {
    stash.use(it);const kinds = Object.keys(BOOSTS), kind = kinds[Math.floor(Math.random() * kinds.length)];
    boost = {kind, t: 30};audio.sfx(it.kind === 'line' ? 'sniff' : 'needle');audio.muffle(kind === 'slowmo');
    notice(`${BOOSTS[kind]} for 30 seconds!`, 4);
  }
  // ---------- Shops (Joker and Kiwi) and money in kroner ----------
  const wallet = shops.wallet, KR = shops.kr, GREET = ['Hei! Velkommen inn.', 'Hei hei!', 'God dag!', 'Hei, si fra om du trenger hjelp.'];
  let basket = [], walletT = 0, lastShop = null, bag = null;
  function earn(amount, why) {if (!(amount > 0)) return;wallet.earn(amount);walletT = 6;audio.sfx('coin');notice(`+${KR(amount)} · ${why}`, 4);}
  function shopPrompt() {
    if (player.mode !== 'foot') return null;
    const s = shops.shopAt(player, player.feet), o = lookTarget(), u = (o && o.userData) || {};
    if (u.shopItem) {const {shop, product} = u.shopItem, price = shops.priceOf(shop, product);return {text: `□ Take ${product.name} – ${KR(price)}`, act: () => takeProduct(shop, product, price)};}
    if (u.shopPant) {const n = wallet.pant;return n ? {text: `□ Return ${n} empty bottle${n > 1 ? 's' : ''} – ${KR(n * shops.PANT)}`, act: () => returnPant()} : {text: 'Bottle return – no empty bottles yet', act: () => {}};}
    if (s && (u.shopTill || shops.atTill(s, player))) {
      const mine = basket.filter(b => b.shop === s), total = mine.reduce((t, b) => t + b.price, 0);
      return mine.length ? {text: `□ Pay ${KR(total)} (${mine.length} item${mine.length > 1 ? 's' : ''})`, act: () => payAt(s)} : {text: '□ Talk to the cashier', act: () => s.say(GREET[Math.floor(Math.random() * GREET.length)])};
    }
    return null;
  }
  function takeProduct(shop, product, price = shops.priceOf(shop, product)) {basket.push({shop, product, price});audio.sfx('click');walletT = 8;notice(`${product.name} in the basket – pay at the till`, 2);}
  function returnPant() {const n = wallet.cashPant();audio.sfx('pant');walletT = 6;notice(`The machine takes ${n} bottle${n > 1 ? 's' : ''}: +${KR(n * shops.PANT)}`, 3);}
  function payAt(s) {
    const mine = basket.filter(b => b.shop === s), beerOk = shops.beerAllowed(new Date());
    const refused = beerOk ? [] : mine.filter(b => b.product.beer), buy = mine.filter(b => !refused.includes(b)), total = Math.round(buy.reduce((t, b) => t + b.price, 0) * 100) / 100;
    if (!buy.length) {basket = basket.filter(b => b.shop !== s);s.say('Beklager, jeg kan ikke selge øl nå. Ølsalget er man–fre 08–20 og lør 08–18.');notice('No beer sales now (Norwegian law: Mon–Fri 08–20, Sat 08–18, never on Sunday). The beer goes back.', 5);return;}
    if (!wallet.pay(total)) {s.say(`Kortet ble avvist – du mangler ${KR(total - wallet.money)}.`);audio.sfx('locked');notice(`Card declined: you have ${KR(wallet.money)}, the total is ${KR(total)}.`, 4);return;}
    buy.forEach(b => wallet.add(b.product.id));basket = basket.filter(b => b.shop !== s);
    s.say(refused.length ? `Det blir ${KR(total)}. Ølet må jeg ta tilbake – ølsalget er stengt nå.` : `Det blir ${KR(total)}. Takk for handelen!`);
    audio.sfx('register');walletT = 8;notice(`Paid ${KR(total)}${refused.length ? ' (no beer at this hour)' : ''} · D-pad ↓ or the ↓ key opens your bag`, 4);
  }
  function shopsTick(dt) {
    shops.update(dt, player, camera);walletT = Math.max(0, walletT - dt);
    const s = player.mode === 'foot' ? shops.shopAt(player, player.feet) : null;
    if (s && s !== lastShop) s.say(GREET[Math.floor(Math.random() * 2)]);
    // Walking out with unpaid items sets off the alarm, and the items go back on the shelves.
    if (lastShop && s !== lastShop && basket.some(b => b.shop === lastShop)) {audio.sfx('alarm');basket = basket.filter(b => b.shop !== lastShop);notice('BEEP BEEP – the alarm at the door goes off. You put the unpaid items back.', 4);}
    lastShop = s;if (bag && player.mode !== 'foot') {bag = null;renderBag();}
    const show = s || basket.length || bag || walletT > 0, w = $('wallet');w.hidden = !show;if (show) w.textContent = `💰 ${KR(wallet.money)} · 🍾 ${wallet.pant}`;
    const bk = $('basket');bk.hidden = !basket.length;
    if (basket.length) {const total = basket.reduce((t, b) => t + b.price, 0);bk.innerHTML = '<b>Basket</b>' + basket.map(b => `<div>${b.product.name}<span>${KR(b.price)}</span></div>`).join('') + `<div class="total">Total<span>${KR(total)}</span></div><small>Pay at the till</small>`;}
  }
  // The bag: what you have bought (D-pad ↓ / ↓ key on foot).
  const USE = {beer: 'drink: +30 s drunk, a bottle for the deposit', soda: 'drink: a bottle for the deposit', energy: 'drink: 15 s of 3× speed', food: 'eat: sobers you up', wood: 'empty onto the firepit (+3)'};
  const bagItems = () => Object.entries(wallet.bag).map(([id, n]) => ({p: shops.products.find(x => x.id === id), n})).filter(e => e.p);
  function openBag() {bag = {sel: 0, stick: false};watch.hide();renderBag();}
  function renderBag() {
    const el = $('bag');el.hidden = !bag;if (!bag) return;const items = bagItems();bag.sel = Math.max(0, Math.min(bag.sel, items.length - 1));
    el.innerHTML = `<b>Bag · ${KR(wallet.money)} · ${wallet.pant} empty bottle${wallet.pant === 1 ? '' : 's'}</b>` + (items.length ? items.map((e, i) => `<div class="${i === bag.sel ? 'sel' : ''}">${e.n} × ${e.p.name}<small>${USE[e.p.use]}</small></div>`).join('') : '<div>Empty – buy something at Joker or Kiwi</div>') + '<small>↑↓ choose · ✕ / Space use · ○ / C close</small>';
    // A long bag scrolls so the chosen item stays in view.
    const sel = el.querySelector('.sel');if (sel) {const t = sel.offsetTop - 34, b = sel.offsetTop + sel.offsetHeight + 30;if (t < el.scrollTop) el.scrollTop = t;else if (b > el.scrollTop + el.clientHeight) el.scrollTop = b - el.clientHeight;}
  }
  function bagInput(input) {
    const y = input.move.y, items = bagItems();
    if (input.pressed.has('up') || (y < -.5 && !bag.stick)) bag.sel = Math.max(0, bag.sel - 1);
    if (input.pressed.has('down') || (y > .5 && !bag.stick)) bag.sel = Math.min(Math.max(0, items.length - 1), bag.sel + 1);
    bag.stick = Math.abs(y) > .5;
    if (input.pressed.has('cross') && items[bag.sel]) useItem(items[bag.sel].p);
    if (input.pressed.has('circle')) bag = null;
    renderBag();input.move.x = input.move.y = 0;['cross', 'circle', 'up', 'down', 'square', 'triangle'].forEach(b => input.pressed.delete(b));
  }
  function useItem(p) {
    if (p.use === 'wood') {
      if (Math.hypot(player.x - FIRE_POS.x, player.z - FIRE_POS.z) > 8) {notice('Take the firewood to the firepit in the park first.', 3);return;}
      for (let i = 0; i < 3; i++) setTimeout(() => {fire.throwLog({x: player.x, y: player.feet + 1.3, z: player.z});mp.addWood();}, i * 350);notice('You empty the sack of firewood onto the fire.', 3);
    } else if (p.use === 'beer') {audio.sfx('bottleOpen');audio.sfx('gulp');drunk += 30;beerT = 20;wallet.addPant(1);notice(`You drink a ${p.name}. (+30 s drunk, +1 bottle for the deposit)`, 3);}
    else if (p.use === 'soda') {audio.sfx('bottleOpen');audio.sfx('gulp');drunk = Math.max(0, drunk - 10);wallet.addPant(1);notice(`${p.name} – refreshing. (+1 bottle for the deposit)`, 3);}
    else if (p.use === 'energy') {audio.sfx('bottleOpen');audio.sfx('gulp');boost = {kind: 'speed', t: 15};wallet.addPant(1);notice(`${p.name}: 15 s of 3× speed! (+1 can for the deposit)`, 3);}
    else {drunk = Math.max(0, drunk - (p.sober || 10));audio.sfx('gulp');notice(`You eat ${p.name}.${p.sober ? ` (−${p.sober} s drunk)` : ''}`, 3);}
    wallet.take(p.id);walletT = 4;
  }
  function firePrompt() {
    const near = (p, r) => Math.hypot(player.x - p.x, player.z - p.z) < r && Math.abs(player.feet - fire.center.y) < 1.5;
    if (near(fire.woodpile, 1.9)) return {text: `□ Throw a log on the fire (fire ${Math.round(mp.fireLevel() / fire.MAX * 100)} %)`, act: () => addLog()};
    if (near(fire.crate, 1.7)) return {text: '□ Take a beer from the crate', act: () => drinkBeer('crate')};
    let best = null, bd = 1.0;for (const s of fire.spots) {const d = Math.hypot(player.x - s.x, player.z - s.z);if (d < bd && !mp.sitTaken(s.id)) {bd = d;best = s;}}
    if (best) return {text: '□ Sit down by the fire', act: () => sitDown(best)};
    return null;
  }
  function addLog() {fire.throwLog({x: player.x, y: player.feet + 1.3, z: player.z});audio.sfx('throw');setTimeout(() => {audio.sfx('thud', {gain: .4});}, 600);mp.addWood();notice('You throw a log on the fire.', 2);}
  function drinkBeer() {audio.sfx('bottleOpen');audio.sfx('gulp');drunk += 30;beerT = 20;wallet.addPant(1);notice('Skål! A beer from the crate. (+30 s drunk, +1 bottle for the deposit)', 3);}
  function sitDown(spot) {watch.hide();if (photo.active) photo.toggle();player.mode = 'sit';player.sitSpot = spot;player.lookYaw = 0;player.lookPitch = -.32;notice('Sitting by the fire · R2 / left mouse: have a beer · □ stand up', 4);}
  function standUp() {const sp = player.sitSpot;player.mode = 'foot';player.sitSpot = null;const dx = fire.center.x - sp.x, dz = fire.center.z - sp.z, l = Math.hypot(dx, dz) || 1;place(sp.x + dx / l * .7, sp.z + dz / l * .7, sp.yaw);}
  function exitVehicle() {
    const v = player.vehicle, i = vehicles.indexOf(v), p = v.exit();if (mp) mp.release(i, 'd', v.netGet());player.mode = 'foot';player.vehicle = null;setClip(.05, 450);$('speedo').classList.remove('air');updateDynamic();
    const y = heightAt(p.x, p.z), sp = collision.findSafeSpawn(walkWorld, p, player.eye, {y, maxDistance: 5});
    if (sp) {player.x = sp.x;player.z = sp.z;player.feet = sp.y;}player.yaw = v.yaw;player.pitch = 0;
    for (const [, w] of vehicleWorlds) collision.setDynamic(w, []);updateDynamic();
  }

  // ---------- Minigames and mission start ----------
  function startMinigame(g) {const s = g.start({x: player.x, z: player.z, y: player.feet});player.mode = 'minigame';player.minigame = g;if (s) place(s.x, s.z, s.yaw);}
  function startMission() {
    if (!caseGame) return;if (photo.active) photo.toggle();
    caseGame.start(false);notice('');
  }

  // ---------- Input ----------
  const keys = new Set(), keyEdges = new Set();let mouse = {dx: 0, dy: 0, left: false, right: false, leftEdge: false, leftUp: false, wheel: 0};
  document.addEventListener('keydown', e => {if (e.target && /^(INPUT|TEXTAREA|SELECT)$/.test(e.target.tagName)) {if (e.code === 'Escape') e.target.blur();return;}if (e.repeat) return;
    if ((e.code === 'Enter' || e.code === 'KeyT') && started && !paused) {e.preventDefault();openChat();return;}keys.add(e.code);keyEdges.add(e.code);if (['Space', 'ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight', 'Tab'].includes(e.code)) e.preventDefault();audio.resume();});
  document.addEventListener('keyup', e => {keys.delete(e.code);});
  window.addEventListener('blur', () => keys.clear());
  document.addEventListener('mousemove', e => {if (document.pointerLockElement === canvas) {mouse.dx += e.movementX;mouse.dy += e.movementY;}});
  canvas.addEventListener('mousedown', e => {audio.resume();if (document.pointerLockElement !== canvas) {if (started) lockPointer();return;}if (e.button === 0) {mouse.left = true;mouse.leftEdge = true;}if (e.button === 2) mouse.right = true;if (caseGame?.active) caseGame.mouse(e.button, true);});
  document.addEventListener('mouseup', e => {if (e.button === 0) {if (mouse.left) mouse.leftUp = true;mouse.left = false;}if (e.button === 2) mouse.right = false;if (caseGame?.active) caseGame.mouse(e.button, false);});
  canvas.addEventListener('contextmenu', e => e.preventDefault());
  canvas.addEventListener('wheel', e => {mouse.wheel += e.deltaY;}, {passive: true});
  let padPrev = null;
  function readInput() {
    const pads = navigator.getGamepads ? navigator.getGamepads() : [], gp = padReader.pickGamepad(pads), st = gp ? padReader.read(gp) : null;
    const edges = st ? padReader.pressedEdges(padPrev, st) : [], released = st && padPrev ? Object.keys(st.buttons).filter(k => !st.buttons[k] && padPrev.buttons[k]) : [];padPrev = st;
    const k = c => keys.has(c), ke = c => keyEdges.has(c);
    const kx = (k('KeyD') ? 1 : 0) - (k('KeyA') ? 1 : 0), ky = (k('KeyS') ? 1 : 0) - (k('KeyW') ? 1 : 0);
    const input = {
      move: {x: st ? st.move.x : 0, y: st ? st.move.y : 0}, look: {x: st ? st.look.x : 0, y: st ? st.look.y : 0},
      held: Object.assign({}, st?.buttons || {}), pressed: new Set(edges), released: new Set(released), r2: st ? st.run : 0, l2: st ? st.aim : 0, gamepad: Boolean(gp), mouse: {...mouse}
    };
    if (kx || ky) {input.move.x = kx;input.move.y = ky;}
    const map = {Space: 'cross', KeyC: 'circle', KeyE: 'square', KeyF: 'triangle', Escape: 'options', KeyM: 'touchpad', KeyG: 'gallery', KeyZ: 'prone', KeyR: 'reload', ArrowLeft: 'left', ArrowRight: 'right', ArrowUp: 'up', ArrowDown: 'down', ShiftLeft: 'l3', KeyQ: 'l1', KeyV: 'view', KeyY: 'share'};
    for (const [code, name] of Object.entries(map)) {if (ke(code)) input.pressed.add(name);if (k(code)) input.held[name] = true;}
    if (mouse.left) input.r2 = Math.max(input.r2, 1);if (mouse.right) input.l2 = Math.max(input.l2, 1);
    if (mouse.leftUp) input.released.add('mouseLeft');if (mouse.leftEdge) input.pressed.add('mouseLeft');
    if (k('KeyW') && player.mode === 'vehicle') input.r2 = 1;if (k('KeyS') && player.mode === 'vehicle') input.l2 = 1;
    mouse.dx = mouse.dy = 0;mouse.leftEdge = mouse.leftUp = false;mouse.wheel = 0;keyEdges.clear();
    if (edges.length || (st && (Math.abs(st.move.x) + Math.abs(st.move.y) + Math.abs(st.look.x) + Math.abs(st.look.y) > 0))) audio.resume();
    return input;
  }

  // ---------- Start / pause overlays ----------
  function lockPointer() {try {const r = canvas.requestPointerLock && canvas.requestPointerLock();if (r && r.catch) r.catch(() => {});} catch {}}
  let started = false, paused = false;
  function begin() {if (started) return;started = true;$('start').hidden = true;audio.resume();lockPointer();world.setImagery(imageryWanted());const q = (new URLSearchParams(location.search).get('lobby') || '').toUpperCase();if (!(q && mp.net.connect(q))) mp.net.home();}
  function imageryWanted() {try {return localStorage.getItem('haarklous-aerial-imagery-v1') !== 'off';} catch {return true;}}
  $('startButton').onclick = begin;
  function setPaused(p) {paused = p;$('pause').hidden = !p;if (p && document.pointerLockElement === canvas) document.exitPointerLock();if (!p) lockPointer();renderSettings();}
  $('resume').onclick = () => setPaused(false);
  function renderSettings() {
    $('walletLine').textContent = `Money: ${shops.kr(wallet.money)} · empty bottles: ${wallet.pant} (${shops.kr(wallet.pant * shops.PANT)} at the bottle return)`;
    $('sensPad').value = sensitivity.pad;$('sensMouse').value = sensitivity.mouse;$('sensPadOut').textContent = sensitivity.pad.toFixed(2) + '×';$('sensMouseOut').textContent = sensitivity.mouse.toFixed(2) + '×';
    $('imagery').checked = imageryWanted();$('quality').value = String(quality);
    $('musicVol').value = music.volume;$('musicOut').textContent = music.volume ? Math.round(music.volume * 100) + ' %' : 'off';
    const np = music.position();$('nowPlaying').textContent = np ? `${np.title} (${Math.floor(np.length / 60)}:${String(Math.round(np.length % 60)).padStart(2, '0')} loop)` : '–';
  }
  $('musicVol').oninput = e => {music.setVolume(+e.target.value);renderSettings();};
  function musicTrack() {if (caseGame?.active) return 'mission';if (player.mode === 'minigame') return discCourses.includes(player.minigame) ? 'discgolf' : 'petanque';return 'main';}
  $('sensPad').oninput = e => {setSens('pad', +e.target.value);renderSettings();};$('sensMouse').oninput = e => {setSens('mouse', +e.target.value);renderSettings();};
  $('imagery').onchange = e => {try {localStorage.setItem('haarklous-aerial-imagery-v1', e.target.checked ? 'on' : 'off');} catch {}world.setImagery(e.target.checked);};
  let quality = 1;try {quality = +(localStorage.getItem('haarklous-quality') ?? 1);} catch {}
  $('quality').onchange = e => {quality = +e.target.value;try {localStorage.setItem('haarklous-quality', String(quality));} catch {}applyQuality();};
  function applyQuality() {sun.castShadow = quality > 0;sun.shadow.mapSize.set(quality > 1 ? 4096 : 2048, quality > 1 ? 4096 : 2048);if (sun.shadow.map) {sun.shadow.map.dispose();sun.shadow.map = null;}pixelRatio = Math.min(window.devicePixelRatio || 1, [.8, 1.25, 2][quality]);renderer.setPixelRatio(pixelRatio);resize();}

  // ---------- Frame update ----------
  const clock = new T.Clock();let fpsAvg = 30, lowFrames = 0, ambienceTimer = 0, promptTimer = 0, cullT = 0, fireSoundT = 0;
  function resize() {const w = view.clientWidth, h = view.clientHeight;renderer.setSize(w, h, false);camera.aspect = w / h;camera.updateProjectionMatrix();overlayCamera.aspect = w / h;overlayCamera.updateProjectionMatrix();}
  new ResizeObserver(resize).observe(view);resize();
  function frame() {
    requestAnimationFrame(frame);
    const dt = Math.min(clock.getDelta(), .05), t = clock.elapsedTime, input = readInput();
    fpsAvg += (1 / Math.max(dt, .001) - fpsAvg) * .05;
    if (!started) {if (input.pressed.has('cross') || input.pressed.has('options')) begin();renderFrame(dt, t);return;}
    // Global toggles.
    if (input.pressed.has('options') && !caseGame?.active) {if (map.open) map.toggle();else if (photo.gallery) photo.openGallery(false);else setPaused(!paused);}
    if (paused) {
      if (codePad) codePadInput(input);
      else {
        if (input.pressed.has('left') || input.pressed.has('right')) {music.setVolume(music.volume + (input.pressed.has('right') ? .1 : -.1));renderSettings();}
        if (input.pressed.has('triangle')) openCodePad();if (input.pressed.has('square') && mp.net.code !== mp.net.ownCode) {mp.net.home();notice('Back in your own lobby.', 3);}
      }
      renderFrame(0, t);return;
    }
    // Chat: the controller's Share button opens quick chat; keyboard Enter / T opens the text box.
    if (input.pressed.has('share') && !map.open) {quick = quick ? null : {sel: 0, stick: false};renderQuick();}
    if (quick) quickInput(input);
    if (bag) bagInput(input);
    if (caseGame?.active) {
      // The mission owns ✕ (begin), ○ (leave on panels), Options (pause), □ (door or reload), triggers.
      for (const name of input.pressed) {if (name === 'reload') caseGame.key({key: 'r', code: 'KeyR', repeat: false});else if (name === 'options' || caseGame.state !== 'play') caseGame.padButton(name === 'mouseLeft' ? '' : name);else if (['square', 'touchpad'].includes(name)) caseGame.padButton(name);}
      if (input.gamepad) caseGame.triggers(input.l2, input.r2);
      // The button that left the mission (○, ✕ …) must not also crouch or jump.
      if (!caseGame.active) {player.mode = 'foot';['circle', 'cross', 'square', 'options'].forEach(b => input.pressed.delete(b));}
    }
    if (input.pressed.has('touchpad') && !caseGame?.active) map.toggle(player);
    if (map.open) {
      if (input.pressed.has('circle')) map.toggle();
      map.pan(input.move.x * 400 * dt, input.move.y * 400 * dt);if (input.look.y) map.zoom(Math.exp(-input.look.y * dt * 2));
      map.draw({x: player.x, z: player.z, headingLocal: -(player.yaw - theta) + Math.PI});renderFrame(dt, t);return;
    }
    // △: a tap takes out the photo camera (or puts it away); holding it opens the gear menu: camera or FPV drone.
    if (player.mode === 'foot' && !caseGame?.active && !pbBusy()) {// the paintball lobby and game use △ and the D-pad
      if (input.pressed.has('triangle')) {if (photo.active) photo.toggle();else gear = {t0: performance.now(), open: false, sel: gearLast, stick: false};}
      if (gear) {
        if (input.held.triangle) {
          if (!gear.open && performance.now() - gear.t0 > 280) {gear.open = true;watch.hide();}
          if (gear.open) {
            const sx = Math.abs(input.move.x) > Math.abs(input.move.y) ? input.move.x : input.move.y;if (input.pressed.has('left') || input.pressed.has('up') || (sx < -.5 && !gear.stick)) gear.sel = 0;if (input.pressed.has('right') || input.pressed.has('down') || (sx > .5 && !gear.stick)) gear.sel = 1;gear.stick = Math.abs(sx) > .5;
            if (input.mouse.dx < -4) gear.sel = 0;else if (input.mouse.dx > 4) gear.sel = 1;
            input.move.x = input.move.y = 0;input.look.x = input.look.y = 0;input.mouse.dx = input.mouse.dy = 0;showGear(gear.sel);
          }
        } else {const g = gear;gear = null;showGear(null);if (g.open) {gearLast = g.sel;if (g.sel === 1) startDrone();else photo.toggle();} else photo.toggle();}
      }
      if (input.pressed.has('up') && !photo.active && !gear) watch.toggle();
      if (input.pressed.has('down') && !photo.active && !gear && !bag) {openBag();input.pressed.delete('down');}
    } else if (gear) {gear = null;showGear(null);}
    if (photo.active || player.mode !== 'foot') watch.hide();
    if (photo.active) {
      if (input.pressed.has('square') || input.pressed.has('gallery')) photo.openGallery();
      if (photo.gallery) {if (input.pressed.has('left')) photo.browse(-1);if (input.pressed.has('right')) photo.browse(1);if (input.move.x > .6 && !photo.stick) {photo.browse(1);photo.stick = true;} else if (input.move.x < -.6 && !photo.stick) {photo.browse(-1);photo.stick = true;} else if (Math.abs(input.move.x) < .3) photo.stick = false;if (input.pressed.has('circle')) photo.openGallery(false);renderFrame(dt, t);return;}
      if (input.pressed.has('circle')) photo.toggle();
      photo.setRaised(input.l2 > .4 ? 1 : 0);if (photo.raised > .5 && input.look.y) photo.setZoom(photo.zoom * Math.exp(-input.look.y * dt * 1.5));
      if (input.r2 > .6 && !photo.shutterHeld) {photo.shutterHeld = true;renderFrame(0, t, true);photo.capture(canvas, placeName());renderFrame(dt, t);return;}
      if (input.r2 < .3) photo.shutterHeld = false;
    }
    // Drunk effect: decays in real time.
    drunk = Math.max(0, drunk - dt);const dl = drunkLevel();
    if (boost && (boost.t -= dt) <= 0) {boost = null;audio.muffle(false);notice('The boost wears off.', 2);}
    const wdt = dt * worldScale();stash.update(dt);shopsTick(dt);
    if (player.mode === 'vehicle') updateVehicle(dt, input, dl);
    else if (player.mode === 'passenger') updatePassenger(dt, input, dl);
    else if (player.mode === 'sit') updateSit(dt, input, dl);
    else if (player.mode === 'drone') updateDrone(dt, input, dl);
    else if (player.mode === 'minigame') updateMinigame(dt, input, dl);
    else updateFoot(dt, input, dl);
    // Other vehicles: parked ones idle (far ones are hidden and skipped), ones driven by other players are posed by the lobby.
    cullT -= dt;if (cullT <= 0) {cullT = .4;for (const v of vehicles) {const near = Math.hypot(v.x - camera.position.x, v.z - camera.position.z) < (v.cullDistance || 170);v.group.visible = near || v === player.vehicle;}if (vehicles.some((v, i) => mp.remoteDriven(i))) updateDynamic();}
    vehicles.forEach((v, i) => {if (v === player.vehicle && player.mode === 'vehicle') return;if (mp.remoteDriven(i)) return;if (!v.group.visible && Math.abs(v.speed || 0) < .01 && !v.aircraft) return;v.update(wdt, {});});
    beerT = Math.max(0, beerT - dt);mp.update(dt);
    if (site) site.update(wdt);
    if (paintball) {
      const foot = player.mode === 'foot', p = foot ? input.pressed : new Set(), h = foot ? input.held : {};
      paintball.update(dt, {r2: foot ? input.r2 : 0, l2: foot ? input.l2 : 0, reload: p.has('square') || p.has('reload'), up: p.has('up'), down: p.has('down'), left: p.has('left'), right: p.has('right'),
        triangle: p.has('triangle'), cross: p.has('cross'), crossHeld: !!h.cross, triangleHeld: !!h.triangle}, {remotes: [...mp.remotes.values()], onFoot: foot, camera});
    }
    {const fl = mp.fireLevel();fire.update(wdt, fl, camera);fireSoundT -= dt;if (fireSoundT <= 0) {fireSoundT = .25;const d = Math.hypot(player.x - FIRE_POS.x, player.z - FIRE_POS.z);audio.fire(fl, Math.max(0, 1 - d / 28));}}
    // Door animations.
    let doorsChanged = false;
    for (const d of doors) if (d.anim) {d.anim.t = Math.min(1, d.anim.t + dt * 2.2);const e = d.anim.t < .5 ? 2 * d.anim.t * d.anim.t : 1 - Math.pow(-2 * d.anim.t + 2, 2) / 2;setDoor(d, d.anim.from + (d.anim.to - d.anim.from) * e);if (d.anim.t >= 1) {d.open = d.anim.to === 1;d.anim = null;doorsChanged = true;}}
    if (doorsChanged || caseGame?.active) updateDynamic();
    promptTimer -= dt;if (promptTimer <= 0) {promptTimer = .15;promptAction = player.mode === 'minigame' || player.mode === 'drone' || gear?.open || quick || bag || photo.active || (caseGame?.active && caseGame.state !== 'play') ? null : findPrompt();prompt(promptAction?.text);}
    ambienceTimer -= dt;if (ambienceTimer <= 0) {ambienceTimer = .25;updateAmbience();if (audio.context) music.attach(audio.context);music.play(musicTrack());}
    // Adaptive resolution keeps the frame rate in the 20–30+ fps range on slower PCs.
    if (fpsAvg < 22) {if (++lowFrames > 90 && pixelRatio > .6) {pixelRatio = Math.max(.6, pixelRatio - .1);renderer.setPixelRatio(pixelRatio);resize();lowFrames = 0;}} else lowFrames = 0;
    renderFrame(dt, t);
  }
  function placeName() {if (inApartment()) return 'Haarklous plass 17A, 4th floor';const tw = towerAt(player.x, player.z, player.feet);if (tw) return tw.t.own ? 'Stairwell, Haarklous plass 17A' : 'Stairwell, Torshov';return 'Torshov, Oslo';}

  function updateFoot(dt, input, dl) {
    // Look.
    const mouseScale = .0024 * sensitivity.mouse, padScale = sensitivity.pad * (photo.raised > .5 ? 1 / photo.zoom : 1) * (caseGame?.active ? caseGame.lookScale() : 1), curve = v => Math.sign(v) * v * v;
    player.yaw -= input.mouse.dx * mouseScale + curve(input.look.x) * 2.8 * padScale * dt;
    player.pitch = Math.max(-1.5, Math.min(1.5, player.pitch - input.mouse.dy * mouseScale - curve(input.look.y) * 1.9 * padScale * dt));
    // Buttons: ✕ jump, ○ tap crouch / hold prone, □ interact, L3 run toggle.
    if (!caseGame?.active || caseGame.state === 'play') {
      if (input.pressed.has('cross')) {if (player.stance !== 'stand') setStance('stand');else if (!player.jumpY && !player.jumpV) {player.jumpV = 3.1 * (boostIs('jump') ? Math.sqrt(3) : 1);audio.footstep('grass', false, .4);}}
      if (input.pressed.has('circle')) {player.circleAt = performance.now();player.circleHeld = false;}
      if (player.circleAt) {if (!input.held.circle) {if (!player.circleHeld) setStance(player.stance === 'crouch' ? 'stand' : 'crouch');player.circleAt = null;} else if (!player.circleHeld && performance.now() - player.circleAt > 320) {player.circleHeld = true;setStance(player.stance === 'prone' ? 'stand' : 'prone');}}
      if (input.pressed.has('prone')) setStance(player.stance === 'prone' ? 'stand' : 'prone');
      if (input.pressed.has('l3')) player.run = !player.run;
      if (input.pressed.has('square') && !photo.active && !caseGame?.active && !pbArmed()) interact();// paintball: □ reloads
    }
    // Move.
    const canMove = (!caseGame?.active || caseGame.canMove()) && !(paintball && paintball.frozen);
    let mx = input.move.x, my = input.move.y;
    if (dl > 0) {mx += Math.sin(performance.now() / 700) * .55 * dl;my += Math.sin(performance.now() / 1100) * .15 * dl;}
    const mag = Math.min(1, Math.hypot(mx, my));
    if (canMove && mag > .05) {
      const running = player.stance === 'stand' && (player.run || (!caseGame?.active && !photo.active && !pbArmed() && input.r2 > .3) || input.held.l3);
      // Standing: 3.5 m/s (the user's wish for the Unreal version, 28 Oct 2026); crouching and prone keep 1.45 x .55 and x .28.
      const base = player.stance === 'stand' ? WALK_SPEED : 1.45 * stanceSpeed();
      const speed = (running ? 3.6 : base) * (caseGame?.active ? caseGame.moveScale() : 1) * (1 - dl * .25) * (boostIs('speed') ? 3 : 1), fwd = -my, right = mx;
      const dx = (-Math.sin(player.yaw) * fwd + Math.cos(player.yaw) * right) * speed * dt, dz = (-Math.cos(player.yaw) * fwd - Math.sin(player.yaw) * right) * speed * dt;
      const bx = player.x, bz = player.z;move(dx, dz);
      player.stride += Math.hypot(player.x - bx, player.z - bz);
      if (player.stride > (running || speed > 2.5 ? 1.05 : .72) * (player.stance === 'stand' ? 1 : .8)) {player.stride = 0;footstep(running ? 1.2 : player.stance === 'stand' ? .8 : .45);}
    } else if (!mag) player.run = false;
    // Jump and stance easing.
    player.eyeNow += (stanceEye(player.stance) - player.eyeNow) * Math.min(1, dt * 9);
    if (player.jumpV || player.jumpY) {player.jumpV -= 9.81 * dt;player.jumpY += player.jumpV * dt;const head = collision.ceilingAt(walkWorld, player, player.feet) - .1 - player.eyeNow;if (player.jumpY > head) {player.jumpY = Math.max(0, head);player.jumpV = Math.min(0, player.jumpV);}if (player.jumpY <= 0 && player.jumpV <= 0) {if (player.jumpV < -2) footstep(1.3);player.jumpY = 0;player.jumpV = 0;}}
    // Camera.
    const sway = dl ? {r: Math.sin(performance.now() / 1300) * .09 * dl, y: Math.sin(performance.now() / 1700) * .05 * dl, p: Math.sin(performance.now() / 900) * .03 * dl} : {r: 0, y: 0, p: 0};
    camera.position.set(player.x, player.feet + player.eyeNow + player.jumpY, player.z);
    camera.rotation.set(0, 0, 0, 'YXZ');camera.rotation.order = 'YXZ';camera.rotation.y = player.yaw + sway.y;camera.rotation.x = player.pitch + sway.p;camera.rotation.z = sway.r;
    const fov = photo.active && photo.raised > .5 ? 70 / photo.zoom : caseGame?.active ? camera.fov : pbArmed() ? 70 / (1 + .45 * paintball.aim) : boostIs('speed') ? 82 : 70;if (Math.abs(camera.fov - fov) > .01 && !caseGame?.active) {camera.fov = fov;camera.updateProjectionMatrix();}
  }
  function footstep(soft = 1) {
    let surface = 'grass', stair = false;
    if (shops.shopAt(player, player.feet)) surface = 'tile';
    else if (inApartment()) {const r = roomAt();surface = r === 'bath' ? 'tile' : r ? 'wood' : 'metal';}
    else if (towerAt(player.x, player.z, player.feet)) {surface = 'stone';stair = true;}
    else {const s = world.surfaceAt(player.x, player.z);surface = s === 'asphalt' ? 'asphalt' : s === 'gravel' ? 'gravel' : s === 'sand' ? 'sand' : 'grass';}
    audio.footstep(surface, stair, soft);
  }
  function updateVehicle(dt, input, dl) {
    const v = player.vehicle;
    if (v.aircraft) {updateAircraft(dt, input, dl);return;}
    if (input.pressed.has('square') && Math.abs(v.speed) < 3) {exitVehicle();return;}
    const steer = Math.abs(input.move.x) > .08 ? input.move.x : 0;
    // Construction machines: the arrow keys work as the right stick on a keyboard.
    const kb = (a, b) => (input.held[a] ? 1 : 0) - (input.held[b] ? 1 : 0), rx = Math.max(-1, Math.min(1, input.look.x + (v.site ? kb('right', 'left') : 0))), ry = Math.max(-1, Math.min(1, input.look.y + (v.site ? kb('down', 'up') : 0)));
    v.update(dt, {throttle: input.r2, brake: input.l2, steer, drunk: dl, l1: input.held.l1, l2: input.l2, r1: input.held.r1, r2: input.r2, cross: input.pressed.has('cross'), lx: input.move.x, ly: input.move.y, rx, ry, sticks: {lx: input.move.x, ly: input.move.y, rx: input.look.x, ry: input.look.y}});
    // Look around from the seat (right stick / mouse); the excavators use the right stick for the boom, so mouse only there.
    const tool = v.kind === 'excavator' || v.rightStickTool, lookX = tool ? 0 : input.look.x, lookY = tool ? 0 : input.look.y;
    player.lookYaw = (player.lookYaw || 0) - input.mouse.dx * .0024 * sensitivity.mouse - lookX * 2.2 * dt;player.lookPitch = Math.max(-1.2, Math.min(1, (player.lookPitch || 0) - input.mouse.dy * .0024 * sensitivity.mouse - lookY * 1.6 * dt));
    if (!input.mouse.dx && !lookX) player.lookYaw *= Math.max(0, 1 - dt * 1.2);
    const seat = v.seat(), sway = dl ? Math.sin(performance.now() / 1300) * .08 * dl : 0;
    camera.position.set(seat.x + (seat.lateral || 0) * Math.cos(v.yaw), seat.y, seat.z - (seat.lateral || 0) * Math.sin(v.yaw));
    camera.rotation.order = 'YXZ';camera.rotation.y = seat.yaw + player.lookYaw - (v.kind === 'excavator' ? .22 : 0) + (v.camYaw || 0);camera.rotation.x = seat.pitch + player.lookPitch - (v.kind === 'motorcycle' ? .12 : v.kind === 'excavator' ? .38 : .05) + (v.camPitch || 0);camera.rotation.z = -(seat.roll || 0) + sway;
    if (camera.fov !== 70) {camera.fov = 70;camera.updateProjectionMatrix();}
    if (v.site) siteMachineView(v, seat, input);
    player.x = v.x;player.z = v.z;player.feet = v.y;player.yaw = v.yaw;
    $('speedo').hidden = false;$('speedo').textContent = v.hud ? v.hud() : v.kind === 'excavator' ? `${(Math.abs(v.speed) * 3.6).toFixed(1)} km/h · bucket ${Math.round(v.load * 100)} %` : `${Math.round(Math.abs(v.speed) * 3.6)} km/h${v.kind === 'motorcycle' ? ` · gear ${v.gear} · ${Math.round(v.rpm / 100) * 100} rpm` : ''}`;
  }
  // Construction-site machines (ported from the Unreal version, where the cabs were hard to see from): R3 / V cycles the
  // view from the cab, from outside (behind and above along the operator's heading; the mouse turns it round the machine)
  // and, for the two cranes, straight down from above the hook (the jib's direction up the screen).
  function siteMachineView(v, seat, input) {
    const tower = v.kind === 'tower', crane = tower || v.kind === 'crane';
    if (input.pressed.has('r3') || input.pressed.has('view')) {
      const modes = tower ? [0, 2] : crane ? [0, 2, 1] : [0, 1];player.siteView = modes[(modes.indexOf(player.siteView || 0) + 1) % modes.length];player.lookYaw = 0;player.lookPitch = 0;
      notice(player.siteView === 0 ? 'View: from the cab' : player.siteView === 1 ? 'View: from outside' : 'View: from above the hook', 1.5);
    }
    if (player.siteView === 2 && v.hook) {
      const top = (tower ? v.trolleyNode : v.tipNode).getWorldPosition(new T.Vector3()), up = Math.max(3, Math.min(16, top.y - v.hook.p.y - 1));
      camera.position.copy(v.hook.p).add(new T.Vector3(0, up, 0));camera.rotation.set(-Math.PI / 2 + .001, seat.yaw, 0);
      if (camera.fov !== 75) {camera.fov = 75;camera.updateProjectionMatrix();}
    } else if (player.siteView === 1) {
      const H = seat.yaw + (player.lookYaw || 0), dist = Math.max(10, v.length * .5 + 8), up = v.height + (crane ? 6 : 3.5), fx = -Math.sin(H), fz = -Math.cos(H);
      const look = new T.Vector3(v.x + fx * v.length * .4, v.y + 1.2, v.z + fz * v.length * .4), cam = new T.Vector3(v.x - fx * dist, v.y + up, v.z - fz * dist);
      cam.y = Math.max(cam.y, heightAt(cam.x, cam.z) + 1.5);camera.position.copy(cam);
      camera.rotation.set(Math.atan2(look.y - cam.y, Math.hypot(look.x - cam.x, look.z - cam.z)) + (player.lookPitch || 0) * .5, H, 0);
    }
  }
  // Mode 2 sticks for the aircraft and the drone; the keyboard uses W/S, A/D and the arrow keys.
  function flightSticks(input, dl) {
    const dz = a => Math.abs(a) < .07 ? 0 : a, kb = (a, b) => (input.held[a] ? 1 : 0) - (input.held[b] ? 1 : 0), wob = dl ? Math.sin(performance.now() / 480) * .3 * dl : 0;
    return {lx: dz(input.move.x) + wob * .5, ly: dz(input.move.y), rx: Math.max(-1, Math.min(1, dz(input.look.x) + kb('right', 'left') + wob)), ry: Math.max(-1, Math.min(1, dz(input.look.y) + kb('down', 'up') + wob * .4)), brake: input.l2};
  }
  function updateAircraft(dt, input, dl) {
    const v = player.vehicle;
    if (input.pressed.has('square') && v.grounded && v.speed < 3) {exitVehicle();return;}
    if (input.pressed.has('r3') || input.pressed.has('view')) {player.chase = !player.chase;player.chaseCam = null;}
    v.update(dt, flightSticks(input, dl));
    const pose = v.cameraPose(player.chase, player.chaseCam);if (player.chase) player.chaseCam = pose.position.clone();
    camera.position.copy(pose.position);camera.quaternion.copy(pose.quaternion);
    if (!player.chase) {
      player.lookYaw = (player.lookYaw || 0) - input.mouse.dx * .0024 * sensitivity.mouse;player.lookPitch = Math.max(-1.1, Math.min(1, (player.lookPitch || 0) - input.mouse.dy * .0024 * sensitivity.mouse));
      if (!input.mouse.dx) player.lookYaw *= Math.max(0, 1 - dt * .8);if (!input.mouse.dy) player.lookPitch *= Math.max(0, 1 - dt * .8);
      camera.rotateY(player.lookYaw);camera.rotateX(player.lookPitch);
    }
    if (camera.fov !== 72) {camera.fov = 72;camera.updateProjectionMatrix();}
    player.x = v.x;player.z = v.z;player.feet = v.y;player.yaw = v.yaw;
    const sp = $('speedo');sp.hidden = false;sp.classList.add('air');sp.textContent = v.broken ? 'CRASHED' : v.hud();
  }
  // ---------- FPV drone ----------
  function floorBelow(p, y) {
    let best = null;
    for (const f of walkWorld.floors) {const b = f.bounds;if (p.x < b.minX || p.x > b.maxX || p.z < b.minZ || p.z > b.maxZ || !inside(p, f.points)) continue;const h = f.surface(p);if (h <= y + .05 && (!best || h > best.y)) best = {y: h, ceiling: f.ceilingHeight ?? Infinity};}
    return best;
  }
  function startDrone() {
    const f = {x: -Math.sin(player.yaw), z: -Math.cos(player.yaw)}, p = {x: player.x + f.x * 1.1, z: player.z + f.z * 1.1}, fl = floorBelow(p, player.feet + .4);
    if (!fl || Math.abs(fl.y - player.feet) > .6 || collision.hitsSolid(walkWorld, p, fl.y, fl.y + .35)) {notice('No room to launch the drone here – face an open space.');return;}
    watch.hide();if (photo.active) photo.toggle();drone.launch(p.x, fl.y, p.z, player.yaw);player.mode = 'drone';setClip(.03, 900);view.dataset.drone = '';
    notice('FPV drone · left stick ↑↓ thrust, ←→ yaw · right stick tilt · L1 / Q angle ↔ acro · ✕ reset after a crash · ○ / △ pack it away', 8);
  }
  function stopDrone() {drone.stop();player.mode = 'foot';setClip(.05, 450);$('osd').hidden = true;delete view.dataset.drone;}
  let osdT = 0;
  function updateDrone(dt, input, dl) {
    if (input.pressed.has('circle') || input.pressed.has('triangle')) {stopDrone();notice('Drone packed away.', 2);return;}
    if (input.pressed.has('l1')) {drone.setMode(drone.mode === 'angle' ? 'acro' : 'angle');notice(drone.mode === 'acro' ? 'ACRO: the right stick sets rotation rates – nothing self-levels. Flips and rolls!' : 'ANGLE: the drone levels itself and holds altitude with the stick centred.', 4);}
    if (input.pressed.has('cross') && (drone.crashed || !drone.armed)) {if (drone.battery > 0) drone.reset();else notice('Battery empty – ○ to pack the drone away, then launch it again with a fresh pack.', 4);}
    drone.update(dt, flightSticks(input, dl), {floor: floorBelow, solid: (p, a, b) => collision.hitsSolid(walkWorld, p, a, b), listener: {x: player.x, y: player.feet + 1.6, z: player.z}});
    const pose = drone.cameraPose();camera.position.copy(pose.position);camera.quaternion.copy(pose.quaternion);
    if (camera.fov !== 100) {camera.fov = 100;camera.updateProjectionMatrix();}
    if ((osdT -= dt) > 0) return;osdT = .1;
    const fl = floorBelow(drone.pos, drone.pos.y + .1), o = drone.osd(fl ? fl.y : heightAt(drone.pos.x, drone.pos.z)), mm = Math.floor(o.time / 60), ss = String(Math.floor(o.time % 60)).padStart(2, '0');
    const el = $('osd');el.hidden = false;
    el.innerHTML = `<div class="tl">${o.mode}<br>${o.armed ? 'ARMED' : 'DISARMED'}</div><div class="tr">${o.volts} V · ${o.battery} %<span class="bar"><i style="width:${o.battery}%"></i></span><br>${mm}:${ss}</div>` +
      `<div class="bl">ALT ${o.alt.toFixed(1)} m<br>THR ${Math.round(o.thrust * 100)} %</div><div class="br">${Math.round(o.speed)} km/h<br>HOME ${Math.round(o.dist)} m</div><div class="mid">-+-</div>` +
      (o.crashed ? '<div class="warn">CRASHED<br><small>✕ reset · ○ pack away</small></div>' : o.battery <= 0 ? '<div class="warn">BATTERY EMPTY</div>' : o.battery < 20 ? '<div class="warn">LOW BATTERY</div>' : '');
  }
  let gear = null, gearLast = 0;
  function showGear(sel) {const m = $('gearMenu');m.hidden = sel === null;if (sel !== null) m.querySelectorAll('.gm-item').forEach(e => e.classList.toggle('sel', +e.dataset.i === sel));}
  function updatePassenger(dt, input, dl) {
    const v = player.vehicle;
    if (input.pressed.has('square') && Math.abs(v.speed) < 3 && (!v.aircraft || v.grounded)) {exitPassenger();return;}
    player.lookYaw = Math.max(-2.6, Math.min(2.6, (player.lookYaw || 0) - input.mouse.dx * .0024 * sensitivity.mouse - input.look.x * 2.2 * dt));player.lookPitch = Math.max(-1.2, Math.min(1, (player.lookPitch || 0) - input.mouse.dy * .0024 * sensitivity.mouse - input.look.y * 1.6 * dt));
    v.group.updateMatrixWorld(true);camera.position.copy(v.group.localToWorld(v.passengerEye.clone()));
    camera.quaternion.copy(v.group.getWorldQuaternion(new T.Quaternion())).multiply(new T.Quaternion().setFromEuler(new T.Euler(player.lookPitch - (v.kind === 'motorcycle' ? .1 : .05), player.lookYaw, 0, 'YXZ')));
    if (dl) camera.rotateZ(Math.sin(performance.now() / 1300) * .08 * dl);
    if (camera.fov !== 72) {camera.fov = 72;camera.updateProjectionMatrix();}
    player.x = v.x;player.z = v.z;player.feet = v.y;player.yaw = v.yaw + player.lookYaw;
    const kmh = Math.abs(v.speed) * 3.6, drv = mp.occupant(vehicles.indexOf(v), 'd');
    if (passengerEngine) {if (v.kind === 'helicopter') passengerEngine.set(v.rpm, .5);else if (v.kind === 'plane') passengerEngine.set(v.prop || 700, v.throttle || 0);else if (v.kind === 'motorcycle') passengerEngine.set(1300 + (kmh % 50) / 50 * 8500, drv ? .5 : 0);else passengerEngine.set(800 + (kmh * 38) % 3600, drv ? .4 : 0);}
    const sp = $('speedo');sp.hidden = false;sp.classList.toggle('air', !!v.aircraft);sp.textContent = `Passenger${drv ? ' · ' + mp.nameOf(drv) + (v.aircraft ? ' flying' : ' driving') : ''} · ${Math.round(kmh)} km/h${v.aircraft ? ` · alt ${Math.max(0, v.y - heightAt(v.x, v.z)).toFixed(0)} m` : ''}`;
  }
  function updateSit(dt, input, dl) {
    const sp = player.sitSpot;
    if (input.pressed.has('square') || input.pressed.has('cross')) {standUp();return;}
    if ((input.r2 > .6 || input.mouse.left) && !player.r2Held) {player.r2Held = true;drinkBeer();}if (input.r2 < .3 && !input.mouse.left) player.r2Held = false;
    const curve = v => Math.sign(v) * v * v;
    player.lookYaw = Math.max(-2, Math.min(2, (player.lookYaw || 0) - input.mouse.dx * .0024 * sensitivity.mouse - curve(input.look.x) * 2.2 * sensitivity.pad * dt));player.lookPitch = Math.max(-1.2, Math.min(1.1, (player.lookPitch || 0) - input.mouse.dy * .0024 * sensitivity.mouse - curve(input.look.y) * 1.6 * sensitivity.pad * dt));
    const sway = dl ? Math.sin(performance.now() / 1300) * .07 * dl : 0;
    camera.position.set(sp.x, sp.y + .78, sp.z);camera.rotation.order = 'YXZ';camera.rotation.set(player.lookPitch, sp.yaw + player.lookYaw, sway);
    if (camera.fov !== 70) {camera.fov = 70;camera.updateProjectionMatrix();}
    player.x = sp.x;player.z = sp.z;player.feet = sp.y - .47;player.yaw = sp.yaw + player.lookYaw;
  }
  // What other players see of us (sent ~10 times a second when someone else is in the lobby).
  function localState() {
    const r = v => Math.round(v * 100) / 100, q = v => Math.round(v * 1000) / 1000;
    const s = {p: [r(player.x), r(player.feet + (player.jumpY || 0)), r(player.z)], y: r(player.yaw), st: player.stance, b: beerT > 0 ? 1 : 0};
    if ((player.mode === 'vehicle' || player.mode === 'passenger') && player.vehicle) {s.v = vehicles.indexOf(player.vehicle);s.s = player.mode === 'vehicle' ? 'd' : 'p';}
    if (player.mode === 'sit' && player.sitSpot) {const sp = player.sitSpot;s.sit = sp.id + 1;s.p = [r(sp.x), r(sp.y), r(sp.z)];s.y = r(sp.yaw);}
    if (player.mode === 'drone') {s.st = 'drone';s.dr = [...drone.pos.toArray().map(r), ...drone.q.toArray().map(q)];}
    const pb = paintball && paintball.rules.localState();if (pb) s.pb = pb;
    return s;
  }
  // ---------- Chat ----------
  const chatLog = $('chatLog'), chatInput = $('chatInput'), QUICK = ['Hei! 👋', 'Skål! 🍺', 'Kom til bålet 🔥', 'Følg meg!', 'Hopp på!', 'Vent litt', 'Haha 😂', 'Ha det!', '⌨ Type a message…'];
  let quick = null;
  function chatLine(m, mine, old) {
    const row = document.createElement('div');row.className = 'line' + (m.sys ? ' sys' : '');
    const who = document.createElement('b');who.textContent = (m.name || 'Player') + (m.sys ? ' ' : ': ');who.style.color = window.GameAvatars.slotCss(m.slot || 0);
    const txt = document.createElement('span');txt.textContent = m.text;row.append(who, txt);chatLog.append(row);while (chatLog.children.length > 30) chatLog.firstChild.remove();
    row.dataset.t = old ? 0 : performance.now();chatLog.classList.add('recent');clearTimeout(chatLine.t);chatLine.t = setTimeout(() => chatLog.classList.remove('recent'), 12000);
    if (!mine && !old && !m.sys) audio.sfx('beep', {gain: .4});
  }
  function openChat() {chatInput.hidden = false;chatLog.classList.add('open');chatInput.value = '';setTimeout(() => chatInput.focus(), 0);if (document.pointerLockElement) document.exitPointerLock();}
  function closeChat() {chatInput.hidden = true;chatLog.classList.remove('open');chatInput.blur();if (started && !paused) lockPointer();}
  chatInput.addEventListener('keydown', e => {e.stopPropagation();if (e.code === 'Enter') {const t = chatInput.value.trim();if (t) {if (!mp.sendChat(t) && !mp.net.online) notice('You are offline – only you can see that.', 3);}closeChat();} else if (e.code === 'Escape') closeChat();});
  function renderQuick() {
    const el = $('quickChat');el.hidden = !quick;if (!quick) return;
    el.innerHTML = '<b>Quick chat</b>' + QUICK.map((q, i) => `<div class="${i === quick.sel ? 'sel' : ''}">${q}</div>`).join('') + '<small>↑↓ choose · ✕ send · ○ close</small>';
  }
  function quickInput(input) {
    const y = input.move.y;let moved = false;
    if (input.pressed.has('up') || (y < -.5 && !quick.stick)) {quick.sel = (quick.sel + QUICK.length - 1) % QUICK.length;moved = true;}
    if (input.pressed.has('down') || (y > .5 && !quick.stick)) {quick.sel = (quick.sel + 1) % QUICK.length;moved = true;}
    quick.stick = Math.abs(y) > .5;
    if (input.pressed.has('cross')) {const i = quick.sel;quick = null;if (i === QUICK.length - 1) openChat();else if (!mp.sendChat(QUICK[i]) && !mp.net.online) notice('You are offline – nobody else can hear you.', 3);}
    else if (input.pressed.has('circle')) quick = null;
    renderQuick();
    // The menu owns the sticks and buttons while it is open.
    input.move.x = input.move.y = 0;['cross', 'circle', 'up', 'down', 'square', 'triangle'].forEach(b => input.pressed.delete(b));void moved;
  }
  // ---------- Lobby (pause menu) ----------
  let codePad = null;
  function renderMp() {
    const n = mp.net, el = $('mpStatus');if (!el) return;
    el.textContent = n.online ? `Online · lobby ${n.code} · ${n.players.size}/${n.max} players` : n.status === 'connecting' ? `Connecting to lobby ${n.code || ''}…` : n.status === 'full' ? `That lobby is full (${n.max} players).` : 'Offline – single player (no connection to the lobby server).';
    $('mpOwnCode').textContent = n.ownCode;$('mpHome').hidden = !n.code || n.code === n.ownCode;
    $('mpWhere').textContent = n.code && n.code !== n.ownCode ? `You are in lobby ${n.code}.` : 'You are in your own lobby – friends join with your code.';
    $('mpLink').textContent = `${location.protocol === 'https:' ? location.origin : 'https://' + n.base.replace(/^wss?:\/\//, '')}/?lobby=${n.code || n.ownCode}`;
    $('mpPlayers').innerHTML = [...n.players.values()].sort((a, b) => a.slot - b.slot).map(p => `<li><i style="background:${window.GameAvatars.slotCss(p.slot)}"></i>${p.name.replace(/[<>&]/g, '')}${p.id === n.you ? ' (you)' : ''}${p.mic ? ' 🎙' : ''}</li>`).join('');
    if (document.activeElement !== $('mpName')) $('mpName').value = n.name;$('mpMic').checked = mp.voice.mic;
    const pad = $('codePad');pad.hidden = !codePad;if (codePad) pad.innerHTML = '<b>Join lobby</b><div class="slots">' + codePad.chars.map((c, i) => `<span class="${i === codePad.pos ? 'sel' : ''}">${c}</span>`).join('') + '</div><small>←→ move · ↑↓ change · ✕ join · ○ cancel</small>';
  }
  function openCodePad() {codePad = {chars: (mp.net.code && mp.net.code !== mp.net.ownCode ? mp.net.code : 'AAAA').split(''), pos: 0};renderMp();}
  function codePadInput(input) {
    const A = window.GameNet.ALPHABET, c = codePad;
    if (input.pressed.has('left')) c.pos = (c.pos + 3) % 4;if (input.pressed.has('right')) c.pos = (c.pos + 1) % 4;
    const step = input.pressed.has('up') ? 1 : input.pressed.has('down') ? -1 : 0;if (step) c.chars[c.pos] = A[(A.indexOf(c.chars[c.pos]) + step + A.length) % A.length];
    if (input.pressed.has('cross')) {const code = c.chars.join('');codePad = null;if (mp.net.connect(code)) notice(`Joining lobby ${code}…`, 3);}
    if (input.pressed.has('circle')) codePad = null;
    renderMp();
  }
  $('mpJoin').onclick = () => {const c = $('mpJoinCode').value.toUpperCase().trim();if (!mp.net.connect(c)) notice('A lobby code is 4 letters/digits (no O, 0, I or 1).', 4);else notice(`Joining lobby ${c}…`, 3);};
  $('mpJoinCode').addEventListener('keydown', e => {if (e.code === 'Enter') $('mpJoin').click();});
  $('mpHome').onclick = () => {mp.net.home();notice('Back in your own lobby.', 3);};
  $('mpName').onchange = e => {mp.net.setName(e.target.value);renderMp();};
  $('mpMic').onchange = async e => {const ok = await mp.setMic(e.target.checked);if (!ok && e.target.checked) notice('The microphone could not be opened (permission denied or no microphone).', 5);renderMp();};
  function applyDoor(id, open, instant) {
    const d = doors.find(x => x.id === id);if (!d) return;
    if (instant) {d.anim = null;d.open = open;setDoor(d, open ? 1 : 0);updateDynamic();return;}
    if (d.open === open || d.anim) return;d.anim = {from: d.open ? 1 : 0, to: open ? 1 : 0, t: 0};
    const p = d.leaf.getWorldPosition(new T.Vector3()), dist = Math.hypot(p.x - player.x, p.z - player.z);if (dist < 25) audio.sfx(open ? 'doorOpen' : 'doorClose', {gain: Math.max(.1, 1 - dist / 25)});
  }
  mp = window.GameMultiplayer.create({T, lib, scene, camera, vehicles, fireDecayS: fire.DECAY_S, fireMax: fire.MAX, localState, notice,
    drivingIndex: () => player.mode === 'vehicle' && player.vehicle ? vehicles.indexOf(player.vehicle) : -1,
    listener: () => ({x: player.x, y: player.feet + 1.5, z: player.z}), applyDoor, onChat: chatLine, onStatus: () => {renderMp();onLobbyChange();},
    onRemoteWood: r => fire.throwLog({x: r.pos.x, y: r.pos.y + 1.3, z: r.pos.z}), holdsMarker: id => !!paintball && paintball.rules.memberPlaying(id)});
  // A new lobby: vehicles go back to where everybody else has them (the lobby then sends the ones that were moved).
  let lastLobby = null;
  function onLobbyChange() {
    const n = mp.net;if (!n.online || n.code === lastLobby) return;lastLobby = n.code;
    vehicles.forEach((v, i) => {if (v === player.vehicle) {mp.claim(i, player.mode === 'vehicle' ? 'd' : 'p');return;}v.netSet(v.spawn);if (v.aircraft) v.reset();});
  }
  function updateMinigame(dt, input, dl) {
    const g = player.minigame, mouseScale = .0024 * sensitivity.mouse, curve = v => Math.sign(v) * v * v;
    player.yaw -= input.mouse.dx * mouseScale + curve(input.look.x) * 2 * sensitivity.pad * dt;player.pitch = Math.max(-1.2, Math.min(1.2, player.pitch - input.mouse.dy * mouseScale - curve(input.look.y) * 1.4 * sensitivity.pad * dt));
    const holding = input.r2 > .5;if (holding) g.charge = Math.min(1, (g.charge || 0) + dt / 1.2);
    const release = !holding && (g.charge || 0) > 0, power = g.charge || 0;if (release) g.charge = 0;
    const r = g.update(dt * worldScale(), {aimYaw: player.yaw + (dl ? Math.sin(performance.now() / 600) * .05 * dl : 0), aimPitch: player.pitch, charge: g.charge || 0, power, release, alt: input.l2 > .5, hyzer: input.move.x, cancel: input.pressed.has('circle'), drunk: dl}, camera) || {};
    if (r.teleport) place(r.teleport.x, r.teleport.z, r.teleport.yaw);
    if (r.stand) {player.x = r.stand.x;player.z = r.stand.z;player.feet = r.stand.y ?? heightAt(player.x, player.z);}
    if (r.face !== undefined) {player.yaw = r.face;player.pitch = -.12;}
    if (r.done) {notice(r.done, 6);player.mode = 'foot';player.minigame = null;}
    if (r.camera) {camera.position.set(r.camera.x, r.camera.y, r.camera.z);camera.lookAt(r.camera.lookAt);}
    else {camera.position.set(player.x, player.feet + player.eye, player.z);camera.rotation.order = 'YXZ';camera.rotation.set(player.pitch, player.yaw, 0);}
  }
  function updateAmbience() {
    const inApt = inFlat(), tw = towerAt(player.x, player.z, player.feet), indoor = inApt || tw || shops.shopAt(player, player.feet) ? 1 : player.vehicle && player.vehicle.kind === 'car' ? .7 : player.vehicle && player.vehicle.aircraft ? .4 : 0;
    audio.ambience({indoor, green: world.greenery(player.x, player.z), traffic: world.trafficDistance(player.x, player.z), height: Math.max(0, player.feet - heightAt(player.x, player.z)) + (inApt ? 10 : 0), stair: Boolean(tw), kitchen: inApt && roomAt() === 'kitchen'});
    // Stair-tower light follows the player into whichever stairwell they are in.
    if (tw) {const l = tw.t.stair.lampAt({x: tw.l.x, z: tw.l.z}, player.feet - tw.t.matrix.elements[13]);if (l) {const p = new T.Vector3(l.x, l.y, l.z).applyMatrix4(tw.t.matrix);towerLamp.position.copy(p);towerLamp.intensity = 5.5;}} else towerLamp.intensity = 0;
    if (player.mode !== 'vehicle' && player.mode !== 'passenger') $('speedo').hidden = true;
    const dl = drunkLevel();$('drunk').hidden = !dl;if (dl) $('drunk').textContent = `Drunk · ${Math.ceil(drunk)} s`;
  }

  // ---------- Rendering ----------
  const ghost = $('ghost'), gctx = ghost.getContext('2d');
  function renderFrame(dt, t, forPhoto = false) {
    // Sun shadow frustum follows the player (wide outdoors, tight indoors for crisp shadows).
    const flying = player.mode === 'drone' || player.vehicle?.aircraft, indoor = !flying && (inApartment() || towerAt(player.x, player.z, player.feet)), reach = indoor ? 16 : 42;
    if (flying) {const c = camera.position;sun.target.position.set(c.x, Math.max(heightAt(c.x, c.z), c.y - 25), c.z);} else sun.target.position.set(player.x, player.feet, player.z);sun.position.copy(sun.target.position).addScaledVector(sunDir, indoor ? 45 : 110);sun.target.updateMatrixWorld();
    if (sun.shadow.camera.right !== reach) {Object.assign(sun.shadow.camera, {left: -reach, right: reach, top: reach, bottom: -reach, near: 1, far: indoor ? 100 : 240});sun.shadow.camera.updateProjectionMatrix();}
    camera.updateMatrixWorld();
    farCamera.fov = camera.fov;farCamera.aspect = camera.aspect;farCamera.position.copy(camera.position);farCamera.quaternion.copy(camera.quaternion);farCamera.updateProjectionMatrix();farCamera.updateMatrixWorld();
    renderer.autoClear = false;renderer.clear();renderer.render(farScene, farCamera);renderer.clearDepth();renderer.render(scene, camera);
    if (!forPhoto) {
      if (caseGame?.active) caseGame.renderOverlay(camera);
      else {const showWatch = watch.update(dt), showPhoto = photo.active && !photo.gallery;photo.model.visible = showPhoto;if (showPhoto || showWatch || paintball?.marker.visible) {renderer.clearDepth();renderer.render(overlayScene, overlayCamera);}}
    }
    renderer.autoClear = true;
    // Drunk: blur and a drifting double image.
    const dl = drunkLevel();
    const boostFilter = !boost ? '' : boost.kind === 'speed' ? ' saturate(1.35) contrast(1.06)' : boost.kind === 'jump' ? ' hue-rotate(-14deg) saturate(1.25)' : ' saturate(.35) contrast(1.15) brightness(.96)';
    canvas.style.filter = ((dl ? `blur(${(dl * 2.2).toFixed(2)}px) saturate(${1 + dl * .3})` : '') + boostFilter).trim();
    const fx = $('boostFx');fx.hidden = !boost;if (boost) fx.className = boost.kind;
    const bh = $('boost');bh.hidden = !boost;if (boost) bh.textContent = `${BOOSTS[boost.kind]} · ${Math.ceil(boost.t)} s`;
    ghost.hidden = !dl;
    if (dl) {ghost.width = canvas.width / 2;ghost.height = canvas.height / 2;gctx.globalAlpha = .35 * dl;const off = Math.sin(performance.now() / 650) * 12 * dl;gctx.drawImage(canvas, off, Math.cos(performance.now() / 900) * 4 * dl, ghost.width, ghost.height);}
  }

  // ---------- Test / inspection handle ----------
  window.haarklousGame = {player, vehicles, site, paintball, groundAt, heli, plane, drone, watch, fire, stash, shops, sculpture, wallet, get basket() {return basket;}, payAt, takeProduct, useItem, earn, openBag, takeBoost, get boost() {return boost;}, set boost(b) {boost = b;}, get mp() {return mp;}, sitDown, standUp, tryDrive, tryRide, openChat, startDrone, stopDrone, floorBelow, doors, mini, music, photo, map, world, apt, towers, walkWorld, get caseGame() {return caseGame;}, place, toggleDoor, enterVehicle, exitVehicle, startMinigame, startMission, drink, interact, findPrompt, renderFrame, begin, get fps() {return fpsAvg;}, kick: () => requestAnimationFrame(frame), get drunk() {return drunk;}, set drunk(v) {drunk = v;}, setStance, camera, scene};
  loading.remove();$('start').hidden = false;
  requestAnimationFrame(frame);
})();
