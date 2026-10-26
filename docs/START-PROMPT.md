You are starting a new project in `C:\Users\Lenovo\Documents\haarklous-ue5`: rebuilding my game "Haarklous online" in Unreal Engine 5, with 3D assets made in Blender, with better graphics.

The game today is a browser game (three.js + JavaScript), an open world around my apartment at Haarklous plass 17A, Torshov, Oslo. A read-only snapshot of it is in `reference/web-game/`, and it is the specification. Before anything else, read `AGENTS.md`, `README.md`, `docs/PORTING-CHECKLIST.md`, then `reference/web-game/README.md` and `reference/web-game/docs/HANDOFF.md`. You can play the reference by opening `reference/web-game/haarklous-game.html` in Chrome.

What I want:
- The whole game, in one go: every item in `docs/PORTING-CHECKLIST.md`, built and tested by you before you report back to me. Do not stop after a first part to ask for approval; work through the full checklist.
- Only the graphics get reworked: models, materials, lighting, sky, foliage, effects, animation, UI looks. Gameplay, controls, speeds, physics, rules, map, apartment layout, missions, minigames and menus must behave exactly like the web version. Take the numbers from the JavaScript. If you think something about gameplay should change, note it for me instead of changing it.
- Keep all online features (lobbies with codes, up to 5 players, synced players and vehicles, passengers, chat, voice, shared doors and firepit) so I can launch the game online later. The Unreal client must speak the same protocol as the existing server in `reference/web-game/cloud/`. Build this code, but leave it switched off and do not test it now.
- Keep everything offline: no Cloudflare at all (no deploying, no wrangler, no connecting to the live server or website), no GitHub, local git only.
- Never change anything in `C:\Users\Lenovo\Documents\haarklous-plass-ps-kontroller` (the live web game). Only read it.
- My PC is a laptop with an AMD Ryzen 5 5600H, integrated Radeon graphics and 14 GB RAM. It must run at 20–30 fps or better, so no Lumen, Nanite, virtual shadow maps or ray tracing.
- I play with a PS4 DualShock 4 (USB or Bluetooth), plus keyboard and mouse. The controller works with the web version, but I do not have it with me now. Build full controller support with the same button mapping as the web version, and test everything with keyboard, mouse and simulated controller input instead.

How to work and test:
- Write the game code in C++, not Blueprint graphs, and make assets with scripts (Blender Python run headless, Unreal Python editor scripts) kept in `tools/`, so everything can be rebuilt.
- Test everything yourself: automated tests for the logic (collision, vehicle physics, minigame rules, mission rules, menus) and scripted play-throughs that drive the game with simulated input, like `reference/web-game/tests/browser-harness.js` does for the web version. Check each feature with screenshots and logs, compare it with the web version's behaviour and numbers, and measure the frame rate in the busiest places (home, the park, the firepit, flying).
- Prefer scripts and the command line. Use computer use (controlling my screen) only for steps that exist only in a window, such as installers or first-time editor dialogs.
- If Unreal Engine 5 or Visual Studio 2022 (with "Game development with C++") is missing, tell me exactly what to install, then wait. Otherwise only ask me when you are truly blocked, one question at a time, with an image in the same message when it is about something visual.
- Keep `AGENTS.md`, `README.md` and the checklist up to date as you go so a new session can continue, and commit locally often with short, plain-language bullet-point messages.

When everything is done, give me one report: what was built, how each checklist item was tested (with screenshots), the frame rates, how to start the game, and anything that still needs me (testing the DualShock 4, and the online features once I decide to launch).
