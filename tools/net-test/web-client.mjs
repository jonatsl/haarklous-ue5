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
  '--window-size=1280,720', 'about:blank'], {stdio: 'ignore'});
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
  await send('Emulation.setDeviceMetricsOverride', {width: 1280, height: 720, deviceScaleFactor: 1, mobile: false});
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
      check('unrealDone', chatText().includes('done-unreal'));
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
  await evaluate(voiceMode ? voiceSetup : setup);
  const names = voiceMode ? ['online', 'sawUnreal', 'micOn', 'peerConnected', 'heardUnreal', 'unrealHeardWeb'] :
    ['online', 'sawUnreal', 'claim5', 'unrealState', 'unrealAvatarVisible', 'unrealChat', 'unrealDoor', 'fireShared', 'unrealDrives3', 'unrealSeatedIn3', 'vehicle3Moved'];
  let shot = false, doneSent = false;
  const tw = Date.now();
  for (;;) {
    const W = await evaluate('JSON.parse(JSON.stringify({seen: __hkTest.seen, notes: __hkTest.notes, log: __hkTest.log}))');
    // A picture of the Unreal player's avatar (with name tag and chat bubble) as the web client sees it.
    if (!voiceMode && !shot && W.seen.unrealAvatarVisible !== undefined && W.seen.unrealChat !== undefined) {
      shot = true;
      await evaluate(`(() => {const G = haarklousGame, r = [...G.mp.remotes.values()][0];if (!r) return 0;const p = G.player;p.yaw = Math.atan2(-(r.pos.x - p.x), -(r.pos.z - p.z));p.pitch = -.05;return 0;})()`);
      await sleep(1200);
      const png = await send('Page.captureScreenshot', {format: 'png'});
      fs.writeFileSync(path.join(outDir, `web-sees-unreal-${mode}.png`), Buffer.from(png.data, 'base64'));
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
  L('error: ' + e.message);result.error = e.message;
} finally {
  fs.writeFileSync(path.join(outDir, `web-client-${mode}${process.argv.includes('--voice') ? '-voice' : ''}.json`), JSON.stringify(result, null, 1));
  try {ws && ws.close();} catch {}
  proc.kill();
  await sleep(500);
  try {fs.rmSync(profile, {recursive: true, force: true});} catch {}
  L(`exit ${exitCode}`);
  process.exit(exitCode);
}
