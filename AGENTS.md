# Haarklous UE5 – continuation instructions

A local exploration (started 26 Oct 2026): rebuild the **graphics** of the Haarklous online web game in **Unreal Engine 5**, with 3D assets made in **Blender**. Read `README.md`, `docs/PORTING-CHECKLIST.md` and `reference/web-game/docs/HANDOFF.md` before working.

## The one rule: only the graphics change
- The web game in `reference/web-game/` is the **specification**. Port its behaviour 1:1: the same map and coordinates, features, controls, speeds, physics values, rules, menus, mission, minigames and apartment layout. Read the JavaScript to get the exact numbers; do not guess or "improve" gameplay.
- What may change: how things look (models, materials, textures, lighting, sky, foliage, effects, character and hand animation, UI styling) and anything needed to make that run well in Unreal.
- Any change to gameplay, controls, rules or features needs the user's OK first. Ask; do not decide.
- The apartment design is frozen (`reference/web-game/apartment-data.js`, "your kitchen + bedroom design"): kitchen and bedroom layout must not change, only finishes.

## Online features: keep them, but stay offline
- Keep every online feature (lobbies with 4-letter codes, up to 5 players, synced players and vehicles, passengers, chat, voice, shared doors and firepit) so the game can be launched later.
- The backend is `reference/web-game/cloud/` (Cloudflare Worker + Durable Objects). Keep it unchanged. The Unreal client must speak the same WebSocket JSON protocol (`cloud/src/index.js`, `game-net.js`, `game-multiplayer.js`), including the same vehicle numbering (vehicle list order and the seeded generator in `game-main.js`).
- **Live game untouched:** never deploy to, change or connect to the live Worker `haarklous-online` (`haarklous-online.server-smoke.workers.dev`). Online code is switched off by default in builds.
- **Online testing (allowed since 26 Oct 2026):** on a separate test server only. Copy `reference/web-game/cloud/` to `server/`, change nothing but the Worker name to `haarklous-ue5-test` in `server/wrangler.toml` (and put the reference `haarklous-game.html` in `server/public/index.html`), test with `npx wrangler dev` locally, then deploy that test Worker to the user's Cloudflare account (46fd597da1c5fe9de4c3ebbaae86f906, Wrangler is logged in on this PC) at `haarklous-ue5-test.server-smoke.workers.dev`. Test the Unreal client together with the web client served by that test Worker in one lobby.
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
- Input: PS4 DualShock 4 over USB or Bluetooth is the main controller (Unreal does not see it as an Xbox pad by default: use the Windows RawInput plugin or Steam Input), plus keyboard and mouse. The controller works with the web version but is **not available right now**: build full support with the web version's mapping, test with keyboard, mouse and simulated controller input, and list the DualShock test as still to do. (27 Oct 2026: everything else is tested, including voice with the real microphone; the user confirmed the controller is still not here.)

## Where things are (27 Oct 2026)
- Architecture and conventions: `docs/ARCHITECTURE.md` (port 1:1 in the web frame, one conversion in `HKMath.h`, rigs, module rules).
- Commands: see README "How it is built". Builds, tests and Unreal Python runs share one lock file (several helpers may run).
- Helper sessions worked in git worktrees under `.claude/worktrees/` (ignored by git); all their branches are merged into `main` and the worktrees removed (27 Oct 2026).
- Play-throughs (`tools\run-game.ps1 -Test <name>`, scripts in `HKTestScripts.cpp` / `HKOnlineTestScript.cpp`): `shots`
  (tour of the world), `walkhome`, `vehicles`, `activities`, `mission`, `drunk`, `menus` (keyboard, mouse, pause menu,
  settings; add `-Extra "-HKOnline -HKVoiceTone"` for the lobby section on the test server), `stairwells`, `mic` (real microphone level), `fps` (frame rate in the busiest
  places), `online` (via `tools\net-test\run-live.ps1`, test server only).
- Look decisions (sky, horizon, lamp globes without shadows, ground, trees): `docs/ARCHITECTURE.md` "Look of the world".
- Photo-real pass (28 Oct 2026, goal: real grass, buildings like the real ones, Battlefield 4-style realism):
  `docs/ARCHITECTURE.md` "Photo-real pass". The real blocks around 17A are salmon brick with dark-brown windows,
  grey plinths and concrete balconies; neighbours yellow/red plaster; roofs dark slate, some red tile (Street View,
  Esri aerial, OSM colours). Free CC0 scans from Poly Haven and ambientCG are allowed (user, 27 Oct 2026; re-download
  with `python tools/fetch-library.py`); Fab/Megascans is not used (needs the user's Epic sign-in and licence).
- Bugs found in the web game and ideas (not changed without the user's OK): `docs/GAMEPLAY-IDEAS.md`.
- Final report: `docs/REPORT.md`; `python tools\make-report.py` turns it into `docs/report/Haarklous-UE5-report.html`
  with the screenshots embedded.

## User preferences (carried over)
- Ask **one question at a time**; when asking about something visual, show the image in the same message (absolute local path in Markdown).
- Build and maintain the project; do not just hand the user instructions.
- Write decisions into files as you go (this file, `README.md`, `docs/`) so a new session can continue.
- Commit messages: very simple human language with bullet points.
- Clean up disposable test files after use.
