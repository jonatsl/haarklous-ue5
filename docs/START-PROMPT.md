You are starting a new project in `C:\Users\Lenovo\Documents\haarklous-ue5`: rebuilding the graphics of my game "Haarklous online" in Unreal Engine 5, with 3D assets made in Blender.

The game today is a browser game (three.js + JavaScript), an open world around my apartment at Haarklous plass 17A, Torshov, Oslo. A read-only snapshot of it is in `reference/web-game/`, and it is the specification. Before anything else, read `AGENTS.md`, `README.md`, `docs/PORTING-CHECKLIST.md`, then `reference/web-game/README.md` and `reference/web-game/docs/HANDOFF.md`. You can play the reference by opening `reference/web-game/haarklous-game.html` in Chrome.

What I want:
- Only the graphics get reworked: models, materials, lighting, sky, foliage, effects, animation, UI looks. Gameplay, controls, speeds, physics, rules, map, apartment layout, missions, minigames and menus must behave exactly like the web version. Take the numbers from the JavaScript. If you think something about gameplay should change, ask me first.
- Keep all online features (lobbies with codes, up to 5 players, synced players and vehicles, passengers, chat, voice, shared doors and firepit) so I can launch the game online later. The Unreal client must speak the same protocol as the existing server in `reference/web-game/cloud/`.
- But keep everything offline for now: no Cloudflare at all (no deploying, no wrangler, no connecting to the live server), no GitHub, local git only. Build the online code but leave it switched off by default.
- Never change anything in `C:\Users\Lenovo\Documents\haarklous-plass-ps-kontroller` (the live web game). Only read it.
- My PC is a laptop with an AMD Ryzen 5 5600H, integrated Radeon graphics and 14 GB RAM. It must run at 20–30 fps or better, so no Lumen, Nanite, virtual shadow maps or ray tracing.
- I play with a PS4 DualShock 4 (USB or Bluetooth), plus keyboard and mouse.

How to work:
- Write the game code in C++, not Blueprint graphs, and make assets with scripts (Blender Python run headless, Unreal Python editor scripts) kept in `tools/`, so everything can be rebuilt and you can check it yourself with screenshots and logs.
- Unreal Engine 5 and Visual Studio 2022 (with "Game development with C++") are not installed yet. Guide me through installing them and creating the C++ project in `unreal/`, one step at a time.
- Ask me one question at a time. When it is about something visual, show the image in the same message.
- Keep `AGENTS.md`, `README.md` and the checklist up to date so a new session can continue. Commit locally with short, plain-language bullet-point messages.

First goal (a small vertical slice before anything else):
1. Get Unreal and Visual Studio installed, create the project with settings suited for my laptop, and get the DualShock 4 working.
2. Build the terrain and the buildings for the area right around Haarklous plass 17A from the reference data (same coordinates as the web game), with good-looking materials.
3. Walk around there in first person with the same speeds, jump, crouch, prone and look controls as the web game.
Then show me screenshots and the frame rate, and propose the order of the next steps from the checklist.
