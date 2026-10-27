// Render the web game's soundtrack into seamless loops for the Unreal version.
//   node tools/web-export/render-music.mjs [--only=main,mission] [--rate=48000]
// Runs reference/web-game/game-music.js in headless Chrome (a blank page, all network blocked) and uses its own
// GameMusic.renderOffline() through music-render-lib.js. Writes 16-bit stereo WAV files (before the compressor;
// the Unreal player applies the browser's compressor after its volume and crossfades) and music.json to
// unreal/Haarklous/Content/Data/audio.
import {spawn} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import {fileURLToPath} from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url)), root = path.resolve(here, '..', '..');
const arg = k => (process.argv.find(a => a.startsWith(`--${k}=`)) || '').slice(k.length + 3);
const rate = +(arg('rate') || 48000), only = arg('only') ? arg('only').split(',') : null;
const outDir = path.join(root, 'unreal', 'Haarklous', 'Content', 'Data', 'audio');
const chrome = ['C:/Program Files/Google/Chrome/Application/chrome.exe', 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'].find(p => fs.existsSync(p));
const port = 9533 + Math.floor(Math.random() * 200);
const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'hk-chrome-'));
const proc = spawn(chrome, ['--headless=new', `--remote-debugging-port=${port}`, `--user-data-dir=${profile}`, '--no-first-run', '--no-default-browser-check',
  '--disable-background-timer-throttling', '--disable-renderer-backgrounding', 'about:blank'], {stdio: 'ignore'});
const sleep = ms => new Promise(r => setTimeout(r, ms));
async function json(url) {for (let i = 0; i < 100; i++) {try {return await (await fetch(url)).json();} catch {await sleep(200);}}throw new Error('Chrome did not start');}
let ws, nextId = 1;const waiting = new Map();
function send(method, params = {}) {const id = nextId++;ws.send(JSON.stringify({id, method, params}));return new Promise((res, rej) => waiting.set(id, {res, rej, method}));}
async function evaluate(expression, awaitPromise = true) {
  const r = await send('Runtime.evaluate', {expression, awaitPromise, returnByValue: true, timeout: 1800000});
  if (r.exceptionDetails) throw new Error(`${expression.slice(0, 60)}: ${r.exceptionDetails.exception?.description || r.exceptionDetails.text}`);
  return r.result.value;
}
function wav(pcm, frames) {
  const h = Buffer.alloc(44);
  h.write('RIFF', 0);h.writeUInt32LE(36 + frames * 4, 4);h.write('WAVE', 8);h.write('fmt ', 12);h.writeUInt32LE(16, 16);h.writeUInt16LE(1, 20);h.writeUInt16LE(2, 22);
  h.writeUInt32LE(rate, 24);h.writeUInt32LE(rate * 4, 28);h.writeUInt16LE(4, 32);h.writeUInt16LE(16, 34);h.write('data', 36);h.writeUInt32LE(frames * 4, 40);
  return Buffer.concat([h, pcm]);
}

try {
  const targets = await json(`http://127.0.0.1:${port}/json/list`), target = targets.find(t => t.type === 'page');
  ws = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((res, rej) => {ws.onopen = res;ws.onerror = rej;});
  ws.onmessage = e => {const m = JSON.parse(e.data);if (m.id && waiting.has(m.id)) {const w = waiting.get(m.id);waiting.delete(m.id);m.error ? w.rej(new Error(`${w.method}: ${m.error.message}`)) : w.res(m.result);}};
  await send('Runtime.enable');await send('Network.enable');
  await send('Network.setBlockedURLs', {urls: ['http://*', 'https://*', 'ws://*', 'wss://*']});
  await evaluate(fs.readFileSync(path.join(root, 'reference', 'web-game', 'game-music.js'), 'utf8'));
  await evaluate(fs.readFileSync(path.join(here, 'music-render-lib.js'), 'utf8'));
  const names = (await evaluate('Object.keys(GameMusic.tracks)')).filter(n => !only || only.includes(n));
  fs.mkdirSync(outDir, {recursive: true});
  const metaPath = path.join(outDir, 'music.json');
  const meta = fs.existsSync(metaPath) ? JSON.parse(fs.readFileSync(metaPath, 'utf8')) : {};
  meta.note = 'Rendered by tools/web-export/render-music.mjs from reference/web-game/game-music.js (GameMusic.renderOffline), before the compressor, at master gain 0.8 (volume 1). Sample = int16 / 32767 * gain.';
  meta.sampleRate = rate;meta.tracks = meta.tracks || {};
  for (const name of names) {
    const t0 = Date.now();
    const info = await evaluate(`(async () => {
      const r = await __hkMusic.renderLoop(${JSON.stringify(name)}, ${rate});
      let peak = 0;for (let i = 0; i < r.loopFrames; i++) peak = Math.max(peak, Math.abs(r.L[i]), Math.abs(r.R[i]));
      const gain = peak > .999 ? peak / .999 : 1, pcm = new Int16Array(r.loopFrames * 2);
      for (let i = 0; i < r.loopFrames; i++) {pcm[2 * i] = Math.max(-32767, Math.min(32767, Math.round(r.L[i] / gain * 32767)));pcm[2 * i + 1] = Math.max(-32767, Math.min(32767, Math.round(r.R[i] / gain * 32767)));}
      // Levels of 10 s pieces (before the compressor), for the record.
      const seg = [];for (let s = 0; s < r.loopFrames; s += ${rate} * 10) {let e = 0, n = 0;for (let i = s; i < Math.min(r.loopFrames, s + ${rate} * 10); i++) {e += r.L[i] * r.L[i] + r.R[i] * r.R[i];n += 2;}seg.push(Math.round(10 * Math.log10(e / n) * 10) / 10);}
      window.__hkPcm = new Uint8Array(pcm.buffer);
      return {frames: r.loopFrames, barFrames: r.barFrames, seamMax: r.seamMax, joins: r.joins, segments: r.segments, segBars: r.segBars, preBars: r.preBars, peak, gain, segDb: seg};
    })()`);
    const parts = [];
    for (let o = 0; o < info.frames * 4; o += 3 << 20) parts.push(Buffer.from(await evaluate(`(() => {const b = __hkPcm.subarray(${o}, ${o + (3 << 20)});let s = '';for (let i = 0; i < b.length; i += 32768) s += String.fromCharCode.apply(null, b.subarray(i, i + 32768));return btoa(s);})()`), 'base64'));
    const file = `music-${name}.wav`;
    fs.writeFileSync(path.join(outDir, file), wav(Buffer.concat(parts), info.frames));
    const tr = await evaluate(`GameMusic.tracks[${JSON.stringify(name)}]`);
    meta.tracks[name] = {file, title: tr.title, seconds: tr.seconds, bars: tr.bars, bpm: tr.bpm, beats: tr.beats, frames: info.frames, gain: info.gain, peak: Math.round(info.peak * 1e4) / 1e4,
      seamMax: info.seamMax, segments: info.segments, segBars: info.segBars, preBars: info.preBars, segmentDbBeforeCompressor: info.segDb};
    if (process.argv.includes('--debug')) console.log(JSON.stringify(info.joins));
    console.log(`${name}: ${info.frames} frames (${(info.frames / rate).toFixed(2)} s), ${info.segments} segments, peak ${info.peak.toFixed(3)}, seam difference ${info.seamMax.toExponential(2)} (${((Date.now() - t0) / 1000).toFixed(1)} s)`);
  }
  fs.writeFileSync(metaPath, JSON.stringify(meta, null, 1));
} finally {
  try {ws && ws.close();} catch {}
  proc.kill();
  await sleep(500);
  try {fs.rmSync(profile, {recursive: true, force: true});} catch {}
}
