// A full online paintball lobby: 12 bot players (the game's own paintball rules, the real lobby protocol) play 6 v 6 on a
// lobby server, and a 13th player is turned away. Run against a local server, never the live one:
//   cd cloud && npx wrangler dev --port 8787        then   node tests/online-paintball.mjs [ws://127.0.0.1:8787]
import {createRequire} from 'node:module';
const require = createRequire(import.meta.url);
globalThis.window = globalThis;
require('../paintball-data.js');
const PB = require('../game-paintball.js');
const data = window.PAINTBALL, base = process.argv[2] || 'ws://127.0.0.1:8787', code = 'B' + 'CDFGHJKLMNPQRSTVWXYZ'[Math.floor(Math.random() * 20)] + 'TS';
if (/workers\.dev/.test(base)) throw new Error('Never run the bots against the live server.');
const wait = ms => new Promise(r => setTimeout(r, ms));
const fails = [];
const check = (ok, what) => {console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`);if (!ok) fails.push(what);};

function bot(n) {
  const b = {n, name: `Bot ${n}`, you: null, offset: 0, others: new Map(), pos: {x: 0, y: data.floor, z: 0}, yaw: 0, input: {}, full: false, max: 0, notices: []};
  b.ws = new WebSocket(`${base}/lobby/${code}`);
  b.ws.onopen = () => b.ws.send(JSON.stringify({t: 'hello', name: b.name}));
  b.ws.onmessage = e => {
    const m = JSON.parse(e.data);
    if (m.t === 'welcome') {b.you = m.you;b.offset = m.now - Date.now();b.max = m.max;for (const [id, s] of Object.entries(m.states || {})) b.others.set(+id, s);}
    else if (m.t === 'st') b.others.set(m.id, m.s);
    else if (m.t === 'leave') b.others.delete(m.id);
    else if (m.t === 'full') {b.full = true;b.max = m.max;}
  };
  b.rules = new PB.Rules(data, {
    serverNow: () => Date.now() + b.offset, you: () => b.you ?? 0, yourName: () => b.name, yourSlot: () => n,
    others: () => [...b.others.entries()].filter(([, s]) => s && s.pb).map(([id, s]) => PB.memberFromJson(s.pb, {id, name: `#${id}`, slot: 0, local: false, feet: {x: s.p[0], y: s.p[1], z: s.p[2]}, yaw: s.y || 0, stance: s.st || 'stand'})),
    feet: () => ({...b.pos}), yaw: () => b.yaw, stance: () => 'stand',
    teleport: (x, z, yaw) => {b.pos = {x, y: data.floor, z};b.yaw = yaw;},
    cameraPos: () => ({x: b.pos.x, y: b.pos.y + 1.65, z: b.pos.z}), cameraFwd: () => ({x: -Math.sin(b.yaw), y: 0, z: -Math.cos(b.yaw)}),
    ground: () => data.floor, notice: t => b.notices.push(t), sound: () => {}, random: Math.random});
  return b;
}

const bots = Array.from({length: 12}, (_, i) => bot(i));
for (let k = 0; k < 100 && bots.some(b => b.you == null); k++) await wait(100);
check(bots.every(b => b.you != null), '12 players joined one lobby');
check(bots[0].max === 12, `the server's limit is ${bots[0].max}`);
const extra = bot(12);await wait(1500);
check(extra.full && extra.you == null, 'a 13th player is turned away (lobby full)');
// The game loop: 20 frames a second, state to the lobby 10 times a second (like the game).
let frame = 0;
const loop = setInterval(() => {
  frame++;
  for (const b of bots) {
    b.rules.update(.05, b.input);
    if (frame % 2 === 0) {const pb = b.rules.localState();b.ws.send(JSON.stringify({t: 'st', s: {p: [b.pos.x, b.pos.y, b.pos.z], y: b.yaw, st: 'stand', ...(pb ? {pb} : {})}}));}
  }
}, 50);
// Into the lobby one after the other (bot 0 first: the admin).
const c = data.centre, l = data.lobby;
for (const b of bots) {const w = b.rules.world(l.x0 - c.x + 2 + b.n * 2, 0, l.s0 - c.s + l.d / 2);b.pos = {x: w.x, y: -31, z: w.z};await wait(150);}
await wait(1500);
check(bots.every(b => b.rules.inLobby && b.rules.adminId === bots[0].you), 'everyone is in the paintball lobby and agrees bot 0 is the admin');
bots[0].input = {crossHeld: true};await wait(1600);bots[0].input = {};
await wait(1500);
const S = bots[5].rules.ses, teams = Object.values(S.team);
check(S.state === 'count' || S.state === 'play', `the admin started a game (${S.state})`);
check(teams.filter(t => t === 0).length === 6 && teams.filter(t => t === 1).length === 6, `6 v 6 (${teams.join('')})`);
check(bots.every(b => b.rules.inGame() && b.rules.inPlay(b.pos.x, b.pos.z)), 'all 12 spawned on the course');
for (let k = 0; k < 120 && !bots.every(b => b.rules.ses.state === 'play'); k++) await wait(100);
check(bots.every(b => b.rules.ses.state === 'play'), 'everyone sees the game playing');
// A blue player shoots a red one 10 m away, in the open.
const blue = bots.find(b => b.rules.myTeam() === 0), red = bots.find(b => b.rules.myTeam() === 1);
const at = blue.rules.world(0, data.floor, -19), there = blue.rules.world(-10, data.floor, -19);
red.pos = {x: there.x, y: data.floor, z: there.z};blue.pos = {x: at.x, y: data.floor, z: at.z};await wait(800);
blue.yaw = Math.atan2(-(there.x - at.x), -(there.z - at.z));
for (let k = 0; k < 3 && red.rules.alive; k++) {blue.input = {r2: 1};await wait(120);blue.input = {};await wait(1200);}
check(!red.rules.alive && red.rules.outBy === blue.you, `the red player was hit by the blue player's ball (it flew over the network)`);
await wait(1000);
check(bots.every(b => PB.score(b.rules.ses, 0) >= 1), 'all 12 see blue score');
check(bots.some(b => b !== blue && b.rules.feed.some(f => /hit/.test(f.text))), 'the others see it in the feed');
clearInterval(loop);
for (const b of [...bots, extra]) try {b.ws.close();} catch {}
console.log(fails.length ? `${fails.length} FAILED` : 'all passed');
process.exit(fails.length ? 1 : 0);
