Rebuild my game "Haarklous online" in Unreal Engine 5, with 3D assets made in Blender, in the folder `C:\Users\Lenovo\Documents\haarklous-ue5` – same game, better graphics.

Start by reading `AGENTS.md`, `README.md`, `docs/PORTING-CHECKLIST.md`, `reference/web-game/README.md` and `reference/web-game/docs/HANDOFF.md`. The browser game snapshot in `reference/web-game/` is the specification (play it by opening `reference/web-game/haarklous-game.html` in Chrome).

Rules:
- Only the graphics change: models, materials, lighting, sky, foliage, effects, animation, UI looks. Gameplay, controls, speeds, physics, rules, map and coordinates, apartment layout (frozen kitchen and bedroom), mission, minigames and menus must behave exactly like the web version; take the numbers from the JavaScript. Ideas for gameplay changes go in the final report, not into the game.
- Game code in C++, not Blueprint graphs. Assets made by scripts in `tools/` (Blender Python run headless, Unreal Python editor scripts), so everything can be rebuilt. Prefer scripts and the command line; use computer use only for steps that exist only in a window.
- My laptop: AMD Ryzen 5 5600H, integrated Radeon graphics, 14 GB RAM. No Lumen, Nanite, virtual shadow maps or ray tracing.
- Controls: PS4 DualShock 4 (USB or Bluetooth) with the same mapping as the web version, plus keyboard and mouse. The controller is not here now, so test with keyboard, mouse and simulated controller input.
- Online: keep every online feature and the same protocol as the server in `reference/web-game/cloud/`, so the Unreal and web clients can play together. Builds start with online switched off. Test online only on a separate test server: copy `reference/web-game/cloud/` to `server/`, rename the Worker to `haarklous-ue5-test` (nothing else changes), try it with `npx wrangler dev`, then deploy it to my Cloudflare account (Wrangler is logged in on this PC).
- Never touch the live game: no changes, deploys or connections to the Worker `haarklous-online` or its website, and nothing in `C:\Users\Lenovo\Documents\haarklous-plass-ps-kontroller` (read only). No GitHub; local git only.
- If Unreal Engine 5 or Visual Studio 2022 with "Game development with C++" is missing, tell me exactly what to install and wait. Otherwise do not stop to ask for approval; only ask when truly blocked, one question at a time, with an image when it is about something visual.
- Keep `AGENTS.md`, `README.md` and the checklist up to date, and commit locally often with short, plain-language bullet-point messages.

Done when all of this is true:
1. Every item in `docs/PORTING-CHECKLIST.md` is built in Unreal and ticked, with the Unreal class or asset noted next to it.
2. Each item was tested by you: automated tests for the logic (collision, vehicle physics, minigame and mission rules, menus) and scripted play-throughs with simulated input (like `reference/web-game/tests/browser-harness.js`), checked with screenshots and logs against the web version's behaviour and numbers.
3. The game runs at 20 fps or more on this laptop in the busiest places (home, the park, the firepit, flying), measured and written down.
4. Online works on the test server `haarklous-ue5-test`: an Unreal client and a web client (served by the test Worker) in one lobby join by code, see each other move, chat, ride as each other's passenger, and share doors and the firepit. Voice is tested too; if voice that works with the web version is not practical in Unreal, everything else is still done and the options are explained in the report.
5. I get one final report: what was built, how each item was tested (with screenshots), the frame rates, how to start the game, and what still needs me (testing the real DualShock 4).
