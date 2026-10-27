# Architecture of the Unreal version

The web game in `reference/web-game/` is the specification. The Unreal version runs **the same game logic, ported
line by line to C++**, and Unreal only does what the browser did for presentation: rendering, sound output, input
devices, windows and networking sockets. This is how "only the graphics change" is kept.

## Coordinates (one place only)
All game logic works in the **web frame**: metres, y up, three.js axes, the 4th-floor apartment floor at y = 0, the
street about y = −10. Numbers can therefore be copied from the JavaScript unchanged. `HKMath.h` converts:
`UE = (x, z, y) × 100` (cm, z up). A web quaternion (x, y, z, w) becomes `FQuat(−x, −z, −y, w)`; a web yaw is an
Unreal yaw of −yaw. Use `HK::ToUE`, `HK::QuatToUE`, `HK::TransformToUE`; never convert anywhere else.
Blender files use (x, −z, y) of the web frame (metres, z up), which the FBX import turns into the same Unreal frame.

## Data pipeline (everything rebuildable)
1. `node tools/web-export/export.mjs` runs `reference/web-game/haarklous-game.html` in headless Chrome with **all
   network blocked** (it never talks to a server) and writes
   - geometry of every model: `tools/out/web-export/<name>.json + .bin + textures/` (`exporter.js`, plus any
     `exporter-<module>.js` that a module port adds; they use `window.__hkExport.exportRig/keysOf`),
   - gameplay data: `unreal/Haarklous/Content/Data/game.json`, `surroundings.json`, `apartment.json`,
     `surface-classes.bin` (`game-data.js`),
   - reference answers for the C++ tests: `Content/Data/tests/*.json` (`reference-tests.js` and one
     `reference-<module>.js` per module; run one with `--only=tests --script=reference-foo.js`).
2. `node tools/web-export/make-rigs.mjs` turns geometry exports into **rigs** (`Content/Data/rigs/<rig>.json`: node
   tree with rest poses and mesh asset names) and a Blender build plan (`tools/out/build-plan/`). Extra rigs:
   `tools/web-export/rigs.d/<module>.json` = `[[rig, exportFile, levelStatic?], ...]`.
3. Blender scripts (`tools/blender/*.py`, run headless) build better-looking assets from the plan and export FBX;
   Unreal Python scripts (`tools/unreal/*.py`) import them, make materials and build the level.

## Look of the world (what changed from the web, on purpose)
- **Ground:** one material `M_HK_Ground` blends grass, asphalt, gravel and sand (textures from
  `tools/blender/make_textures.py`) by the web's surface classes and tints them with the web's own map painting.
- **Trees and bushes:** new Blender models (`foliage()` in `build_assets.py`) at the web's exact positions and sizes,
  drawn as instanced meshes (`AHKGame::SpawnTrees`, lists in `Content/Data/instances`).
- **Sky:** Unreal's sky atmosphere and height fog instead of the web's gradient dome; the web's clouds (same formula,
  baked into `T_Clouds`) on an unlit dome (`M_HK_Clouds`, no fog, as the web drew its sky without fog).
- **Horizon:** the web drew its far scene (18 km plane, hills, fjord) in a separate pass behind the district. In one
  Unreal scene the plane would cut through the district, so `horizon_with_hole()` cuts a hole over the district
  terrain whose rim follows the terrain's edge heights.
- **Lighting:** stationary sun along the web's `sunDir` with cascaded shadows, stationary sky light, baked indirect
  light (Lightmass, `tools/unreal/build-lighting.ps1`), static lamps with the web's values. No Lumen, Nanite, virtual
  shadow maps or ray tracing (integrated Radeon laptop).

## Core C++ (Source/Haarklous)
| File | What |
| --- | --- |
| `HKMath` | conversions, three.js Euler/quaternion/matrix maths (`FHKMat`), seeded generator `HK::FParkMiller` |
| `HKNode` | `FHKNode`/`FHKRig`: three.js-style scene graph in web space (position, rotation with order, quaternion, scale, visible, localToWorld ...). Ported code moves rig nodes exactly like the JS moved `Object3D`s |
| `HKCollision` | `FHKWalkWorld`: port of `walk-collision.js` (verified identical to the web code) |
| `HKData` | `FHKData::Get()`: exported world data, terrain `HeightWorld`, `ToWorld/ToLocal`, `SurfaceAt`, `TrafficDistance`, `Greenery`, the walk world and `VehicleWorld(radius)` |
| `HKVehicle` | base class of all vehicles and aircraft: what the main loop uses |
| `HKAudio` | sound API (`FHKAudio::Get().Sfx(...)`, `Engine(kind)`, `FHKMusic::Get().Play(track)`); silent until the audio module registers |
| `HKUi` | 2D drawing on the Unreal canvas (panels, text, meters, projection of web-space points); all HUD and menus use it |
| `HKRigActor` | shows an `FHKRig` with its meshes, copies node poses to Unreal every frame |
| `HKGame` | port of `game-main.js`: the frame loop, player, modes, doors, interaction, HUD, menus |

## Rules for module ports
- One module = `Public/HK<Module>.h` + `Private/HK<Module>.cpp` (+ `Private/Tests/HK<Module>Tests.cpp`). Keep the JS
  structure and names (UpperCamelCase), keep every constant, keep the order of operations and random-number calls.
- Dependencies on the rest of the game go through a context struct of `TFunction`s defined in the module's header
  (like the JS `ctx` objects), filled in by `AHKGame`. Modules do not include `HKGame.h`.
- Visuals: move rig nodes (`FHKNode`), never Unreal components directly, except for effects that have no web
  equivalent geometry (particles, lights) which live in the module's own actor class.
- HUD: a module that shows text draws it in `DrawHud(UCanvas*)` with `FHKUi`; `AHKHud` calls it.
- Tests: automation tests named `Haarklous.<Area>.<Name>` that compare against reference answers exported from the
  web code (same inputs, fixed time steps). Run `powershell -File tools\run-tests.ps1`.
- Build: `powershell -File tools\build-unreal.ps1` (builds and test runs share one lock, so several helpers can call
  them). MSVC quirk: in exported classes declare one static member per line.
- No Blueprint graphs. No changes in `reference/` or in the web game's own folder. Online code never points at the
  live server; only the test server `haarklous-ue5-test` is configured.
