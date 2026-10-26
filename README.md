# Haarklous UE5 (local exploration)

Rebuilding the look of **Haarklous online** – the open-world game around Haarklous plass 17A, Torshov, Oslo – in Unreal Engine 5 with assets made in Blender. Gameplay, controls and online features stay exactly as in the web game; only the graphics are reworked. Offline for now.

## Folders
- `reference/web-game/` – read-only snapshot of the web game (the specification): all its code, data (`assets/osm/` OpenStreetMap + Kartverket heights), the playable offline build (`haarklous-game.html`), docs and the multiplayer server (`cloud/`, kept for a later launch, not deployed from here). Snapshot of `main` at commit `d3d40ad` (26 Oct 2026).
- `blender/` – Blender source files (`.blend`) for models: apartment, stairwell, buildings, vehicles, characters, props.
- `unreal/` – the Unreal Engine 5 project (C++).
- `tools/` – scripts that rebuild assets from the reference data (Blender Python, Unreal Python) and `refresh-reference.ps1`.
- `docs/` – `PORTING-CHECKLIST.md` (everything the game must do, with where it lives in the web code) and `START-PROMPT.md` (the prompt that started this project).

## Not in the snapshot yet
Open pull requests in the web game at snapshot time (port them once merged, after refreshing the snapshot):
- #3 – web-only rendering fix for buildings and ground far from home (nothing to port; Unreal handles distance its own way).
- #4 – three hidden stashes with 30-second boosts (3× speed, 3× jump, bullet time).

Refresh after changes in the web game: `powershell -File tools\refresh-reference.ps1`.

## Setup still needed
1. Unreal Engine 5 (latest 5.x) from the Epic Games Launcher.
2. Visual Studio 2022 Community with the "Game development with C++" workload (the installed Visual Studio 2019 is not enough).
3. Then create the C++ project in `unreal/` (the agent will guide this, one step at a time).

## Licences of the data
Map data © OpenStreetMap contributors (ODbL); height data © Kartverket (CC BY 4.0). Keep these credits in the game.
