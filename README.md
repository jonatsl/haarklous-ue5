# Haarklous UE5 (local exploration)

Rebuilding the look of **Haarklous online** – the open-world game around Haarklous plass 17A, Torshov, Oslo – in Unreal Engine 5 with assets made in Blender. Gameplay, controls and online features stay exactly as in the web game; only the graphics are reworked. The live online game is never touched; online features are tested on a separate test server (`haarklous-ue5-test`).

## Folders
- `reference/web-game/` – read-only snapshot of the web game (the specification): all its code, data (`assets/osm/` OpenStreetMap + Kartverket heights), the playable offline build (`haarklous-game.html`), docs and the multiplayer server (`cloud/`, kept for a later launch). Snapshot of `main` at commit `d3d40ad` (26 Oct 2026).
- `blender/` – Blender files saved by the build scripts (`tools/blender/build_assets.py --blend`).
- `unreal/Haarklous/` – the Unreal Engine 5 project (C++ in `Source/Haarklous`, game data exported from the web version in `Content/Data`).
- `tools/` – scripts that rebuild everything: `web-export/` (Node + headless Chrome), `blender/` (Blender Python), `unreal/` (Unreal Python), build/test/run scripts, `refresh-reference.ps1`.
- `docs/` – `PORTING-CHECKLIST.md` (everything the game must do, with the Unreal class and test for each item), `REPORT.md` (the final report; HTML with screenshots in `docs/report/`), `ARCHITECTURE.md`, `ONLINE.md`, `AUDIO.md`, `GAMEPLAY-IDEAS.md` (web bugs and ideas that need your OK) and `START-PROMPT.md` (the /goal prompt that runs this project).
- `server/` – copy of the web game's cloud server with only the Worker name changed (`haarklous-ue5-test`), for online tests.

## Not in the snapshot yet
Open pull requests in the web game at snapshot time (port them once merged, after refreshing the snapshot):
- #3 – web-only rendering fix for buildings and ground far from home (nothing to port; Unreal handles distance its own way).
- #4 – three hidden stashes with 30-second boosts (3× speed, 3× jump, bullet time).

Refresh after changes in the web game: `powershell -File tools\refresh-reference.ps1`.

## How to start the game
- **Packaged build:** `dist\Haarklous-UE5\Windows\Haarklous.exe` (make it with `powershell -File tools\package-game.ps1`).
- **From the project (no packaging):**
  `"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" unreal\Haarklous\Haarklous.uproject -game -dx11`.
- Controls are the web game's: DualShock 4 (USB or Bluetooth, plug it in before starting) or keyboard and mouse.
- **Online is off.** To try it against the test server only: start with `-HKOnline` (server
  `wss://haarklous-ue5-test.server-smoke.workers.dev`, see `docs/ONLINE.md`). Never point it at the live game.

## Frame rates (28 Oct 2026, this laptop, 2240 × 1400 full screen, quality "Balanced")
Packaged game with the photo-real pass: home 31.8 fps, firepit 26.2, park 27.4, street 27.2, flying 29.6 (averages;
slowest 1 % of frames 21–27 fps, one flying run 17.9; before the photo-real pass 3–7 fps faster). Measure again: `dist\Haarklous-UE5\Windows\Haarklous.exe -hktest=fps
-hkout=<folder> -fullscreen -ResX=2240 -ResY=1400` (results in `<folder>\fps.txt`), or in the editor build
`powershell -File tools\run-game.ps1 -Test fps -Res 2240x1400 -Fullscreen`. Details: `docs/REPORT.md`.

## Tools (installed on this PC)
Unreal Engine 5.8.4, Visual Studio 2022 with "Game development with C++", Blender 5.2, Node 24, Python 3.12, Chrome.

## How it is built (everything from scripts)
See `docs/ARCHITECTURE.md`. In short:
- `powershell -File tools\build-assets.ps1` - export the web game's geometry and data (headless Chrome, network blocked), make rigs, build FBX files and ground textures in Blender, import them into Unreal, make the materials and build the level (`-From N` starts at a later step).
- `powershell -File tools\unreal\build-lighting.ps1` - bake the lighting (Lightmass).
- Realism pass: `tools/blender/make_textures.py` (brick, zinc roof, plaster, concrete, grass, asphalt),
  `tools/blender/build_grass.py` (3D grass clump), `tools/unreal/materials_realism.py` (facade with windows and rooms,
  brick, roof, plaster, car paint, foliage wind, grass); all run by `tools\build-assets.ps1`. See `docs/ARCHITECTURE.md`.
- Photo-real pass: `python tools/fetch-library.py` (CC0 scans), `tools/make-building-map.py` (each building's real
  look), `tools/make-grass-atlas.py`, `tools/make-leaf-atlas.py`, `tools/blender/build_details.py` (balconies,
  downpipes, chimneys, kerbs), `tools/unreal/materials_photoreal.py`; the light and camera look is
  `tools/unreal/look.py` (`apply_look.py` changes the level without re-baking), the sky photo (`tools/make-sky.py`,
  `tools/unreal/materials_sky.py`) the new car and tree crowns (`tools/blender/car_model.py`, `tree_model.py`, used by build_assets.py;
  `tools/blender/build_trees.py` + `tools/unreal/import_trees.py` rebuild only the crowns), and the 3D walls of the
  near district (`tools/blender/build_facades.py`, `tools/unreal/import_facades.py`; then bake the lighting). Also run
  by `tools\build-assets.ps1`.
- Construction site (28 Oct 2026, a new feature the user asked for): `python tools/make-site.py` (place, soil,
  machines, props -> `Content/Data/site.json`), `tools/blender/build_machines.py` (dozer, excavator, wheel loader,
  hauler, tipper, mobile and tower crane, site props), `tools/unreal/materials_site.py` (soil and machine materials,
  imports). See `docs/ARCHITECTURE.md` "Construction site".
- Realistic people (28 Oct 2026): MakeHuman (CC0) figures. `powershell -File tools\setup-people.ps1` once (MPFB add-on and
  asset packs into a private Blender profile), then with `BLENDER_USER_RESOURCES=tools\out\blender-user`:
  `blender -b -P tools/blender/build_people.py`, then `tools\unreal\run-python.ps1 tools\unreal\import_people.py`.
- Paintball course (28 Oct 2026): `python tools/make-paintball.py`, `python tools/make-signs.py`,
  `blender -b --factory-startup -P tools/blender/build_paintball.py`, `tools\unreal\run-python.ps1 tools\unreal\materials_paintball.py`
  (after `materials_site.py`); the yard is cut out of the terrain by `build_assets.py` (world_far). See
  `docs/ARCHITECTURE.md` "Paintball course".
- `powershell -File tools\build-unreal.ps1` - compile the C++ game.
- `powershell -File tools\run-tests.ps1` - automation tests (the C++ port against the web code's own answers).
- `powershell -File tools\run-game.ps1 -Test walkhome` - scripted play-through with a simulated DualShock 4; screenshots in `tools\out\shots\<test>`.
- `powershell -File tools\package-game.ps1` - packaged Shipping build in `dist\Haarklous-UE5\Windows\Haarklous.exe`.

## Licences of the data
Map data © OpenStreetMap contributors (ODbL); height data © Kartverket (CC BY 4.0). Keep these credits in the game.
Photo-scanned textures from Poly Haven (polyhaven.com) and ambientCG (ambientcg.com), both CC0 (public domain; no
credit required, credited anyway). People: MakeHuman assets (makehumancommunity.org, CC0) built with the MPFB 2 add-on.
