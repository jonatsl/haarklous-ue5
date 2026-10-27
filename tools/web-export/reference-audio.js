/* Runs inside haarklous-game.html (node tools/web-export/export.mjs --only=tests --script=reference-audio.js).
 * Reference renders of the web game's own sound code for the C++ audio tests (Private/Tests/HKAudioTests.cpp),
 * written to unreal/Haarklous/Content/Data/tests/audio.json:
 *  - cases: GameAudio.create() (game-audio.js) and case-game.js sound() rendered offline: window.AudioContext is
 *    replaced by an OfflineAudioContext (behind a Proxy that reports state 'running'), setInterval/setTimeout are
 *    captured and run at the same render quanta through suspend(), and Math.random is a seeded mulberry32 during every
 *    call into the audio code. The C++ port uses the same generator in the same order, so both render the same noise,
 *    birds and cars. Each case lists its events ([block, op, ...args]) so the C++ test replays exactly the same.
 *  - compressor: the browser's DynamicsCompressorNode with the music's settings on a known test signal.
 *  - music: regions of each track rendered with the real compressor (music-render-lib.js, same seed/hash/snapping as
 *    the WAV loops) at two volumes, to check the Unreal player (WAV + its compressor) end to end.
 * Only features are stored: RMS per 20 ms, peak, octave-band energy, spectral centroid, left/right balance.
 */
(async () => {
  'use strict';
  const files = window.__hkFiles, log = window.__hkLog, RATE = 48000, Q = 128, WIN = 960, NFFT = 2048;
  const BAND_EDGES = [0, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000, 24001];
  function mulberry32(a) {return function () {a |= 0;a = a + 0x6D2B79F5 | 0;let t = Math.imul(a ^ a >>> 15, 1 | a);t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t;return ((t ^ t >>> 14) >>> 0) / 4294967296;};}
  const load = url => {const x = new XMLHttpRequest();x.open('GET', url, false);x.overrideMimeType('text/plain');x.send();if (!x.responseText) throw new Error('cannot read ' + url);return x.responseText;};
  try {
    // ---------- features (the C++ test computes exactly the same) ----------
    const cos = new Float64Array(NFFT / 2), sin = new Float64Array(NFFT / 2), hann = new Float64Array(NFFT);
    for (let k = 0; k < NFFT / 2; k++) {cos[k] = Math.cos(2 * Math.PI * k / NFFT);sin[k] = Math.sin(2 * Math.PI * k / NFFT);}
    for (let i = 0; i < NFFT; i++) hann[i] = .5 - .5 * Math.cos(2 * Math.PI * i / NFFT);
    function fft(re, im) {
      const n = re.length;
      for (let i = 1, j = 0; i < n; i++) {let bit = n >> 1;for (; j & bit; bit >>= 1) j ^= bit;j ^= bit;if (i < j) {[re[i], re[j]] = [re[j], re[i]];[im[i], im[j]] = [im[j], im[i]];}}
      for (let len = 2; len <= n; len <<= 1) {
        const half = len >> 1, step = n / len;
        for (let i = 0; i < n; i += len) for (let k = 0; k < half; k++) {
          const wr = cos[k * step], wi = -sin[k * step], a = i + k, b = a + half;
          const xr = re[b] * wr - im[b] * wi, xi = re[b] * wi + im[b] * wr;
          re[b] = re[a] - xr;im[b] = im[a] - xi;re[a] += xr;im[a] += xi;
        }
      }
    }
    const r2 = v => Math.round(v * 100) / 100;
    function features(L, R) {
      const n = L.length, nw = Math.floor(n / WIN), rms = [];
      let peak = 0, eL = 0, eR = 0;
      for (let w = 0; w < nw; w++) {let e = 0;for (let i = w * WIN; i < (w + 1) * WIN; i++) e += L[i] * L[i] + R[i] * R[i];rms.push(r2(Math.max(-120, 10 * Math.log10(e / (2 * WIN) + 1e-30))));}
      for (let i = 0; i < n; i++) {peak = Math.max(peak, Math.abs(L[i]), Math.abs(R[i]));eL += L[i] * L[i];eR += R[i] * R[i];}
      const starts = [], energies = [];
      for (let s = 0; s + NFFT <= n; s += NFFT / 2) {let e = 0;for (let i = 0; i < NFFT; i++) {const m = .5 * (L[s + i] + R[s + i]) * hann[i];e += m * m;}starts.push(s);energies.push(e);}
      const maxE = Math.max(0, ...energies), P = new Float64Array(NFFT / 2 + 1), re = new Float64Array(NFFT), im = new Float64Array(NFFT);
      starts.forEach((s, f) => {
        if (energies[f] < maxE * 1e-4 || energies[f] <= 0) return;
        for (let i = 0; i < NFFT; i++) {re[i] = .5 * (L[s + i] + R[s + i]) * hann[i];im[i] = 0;}
        fft(re, im);
        for (let k = 0; k <= NFFT / 2; k++) P[k] += re[k] * re[k] + im[k] * im[k];
      });
      const bands = new Float64Array(BAND_EDGES.length - 1);let total = 0, fp = 0;
      for (let k = 0; k <= NFFT / 2; k++) {const f = k * RATE / NFFT;let b = 0;while (b < bands.length - 1 && f >= BAND_EDGES[b + 1]) b++;bands[b] += P[k];total += P[k];fp += f * P[k];}
      return {rms, peak: Math.round(peak * 1e6) / 1e6, bands: [...bands].map(v => r2(10 * Math.log10(v / (total || 1) + 1e-12))), centroid: Math.round(total ? fp / total : 0),
        balance: r2(10 * Math.log10((eL + 1e-30) / (eR + 1e-30))), level: r2(10 * Math.log10((eL + eR) / (2 * n) + 1e-30))};
    }

    // ---------- the cases ----------
    const B = s => Math.round(s * RATE / Q);
    const STAIR = {indoor: 1, green: .2, traffic: 999, height: 0, stair: true, kitchen: false};
    const QUIET = {indoor: 0, green: 0, traffic: 999, height: 0, stair: false, kitchen: false};
    const cases = [];
    const add = (name, seconds, events, ticks = false) => cases.push({name, seconds, ticks, seed: 1000 + cases.length * 17, events});
    const SFX = ['doorOpen', 'doorClose', 'locked', 'carDoor', 'bottleOpen', 'gulp', 'shutter', 'beep', 'throw', 'chains', 'thud', 'boule', 'gravel', 'click', 'scrape', 'dump', 'hit', 'cheer', 'starter', 'crash', 'arm'];
    for (const n of SFX) add('sfx-' + n, n === 'crash' ? 2.4 : n === 'gulp' ? 1.6 : 1.2, [[0, 'resume'], [0, 'sfx', n, {}]]);
    add('sfx-doorOpen-gain', 1, [[0, 'resume'], [0, 'sfx', 'doorOpen', {gain: .4}]]);
    add('sfx-doorClose-reverb-stair', 3.5, [[0, 'resume'], [0, 'ambience', STAIR], [B(1.2), 'sfx', 'doorClose', {reverb: .6}]], true);
    const SURF = ['grass', 'gravel', 'sand', 'asphalt', 'wood', 'tile', 'stone', 'metal', 'terrain'];
    for (const s of SURF) add('step-' + s, .5, [[0, 'resume'], [0, 'footstep', s, false, 1]]);
    add('step-grass-soft', .5, [[0, 'resume'], [0, 'footstep', 'grass', false, .4]]);
    add('step-unknown-surface', .5, [[0, 'resume'], [0, 'footstep', 'carpet', false, 1]]);
    add('step-stone-stair', 3.5, [[0, 'resume'], [0, 'ambience', STAIR], [B(1.2), 'footstep', 'stone', true, 1], [B(1.7), 'footstep', 'stone', true, 1]], true);
    add('step-wood-stair', 3.5, [[0, 'resume'], [0, 'ambience', STAIR], [B(1.2), 'footstep', 'wood', true, 1], [B(1.7), 'footstep', 'wood', true, .6]], true);
    add('steps-gravel-walk', 3, [[0, 'resume'], ...[0, 1, 2, 3, 4, 5].map(i => [B(.2 + i * .45), 'footstep', 'gravel', false, 1])]);
    const eng = (kind, a, b, secs = 1.5) => add(`engine-${kind}-${a}-${b}`, secs, [[0, 'resume'], [0, 'engine', 0, kind], [0, 'set', 0, a, b]]);
    eng('bike', 1300, 0);eng('bike', 6000, .5);eng('bike', 11000, 1);
    eng('car', 800, 0);eng('car', 3000, .5);eng('car', 6000, 1);
    eng('excavator', 1000, 0);eng('excavator', 2000, 1);
    eng('plane', 700, 0);eng('plane', 2700, 1);
    eng('heli', .3, .2, 2);eng('heli', 1, .8, 2);
    eng('drone', .2, 1);eng('drone', .9, .5);
    add('engine-excavator-hydraulics', 1.5, [[0, 'resume'], [0, 'engine', 0, 'excavator'], [0, 'set', 0, 1600, .6], [0, 'hyd', 0, .6]]);
    add('engine-bike-stop', 2.5, [[0, 'resume'], [0, 'engine', 0, 'bike'], [0, 'set', 0, 6000, .5], [B(1), 'stop', 0]]);
    add('engine-heli-stop', 4, [[0, 'resume'], [0, 'engine', 0, 'heli'], [0, 'set', 0, 1, .8], [B(1), 'stop', 0]]);
    add('engine-drone-stop', 2, [[0, 'resume'], [0, 'engine', 0, 'drone'], [0, 'set', 0, .7, 1], [B(1), 'stop', 0]]);
    {
      // A car accelerating: set() every 6 render quanta (16 ms, like a frame), 800 -> 6000 rpm in 2 s.
      const ev = [[0, 'resume'], [0, 'engine', 0, 'car']];
      for (let b = 0; b <= B(2); b += 6) ev.push([b, 'set', 0, Math.round(800 + 5200 * b / B(2)), 1]);
      add('engine-car-sweep', 2.5, ev);
    }
    const amb = (name, state) => add('amb-' + name, 8, [[0, 'resume'], [0, 'ambience', state]], true);
    amb('park', {indoor: 0, green: 1, traffic: 300, height: 0, stair: false, kitchen: false});
    amb('street', {indoor: 0, green: .2, traffic: 5, height: 0, stair: false, kitchen: false});
    amb('high', {indoor: 0, green: .3, traffic: 40, height: 30, stair: false, kitchen: false});
    amb('kitchen', {indoor: 1, green: .5, traffic: 30, height: 10, stair: false, kitchen: true});
    amb('stair', {indoor: 1, green: .2, traffic: 25, height: 3, stair: true, kitchen: false});
    amb('in-car', {indoor: .7, green: .4, traffic: 8, height: 0, stair: false, kitchen: false});
    amb('default-state', null);
    for (const [level, near] of [[1, 1], [3, 1], [6, .5], [6, 1]]) {
      const ev = [[0, 'resume'], [0, 'ambience', QUIET]];
      for (let b = 0; b < B(4); b += 94) ev.push([b, 'fire', level, near]);
      add(`fire-${level}-${near}`, 4, ev, true);
    }
    add('case-shot', 1, [[0, 'resume'], [0, 'case', 'shot', 1]]);
    add('case-click', .5, [[0, 'resume'], [0, 'case', 'click', 1]]);
    add('case-enemy-2m', 1, [[0, 'resume'], [0, 'case', 'enemy', 2]]);
    add('case-enemy-12m', 1, [[0, 'resume'], [0, 'case', 'enemy', 12]]);

    // case-game.js sound(): the function's own source, bound to a given context.
    const caseSrc = load('case-game.js'), from = caseSrc.indexOf('function sound(kind, distance = 1) {');
    if (from < 0) throw new Error('case-game.js: sound() not found');
    let depth = 0, end = from;
    for (let i = caseSrc.indexOf('{', from); i < caseSrc.length; i++) {if (caseSrc[i] === '{') depth++;else if (caseSrc[i] === '}' && --depth === 0) {end = i + 1;break;}}
    const makeCaseSound = new Function('ctx', `let audio = ctx; ${caseSrc.slice(from, end)}; return sound;`);

    async function render(c) {
      const frames = Math.round(c.seconds * RATE / Q) * Q, offline = new OfflineAudioContext(2, frames, RATE);
      const proxy = new Proxy(offline, {get(t, k) {if (k === 'state') return 'running';const v = t[k];return typeof v === 'function' ? v.bind(t) : v;}});
      const rnd = mulberry32(c.seed), plan = new Map();let interval = null;
      const withSeed = fn => {
        const r = Math.random, si = window.setInterval, st = window.setTimeout, AC = window.AudioContext;
        Math.random = rnd;window.AudioContext = function () {return proxy;};
        window.setInterval = f => {interval = f;return 1;};
        window.setTimeout = (f, ms) => {at(Math.ceil((offline.currentTime + ms / 1000) * RATE / Q - 1e-6)).timeouts.push(f);return 1;};
        try {return fn();} finally {Math.random = r;window.setInterval = si;window.setTimeout = st;window.AudioContext = AC;}
      };
      const audio = GameAudio.create(), caseSound = makeCaseSound(proxy), engines = {};
      const doEvent = e => {
        const [, op, ...a] = e;
        if (op === 'resume') audio.resume();
        else if (op === 'ambience') {if (a[0]) audio.ambience(a[0]);}
        else if (op === 'footstep') audio.footstep(a[0], a[1], a[2]);
        else if (op === 'sfx') audio.sfx(a[0], a[1]);
        else if (op === 'engine') engines[a[0]] = audio.engine(a[1]);
        else if (op === 'set') engines[a[0]].set(a[1], a[2]);
        else if (op === 'hyd') engines[a[0]].hydraulics(a[1]);
        else if (op === 'stop') engines[a[0]].stop();
        else if (op === 'fire') audio.fire(a[0], a[1]);
        else if (op === 'case') caseSound(a[0], a[1]);
      };
      const run = b => {
        const p = plan.get(b);plan.delete(b);
        if (c.ticks && b > 0 && b % 45 === 0 && interval) withSeed(interval);
        for (const e of p.events) withSeed(() => doEvent(e));
        for (const f of p.timeouts) withSeed(f);
        if (b > 0) offline.resume();
      };
      function at(b) {
        if (!plan.has(b)) {plan.set(b, {events: [], timeouts: []});if (b > 0 && b * Q < frames) offline.suspend(b * Q / RATE).then(() => run(b));}
        return plan.get(b);
      }
      for (const e of c.events) at(e[0]).events.push(e);
      if (c.ticks) for (let b = 45; b * Q < frames; b += 45) at(b);
      if (plan.has(0)) run(0);
      const buf = await offline.startRendering();
      return features(buf.getChannelData(0), buf.getChannelData(1));
    }
    const out = {sampleRate: RATE, block: Q, window: WIN, fft: NFFT, bandEdges: BAND_EDGES, cases: []};
    const t0 = performance.now();
    for (const c of cases) out.cases.push({...c, frames: Math.round(c.seconds * RATE / Q) * Q, ...(await render(c))});
    log.push(`reference audio: ${out.cases.length} cases (${((performance.now() - t0) / 1000).toFixed(1)} s)`);

    // ---------- compressor on a known signal ----------
    {
      const levels = [.05, .5, 1, .2, .8, .02], n = 3 * RATE, c = new OfflineAudioContext(2, n, RATE), buf = c.createBuffer(2, n, RATE);
      const L = buf.getChannelData(0), R = buf.getChannelData(1);
      for (let i = 0; i < n; i++) {const a = levels[Math.floor(i / (RATE / 2))];L[i] = a * Math.sin(2 * Math.PI * 220 * i / RATE);R[i] = a * .7 * Math.sin(2 * Math.PI * 330 * i / RATE + 1);}
      const src = c.createBufferSource(), comp = c.createDynamicsCompressor();
      comp.threshold.value = -18;comp.ratio.value = 3;comp.attack.value = .01;comp.release.value = .25;
      src.buffer = buf;src.connect(comp).connect(c.destination);src.start(0);
      const o = await c.startRendering(), oL = o.getChannelData(0), samples = [];
      for (let i = 0; i < n; i += 1009) samples.push(Math.round(oL[i] * 1e7) / 1e7);
      out.compressor = {levels, seconds: 3, f: [220, 330], ...features(oL, o.getChannelData(1)), samples, sampleStep: 1009};
    }

    // ---------- music regions with the real compressor ----------
    eval(load('../../tools/web-export/music-render-lib.js'));
    out.music = [];
    for (const [name, bar, bars, volume] of [['main', 12, 3, 1], ['main', 20, 3, .5], ['discgolf', 16, 4, 1], ['petanque', 32, 9, .5], ['mission', 24, 4, 1]]) {
      const {barSec} = __hkMusic.loopInfo(name, RATE);
      const r = await __hkMusic.renderRegion(name, bar, Math.ceil(10.5 / barSec), bars, RATE, {compressor: true, volume});
      out.music.push({name, bar, bars, preBars: r.preBars, volume, ...features(r.L, r.R)});
    }
    log.push(`reference audio: compressor and ${out.music.length} music regions (${((performance.now() - t0) / 1000).toFixed(1)} s)`);
    files.audio = {json: JSON.stringify(out), bin: null};
  } catch (e) {
    log.push('reference audio FAILED: ' + (e && e.stack || e));
  }
  window.__hkDone = true;
})();
