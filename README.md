# Haarklous UE5 (local exploration)

Rebuilding the look of **Haarklous online** – the open-world game around Haarklous plass 17A, Torshov, Oslo – in Unreal Engine 5 with assets made in Blender. Gameplay, controls and online features stay exactly as in the web game; only the graphics are reworked. The live online game is never touched; online features are tested on a separate test server (`haarklous-ue5-test`).

## Folders
- `reference/web-game/` – read-only snapshot of the web game (the specification): all its code, data (`assets/osm/` OpenStreetMap + Kartverket heights), the playable offline build (`haarklous-game.html`), docs and the multiplayer server (`cloud/`, kept for a later launch). Snapshot of `main` at commit `d3d40ad` (26 Oct 2026).
- `blender/` – Blender files saved by the build scripts (`tools/blender/build_assets.py --blend`).
- `unreal/Haarklous/` – the Unreal Engine 5 project (C++ in `Source/Haarklous`, game data exported from the web version in `Content/Data`).
- `tools/` – scripts that rebuild everything: `web-export/` (Node + headless Chrome), `blender/` (Blender Python), `unreal/` (Unreal Python), build/test/run scripts, `refresh-reference.ps1`.
- `docs/` – `PORTING-CHECKLIST.md` (everything the game must do, with where it lives in the web code) and `START-PROMPT.md` (the /goal prompt that runs this project).

## Not in the snapshot yet
Open pull requests in the web game at snapshot time (port them once merged, after refreshing the snapshot):
- #3 – web-only rendering fix for buildings and ground far from home (nothing to port; Unreal handles distance its own way).
- #4 – three hidden stashes with 30-second boosts (3× speed, 3× jump, bullet time).

Refresh after changes in the web game: `powershell -File tools\refresh-reference.ps1`.

## Tools (installed on this PC)
Unreal Engine 5.8.4, Visual Studio 2022 with "Game development with C++", Blender 5.2, Node 24, Python 3.12, Chrome.

## How it is built (everything from scripts)
See `docs/ARCHITECTURE.md`. In short:
- `powershell -File tools\build-assets.ps1` - export the web game's geometry and data (headless Chrome, network blocked), make rigs, build FBX files and ground textures in Blender, import them into Unreal, make the materials and build the level (`-From N` starts at a later step).
- `powershell -File tools\unreal\build-lighting.ps1` - bake the lighting (Lightmass).
- `powershell -File tools\build-unreal.ps1` - compile the C++ game.
- `powershell -File tools\run-tests.ps1` - automation tests (the C++ port against the web code's own answers).
- `powershell -File tools\run-game.ps1 -Test walkhome` - scripted play-through with a simulated DualShock 4; screenshots in `tools\out\shots\<test>`.
- Play: `UnrealEditor.exe unreal\Haarklous\Haarklous.uproject -game` (a packaged build comes at the end).

## Licences of the data
Map data © OpenStreetMap contributors (ODbL); height data © Kartverket (CC BY 4.0). Keep these credits in the game.
