// A real web client for the live online tests: the web game (served by the test server) in headless Chrome, with a
// fake microphone, playing in one lobby with the Unreal client (Haarklous.Net.Live, see run-live.ps1).
//   node tools/net-test/web-client.mjs --page=<url> --lobby=CODE --allow=<host> --out=<folder> [--mode=local|deployed] [--timeout=300]
// Safety: Chrome resolves no host name except --allow (--host-resolver-rules), and the page may only open WebSockets
// to that host. The live game server can never be reached.
import {spawn} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';

const arg = (k, d) => (process.argv.find(a => a.startsWith(`--${k}=`)) || '').slice(k.length + 3) || d;
const page = arg('page'), lobby = arg('lobby'), allow = arg('allow', '127.0.0.1'), outDir = arg('out', '.'), mode = arg('mode', 'local'), timeoutS = +arg('timeout', '300');
if (!page || !lobby) {console.error('need --page and --lobby');process.exit(2);}
if (/haarklous-online|haarklous-plass/i.test(page + allow)) {console.error('refusing: that is the live game');process.exit(2);}
const chrome = ['C:/Program Files/Google/Chrome/Application/chrome.exe', 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'].find(p => fs.existsSync(p));
const port = 9533 + Math.floor(Math.random() * 300);
const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'hk-web-client-'));
fs.mkdirSync(outDir, {recursive: true});
const logLines = [], t0 = Date.now();
const L = s => {const line = `${((Date.now() - t0) / 1000).toFixed(1).padStart(6)} s  ${s}`;logLines.push(line);console.log(line);};

const proc = spawn(chrome, ['--headless=new', `--remote-debugging-port=${port}`, `--user-data-dir=${profile}`, '--no-first-run', '--no-default-browser-check',
  '--enable-unsafe-swiftshader', '--use-angle=swiftshader', '--disable-background-timer-throttling', '--disable-renderer-backgrounding', '--disable-backgrounding-occluded-windows',
  `--host-resolver-rules=MAP * ~NOTFOUND, EXCLUDE ${allow}`, '--use-fake-device-for-media-stream', '--use-fake-ui-for-media-stream', '--autoplay-policy=no-user-gesture-required',
  process.argv.includes('--game') ? '--window-size=640,360' : '--window-size=1280,720', 'about:blank'], {stdio: 'ignore'});
const sleep = ms => new Promise(r => setTimeout(r, ms));
async function json(url) {for (let i = 0; i < 100; i++) {try {return await (await fetch(url)).json();} catch {await sleep(200);}}throw new Error('Chrome did not start');}
let ws, nextId = 1;const waiting = new Map();
function send(method, params = {}) {const id = nextId++;ws.send(JSON.stringify({id, method, params}));return new Promise((res, rej) => waiting.set(id, {res, rej, method}));}
async function evaluate(expression, timeout = 600000) {
  const r = await send('Runtime.evaluate', {expression, awaitPromise: true, returnByValue: true, timeout});
  if (r.exceptionDetails) throw new Error(`${expression.slice(0, 60)}: ${r.exceptionDetails.exception?.description || r.exceptionDetails.text}`);
  return r.result.value;
}
// Runs before the page: only WebSockets to the allowed host; the player's name.
const guard = `(() => {
  const allow = ${JSON.stringify(allow)}, W = window.WebSocket;
  window.WebSocket = function (url, p) {
    const h = new URL(url).hostname;if (h !== allow) throw new Error('blocked by the test: ' + url);
    const s = p === undefined ? new W(url) : new W(url, p);
    // Tap the lobby's fire messages for the shared-fire check.
    s.addEventListener('message', e => {try {
      const m = JSON.parse(e.data);if (m.t === 'fire') (window.__hkFire = window.__hkFire || []).push(m);
      // Arrival times of other players' states and vehicle updates (rate and gaps).
      if (m.t === 'st' || m.t === 'vs') (window.__hkIn = window.__hkIn || []).push([performance.now(), m.t, m.t === 'st' ? m.id : +m.v]);
    } catch {}});
    return s;
  };
  window.WebSocket.prototype = W.prototype;Object.assign(window.WebSocket, {CONNECTING: 0, OPEN: 1, CLOSING: 2, CLOSED: 3});
  try {localStorage.setItem('haarklous-name-v1', 'Web tester');} catch {}
  // A simulated DualShock 4 (like tests/browser-harness.js), used by the --game scenario to walk and drive.
  if (${JSON.stringify(process.argv.includes('--game'))}) {
    window.fakePad = {id: 'Wireless Controller (STANDARD GAMEPAD Vendor: 054c Product: 05c4)', mapping: 'standard', connected: true, index: 0, timestamp: 0, axes: [0, 0, 0, 0],
      buttons: Array.from({length: 18}, () => ({pressed: false, touched: false, value: 0}))};
    Object.defineProperty(navigator, 'getGamepads', {value: () => [window.fakePad], configurable: true});
    // Headless Chrome runs requestAnimationFrame about once a second (the page counts as hidden); like the web's own
    // tests/browser-harness.js, frames run on a timer instead.
    window.requestAnimationFrame = cb => setTimeout(() => cb(performance.now()), 33);
  }
})();`;
const result = {mode, page, lobby, checks: {}, log: logLines};
let exitCode = 1;
try {
  await json(`http://127.0.0.1:${port}/json/version`);
  const target = (await json(`http://127.0.0.1:${port}/json/list`)).find(t => t.type === 'page');
  ws = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((res, rej) => {ws.onopen = res;ws.onerror = rej;});
  ws.onmessage = e => {const m = JSON.parse(e.data);if (m.id && waiting.has(m.id)) {const w = waiting.get(m.id);waiting.delete(m.id);m.error ? w.rej(new Error(`${w.method}: ${m.error.message}`)) : w.res(m.result);}
    else if (m.method === 'Runtime.consoleAPICalled' && (m.params.type === 'error' || m.params.type === 'warning')) L('page ' + m.params.type + ': ' + m.params.args.map(a => a.value ?? a.description).join(' ').slice(0, 200));};
  await send('Runtime.enable');await send('Page.enable');
  await send('Page.addScriptToEvaluateOnNewDocument', {source: guard});
  await send('Emulation.setDeviceMetricsOverride', {width: process.argv.includes('--game') ? 640 : 1280, height: process.argv.includes('--game') ? 360 : 720, deviceScaleFactor: 1, mobile: false});
  L(`opening ${page}`);
  await send('Page.navigate', {url: page});
  const tl = Date.now();
  while (!(await evaluate('!!(window.haarklousGame && window.haarklousGame.walkWorld)').catch(() => false))) {if (Date.now() - tl > 240000) throw new Error('The game did not load');await sleep(500);}
  L(`game loaded in ${((Date.now() - tl) / 1000).toFixed(1)} s`);
  // --delay-begin=N: join the lobby N s later (so the Unreal client is there first and gets the lower id, i.e. the
  // voice offerer role of game-voice.js).
  const delay = +arg('delay-begin', '0');
  if (delay > 0) {L(`waiting ${delay} s before joining`);await sleep(delay * 1000);}
  await evaluate('haarklousGame.begin(); 0');
  // Watchers record when each condition is first true; the test acts once it sees the Unreal player.
  const setup = `(() => {
    const G = haarklousGame, mp = G.mp, net = mp.net, t0 = performance.now(), W = window.__hkTest = {seen: {}, notes: {}, log: []};
    const L = s => W.log.push(((performance.now() - t0) / 1000).toFixed(1) + ' s  ' + s);
    const check = (k, ok, note) => {if (ok && !(k in W.seen)) {W.seen[k] = +((performance.now() - t0) / 1000).toFixed(2);if (note) W.notes[k] = note;L('ok: ' + k + ' ' + (note || ''));}};
    const chatText = () => document.getElementById('chatLog').textContent;
    let acted = false, U = null;
    W.timer = setInterval(() => {
      check('online', net.online && net.code === ${JSON.stringify(lobby)}, 'lobby ' + net.code + ', id ' + net.you + ', slot ' + net.slot);
      if (!U) {U = [...net.players.values()].find(p => p.name === 'Unreal tester') || null;if (U) check('sawUnreal', true, 'id ' + U.id + ' slot ' + U.slot);}
      if (net.online && !W.placed) {W.placed = true;G.place(-4, 30, Math.atan2(4, -2));L('placed at (-4, 30)');}
      if (U && !acted) {
        acted = true;
        mp.claim(5, 'd').then(r => {check('claim5', r.ok, JSON.stringify(r));});
        mp.sendChat('hello from web');
        const d = G.doors.find(x => x.id === 'W09-0');if (d && !d.open) G.toggleDoor(d);
        mp.addWood();L('claimed vehicle 5, sent chat, opened door W09-0, added wood');
      }
      if (!U) return;
      const r = mp.remotes.get(U.id);
      if (r && r.st) check('unrealState', r.st.p && Math.hypot(r.st.p[0] - 0, r.st.p[2] - 32) < 1, 'p = ' + JSON.stringify(r.st.p) + ' st ' + r.st.st);
      if (r) check('unrealAvatarVisible', r.seen && r.av.group.visible, 'at ' + r.av.group.position.toArray().map(v => v.toFixed(2)).join(', '));
      check('unrealChat', chatText().includes('hello from unreal'));
      const d5 = G.doors.find(x => x.id === 'W05-0');check('unrealDoor', d5 && d5.open === true, 'W05-0 open');
      // Both logs count and both clients agree: the lobby's fire messages by both players, the second one level + 1.
      const F = window.__hkFire || [];
      if (F.some(m => m.by === U.id) && F.some(m => m.by === net.you)) {
        const a = F[0], b = F[F.length - 1], expect = Math.min(6, Math.max(0, a.fire.level - (b.fire.t - a.fire.t) / 90000) + 1), nowLevel = Math.max(0, b.fire.level - (net.serverNow() - b.fire.t) / 90000);
        check('fireShared', Math.abs(b.fire.level - expect) < .01 && Math.abs(mp.fireLevel() - nowLevel) < .01, 'levels ' + F.map(m => m.fire.level.toFixed(3) + ' by ' + m.by).join(', ') + '; now ' + mp.fireLevel().toFixed(3));
      }
      const v3 = G.vehicles[3];
      check('unrealDrives3', mp.occupant(3, 'd') === U.id && mp.remoteDriven(3), 'vehicle 3 driver ' + mp.occupant(3, 'd'));
      if (r && r.st && r.st.v === 3) check('unrealSeatedIn3', Math.hypot(r.av.group.position.x - v3.x, r.av.group.position.z - v3.z) < 3, 'avatar at the car seat');
      check('vehicle3Moved', mp.remoteDriven(3) && Math.hypot(v3.x - 4, v3.z - 38) < 1.5, 'car 3 at (' + v3.x.toFixed(2) + ', ' + v3.z.toFixed(2) + ')');
      check('unrealDone', chatText().includes(${JSON.stringify(process.argv.includes('--game') ? 'done-game' : 'done-unreal')}));
      // --game: the real game (-hktest=online) instead of the test client.
      check('gameChat', chatText().includes('hello from the game'));
      if (r && r.st) check('gameState', r.seen && r.av.group.visible && r.st.p && Math.hypot(r.st.p[0] + 1, r.st.p[2] - 32.5) < 1.5, 'p = ' + JSON.stringify(r.st.p) + ' st ' + r.st.st + ' y ' + r.st.y);
    }, 100);
    return 0;
  })()`;
  // Voice test: the fake microphone on, then listen to the Unreal client's channel (a 440 Hz tone).
  const voiceSetup = `(() => {
    const G = haarklousGame, mp = G.mp, net = mp.net, t0 = performance.now(), W = window.__hkTest = {seen: {}, notes: {}, log: [], max: 0};
    const L = s => W.log.push(((performance.now() - t0) / 1000).toFixed(1) + ' s  ' + s);
    const check = (k, ok, note) => {if (ok && !(k in W.seen)) {W.seen[k] = +((performance.now() - t0) / 1000).toFixed(2);if (note) W.notes[k] = note;L('ok: ' + k + ' ' + (note || ''));}};
    const chatText = () => document.getElementById('chatLog').textContent;
    let U = null, asked = false, meter = null, said = false;
    const ac = new AudioContext();
    W.timer = setInterval(async () => {
      check('online', net.online && net.code === ${JSON.stringify(lobby)}, 'id ' + net.you);
      if (!U) {U = [...net.players.values()].find(p => p.name === 'Unreal tester') || null;if (U) check('sawUnreal', true, 'id ' + U.id);}
      if (!U) return;
      if (!asked) {asked = true;mp.setMic(true).then(ok => {check('micOn', ok, 'fake microphone');if (!ok) L('mic failed');});}
      const p = mp.voice.peers.get(U.id);if (!p) return;
      check('peerConnected', p.pc.connectionState === 'connected', p.pc.connectionState + (p.offerer ? ' (we offered)' : ' (Unreal offered)'));
      const s = p.audio.srcObject;
      if (s && (!meter || meter.s !== s)) {const an = ac.createAnalyser();an.fftSize = 2048;ac.createMediaStreamSource(s).connect(an);meter = {s, an, buf: new Float32Array(2048)};}
      if (meter) {meter.an.getFloatTimeDomainData(meter.buf);let e = 0;for (const v of meter.buf) e += v * v;const rms = Math.sqrt(e / meter.buf.length);W.max = Math.max(W.max, rms);
        // The tone's pitch: count zero crossings over the 2048 samples.
        let z = 0;for (let i = 1; i < meter.buf.length; i++) if ((meter.buf[i - 1] < 0) !== (meter.buf[i] < 0)) z++;
        const hz = z / 2 / (meter.buf.length / ac.sampleRate);
        check('heardUnreal', rms > .05, 'RMS ' + rms.toFixed(3) + ', about ' + hz.toFixed(0) + ' Hz, playing ' + !p.audio.paused);
        if (W.seen.heardUnreal !== undefined && !said) {said = true;mp.sendChat('voice-heard-unreal ' + W.max.toFixed(3));}
      }
      check('unrealHeardWeb', chatText().includes('voice-heard-web'), (chatText().match(/voice-heard-web [0-9.]+/) || [''])[0]);
      check('unrealDone', chatText().includes('done-unreal-voice'));
    }, 100);
    return 0;
  })()`;
  const voiceMode = process.argv.includes('--voice');
  // --game: the real game (-hktest=online, HKOnlineTestScript.cpp). Both sides run the same script and hand over
  // with chat lines (A chat/doors/fire/walking, B web drives + game rides along, C game drives + web pillion, D voice).
  const gameScenario = `(async () => {
    const G = haarklousGame, mp = G.mp, net = mp.net, P = G.player, t0 = performance.now(), R = window.__hkTest = {seen: {}, notes: {}, log: [], fail: []};
    const L = s => R.log.push(((performance.now() - t0) / 1000).toFixed(1) + ' s  ' + s);
    const ok = (k, cond, note) => {if (cond) {R.seen[k] = +((performance.now() - t0) / 1000).toFixed(2);if (note) R.notes[k] = note;L('ok: ' + k + ' ' + (note || ''));} else {R.fail.push(k);L('FAIL: ' + k + ' ' + (note || ''));}return !!cond;};
    const wait = ms => new Promise(r => setTimeout(r, ms));
    const until = async (fn, ms) => {const t = performance.now();while (performance.now() - t < ms) {try {const v = fn();if (v) return v;} catch {}await wait(100);}return null;};
    const chat = () => document.getElementById('chatLog').textContent;
    const heard = (s, ms = 120000) => until(() => chat().includes(s), ms);
    const shot = async name => {window.__hkShot = name;await until(() => !window.__hkShot, 30000);};
    const pad = window.fakePad, hold = (i, v) => {pad.buttons[i] = {pressed: v > .5, touched: v > 0, value: v};}, press = async i => {hold(i, 1);await wait(250);hold(i, 0);await wait(400);};
    const dist = (a, b) => Math.hypot(a.x - b.x, a.z - b.z);
    const face = p => {P.yaw = Math.atan2(-(p.x - P.x), -(p.z - P.z));P.pitch = -.05;};
    const anchor = (v, seat) => {const a = v.hipAnchor(seat);a.obj.updateWorldMatrix(true, false);return a.obj.localToWorld(a.pos.clone());};
    const stopCar = async v => {hold(7, 0);hold(6, 1);await until(() => Math.abs(v.speed) < .2, 40000);hold(6, 0);await wait(300);};
    try {
      // Lighter rendering for the software renderer (the pause menu's graphics setting: Fast, no shadows).
      {const q = document.getElementById('quality');if (q) {q.value = '0';q.dispatchEvent(new Event('change'));}}
      if (!ok('online', await until(() => net.online && net.code === ${JSON.stringify(lobby)}, 60000), 'id ' + net.you)) return R;
      const U = await until(() => [...net.players.values()].find(p => p.name === 'Unreal tester'), ${timeoutS * 1000});
      if (!ok('sawGame', !!U, U && 'id ' + U.id + ' slot ' + U.slot)) return R;
      const rem = () => mp.remotes.get(U.id);
      // ---- A: chat both ways, a door and a log from each side, both walk ----
      G.place(0.5, 34, Math.atan2(1.5, -1.5));
      mp.sendChat('hello from web');
      const d9 = G.doors.find(d => d.id === 'W09-0');if (d9 && !d9.open) G.toggleDoor(d9);
      G.fire.throwLog({x: P.x, y: P.feet + 1.3, z: P.z});mp.addWood();   // the game's addLog()
      ok('chatFromGame', await heard('hello from the game'));
      ok('doorFromGame', await until(() => G.doors.find(d => d.id === 'W05-0').open === true, 60000), 'W05-0 open');
      const F = await until(() => {const f = (window.__hkFire || []);return f.some(m => m.by === U.id) && f.some(m => m.by === net.you) ? f : null;}, 60000);
      if (F) {const a = F[0], b = F[F.length - 1], expect = Math.min(6, Math.max(0, a.fire.level - (b.fire.t - a.fire.t) / 90000) + 1);
        ok('fireBothLogs', Math.abs(b.fire.level - expect) < .01, 'levels ' + F.map(m => m.fire.level.toFixed(3) + ' by ' + m.by).join(', '));} else ok('fireBothLogs', false);
      mp.sendChat('fire-web ' + mp.fireLevel().toFixed(4) + ' ' + Math.round(net.serverNow()));
      ok('gameVisible', await until(() => rem() && rem().seen && rem().av.group.visible, 30000));
      // The web player walks (left stick forward) toward the street, then watches the game player walk.
      // (Next to the game the headless page renders slowly and its frame time is capped at 50 ms, so the web player
      // walks and drives by distance, not by time.)
      mp.sendChat('web-walk-start');
      {
        // Walk forward; if something blocks the way for a while, turn a quarter and go on.
        const w0 = {x: P.x, z: P.z};let yaw = Math.atan2(-(6 - P.x), -(41 - P.z)), last = {x: P.x, z: P.z}, tl = performance.now();
        P.yaw = yaw;pad.axes = [0, -1, 0, 0];
        await until(() => {
          if (performance.now() - tl > 6000) {if (dist(P, last) < .5) {yaw += Math.PI / 2;L('blocked, turning');}last = {x: P.x, z: P.z};tl = performance.now();}
          P.yaw = yaw;return dist(P, w0) > 6;
        }, 120000);
        pad.axes = [0, 0, 0, 0];ok('webWalked', dist(P, w0) > 3, dist(P, w0).toFixed(1) + ' m');
      }
      mp.sendChat('web-walk-end');
      // The game player walks to and fro until we say we saw it (moved 1.5 m and a walking speed).
      let maxSpeed = 0, g0 = null, moved = 0;
      ok('gameWalks', await until(() => {
        if (!chat().includes('game-walk-start')) return false;
        const r = rem();face(r.pos);if (!g0) g0 = {x: r.pos.x, z: r.pos.z};
        maxSpeed = Math.max(maxSpeed, r.speed);moved = Math.max(moved, dist(r.pos, g0));return moved > 1.5 && r.speed > .3;
      }, 150000), 'moved ' + moved.toFixed(1) + ' m, speed up to ' + maxSpeed.toFixed(2));
      await shot('A2-web-sees-game-walking');
      mp.sendChat('web-saw-game-walking');
      await heard('game-walk-end', 60000);
      // ---- B: the web player drives a car, the game rides along ----
      const ci = 1;   // the parked car by the firepit with the most free street ahead (car 2 is 26 m further on)
      const car = G.vehicles[ci];G.tryDrive(car);
      if (!ok('webDrivesCar', await until(() => P.mode === 'vehicle' && P.vehicle === car && mp.occupant(ci, 'd') === net.you, 60000), 'vehicle ' + ci)) return R;
      {const s0 = {x: car.x, z: car.z};hold(7, 1);await until(() => dist(car, s0) > 4, 90000);await stopCar(car);ok('webDroveAlone', dist(car, s0) > 4, 'moved ' + dist(car, s0).toFixed(1) + ' m');}
      mp.sendChat('web-drives ' + ci);
      ok('gameRidesAlong', await until(() => mp.occupant(ci, 'p') === U.id && chat().includes('unreal-rides'), 120000), 'occupants d ' + mp.occupant(ci, 'd') + ' p ' + mp.occupant(ci, 'p'));
      ok('gameSeatState', await until(() => rem().st && rem().st.v === ci && rem().st.s === 'p', 15000), JSON.stringify(rem().st));
      ok('gameAvatarAtPassengerAnchor', await until(() => dist(rem().av.group.position, anchor(car, 'p')) < .05, 15000), 'avatar ' + dist(rem().av.group.position, anchor(car, 'p')).toFixed(3) + ' m from the anchor');
      // Look at the passenger, picture, then drive on with the passenger.
      {
        // (The game eases the look back to straight ahead every frame, so it is held while the picture is taken.)
        const s = car.seat(), w = anchor(car, 'p'), ly = Math.atan2(-(w.x - s.x), -(w.z - s.z)) - s.yaw;
        const keep = setInterval(() => {P.lookYaw = ly;P.lookPitch = -.15;}, 10);
        await wait(800);await shot('B2-web-drives-game-passenger');clearInterval(keep);P.lookYaw = 0;
      }
      const c0 = {x: car.x, z: car.z};hold(7, 1);await until(() => dist(car, c0) > 5, 120000);await stopCar(car);
      ok('webDroveWithPassenger', dist(car, c0) > 3, 'moved ' + dist(car, c0).toFixed(1) + ' m');
      mp.sendChat('web-drove');
      await heard('unreal-left-car', 90000);
      ok('gameGotOut', await until(() => !mp.occupant(ci, 'p'), 15000));
      G.exitVehicle();ok('webLeftCar', P.mode === 'foot');
      mp.sendChat('web-left-car');
      // ---- C: the game drives the motorcycle, the web player rides pillion ----
      const bike = G.vehicles[5];
      if (!ok('gameDrivesBike', await until(() => chat().includes('unreal-drives 5') && mp.occupant(5, 'd') === U.id && mp.remoteDriven(5), 150000))) return R;
      G.tryRide(bike);
      ok('webRidesPillion', await until(() => P.mode === 'passenger' && P.vehicle === bike && mp.occupant(5, 'p') === net.you, 60000), 'occupants d ' + mp.occupant(5, 'd') + ' p ' + mp.occupant(5, 'p'));
      mp.sendChat('web-rides');
      const b0 = {x: bike.x, z: bike.z};
      await heard('unreal-drove', 120000);
      await until(() => dist(bike, b0) > 3 && dist(P, bike) < .3, 20000);
      ok('webFollowsGameBike', dist(bike, b0) > 3 && dist(P, bike) < .3 && P.mode === 'passenger', 'bike moved ' + dist(bike, b0).toFixed(1) + ' m, web player ' + dist(P, bike).toFixed(2) + ' m from it');
      {const d = rem().av.group.position;P.lookYaw = 0;P.lookPitch = -.25;}
      await wait(500);await shot('C3-web-pillion-behind-game-rider');
      await until(() => Math.abs(bike.speed) < .3, 10000);
      for (let k = 0; k < 8 && P.mode !== 'foot'; k++) {hold(2, 1);await wait(700);hold(2, 0);await wait(700);}   // □ get off (held long enough for a slow frame)
      ok('webLeftBike', await until(() => P.mode === 'foot', 8000));
      mp.sendChat('web-left-bike');
      // ---- D: voice ----
      await heard('game-mic-on', 120000);
      ok('webMicOn', await mp.setMic(true), 'fake microphone');
      const ac = new AudioContext();let meter = null, max = 0, hz = 0;
      ok('heardGame', await until(() => {
        const p = mp.voice.peers.get(U.id);if (!p || !p.audio.srcObject) return false;
        if (!meter || meter.s !== p.audio.srcObject) {const an = ac.createAnalyser();an.fftSize = 2048;ac.createMediaStreamSource(p.audio.srcObject).connect(an);meter = {s: p.audio.srcObject, an, buf: new Float32Array(2048)};}
        meter.an.getFloatTimeDomainData(meter.buf);let e = 0, z = 0;for (let i = 0; i < meter.buf.length; i++) {e += meter.buf[i] ** 2;if (i && (meter.buf[i - 1] < 0) !== (meter.buf[i] < 0)) z++;}
        const rms = Math.sqrt(e / meter.buf.length);if (rms > max) {max = rms;hz = z / 2 / (meter.buf.length / ac.sampleRate);}
        return p.pc.connectionState === 'connected' && rms > .05;
      }, 90000), 'RMS ' + max.toFixed(3) + ', about ' + hz.toFixed(0) + ' Hz');
      mp.sendChat('voice-heard-game ' + max.toFixed(3));
      ok('gameHeardWeb', await heard('voice-heard-web', 90000));
      mp.sendChat('done-web');
      await heard('done-game', 60000);
    } catch (e) {L('error: ' + e.message);R.fail.push('exception');}
    return R;
  })()`;
  if (process.argv.includes('--game')) {
    const run = evaluate(gameScenario, 1800000);
    let finished = false;run.then(() => {finished = true;}, () => {finished = true;});
    while (!finished) {
      const name = await evaluate('window.__hkShot || ""').catch(() => '');
      if (name) {
        await sleep(1000);
        const png = await send('Page.captureScreenshot', {format: 'png'});
        fs.writeFileSync(path.join(outDir, `web-${mode}-${name}.png`), Buffer.from(png.data, 'base64'));
        L('screenshot ' + name);
        await evaluate('window.__hkShot = null; 0');
      }
      await sleep(300);
    }
    const R = await run.catch(e => ({seen: {}, notes: {}, log: ['error ' + e.message], fail: ['exception']}));
    for (const k of Object.keys(R.seen).concat(R.fail)) result.checks[k] = {ok: k in R.seen && !R.fail.includes(k), seconds: R.seen[k], note: R.notes[k]};
    result.pageLog = R.log;
    for (const line of R.log) L('page ' + line);
    result.fps = await evaluate('haarklousGame.fps').catch(() => null);
    exitCode = R.fail.length === 0 && Object.keys(R.seen).length > 0 ? 0 : 1;
    throw {done: true};
  }
  await evaluate(voiceMode ? voiceSetup : setup);
  const gameMode = process.argv.includes('--game');
  const names = voiceMode ? ['online', 'sawUnreal', 'micOn', 'peerConnected', 'heardUnreal', 'unrealHeardWeb'] :
    gameMode ? ['online', 'sawUnreal', 'claim5', 'gameState', 'gameChat'] :
    ['online', 'sawUnreal', 'claim5', 'unrealState', 'unrealAvatarVisible', 'unrealChat', 'unrealDoor', 'fireShared', 'unrealDrives3', 'unrealSeatedIn3', 'vehicle3Moved'];
  let shot = false, doneSent = false;
  const tw = Date.now();
  for (;;) {
    const W = await evaluate('JSON.parse(JSON.stringify({seen: __hkTest.seen, notes: __hkTest.notes, log: __hkTest.log}))');
    // A picture of the Unreal player's avatar (with name tag and chat bubble) as the web client sees it.
    if (!voiceMode && !shot && W.seen.unrealAvatarVisible !== undefined && (W.seen.unrealChat ?? W.seen.gameChat) !== undefined) {
      shot = true;
      await evaluate(`(() => {const G = haarklousGame, r = [...G.mp.remotes.values()][0];if (!r) return 0;const p = G.player;p.yaw = Math.atan2(-(r.pos.x - p.x), -(r.pos.z - p.z));p.pitch = -.05;return 0;})()`);
      await sleep(1200);
      const png = await send('Page.captureScreenshot', {format: 'png'});
      fs.writeFileSync(path.join(outDir, `web-sees-unreal-${mode}${gameMode ? "-game" : ""}.png`), Buffer.from(png.data, 'base64'));
      L('screenshot saved');
    }
    const all = names.every(n => W.seen[n] !== undefined);
    if (all && !doneSent) {doneSent = true;await evaluate(`haarklousGame.mp.sendChat('${voiceMode ? 'done-web-voice' : 'done-web'}'); 0`);L('all checks seen; told the Unreal client');}
    if ((doneSent && W.seen.unrealDone !== undefined) || Date.now() - tw > timeoutS * 1000) {
      for (const n of names) result.checks[n] = {ok: W.seen[n] !== undefined, seconds: W.seen[n], note: W.notes[n]};
      result.unrealDone = W.seen.unrealDone !== undefined;result.pageLog = W.log;
      if (voiceMode) result.maxLevel = await evaluate('__hkTest.max');
      result.arrivals = await evaluate(`(() => {const out = {};for (const t of ['st', 'vs']) {const a = (window.__hkIn || []).filter(x => x[1] === t).map(x => x[0]);if (a.length < 2) continue;
        const gaps = a.slice(1).map((v, i) => v - a[i]);out[t] = {count: a.length, perSecond: +((a.length - 1) / ((a[a.length - 1] - a[0]) / 1000)).toFixed(2), maxGapMs: Math.round(Math.max(...gaps)), medianGapMs: Math.round(gaps.sort((x, y) => x - y)[gaps.length >> 1])};}return out;})()`);
      if (result.arrivals) L('arrivals from Unreal: ' + JSON.stringify(result.arrivals));
      result.fps = await evaluate('haarklousGame.fps');
      exitCode = all ? 0 : 1;
      break;
    }
    await sleep(500);
  }
  await sleep(2000);
} catch (e) {
  if (!(e && e.done)) {L('error: ' + e.message);result.error = e.message;}
} finally {
  fs.writeFileSync(path.join(outDir, `web-client-${mode}${process.argv.includes('--voice') ? '-voice' : process.argv.includes('--game') ? '-game' : ''}.json`), JSON.stringify(result, null, 1));
  try {ws && ws.close();} catch {}
  proc.kill();
  await sleep(500);
  try {fs.rmSync(profile, {recursive: true, force: true});} catch {}
  L(`exit ${exitCode}`);
  process.exit(exitCode);
}
