/* Runs inside haarklous-game.html (tools/web-export/export.mjs): gameplay data for the Unreal version, written to
 * unreal/Haarklous/Content/Data. Everything stays in the web game's frame (metres, y up, three.js axes);
 * the C++ code converts to Unreal coordinates in one place (HKMath.h).
 *   game.json            walk-collision floors and obstacles, doors, drinks, towers, fire, vehicles, minigames, mission
 *   surroundings.json    the web game's surroundings-data.js (OpenStreetMap + Kartverket), unchanged
 *   apartment.json       the web game's apartment-data.js (frozen design), unchanged
 *   surface-classes.bin  1024 x 1024 bytes: surfaceAt() per cell (0 grass, 1 asphalt, 2 gravel, 3 sand, 4 none = inside a building footprint)
 */
(() => {
  'use strict';
  const G = window.haarklousGame, T = window.THREE, W = G.world, D = W.data, apt = G.apt, files = window.__hkFiles, log = window.__hkLog;
  const BIG = 1e9, num = v => v === Infinity ? BIG : v === -Infinity ? -BIG : v;
  const pts = list => list.map(p => [num(p.x), num(p.z)]);
  const mat = m => Array.from(m.elements);
  const v3 = v => [num(v.x), num(v.y), num(v.z)];
  const floor = f => ({name: f.name || '', points: pts(f.points), y: num(f.y ?? 0), slope: f.slope ? {x: num(f.slope.x || 0), z: num(f.slope.z || 0), origin: [num(f.slope.origin.x), num(f.slope.origin.z)]} : null,
    ceilingHeight: num(f.ceilingHeight ?? Infinity), surfaceKind: f.surfaceKind || '', terrain: typeof f.height === 'function'});
  const obstacle = o => ({points: pts(o.points), minY: num(o.minY ?? -Infinity), maxY: num(o.maxY ?? Infinity), kind: o.kind || ''});
  const ww = G.walkWorld;
  const out = {};
  out.meta = {theta: D.geo.theta, geo: D.geo, ground: D.ground, playHalf: W.playHalf, radius: ww.radius, clearance: ww.clearance, step: window.WalkCollision.STEP,
    sunDir: (() => {const b = 225 * Math.PI / 180, e = 38 * Math.PI / 180, x = Math.sin(b), z = -Math.cos(b), th = D.geo.theta;return v3(new T.Vector3((x * Math.cos(th) + z * Math.sin(th)) * Math.cos(e), Math.sin(e), (-x * Math.sin(th) + z * Math.cos(th)) * Math.cos(e)).normalize());})()};
  out.floors = ww.floors.map(floor);
  // walkWorld.obstacles = [apartment, stair towers, surroundings, firepit, stashes, shops, sculpture, extra]; "extra" is the
  // web game's copy of our construction-site fence and paintball walls (game-site.js, game-paintball.js): the C++ adds
  // its own (AHKGame::SiteObstacles, FHKPaintball::Obstacles), so they are left out here.
  const own = apt.obstacles.length + G.towers.reduce((s, t) => s + t.stair.obstacles().length, 0) + W.obstacles().length + G.fire.obstacles.length + G.stash.obstacles.length + G.shops.obstacles.length + G.sculpture.obstacles.length;
  const extra = ww.obstacles.slice(own);
  // (The walk world keeps no 'kind', so check the tail against the paintball obstacles and the fence panels' 4 corners.)
  {const pb = G.paintball ? G.paintball.obstacles : [], tail = extra.slice(extra.length - pb.length);
    if (tail.some((o, i) => Math.abs(o.points[0].x - pb[i].points[0].x) > 1e-9) || extra.slice(0, extra.length - pb.length).some(o => o.points.length !== 4)) throw new Error(`unexpected obstacles after the sculpture: ${extra.length}, paintball ${pb.length}`);}
  out.obstacles = ww.obstacles.slice(0, own).map(obstacle);
  out.boundary = ww.boundary.map(b => ({a: [num(b.a.x), num(b.a.z)], b: [num(b.b.x), num(b.b.z)], floor: ww.floors.indexOf(b.floor)}));
  // Vehicles and aircraft collide with buildings, trees, hedges, stair towers and the firepit, not with the flat's furniture.
  // walkWorld.obstacles = [apartment, stair towers, surroundings, firepit]; vehicle worlds use [surroundings, towers, firepit].
  out.counts = {aptFloors: apt.floors.length, aptObstacles: apt.obstacles.length, towers: G.towers.length,
    towerObstacles: G.towers.reduce((s, t) => s + t.stair.obstacles().length, 0), fireObstacles: G.fire.obstacles.length, worldObstacles: W.obstacles().length,
    stashObstacles: G.stash.obstacles.length, shopObstacles: G.shops.obstacles.length, sculptureObstacles: G.sculpture.obstacles.length, webSiteObstacles: extra.length};
  // Doors: hinge pose when closed (world matrix of the hinge's parent, hinge local pose), leaf box and its pose in the hinge.
  const doorInfo = d => {
    d.hinge.updateMatrixWorld(true);const p = d.leaf.geometry.parameters || {};
    const leafLocal = new T.Matrix4().copy(d.hinge.matrixWorld).invert().multiply(d.leaf.matrixWorld);
    return {id: d.id, name: d.name, motion: d.motion || 'swing', openAngle: num(d.openAngle ?? 0), closedX: num(d.closedX ?? 0), travel: num(d.travel ?? 0), sound: d.sound || '',
      parentWorld: mat(d.hinge.parent.matrixWorld), hingePos: v3(d.hinge.position), hingeQuat: d.hinge.quaternion.toArray().map(num), hingeScale: v3(d.hinge.scale),
      leafBox: [num(p.width ?? (p.radiusTop ?? 0) * 2), num(p.height ?? 0), num(p.depth ?? (p.radiusTop ?? 0) * 2)], leafLocal: mat(leafLocal),
      inApartment: apt.doors.includes(d), tower: G.towers.findIndex(t => t.stair.doors.includes(d))};
  };
  G.doors.forEach(d => {if (d.open || d.anim) throw new Error('door open at export');});
  out.doors = G.doors.map(doorInfo);
  out.drinks = apt.drinks.map(d => ({kind: d.kind, name: d.name, sips: d.sips ?? 0, world: mat(d.object.matrixWorld), liquid: !!d.object.userData.liquid}));
  out.apartment = {
    rooms: apt.rooms.map(r => ({id: r.id, name: r.name, points: r.polygon.map(apt.xy).map(([x, z]) => [num(x), num(z)])})),
    spawnAreas: apt.spawnAreas.map(a => ({name: a.name, points: pts(a.points), ceilingHeight: num(a.ceilingHeight ?? Infinity)})),
    heights: apt.heights,
    lights: apt.lights.map(l => {l.updateMatrixWorld(true);return {pos: v3(l.getWorldPosition(new T.Vector3())), color: l.color.getHex(), intensity: l.intensity, distance: l.distance, decay: l.decay};}),
    entrance: (() => {const d = apt.doors.find(d => d.name === 'Entrance door');return d ? v3(d.leaf.getWorldPosition(new T.Vector3())) : [1.66, 0, 5.83];})(),
    floorNames: apt.floors.map(f => f.name || f.id || '')
  };
  const S = window.Stairwell;
  out.towers = G.towers.map((t, i) => ({own: !!t.own, building: t.building ? D.buildings.indexOf(t.building) : -1, matrix: mat(t.matrix), inverse: mat(t.inverse)}));
  out.stair = {STOREY: S.STOREY, GROUND: S.GROUND, X0: S.X0, X1: S.X1, XA: S.XA, ZB: S.ZB, ZL: S.ZL, ZH: S.ZH, ZW: S.ZW, ZO: S.ZO,
    doorOutside: G.towers[0].stair.doorOutside, landingStart: G.towers[0].stair.landingStart, ownFloors: G.towers[0].stair.floors().map(floor),
    lamps: (() => {const s = G.towers[0].stair, r = [];for (let y = -12; y <= 2.5; y += .25) for (const z of [5.5, 7.5, 9.5, 10.8]) {const l = s.lampAt({x: .1, z}, y);r.push({z, feet: y, lamp: l ? v3(l) : null});}return r;})()};
  out.fire = {center: v3(G.fire.center), spots: G.fire.spots.map(s => ({id: s.id, x: num(s.x), y: num(s.y), z: num(s.z), yaw: num(s.yaw)})), woodpile: [num(G.fire.woodpile.x), num(G.fire.woodpile.z)], crate: [num(G.fire.crate.x), num(G.fire.crate.z)],
    obstacles: G.fire.obstacles.map(obstacle), DECAY_S: G.fire.DECAY_S, MAX: G.fire.MAX, pos: [-2.7, 34.7]};
  out.mission = {mark: [4.8, 14.2], start: D.vehicles.missionStart};
  out.air = {heli: {x: 26, z: 14, yaw: .6}, plane: {x: -116.5, z: 0, yaw: .5236}};
  // Without the web game's copy of our site machines (appended after its own vehicles, game-site.js).
  const siteMachines = new Set(G.site ? G.site.machines : []);
  out.vehicles = G.vehicles.filter(v => !siteMachines.has(v)).map((v, i) => ({i, kind: v.kind, name: v.name, x: num(v.x), z: num(v.z), y: num(v.y), yaw: num(v.yaw), length: v.length, width: v.width, height: v.height, radius: v.radius,
    model: v.model ?? null, color: v.color ?? null, spawn: v.spawn, footprint: obstacle(v.footprint ? [].concat(v.footprint())[0] : {points: []})}));
  out.dynamicAtStart = (ww.dynamic || []).map(obstacle);
  // Minigame state that is plain data.
  const plainOf = (o, depth = 0) => {
    if (o === null || typeof o !== 'object') return typeof o === 'function' ? undefined : num(o);
    if (o.isObject3D || o.isMaterial || o.isBufferGeometry) return undefined;
    if (o.isVector3) return v3(o);if (o.isVector2) return [o.x, o.y];
    if (depth > 4) return undefined;
    if (Array.isArray(o)) return o.map(x => plainOf(x, depth + 1));
    const r = {};for (const [k, v] of Object.entries(o)) {const p = plainOf(v, depth + 1);if (p !== undefined) r[k] = p;}return r;
  };
  out.minigames = {discgolf: plainOf(G.mini.discgolf), petanque: plainOf(G.mini.petanque)};
  // Hidden stashes (game-stash.js, game-main.js "Stash boosts"): crates, items (the holder's world position) and hit boxes.
  const box = h => {h.updateMatrixWorld(true);const p = h.geometry.parameters;return {world: mat(h.matrixWorld), size: [p.width, p.height, p.depth]};};
  out.stash = {cooldown: G.stash.COOLDOWN, places: [...new Set(G.stash.items.map(it => it.place))].map(p => ({name: p.name, x: num(p.x), y: num(p.y), z: num(p.z), yaw: num(p.yaw || 0)})),
    items: G.stash.items.map(it => ({kind: it.kind, place: G.stash.items.map(x => x.place).filter((x, i, a) => a.indexOf(x) === i).indexOf(it.place), x: num(it.x), y: num(it.y), z: num(it.z), hit: box(it.hit)})),
    obstacles: G.stash.obstacles.map(obstacle)};
  // Shops (game-shops.js): each shop's frame (the same steps as create()), the products and the hit boxes to aim at.
  {
    const SH = window.GameShops, shops = G.shops.shops;
    out.shops = {pant: SH.PANT, products: SH.PRODUCTS.map(p => ({id: p.id, name: p.name, price: p.price, beer: !!p.beer, color: p.color, where: p.where, use: p.use, sober: p.sober ?? 0})),
      shops: shops.map(s => {
        const L = s.local, w0 = W.toWorld(...L.P0), w1 = W.toWorld(...L.P1), wn = W.toWorld(L.P0[0] + L.n[0], L.P0[1] + L.n[1]);
        let ux = (w1.x - w0.x) / L.W, uz = (w1.z - w0.z) / L.W, origin = w0;const nx0 = wn.x - w0.x, nz0 = wn.z - w0.z;
        if (ux * nz0 - uz * nx0 < 0) {origin = w1;ux = -ux;uz = -uz;}
        return {id: s.id, name: s.name, brand: s.brand, hours: s.hours, shirt: s.shirt, priceFactor: s.priceFactor, building: s.building, floorY: num(s.floorY), W: num(s.W), D: num(s.D),
          uDoor: num(s.W - 2.0), origin: [num(origin.x), num(origin.z)], u: [num(ux), num(uz)], v: [num(-uz), num(ux)], prices: SH.PRODUCTS.map(p => G.shops.priceOf(s, p))};
      }),
      hitboxes: G.shops.hitboxes.map(h => {const u = h.userData;return {kind: u.shopItem ? 'item' : u.shopTill ? 'till' : 'pant', shop: shops.indexOf(u.shopItem ? u.shopItem.shop : u.shopTill || u.shopPant), product: u.shopItem ? u.shopItem.product.id : '', ...box(h)};}),
      floors: G.shops.floors.length, obstacles: G.shops.obstacles.length};
  }
  // HODET N.N. (game-sculpture.js): place, facing and base height; its ring and steps are in floors / obstacles.
  out.sculpture = {at: G.sculpture.at, yaw: num(G.sculpture.yaw), base: num(G.sculpture.base), height: window.GameSculpture.HEIGHT};
  // The real Muselunden DiscGolfPark (muselunden-data.js): its name and the 12 holes in the local frame.
  out.muselunden = window.MUSELUNDEN || null;
  out.trees = W.trees.map(t => [num(t.x), num(t.s), num(t.h)]);
  files.game = {json: JSON.stringify(out), bin: null};
  files.surroundings = {json: JSON.stringify(D), bin: null};
  files.apartment = {json: JSON.stringify(window.APARTMENT), bin: null};
  // surfaceAt() per cell of its 1024 x 1024 class map (cell centres, local frame), so footsteps sound the same.
  {
    const N = 1024, H = W.playHalf, names = ['grass', 'asphalt', 'gravel', 'sand', 'none'], cls = new Uint8Array(N * N);
    for (let j = 0; j < N; j++) for (let i = 0; i < N; i++) {const lx = -H + (i + .5) / N * 2 * H, ls = -H + (j + .5) / N * 2 * H, p = W.toWorld(lx, ls), s = W.surfaceAt(p.x, p.z);cls[j * N + i] = Math.max(0, names.indexOf(s));}
    files['surface-classes'] = {json: '', bin: cls};
  }
  log.push(`game.json: ${out.floors.length} floors, ${out.obstacles.length} obstacles, ${out.boundary.length} boundary pieces, ${out.doors.length} doors, ${out.vehicles.length} vehicles, counts ${JSON.stringify(out.counts)}`);
  window.__hkDone = true;
})();
