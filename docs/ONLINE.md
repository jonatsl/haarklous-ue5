# Online play in the Unreal version

Port of the web game's `game-net.js`, `game-multiplayer.js`, `game-avatars.js` and `game-voice.js` (plus the parts of
`game-main.js` that use them). Unreal and web clients play together in one lobby: same JSON protocol
(`reference/web-game/cloud/src/index.js`), same vehicle numbering, same rules. Online is **off** in builds
(`[Haarklous.Online] bOnlineEnabled=False` in `Config/DefaultGame.ini`); only the separate test server is configured.

## Files
| File | What |
| --- | --- |
| `HKNet.h/.cpp` | `FHKNet`: lobby connection (Unreal WebSockets), own 4-character code, name, reconnect back-off `min(30000, 1500·2^retry)`, full lobby → own lobby, clock offset (welcome/pong), ping every 20 s. `FHKStore` (the web's localStorage: `Saved/LocalStorage.json`), `HKJsonWire` (JSON exactly like `JSON.stringify`), `FHKTimers` (setTimeout/setInterval), `FHKCodePad` |
| `HKMultiplayer.h/.cpp` | `FHKMultiplayer`: remotes, avatar smoothing, remote-driven vehicles (`smoothVehicle` via `FHKVehicle::NetSet/NetAnimate/NetCur`), seat claims (4 s timeout), release, occupant, chat, doors, shared fire, `onLobbyChange`. `FHKPlayerState` = the `st` message. `IHKVoice` = voice interface |
| `HKAvatars.h/.cpp` | `FHKAvatar`: the other players' figure (rigs `avatar_0..4`, jacket colour per slot), `Pose()` (walk, crouch, prone, sit, drive, ride, pillion, drone, beer), name tag and chat bubble drawn in `DrawHud` with `FHKUi::Project`; `HKAvatars::Drone()` (rig `avatar_drone`) |
| `HKVoice.h/.cpp` | `FHKWebVoice`: voice chat through the web's own `game-voice.js` in a hidden CEF page (`Content/Web/voice/`) |
| `HKOnlineGame.h/.cpp` | `AHKOnline`: connects all of it to `AHKGame`'s hooks (localState, ctx, pause-menu lobby section, code pad). One call: `AHKOnline::Setup(this)` at the end of `AHKGame::SetupModules()` |
| `HKOnlineTestScript.cpp` | play-through `-hktest=online` (the real game in a lobby with a web client) |
| `Tests/HKNetTests.cpp` | `Haarklous.Net.Codes/Json/AvatarPoses/Session`: compared with reference answers from the web code (`tools/web-export/reference-net.js` → `Content/Data/tests/net.json`) |
| `Tests/HKNetLiveTests.cpp` | `Haarklous.Net.Live` (+`LiveFull`): live, with a web client, against the test server |
| `Tests/HKNetVoiceTests.cpp` | `Haarklous.Net.Voice` (live voice), `Haarklous.Net.VoicePage` (the page's `game-voice.js` is unchanged) |
| `tools/web-export/exporter-avatars.js`, `rigs.d/avatars.json` | avatar and remote-drone rigs from the web code |
| `tools/net-test/web-client.mjs`, `run-live.ps1` | headless web client (real web game, fake microphone, network limited to the test host) and the live test runner |
| `server/` | the test server: copy of `reference/web-game/cloud/` with only `name = "haarklous-ue5-test"` changed; `public/index.html` = `reference/web-game/haarklous-game.html` |

## Settings
- `[Haarklous.Online] bOnlineEnabled`, `ServerBase` (DefaultGame.ini). Command line: `-HKOnline` / `-HKOffline`,
  `-HKServer=ws(s)://host` (the web's `?server=`), `-HKLobby=CODE` (`?lobby=`).
- Any server address containing `haarklous-online` (the live game) is refused: online stays off.
- While online is off the game behaves like the web game without WebSocket support (status 'offline', seats free).

## Test server
`https://haarklous-ue5-test.server-smoke.workers.dev` (Cloudflare account 46fd597da1c5fe9de4c3ebbaae86f906). Redeploy:
check `server/wrangler.toml` says `haarklous-ue5-test`, then `npx wrangler deploy` in `server/`. Local: `npx wrangler dev
--ip 127.0.0.1 --port 8787` in `server/`. The web client served by `wrangler dev` needs `?server=ws://127.0.0.1:8787`
(without it the web code falls back to the live server; the test client blocks every other host).

## Tests
- `powershell -File tools\run-tests.ps1 -Filter Haarklous.Net` (no network).
- `powershell -File tools\net-test\run-live.ps1 -Mode local|deployed [-Voice] [-Game] [-WebDelayS 100]` – logs and
  summaries in `tools/out/net-tests/` (`unreal-client-*.json`, `web-client-*.json`, `web-sees-unreal-*.png`).
- `-Game`: the real game (`-game`, map Torshov, play-through `-hktest=online`, `HKOnlineTestScript.cpp`) and the web
  game (`web-client.mjs --game`) follow one script, handing over with chat lines: A chat both ways, a door and a log on
  the fire from each side, both walk; B the web player drives car 1, the game rides along through its own
  "□ Ride along" prompt (lobby gives 'p', the web sees the avatar at the passenger anchor, the game's camera moves
  with the car); C the game drives the motorcycle (prompt □ + simulated R2), the web player rides pillion; D voice
  (the game's microphone is a 440 Hz tone with `-HKVoiceTone`). Screenshots: `tools/out/net-tests/shots-<mode>-game/`
  (game side) and `web-<mode>-*.png` (web side).
- `-Game -RealMic`: the same with the laptop's real microphone instead of the tone, while alternating beeps play
  through the speakers. `powershell -File tools\run-game.ps1 -Test mic` only opens the microphone the way the voice
  chat does and logs the level for 5 seconds.
- The headless web page runs requestAnimationFrame about once a second (hidden page), so the test client runs frames
  on a timer like the web's own `tests/browser-harness.js`; next to the game it still renders slowly and its game time
  (capped at 50 ms a frame) runs at about a third of real time, so it walks and drives by distance, not by time.

## Voice: how and why
CEF in UE 5.8 (Chromium 128) has WebRTC, but it cannot open the microphone itself: UE's CEF client has no media
permission handler and CEF's command line is locked (`command_line_args_disabled`, no `--enable-media-stream`).
So the hidden page runs the web's unchanged `game-voice.js` and its "microphone" is a Web Audio stream fed with 48 kHz
PCM from Unreal (`AudioCaptureCore`, default input device; tests use a 440 Hz tone). Remote voices play in the page
(Chromium's audio output, full volume like the web). Browsers only start audio after a user gesture: the bridge sends
one key press to the hidden page. Signalling uses the lobby's `rtc` messages. Needs rendering (CEF is off with
`-nullrhi` and in commandlets). Tested Unreal↔web in both offer directions, locally and on the deployed test server.
Other options and their costs:
1. Native WebRTC in Unreal (Epic's PixelStreaming WebRTC library or libwebrtc built by hand): no browser, but a large
   build/licensing job and our own SDP/ICE code to match `game-voice.js`; days of work, hard to keep in sync.
2. Patch the engine's CEF client (permission handler / `--enable-media-stream`): needs a source-built engine.
3. Unreal's own VoIP (Online Subsystem voice): cannot talk to the web clients (different transport and codec).
4. A server-side relay (SFU / TURN service): changes the backend, which must stay unchanged.

## Still open
- Avatar meshes are imported (`/Game/Haarklous/Meshes/avatar_0..4`, `avatar_drone`). After a full re-export of the
  web geometry, rebuild them: `powershell -File tools\build-assets.ps1 -From 3 -Rigs "avatar_0 avatar_1 avatar_2 avatar_3 avatar_4 avatar_drone"`.
- Real microphone: tested on 27 Oct 2026 with `run-live.ps1 -Mode deployed -Voice -RealMic` while the user talked
  to the laptop: the game opened "Mikrofonrekke (Realtek(R) Audio)" and the web game heard the voice (RMS 0.068; the
  game heard the web at 0.205). Before that the microphone was muted with the laptop's key (pure silence everywhere).
  The game's microphone sound gets an automatic gain and a noise gate (`FHKWebVoice::PumpAudio`), like the browser's
  autoGainControl / noiseSuppression that `game-voice.js` asks for. No echo cancellation (headphones avoid echo).
- Fixed: turning the microphone off or quitting after using it crashed (the engine's capture thread outlived the
  capture object); `AbortCapturing()` now stops and closes it first.
- No TURN server (like the web): voice may fail between homes behind strict NATs; text chat always works.
