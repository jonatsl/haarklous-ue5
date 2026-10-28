// Paintball bots for the online paintball test (run-paintball.ps1, -hktest=pbonline): simple clients in one lobby on
// the TEST server that speak the game's own protocol ('hello', then 'st' with the 'pb' field ~10 times a second).
//   node tools/net-test/pb-bots.mjs --server=ws://127.0.0.1:8787 --lobby=CODE --bots=7 --timeout=240 --out=<folder>
// They wait until the Unreal player is in the paintball lobby (so it is the admin), join it, play in its games: take
// their spawn points, one of the other team walks up and fires at the Unreal player, and each bot reports being put out
// when one of the Unreal player's balls reaches it (its own simple ball flight, no cover). Never the live server.
import fs from 'node:fs';
import path from 'node:path';

const arg = (k, d) => (process.argv.find(a => a.startsWith(`--${k}=`)) || '').slice(k.length + 3) || d;
const server = arg('server', 'ws://127.0.0.1:8787'), lobby = arg('lobby'), nBots = +arg('bots', '7'), timeoutS = +arg('timeout', '240'), outDir = arg('out', '.');
if (!lobby) {console.error('need --lobby');process.exit(2);}
if (/haarklous-online|haarklous-plass/i.test(server)) {console.error('Refusing: that is the live game server');process.exit(3);}
const root = path.resolve(path.dirname(new URL(import.meta.url).pathname.replace(/^\/([A-Z]:)/, '$1')), '..', '..');
const course = JSON.parse(fs.readFileSync(path.join(root, 'unreal', 'Haarklous', 'Content', 'Data', 'paintball.json'), 'utf8'));
const th = course.centre.theta, [cxw, czw] = course.centre.world, floor = course.floor;
const world = (u, v) => [cxw + u * Math.cos(th) + v * Math.sin(th), floor, czw - u * Math.sin(th) + v * Math.cos(th)];
const spawn = (team, i) => {const s = course.bases[team].spawns[i % 6];return {p: world(s[0], s[1]), yaw: s[2] + th};};
const log = (...a) => {const line = `[${new Date().toISOString()}] ${a.join(' ')}`;console.log(line);};
const summary = {bots: nBots, connected: 0, joinedLobby: 0, inGame: 0, outs: 0, fired: 0, games: 0, errors: []};

function bot(i) {
  const B = {i, name: `Bot ${i + 1}`, you: 0, offset: 0, lob: 0, ses: 0, round: 0, alive: true, outs: 0, by: 0, team: -1, seq: 0, shots: [],
    pos: world(-12 + i * 1.6, 31 + (i % 2)), yaw: th, others: new Map(), seen: new Map(), respawnAt: 0, nextShot: 0, role: 'stand'};
  const ws = new WebSocket(`${server}/lobby/${lobby}`);
  const now = () => Date.now() + B.offset;
  const send = m => {try {ws.send(JSON.stringify(m));} catch {}};
  ws.onopen = () => {summary.connected++;send({t: 'hello', name: B.name});};
  ws.onerror = e => summary.errors.push(`${B.name}: ${e.message || 'socket error'}`);
  ws.onmessage = ev => {
    const m = JSON.parse(ev.data);
    if (m.t === 'welcome') {B.you = m.you;B.offset = m.now - Date.now();for (const [id, s] of Object.entries(m.states || {})) B.others.set(+id, s);log(B.name, 'welcome as', m.you, 'slot', m.slot, 'max', m.max);}
    else if (m.t === 'st') B.others.set(m.id, m.s);
    else if (m.t === 'leave') B.others.delete(m.id);
    else if (m.t === 'full') {summary.errors.push(`${B.name}: lobby full (max ${m.max})`);log(B.name, 'lobby full', m.max);}
  };
  // The admin: the member with the earliest lobby time (then the lowest id) - the Unreal player in this test.
  const admin = () => {let best = null;for (const [id, s] of B.others) if (s && s.pb && s.pb.l > 0 && (!best || s.pb.l < best.s.pb.l)) best = {id, s};return best;};
  // Balls the Unreal player fired: fly them (gravity, drag, no cover) and see whether one passes through our body.
  const checkHits = (id, s) => {
    for (const h of (s.pb.h || [])) {
      const seen = B.seen.get(id) || 0;if (h[0] <= seen) continue;B.seen.set(id, h[0]);
      if (!B.alive || !B.ses) continue;
      let p = [h[2], h[3], h[4]], v = [h[5], h[6], h[7]];
      const a = [B.pos[0], B.pos[1] + .15, B.pos[2]], top = 1.72 - .15;
      for (let t = 0; t < 1.5; t += 1 / 120) {
        const sp = Math.hypot(...v);v = v.map((c, k) => c - c * .022 * sp / 120 - (k === 1 ? 9.81 / 120 : 0));p = p.map((c, k) => c + v[k] / 120);
        const dy = Math.min(Math.max(p[1] - a[1], 0), top), d = Math.hypot(p[0] - a[0], p[1] - (a[1] + dy), p[2] - a[2]);
        if (d < .25) {
          B.alive = false;B.outs++;B.by = id;B.respawnAt = Date.now() + 5000;summary.outs++;
          log(B.name, 'OUT by', id, 'outs', B.outs);break;
        }
      }
    }
  };
  setInterval(() => {
    if (!B.you) return;
    const ad = admin();
    const ses = ad && ad.s.pb.ses;
    if (ad && !B.lob) {B.lob = now();summary.joinedLobby++;log(B.name, 'joined the paintball lobby; admin is', ad.id);}
    if (ses && ses.st !== 'lobby' && ses.tm && ses.tm[B.you] !== undefined && B.ses !== ses.id) {
      B.ses = ses.id;B.team = ses.tm[B.you];B.round = ses.rn;B.alive = true;B.outs = 0;summary.inGame++;
      const mates = Object.entries(ses.tm).filter(([, t]) => t === B.team).map(([k]) => +k).sort((x, y) => x - y);
      const sp = spawn(B.team, mates.indexOf(B.you));B.pos = sp.p;B.yaw = sp.yaw;
      B.role = 'stand';
      log(B.name, 'in game', ses.id, 'team', B.team ? 'red' : 'blue');
    }
    if (ses && ses.st === 'lobby' && B.ses) {B.ses = 0;B.pos = world(-12 + i * 1.6, 31 + (i % 2));summary.games++;log(B.name, 'back in the lobby');}
    if (B.ses && !B.alive && Date.now() > B.respawnAt) {B.alive = true;log(B.name, 'back in');}
    // The first bot of the Unreal player's other team walks out 9 m in front of it and fires at it every 0.4 s.
    const me = ad && B.others.get(ad.id);
    if (B.ses && ses && ses.st === 'play' && me && me.p) {
      const myTeam = ses.tm[ad.id], foes = Object.entries(ses.tm).filter(([k, t]) => t !== myTeam && +k !== ad.id).map(([k]) => +k).sort((x, y) => x - y);
      if (foes[0] === B.you) {
        const toward = -Math.sign(myTeam === 1 ? 1 : -1), dir = [Math.cos(th) * toward, Math.sin(th) * -toward];
        if (B.role !== 'shooter') {B.role = 'shooter';B.pos = [me.p[0] + dir[0] * 9, floor, me.p[2] + dir[1] * 9];}
        B.yaw = Math.atan2(-(me.p[0] - B.pos[0]), -(me.p[2] - B.pos[2]));
        if (B.alive && Date.now() > B.nextShot) {
          B.nextShot = Date.now() + 400;
          const eye = [B.pos[0], B.pos[1] + 1.55, B.pos[2]], at = [me.p[0], me.p[1] + 1.2, me.p[2]];
          const d = Math.hypot(at[0] - eye[0], at[1] - eye[1], at[2] - eye[2]);
          let dv = [at[0] - eye[0], at[1] - eye[1] + d * d * .0012, at[2] - eye[2]];const L = Math.hypot(...dv);dv = dv.map(c => c / L);
          const o = eye.map((c, k) => c + dv[k] * .45), v = dv.map(c => c * 90);
          B.shots.push([++B.seq, Math.round(now()), ...o.map(c => +c.toFixed(3)), ...v.map(c => +c.toFixed(3))]);summary.fired++;
        }
      }
    }
    for (const [id, s] of B.others) if (s && s.pb) checkHits(id, s);
    const pb = {l: Math.round(B.lob), q: -1, s: B.ses, a: B.alive ? 1 : 0, o: B.outs, b: B.by, r: B.round};
    if (B.shots.length) {pb.h = B.shots;B.shots = [];}
    send({t: 'st', s: {p: B.pos.map(c => +c.toFixed(2)), y: +B.yaw.toFixed(2), st: 'stand', b: 0, pb}});
  }, 100);
  setInterval(() => send({t: 'ping', c: Date.now()}), 5000);
  return B;
}

const bots = [];
for (let i = 0; i < nBots; i++) setTimeout(() => bots.push(bot(i)), 300 * i);
setTimeout(() => {
  fs.writeFileSync(path.join(outDir, `pb-bots-${lobby}.json`), JSON.stringify(summary, null, 1));
  log('summary', JSON.stringify(summary));
  process.exit(0);
}, timeoutS * 1000);
