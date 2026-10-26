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
- **Offline for now:** do not deploy, do not run `wrangler`, do not connect to the live server (`haarklous-online.server-smoke.workers.dev`), and do not use any Cloudflare account. Online code is built but switched off by default.
- No GitHub for this project: local git only, unless the user asks.

## Never touch the web game
- Do not edit, build, commit, push or deploy anything in `C:\Users\Lenovo\Documents\haarklous-plass-ps-kontroller` (the live web game, its own repo and PRs). Read from it only to refresh the snapshot (`tools\refresh-reference.ps1`).
- `reference/web-game/` is a read-only snapshot. Do not edit it; refresh it with the script when the user says the web game changed.

## This PC
- AMD Ryzen 5 5600H with integrated Radeon graphics (2 GB shared video memory), 14 GB RAM, Windows 11. Performance target: 20–30 fps or better on this laptop.
- So: no Lumen, no Nanite, no virtual shadow maps, no hardware ray tracing. Use static or stationary lighting, cascaded shadow maps, scalability Medium/Low, screen percentage around 70–80 %, and LODs plus HLOD/merged actors for the city.
- Installed: Blender 5.2 (`C:\Program Files\Blender Foundation\Blender 5.2\blender.exe`), Git LFS 3.7, Epic Games Launcher. Not yet installed: Unreal Engine 5 and Visual Studio 2022 with the "Game development with C++" workload. Record the Unreal version here once it is chosen.

## How to work (an agent cannot click through the editor reliably)
- Game code in **C++** (text you can edit and review), not Blueprint graphs. Blueprints only as thin data or asset wrappers.
- Automate asset work with scripts: Blender Python run headless (`blender -b -P tools/....py`) to make `.blend`/`.fbx`/`.glb` files from the reference data, and Unreal Python editor scripts (`UnrealEditor-Cmd.exe <project> -run=pythonscript -script=...`) to import and place them. Keep the scripts in `tools/` so every asset can be rebuilt.
- Build from the command line (UnrealBuildTool / RunUAT). Check your work with screenshots and logs before telling the user something works.
- When something needs the user's hands (installing, clicking in the editor, testing the DualShock 4), ask for **one step at a time**.
- Input: PS4 DualShock 4 over USB or Bluetooth is the main controller (Unreal does not see it as an Xbox pad by default: use the Windows RawInput plugin or Steam Input, and test it), plus keyboard and mouse.

## User preferences (carried over)
- Ask **one question at a time**; when asking about something visual, show the image in the same message (absolute local path in Markdown).
- Build and maintain the project; do not just hand the user instructions.
- Write decisions into files as you go (this file, `README.md`, `docs/`) so a new session can continue.
- Commit messages: very simple human language with bullet points.
- Clean up disposable test files after use.
