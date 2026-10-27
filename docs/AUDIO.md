# Sound and music (Unreal version)

Goal: the sounds and music sound the same as in the web version. Decisions (27 Oct 2026):

## How it works
- **Sound effects, footsteps, ambience, engines, fire, mission pistol**: `game-audio.js` and `case-game.js sound()` are
  ported line by line (`Private/HKAudioImpl.cpp`) onto a small Web Audio engine in C++ (`HKSynth.h/.cpp`): gain,
  oscillator (Chromium's band-limited wavetables), buffer source, biquad filter, stereo panner, wave shaper,
  convolver (zero-latency partitioned FFT, Chromium's IR normalisation) and AudioParam automation. Math.random is a
  mulberry32 generator, seeded per session (random at run time), in the same call order as the JavaScript.
  Chromium details that were measured in the browser and copied: loops run from `loopStart` to the buffer end even
  when `loopEnd` is 0; buffer sources and oscillators start with sub-sample accuracy; an oscillator with automated
  frequency uses the frequency values from the start of its first render quantum.
- **Music**: `tools/web-export/render-music.mjs` renders every track with the web game's own
  `GameMusic.renderOffline()` (headless Chrome, network blocked) into seamless loops, 48 kHz 16-bit stereo WAV in
  `Content/Data/audio` (+ `music.json`), taken before the compressor. `FHKMusicPlayer` is a port of `create()` in
  `game-music.js` (lanes, faders, 60 ms tick scheduling bars 1.2 s ahead, a returning track continues at its next
  bar) and applies the browser's DynamicsCompressor (Chromium kernel port) after volume and crossfades, so every
  volume setting sounds like the web. Rebuild the loops: `node tools/web-export/render-music.mjs`.
  To make loops exact, the render seeds Math.random the same for every segment, replaces the noise offsets of
  hats/brushes with a hash of the note's loop position, snaps note times to frames and keeps every segment on the
  same 128-frame grid (Chromium oscillator start-phase quirk). Joins differ by < 1e-7.
- **Unreal glue**: `UHKAudioSubsystem` (world subsystem) spawns `AHKAudioActor` in every game/PIE world; it creates
  `FHKAudioEngine` (audio thread: game audio + music), a `UHKAudioSynthComponent` (USynthComponent, stereo,
  not spatialised, keeps playing when paused) and registers `FHKAudio` / `FHKMusic`. Game-thread calls are queued
  commands. Unreal's platform headroom (-3 dB on Windows) is undone so levels match the browser.
- Engines: `FHKAudio::Get().Engine(kind)` -> `IHKEngineSound` with `Set(a, b)`, `Stop()` and `Hydraulics(flow)`
  (the excavator's `engine.hydraulics(flow)`, a no-op for other kinds). When the audio actor ends it logs
  `Audio: rendered ... s, ... ms audio-thread time per second, output peak, average dBFS, music <track>`.
- Music volume is saved in `GameUserSettings.ini` `[Haarklous.Music] haarklous-music-volume-v1` (default 0.5).

## Small deviations (inaudible)
- The excavator's hydraulics noise is stopped with the engine (the web leaves it running at gain 0).
- Dropping an engine handle stops the engine (the web would leave it running).
- Music tails during a crossfade: the loop already contains the reverb, so the 2.5 s fader also fades the reverb
  tail (the web lets the shared reverb ring out).
- `SetListener` is stored only: the web version has no positional sound.

## Tests (`tools\run-tests.ps1 -Filter Haarklous.Audio`)
Reference renders from the browser: `node tools/web-export/export.mjs --only=tests --script=reference-audio.js`
(`Content/Data/tests/audio.json`, 71 cases with their events, the compressor and 5 music regions).
