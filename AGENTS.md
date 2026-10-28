# Haarklous UE5 – continuation instructions

A local exploration (started 26 Oct 2026): rebuild the **graphics** of the Haarklous online web game in **Unreal Engine 5**, with 3D assets made in **Blender**. Read `README.md`, `docs/PORTING-CHECKLIST.md` and `reference/web-game/docs/HANDOFF.md` before working.

## The one rule: only the graphics change
- The web game in `reference/web-game/` is the **specification**. Port its behaviour 1:1: the same map and coordinates, features, controls, speeds, physics values, rules, menus, mission, minigames and apartment layout. Read the JavaScript to get the exact numbers; do not guess or "improve" gameplay.
- What may change: how things look (models, materials, textures, lighting, sky, foliage, effects, character and hand animation, UI styling) and anything needed to make that run well in Unreal.
- Any change to gameplay, controls, rules or features needs the user's OK first. Ask; do not decide.
- The apartment design is frozen (`reference/web-game/apartment-data.js`, "your kitchen + bedroom design"): kitchen and bedroom layout must not change, only finishes.

## Online features: keep them, but stay offline
- Keep every online feature (lobbies with 4-letter codes, up to 12 players (the user's decision on 28 Oct 2026, for the
  whole lobby; the web game and the live server still say 5), synced players and vehicles, passengers, chat, voice, shared doors and firepit) so the game can be launched later.
- The backend is `reference/web-game/cloud/` (Cloudflare Worker + Durable Objects). Keep it unchanged. The Unreal client must speak the same WebSocket JSON protocol (`cloud/src/index.js`, `game-net.js`, `game-multiplayer.js`), including the same vehicle numbering (vehicle list order and the seeded generator in `game-main.js`).
- **Live game untouched:** never deploy to, change or connect to the live Worker `haarklous-online` (`haarklous-online.server-smoke.workers.dev`). Online code is switched off by default in builds.
- **Online testing (allowed since 26 Oct 2026):** on a separate test server only. Copy `reference/web-game/cloud/` to `server/`, change nothing but the Worker name to `haarklous-ue5-test` in `server/wrangler.toml` and, since 28 Oct 2026 with the user's OK, `MAX_PLAYERS = 12` in `server/src/index.js` for paintball 6 v 6 (and put the reference `haarklous-game.html` in `server/public/index.html`), test with `npx wrangler dev` locally, then deploy that test Worker to the user's Cloudflare account (46fd597da1c5fe9de4c3ebbaae86f906, Wrangler is logged in on this PC) at `haarklous-ue5-test.server-smoke.workers.dev`. Test the Unreal client together with the web client served by that test Worker in one lobby.
- GitHub (allowed by the user on 27 Oct 2026): the private repo **jonatsl/haarklous-ue5** (remote `origin`); push
  there. Never use the GitHub account **ms-jonathans** for this project, even if it is available.

## Never touch the web game
- Do not edit, build, commit, push or deploy anything in `C:\Users\Lenovo\Documents\haarklous-plass-ps-kontroller` (the live web game, its own repo and PRs). Read from it only to refresh the snapshot (`tools\refresh-reference.ps1`).
- `reference/web-game/` is a read-only snapshot. Do not edit it; refresh it with the script when the user says the web game changed.

## This PC
- AMD Ryzen 5 5600H with integrated Radeon graphics (2 GB shared video memory), 14 GB RAM, Windows 11. Performance target: 20–30 fps or better on this laptop.
- So: no Lumen, no Nanite, no virtual shadow maps, no hardware ray tracing. Use static or stationary lighting, cascaded shadow maps, scalability Medium/Low, screen percentage around 70–80 %, and LODs plus HLOD/merged actors for the city.
- Installed: Blender 5.2 (`C:\Program Files\Blender Foundation\Blender 5.2\blender.exe`), Git LFS 3.7, Epic Games Launcher. **Unreal Engine 5.8.4** chosen (installed from the Epic launcher on 26 Oct 2026: Windows only, no other target platforms). Visual Studio 2022 Community with "Game development with C++" (MSVC 14.44, Windows SDK 26100) is installed too.

## How to work (an agent cannot click through the editor reliably)
- Game code in **C++** (text you can edit and review), not Blueprint graphs. Blueprints only as thin data or asset wrappers.
- Automate asset work with scripts: Blender Python run headless (`blender -b -P tools/....py`) to make `.blend`/`.fbx`/`.glb` files from the reference data, and Unreal Python editor scripts (`UnrealEditor-Cmd.exe <project> -run=pythonscript -script=...`) to import and place them. Keep the scripts in `tools/` so every asset can be rebuilt.
- Build from the command line (UnrealBuildTool / RunUAT). Check your work with screenshots and logs before telling the user something works.
- **Work in one go** (user's wish, 26 Oct 2026): port and test the whole checklist before reporting back, then give one report with screenshots, frame rates and what still needs the user. Do not stop for approval between features.
- Test everything yourself: automated tests for the logic plus scripted play-throughs with simulated input (like `reference/web-game/tests/browser-harness.js`), screenshots and logs, frame rate in the busiest places. Online features are tested only against the separate test server (see above).
- Prefer scripts and the command line; computer use (controlling the screen) only for steps that exist only in a window (installers, first-time dialogs). Ask the user only when truly blocked, one question at a time.
- Input: PS4 DualShock 4 over USB or Bluetooth is the main controller (Unreal does not see it as an Xbox pad by default: use the Windows RawInput plugin or Steam Input), plus keyboard and mouse. Full support with the web version's mapping is built; the user tested the real DualShock 4 on 28 Oct 2026 and it works. Keep testing with keyboard, mouse and the simulated controller in the scripted play-throughs.

## Where things are (27 Oct 2026)
- Architecture and conventions: `docs/ARCHITECTURE.md` (port 1:1 in the web frame, one conversion in `HKMath.h`, rigs, module rules).
- Commands: see README "How it is built". Builds, tests and Unreal Python runs share one lock file (several helpers may run).
- Helper sessions worked in git worktrees under `.claude/worktrees/` (ignored by git); all their branches are merged into `main` and the worktrees removed (27 Oct 2026).
- Play-throughs (`tools\run-game.ps1 -Test <name>`, scripts in `HKTestScripts.cpp` / `HKOnlineTestScript.cpp`): `shots`
  (tour of the world), `walkhome`, `vehicles`, `activities`, `mission`, `drunk`, `menus` (keyboard, mouse, pause menu,
  settings; add `-Extra "-HKOnline -HKVoiceTone"` for the lobby section on the test server), `stairwells`, `mic` (real microphone level), `fps` (frame rate in the busiest
  places), `online` (via `tools\net-test\run-live.ps1`, test server only), `site` (construction site machines),
  `people` (the realistic people in every pose), `paintball` (the course with five simulated friends), `balcony` (the
  balcony door prompt from 25 places, open, close from outside);
  `tools\net-test\run-paintball.ps1` (the real game and seven bot clients on the test server). Scripted tests ignore the
  real keyboard and mouse.
- Paintball (28 Oct 2026, the user's own feature): `docs/ARCHITECTURE.md` "Paintball course". The TEST server
  (`server/`) allows 12 players a lobby (allowed by the user on 28 Oct 2026, for 6 v 6); that is the only change to the
  test server's code. The live server keeps 5; changing it is the user's decision.
- Open issues the user filed (not started): GitHub issues on jonatsl/haarklous-ue5 (#1 paintball cover that looks
  see-through, #2 street lamps, #3 the Torshovdalen sculpture). Do not code them unless the user asks.
- Gameplay changes the user asked for (28 Oct 2026, differ from the web game on purpose): walking speed standing
  3.5 m/s (`HKFoot::Speed`; the web walks at 1.45; running stays 3.6, crouch and prone keep the web's speeds); a
  paintball game with nobody left in it ends after 3 s (back to the lobby); the construction site is 140 x 90 m with
  two gates and R3 / V views (cab, outside, above the hook). Bug fixed against the web's own behaviour: the look trace
  for doors and drinks passes through walls like the web's raycast (the balcony door prompt never showed).
- New features the user asked for (28 Oct 2026, not in the web game): the construction site and realistic people
  (`docs/ARCHITECTURE.md`). MakeHuman tools live in a private Blender profile (`tools\out\blender-user`, made by
  `tools\setup-people.ps1`); never install them into the user's own Blender.
- Look decisions (sky, horizon, lamp globes without shadows, ground, trees): `docs/ARCHITECTURE.md` "Look of the world".
- Photo-real pass (28 Oct 2026, goal: real grass, buildings like the real ones, Battlefield 4-style realism):
  `docs/ARCHITECTURE.md` "Photo-real pass". The real blocks around 17A are salmon brick with dark-brown windows,
  grey plinths and concrete balconies; neighbours yellow/red plaster; roofs dark slate, some red tile (Street View,
  Esri aerial, OSM colours). Free CC0 scans from Poly Haven and ambientCG are allowed (user, 27 Oct 2026; re-download
  with `python tools/fetch-library.py`; the sky photo kloofendal_38d_partly_cloudy_puresky allowed 28 Oct 2026);
  Fab/Megascans is not used (needs the user's Epic sign-in and licence). Vehicles: the car has a new Blender body
  (`tools/blender/car_model.py`); the others keep the web shapes with rounded edges. Blocks within 270 m have real
  3D walls (`tools/blender/facade_model.py`); after changing them, rebuild world_near and bake the lighting.
- Bugs found in the web game and ideas (not changed without the user's OK): `docs/GAMEPLAY-IDEAS.md`.
- Final report: `docs/REPORT.md`; `python tools\make-report.py` turns it into `docs/report/Haarklous-UE5-report.html`
  with the screenshots embedded.

## User preferences (carried over)
- Ask **one question at a time**; when asking about something visual, show the image in the same message (absolute local path in Markdown).
- Build and maintain the project; do not just hand the user instructions.
- Write decisions into files as you go (this file, `README.md`, `docs/`) so a new session can continue.
- Commit messages: very simple human language with bullet points.
- Clean up disposable test files after use.
