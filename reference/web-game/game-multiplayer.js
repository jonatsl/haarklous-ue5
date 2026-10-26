/* Multiplayer glue between the game and the lobby (game-net.js): remote players as avatars (on foot, on the firepit
 * benches, in vehicle seats, flying a drone), vehicles driven by other players (smoothed between their updates), seat
 * claims (one driver and one passenger per vehicle), chat, doors and the shared firepit level.
 * GameMultiplayer.create(ctx) -> {net, voice, update(dt), claim(i, seat) -> Promise<{ok, by}>, release(i, seat, final),
 *   occupant(i, seat), remoteDriven(i), sendChat(text), sendDoor(id, open), addWood(), fireLevel(), remotes, nameOf(id),
 *   sitTaken(spotId), setMic(on)}
 * ctx: {T, lib, scene, camera, vehicles, localState(), drivingIndex(), listener(), applyDoor(id, open, instant), onChat(line, mine),
 *   onStatus(), notice(text, s), fireDecayS, fireMax}
 */
(function (root, factory) {
  root.GameMultiplayer = factory();
})(typeof window === 'undefined' ? globalThis : window, function () {
  'use strict';
  function create(ctx) {
    const {T, lib, scene, vehicles} = ctx, avatars = window.GameAvatars.create({T, lib});
    const remotes = new Map();// id -> {id, slot, name, st, av, pos, yaw, speed, drone}
    let occ = {}, fire = {level: 2, t: Date.now()}, sendT = 0, vehT = 0, lastLocal = null;
    const pending = new Map();// vehicle index -> resolve
    const vehTargets = new Map();// index -> {a, prev, at, prevAt}
    const net = window.GameNet.create({onMessage, onStatus: () => ctx.onStatus()});
    const voice = window.GameVoice.create({send: (to, d) => net.send({t: 'rtc', to, d}), you: () => net.you});
    function remote(id) {
      let r = remotes.get(id);if (r) return r;const p = net.players.get(id) || {slot: 0, name: 'Player'};
      const av = avatars.avatar(p.slot, p.name);av.group.visible = false;scene.add(av.group);
      r = {id, slot: p.slot, name: p.name, st: null, av, pos: new T.Vector3(), yaw: 0, speed: 0, drone: null, seen: false};remotes.set(id, r);return r;
    }
    function drop(id) {const r = remotes.get(id);if (!r) return;scene.remove(r.av.group);r.av.dispose();if (r.drone) scene.remove(r.drone);remotes.delete(id);voice.peerLeft(id);}
    function onMessage(m) {
      switch (m.t) {
        case 'welcome':
          for (const id of [...remotes.keys()]) drop(id);
          for (const p of m.players) if (p.id !== m.you) {remote(p.id);voice.peerJoined(p.id);}
          for (const [id, s] of Object.entries(m.states || {})) {const r = remote(+id);r.st = s;}
          occ = m.occ || {};fire = m.fire || fire;vehTargets.clear();
          for (const [i, s] of Object.entries(m.vehicles || {})) {const v = vehicles[+i];if (v && s) v.netSet(s);}
          for (const [id, open] of Object.entries(m.doors || {})) ctx.applyDoor(id, open, true);
          for (const line of m.chat || []) ctx.onChat(line, false, true);
          if (voice.mic) net.send({t: 'mic', on: true});
          ctx.notice(m.players.length > 1 ? `Joined lobby ${m.code} · ${m.players.length} players` : `Online · your lobby code is ${m.code}`, 4);
          break;
        case 'join': remote(m.id).av.setName(m.name);voice.peerJoined(m.id);ctx.notice(`${m.name} joined`, 3);ctx.onChat({name: m.name, text: 'joined the lobby', slot: m.slot, sys: true}, false);break;
        case 'leave': {const r = remotes.get(m.id);if (r) ctx.onChat({name: r.name, text: 'left', slot: r.slot, sys: true}, false);drop(m.id);break;}
        case 'name': {const r = remotes.get(m.id);if (r) {r.name = m.name;r.av.setName(m.name);}break;}
        case 'st': {const r = remote(m.id);r.st = m.s;break;}
        case 'vs': {const i = +m.v, v = vehicles[i];if (!v) break;const o = occ[i];if (o && o.d && o.d !== net.you) {const prev = vehTargets.get(i);vehTargets.set(i, {a: m.s, prev: prev ? prev.a : m.s, at: performance.now(), prevAt: prev ? prev.at : performance.now() - 100});} else if (ctx.drivingIndex() !== i) v.netSet(m.s);break;}
        case 'occ': {
          const before = occ;occ = m.occ || {};
          for (const [i, o] of Object.entries(before)) if (o.d && o.d !== net.you && !(occ[i] && occ[i].d === o.d)) vehTargets.delete(+i);
          break;
        }
        case 'claim': {const res = pending.get(+m.v);if (res) {pending.delete(+m.v);res({ok: m.ok, by: m.by});}break;}
        case 'chat': {const mine = m.id === net.you;ctx.onChat(m, mine);if (!mine) {const r = remotes.get(m.id);if (r) r.av.say(m.text);}break;}
        case 'door': ctx.applyDoor(m.id, m.open, false);break;
        case 'fire': fire = m.fire;if (m.by && m.by !== net.you) {const r = remotes.get(m.by);if (r && ctx.onRemoteWood) ctx.onRemoteWood(r);}break;
        case 'rtc': voice.onSignal(m.from, m.d);break;
        case 'full': ctx.notice(`Lobby ${m.code || ''} is full (5 players). Back in your own lobby.`, 5);break;
        case 'reset': for (const id of [...remotes.keys()]) drop(id);occ = {};vehTargets.clear();for (const [, res] of pending) res({ok: true, offline: true});pending.clear();break;
      }
    }
    // Seats: online the lobby decides who gets a seat; offline every seat is yours.
    function claim(i, seat) {
      if (!net.online) return Promise.resolve({ok: true});
      return new Promise(res => {pending.set(i, res);net.send({t: 'claim', v: i, seat});setTimeout(() => {if (pending.get(i) === res) {pending.delete(i);res({ok: false, timeout: true});}}, 4000);});
    }
    function release(i, seat, final) {if (net.online) net.send({t: 'release', v: i, seat, s: final});const o = occ[i];if (o && o[seat] === net.you) delete o[seat];}
    const occupant = (i, seat) => {const o = occ[i];return o ? o[seat] || null : null;};
    const remoteDriven = i => {const d = occupant(i, 'd');return !!d && d !== net.you && remotes.has(d);};
    const nameOf = id => id === net.you ? net.name : (remotes.get(id) || {}).name || 'someone';
    function sitTaken(spotId) {for (const r of remotes.values()) if (r.st && r.st.sit === spotId + 1) return true;return false;}
    function fireLevel() {const now = net.online ? net.serverNow() : Date.now();return Math.max(0, fire.level - (now - fire.t) / (ctx.fireDecayS * 1000));}
    function addWood() {
      const now = net.online ? net.serverNow() : Date.now();fire = {level: Math.min(ctx.fireMax, fireLevel() + 1), t: now};
      if (net.online) net.send({t: 'fire'});
    }
    const angLerp = (a, b, k) => a + Math.atan2(Math.sin(b - a), Math.cos(b - a)) * k;
    // Remote-driven vehicle: extrapolate the latest update a little and ease toward it.
    function smoothVehicle(i, v, tgt, dt) {
      const age = Math.min(.25, (performance.now() - tgt.at) / 1000), span = Math.max(.03, (tgt.at - tgt.prevAt) / 1000), a = tgt.a.slice(), k = 1 - Math.exp(-dt * 14);
      for (let j = 0; j < 3; j++) a[j] += (tgt.a[j] - tgt.prev[j]) / span * age;
      const cur = v.netCur || (v.netCur = tgt.a.slice()), out = cur;
      if (v.aircraft) {
        for (let j = 0; j < 3; j++) out[j] += (a[j] - out[j]) * k;
        const q = new T.Quaternion(out[3], out[4], out[5], out[6]).slerp(new T.Quaternion(a[3], a[4], a[5], a[6]), k);out[3] = q.x;out[4] = q.y;out[5] = q.z;out[6] = q.w;
        for (let j = 7; j < a.length; j++) out[j] += (a[j] - out[j]) * k;
      } else {
        for (let j = 0; j < a.length; j++) out[j] = j >= 3 && j <= 5 ? angLerp(out[j], a[j], k) : out[j] + (a[j] - out[j]) * k;
      }
      v.netSet(out);v.netAnimate(dt);
    }
    function update(dt) {
      const now = performance.now(), cam = ctx.camera;
      // Send our own state ~10 Hz (only when somebody else is in the lobby).
      sendT -= dt;vehT -= dt;
      if (net.online && net.others > 0) {
        if (sendT <= 0) {sendT = .1;const s = ctx.localState();lastLocal = s;net.send({t: 'st', s});}
        const di = ctx.drivingIndex();if (di >= 0 && vehT <= 0) {vehT = 1 / 12;net.send({t: 'vs', v: di, s: vehicles[di].netGet()});}
      }
      // Vehicles driven by others.
      for (const [i, tgt] of vehTargets) {const v = vehicles[i];if (!v || !remoteDriven(i)) {vehTargets.delete(i);if (v) v.netCur = null;continue;}smoothVehicle(i, v, tgt, dt);}
      // Avatars.
      for (const r of remotes.values()) {
        const st = r.st;if (!st) {r.av.group.visible = false;continue;}
        const g = r.av.group;g.visible = true;let stance = st.st || 'stand';
        const vi = st.v ?? -1, anchor = vi >= 0 && vehicles[vi] ? vehicles[vi].hipAnchor(st.s) : null;
        if (anchor) {
          anchor.obj.updateWorldMatrix(true, false);g.position.copy(anchor.obj.localToWorld(anchor.pos.clone()));g.quaternion.copy(anchor.obj.getWorldQuaternion(new T.Quaternion()));stance = anchor.stance;r.pos.copy(g.position);r.speed = 0;
        } else {
          const target = new T.Vector3(st.p[0], st.p[1], st.p[2]);
          if (!r.seen || r.pos.distanceTo(target) > 12) r.pos.copy(target);
          const before = r.pos.clone(), k = 1 - Math.exp(-dt * 10);r.pos.lerp(target, k);r.yaw = angLerp(r.yaw, st.y || 0, k);
          r.speed += ((dt > 0 ? before.distanceTo(r.pos) / dt : 0) - r.speed) * Math.min(1, dt * 6);
          g.position.copy(r.pos);g.quaternion.setFromEuler(new T.Euler(0, r.yaw, 0));
          if (st.sit) stance = 'sit';
        }
        r.seen = true;r.av.pose({stance, speed: stance === 'stand' || stance === 'crouch' || stance === 'prone' ? r.speed : 0, dt, beer: !!st.b});r.av.update(dt, cam);
        // Their drone.
        if (st.dr) {if (!r.drone) {r.drone = avatars.drone();scene.add(r.drone);}r.drone.visible = true;r.drone.position.set(st.dr[0], st.dr[1], st.dr[2]);r.drone.quaternion.set(st.dr[3], st.dr[4], st.dr[5], st.dr[6]);}
        else if (r.drone) r.drone.visible = false;
      }
    }
    return {
      net, voice, update, claim, release, occupant, remoteDriven, nameOf, sitTaken, fireLevel, addWood, remotes,
      sendChat(text) {text = String(text || '').trim().slice(0, 200);if (!text) return false;if (net.online) {net.send({t: 'chat', text});return true;}ctx.onChat({name: net.name, text, slot: 0}, true);return false;},
      sendDoor(id, open) {if (net.online) net.send({t: 'door', id, open});},
      async setMic(on) {const ok = await voice.setMic(on);if (ok && net.online) net.send({t: 'mic', on: voice.mic});return ok;},
      get occ() {return occ;}
    };
  }
  return {create};
});
