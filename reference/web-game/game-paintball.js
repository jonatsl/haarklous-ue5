/* The urban paintball / reball course (ported from the Unreal version, where the user asked for it on 28 Oct 2026):
 * a 64 x 48 m walled yard in the park valley next to the construction site, a 60 x 40 m play area inside a net and a
 * lobby on the lawn outside the gate. Course data: paintball-data.js (the Unreal project's tools/make-paintball.py);
 * models: paintball-models.js.
 *
 * Lobby: walking into it joins the paintball group; the first one in is the admin (earliest join time, then the lowest
 * player id), who picks the game (team deathmatch with respawns, team deathmatch in rounds, capture the flag), paint or
 * reball, and starts a game for everyone in the lobby, up to 6 against 6. Players can ask for a team; the admin balances
 * the rest. Somebody who comes later waits in the lobby for the next game.
 * Rules (normal paintball): a ball that breaks on you (or your marker) puts you out, also from your own team; a ball
 * that bounces does not (reball balls count whenever they hit). Semi-automatic markers, at most 10 balls a second,
 * 90 m/s, 200 balls in the hopper (□ reloads). Team deathmatch with respawns: back in at your base after 5 s, first team
 * to 20 eliminations (or the most after 10 minutes). In rounds: out is out until one team is left, first to 3 rounds.
 * Capture the flag: bring their flag to your base; a carrier who is hit drops it; the owners send it home by touching it
 * (or it goes home after 30 s); back in after 8 s; first to 3 captures.
 * Online: everything rides in the players' own state messages ('st', field 'pb'): lobby time, team wish, game, life,
 * eliminations suffered and by whom, round, the balls fired since the last message, and the admin's game. Each player
 * decides whether a ball hit them (their own view, so lag never puts anybody out unseen); the admin keeps the score.
 *
 * GamePaintball.Rules(data, ctx) is pure logic (Node tests); GamePaintball.create({...}) adds the visuals and the HUD.
 */
(function (root, factory) {
  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.GamePaintball = api;
})(typeof window === 'undefined' ? globalThis : window, function () {
  'use strict';
  const PB = {V0: 90, DRAG: .022, RATE: 10, HOPPER: 200, RELOAD_S: 2.4, TEAM_MAX: 6, COUNT_S: 5, END_S: 10, RESPAWN_TDM: 5, RESPAWN_CTF: 8, FLAG_HOME_S: 30, BODY_R: .25};
  const TEAM_NAME = ['Blue', 'Red'], TEAM_CSS = ['#4d8cff', '#ff5a40'];
  const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
  const v3 = (x = 0, y = 0, z = 0) => ({x, y, z});
  const add = (a, b) => v3(a.x + b.x, a.y + b.y, a.z + b.z), sub = (a, b) => v3(a.x - b.x, a.y - b.y, a.z - b.z), mul = (a, k) => v3(a.x * k, a.y * k, a.z * k);
  const dot = (a, b) => a.x * b.x + a.y * b.y + a.z * b.z, len = a => Math.sqrt(dot(a, a)), norm = a => {const l = len(a);return l > 1e-12 ? mul(a, 1 / l) : v3();};
  const cross = (a, b) => v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x), dist = (a, b) => len(sub(a, b));
  const r3 = v => Math.round(v * 1000) / 1000;
  // Closest distance between segments AB and CD (s = parameter on AB).
  function segSeg(A, B, C, D) {
    const d1 = sub(B, A), d2 = sub(D, C), r = sub(A, C), a = dot(d1, d1), e = dot(d2, d2), f = dot(d2, r);let s = 0, t = 0;
    if (a < 1e-12 && e < 1e-12) return {d: len(r), s: 0};
    if (a < 1e-12) {s = 0;t = clamp(f / e, 0, 1);}
    else {
      const c = dot(d1, r);
      if (e < 1e-12) {t = 0;s = clamp(-c / a, 0, 1);}
      else {const b = dot(d1, d2), den = a * e - b * b;s = den > 1e-12 ? clamp((b * f - c * e) / den, 0, 1) : 0;t = (b * s + f) / e;if (t < 0) {t = 0;s = clamp(-c / a, 0, 1);} else if (t > 1) {t = 1;s = clamp((b - c) / a, 0, 1);}}
    }
    return {d: len(sub(add(A, mul(d1, s)), add(C, mul(d2, t)))), s};
  }
  // A body: a capsule from the feet up (lying along the facing when prone).
  function body(feet, yaw, stance) {
    if (stance === 'prone') {const f = v3(-Math.sin(yaw), 0, -Math.cos(yaw)), b = add(feet, v3(0, .2, 0));return [add(b, mul(f, .75)), sub(b, mul(f, .65))];}
    const top = stance === 'crouch' ? 1.15 : stance === 'sit' ? 1.0 : 1.72;return [add(feet, v3(0, .15, 0)), add(feet, v3(0, top, 0))];
  }
  // Chance that a ball breaks (and marks) at this speed along the surface normal (m/s): bounces when slow or glancing.
  const breakChance = vn => clamp(.15 + (vn - 25) / 40, .05, .95);

  // ---------- Session (the admin's game) and member states: JSON on the wire, short keys ----------
  const newFlag = () => ({s: 'home', by: 0, p: v3(), t: 0});
  const newSession = () => ({id: 0, mode: 'tdm', reball: false, respawn: true, state: 'lobby', t0: 0, dur: 600, lim: 20, team: {}, outs: {}, flag: [newFlag(), newFlag()], caps: [0, 0], rounds: [0, 0], round: 1, endT: 0, winner: -1});
  const teamOf = (S, id) => S.team[id] ?? -1;
  function score(S, t) {
    if (S.mode === 'ctf') return S.caps[t];
    if (!S.respawn) return S.rounds[t];
    let n = 0;for (const [id, o] of Object.entries(S.outs)) if (teamOf(S, +id) === 1 - t) n += o;return n;// eliminations of the other team's players
  }
  const sesToJson = S => ({id: S.id, m: S.mode, rb: S.reball ? 1 : 0, rs: S.respawn ? 1 : 0, st: S.state, t0: S.t0, du: S.dur, li: S.lim, tm: {...S.team}, ou: {...S.outs},
    fl: S.flag.map(f => ({s: f.s, by: f.by, p: [r3(f.p.x), r3(f.p.y), r3(f.p.z)], t: f.t})), ca: [...S.caps], ro: [...S.rounds], rn: S.round, et: S.endT, wi: S.winner});
  function sesFromJson(O) {
    if (!O || typeof O !== 'object') return null;const S = newSession(), num = (v, d) => typeof v === 'number' && isFinite(v) ? v : d;
    S.id = num(O.id, 0);if (typeof O.m === 'string') S.mode = O.m;S.reball = num(O.rb, 0) !== 0;S.respawn = num(O.rs, 1) !== 0;if (typeof O.st === 'string') S.state = O.st;
    S.t0 = num(O.t0, 0);S.dur = num(O.du, 600);S.lim = num(O.li, 20);
    for (const [k, v] of Object.entries(O.tm || {})) S.team[+k] = +v;for (const [k, v] of Object.entries(O.ou || {})) S.outs[+k] = +v;
    (O.fl || []).slice(0, 2).forEach((f, i) => {if (!f) return;S.flag[i] = {s: typeof f.s === 'string' ? f.s : 'home', by: num(f.by, 0), t: num(f.t, 0), p: Array.isArray(f.p) && f.p.length === 3 ? v3(+f.p[0], +f.p[1], +f.p[2]) : v3()};});
    for (let i = 0; i < 2; i++) {S.caps[i] = num((O.ca || [])[i], 0);S.rounds[i] = num((O.ro || [])[i], 0);}
    S.round = num(O.rn, 1);S.endT = num(O.et, 0);S.winner = num(O.wi, -1);return S;
  }
  // A member from its 'pb' state (plus where its avatar is).
  function memberFromJson(O, extra) {
    if (!O || typeof O !== 'object') return null;const num = (v, d) => typeof v === 'number' && isFinite(v) ? v : d;
    return {...extra, lobby: num(O.l, 0), req: num(O.q, -1), ses: num(O.s, 0), alive: num(O.a, 1) !== 0, outs: num(O.o, 0), by: num(O.b, 0), round: num(O.r, 0),
      shots: Array.isArray(O.h) ? O.h.filter(s => Array.isArray(s) && s.length === 8) : [], sesJson: O.ses && typeof O.ses === 'object' ? O.ses : null};
  }

  // ---------- Rules ----------
  class Rules {
    // ctx: {serverNow(), you(), yourName(), yourSlot(), others() -> members, feet() -> {x,y,z}, yaw(), stance(), teleport(x, z, yaw),
    //   cameraPos() -> {x,y,z}, cameraFwd() -> {x,y,z}, ground(x, z), notice(text, s), sound(name, gain), random()}
    constructor(data, ctx) {
      this.ctx = ctx;const c = data.centre, cx = c.x, cs = c.s;
      this.centre = {x: c.world[0], z: c.world[1]};this.theta = c.theta;this.floor = data.floor;this.C = Math.cos(this.theta);this.S = Math.sin(this.theta);
      this.rect0 = {u: data.rect.x0 - cx, v: data.rect.s0 - cs};this.rect1 = {u: this.rect0.u + data.rect.w, v: this.rect0.v + data.rect.d};
      this.halfU = data.play.w / 2;this.halfV = data.play.d / 2;
      this.lobby0 = {u: data.lobby.x0 - cx, v: data.lobby.s0 - cs};this.lobby1 = {u: this.lobby0.u + data.lobby.w, v: this.lobby0.v + data.lobby.d};
      this.gateU0 = data.gate[0] - cx;this.gateU1 = data.gate[1] - cx;
      this.boxes = [];this.obstacles = [];
      const ob = (u, v, w, d, yaw, y0, y1) => {
        const C = Math.cos(yaw), S = Math.sin(yaw);
        this.obstacles.push({points: [[-1, -1], [1, -1], [1, 1], [-1, 1]].map(([a, b]) => {const X = a * w / 2, Z = b * d / 2, p = this.world(u + X * C + Z * S, 0, v - X * S + Z * C);return {x: p.x, z: p.z};}), minY: y0, maxY: y1, kind: 'paintball'});
      };
      for (const B of data.boxes) {// u, v, w, d, y0, h, yaw
        this.boxes.push({c: this.world(B[0], this.floor + B[4] + B[5] / 2, B[1]), h: [B[2] / 2, B[5] / 2, B[3] / 2], yaw: B[6] + this.theta});
        if (B[4] < 1.2) ob(B[0], B[1], B[2], B[3], B[6], this.floor + B[4], this.floor + B[4] + B[5]);
      }
      // The net round the play area and the yard's walls (a gap for the gate).
      const Hu = this.halfU, Hv = this.halfV, F = this.floor;
      ob(0, -Hv, 2 * Hu, .12, 0, F - 1, F + 5);ob(0, Hv, 2 * Hu, .12, 0, F - 1, F + 5);ob(-Hu, 0, .12, 2 * Hv, 0, F - 1, F + 5);ob(Hu, 0, .12, 2 * Hv, 0, F - 1, F + 5);
      const U0 = this.rect0.u, U1 = this.rect1.u, V0 = this.rect0.v, V1 = this.rect1.v;
      ob((U0 + U1) / 2, V0, U1 - U0, .3, 0, F - 12, F + 14);ob(U0, (V0 + V1) / 2, .3, V1 - V0, 0, F - 12, F + 14);ob(U1, (V0 + V1) / 2, .3, V1 - V0, 0, F - 12, F + 14);
      ob((U0 + this.gateU0) / 2, V1, this.gateU0 - U0, .3, 0, F - 12, F + 14);ob((this.gateU1 + U1) / 2, V1, U1 - this.gateU1, .3, 0, F - 12, F + 14);
      this.flags = [];this.spawns = [[], []];
      data.bases.slice(0, 2).forEach((b, i) => {this.flags[i] = {u: b.flag[0], v: b.flag[1]};this.spawns[i] = b.spawns.map(s => ({u: s[0], v: s[1], yaw: s[2]}));});
      // Lobby furniture on the lawn outside the gate (the board faces the lawn, the sign the path).
      const gu = (this.gateU0 + this.gateU1) / 2, gv = V1;
      this.lobbyProps = [['shelter', gu - 8, gv + 7.5, 0], ['bench', gu - 9, gv + 6.8, 0], ['bench', gu - 6.5, gv + 6.8, 0], ['rack', gu - 4, gv + 8.5, 0], ['board', gu + 5.5, gv + 8.5, Math.PI], ['sign', gu + 3.2, gv + 1.2, 0], ['bench', gu + 6, gv + 4.5, 0]]
        .map(([kind, u, v, yaw]) => {const p = this.world(u, 0, v);return {kind, x: p.x, z: p.z, yaw: yaw + this.theta};});
      // State.
      this.ses = newSession();this.all = [];this.adminId = 0;this.inLobby = false;this.lobbyT = 0;this.req = -1;this.mySes = 0;this.leftSes = 0;this.myRound = 0;this.seenState = '';
      this.alive = true;this.outsN = 0;this.outBy = 0;this.respawnT = 0;this.mag = PB.HOPPER;this.reloadT = 0;this.coolT = 0;this.aimT = 0;this.recoilT = 0;this.startHold = 0;this.leaveHold = 0;this.emptyT = 0;
      this.trigger = false;this.seq = 0;this.outbox = [];this.seenShot = new Map();this.seenOuts = new Map();this.feed = [];this.clock = 0;
      this.balls = [];this.newSplats = [];this.newFired = [];this.hitFlash = 0;this.hitTeam = 0;this.onBallHitsFigure = null;
    }
    // ---- course ----
    course(x, z) {const dx = x - this.centre.x, dz = z - this.centre.z;return {u: dx * this.C - dz * this.S, v: dx * this.S + dz * this.C};}
    world(u, y, v) {return v3(this.centre.x + u * this.C + v * this.S, y, this.centre.z - u * this.S + v * this.C);}
    inYard(x, z, m = 0) {const p = this.course(x, z);return p.u > this.rect0.u + m && p.u < this.rect1.u - m && p.v > this.rect0.v + m && p.v < this.rect1.v - m;}
    ground(x, z) {return this.inYard(x, z) ? this.floor : null;}
    inLobbyAt(x, z) {const p = this.course(x, z);return p.u > this.lobby0.u && p.u < this.lobby1.u && p.v > this.lobby0.v && p.v < this.lobby1.v;}
    inPlay(x, z, m = 0) {const p = this.course(x, z);return Math.abs(p.u) < this.halfU - m && Math.abs(p.v) < this.halfV - m;}
    spawnPoint(team, i) {const s = this.spawns[team][i % this.spawns[team].length];return {p: this.world(s.u, this.floor, s.v), yaw: s.yaw + this.theta};}
    baseFlag(t) {return this.world(this.flags[t].u, this.floor + .27, this.flags[t].v);}
    // ---- state ----
    you() {return this.ctx.you ? this.ctx.you() : 0;}
    now() {return this.ctx.serverNow ? this.ctx.serverNow() : this.clock * 1000;}
    rand() {return this.ctx.random ? this.ctx.random() : Math.random();}
    sound(n, g) {if (this.ctx.sound) this.ctx.sound(n, g);}
    notice(t, s) {if (this.ctx.notice) this.ctx.notice(t, s);}
    get member() {return this.lobbyT > 0;}
    inGame() {return this.mySes !== 0 && this.ses.id === this.mySes && this.ses.state !== 'lobby';}
    armed() {return this.inGame() && (this.ses.state === 'play' || this.ses.state === 'count');}
    frozen() {return this.inGame() && this.ses.state === 'count';}
    busy() {return this.inGame() || this.inLobby;}
    isAdmin() {return this.adminId === this.you();}
    myTeam() {return this.inGame() ? teamOf(this.ses, this.you()) : -1;}
    teamOfMember(id) {return teamOf(this.ses, id);}
    memberPlaying(id) {
      if (!this.ses.id || this.ses.state === 'lobby' || teamOf(this.ses, id) < 0) return false;
      const m = this.all.find(m => m.id === id);return !!m && m.ses === this.ses.id;
    }
    addFeed(text) {this.feed.push({text, t: this.clock});if (this.feed.length > 5) this.feed.shift();}
    update(dt, input = {}) {
      this.clock += dt;this.hitFlash = Math.max(0, this.hitFlash - dt);this.newFired = [];
      this.outbox = this.outbox.filter(s => this.now() - s[1] <= 2000);// our balls that nobody took
      this.readMembers();
      // The admin: the earliest member (then the lowest id). Its game is everybody's game.
      this.adminId = 0;let best = 0;
      for (const m of this.all) if (m.lobby > 0 && (best === 0 || m.lobby < best || (m.lobby === best && m.id < this.adminId))) {best = m.lobby;this.adminId = m.id;}
      if (this.adminId !== this.you()) for (const m of this.all) if (m.id === this.adminId && m.sesJson) {const s = sesFromJson(m.sesJson);if (s) this.ses = s;}
      if (best > 0 && this.adminId === this.you()) this.runAdmin(dt, input);
      // Balls the others fired (each once, caught up to now).
      for (const m of this.all) {
        if (m.local) continue;let last = this.seenShot.get(m.id) ?? 0;
        for (const s of m.shots) {
          if (s[0] <= last) continue;last = s[0];
          const age = (this.now() - s[1]) / 1000;if (age > 1.5 || !m.ses || m.ses !== this.ses.id) continue;
          const b = {owner: m.id, team: Math.max(0, teamOf(this.ses, m.id)), p: v3(s[2], s[3], s[4]), v: v3(s[5], s[6], s[7]), age: 0, bounces: 0};
          this.newFired.push(b.team);
          const keep = this.balls;this.balls = [b];if (age > 0) this.stepBalls(age);this.balls = keep.concat(this.balls);
        }
        this.seenShot.set(m.id, last);
      }
      // The feed: who put whom out.
      for (const m of this.all) {
        const seen = this.seenOuts.get(m.id) || 0;
        if (m.ses === this.ses.id && this.ses.id && m.outs > seen) {
          const by = (this.all.find(k => k.id === m.by) || {}).name || '?';this.addFeed(`${by}  hit  ${m.name}`);
          if (m.by === this.you() && !m.local) this.notice(`Hit! ${m.name} is out.`, 2);
        }
        this.seenOuts.set(m.id, m.ses === this.ses.id ? m.outs : 0);
      }
      this.local(dt, input);this.stepBalls(dt);
    }
    readMembers() {
      const c = this.ctx;
      this.all = [{id: this.you(), local: true, name: c.yourName ? c.yourName() : 'You', slot: c.yourSlot ? c.yourSlot() : 0, lobby: this.lobbyT, req: this.req, ses: this.inGame() ? this.mySes : 0, alive: this.alive, outs: this.outsN, by: this.outBy, round: this.myRound,
        feet: c.feet ? c.feet() : v3(), yaw: c.yaw ? c.yaw() : 0, stance: c.stance ? c.stance() : 'stand', shots: [], sesJson: null}];
      for (const m of (c.others ? c.others() : [])) if (m && m.lobby > 0) this.all.push(m);
    }
    runAdmin(dt, input) {
      const S = this.ses, t = this.now();
      if (S.state === 'lobby') {
        if (!this.inLobby) return;
        // ←→ the game: team deathmatch with respawns, in rounds, capture the flag. △ paint or reball.
        if (input.left || input.right) {const k = S.mode === 'ctf' ? 2 : S.respawn ? 0 : 1, n = (k + (input.right ? 1 : 2)) % 3;S.mode = n === 2 ? 'ctf' : 'tdm';S.respawn = n !== 1;S.lim = n === 0 ? 20 : 3;this.sound('beep', .4);}
        if (input.triangle) {S.reball = !S.reball;this.sound('beep', .4);}
        this.startHold = input.crossHeld ? this.startHold + dt : 0;
        if (this.startHold > 1) {this.startHold = 0;this.startGame();}
        return;
      }
      // Nobody is in the game any more (everyone left or went offline): back to the lobby so a new game can start.
      if (S.state === 'count' || S.state === 'play') {
        const anyone = this.all.some(m => teamOf(S, m.id) >= 0 && m.ses === S.id);this.emptyT = anyone ? 0 : this.emptyT + dt;
        if (this.emptyT > 3) {this.emptyT = 0;S.state = 'lobby';return;}
      }
      if (S.state === 'count') {if (t >= S.t0) S.state = 'play';return;}
      if (S.state === 'end') {if (t >= S.endT + PB.END_S * 1000) S.state = 'lobby';return;}
      // Playing.
      const in0 = [], in1 = [];
      for (const m of this.all) {const tm = teamOf(S, m.id);if (tm < 0 || m.ses !== S.id) continue;S.outs[m.id] = Math.max(S.outs[m.id] || 0, m.outs);(tm === 0 ? in0 : in1).push(m);}
      let end = t >= S.t0 + S.dur * 1000;
      if (S.mode === 'tdm' && S.respawn) end = end || score(S, 0) >= S.lim || score(S, 1) >= S.lim;
      if (S.mode === 'tdm' && !S.respawn && in0.length && in1.length) {
        // A round: out is out until one team is left (players not yet in the new round count as alive).
        const down = L => L.every(m => !m.alive && m.round === S.round), d0 = down(in0), d1 = down(in1);
        if (d0 || d1) {const w = d0 && d1 ? -1 : d0 ? 1 : 0;if (w >= 0) S.rounds[w]++;if (w >= 0 && S.rounds[w] >= S.lim) end = true;else {S.round++;this.sound('whistle', .6);}}
      }
      if (S.mode === 'ctf') for (let F = 0; F < 2; F++) {
        const fl = S.flag[F], owners = F === 0 ? in0 : in1, takers = F === 0 ? in1 : in0;
        if (fl.s === 'carried') {
          const c = takers.find(m => m.id === fl.by);
          if (!c || !c.alive) {fl.s = 'dropped';fl.t = t;fl.by = 0;if (c) fl.p = add(c.feet, v3(0, .05, 0));continue;}
          fl.p = {...c.feet};
          const u = this.course(c.feet.x, c.feet.z).u;// home: the carrier's own base, beyond its base line
          if ((F === 0 && u > 25) || (F === 1 && u < -25)) {S.caps[1 - F]++;S.flag[F] = {...newFlag(), p: this.baseFlag(F)};this.sound('cheer', .5);if (S.caps[1 - F] >= S.lim) end = true;}
          continue;
        }
        if (fl.s === 'dropped') {let back = t - fl.t > PB.FLAG_HOME_S * 1000;for (const m of owners) if (m.alive && dist(m.feet, fl.p) < 1.4) back = true;if (back) {S.flag[F] = {...newFlag(), p: this.baseFlag(F)};continue;}}
        if (fl.s === 'home') fl.p = this.baseFlag(F);
        for (const m of takers) if (m.alive && Math.hypot(m.feet.x - fl.p.x, m.feet.z - fl.p.z) < 1.4 && Math.abs(m.feet.y - fl.p.y) < 1.5) {fl.s = 'carried';fl.by = m.id;break;}
      }
      if (end) {S.state = 'end';S.endT = t;const a = score(S, 0), b = score(S, 1);S.winner = a > b ? 0 : b > a ? 1 : -1;}
    }
    startGame() {
      // Everybody in the lobby, in the order they came; wishes first (6 a team at most), the rest balanced.
      const S = this.ses, L = this.all.filter(m => m.lobby > 0).sort((a, b) => a.lobby - b.lobby || a.id - b.id), n = [0, 0];
      S.team = {};S.outs = {};
      for (const m of L) if (m.req >= 0 && n[m.req] < PB.TEAM_MAX) {S.team[m.id] = m.req;n[m.req]++;}
      for (const m of L) {if (m.id in S.team) continue;const t = n[0] <= n[1] ? 0 : 1;if (n[t] >= PB.TEAM_MAX) continue;S.team[m.id] = t;n[t]++;}
      S.id = Math.floor((this.now() / 1000) % 1e8) + 1;S.state = 'count';S.t0 = this.now() + PB.COUNT_S * 1000;S.dur = 600;S.lim = S.mode === 'tdm' && S.respawn ? 20 : 3;
      for (let i = 0; i < 2; i++) {S.caps[i] = S.rounds[i] = 0;S.flag[i] = {...newFlag(), p: this.baseFlag(i)};}
      S.round = 1;S.endT = 0;S.winner = -1;
    }
    spawn(first) {
      const t = teamOf(this.ses, this.you());if (t < 0 || !this.spawns[t].length) return;
      const ids = Object.entries(this.ses.team).filter(([, v]) => v === t).map(([k]) => +k).sort((a, b) => a - b), sp = this.spawnPoint(t, Math.max(0, ids.indexOf(this.you())));
      if (this.ctx.teleport) this.ctx.teleport(sp.p.x, sp.p.z, sp.yaw);
      this.alive = true;this.respawnT = 0;this.mag = PB.HOPPER;this.reloadT = 0;
      if (first) this.notice(`You play for ${TEAM_NAME[t]}. ${this.ses.mode === 'ctf' ? 'Bring their flag to your base.' : this.ses.respawn ? 'Put out as many of them as you can.' : 'Out is out until the round is won.'}`, 5);
    }
    toLobby() {const p = this.world((this.gateU0 + this.gateU1) / 2 + (this.rand() - .5) * 10, 0, this.rect1.v + 4 + this.rand() * 4);if (this.ctx.teleport) this.ctx.teleport(p.x, p.z, this.theta);this.alive = true;this.respawnT = 0;}
    local(dt, input) {
      const f = this.ctx.feet ? this.ctx.feet() : v3();this.inLobby = this.inLobbyAt(f.x, f.z);const t = teamOf(this.ses, this.you());
      // Out of a finished game: back to the lobby, still a member (the admin stays the admin).
      if (this.mySes !== 0 && (this.ses.id !== this.mySes || this.ses.state === 'lobby' || t < 0)) {this.mySes = 0;this.toLobby();this.inLobby = true;}
      // Joining and leaving the group.
      if (this.inLobby && this.lobbyT === 0) {this.lobbyT = Math.max(1, Math.round(this.now()));this.notice('Paintball lobby. Wait here for the others; the first one here runs the games.', 5);}
      if (!this.inLobby && !this.inGame() && this.lobbyT > 0) {this.lobbyT = 0;this.req = -1;}
      if (this.inLobby && this.ses.state === 'lobby') {
        if (input.up) this.req = this.req === -1 ? 0 : this.req === 0 ? 1 : -1;
        if (input.down) this.req = this.req === -1 ? 1 : this.req === 1 ? 0 : -1;
      }
      // Into a new game.
      if (this.ses.id && this.ses.state !== 'lobby' && t >= 0 && this.mySes !== this.ses.id && this.leftSes !== this.ses.id && this.lobbyT > 0) {
        this.mySes = this.ses.id;this.outsN = 0;this.outBy = 0;this.myRound = this.ses.round;this.spawn(true);this.seenState = '';
      }
      if (!this.inGame()) {this.aimT = 0;return;}
      if (this.seenState !== this.ses.state) {if (this.ses.state === 'play' || this.ses.state === 'end') this.sound('whistle', .8);this.seenState = this.ses.state;}
      if (this.ses.round !== this.myRound) {this.myRound = this.ses.round;this.spawn(false);}
      if (!this.alive && this.respawnT > 0 && (this.respawnT -= dt) <= 0) this.spawn(false);
      // Leave the game: hold △ for 2 s.
      this.leaveHold = input.triangleHeld ? this.leaveHold + dt : 0;
      if (this.leaveHold > 2) {this.leaveHold = 0;this.leftSes = this.mySes;this.mySes = 0;this.toLobby();this.inLobby = true;this.notice('You left the game.', 3);return;}
      // The marker.
      this.coolT -= dt;this.recoilT = Math.max(0, this.recoilT - dt);
      if (this.reloadT > 0 && (this.reloadT -= dt) <= 0) {this.reloadT = 0;this.mag = PB.HOPPER;this.sound('click', .5);}
      const can = this.ses.state === 'play' && this.alive;
      if (input.reload && can && this.mag < PB.HOPPER && this.reloadT <= 0) {this.reloadT = PB.RELOAD_S;this.sound('chains', .25);}
      this.aimT += ((input.l2 > .3 && can && this.reloadT <= 0 ? 1 : 0) - this.aimT) * Math.min(1, dt * 10);
      if (input.r2 > .55 && !this.trigger) {
        this.trigger = true;
        if (can && this.reloadT <= 0 && this.coolT <= 0) {if (this.mag > 0) this.fire(this.ctx.cameraFwd ? this.ctx.cameraFwd() : v3(0, 0, -1));else {this.sound('pistolClick', .6);this.reloadT = PB.RELOAD_S;}}
      } else if ((input.r2 || 0) < .35) this.trigger = false;
    }
    // The ball leaves the barrel just under the eye, with the marker's spread (less when aiming down the barrel).
    fire(dir) {
      const spread = (this.aimT > .5 ? .5 : 1.2) * Math.PI / 180;let d = norm(dir);
      const side = norm(cross(d, v3(0, 1, 0))), up = cross(side, d), a = this.rand() * 2 * Math.PI, r = Math.sqrt(-2 * Math.log(Math.max(1e-6, this.rand()))) * .5 * spread;
      d = norm(add(d, add(mul(side, Math.cos(a) * r), mul(up, Math.sin(a) * r))));
      const cam = this.ctx.cameraPos ? this.ctx.cameraPos() : v3(), b = {owner: this.you(), team: Math.max(0, this.myTeam()), p: sub(add(cam, mul(d, .45)), v3(0, .07, 0)), v: mul(d, PB.V0 * (this.ses.reball ? .94 : 1) * (1 + (this.rand() - .5) * .04)), age: 0, bounces: 0};
      this.balls.push(b);this.outbox.push([++this.seq, Math.round(this.now()), r3(b.p.x), r3(b.p.y), r3(b.p.z), r3(b.v.x), r3(b.v.y), r3(b.v.z)]);
      this.mag--;this.coolT = 1 / PB.RATE;this.recoilT = .08;this.sound('pbShot', .7);
    }
    out(by, team) {
      this.alive = false;this.outsN++;this.outBy = by;this.hitFlash = 1.5;this.hitTeam = team;
      this.respawnT = this.ses.mode === 'ctf' ? PB.RESPAWN_CTF : this.ses.respawn ? PB.RESPAWN_TDM : 0;this.aimT = 0;this.reloadT = 0;
      const m = this.all.find(m => m.id === by), name = m ? (m.local ? 'yourself' : m.name) : 'someone';
      this.sound('pbSplat', 1);this.sound('hit', .6);
      this.notice(this.respawnT > 0 ? `HIT by ${name}! Out – back in at your base in ${Math.round(this.respawnT)} s.` : `HIT by ${name}! Out until the next round.`, 4);
    }
    trace(A, B) {
      let T = 2, N = v3(0, 1, 0), fig = 0;
      // The ground (the yard floor or the park).
      const g = (x, z) => this.ctx.ground ? this.ctx.ground(x, z) : this.floor, ga = A.y - g(A.x, A.z), gb = B.y - g(B.x, B.z);
      if (gb < 0 && ga >= 0) {T = ga / Math.max(1e-9, ga - gb);N = v3(0, 1, 0);}
      // Cover.
      for (const X of this.boxes) {
        const da = sub(A, X.c), db = sub(B, X.c);
        if (Math.abs(da.x) > 8 && Math.abs(db.x) > 8 && Math.sign(da.x) === Math.sign(db.x)) continue;
        if (Math.abs(da.z) > 8 && Math.abs(db.z) > 8 && Math.sign(da.z) === Math.sign(db.z)) continue;
        const C = Math.cos(X.yaw), S = Math.sin(X.yaw), toL = v => [v.x * C - v.z * S, v.y, v.x * S + v.z * C], la = toL(da), lb = toL(db), D = [lb[0] - la[0], lb[1] - la[1], lb[2] - la[2]];
        let t0 = 0, t1 = 1, axis = -1, sg = 0, miss = false;
        for (let k = 0; k < 3 && !miss; k++) {
          const P = la[k], dk = D[k], H = X.h[k];
          if (Math.abs(dk) < 1e-12) {if (P < -H || P > H) miss = true;continue;}
          let ta = (-H - P) / dk, tb = (H - P) / dk, s = -1;if (ta > tb) {[ta, tb] = [tb, ta];s = 1;}
          if (ta > t0) {t0 = ta;axis = k;sg = s;}t1 = Math.min(t1, tb);if (t0 > t1) miss = true;
        }
        if (miss || axis < 0 || t0 >= T) continue;
        const ln = [0, 0, 0];ln[axis] = sg;T = t0;N = v3(ln[0] * C + ln[2] * S, ln[1], -ln[0] * S + ln[2] * C);// back to the world
      }
      // The net catches what crosses it below its top.
      {
        const ca = this.course(A.x, A.z), cb = this.course(B.x, B.z), ia = Math.abs(ca.u) < this.halfU && Math.abs(ca.v) < this.halfV, ib = Math.abs(cb.u) < this.halfU && Math.abs(cb.v) < this.halfV;
        if (ia !== ib) {
          let tn = 1;
          for (let k = 0; k < 2; k++) {const H = k === 0 ? this.halfU : this.halfV, pa = k === 0 ? ca.u : ca.v, pb = k === 0 ? cb.u : cb.v;for (const E of [-H, H]) if ((pa - E) * (pb - E) < 0) tn = Math.min(tn, (E - pa) / (pb - pa));}
          const y = A.y + (B.y - A.y) * tn;if (tn < T && y < this.floor + 5) {T = tn;N = v3(0, 1, 0);fig = -1;}
        }
      }
      return {hit: T <= 1, t: T, n: N, fig};
    }
    stepBalls(dt) {
      const H = 1 / 120, steps = clamp(Math.ceil(dt / H), 1, 240), h = dt / steps, cam = this.ctx.cameraPos ? this.ctx.cameraPos() : v3();
      // Bodies balls can hit: ours and the others' who are in this game.
      const figs = [];
      for (const m of this.all) {
        if (m.local ? !(this.inGame() && this.alive) : !(m.ses === this.ses.id && this.ses.id && m.alive)) continue;
        const [a, b] = body(m.feet, m.yaw, m.stance);figs.push({id: m.id, a, b});
      }
      for (let s = 0; s < steps; s++) for (let i = this.balls.length - 1; i >= 0; i--) {
        const B = this.balls[i], A = B.p;
        B.v.y -= 9.81 * h;B.v = sub(B.v, mul(B.v, PB.DRAG * len(B.v) * h));
        const E = add(A, mul(B.v, h));let {hit, t: T, n: N, fig} = this.trace(A, E), isBody = false, victim = 0;
        for (const F of figs) {
          if (F.id === B.owner && B.age < .3) continue;
          const r = segSeg(A, E, F.a, F.b);if (r.d < PB.BODY_R && r.s < T) {T = r.s;isBody = true;victim = F.id;fig = 0;N = mul(norm(B.v), -1);hit = true;}
        }
        if (!hit) {B.p = E;B.age += h;if (B.age > 3 || B.p.y < this.floor - 25) this.balls.splice(i, 1);continue;}
        const P = add(A, mul(sub(E, A), T)), vn = Math.abs(dot(B.v, N)), d = dist(P, cam), near = d < 30;
        if (fig === -1) {this.balls.splice(i, 1);continue;}// the net
        if (isBody) {
          const breaks = this.ses.reball || this.rand() < breakChance(len(B.v) * .9);
          if (victim === this.you()) {
            if (B.owner !== this.you() || B.age >= .3) {if (breaks && this.alive && this.ses.state === 'play' && this.inGame()) this.out(B.owner, B.team);else if (!breaks) this.sound('pbBounce', .5);}
          } else {if (this.onBallHitsFigure) this.onBallHitsFigure(B.owner, victim);if (breaks && !this.ses.reball) this.newSplats.push({p: P, n: N, team: B.team, onFigure: victim});}
          this.balls.splice(i, 1);continue;
        }
        if (!this.ses.reball && this.rand() < breakChance(vn)) {
          this.newSplats.push({p: add(P, mul(N, .004)), n: N, team: B.team, onFigure: 0});if (near) this.sound('pbSplat', clamp(1 - d / 30, .1, .6));this.balls.splice(i, 1);continue;
        }
        // A bounce: what is left of the speed goes off the surface; it soon drops.
        B.v = mul(sub(B.v, mul(N, 2 * dot(B.v, N))), .3);B.p = add(P, mul(N, .01));
        if (++B.bounces > 2 || len(B.v) < 4) this.balls.splice(i, 1);
        if (near && B.bounces <= 1) this.sound('pbBounce', .3);
      }
    }
    // 'pb' for our next 'st' (takes the balls fired since the last one); null when we are not in the group.
    localState() {
      if (this.lobbyT <= 0 && !this.inGame()) {this.outbox = [];return null;}
      const o = {l: Math.round(this.lobbyT), q: this.req, s: this.inGame() ? this.mySes : 0, a: this.alive ? 1 : 0, o: this.outsN, b: this.outBy, r: this.myRound};
      if (this.outbox.length) {o.h = this.outbox;this.outbox = [];}
      if (this.isAdmin()) o.ses = sesToJson(this.ses);
      return o;
    }
    flagPos(team) {const f = this.ses.flag[team], carrier = f.s === 'carried' ? f.by : -1;return {p: f.s === 'home' || (!f.p.x && !f.p.y && !f.p.z) ? this.baseFlag(team) : f.p, carrier};}
    modeName() {return this.ses.mode === 'ctf' ? 'Capture the flag' : this.ses.respawn ? 'Team deathmatch' : 'Team deathmatch – rounds';}
  }

  // ---------- In the game: the course, lobby furniture, flags, balls, splats, the marker, the others' gear, the HUD ----------
  function create({T, scene, overlay, models, data, view, groundAt, ctx, rules = new Rules(data, ctx)}) {
    const group = new T.Group();group.name = 'Paintball';scene.add(group);
    const course = models.course();course.position.set(rules.centre.x, rules.floor, rules.centre.z);course.rotation.y = rules.theta;group.add(course);
    for (const p of rules.lobbyProps) {const m = models[p.kind] && models[p.kind]();if (!m) continue;m.position.set(p.x, groundAt(p.x, p.z) - .02, p.z);m.rotation.y = p.yaw;group.add(m);}
    const flags = [0, 1].map(t => {const f = models.flag(t);f.visible = false;group.add(f);return f;});
    const MAXB = 600, MAXS = 400, mtx = new T.Matrix4(), q = new T.Quaternion(), one = new T.Vector3(1, 1, 1), up = new T.Vector3(0, 0, 1);
    const balls = [0, 1].map(t => {const b = models.ball(t), m = new T.InstancedMesh(b.geometry, b.material, MAXB);m.count = 0;m.frustumCulled = false;m.castShadow = false;group.add(m);return m;});
    const splats = [0, 1].map(t => {const s = models.splat(t), m = new T.InstancedMesh(s.geometry, s.material, MAXS);m.count = 0;m.frustumCulled = false;m.renderOrder = 2;group.add(m);return {m, next: 0};});
    let sesSeen = 0;
    // The marker in our hands (first-person overlay).
    const marker = models.marker();marker.visible = false;marker.scale.setScalar(.85);overlay.add(marker);
    // HUD.
    const el = document.createElement('div');el.id = 'pbHud';el.innerHTML = '<div class="pb-score" hidden></div><div class="pb-big" hidden></div><div class="pb-lobby" hidden></div><div class="pb-feed"></div><div class="pb-ammo" hidden></div><div class="pb-cross" hidden></div><div class="pb-paint" hidden></div>';view.append(el);
    const $ = c => el.querySelector('.' + c), esc = s => String(s).replace(/[<>&"]/g, c => ({'<': '&lt;', '>': '&gt;', '&': '&amp;', '"': '&quot;'})[c]);
    let hudT = 0;
    function hud() {
      const R = rules, S = R.ses, t = R.now(), inGame = R.inGame(), showScore = inGame || (S.state !== 'lobby' && S.id !== 0 && R.member);
      const sc = $('pb-score');sc.hidden = !showScore;
      if (showScore) {
        const left = S.state === 'count' ? (S.t0 - t) / 1000 : S.state === 'play' ? S.dur - (t - S.t0) / 1000 : 0, l = Math.max(0, left);
        sc.innerHTML = `<b style="color:${TEAM_CSS[0]}">${score(S, 0)}</b><span><big>${Math.floor(l / 60)}:${String(Math.floor(l % 60)).padStart(2, '0')}</big><small>${R.modeName()} · first to ${S.lim}</small></span><b style="color:${TEAM_CSS[1]}">${score(S, 1)}</b>` +
          (S.mode === 'ctf' ? `<em style="color:${TEAM_CSS[0]}">Blue flag: ${S.flag[0].s === 'carried' ? 'taken' : S.flag[0].s}</em><em style="color:${TEAM_CSS[1]}">Red flag: ${S.flag[1].s === 'carried' ? 'taken' : S.flag[1].s}</em>` : '');
        const big = $('pb-big');big.hidden = !(S.state === 'count' || S.state === 'end');
        if (S.state === 'count') big.innerHTML = `<span class="n">${Math.ceil(l)}</span>`;
        else if (S.state === 'end') big.innerHTML = `<b style="color:${S.winner < 0 ? '#fff' : TEAM_CSS[S.winner]}">${S.winner < 0 ? 'A draw' : TEAM_NAME[S.winner] + ' team wins'}</b><small>${score(S, 0)} : ${score(S, 1)} · back to the lobby soon</small>`;
      } else $('pb-big').hidden = true;
      const am = $('pb-ammo');am.hidden = !inGame;
      if (inGame) {
        const tm = R.myTeam();
        am.innerHTML = `<div class="team" style="color:${tm >= 0 ? TEAM_CSS[tm] : '#fff'}">${tm >= 0 ? TEAM_NAME[tm] + ' team' : ''}</div><div class="${R.alive ? 'keys' : 'warn'}">${R.alive ? 'R2 fire · L2 aim · □ reload · hold △ leave' : R.respawnT > 0 ? `OUT · back in ${Math.ceil(R.respawnT)} s` : 'OUT · wait for the next round'}</div>` +
          `<div class="mag"><b>${R.mag > 0 ? R.mag : 'EMPTY'}</b><small>${R.reloadT > 0 ? 'RELOADING' : S.reball ? 'reballs' : 'paintballs'}</small></div>`;
      }
      $('pb-cross').hidden = !(inGame && R.alive && S.state === 'play' && R.aimT < .5);
      const lb = $('pb-lobby'), showLobby = R.inLobby && !inGame;lb.hidden = !showLobby;
      if (showLobby) {
        const L = R.all.filter(m => m.lobby > 0).sort((a, b) => a.lobby - b.lobby);
        lb.innerHTML = `<b>PAINTBALL LOBBY</b><div>${R.modeName()} · ${S.reball ? 'reball' : 'paint'}</div><small>${S.state === 'lobby' ? 'Waiting for the admin to start' : 'A game is on: you play in the next one'}</small><ul>` +
          L.map(m => `<li class="${m.local ? 'me' : ''}"><span>${esc(m.name)}${m.id === R.adminId ? '  (admin)' : ''}</span><i style="color:${m.req < 0 ? '#aab' : TEAM_CSS[m.req]}">${m.req < 0 ? 'any team' : TEAM_NAME[m.req]}</i></li>`).join('') +
          `</ul><small>${R.isAdmin() ? `↑↓ team · ←→ game · △ ${S.reball ? 'paint' : 'reball'} · hold ✕ start` : '↑↓ team · the admin starts the game'}</small>${R.startHold > 0 ? `<i class="meter"><u style="width:${Math.round(Math.min(1, R.startHold) * 100)}%"></u></i>` : ''}`;
      }
      $('pb-feed').innerHTML = R.feed.filter(f => R.clock - f.t < 6).map(f => `<div>${esc(f.text)}</div>`).join('');
    }
    // Others' gear in a game: the marker in the right hand, goggles, armbands in the team colour.
    const gear = new Map();
    function updateGear(remotes) {
      const seen = new Set();
      for (const r of remotes || []) {
        if (!rules.memberPlaying(r.id) || !r.av || !r.av.joints) continue;seen.add(r.id);const team = Math.max(0, rules.teamOfMember(r.id));
        let G = gear.get(r.id);
        if (!G || G.av !== r.av || G.team !== team) {
          if (G) G.parts.forEach(p => p.parent && p.parent.remove(p));
          const J = r.av.joints, put = (parent, obj, pos, rot) => {obj.position.set(...pos);obj.rotation.set(...rot);parent.add(obj);return obj;};
          G = {av: r.av, team, parts: [put(J.R.el, models.marker(), [0, -.28, -.02], [-Math.PI / 2, 0, 0]), put(J.head, models.mask(), [0, -.02, -.01], [0, 0, 0]), put(J.L.sh, models.band(team), [0, -.12, 0], [0, 0, Math.PI / 2]), put(J.R.sh, models.band(team), [0, -.12, 0], [0, 0, Math.PI / 2])]};
          gear.set(r.id, G);
        }
      }
      for (const [id, G] of gear) if (!seen.has(id)) {G.parts.forEach(p => p.parent && p.parent.remove(p));gear.delete(id);}
    }
    function update(dt, input, {remotes, onFoot, camera}) {
      rules.update(dt, input);
      // A new game: a clean course.
      if (rules.ses.id !== sesSeen && rules.ses.state === 'count') {sesSeen = rules.ses.id;splats.forEach(s => {s.m.count = 0;s.next = 0;});}
      // Balls in flight.
      const n = [0, 0];
      for (const b of rules.balls) {const t = b.team ? 1 : 0;if (n[t] >= MAXB) continue;mtx.makeTranslation(b.p.x, b.p.y, b.p.z);balls[t].setMatrixAt(n[t]++, mtx);}
      balls.forEach((m, t) => {m.count = n[t];m.instanceMatrix.needsUpdate = true;});
      // Paint where a ball broke (on the course; those on players show as their hit).
      for (const s of rules.newSplats) {
        if (s.onFigure) continue;const S = splats[s.team ? 1 : 0], k = .18 * (.6 + Math.random() * .8);
        q.setFromUnitVectors(up, new T.Vector3(s.n.x, s.n.y, s.n.z)).multiply(new T.Quaternion().setFromAxisAngle(up, Math.random() * Math.PI * 2));
        mtx.compose(new T.Vector3(s.p.x, s.p.y, s.p.z), q, one.clone().multiplyScalar(k));
        if (S.m.count < MAXS) S.m.setMatrixAt(S.m.count++, mtx);else {S.m.setMatrixAt(S.next, mtx);S.next = (S.next + 1) % MAXS;}
        S.m.instanceMatrix.needsUpdate = true;
      }
      rules.newSplats = [];
      // Flags (capture the flag only), on a carrier's back when carried.
      const ctf = rules.ses.mode === 'ctf' && rules.ses.state !== 'lobby';
      for (let t = 0; t < 2; t++) {
        flags[t].visible = ctf;if (!ctf) continue;const {p, carrier} = rules.flagPos(t);let pos = p, yaw = rules.theta;
        if (carrier >= 0) {const m = rules.all.find(m => m.id === carrier);if (m) {pos = add(m.feet, v3(Math.sin(m.yaw) * .3, .35, Math.cos(m.yaw) * .3));yaw = m.yaw;}}
        flags[t].position.set(pos.x, pos.y, pos.z);flags[t].rotation.y = yaw;
      }
      // The marker in our hands (at the hip; aiming lower and to the right so the middle of the view is clear; a kick on each shot).
      const armed = rules.armed() && rules.alive && onFoot;marker.visible = armed;
      if (armed) {
        const A = rules.aimT, K = rules.recoilT / .08, Rl = rules.reloadT > 0 ? Math.sin(Math.PI * (1 - rules.reloadT / PB.RELOAD_S)) : 0;
        marker.position.set(.2 - .06 * A, -.25 - .04 * A - .08 * Rl, -.66 + .04 * A + .03 * K);marker.rotation.set(.035 * K - .44 * Rl, .07 * (1 - A), .26 * Rl);
      }
      updateGear(remotes);
      if ((hudT -= dt) <= 0) {hudT = .1;hud();}
      const paint = $('pb-paint');paint.hidden = rules.hitFlash <= 0;if (rules.hitFlash > 0) {paint.style.opacity = Math.min(1, rules.hitFlash).toFixed(2);paint.dataset.team = rules.hitTeam;}
      void camera;
    }
    return {rules, group, update, obstacles: rules.obstacles, marker, get busy() {return rules.busy();}, get armed() {return rules.inGame();}, get frozen() {return rules.frozen();}, get aim() {return rules.aimT;}};
  }
  return {Rules, create, PB, sesToJson, sesFromJson, memberFromJson, score};
});
