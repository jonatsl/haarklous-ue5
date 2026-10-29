const test = require('node:test');
const assert = require('node:assert/strict');
globalThis.window = globalThis;
require('../paintball-data.js');
const PB = require('../game-paintball.js');
const data = window.PAINTBALL;

// A little world of players driven by hand: each has a Rules object whose 'others' are the other players' 'pb' states.
function lobby(n) {
  let clock = 1000;
  const players = [];
  for (let id = 0; id < n; id++) {
    const p = {id, name: `P${id}`, feet: {x: 0, y: 0, z: 0}, yaw: 0, stance: 'stand', pb: null, notices: []};
    p.rules = new PB.Rules(data, {serverNow: () => clock, you: () => id, yourName: () => p.name, yourSlot: () => id,
      others: () => players.filter(o => o !== p && o.pb).map(o => PB.memberFromJson(o.pb, {id: o.id, name: o.name, slot: o.id, local: false, feet: {...o.feet}, yaw: o.yaw, stance: o.stance})),
      feet: () => ({...p.feet}), yaw: () => p.yaw, stance: () => p.stance, teleport: (x, z, yaw) => {p.feet = {x, y: data.floor, z};p.yaw = yaw;},
      cameraPos: () => ({x: p.feet.x, y: p.feet.y + 1.65, z: p.feet.z}), cameraFwd: () => ({x: -Math.sin(p.yaw), y: 0, z: -Math.cos(p.yaw)}),
      ground: () => data.floor, notice: t => p.notices.push(t), sound: () => {}, random: () => .5});
    players.push(p);
  }
  const step = (dt = 1 / 30, input = () => ({})) => {clock += dt * 1000;for (const p of players) {p.rules.update(dt, input(p));p.pb = p.rules.localState();}};
  const toLobby = p => {const l = data.lobby, c = data.centre, w = p.rules.world(l.x0 - c.x + l.w / 2, 0, l.s0 - c.s + l.d / 2);p.feet = {x: w.x, y: -31, z: w.z};};
  return {players, step, toLobby, get clock() {return clock;}};
}

test('the course: yard floor, gate gap, lobby outside the gate, spawns inside the net', () => {
  const r = new PB.Rules(data, {});
  assert.equal(r.ground(data.centre.world[0], data.centre.world[1]), data.floor);
  assert.equal(r.ground(0, 0), null);
  for (const t of [0, 1]) for (let i = 0; i < 6; i++) {const s = r.spawnPoint(t, i);assert.ok(r.inPlay(s.p.x, s.p.z, .5), `spawn ${t}/${i} inside the net`);}
  assert.ok(r.obstacles.length > 60, 'cover, net and walls block walking');
});

test('the first one in the lobby is the admin; teams are balanced; a ball that breaks puts a player out', () => {
  const L = lobby(4);
  L.toLobby(L.players[2]);L.step();L.step();
  for (const p of L.players) L.toLobby(p);
  for (let k = 0; k < 5; k++) L.step();
  for (const p of L.players) assert.equal(p.rules.adminId, 2, 'everyone agrees the first one in is the admin');
  // Player 0 asks for red; the admin holds ✕ to start.
  L.step(1 / 30, p => p.id === 0 ? {up: true} : {});L.step(1 / 30, p => p.id === 0 ? {up: true} : {});
  assert.equal(L.players[0].rules.req, 1);
  for (let k = 0; k < 40; k++) L.step(1 / 30, p => p.id === 2 ? {crossHeld: true} : {});
  const S = L.players[1].rules.ses;
  assert.equal(S.state, 'count');assert.equal(S.team[0], 1, 'the wish is granted');
  const teams = Object.values(S.team);assert.equal(teams.filter(t => t === 0).length, 2);assert.equal(teams.filter(t => t === 1).length, 2);
  for (const p of L.players) assert.ok(p.rules.inGame() && p.rules.inPlay(p.feet.x, p.feet.z), 'everyone spawned on the course');
  for (let k = 0; k < 160; k++) L.step();
  assert.equal(L.players[2].rules.ses.state, 'play');
  // A blue shooter fires at a red player standing 10 m in front of them, with nothing between.
  const shooter = L.players.find(p => S.team[p.id] === 0), victim = L.players.find(p => S.team[p.id] === 1);
  const r = shooter.rules, at = r.world(0, data.floor, -19), there = r.world(-10, data.floor, -19);
  shooter.feet = {x: at.x, y: data.floor, z: at.z};victim.feet = {x: there.x, y: data.floor, z: there.z};
  shooter.yaw = Math.atan2(-(there.x - at.x), -(there.z - at.z));
  L.step(1 / 30, p => p === shooter ? {r2: 1} : {});
  for (let k = 0; k < 20; k++) L.step();
  assert.equal(victim.rules.alive, false, 'the victim decided it was hit');
  assert.equal(victim.rules.outBy, shooter.id);
  for (let k = 0; k < 5; k++) L.step();
  assert.equal(PB.score(L.players[2].rules.ses, 0), 1, 'the admin counts the elimination for blue');
  for (let k = 0; k < 200; k++) L.step();
  assert.equal(victim.rules.alive, true, 'back in at the base after 5 s');
});

test('session JSON survives the wire', () => {
  const r = new PB.Rules(data, {});r.ses.mode = 'ctf';r.ses.team = {3: 0, 7: 1};r.ses.caps = [2, 1];r.ses.flag[1] = {s: 'carried', by: 3, p: {x: 1, y: 2, z: 3}, t: 9};
  const back = PB.sesFromJson(JSON.parse(JSON.stringify(PB.sesToJson(r.ses))));
  assert.deepEqual(back.team, {3: 0, 7: 1});assert.equal(back.flag[1].by, 3);assert.equal(PB.score(back, 0), 2);
});

test('holding △ for 2 s leaves the game (not before), and a game with nobody left returns to the lobby after 3 s', () => {
  const L = lobby(2);
  for (const p of L.players) L.toLobby(p);
  for (let k = 0; k < 5; k++) L.step();
  for (let k = 0; k < 40; k++) L.step(1 / 30, p => p.id === 0 ? {crossHeld: true} : {});
  for (let k = 0; k < 160; k++) L.step();
  assert.ok(L.players.every(p => p.rules.inGame()), 'both play');
  // 1.9 s of △ is not enough; 2.1 s is.
  for (let k = 0; k < 57; k++) L.step(1 / 30, p => ({triangleHeld: true}));
  assert.ok(L.players.every(p => p.rules.inGame()), 'still in after 1.9 s');
  for (let k = 0; k < 6; k++) L.step(1 / 30, p => ({triangleHeld: true}));
  assert.ok(L.players.every(p => !p.rules.inGame() && p.rules.inLobby), 'both left and stand in the lobby');
  assert.ok(L.players[1].notices.includes('You left the game.'));
  assert.equal(L.players[0].rules.ses.state, 'play', 'the empty game waits 3 s');
  for (let k = 0; k < 100; k++) L.step();
  assert.equal(L.players[0].rules.ses.state, 'lobby', 'then the lobby opens again');
  assert.equal(L.players[1].rules.ses.state, 'lobby', 'for everyone');
});

test('cover end caps: the concave jersey barrier profile is filled exactly, with every triangle facing the same way', () => {
  require('../paintball-models.js');
  const {capTris} = window.PaintballModels, area2 = (a, b, c) => (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
  const jersey = [[-.3, 0], [.3, 0], [.3, .08], [.13, .32], [.1, .85], [-.1, .85], [-.13, .32], [-.3, .08]];
  for (const prof of [jersey, jersey.slice().reverse(), [[0, 0], [1, 0], [1, 1], [0, 1]]]) {
    let poly = 0;prof.forEach((a, i) => {const b = prof[(i + 1) % prof.length];poly += a[0] * b[1] - b[0] * a[1];});
    const tris = capTris(prof), sum = tris.reduce((s, [a, b, c]) => s + area2(prof[a], prof[b], prof[c]), 0);
    assert.equal(tris.length, prof.length - 2);
    for (const [a, b, c] of tris) assert.ok(Math.sign(area2(prof[a], prof[b], prof[c])) === Math.sign(poly), 'a triangle is wound the wrong way');
    assert.ok(Math.abs(sum - poly) < 1e-9, `caps cover ${sum / 2} m² instead of ${poly / 2} m²`);
  }
});
