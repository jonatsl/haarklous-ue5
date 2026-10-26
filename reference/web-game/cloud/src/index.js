/* Haarklous online multiplayer server (Cloudflare Workers + Durable Objects).
 * The Worker serves the game (static assets in ./public) and routes /lobby/<CODE> WebSockets to one Durable Object per
 * lobby code. A lobby holds up to five players. It relays player states (~10 Hz), vehicle states from whoever is driving,
 * chat, door and firepit events and WebRTC voice signalling, and it arbitrates vehicle seats (one driver, one passenger).
 * It also remembers where vehicles were left, which doors are open, the fire and the last chat lines for players who join.
 */
const MAX_PLAYERS = 5, CODE = /^[A-HJ-NP-Z2-9]{4}$/, FIRE_DECAY_MS = 90000, FIRE_MAX = 6;
const clean = (s, n) => String(s ?? '').replace(/[\u0000-\u001f<>]/g, '').trim().slice(0, n);

export default {
  async fetch(request, env) {
    const url = new URL(request.url), m = url.pathname.match(/^\/lobby\/([^/]+)$/);
    if (m) {
      const code = m[1].toUpperCase();
      if (!CODE.test(code)) return new Response('Bad lobby code', {status: 400});
      if (request.headers.get('Upgrade') !== 'websocket') return new Response('Expected a WebSocket', {status: 426});
      return env.LOBBY.get(env.LOBBY.idFromName(code)).fetch(request);
    }
    if (url.pathname === '/health') return new Response('ok');
    return env.ASSETS.fetch(request);
  }
};

export class Lobby {
  constructor(ctx, env) {
    this.ctx = ctx;this.players = new Map();this.next = 1;
    this.vehicles = {};this.occ = {};this.doors = {};this.fire = {level: 2, t: Date.now()};this.chat = [];
  }
  fetch(request) {
    const code = new URL(request.url).pathname.split('/').pop().toUpperCase();this.code = code;
    const [client, ws] = Object.values(new WebSocketPair());ws.accept();
    if (this.players.size >= MAX_PLAYERS) {ws.send(JSON.stringify({t: 'full', max: MAX_PLAYERS}));ws.close(4001, 'Lobby full');return new Response(null, {status: 101, webSocket: client});}
    const used = new Set([...this.players.values()].map(p => p.slot)), slot = [0, 1, 2, 3, 4].find(i => !used.has(i));
    const p = {id: this.next++, slot, ws, name: 'Player', st: null, mic: false, hello: false};this.players.set(p.id, p);
    ws.addEventListener('message', e => {if (typeof e.data !== 'string' || e.data.length > 16000) return;let m;try {m = JSON.parse(e.data);} catch {return;}this.onMessage(p, m);});
    const bye = () => this.leave(p);ws.addEventListener('close', bye);ws.addEventListener('error', bye);
    return new Response(null, {status: 101, webSocket: client});
  }
  send(p, m) {try {p.ws.send(JSON.stringify(m));} catch {}}
  broadcast(m, except) {const s = JSON.stringify(m);for (const p of this.players.values()) if (p !== except && p.hello) try {p.ws.send(s);} catch {}}
  roster() {return [...this.players.values()].filter(p => p.hello).map(p => ({id: p.id, slot: p.slot, name: p.name, mic: p.mic}));}
  fireNow(t = Date.now()) {return Math.max(0, this.fire.level - (t - this.fire.t) / FIRE_DECAY_MS);}
  onMessage(p, m) {
    switch (m.t) {
      case 'hello': {
        p.name = clean(m.name, 24) || 'Player';p.hello = true;
        const states = {};for (const q of this.players.values()) if (q !== p && q.st) states[q.id] = q.st;
        this.send(p, {t: 'welcome', you: p.id, slot: p.slot, code: this.code, max: MAX_PLAYERS, players: this.roster(), states, vehicles: this.vehicles, occ: this.occ, doors: this.doors, fire: this.fire, chat: this.chat.slice(-20), now: Date.now()});
        this.broadcast({t: 'join', id: p.id, slot: p.slot, name: p.name, mic: p.mic}, p);
        break;
      }
      case 'st': p.st = m.s;this.broadcast({t: 'st', id: p.id, s: m.s}, p);break;
      case 'vs': {const o = this.occ[m.v];if (o && o.d === p.id) {this.vehicles[m.v] = m.s;this.broadcast({t: 'vs', v: m.v, s: m.s}, p);}break;}
      case 'claim': {
        const v = String(m.v), seat = m.seat === 'p' ? 'p' : 'd', o = this.occ[v] || (this.occ[v] = {});
        for (const [k, q] of Object.entries(this.occ)) if (k !== v) {if (q.d === p.id) delete q.d;if (q.p === p.id) delete q.p;}
        const ok = !o[seat] || o[seat] === p.id;if (ok) {o[seat] = p.id;if (o.d === p.id && o.p === p.id) delete o[seat === 'd' ? 'p' : 'd'];}
        this.send(p, {t: 'claim', v, seat, ok, by: o[seat] || null});if (ok) this.broadcast({t: 'occ', occ: this.occ});
        break;
      }
      case 'release': {
        const v = String(m.v), o = this.occ[v];
        if (o) {if (o.d === p.id) {delete o.d;if (m.s) {this.vehicles[v] = m.s;this.broadcast({t: 'vs', v, s: m.s}, p);}}if (o.p === p.id) delete o.p;}
        this.broadcast({t: 'occ', occ: this.occ});break;
      }
      case 'chat': {
        const text = clean(m.text, 200);if (!text) break;
        const line = {id: p.id, slot: p.slot, name: p.name, text, at: Date.now()};this.chat.push(line);if (this.chat.length > 40) this.chat.shift();
        this.broadcast({t: 'chat', ...line});break;
      }
      case 'door': this.doors[clean(m.id, 60)] = !!m.open;this.broadcast({t: 'door', id: clean(m.id, 60), open: !!m.open}, p);break;
      case 'fire': {const now = Date.now();this.fire = {level: Math.min(FIRE_MAX, this.fireNow(now) + 1), t: now};this.broadcast({t: 'fire', fire: this.fire, by: p.id});break;}
      case 'name': p.name = clean(m.name, 24) || p.name;this.broadcast({t: 'name', id: p.id, name: p.name});break;
      case 'mic': p.mic = !!m.on;this.broadcast({t: 'mic', id: p.id, on: p.mic});break;
      case 'rtc': {const q = this.players.get(m.to);if (q && q.hello) this.send(q, {t: 'rtc', from: p.id, d: m.d});break;}
      case 'ping': this.send(p, {t: 'pong', now: Date.now(), c: m.c});break;
    }
  }
  leave(p) {
    if (!this.players.delete(p.id)) return;
    for (const o of Object.values(this.occ)) {if (o.d === p.id) delete o.d;if (o.p === p.id) delete o.p;}
    this.broadcast({t: 'leave', id: p.id});this.broadcast({t: 'occ', occ: this.occ});
  }
}
