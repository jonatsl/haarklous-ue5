/* Bridge between Unreal (HKVoice.cpp, a hidden page in the engine's Chromium) and the web game's own game-voice.js,
 * so the Unreal client joins the same WebRTC voice mesh as the web clients.
 * Unreal -> page: window.hkVoice.* (ExecuteJavascript). Page -> Unreal: window.ue.hkvoice.ready() / signal(to, json) /
 * micresult(ok, on) / stats(json) (a bound UObject).
 * The "microphone" of game-voice.js is the sound Unreal pushes as 48 kHz 16-bit PCM (its own microphone capture, or a
 * test tone): a Web Audio stream, so the page never needs the browser's microphone permission.
 */
(() => {
  'use strict';
  const call = (fn, ...a) => {const o = window.ue && window.ue.hkvoice;if (o && o[fn]) try {o[fn](...a);} catch (e) {}};
  let you = 0;
  const RATE = 48000, ctx = new AudioContext({sampleRate: RATE}), dest = ctx.createMediaStreamDestination();
  // PCM from Unreal -> ring buffer -> ScriptProcessor -> MediaStream (kept about 0.1 s behind).
  const ring = new Float32Array(RATE);let rd = 0, wr = 0;
  const avail = () => (wr - rd + ring.length) % ring.length;
  const proc = ctx.createScriptProcessor(1024, 1, 1);
  proc.onaudioprocess = e => {const out = e.outputBuffer.getChannelData(0);for (let i = 0; i < out.length; i++) {if (rd !== wr) {out[i] = ring[rd];rd = (rd + 1) % ring.length;} else out[i] = 0;}};
  const silent = ctx.createGain();silent.gain.value = 0;
  proc.connect(dest);proc.connect(silent);silent.connect(ctx.destination);
  const getUserMedia = async () => {ctx.resume().catch(() => {});return new MediaStream([dest.stream.getAudioTracks()[0].clone()]);};
  if (navigator.mediaDevices) navigator.mediaDevices.getUserMedia = getUserMedia;
  else Object.defineProperty(navigator, 'mediaDevices', {value: {getUserMedia}, configurable: true});
  const voice = window.GameVoice.create({send: (to, d) => call('signal', to, JSON.stringify(d)), you: () => you});
  // Received level per peer (RMS of what the page plays).
  const meters = new Map();
  function level(id, p) {
    const s = p.audio.srcObject;if (!s) return 0;
    let m = meters.get(id);
    if (!m || m.stream !== s) {const an = ctx.createAnalyser();an.fftSize = 2048;ctx.createMediaStreamSource(s).connect(an);m = {stream: s, an, buf: new Float32Array(2048)};meters.set(id, m);}
    m.an.getFloatTimeDomainData(m.buf);let e = 0;for (const v of m.buf) e += v * v;return Math.sqrt(e / m.buf.length);
  }
  window.hkVoice = {
    setYou(id) {you = id;},
    peerJoined(id) {voice.peerJoined(id);},
    peerLeft(id) {voice.peerLeft(id);meters.delete(id);},
    onSignal(from, json) {voice.onSignal(from, JSON.parse(json));},
    async setMic(on) {const ok = await voice.setMic(on);call('micresult', ok, voice.mic);},
    setVolume(id, v) {voice.setVolume(id, v);},
    reset() {voice.reset();meters.clear();},
    pcm(b64) {
      const s = atob(b64), n = s.length >> 1;
      for (let i = 0; i < n; i++) {
        let v = s.charCodeAt(2 * i) | (s.charCodeAt(2 * i + 1) << 8);if (v >= 32768) v -= 65536;
        ring[wr] = v / 32768;wr = (wr + 1) % ring.length;if (wr === rd) rd = (rd + 1) % ring.length;
      }
      if (avail() > RATE * .3) rd = (wr - Math.round(RATE * .1) + ring.length) % ring.length;
    },
    unlock() {if (ctx.state !== 'running') ctx.resume().catch(() => {});for (const p of voice.peers.values()) if (p.audio.srcObject && p.audio.paused) p.audio.play().catch(() => {});}
  };
  setInterval(async () => {
    const out = {ctx: ctx.state, mic: voice.mic, buffered: avail(), activation: !!(navigator.userActivation && navigator.userActivation.hasBeenActive), peers: []};
    for (const [id, p] of voice.peers) {
      const st = {id, state: p.pc.connectionState, ice: p.pc.iceConnectionState, offerer: p.offerer, paused: p.audio.paused, stream: !!p.audio.srcObject, bytesIn: 0, bytesOut: 0, level: 0};
      try {st.level = level(id, p);} catch {}
      try {(await p.pc.getStats()).forEach(r => {if (r.type === 'inbound-rtp' && r.kind === 'audio') {st.bytesIn = r.bytesReceived;st.energy = r.totalAudioEnergy;} if (r.type === 'outbound-rtp' && r.kind === 'audio') st.bytesOut = r.bytesSent;});} catch {}
      out.peers.push(st);
    }
    call('stats', JSON.stringify(out));
  }, 1000);
  // Tell Unreal once its object is bound into the page.
  const hello = setInterval(() => {if (window.ue && window.ue.hkvoice) {clearInterval(hello);call('ready');}}, 50);
})();
