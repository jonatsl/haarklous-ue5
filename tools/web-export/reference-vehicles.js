/* Runs inside haarklous-game.html (tools/web-export/export.mjs --only=tests --script=reference-vehicles.js):
 * reference answers for the vehicle and aircraft port (Private/Tests/HKVehiclesTests.cpp), written to
 * unreal/Haarklous/Content/Data/tests/vehicles.json.
 *  - list: the game's own 41 vehicles (kind, name, position, yaw, paint colour, spawn = netGet() at start).
 *  - scenarios: fresh vehicles made with the game's own classes, driven with fixed steps (1/60 s) and scripted inputs;
 *    after every step the state is recorded. The C++ test replays the same inputs and compares.
 * The scripted "pilots" below read the vehicle state to choose the next input; the inputs are recorded, so the C++ side
 * only replays them. Vehicle worlds and the walk world are used without their dynamic obstacles (other vehicles, doors)
 * during the recording (restored afterwards), so both sides see only the static map.
 * Math.random (only used for the excavator's scrape sound) is a seeded Park-Miller generator during the recording.
 */
(() => {
  'use strict';
  const G = window.haarklousGame, T = window.THREE, C = window.WalkCollision, W = G.world, files = window.__hkFiles, log = window.__hkLog;
  const heightAt = W.heightWorld, DT = 1 / 60, ww = G.walkWorld;
  const BIG = 1e9;
  const num = v => typeof v === 'boolean' ? (v ? 1 : 0) : v === undefined || v === null ? 0 : v === Infinity ? BIG : v === -Infinity ? -BIG : Math.round(v * 1e9) / 1e9;

  // ---------- Sandbox: no dynamic obstacles, seeded Math.random ----------
  const orig = {canStand: C.canStand, hitsSolid: C.hitsSolid, random: Math.random}, saved = new Map();
  const grab = w => {if (w && w !== ww && !saved.has(w)) {saved.set(w, w.dynamic || []);C.setDynamic(w, []);}};
  C.canStand = (w, ...a) => {grab(w);return orig.canStand(w, ...a);};
  C.hitsSolid = (w, ...a) => {grab(w);return orig.hitsSolid(w, ...a);};
  const wwDynamic = ww.dynamic || [];C.setDynamic(ww, []);
  let seed = 777;Math.random = () => (seed = (seed * 16807) % 2147483647) / 2147483647;
  let fakeNow = null;const realNow = performance.now;performance.now = function () {return fakeNow !== null ? fakeNow : realNow.call(performance);};

  const out = {dt: DT, randomSeed: 777, list: [], scenarios: []};
  const summary = [];
  try {
    // ---------- The game's vehicle list ----------
    const paintOf = v => {
      if (v.kind !== 'car' && v.kind !== 'motorcycle') return null;
      let c = null;
      v.group.traverse(o => {if (c !== null || !o.isMesh) return;for (const m of [].concat(o.material)) if (m.isMeshPhysicalMaterial && m.clearcoat === 1 && (v.kind === 'car' ? m.clearcoatRoughness === .06 && m.metalness === .6 : m.clearcoatRoughness === .08 && m.metalness === .55)) {c = m.color.getHex();break;}});
      return c;
    };
    out.list = G.vehicles.map((v, i) => ({i, kind: v.kind, name: v.name, x: v.x, y: v.y, z: v.z, yaw: v.yaw, paint: paintOf(v), spawn: v.spawn}));

    // ---------- Helpers ----------
    const ctorOf = kind => (kind === 'drone' ? G.drone : G.vehicles.find(v => v.kind === kind)).constructor;
    const resolve = (v, path) => {
      if (path === 'root') return v.group;
      let o = v;for (const part of path.split('.')) {const m = /^(\w+)\[(\d+)\]$/.exec(part);o = m ? o[m[1]][+m[2]] : o[part];}
      return o;
    };
    const visibleInTree = (o, root) => {for (let p = o; p; p = p.parent) {if (!p.visible) return false;if (p === root) break;}return true;};
    const nodeVals = (v, path) => {
      const o = resolve(v, path), p = new T.Vector3(), q = new T.Quaternion(), s = new T.Vector3();
      o.matrixWorld.decompose(p, q, s);return [...p.toArray(), ...q.toArray(), ...s.toArray(), visibleInTree(o, v.group)];
    };
    const camArr = c => [...c.position.toArray(), ...c.quaternion.toArray()];
    const seatArr = v => {const s = v.seat();return [s.x, s.y, s.z, s.yaw, s.pitch, s.roll || 0, s.lateral || 0];};
    const footArr = v => [].concat(v.footprint()).flatMap(o => [...o.points.flatMap(p => [p.x, p.z]), o.minY, o.maxY]);
    const hipArr = (v, seat) => {const h = v.hipAnchor(seat);if (!h) return [0, 0, 0, 0];return [1, ...h.pos.toArray()];};
    const inp = (o = {}) => {const a = [o.t || 0, o.b || 0, o.s || 0, o.l1 ? 1 : 0, o.r1 ? 1 : 0, o.l2 || 0, o.r2 || 0, o.lx || 0, o.ly || 0, o.rx || 0, o.ry || 0];if (o.drunk) a.push(o.drunk);return a;};
    const clamp = (x, a, b) => Math.max(a, Math.min(b, x));

    function scenario(name, kind, ctor, v, fields, nodes, extra = {}) {
      const sc = {name, kind, ctor, dt: DT, fields: fields.map(f => f[0]), sizes: null, nodes, steps: [], ...extra};
      out.scenarios.push(sc);
      let count = 0;
      // Every step keeps its input; the state is recorded after every 3rd update (and every other operation), node poses every 9th.
      const rec = step => {
        if (step.op !== 'u' || count % 3 === 0) {
          const vals = [], sizes = [];
          for (const [, fn] of fields) {const r = fn();const a = Array.isArray(r) ? r : [r];sizes.push(a.length);vals.push(...a.map(num));}
          if (!sc.sizes) sc.sizes = sizes;
          step.v = vals;
          if (nodes.length && (count % 9 === 0 || step.op !== 'u')) {v.group && v.group.updateMatrixWorld(true);step.n = nodes.flatMap(p => nodeVals(v, p).map(num));}
          if (v.hud) step.h = v.broken ? 'CRASHED' : v.hud();
        }
        count++;sc.steps.push(step);
      };
      return {sc, rec};
    }
    const ground = kind => kind === 'motorcycle' || kind === 'car' || kind === 'excavator';
    // Drunk steps (a[11] > 0) see a scripted performance.now() (recorded as step.now) for the wobble.
    function drive(R, v, a) {
      const step = {op: 'u', in: a};
      if (a && a[11]) {fakeNow = 5000 + R.sc.steps.length * 1000 / 60;step.now = fakeNow;}
      if (a === null) v.update(DT, {});
      else if (ground(v.kind)) v.update(DT, {throttle: a[0], brake: a[1], steer: a[2], drunk: a[11] || 0, l1: !!a[3], r1: !!a[4], l2: a[5], r2: a[6], sticks: {lx: a[7], ly: a[8], rx: a[9], ry: a[10]}});
      else v.update(DT, {lx: a[7], ly: a[8], rx: a[9], ry: a[10], brake: a[1]});
      fakeNow = null;
      R.rec(step);
    }
    // n steps of input pick() (an array or a function of the step index), stopping early when until() is true.
    function run(R, v, n, pick, until = null) {for (let i = 0; i < n; i++) {if (until && until()) return i;drive(R, v, typeof pick === 'function' ? pick(i) : pick);}return n;}
    const groundFields = v => [['x', () => v.x], ['y', () => v.y], ['z', () => v.z], ['yaw', () => v.yaw], ['speed', () => v.speed], ['pitch', () => v.pitch], ['roll', () => v.roll || 0],
      ['terrainRoll', () => v.terrainRoll || 0], ['active', () => v.active], ['seat', () => seatArr(v)], ['net', () => v.netGet()], ['foot', () => footArr(v)], ['hipD', () => hipArr(v, 'd')], ['hipP', () => hipArr(v, 'p')]];

    // ---------- Motorcycle ----------
    const RUN = {x: -116.5, z: 0, yaw: .5236};   // the long grass strip in Torshovdalen (plane's runway)
    {
      // A straight run of 430 m along a street with nothing in the way (a 0.45 m circle and 2 m either side), to reach 6th gear.
      const probe = new (ctorOf('motorcycle'))(0, 20, 0);probe.clear(probe.x, probe.z);
      const w45 = [...saved.keys()].find(w => Math.abs(w.radius - .45) < 1e-9);
      const open = (x, z, yaw, L) => {
        const f = {x: -Math.sin(yaw), z: -Math.cos(yaw)}, r = {x: -f.z, z: f.x};let prev = heightAt(x, z);
        for (let d = 0; d <= L; d += 1) for (const o of [0, -2, 2]) {const p = {x: x + f.x * d + r.x * o, z: z + f.z * d + r.z * o}, y = heightAt(p.x, p.z);if (!orig.canStand(w45, p, 1.2, y)) return false;if (!o) {if (Math.abs(y - prev) > .5) return false;prev = y;}}
        return true;
      };
      let line = null;
      for (const road of W.data.roads) {
        if (line) break;
        for (let k = 0; k + 3 < road.p.length && !line; k += 2) {
          const a = W.toWorld(road.p[k], road.p[k + 1]), b = W.toWorld(road.p[k + 2], road.p[k + 3]);
          for (const [s, e] of [[a, b], [b, a]]) {const yaw = Math.atan2(-(e.x - s.x), -(e.z - s.z));if (open(s.x, s.z, yaw, 430)) {line = {x: s.x, z: s.z, yaw, road: road.n || road.k};break;}}
        }
      }
      if (!line) throw new Error('no straight run for the motorcycle');
      const ctor = [line.x, line.z, line.yaw, 0x9e1b20], v = new (ctorOf('motorcycle'))(...ctor);
      const R = scenario('motorcycle', 'motorcycle', ctor, v, [...groundFields(v), ['lean', () => v.lean], ['bar', () => v.bar], ['gear', () => v.gear], ['rpm', () => v.rpm], ['wheelSpin', () => v.wheelSpin]],
        ['root', 'body', 'front', 'fw', 'rw', 'steer', 'bars', 'hands', 'arms']);
      R.rec({op: 'init'});
      run(R, v, 30, null);
      v.enter();R.rec({op: 'enter'});
      // Low speed: the bars steer directly (a little to each side), then hold the line's heading while accelerating.
      run(R, v, 45, inp({t: .3, s: .5}));
      run(R, v, 45, inp({t: .3, s: -.5}));
      const hold = (t, b = 0) => () => inp({t, b, s: clamp((v.yaw - line.yaw) * 4, -.4, .4)});
      let maxGear = 0, maxV = 0;
      run(R, v, 1100, hold(1), () => {maxGear = Math.max(maxGear, v.gear);maxV = Math.max(maxV, v.speed);return v.gear === 6 && v.speed > 41;});
      run(R, v, 400, hold(0, 1), () => v.speed < 15);
      run(R, v, 100, inp({t: .6, s: .6}));
      run(R, v, 60, inp({t: .4, s: -.5}));
      run(R, v, 700, inp({b: 1}), () => v.speed < .3);
      run(R, v, 100, inp({b: 1}));
      run(R, v, 30, inp({}));
      const e = v.exit();R.rec({op: 'exit', r: [num(e.x), num(e.z)]});
      run(R, v, 40, null);
      const a = [RUN.x + 3, -6.5, RUN.z - 2, .7, .02, -.1, 5, .2, .15];v.netSet(a);R.rec({op: 'net', a});
      v.netAnimate(DT);R.rec({op: 'anim'});
      summary.push(`motorcycle: ${line.road} from ${line.x.toFixed(1)}, ${line.z.toFixed(1)} yaw ${line.yaw.toFixed(3)}; ${R.sc.steps.length} steps, top ${(maxV * 3.6).toFixed(1)} km/h, gear ${maxGear}, ended at ${v.x.toFixed(1)}, ${v.z.toFixed(1)}`);
    }

    // ---------- Car on the strip ----------
    {
      const ctor = [RUN.x, RUN.z, RUN.yaw, 4], v = new (ctorOf('car'))(...ctor);
      const R = scenario('car', 'car', ctor, v, [...groundFields(v), ['steerAngle', () => v.steerAngle], ['rpm', () => v.rpm], ['wheelSpin', () => v.wheelSpin]],
        ['root', 'body', 'wheels[0].holder', 'wheels[0].wheel', 'wheels[1].holder', 'wheels[3].wheel', 'wheelPivot', 'steeringWheel', 'hands', 'wrists[0]', 'wrists[1]', 'arms', 'forearms[0]', 'forearms[1]']);
      R.rec({op: 'init'});
      run(R, v, 20, null);
      v.enter();R.rec({op: 'enter'});
      let maxV = 0;
      run(R, v, 360, i => inp({t: 1, s: i > 240 ? .3 : 0}), () => {maxV = Math.max(maxV, v.speed);return false;});
      run(R, v, 90, inp({t: .5, s: -1}));
      run(R, v, 400, inp({b: 1}), () => v.speed < .3);
      run(R, v, 150, inp({b: 1, s: .6}));
      run(R, v, 90, inp({}));
      run(R, v, 120, inp({t: .4, s: -.4}));
      run(R, v, 200, inp({}), () => v.speed === 0);
      const e = v.exit();R.rec({op: 'exit', r: [num(e.x), num(e.z)]});
      run(R, v, 30, null);
      const a = [RUN.x - 4, -6.2, RUN.z + 3, -.4, .01, .02, 3];a.push(.25);v.netSet(a);R.rec({op: 'net', a});
      v.netAnimate(DT);R.rec({op: 'anim'});
      summary.push(`car: ${R.sc.steps.length} steps, top ${(maxV * 3.6).toFixed(1)} km/h`);
    }

    // ---------- Car into a building (collision stop) ----------
    {
      const probe = new (ctorOf('car'))(0, 20, 0, 0);probe.clear(probe.x, probe.z);
      const w95 = [...saved.keys()].find(w => Math.abs(w.radius - .95) < 1e-9);
      // Find a direction from a street point near Haarklous plass with a clear run of 15-35 m ending at a wall.
      let best = null;
      for (const [sx, sz] of [[0, 20], [-10, 28], [14, 27], [-20, 28]]) {
        for (let k = 0; k < 72 && !best; k++) {
          const yaw = k / 72 * Math.PI * 2, f = {x: -Math.sin(yaw), z: -Math.cos(yaw)};let d = 0, blocked = -1, prevY = heightAt(sx, sz);
          for (; d < 40; d += .5) {const p = {x: sx + f.x * d, z: sz + f.z * d}, y = heightAt(p.x, p.z);if (!orig.canStand(w95, p, 1.2, y) || Math.abs(y - prevY) > .3) {blocked = d;break;}prevY = y;}
          if (blocked > 15 && blocked < 35) best = {x: sx, z: sz, yaw, blocked};
        }
        if (best) break;
      }
      const ctor = [best.x, best.z, best.yaw, 1], v = new (ctorOf('car'))(...ctor);
      const R = scenario('carCrash', 'car', ctor, v, [...groundFields(v), ['steerAngle', () => v.steerAngle], ['rpm', () => v.rpm], ['wheelSpin', () => v.wheelSpin]], ['root', 'wheels[0].wheel']);
      R.rec({op: 'init'});v.enter();R.rec({op: 'enter'});
      let stopped = -1;run(R, v, 600, inp({t: 1}), () => {if (v.speed === 0 && R.sc.steps.length > 30) {stopped = R.sc.steps.length;return true;}return false;});
      run(R, v, 30, inp({t: 1}));
      summary.push(`carCrash: wall at ${best.blocked} m (yaw ${best.yaw.toFixed(3)}), stopped at step ${stopped} after ${Math.hypot(v.x - best.x, v.z - best.z).toFixed(1)} m`);
    }

    // ---------- Excavator ----------
    {
      const ex = G.vehicles.find(v => v.kind === 'excavator'), ctor = [ex.x, ex.z, -.4], v = new (ctorOf('excavator'))(...ctor);
      const dumps = [];v.onDump = (x, z) => dumps.push([num(x), num(z), R.sc.steps.length]);
      const tipW = () => v.tip.getWorldPosition(new T.Vector3()).toArray();
      const R = scenario('excavator', 'excavator', ctor, v, [...groundFields(v), ['swing', () => v.swing], ['boom', () => v.boom], ['stickA', () => v.stickA], ['bucketA', () => v.bucketA], ['load', () => v.load],
        ['vl', () => v.vl], ['vr', () => v.vr], ['trackPhase', () => v.trackPhase], ['tip', tipW], ['dirtVisible', () => v.dirt.visible], ['handsVisible', () => v.levers[0].userData.hand.visible && v.levers[1].userData.hand.visible]],
        ['root', 'body', 'tracks[0]', 'tracks[1]', 'upper', 'cab', 'levers[0]', 'levers[1]', 'boomPivot', 'stickPivot', 'bucketPivot', 'dirt', 'tip', 'rams[0].barrel', 'rams[0].rod', 'rams[1].barrel', 'rams[1].rod', 'rams[2].barrel', 'rams[2].rod'], {dumps});
      R.rec({op: 'init'});
      run(R, v, 10, null);
      v.enter();R.rec({op: 'enter'});
      run(R, v, 120, inp({l1: true, r1: true}));
      run(R, v, 120, inp({l1: true, r2: 1}));
      run(R, v, 60, inp({l2: 1, r2: .5}));
      run(R, v, 60, inp({}));
      run(R, v, 50, inp({lx: .8}));run(R, v, 50, inp({lx: -.5}));run(R, v, 40, inp({lx: .1}));
      run(R, v, 60, inp({ly: -1}));run(R, v, 60, inp({ly: .7}));
      run(R, v, 50, inp({ry: 1}));run(R, v, 50, inp({ry: -.6}));
      run(R, v, 40, inp({rx: 1}));run(R, v, 60, inp({rx: -1}));
      // Dig: lower the boom until the teeth are in the ground, curl the bucket in until it is full.
      const tipDepth = () => {const t = v.tip.getWorldPosition(new T.Vector3());return t.y - heightAt(t.x, t.z);};
      run(R, v, 600, inp({ry: -1, ly: -.3}), () => tipDepth() < .05);
      run(R, v, 400, i => inp({rx: -1, ry: tipDepth() > .05 ? -.5 : 0}), () => v.load >= 1);
      const full = v.load;
      run(R, v, 150, inp({ry: 1}));
      run(R, v, 80, inp({lx: 1}));
      run(R, v, 300, inp({rx: 1}), () => dumps.length > 0 && v.load === 0);
      run(R, v, 30, inp({rx: 1, ry: .5}));
      // Push the bucket far into the ground: the boom lifts itself.
      run(R, v, 200, inp({ry: -1, ly: -1}), () => tipDepth() < -.5);
      run(R, v, 30, inp({}));
      const e = v.exit();R.rec({op: 'exit', r: [num(e.x), num(e.z)]});
      run(R, v, 30, null);
      const a = [ex.x + 1, -9.9, ex.z - 1, -.2, .01, 0, 0, .4, .1, -1.2, -.5, .6, .3, -.2];v.netSet(a);R.rec({op: 'net', a});
      v.netAnimate(DT);R.rec({op: 'anim'});
      summary.push(`excavator: ${R.sc.steps.length} steps, bucket full ${full}, dumps ${dumps.length}`);
    }

    // ---------- Aircraft ----------
    const craftFields = v => {let chase = null;return [['pos', () => v.pos.toArray()], ['vel', () => v.vel.toArray()], ['q', () => v.q.toArray()], ['w', () => v.w.toArray()],
      ['x', () => v.x], ['y', () => v.y], ['z', () => v.z], ['yaw', () => v.yaw], ['speed', () => v.speed], ['grounded', () => v.grounded], ['broken', () => v.broken], ['crashT', () => v.crashT], ['active', () => v.active],
      ['net', () => v.netGet()], ['cam', () => camArr(v.cameraPose(false))], ['chase', () => {const p = v.cameraPose(true, chase);chase = p.position.clone();return camArr(p);}], ['foot', () => footArr(v)],
      ['exitPt', () => {const e = v.exitPoint();return [e.x, e.z];}], ['hipD', () => hipArr(v, 'd')], ['hipP', () => hipArr(v, 'p')]];};
    const att = v => v.attitude(), agl = v => v.pos.y - v.gearH - heightAt(v.pos.x, v.pos.z);
    {
      const ctor = [26, 14, .6], v = new (ctorOf('helicopter'))(...ctor);let crashes = 0;v.onCrash = () => {crashes++;};
      const R = scenario('helicopter', 'helicopter', ctor, v, [...craftFields(v), ['rpm', () => v.rpm], ['rotorAngle', () => v.rotorAngle], ['bladeOpacity', () => v.bladeMat.opacity], ['discOpacity', () => v.disc.material.opacity]],
        ['root', 'rotor', 'disc', 'tailRotor', 'cyclic', 'collective', 'hands[0]', 'hands[1]', 'armGroup', 'arms[0].mesh', 'arms[1].mesh', 'arms[0].wrist']);
      R.rec({op: 'init'});
      run(R, v, 10, null);
      v.enter();R.rec({op: 'enter'});
      run(R, v, 560, inp({}));
      // Autopilot: hold a pitch, a bank and a climb rate with the sticks.
      const pilot = (pitchT, bankT, climbT, lx = 0) => () => {const a = att(v);return inp({ry: clamp((pitchT - a.pitch) * 3, -1, 1), rx: clamp((bankT - a.bank) * 2, -1, 1), ly: clamp((v.vel.y - climbT) * .4, -1, 1), lx});};
      let liftStep = -1;
      run(R, v, 900, pilot(0, 0, 2), () => {if (liftStep < 0 && !v.grounded) liftStep = R.sc.steps.length;return agl(v) > 6;});
      run(R, v, 90, pilot(0, 0, 0));
      // Land again on the pad (straight down), wait with the collective down.
      let landed = -1;
      run(R, v, 1200, () => pilot(0, 0, agl(v) < 2.5 ? -.4 : -1.2)(), () => {if (v.grounded) landed = R.sc.steps.length;return v.grounded || v.broken;});
      run(R, v, 60, inp({ly: .5}));
      // Up to 45 m, forward flight, a banked turn, slow down.
      run(R, v, 1200, pilot(0, 0, 5), () => agl(v) > 45);
      run(R, v, 60, pilot(0, 0, 0));
      run(R, v, 300, pilot(-.2, 0, 0));
      let maxV = 0;
      run(R, v, 240, pilot(-.1, .35, 0, .2), () => {maxV = Math.max(maxV, v.speed);return false;});
      run(R, v, 600, pilot(.15, 0, 0), () => -v.bodyVel().z < 2);
      run(R, v, 60, pilot(0, 0, 0));
      // Crash: roll hard to the right and let it fall.
      run(R, v, 900, inp({rx: 1}), () => v.broken);
      const crashStep = R.sc.steps.length;
      run(R, v, 200, inp({}));
      const e = v.exit();R.rec({op: 'exit', r: [num(e.x), num(e.z)]});
      run(R, v, 120, null);
      const a = [27, -8.5, 15, 0, .3, 0, .95, 4, .8, 0, 0];v.netSet(a);R.rec({op: 'net', a});
      v.netAnimate(DT);R.rec({op: 'anim'});
      summary.push(`helicopter: landed on the pad at step ${landed}, ${R.sc.steps.length} steps, lift-off at step ${liftStep}, top ${(maxV * 3.6).toFixed(0)} km/h, crashes ${crashes} (step ${crashStep}), back home ${v.grounded} at ${v.x.toFixed(2)}, ${v.z.toFixed(2)}`);
    }
    {
      const ctor = [-116.5, 0, .5236], v = new (ctorOf('plane'))(...ctor);let crashes = 0;v.onCrash = () => {crashes++;};
      const R = scenario('plane', 'plane', ctor, v, [...craftFields(v), ['throttle', () => v.throttle], ['prop', () => v.prop], ['bladeOpacity', () => v.bladeMat.opacity], ['discOpacity', () => v.pdisc.material.opacity]],
        ['root', 'propG', 'pdisc', 'elevator', 'rudder', 'ailerons[0]', 'ailerons[1]', 'stick', 'lever', 'hands[0]', 'hands[1]', 'armGroup', 'arms[0].mesh', 'arms[1].mesh']);
      R.rec({op: 'init'});
      run(R, v, 10, null);
      v.enter();R.rec({op: 'enter'});
      run(R, v, 90, inp({ly: -1}));
      const x0 = v.pos.x, z0 = v.pos.z;
      run(R, v, 1200, inp({}), () => v.speed * 3.6 > 87);
      const rollKmh = v.speed * 3.6, rollM = Math.hypot(v.pos.x - x0, v.pos.z - z0);
      let liftM = -1, liftKmh = -1;
      run(R, v, 400, inp({ry: .5}), () => {if (!v.grounded && liftM < 0) {liftM = Math.hypot(v.pos.x - x0, v.pos.z - z0);liftKmh = v.speed * 3.6;}return !v.grounded;});
      run(R, v, 120, inp({ry: .3}));
      run(R, v, 300, inp({ry: .15}));
      run(R, v, 60, inp({rx: .5, lx: .3}));
      run(R, v, 120, inp({}));
      run(R, v, 60, inp({ly: 1}));
      run(R, v, 240, inp({ry: .05}));
      const maxAlt = agl(v);
      run(R, v, 900, inp({ry: -.6, ly: -1}), () => v.broken);
      const crashStep = R.sc.steps.length;
      run(R, v, 200, inp({}));
      run(R, v, 30, inp({b: 1}));
      // Back on the strip after the reset: roll with full power, then brake to a stop with L2 and a little rudder.
      run(R, v, 80, inp({ly: -1}));
      run(R, v, 200, inp({}), () => v.speed > 15);
      run(R, v, 600, inp({b: 1, ly: 1, lx: .3}), () => v.speed < .3);
      const brakeStop = v.speed;
      run(R, v, 30, inp({b: 1}));
      const e = v.exit();R.rec({op: 'exit', r: [num(e.x), num(e.z)]});
      run(R, v, 60, null);
      const a = [-110, -5, 3, 0, .25, 0, .968, 30, 1800, .7, 0];v.netSet(a);R.rec({op: 'net', a});
      v.netAnimate(DT);R.rec({op: 'anim'});
      // Far out (beyond 1700 m) and nobody at the controls: it glides and turns back toward the neighbourhood.
      const far = [1650, 260, 420, 0, -.38, 0, .925, 42, 1500, .6, 0];v.netSet(far);R.rec({op: 'net', a: far});
      const yaw0 = v.yaw;
      run(R, v, 420, null, () => v.broken);
      const turned = v.yaw - yaw0;
      summary.push(`plane: ${R.sc.steps.length} steps, ${rollKmh.toFixed(1)} km/h after ${rollM.toFixed(1)} m, lift-off after ${liftM.toFixed(1)} m at ${liftKmh.toFixed(1)} km/h, alt ${maxAlt.toFixed(0)} m, crashes ${crashes} (step ${crashStep}), braked to ${brakeStop.toFixed(2)} m/s, far glide turned ${turned.toFixed(3)} rad`);
    }

    // ---------- Helicopter rotor strike: hover at 6 m and creep toward the trees and houses beside the pad ----------
    {
      const ctor = [26, 14, .6], v = new (ctorOf('helicopter'))(...ctor);let crashes = 0;v.onCrash = () => {crashes++;};
      const R = scenario('helicopterStrike', 'helicopter', ctor, v, [...craftFields(v), ['rpm', () => v.rpm], ['rotorAngle', () => v.rotorAngle], ['bladeOpacity', () => v.bladeMat.opacity], ['discOpacity', () => v.disc.material.opacity]],
        ['root', 'rotor', 'tailRotor']);
      R.rec({op: 'init'});
      v.enter();R.rec({op: 'enter'});
      run(R, v, 560, inp({}));
      const pilot = (pitchT, climbT) => () => {const a = att(v);return inp({ry: clamp((pitchT - a.pitch) * 3, -1, 1), rx: clamp(-a.bank * 2, -1, 1), ly: clamp((v.vel.y - climbT) * .4, -1, 1)});};
      run(R, v, 900, pilot(0, 2), () => agl(v) > 6);
      run(R, v, 90, pilot(0, 0));
      run(R, v, 1500, pilot(-.12, 0), () => v.broken);
      const at = [v.pos.x, v.pos.y, v.pos.z], sp = v.speed;
      run(R, v, 60, inp({}));
      summary.push(`helicopterStrike: ${R.sc.steps.length} steps, crashes ${crashes} at ${at.map(c => c.toFixed(1)).join(', ')} (agl ${(at[1] - heightAt(at[0], at[2])).toFixed(1)} m)`);
    }
    // ---------- Drunk driving (the wobble uses performance.now(), scripted here) ----------
    {
      const ctor = [RUN.x, RUN.z, RUN.yaw, 2], v = new (ctorOf('car'))(...ctor);
      const R = scenario('carDrunk', 'car', ctor, v, [...groundFields(v), ['steerAngle', () => v.steerAngle], ['rpm', () => v.rpm], ['wheelSpin', () => v.wheelSpin]], ['root']);
      R.rec({op: 'init'});v.enter();R.rec({op: 'enter'});
      run(R, v, 300, inp({t: .6, drunk: .8}));
      summary.push(`carDrunk: ${R.sc.steps.length} steps, yaw ${v.yaw.toFixed(3)}`);
    }
    {
      const ctor = [RUN.x, RUN.z, RUN.yaw, 0x1f3a63], v = new (ctorOf('motorcycle'))(...ctor);
      const R = scenario('motorcycleDrunk', 'motorcycle', ctor, v, [...groundFields(v), ['lean', () => v.lean], ['bar', () => v.bar], ['gear', () => v.gear], ['rpm', () => v.rpm], ['wheelSpin', () => v.wheelSpin]], ['root']);
      R.rec({op: 'init'});v.enter();R.rec({op: 'enter'});
      run(R, v, 300, inp({t: .5, drunk: .8}));
      summary.push(`motorcycleDrunk: ${R.sc.steps.length} steps, lean ${v.lean.toFixed(3)}`);
    }

    // ---------- FPV drone ----------
    const droneFields = d => [['pos', () => d.pos.toArray()], ['vel', () => d.vel.toArray()], ['q', () => d.q.toArray()], ['yawA', () => d.yawA], ['pitchA', () => d.pitchA], ['rollA', () => d.rollA],
      ['battery', () => d.battery], ['armed', () => d.armed], ['crashed', () => d.crashed], ['landed', () => d.landed], ['thrust', () => d.thrust], ['time', () => d.time], ['home', () => d.home.toArray()],
      ['cam', () => camArr(d.cameraPose())], ['osd', () => {const fl = G.floorBelow(d.pos, d.pos.y + .1), o = d.osd(fl ? fl.y : heightAt(d.pos.x, d.pos.z));return [o.battery, o.alt, o.speed, o.time, o.dist, o.armed, o.crashed, o.thrust];}]];
    const droneScenario = (name, x, z, yaw, script, indoor = false) => {
      const fl = G.floorBelow({x, z}, indoor ? .4 : heightAt(x, z) + .4), y = fl.y, d = new (ctorOf('drone'))();
      const env = {floor: G.floorBelow, solid: (p, a, b) => C.hitsSolid(ww, p, a, b), listener: {x: x + 2, y: y + 1.6, z: z + 1}};
      const R = scenario(name, 'drone', [x, y, z, yaw], {group: null, hud: null}, droneFields(d), [], {listener: [x + 2, y + 1.6, z + 1]});
      const osdText = () => {const fl2 = G.floorBelow(d.pos, d.pos.y + .1), o = d.osd(fl2 ? fl2.y : heightAt(d.pos.x, d.pos.z));return `${o.mode} ${o.volts}`;};
      const rec = step => {R.rec(step);if (step.v) step.h = osdText();};
      rec({op: 'init'});
      d.launch(x, y, z, yaw);rec({op: 'launch'});
      const fly = (n, a, until = null) => {for (let i = 0; i < n; i++) {if (until && until()) return;const b = typeof a === 'function' ? a(i) : a;d.update(DT, {lx: b[7], ly: b[8], rx: b[9], ry: b[10], brake: b[1]}, env);rec({op: 'u', in: b});}};
      const api = {d, fly, rec, stats: {}};script(api);
      summary.push(`${name}: ${R.sc.steps.length} steps, ${JSON.stringify(api.stats)}`);
    };
    droneScenario('drone', 6, 44, .3, ({d, fly, rec, stats}) => {
      fly(30, inp({}));
      fly(60, inp({ly: -1}));
      fly(60, inp({}));
      fly(120, inp({ry: -.7}));
      fly(60, inp({lx: .5, ry: -.3}));
      fly(60, inp({rx: .6}));
      fly(60, inp({}));
      d.setMode('acro');rec({op: 'mode', m: 'acro'});
      fly(30, inp({ly: -.3}));
      fly(26, inp({ry: 1, ly: -.2}));
      fly(30, inp({ly: -.5}));
      fly(20, inp({rx: -1}));
      fly(40, inp({ly: -.2, lx: .4}));
      fly(600, i => inp({ry: i < 12 ? -1 : 0, ly: -1}), () => d.crashed);
      stats.crashed = d.crashed;
      fly(20, inp({}));
      d.reset();rec({op: 'reset'});
      d.setMode('angle');rec({op: 'mode', m: 'angle'});
      fly(90, inp({ly: -.8}));
      d.battery = .004;rec({op: 'battery', b: .004});
      fly(900, inp({ly: -.3, ry: -.2}), () => !d.armed && (d.landed || d.crashed));
      stats.armedAfterEmpty = d.armed;stats.crashed2 = d.crashed;
      fly(30, inp({}));
      d.stop();rec({op: 'stop'});
    });
    // Indoors: the flat's ceiling (a fast climb into it is a crash), then a wall.
    {
      // The most open spot of the flat (rooms with a ceiling): nothing solid up to the ceiling there, and the most free points
      // on rings of 0.35 and 0.7 m around it.
      let cx = 0, cz = 0, best = -1;
      for (const f of G.apt.floors.filter(f => f.ceilingHeight < Infinity)) {
        const xs = f.points.map(p => p.x), zs = f.points.map(p => p.z);
        for (let x = Math.min(...xs); x <= Math.max(...xs); x += .1) for (let z = Math.min(...zs); z <= Math.max(...zs); z += .1) {
          const p = {x, z}, fl = G.floorBelow(p, .4);if (!fl || Math.abs(fl.y) > .01 || C.hitsSolid(ww, p, 0, 2.4)) continue;
          let free = 0;
          for (const rr of [.35, .7]) for (let k = 0; k < 16; k++) {const q = {x: x + Math.cos(k / 8 * Math.PI) * rr, z: z + Math.sin(k / 8 * Math.PI) * rr};if (!C.hitsSolid(ww, q, 0, 2.4) && G.floorBelow(q, .4)) free++;}
          if (free > best) {best = free;cx = x;cz = z;}
        }
      }
      droneScenario('droneIndoor', cx, cz, 0, ({d, fly, rec, stats}) => {
        fly(20, inp({}));
        fly(300, inp({ly: -1}), () => d.crashed);
        stats.ceilingCrash = d.crashed;stats.y = d.pos.y;
        d.reset();rec({op: 'reset'});
        fly(40, inp({ly: -.2}));
        fly(400, inp({ry: -1}), () => d.crashed);
        stats.wallCrash = d.crashed;
        fly(10, inp({}));
      }, true);
    }
  } finally {
    C.canStand = orig.canStand;C.hitsSolid = orig.hitsSolid;Math.random = orig.random;delete performance.now;
    for (const [w, list] of saved) C.setDynamic(w, list);
    C.setDynamic(ww, wwDynamic);
  }
  files.vehicles = {json: JSON.stringify(out), bin: null};
  log.push(...summary, `vehicles.json: ${out.list.length} vehicles, ${out.scenarios.length} scenarios, ${out.scenarios.reduce((s, c) => s + c.steps.length, 0)} steps`);
  window.__hkDone = true;
})();
