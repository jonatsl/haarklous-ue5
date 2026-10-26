/* Multiplayer connection: one WebSocket to the lobby's Durable Object on Cloudflare (see cloud/src/index.js).
 * Every player has an own lobby code (4 characters, kept in localStorage) and starts in it; others join with that code.
 * Up to five players per lobby. Reconnects with back-off; a full or unreachable lobby falls back to your own.
 * GameNet.create({onMessage(m), onStatus()}) -> {connect(code), home(), send(m), setName(n), status, code, ownCode, you,
 *   players: Map(id -> {id, slot, name, mic}), others, serverNow(), base}
 */
(function (root, factory) {
  root.GameNet = factory();
})(typeof window === 'undefined' ? globalThis : window, function () {
  'use strict';
  const ALPHABET = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789';
  // The deployed lobby server. When the game itself is served from workers.dev it uses its own host.
  const DEPLOYED = 'haarklous-online.server-smoke.workers.dev';
  const store = {get(k) {try {return localStorage.getItem(k);} catch {return null;}}, set(k, v) {try {localStorage.setItem(k, v);} catch {}}};
  const randomCode = () => Array.from({length: 4}, () => ALPHABET[Math.floor(Math.random() * ALPHABET.length)]).join('');
  const validCode = c => /^[A-HJ-NP-Z2-9]{4}$/.test(c);
  function serverBase() {
    const q = new URLSearchParams(location.search).get('server');
    if (q) return q.replace(/\/$/, '');
    if (location.protocol === 'https:' && /workers\.dev$/.test(location.hostname)) return `wss://${location.host}`;
    return `wss://${DEPLOYED}`;
  }
  function create({onMessage, onStatus}) {
    let ownCode = store.get('haarklous-lobby-code-v1');if (!validCode(ownCode || '')) {ownCode = randomCode();store.set('haarklous-lobby-code-v1', ownCode);}
    let name = store.get('haarklous-name-v1') || `Player ${Math.floor(100 + Math.random() * 900)}`;
    let ws = null, code = null, want = null, you = null, slot = 0, retry = 0, timer = null, offset = 0, status = 'offline', alive = false;
    const players = new Map(), base = serverBase();
    const setStatus = s => {status = s;onStatus && onStatus();};
    function connect(c) {c = String(c || '').toUpperCase().replace(/[^A-Z0-9]/g, '');if (!validCode(c)) return false;want = c;retry = 0;open();return true;}
    function open() {
      shut();if (!want || typeof WebSocket === 'undefined') return;code = want;setStatus('connecting');
      let sock;try {sock = new WebSocket(`${base}/lobby/${code}`);} catch {setStatus('offline');later();return;}
      ws = sock;alive = true;
      sock.onopen = () => {if (ws !== sock) return;retry = 0;sock.send(JSON.stringify({t: 'hello', name}));};
      sock.onmessage = e => {
        if (ws !== sock) return;let m;try {m = JSON.parse(e.data);} catch {return;}
        if (m.t === 'welcome') {
          you = m.you;slot = m.slot;offset = m.now - Date.now();players.clear();for (const p of m.players) players.set(p.id, p);
          // A name changed while the hello was on its way is sent again.
          const me = players.get(you);if (me && me.name !== name) {me.name = name;sock.send(JSON.stringify({t: 'name', name}));}
          setStatus('online');
        }
        else if (m.t === 'join') players.set(m.id, {id: m.id, slot: m.slot, name: m.name, mic: m.mic});
        else if (m.t === 'leave') players.delete(m.id);
        else if (m.t === 'name') {const p = players.get(m.id);if (p) p.name = m.name;}
        else if (m.t === 'mic') {const p = players.get(m.id);if (p) p.mic = m.on;}
        else if (m.t === 'full') {setStatus('full');}
        else if (m.t === 'pong') offset = m.now - (Date.now() + (m.c || Date.now())) / 2;
        onMessage(m);if (m.t === 'join' || m.t === 'leave' || m.t === 'name') onStatus && onStatus();
      };
      sock.onclose = ev => {
        if (ws !== sock) return;ws = null;you = null;players.clear();onMessage({t: 'reset'});
        if (ev.code === 4001 || status === 'full') {onMessage({t: 'full', code});if (want !== ownCode) {want = ownCode;open();return;}}
        setStatus('offline');later();
      };
      sock.onerror = () => {};
    }
    function later() {clearTimeout(timer);if (!alive) return;timer = setTimeout(open, Math.min(30000, 1500 * 2 ** retry++));}
    function shut() {clearTimeout(timer);if (ws) {const s = ws;ws = null;try {s.close(1000);} catch {}}you = null;players.clear();}
    function send(m) {if (ws && ws.readyState === 1 && you) {ws.send(JSON.stringify(m));return true;}return false;}
    // Keep-alive and clock sync (also keeps idle proxies from dropping the socket).
    setInterval(() => send({t: 'ping', c: Date.now()}), 20000);
    return {
      connect, send, home: () => connect(ownCode),
      setName(n) {n = String(n || '').trim().slice(0, 24);if (!n) return;name = n;store.set('haarklous-name-v1', n);send({t: 'name', name: n});},
      get name() {return name;}, get status() {return status;}, get code() {return code;}, get ownCode() {return ownCode;}, get you() {return you;}, get slot() {return slot;},
      get online() {return status === 'online' && you != null;}, get others() {return Math.max(0, players.size - 1);},
      players, serverNow: () => Date.now() + offset, base, validCode
    };
  }
  return {create, ALPHABET};
});
