// Export the web game's geometry, materials and gameplay data for the Unreal version.
//   node tools/web-export/export.mjs [--only=geometry|data|tests]
// Runs reference/web-game/haarklous-game.html in headless Chrome (network blocked: it never talks to any server),
// injects hooks.js before the page and exporter.js / game-data.js after it has loaded, and writes:
//   tools/out/web-export/*.json + *.bin + textures/*.png   (geometry for Blender)
//   unreal/Haarklous/Content/Data/*.json|bin                (gameplay data loaded by the C++ code)
import {spawn} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import {fileURLToPath, pathToFileURL} from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url)), root = path.resolve(here, '..', '..');
const only = (process.argv.find(a => a.startsWith('--only=')) || '').slice(7);
const geoOut = path.join(root, 'tools', 'out', 'web-export'), dataOut = path.join(root, 'unreal', 'Haarklous', 'Content', 'Data');
const chrome = ['C:/Program Files/Google/Chrome/Application/chrome.exe', 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'].find(p => fs.existsSync(p));
const port = 9333 + Math.floor(Math.random() * 200);
const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'hk-chrome-'));
const page = pathToFileURL(path.join(root, 'reference', 'web-game', 'haarklous-game.html')).href + '?server=ws://127.0.0.1:9';

const proc = spawn(chrome, ['--headless=new', `--remote-debugging-port=${port}`, `--user-data-dir=${profile}`, '--no-first-run', '--no-default-browser-check',
  '--enable-unsafe-swiftshader', '--use-angle=swiftshader', '--disable-background-timer-throttling', '--disable-renderer-backgrounding', '--allow-file-access-from-files', 'about:blank'], {stdio: 'ignore'});
const sleep = ms => new Promise(r => setTimeout(r, ms));
async function json(url) {for (let i = 0; i < 100; i++) {try {return await (await fetch(url)).json();} catch {await sleep(200);}}throw new Error('Chrome did not start');}

let ws, nextId = 1;const waiting = new Map();
function send(method, params = {}) {const id = nextId++;ws.send(JSON.stringify({id, method, params}));return new Promise((res, rej) => waiting.set(id, {res, rej, method}));}
async function evaluate(expression, awaitPromise = true) {
  const r = await send('Runtime.evaluate', {expression, awaitPromise, returnByValue: true, timeout: 600000});
  if (r.exceptionDetails) throw new Error(`${expression.slice(0, 60)}: ${r.exceptionDetails.exception?.description || r.exceptionDetails.text}`);
  return r.result.value;
}
async function pull(name, kind) {
  // Strings in 4 MB slices; binary as base64 slices of 3 MB.
  if (kind === 'json') {
    const n = await evaluate(`__hkFiles[${JSON.stringify(name)}].json.length`);let s = '';
    for (let o = 0; o < n; o += 4 << 20) s += await evaluate(`__hkFiles[${JSON.stringify(name)}].json.slice(${o}, ${o + (4 << 20)})`);
    return s;
  }
  const n = await evaluate(`__hkFiles[${JSON.stringify(name)}].bin ? __hkFiles[${JSON.stringify(name)}].bin.length : -1`);if (n < 0) return null;
  const parts = [];
  for (let o = 0; o < n; o += 3 << 20) {
    const b64 = await evaluate(`(() => {const b = __hkFiles[${JSON.stringify(name)}].bin.subarray(${o}, ${o + (3 << 20)});let s = '';for (let i = 0; i < b.length; i += 32768) s += String.fromCharCode.apply(null, b.subarray(i, i + 32768));return btoa(s);})()`);
    parts.push(Buffer.from(b64, 'base64'));
  }
  return Buffer.concat(parts);
}

try {
  const version = await json(`http://127.0.0.1:${port}/json/version`);
  const targets = await json(`http://127.0.0.1:${port}/json/list`), target = targets.find(t => t.type === 'page');
  ws = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((res, rej) => {ws.onopen = res;ws.onerror = rej;});
  ws.onmessage = e => {const m = JSON.parse(e.data);if (m.id && waiting.has(m.id)) {const w = waiting.get(m.id);waiting.delete(m.id);m.error ? w.rej(new Error(`${w.method}: ${m.error.message}`)) : w.res(m.result);} else if (m.method === 'Runtime.consoleAPICalled' && m.params.type === 'error') console.log('  page error:', m.params.args.map(a => a.value ?? a.description).join(' ').slice(0, 300));};
  console.log('Chrome', version.Browser);
  await send('Runtime.enable');await send('Page.enable');await send('Network.enable');
  // Second safety layer: block every http(s)/ws(s) request at the browser level.
  await send('Network.setBlockedURLs', {urls: ['http://*', 'https://*', 'ws://*', 'wss://*']});
  await send('Page.addScriptToEvaluateOnNewDocument', {source: fs.readFileSync(path.join(here, 'hooks.js'), 'utf8')});
  await send('Emulation.setDeviceMetricsOverride', {width: 1280, height: 720, deviceScaleFactor: 1, mobile: false});
  await send('Page.navigate', {url: page});
  const t0 = Date.now();
  while (!(await evaluate('!!(window.haarklousGame && window.haarklousGame.walkWorld)').catch(() => false))) {if (Date.now() - t0 > 240000) throw new Error('The game did not load');await sleep(500);}
  console.log(`Game loaded in ${((Date.now() - t0) / 1000).toFixed(1)} s`);
  const jobs = [];
  if (!only || only === 'geometry') jobs.push(['exporter.js', geoOut]);
  if (!only || only === 'data') jobs.push(['game-data.js', dataOut]);
  if (!only || only === 'tests') jobs.push(['reference-tests.js', path.join(dataOut, 'tests')]);
  for (const [script, outDir] of jobs) {
    if (!fs.existsSync(path.join(here, script))) {console.log(`(skipping ${script}: not written yet)`);continue;}
    await evaluate('window.__hkFiles = {}; window.__hkLog = []; window.__hkDone = false; 0');
    const t1 = Date.now();
    await evaluate(fs.readFileSync(path.join(here, script), 'utf8'));
    while (!(await evaluate('!!window.__hkDone'))) await sleep(300);
    for (const line of await evaluate('__hkLog')) console.log('  ' + line);
    fs.mkdirSync(outDir, {recursive: true});
    const names = await evaluate('Object.keys(__hkFiles)');
    for (const name of names) {
      const text = await pull(name, 'json');
      if (name.startsWith('tex_')) {
        fs.mkdirSync(path.join(outDir, 'textures'), {recursive: true});
        fs.writeFileSync(path.join(outDir, 'textures', name.slice(4) + '.png'), Buffer.from(text.replace(/^data:image\/png;base64,/, ''), 'base64'));
        continue;
      }
      const isBinOnly = text === '';
      if (!isBinOnly) fs.writeFileSync(path.join(outDir, name + '.json'), text);
      const bin = await pull(name, 'bin');
      if (bin) fs.writeFileSync(path.join(outDir, name + '.bin'), bin);
    }
    console.log(`${script}: ${names.length} files -> ${path.relative(root, outDir)} (${((Date.now() - t1) / 1000).toFixed(1)} s)`);
  }
} finally {
  try {ws && ws.close();} catch {}
  proc.kill();
  await sleep(500);
  try {fs.rmSync(profile, {recursive: true, force: true});} catch {}
}
