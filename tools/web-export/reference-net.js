/* Runs inside haarklous-game.html (tools/web-export/export.mjs --only=tests --script=reference-net.js): reference answers
 * for the online port (HKNet / HKMultiplayer / HKAvatars), computed by the web's own game-net.js, game-multiplayer.js
 * and game-avatars.js. Written to unreal/Haarklous/Content/Data/tests/net.json.
 * A scripted lobby session is played against a fake WebSocket with fake clocks and timers (nothing touches the network):
 * every step (server message, frame update, claim, chat, door, fire, reconnect ...) records what the game was told
 * (events: sent messages, notices, chat lines, doors, vehicle netSet calls ...) and a snapshot (status, occupants, fire
 * level, remote avatars with every joint). The C++ test (HKNetTests.cpp) replays the same steps and must match.
 */
(async () => {
  'use strict';
  const T = window.THREE, files = window.__hkFiles, log = window.__hkLog, G = window.haarklousGame;
  const out = {};
  // ---------- Plain functions ----------
  out.codes = ['ABCD', 'abcd', 'AB1D', 'ABOD', 'AB0D', 'ABID', 'ABC', 'ABCDE', '2345', 'ZZZZ', 'A B ', 'HKNT', '', 'ÆØÅA', 'HJNP'].map(c => [c, G.mp.net.validCode(c)]);
  out.alphabet = window.GameNet.ALPHABET;
  out.backoff = Array.from({length: 10}, (_, r) => Math.min(30000, 1500 * 2 ** r));
  out.numbers = [0, -0, 1, -1, .1, .1 + .2, 1 / 3, 123.456, 1e21, 1e-7, 1.5e-6, 123456789012345680000, 5e-324, 1.7976931348623157e308, 2 ** 53, -12.34, .000001, 100, 1e20, 0.5, 1793061205808, -0.0042, 3.14159, 1e-6, 9.87654321e-5, 2.5e+25]
    .map(v => [v, JSON.stringify(v)]);
  out.strings = ['plain', 'a"b', 'back\\slash', 'line\nbreak\r\n', '\u0001\u001f', 'tab\t', 'æøå ÆØÅ', 'Kom til bålet 🔥', '</script>', '\b\f', 'é '].map(s => [s, JSON.stringify(s)]);
  out.messages = [{t: 'hello', name: 'Tester'}, {t: 'st', s: {p: [1.25, -10, 2.5], y: .75, st: 'stand', b: 0}}, {t: 'vs', v: 3, s: [12.345, -9.87, 0.001, 3.1416, 0, -0, 4.5]},
    {t: 'chat', text: 'Skål! 🍺 "quoted"'}, {t: 'door', id: 'W05-0', open: true}, {t: 'release', v: 1, seat: 'd', s: [1, 2, 3]}].map(m => [m, JSON.stringify(m)]);

  // ---------- The avatar's joints (the same build-order lookup as exporter-avatars.js) ----------
  const groups = o => o.children.filter(c => !c.isMesh && !c.isSprite);
  function parts(g) {
    const [hips] = groups(g), [torso, hipL, hipR] = groups(hips), [neck, shL, shR] = groups(torso), [head] = groups(neck);
    const [elL] = groups(shL), [elR] = groups(shR), [knL] = groups(hipL), [knR] = groups(hipR), [bottle] = groups(elR);
    return {hips, torso, neck, head, 'L.arm.sh': shL, 'L.arm.el': elL, 'R.arm.sh': shR, 'R.arm.el': elR, 'L.leg.hip': hipL, 'L.leg.kn': knL, 'R.leg.hip': hipR, 'R.leg.kn': knR, bottle};
  }
  function avatarSnap(g) {
    const p = parts(g), nodes = {};
    for (const [k, o] of Object.entries(p)) nodes[k] = [o.rotation.x, o.rotation.y, o.rotation.z];
    const tag = g.children[1], bubble = g.children[2];
    return {nodes, hips: p.hips.position.toArray(), bottle: p.bottle.visible, tagY: tag.position.y, bubbleY: bubble.position.y, tagVisible: tag.visible, tagK: tag.scale.x / 1.3, bubbleVisible: bubble.visible, bubbleK: bubble.scale.x / 2.4};
  }
  {
    const A = window.GameAvatars.create({T, lib: window.__hkLib});
    out.poses = [];
    for (const beer of [false, true]) for (const stance of ['stand', 'crouch', 'prone', 'sit', 'drive', 'ride', 'pillion', 'drone']) {
      const av = A.avatar(1, 'P'), seq = [];
      for (const [speed, dt] of [[2.5, .1], [4, .05], [0, .1], [1, .3], [3.6, .02]]) {av.pose({stance, speed, dt, beer});seq.push(avatarSnap(av.group));}
      out.poses.push({stance, beer, args: [[2.5, .1], [4, .05], [0, .1], [1, .3], [3.6, .02]], seq});av.dispose();
    }
  }

  // ---------- A scripted lobby session against the real game-net.js + game-multiplayer.js ----------
  let dateNow = 1793000000000, perfNow = 50000;
  const timers = [];let nextTimer = 1;
  const saved = {dateNow: Date.now, perfNow: performance.now, setTimeout: window.setTimeout, clearTimeout: window.clearTimeout, setInterval: window.setInterval, clearInterval: window.clearInterval,
    WebSocket: window.WebSocket, GameVoice: window.GameVoice, code: localStorage.getItem('haarklous-lobby-code-v1'), name: localStorage.getItem('haarklous-name-v1')};
  const events = [];const ev = (...a) => events.push(JSON.parse(JSON.stringify(a)));
  const sockets = [];
  class FakeWS {
    constructor(url) {this.url = url;this.readyState = 0;sockets.push(this);ev('socket', url);}
    send(text) {ev('send', text);}
    close(code) {this.readyState = 3;ev('wsclose', code);}
  }
  function tick() {
    const limit = nextTimer;
    for (;;) {
      let best = -1;
      timers.forEach((t, i) => {if (t.id < limit && t.due <= perfNow && (best < 0 || t.due < timers[best].due || (t.due === timers[best].due && t.id < timers[best].id))) best = i;});
      if (best < 0) break;
      const t = timers[best];
      if (t.every > 0) {t.due = perfNow + t.every;t.fn();} else {timers.splice(best, 1);t.fn();}
    }
  }
  const flush = async () => {for (let i = 0; i < 8; i++) await Promise.resolve();};
  const init = {dateNow, perfNow, code: 'HKNT', name: 'Tester', base: ''};
  let mp, voiceSend, local = {p: [0, -10, 0], y: 0, st: 'stand', b: 0}, driving = -1, playerVehicle = -1, playerDriving = false, lastLobby = null;
  const camera = new T.PerspectiveCamera();
  // Fake vehicles: two ground vehicles and one aircraft, posed by fixed groups; netSet/netAnimate/reset are recorded.
  const vdefs = [0, 1, 2].map(i => ({aircraft: i === 2, pos: [10 + i, -9 + i * .5, 5 - i], rot: [.1 * i, .5 + i, -.05 * i], seatPos: [.3, .5, -.2], seatRot: [0, .2, 0],
    spawn: i === 2 ? [1, 2, 3, 0, 0, 0, 1, 0, 0, 0, 1] : [i, -10, 2 * i, .3, 0, 0, 0],
    get: i === 2 ? [5, -8, 6, 0, .3827, 0, .9239, 12, 1, .5, 0] : [i + .5, -10, 3, 1.2, .01, 0, 4, .1],
    anchors: {d: {pos: [0, .1, 0], stance: i === 2 ? 'drive' : 'ride'}, p: i === 2 ? null : {pos: [0, .1, .4], stance: 'pillion'}}}));
  const vehicles = vdefs.map((d, i) => {
    const group = new T.Group();group.position.fromArray(d.pos);group.rotation.set(...d.rot);
    const seat = new T.Object3D();seat.position.fromArray(d.seatPos);seat.rotation.set(...d.seatRot);group.add(seat);group.updateMatrixWorld(true);
    return {aircraft: d.aircraft, group, spawn: d.spawn, netCur: null, netGet: () => d.get.slice(), netSet(a) {ev('netSet', i, a.slice());}, netAnimate(dt) {ev('netAnimate', i, dt);}, reset() {ev('reset', i);},
      hipAnchor(s) {const a = d.anchors[s];return a ? {obj: seat, pos: new T.Vector3(...a.pos), stance: a.stance} : null;}};
  });
  function snap() {
    const n = mp.net;
    return {status: n.status, code: n.code || '', own: n.ownCode, you: n.you || 0, online: n.online, others: n.others, name: n.name, slot: n.slot, fire: mp.fireLevel(), serverNow: n.serverNow(),
      occ: [0, 1, 2].map(i => [mp.occupant(i, 'd') || 0, mp.occupant(i, 'p') || 0, mp.remoteDriven(i)]), names: [0, 1, 2, 3, 4, 99].map(id => mp.nameOf(id === 0 ? null : id)),
      sit: [0, 1, 2, 3].map(id => mp.sitTaken(id)), players: [...n.players.values()].map(p => [p.id, p.slot, p.name, !!p.mic]), mic: mp.voice.mic,
      remotes: [...mp.remotes.values()].map(r => ({id: r.id, slot: r.slot, name: r.name, seen: r.seen, visible: r.av.group.visible, pos: r.av.group.position.toArray(), quat: r.av.group.quaternion.toArray(),
        rpos: r.pos.toArray(), yaw: r.yaw, speed: r.speed, ...avatarSnap(r.av.group),
        drone: r.drone ? {visible: r.drone.visible, pos: r.drone.position.toArray(), quat: r.drone.quaternion.toArray()} : null}))};
  }
  const W = (dt, n = 1) => Array.from({length: n}, () => [{op: 'time', ms: dt * 1000}, {op: 'update', dt}]).flat();
  const now = d => () => dateNow + d;
  const steps = [
    {op: 'snap'},
    {op: 'connect', code: 'o0o0'},
    {op: 'begin', lobby: 'xy1z'},
    {op: 'update', dt: .1},
    {op: 'open'},
    {op: 'time', ms: 50},
    {op: 'msg', m: () => ({t: 'welcome', you: 2, slot: 1, code: 'HKNT', max: 5, players: [{id: 1, slot: 0, name: 'Alice', mic: false}, {id: 2, slot: 1, name: 'Tester-old', mic: false}],
      states: {1: {p: [3, -10, 4], y: .5, st: 'stand', b: 0}}, vehicles: {1: [9, -9, 9, 1, 0, 0, 0, 0]}, occ: {0: {d: 1}}, doors: {'W05-0': true, 'W09-0': false}, fire: {level: 3, t: dateNow - 30000},
      chat: [{id: 1, slot: 0, name: 'Alice', text: 'hei', at: dateNow - 5000}], now: dateNow + 1500})},
    {op: 'camera', p: [0, -8.4, 8]},
    {op: 'local', s: {p: [1.25, -10, 2.5], y: .75, st: 'stand', b: 0}},
    ...W(.1, 3),
    {op: 'msg', m: {t: 'st', id: 1, s: {p: [4, -10, 4], y: 1, st: 'stand', b: 0}}}, ...W(.1, 2),
    {op: 'msg', m: {t: 'st', id: 1, s: {p: [5.5, -10, 4.2], y: 1.2, st: 'stand', b: 1}}}, ...W(.05, 3),
    {op: 'msg', m: {t: 'st', id: 1, s: {p: [60, -10, 4.2], y: -3, st: 'crouch', b: 0}}}, ...W(.1, 2),
    {op: 'msg', m: {t: 'st', id: 1, s: {p: [61, -10, 4.2], y: 3, st: 'prone', b: 0}}}, ...W(.1, 2),
    {op: 'msg', m: {t: 'st', id: 1, s: {p: [61, -10, 4.2], y: 3, st: 'drone', b: 0, dr: [62, -5, 6, 0, .1, 0, .995]}}}, ...W(.1, 2),
    {op: 'msg', m: {t: 'st', id: 1, s: {p: [-2.5, -9.6, 33], y: 2, st: 'stand', b: 1, sit: 3}}}, ...W(.1, 2),
    {op: 'msg', m: {t: 'st', id: 1, s: {p: [10, -9, 5], y: 0, st: 'stand', b: 0, v: 0, s: 'd'}}},
    {op: 'msg', m: {t: 'vs', v: 0, s: [10, -9, 5, .2, 0, 0, 3, .1]}}, ...W(.05, 2),
    {op: 'msg', m: {t: 'vs', v: 0, s: [10.3, -9, 5.1, .25, 0, 0, 3, .12]}}, ...W(.05, 3),
    {op: 'msg', m: {t: 'vs', v: 0, s: [10.6, -9, 5.2, -3.1, .01, 0, 3, .14]}}, ...W(.1, 3),
    {op: 'msg', m: {t: 'occ', occ: {2: {d: 1}}}},
    {op: 'msg', m: {t: 'st', id: 1, s: {p: [0, 0, 0], y: 0, st: 'stand', b: 0, v: 2, s: 'd'}}},
    {op: 'msg', m: {t: 'vs', v: 2, s: [5, -8, 6, 0, .38268343, 0, .92387953, 12, 1, .5, 0]}}, ...W(.05, 2),
    {op: 'msg', m: {t: 'vs', v: 2, s: [5.6, -7.9, 6, 0, .5, 0, .8660254, 12.5, 1.1, .6, 0]}}, ...W(.05, 3),
    {op: 'msg', m: {t: 'st', id: 1, s: {p: [11, -9, 4], y: 0, st: 'stand', b: 1, v: 1, s: 'p'}}}, ...W(.1),
    {op: 'msg', m: {t: 'st', id: 1, s: {p: [12, -9, 3], y: .4, st: 'stand', b: 0, v: 2, s: 'p'}}}, ...W(.1),
    {op: 'msg', m: {t: 'join', id: 3, slot: 2, name: 'Bob', mic: false}},
    {op: 'msg', m: {t: 'st', id: 3, s: {p: [150, -10, 0], y: 0, st: 'stand', b: 0}}}, ...W(.1),
    {op: 'msg', m: {t: 'name', id: 3, name: 'Bobby'}},
    {op: 'msg', m: {t: 'mic', id: 3, on: true}},
    {op: 'msg', m: () => ({t: 'chat', id: 1, slot: 0, name: 'Alice', text: 'Kom til bålet 🔥 nå med en gang før det blir mørkt og kaldt her ute', at: dateNow})},
    {op: 'msg', m: () => ({t: 'chat', id: 2, slot: 1, name: 'Tester', text: 'mine', at: dateNow})},
    ...W(.5, 15),
    {op: 'chat', text: '   hello world   '},
    {op: 'chat', text: 'x'.repeat(230)},
    {op: 'chat', text: '    '},
    {op: 'door', id: 'W05-0', open: false},
    {op: 'msg', m: {t: 'door', id: 'W01-1', open: true}},
    {op: 'wood'}, {op: 'snap'},
    {op: 'msg', m: () => ({t: 'fire', fire: {level: 4.2, t: dateNow + 1500}, by: 1})},
    {op: 'msg', m: () => ({t: 'fire', fire: {level: 5, t: dateNow + 1500}, by: 2})},
    {op: 'msg', m: () => ({t: 'pong', now: dateNow + 2000, c: dateNow - 80})},
    {op: 'snap'},
    {op: 'claim', i: 1, seat: 'd'},
    {op: 'msg', m: {t: 'claim', v: '1', seat: 'd', ok: true, by: 2}},
    {op: 'msg', m: {t: 'occ', occ: {1: {d: 2}, 2: {d: 1}}}},
    {op: 'claim', i: 0, seat: 'p'},
    {op: 'msg', m: {t: 'claim', v: '0', seat: 'p', ok: false, by: 3}},
    {op: 'claim', i: 2, seat: 'd'},
    {op: 'time', ms: 3900}, {op: 'time', ms: 200},
    {op: 'driving', i: 1}, ...W(1 / 30, 6),
    {op: 'msg', m: {t: 'vs', v: 1, s: [1, 2, 3, 0, 0, 0, 0, 0]}},
    {op: 'msg', m: {t: 'vs', v: 0, s: [7, -9, 7, 0, 0, 0, 0, 0]}},
    {op: 'release', i: 1, seat: 'd', s: [1.5, -9, 2.25, .5, 0, 0, 0, .1]},
    {op: 'driving', i: -1},
    {op: 'release', i: 0, seat: 'p'},
    {op: 'mic', on: true},
    {op: 'msg', m: {t: 'rtc', from: 1, d: {c: {candidate: 'candidate:1 1 udp 2122260223 192.168.1.2 54321 typ host', sdpMid: '0', sdpMLineIndex: 0}}}},
    {op: 'rtcOut', to: 1, d: {d: {type: 'offer', sdp: 'v=0\r\no=- 1 2 IN IP4 127.0.0.1\r\n'}}},
    {op: 'name', n: '   A very long new name for the player   '},
    {op: 'local', s: {p: [-2.7, -9.6, 34.7], y: 1.57, st: 'stand', b: 1, sit: 4}}, ...W(.1, 2),
    {op: 'local', s: {p: [5, -10, 6], y: .2, st: 'drone', b: 0, dr: [5, -7, 6, 0, .259, 0, .966]}}, ...W(.1, 2),
    {op: 'local', s: {p: [10, -9.5, 5], y: .3, st: 'stand', b: 0, v: 0, s: 'p'}}, ...W(.1),
    {op: 'time', ms: 20000},
    {op: 'raw', text: 'not json'},
    {op: 'msg', m: {t: 'leave', id: 3}},
    {op: 'msg', m: {t: 'occ', occ: {}}},
    {op: 'claim', i: 0, seat: 'd'},
    {op: 'player', i: 0, driving: true},
    {op: 'connect', code: 'ab-cd'},
    {op: 'open'},
    {op: 'msg', m: {t: 'full', max: 5}},
    {op: 'close', code: 4001},
    {op: 'open'},
    {op: 'msg', m: () => ({t: 'welcome', you: 1, slot: 0, code: 'HKNT', max: 5, players: [{id: 1, slot: 0, name: 'A very long new name fo', mic: false}], states: {}, vehicles: {}, occ: {}, doors: {}, fire: {level: 2, t: dateNow}, chat: [], now: dateNow})},
    ...W(.1, 2),
    {op: 'close', code: 1006},
    {op: 'claim', i: 1, seat: 'd'},
    {op: 'chat', text: 'offline line'},
    {op: 'wood'}, {op: 'snap'},
    {op: 'time', ms: 1499}, {op: 'time', ms: 1},
    {op: 'close', code: 1006},
    {op: 'time', ms: 3000},
    {op: 'close', code: 1006},
    {op: 'time', ms: 5999}, {op: 'time', ms: 1},
    {op: 'connect', code: 'ABCD'},
    {op: 'open'},
    {op: 'msg', m: () => ({t: 'welcome', you: 4, slot: 3, code: 'ABCD', max: 5, players: [{id: 4, slot: 3, name: 'A very long new name fo', mic: true}, {id: 5, slot: 1, name: 'Eve', mic: false}],
      states: {5: {p: [1, -10, 1], y: 0, st: 'stand', b: 0}}, vehicles: {0: [3, -10, 3, 0, 0, 0, 0, 0], 2: [1, 1, 1, 0, 0, 0, 1, 0, 0, 0, 1]}, occ: {2: {d: 5}}, doors: {}, fire: {level: 0, t: dateNow}, chat: [], now: dateNow - 700})},
    {op: 'claim', i: 0, seat: 'd'},
    {op: 'msg', m: {t: 'claim', v: '0', seat: 'd', ok: true, by: 4}},
    ...W(.1, 2),
    {op: 'home'},
    {op: 'time', ms: 4000},
    {op: 'snap'}
  ];
  const executed = [], results = [];
  try {
    Date.now = () => dateNow;performance.now = () => perfNow;
    window.setTimeout = (fn, ms) => {const id = nextTimer++;timers.push({id, due: perfNow + Math.max(0, +ms || 0), every: 0, fn});return id;};
    window.setInterval = (fn, ms) => {const id = nextTimer++;const every = Math.max(1, +ms || 0);timers.push({id, due: perfNow + every, every, fn});return id;};
    window.clearTimeout = window.clearInterval = id => {const i = timers.findIndex(t => t.id === id);if (i >= 0) timers.splice(i, 1);};
    window.WebSocket = FakeWS;
    window.GameVoice = {create: ({send}) => {
      voiceSend = send;
      const v = {mic: false, async setMic(on) {v.mic = !!on;ev('voiceMic', !!on);return true;}, onSignal(from, d) {ev('rtcIn', from, d);}, peerJoined(id) {ev('peerJoined', id);}, peerLeft(id) {ev('peerLeft', id);}, setVolume() {}, reset() {}, peers: new Map()};
      return v;
    }};
    localStorage.setItem('haarklous-lobby-code-v1', init.code);localStorage.setItem('haarklous-name-v1', init.name);
    const ctx = {T, lib: window.__hkLib, scene: new T.Scene(), camera, vehicles, fireDecayS: 90, fireMax: 6,
      localState: () => JSON.parse(JSON.stringify(local)), notice: (t, s) => ev('notice', t, s), drivingIndex: () => driving, listener: () => ({x: 0, y: 0, z: 0}),
      applyDoor: (id, open, instant) => ev('door', id, open, !!instant),
      onChat: (m, mine, old) => ev('chat', {id: m.id || 0, slot: m.slot || 0, name: m.name || '', text: m.text, at: m.at || 0, sys: !!m.sys}, !!mine, !!old),
      onStatus: () => {ev('status', mp.net.status);onLobbyChange();},
      onRemoteWood: r => ev('wood', r.id)};
    function onLobbyChange() {
      const n = mp.net;if (!n.online || n.code === lastLobby) return;lastLobby = n.code;
      vehicles.forEach((v, i) => {if (i === playerVehicle) {mp.claim(i, playerDriving ? 'd' : 'p');return;}v.netSet(v.spawn);if (v.aircraft) v.reset();});
    }
    mp = window.GameMultiplayer.create(ctx);
    init.base = mp.net.base;init.ownCode = mp.net.ownCode;init.startName = mp.net.name;
    for (const raw of steps) {
      const step = {...raw};if (typeof step.m === 'function') step.m = step.m();
      executed.push(JSON.parse(JSON.stringify(step)));
      events.length = 0;
      const ws = sockets[sockets.length - 1];
      switch (step.op) {
        case 'snap': break;
        case 'begin': {const q = (step.lobby || '').toUpperCase();if (!(q && mp.net.connect(q))) mp.net.home();break;}
        case 'open': ws.readyState = 1;ws.onopen && ws.onopen();break;
        case 'msg': ws.onmessage && ws.onmessage({data: JSON.stringify(step.m)});break;
        case 'raw': ws.onmessage && ws.onmessage({data: step.text});break;
        case 'close': ws.readyState = 3;ws.onclose && ws.onclose({code: step.code});break;
        case 'time': dateNow += step.ms;perfNow += step.ms;tick();break;
        case 'update': mp.update(step.dt);break;
        case 'local': local = step.s;break;
        case 'driving': driving = step.i;break;
        case 'camera': camera.position.fromArray(step.p);break;
        case 'player': playerVehicle = step.i;playerDriving = step.driving;break;
        case 'claim': {const i = step.i;mp.claim(i, step.seat).then(r => ev('claimResult', i, {ok: !!r.ok, by: r.by || 0, timeout: !!r.timeout, offline: !!r.offline}));break;}
        case 'release': mp.release(step.i, step.seat, step.s);break;
        case 'chat': ev('chatReturn', mp.sendChat(step.text));break;
        case 'door': mp.sendDoor(step.id, step.open);break;
        case 'wood': mp.addWood();break;
        case 'mic': {const ok = await mp.setMic(step.on);ev('micReturn', ok);break;}
        case 'name': mp.net.setName(step.n);break;
        case 'connect': ev('connectReturn', mp.net.connect(step.code));break;
        case 'home': mp.net.home();break;
        case 'rtcOut': voiceSend(step.to, step.d);break;
        default: throw new Error('unknown step ' + step.op);
      }
      await flush();
      results.push({events: events.slice(), snap: snap()});
    }
  } finally {
    Date.now = saved.dateNow;performance.now = saved.perfNow;
    window.setTimeout = saved.setTimeout;window.clearTimeout = saved.clearTimeout;window.setInterval = saved.setInterval;window.clearInterval = saved.clearInterval;
    window.WebSocket = saved.WebSocket;window.GameVoice = saved.GameVoice;
    if (saved.code === null) localStorage.removeItem('haarklous-lobby-code-v1');else localStorage.setItem('haarklous-lobby-code-v1', saved.code);
    if (saved.name === null) localStorage.removeItem('haarklous-name-v1');else localStorage.setItem('haarklous-name-v1', saved.name);
  }
  out.session = {init, vehicles: vdefs, steps: executed, results};
  files.net = {json: JSON.stringify(out), bin: null};
  log.push(`reference net: ${out.codes.length} codes, ${out.numbers.length} numbers, ${out.poses.length} pose sequences, ${executed.length} session steps, ${results.reduce((n, r) => n + r.events.length, 0)} events`);
  window.__hkDone = true;
})();
