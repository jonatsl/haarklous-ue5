/* Procedural game audio (Web Audio API, no sound files).
 * Ambience: birds (more near trees and in the park), wind (stronger outdoors and up high), traffic hum and passing cars
 * near streets, indoor room tone, a fridge hum in the kitchen, echo in stairwells.
 * Interaction sounds: footsteps per surface, doors, drinks, camera shutter, disc golf chains, boules, excavator.
 * Engines: inline-four motorcycle (CBF600), four-cylinder car, diesel excavator with hydraulics, helicopter (rotor slap
 * and turbine), light-aircraft piston engine and propeller, FPV drone motors.
 * GameAudio.create() -> {resume(), ambience(state), footstep(surface, inStair), sfx(name, opts), engine(kind), hydraulics()}
 */
(function (root, factory) {
  root.GameAudio = factory();
})(typeof window === 'undefined' ? globalThis : window, function () {
  'use strict';
  function create() {
    let ctx = null, master, dry, reverbSend, convolver, noiseBuf, amb = null;
    const rand = (a, b) => a + Math.random() * (b - a);
    function init() {
      if (ctx) return true;
      try {ctx = new (window.AudioContext || window.webkitAudioContext)();} catch {return false;}
      master = ctx.createGain();master.gain.value = .8;master.connect(ctx.destination);
      dry = ctx.createGain();dry.connect(master);
      noiseBuf = ctx.createBuffer(1, ctx.sampleRate * 2, ctx.sampleRate);const d = noiseBuf.getChannelData(0);for (let i = 0; i < d.length; i++) d[i] = Math.random() * 2 - 1;
      // Stairwell/indoor echo: a generated decaying impulse response.
      convolver = ctx.createConvolver();const len = ctx.sampleRate * 1.8, ir = ctx.createBuffer(2, len, ctx.sampleRate);
      for (let c = 0; c < 2; c++) {const ch = ir.getChannelData(c);for (let i = 0; i < len; i++) ch[i] = (Math.random() * 2 - 1) * Math.pow(1 - i / len, 3.2) * (i < 400 ? i / 400 : 1);}
      convolver.buffer = ir;reverbSend = ctx.createGain();reverbSend.gain.value = 0;reverbSend.connect(convolver);convolver.connect(master);
      startAmbience();return true;
    }
    function resume() {if (!init()) return;if (ctx.state === 'suspended') ctx.resume();}
    const now = () => ctx.currentTime;
    function noise(loop = true) {const s = ctx.createBufferSource();s.buffer = noiseBuf;s.loop = loop;s.loopStart = Math.random();return s;}
    function out(node, reverb = 0) {node.connect(dry);if (reverb) {const g = ctx.createGain();g.gain.value = reverb;node.connect(g);g.connect(reverbSend);}return node;}
    function env(gainNode, t, peak, attack, decay) {gainNode.gain.setValueAtTime(0, t);gainNode.gain.linearRampToValueAtTime(peak, t + attack);gainNode.gain.exponentialRampToValueAtTime(.0001, t + attack + decay);}

    // ---------- Ambience ----------
    function startAmbience() {
      const wind = noise(), windF = ctx.createBiquadFilter(), windG = ctx.createGain();windF.type = 'lowpass';windF.frequency.value = 420;windG.gain.value = 0;wind.connect(windF).connect(windG);out(windG);wind.start();
      const traffic = noise(), trafF = ctx.createBiquadFilter(), trafG = ctx.createGain();trafF.type = 'lowpass';trafF.frequency.value = 260;trafG.gain.value = 0;traffic.connect(trafF).connect(trafG);out(trafG);traffic.start();
      const room = noise(), roomF = ctx.createBiquadFilter(), roomG = ctx.createGain();roomF.type = 'lowpass';roomF.frequency.value = 140;roomG.gain.value = 0;room.connect(roomF).connect(roomG);out(roomG);room.start();
      const hum = ctx.createOscillator(), humG = ctx.createGain();hum.frequency.value = 100;hum.type = 'sine';humG.gain.value = 0;hum.connect(humG);out(humG);hum.start();
      amb = {windG, windF, trafG, roomG, humG, state: {indoor: 0, green: .5, traffic: 0, height: 0, stair: false, kitchen: false}, birdTimer: 0, carTimer: 3, last: now()};
      setInterval(tickAmbience, 120);
    }
    function ambience(state) {if (amb) Object.assign(amb.state, state);}
    function tickAmbience() {
      if (!amb || ctx.state !== 'running') return;
      const s = amb.state, t = now(), dt = Math.min(.5, t - amb.last);amb.last = t;const outdoors = 1 - s.indoor;
      const set = (param, v) => param.setTargetAtTime(v, t, .4);
      set(amb.windG.gain, (.012 + .03 * Math.min(1, s.height / 12)) * outdoors + .004 * s.indoor);
      amb.windF.frequency.setTargetAtTime(300 + 260 * (.5 + .5 * Math.sin(t * .23)) + 200 * Math.sin(t * .071), t, .8);
      const tr = Math.max(0, 1 - s.traffic / 70);set(amb.trafG.gain, (.012 + .05 * tr * tr) * (outdoors + .25 * s.indoor));
      set(amb.roomG.gain, .03 * s.indoor);set(amb.humG.gain, s.kitchen ? .006 : 0);
      reverbSend.gain.setTargetAtTime(s.stair ? .6 : s.indoor * .12, t, .3);
      // Birds: more frequent near trees and in the park, rare indoors.
      amb.birdTimer -= dt;
      // Indoors only an occasional faint, muffled call through the windows.
      if (amb.birdTimer <= 0) {amb.birdTimer = rand(2.5, 9) / (.4 + s.green * .9);if (Math.random() < outdoors * .85 + .12 * s.indoor) bird(outdoors * (.3 + s.green * .5) + .025 * s.indoor, s.indoor > .5);}
      // Campfire crackles (see fire()).
      if (fireState.gain > .02 && Math.random() < .1 + fireState.level * .08) crackle(fireState.gain);
      amb.carTimer -= dt;
      if (amb.carTimer <= 0) {amb.carTimer = rand(3, 12);if (tr > .15 && outdoors > .3) passingCar(tr * outdoors);}
    }
    function bird(level, muffled = false) {
      const t = now() + .05, pan = ctx.createStereoPanner();pan.pan.value = rand(-.9, .9);const g = ctx.createGain();g.gain.value = level * rand(.05, .12);
      if (muffled) {const lp = ctx.createBiquadFilter();lp.type = 'lowpass';lp.frequency.value = 1400;pan.connect(lp).connect(g);out(g);} else {pan.connect(g);out(g, .15);}
      const species = Math.floor(Math.random() * 4);
      if (species === 0) {// great tit "ti-ta ti-ta"
        for (let i = 0; i < 4; i++) {const o = ctx.createOscillator(), e = ctx.createGain(), f = i % 2 ? 4200 : 5600, at = t + i * .16;o.frequency.setValueAtTime(f, at);o.frequency.exponentialRampToValueAtTime(f * .85, at + .08);o.connect(e).connect(pan);env(e, at, 1, .01, .09);o.start(at);o.stop(at + .12);}
      } else if (species === 1) {// blackbird warble
        let at = t;for (let i = 0; i < 6; i++) {const o = ctx.createOscillator(), e = ctx.createGain(), f = rand(1600, 3200), d = rand(.08, .2);o.frequency.setValueAtTime(f, at);o.frequency.exponentialRampToValueAtTime(f * rand(.7, 1.4), at + d);o.connect(e).connect(pan);env(e, at, 1, .02, d);o.start(at);o.stop(at + d + .05);at += d * rand(.9, 1.3);}
      } else if (species === 2) {// sparrow chirps
        for (let i = 0; i < 3 + Math.floor(Math.random() * 4); i++) {const o = ctx.createOscillator(), e = ctx.createGain(), at = t + i * rand(.12, .25), f = rand(3500, 4800);o.type = 'triangle';o.frequency.setValueAtTime(f, at);o.frequency.linearRampToValueAtTime(f * 1.2, at + .03);o.frequency.linearRampToValueAtTime(f * .8, at + .06);o.connect(e).connect(pan);env(e, at, 1, .005, .06);o.start(at);o.stop(at + .09);}
      } else {// magpie chatter: harsh noise bursts
        for (let i = 0; i < 5; i++) {const n = noise(false), f = ctx.createBiquadFilter(), e = ctx.createGain(), at = t + i * .09;f.type = 'bandpass';f.frequency.value = rand(1800, 2600);f.Q.value = 3;n.connect(f).connect(e).connect(pan);env(e, at, 1.6, .004, .06);n.start(at);n.stop(at + .08);}
      }
    }
    // Campfire: a soft roar plus random crackles, louder for a bigger fire and closer to it. fire(level 0..6, gain 0..1)
    let fireNode = null, fireState = {level: 0, gain: 0};
    function fire(level, gain) {
      if (!ctx || ctx.state !== 'running') return;
      if (!fireNode) {const n = noise(), lp = ctx.createBiquadFilter(), g = ctx.createGain();lp.type = 'lowpass';lp.frequency.value = 500;g.gain.value = 0;n.connect(lp).connect(g);out(g);n.start();fireNode = {g, lp};}
      fireState = {level, gain};const t = now();fireNode.g.gain.setTargetAtTime(.05 * gain * Math.min(1.4, level / 3), t, .3);fireNode.lp.frequency.setTargetAtTime(320 + level * 110, t, .3);
    }
    function crackle(gain) {
      const t = now() + Math.random() * .1;
      for (let i = 0, n = 1 + Math.floor(Math.random() * 3); i < n; i++) {const s = noise(false), f = ctx.createBiquadFilter(), e = ctx.createGain(), at = t + i * rand(.01, .05);f.type = 'bandpass';f.frequency.value = rand(1800, 5200);f.Q.value = 2;s.connect(f).connect(e);out(e);env(e, at, rand(.08, .3) * gain, .001, rand(.02, .07));s.start(at);s.stop(at + .12);}
    }
    function passingCar(level) {
      const t = now(), n = noise(false), f = ctx.createBiquadFilter(), g = ctx.createGain(), pan = ctx.createStereoPanner();f.type = 'lowpass';f.frequency.setValueAtTime(300, t);f.frequency.linearRampToValueAtTime(900, t + 1.6);f.frequency.linearRampToValueAtTime(250, t + 3.4);
      g.gain.setValueAtTime(0, t);g.gain.linearRampToValueAtTime(.09 * level, t + 1.6);g.gain.linearRampToValueAtTime(0, t + 3.5);pan.pan.setValueAtTime(-.8, t);pan.pan.linearRampToValueAtTime(.8, t + 3.4);
      n.connect(f).connect(g).connect(pan);out(pan);n.start(t);n.stop(t + 3.6);
    }

    // ---------- Footsteps ----------
    const steps = {grass: [700, 1.5, .05, .1], gravel: [3800, 1.2, .1, .07], sand: [1600, 1, .06, .08], asphalt: [2200, 2, .07, .04], wood: [900, 2.5, .09, .05], tile: [3200, 4, .07, .03], stone: [2600, 3.5, .1, .04], metal: [1100, 8, .08, .09], terrain: [900, 1.2, .05, .08]};
    function footstep(surface, inStair = false, softness = 1) {
      if (!ctx || ctx.state !== 'running') return;
      const [freq, q, gain, decay] = steps[surface] || steps.terrain, t = now();
      const n = noise(false), f = ctx.createBiquadFilter(), e = ctx.createGain();f.type = 'bandpass';f.frequency.value = freq * rand(.85, 1.15);f.Q.value = q;n.connect(f).connect(e);out(e, inStair ? .9 : 0);env(e, t, gain * softness * rand(.8, 1.2), .004, decay);n.start(t);n.stop(t + decay + .05);
      if (surface === 'gravel') for (let i = 1; i < 4; i++) {const n2 = noise(false), f2 = ctx.createBiquadFilter(), e2 = ctx.createGain(), at = t + i * .018;f2.type = 'highpass';f2.frequency.value = 4000;n2.connect(f2).connect(e2);out(e2);env(e2, at, gain * .6 * softness, .002, .02);n2.start(at);n2.stop(at + .04);}
      if (surface === 'wood' || surface === 'stone' || surface === 'tile') {const o = ctx.createOscillator(), e3 = ctx.createGain();o.frequency.value = surface === 'wood' ? 120 : 180;o.connect(e3);out(e3, inStair ? .9 : 0);env(e3, t, .08 * softness, .003, .07);o.start(t);o.stop(t + .1);}
    }

    // ---------- One-shot effects ----------
    function sfx(name, opts = {}) {
      if (!ctx || ctx.state !== 'running') return;
      const t = now(), level = opts.gain ?? 1, verb = opts.reverb ?? 0;
      const burst = (freq, q, peak, attack, decay, type = 'bandpass', at = t) => {const n = noise(false), f = ctx.createBiquadFilter(), e = ctx.createGain();f.type = type;f.frequency.value = freq;f.Q.value = q;n.connect(f).connect(e);out(e, verb);env(e, at, peak * level, attack, decay);n.start(at);n.stop(at + attack + decay + .05);};
      const tone = (f0, f1, peak, attack, decay, type = 'sine', at = t) => {const o = ctx.createOscillator(), e = ctx.createGain();o.type = type;o.frequency.setValueAtTime(f0, at);o.frequency.exponentialRampToValueAtTime(Math.max(20, f1), at + attack + decay);o.connect(e);out(e, verb);env(e, at, peak * level, attack, decay);o.start(at);o.stop(at + attack + decay + .05);};
      switch (name) {
        case 'doorOpen': tone(320, 520, .03, .05, .35, 'sawtooth');burst(600, 1, .05, .01, .2);break;
        case 'doorClose': burst(160, .8, .5, .004, .25, 'lowpass');tone(90, 50, .3, .003, .2);break;
        case 'locked': for (let i = 0; i < 3; i++) burst(2600, 6, .12, .002, .05, 'bandpass', t + i * .07);break;
        case 'carDoor': burst(220, .8, .5, .004, .3, 'lowpass');tone(70, 40, .4, .003, .25);break;
        case 'bottleOpen': burst(5000, .7, .25, .01, .35, 'highpass');tone(1800, 1200, .05, .002, .04);break;
        case 'gulp': for (let i = 0; i < 3; i++) tone(160, 90, .25, .02, .12, 'sine', t + .25 + i * .32);break;
        case 'shutter': burst(4000, 2, .3, .001, .03);burst(1500, 2, .2, .001, .05, 'bandpass', t + .07);break;
        case 'beep': tone(2000, 2000, .05, .005, .08, 'square');break;
        case 'throw': burst(900, .7, .12, .05, .25);break;
        case 'chains': for (let i = 0; i < 9; i++) tone(rand(1200, 4800), rand(1100, 4600), .05, .002, rand(.2, .7), 'triangle', t + i * .025);burst(3000, 1, .2, .002, .2);break;
        case 'thud': burst(260, 1, .25, .003, .12, 'lowpass');break;
        case 'boule': tone(2600, 2500, .25, .001, .25, 'triangle');tone(5200, 5100, .1, .001, .15);burst(3500, 3, .2, .001, .03);break;
        case 'gravel': burst(2500, .8, .15, .01, .18);break;
        case 'click': burst(3000, 5, .15, .001, .02);break;
        case 'scrape': burst(800, .6, .25, .05, .4);break;
        case 'dump': burst(180, .7, .5, .01, .6, 'lowpass');burst(1200, .5, .2, .02, .5);break;
        case 'hit': burst(500, 1, .4, .002, .15, 'lowpass');break;
        case 'cheer': for (let i = 0; i < 6; i++) tone(rand(300, 700), rand(400, 900), .04, .05, .4, 'sawtooth', t + i * .05);break;
        case 'starter': tone(220, 320, .08, .02, .5, 'square');break;
        case 'crash': burst(220, .7, 1, .003, 1.4, 'lowpass');burst(1600, .6, .5, .002, .7);tone(70, 30, .6, .003, .9);for (let i = 0; i < 6; i++) burst(rand(2500, 6000), 3, .15, .001, .08, 'bandpass', t + .1 + i * rand(.05, .14));break;
        case 'arm': tone(880, 880, .05, .005, .07, 'square');tone(1320, 1320, .05, .005, .1, 'square', t + .12);break;
      }
    }

    // ---------- Engines ----------
    function engine(kind) {
      if (!init()) return {set() {}, stop() {}};
      if (kind === 'heli') return heliEngine();
      if (kind === 'drone') return droneMotors();
      const spec = {bike: {cyl: 4, idle: 1300, max: 11500, tone: 1, gain: .22}, car: {cyl: 4, idle: 800, max: 6200, tone: .6, gain: .16}, excavator: {cyl: 4, idle: 900, max: 2100, tone: .45, gain: .2}, plane: {cyl: 4, idle: 700, max: 2750, tone: .85, gain: .22}}[kind];
      const t = now(), saw = ctx.createOscillator(), sq = ctx.createOscillator(), sub = ctx.createOscillator();saw.type = 'sawtooth';sq.type = 'square';sub.type = 'sine';
      const shaper = ctx.createWaveShaper(), curve = new Float32Array(1024);for (let i = 0; i < 1024; i++) {const x = i / 512 - 1;curve[i] = Math.tanh(x * 3);}shaper.curve = curve;
      const lp = ctx.createBiquadFilter();lp.type = 'lowpass';const g = ctx.createGain();g.gain.value = 0;
      const mix = ctx.createGain();mix.gain.value = .5;saw.connect(mix);const sqG = ctx.createGain();sqG.gain.value = .25;sq.connect(sqG).connect(mix);const subG = ctx.createGain();subG.gain.value = .6;sub.connect(subG).connect(mix);
      // Exhaust roughness: noise amplitude-modulated at the firing frequency.
      const n = noise(), nf = ctx.createBiquadFilter(), nG = ctx.createGain(), am = ctx.createGain();nf.type = 'bandpass';nf.frequency.value = 900;nf.Q.value = .7;nG.gain.value = .25;n.connect(nf).connect(am).connect(nG).connect(mix);
      const amOsc = ctx.createOscillator();amOsc.type = 'square';const amDepth = ctx.createGain();amDepth.gain.value = .5;amOsc.connect(amDepth).connect(am.gain);am.gain.value = .5;
      mix.connect(shaper).connect(lp).connect(g);out(g);
      [saw, sq, sub, amOsc].forEach(o => o.start(t));n.start(t);
      let hyd = null;
      const api = {
        set(rpm, throttle = 0) {
          const tt = now(), fire = rpm / 60 * spec.cyl / 2;
          saw.frequency.setTargetAtTime(fire, tt, .03);sq.frequency.setTargetAtTime(fire * 2.01, tt, .03);sub.frequency.setTargetAtTime(fire * .5, tt, .03);amOsc.frequency.setTargetAtTime(fire, tt, .03);
          lp.frequency.setTargetAtTime(300 + 2600 * spec.tone * (.25 + .75 * throttle) * Math.min(1, rpm / spec.max + .2), tt, .05);
          g.gain.setTargetAtTime(spec.gain * (.35 + .65 * throttle) * (.7 + .3 * rpm / spec.max), tt, .05);
        },
        hydraulics(flow) {
          if (!hyd) {const o = ctx.createOscillator(), hg = ctx.createGain(), hn = noise(), hf = ctx.createBiquadFilter();o.type = 'sawtooth';hf.type = 'bandpass';hf.frequency.value = 1400;hf.Q.value = 2;hg.gain.value = 0;o.connect(hg);hn.connect(hf).connect(hg);out(hg);o.start();hn.start();hyd = {o, hg};}
          hyd.o.frequency.setTargetAtTime(380 + 260 * flow, now(), .05);hyd.hg.gain.setTargetAtTime(.05 * flow, now(), .08);
        },
        stop() {const tt = now();g.gain.setTargetAtTime(0, tt, .15);setTimeout(() => {[saw, sq, sub, amOsc, n].forEach(o => {try {o.stop();} catch {}});if (hyd) {hyd.hg.gain.value = 0;try {hyd.o.stop();} catch {}}}, 900);},
        spec
      };
      return api;
    }
    // Helicopter: main-rotor blade slap (noise, amplitude-modulated at the blade-passing rate), turbine whine, tail-rotor buzz.
    // set(rotor 0..1, load 0..1)
    function heliEngine() {
      const t = now(), n = noise(), bp = ctx.createBiquadFilter(), am = ctx.createGain(), slapG = ctx.createGain(), lfo = ctx.createOscillator(), depth = ctx.createGain();
      bp.type = 'bandpass';bp.frequency.value = 260;bp.Q.value = .9;am.gain.value = .35;lfo.type = 'sawtooth';depth.gain.value = .65;lfo.connect(depth).connect(am.gain);
      slapG.gain.value = 0;n.connect(bp).connect(am).connect(slapG);out(slapG, .08);
      const rumble = noise(), rlp = ctx.createBiquadFilter(), rG = ctx.createGain();rlp.type = 'lowpass';rlp.frequency.value = 110;rG.gain.value = 0;rumble.connect(rlp).connect(rG);out(rG);
      const tur = ctx.createOscillator(), tur2 = ctx.createOscillator(), tG = ctx.createGain();tur.type = 'sine';tur2.type = 'triangle';tG.gain.value = 0;tur.connect(tG);tur2.connect(tG);out(tG);
      const tail = ctx.createOscillator(), tlp = ctx.createBiquadFilter(), tailG = ctx.createGain();tail.type = 'sawtooth';tlp.type = 'lowpass';tlp.frequency.value = 420;tailG.gain.value = 0;tail.connect(tlp).connect(tailG);out(tailG);
      [lfo, tur, tur2, tail].forEach(o => o.start(t));n.start(t);rumble.start(t);
      return {
        set(rpm, load = 0) {
          const tt = now(), blade = 3 * 6.4 * rpm;
          lfo.frequency.setTargetAtTime(Math.max(.5, blade), tt, .1);tail.frequency.setTargetAtTime(30 + 85 * rpm, tt, .1);
          tur.frequency.setTargetAtTime(500 + 1150 * rpm, tt, .2);tur2.frequency.setTargetAtTime(1000 + 2300 * rpm, tt, .2);
          slapG.gain.setTargetAtTime(.32 * rpm * rpm * (.6 + .5 * load), tt, .1);rG.gain.setTargetAtTime(.22 * rpm * (.5 + .5 * load), tt, .1);
          tG.gain.setTargetAtTime(.012 * rpm, tt, .2);tailG.gain.setTargetAtTime(.025 * rpm, tt, .1);
        },
        stop() {const tt = now();[slapG, rG, tG, tailG].forEach(g => g.gain.setTargetAtTime(0, tt, .4));setTimeout(() => {[lfo, tur, tur2, tail, n, rumble].forEach(o => {try {o.stop();} catch {}});}, 2500);}
      };
    }
    // FPV quad: four slightly detuned motors (whine) plus propeller wash. set(thrust 0..1, level 0..1 = distance from the pilot)
    function droneMotors() {
      const t = now(), bp = ctx.createBiquadFilter(), g = ctx.createGain();bp.type = 'bandpass';bp.frequency.value = 1400;bp.Q.value = .5;g.gain.value = 0;bp.connect(g);out(g);
      const motors = [1, 1.013, .987, 1.021].map(k => {const o = ctx.createOscillator();o.type = 'sawtooth';o.connect(bp);o.start(t);return {o, k};});
      const wash = noise(), wf = ctx.createBiquadFilter(), wG = ctx.createGain();wf.type = 'bandpass';wf.frequency.value = 2600;wf.Q.value = .6;wG.gain.value = 0;wash.connect(wf).connect(wG);out(wG);wash.start(t);
      return {
        set(thrust, level = 1) {const tt = now();motors.forEach(m => m.o.frequency.setTargetAtTime((150 + 560 * thrust) * m.k, tt, .04));g.gain.setTargetAtTime((.02 + .07 * thrust) * level, tt, .05);wG.gain.setTargetAtTime(.05 * thrust * level, tt, .05);},
        stop() {const tt = now();g.gain.setTargetAtTime(0, tt, .1);wG.gain.setTargetAtTime(0, tt, .1);setTimeout(() => {motors.forEach(m => {try {m.o.stop();} catch {}});try {wash.stop();} catch {}}, 800);}
      };
    }
    return {resume, ambience, footstep, sfx, engine, fire, get context() {return ctx;}};
  }
  return {create};
});
