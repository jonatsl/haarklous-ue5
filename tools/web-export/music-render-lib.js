/* Shared by render-music.mjs and reference-audio.js (runs in a page that has window.GameMusic from
 * reference/web-game/game-music.js). Renders the web game's soundtrack with its own GameMusic.renderOffline() into
 * seamless loops for the Unreal version:
 *  - long tracks are rendered in ~10 s segments (the JS notes: offline cost grows with queued notes), each with
 *    >= 10.5 s of pre-roll so release and reverb tails of earlier bars are present, also across the loop seam;
 *  - Math.random is seeded the same for every segment, so the noise buffer and the reverb impulse are identical;
 *  - the noise offsets of hats/brushes/claps (Math.random() * 1.5 in kit.noise) are replaced by a hash of the
 *    note's position in the loop, so a note sounds the same in every segment (still white noise, same range) and
 *    segments join sample-exactly; a 2048-frame crossfade at each join is only a safety net;
 *  - every scheduled time is snapped to its sample frame (Chrome rounds start frames up, so the floating-point error
 *    of t += barLen would otherwise move some notes by one frame (21 us) depending on where a render started);
 *  - every render starts on the same 128-frame render-quantum grid of the loop (more pre-roll if needed): Chrome starts
 *    an oscillator whose frequency is automated with a phase that depends on where its start falls in a quantum;
 *  - the compressor can be bypassed: the Unreal player applies the browser's DynamicsCompressor itself after its
 *    volume and crossfades (HKSynth.h FHKSynthCompressor), like the web chain master -> compressor -> speakers.
 * window.__hkMusic = {renderRegion(name, bar, preBars, bars, rate, opts), renderLoop(name, rate, opts), loopInfo(name, rate)}
 */
(() => {
  'use strict';
  const GM = window.GameMusic, SEED = 20261027;
  function mulberry32(a) {return function () {a |= 0;a = a + 0x6D2B79F5 | 0;let t = Math.imul(a ^ a >>> 15, 1 | a);t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t;return ((t ^ t >>> 14) >>> 0) / 4294967296;};}
  const hash01 = n => mulberry32(Math.imul(n, 0x9E3779B1) >>> 0)();
  function loopInfo(name, rate) {
    const info = GM.tracks[name], barSec = info.beats * 60 / info.bpm, barFrames = Math.round(barSec * rate);
    return {info, barSec, barFrames, loopFrames: info.bars * barFrames};
  }
  // Loop region [bar, bar + bars) (plus preTail frames before it) rendered with at least preBars bars of pre-roll.
  async function renderRegion(name, bar, minPreBars, bars, rate, {compressor = false, volume = 1, preTail = 0} = {}) {
    const {info, barSec, barFrames, loopFrames} = loopInfo(name, rate);
    let preBars = minPreBars;
    while (((((bar - preBars) % info.bars + info.bars) % info.bars) * barFrames) % 128) preBars++;
    const startBar = ((bar - preBars) % info.bars + info.bars) % info.bars, seconds = .05 + (preBars + bars) * barSec;
    const startLoopFrame = startBar * barFrames, lead = Math.round(.05 * rate), salt = name.length * 7919;
    const origRandom = Math.random, origStart = AudioBufferSourceNode.prototype.start, origComp = BaseAudioContext.prototype.createDynamicsCompressor, origConnect = AudioNode.prototype.connect;
    Math.random = mulberry32(SEED);
    AudioBufferSourceNode.prototype.start = function (when = 0, offset, duration) {
      if (offset === undefined) return origStart.call(this, when);
      const lf = ((startLoopFrame + Math.round((when - .05) * rate)) % loopFrames + loopFrames) % loopFrames, off = 1.5 * hash01(lf + salt);
      return duration === undefined ? origStart.call(this, when, off) : origStart.call(this, when, off, duration);
    };
    if (!compressor) BaseAudioContext.prototype.createDynamicsCompressor = function () {const g = this.createGain();for (const k of ['threshold', 'knee', 'ratio', 'attack', 'release']) g[k] = {value: 0};return g;};
    else if (volume !== 1) AudioNode.prototype.connect = function (dest, ...rest) {
      // The web's master gain is volume * 0.8; renderOffline uses 0.8 (volume 1): scale in front of the compressor.
      if (dest instanceof DynamicsCompressorNode) {const s = this.context.createGain();s.gain.value = volume;origConnect.call(this, s);return origConnect.call(s, dest, ...rest);}
      return origConnect.call(this, dest, ...rest);
    };
    const snap = t => (Math.round(t * rate) - 1e-4) / rate, saved = [];
    const patchTime = (proto, name, arg) => {const f = proto[name];saved.push([proto, name, f]);proto[name] = function (...a) {if (typeof a[arg] === 'number' && a[arg] > 0) a[arg] = snap(a[arg]);return f.apply(this, a);};};
    for (const m of ['setValueAtTime', 'linearRampToValueAtTime', 'exponentialRampToValueAtTime', 'setTargetAtTime']) patchTime(AudioParam.prototype, m, 1);
    patchTime(AudioScheduledSourceNode.prototype, 'stop', 0);
    const startPatched = AudioBufferSourceNode.prototype.start;
    AudioBufferSourceNode.prototype.start = function (when = 0, ...rest) {return startPatched.call(this, when > 0 ? snap(when) : when, ...rest);};
    const origOscStart = OscillatorNode.prototype.start;
    OscillatorNode.prototype.start = function (when = 0) {return origOscStart.call(this, when > 0 ? snap(when) : when);};
    let promise;
    try {promise = GM.renderOffline(name, seconds, rate, startBar);} finally {
      for (const [proto, n, f] of saved.reverse()) proto[n] = f;
      OscillatorNode.prototype.start = origOscStart;
      Math.random = origRandom;AudioBufferSourceNode.prototype.start = origStart;BaseAudioContext.prototype.createDynamicsCompressor = origComp;AudioNode.prototype.connect = origConnect;
    }
    const buf = await promise, from = lead + preBars * barFrames - preTail, len = preTail + bars * barFrames;
    return {L: buf.getChannelData(0).slice(from, from + len), R: buf.getChannelData(1).slice(from, from + len), preBars};
  }
  // The whole track as one seamless loop (before the compressor).
  async function renderLoop(name, rate, {segSeconds = 10, preSeconds = 10.5, xfade = 2048, progress = () => {}} = {}) {
    const {info, barSec, barFrames, loopFrames} = loopInfo(name, rate);
    const n = Math.max(1, Math.round(segSeconds / barSec)), p = Math.ceil(preSeconds / barSec);
    const L = new Float32Array(loopFrames), R = new Float32Array(loopFrames), tails = [];
    for (let b = 0; b < info.bars; b += n) {
      const cnt = Math.min(n, info.bars - b), r = await renderRegion(name, b, p, cnt, rate, {preTail: xfade});
      L.set(r.L.subarray(xfade), b * barFrames);R.set(r.R.subarray(xfade), b * barFrames);
      tails.push([b, r.L.slice(0, xfade), r.R.slice(0, xfade)]);progress(b + cnt, info.bars);
    }
    let seamMax = 0;const joins = [];
    for (const [b, tl, tr] of tails) for (let i = 0; i < xfade; i++) {
      const f = ((b * barFrames - xfade + i) % loopFrames + loopFrames) % loopFrames, w = (i + 1) / (xfade + 1);
      const d = Math.max(Math.abs(L[f] - tl[i]), Math.abs(R[f] - tr[i]));seamMax = Math.max(seamMax, d);
      if (i === 0) joins.push([b, 0, 0]);const j = joins[joins.length - 1];if (d > j[1]) {j[1] = d;j[2] = i;}
      L[f] = L[f] * (1 - w) + tl[i] * w;R[f] = R[f] * (1 - w) + tr[i] * w;
    }
    return {L, R, loopFrames, barFrames, seamMax, joins, segments: tails.length, segBars: n, preBars: p};
  }
  window.__hkMusic = {renderRegion, renderLoop, loopInfo, SEED};
})();
