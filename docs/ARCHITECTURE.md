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

### Realism pass (27 Oct 2026, `tools/unreal/materials_realism.py`, textures in `tools/blender/make_textures.py`)
The web material instances keep their names, slots and colours; their parents change to new masters:
- `M_HK_Facade` (`MI_facade`, the city blocks): brick at true size (`T_Brick_*`, 240 x 66 mm module), the web's own
  window layout read from its facade texture, painted frames, concrete sills, and glass that shows a fake room
  behind it (interior mapping: walls, floor, ceiling, doors, sofas, curtains and blinds varied per window) under the
  engine's sky reflection.
- `M_HK_Brick` (`MI_brick`, `MI_plinth`), `M_HK_RoofMetal` (`MI_roof`, standing-seam zinc), `M_HK_Plaster`
  (`MI_plaster_*`, keeps each colour), `M_HK_CarPaint` (opaque `MI_Physical_*`: clear-coat paint; `Color` is still set
  at run time by `AHKRigActor::SetPaint`), `M_HK_Foliage` (leaves let light through and sway in the wind, bark sways).
- 3D grass (its blades were replaced by photo grass cards in the photo-real pass below):
  `AHKGame::UpdateGrass` places clumps in 6 m tiles around
  the camera on 'grass' ground only (not in stair towers or the fire ring); off on quality
  Fast. One new tile per frame; tiles that fall out of range go to `GrassPool` and are reused (destroying them made
  the garbage collector stall a frame now and then). `-HKNoGrass` switches it off for frame-rate comparisons (it
  costs about 2-3 fps).
- Lighting: sun source angle 1.2 (soft shadow edges), sky light 1.35, sun glow in the height fog, reflection
  captures around home and in the flat and stairwell, AO 0.7 / 70 cm, slight colour grading, `r.Tonemapper.Sharpen`.
- Asphalt with hairline cracks and sealed patches; grass less saturated with dry patches, clover and soil.

### Photo-real pass (28 Oct 2026, `tools/unreal/materials_photoreal.py`, runs after materials_realism.py)
- **Library:** CC0 scans from Poly Haven and ambientCG (`python tools/fetch-library.py`, allowed by the user,
  `tools/out/library`), imported to `/Game/Haarklous/Textures/Lib`. Normal maps are OpenGL (green flipped on import).
- **Building map:** `python tools/make-building-map.py` rasterises every building footprint (web frame -> Unreal
  world cm) into `T_BuildingId` (2048 px, ~0.8 m, palette index, nearest, no mips) plus `T_BuildingPalette` (256 x 2:
  colour; style brick/plaster, roof slate/tile, frames brown/white, variant). Looks: OSM building:colour (salmon
  `#cb9c89` = brick, else plaster in that colour), roof:colour (red = clay tile) where tagged, else a Torshov mix per
  70 m cell. Materials sample it at the texel centre 60 cm inside the wall (roofs: at the pixel), shared samplers.
- `M_HK_Building` (`MI_facade`, and `MI_plaster_d9d2c3` with FacadeUVScale/Offset so the web's plaster buildings get
  windows too): scanned brick or plaster tinted by the map; windows of 1.0-2.0 m per 3 m bay chosen per column by a
  hash; glass 10-14 cm deep: parallax of the reveal in tangent space (side walls get their own normal), sun shadow
  of the reveal from `SkyAtmosphereLightDirection`; frames, mullions, sashes; interior mapping behind the glass.
- `M_HK_Plinth` (`MI_plinth`): concrete with recessed basement windows. `M_HK_Roof` (`MI_roof`): slate or clay tiles
  in rows along each face (uv from the world normal, world-space normal), bitumen on flat roofs.
  `M_HK_BrickPhoto` (`MI_brick`: stair towers, the flat's two brick walls) in the same salmon brick.
- **Details:** `tools/blender/build_details.py` builds balconies (steel-bar `M_HK_Railing` or panel fronts),
  downpipes, chimneys and kerb stones, and their placements `Content/Data/instances/details.json` (window bays of the
  web's walls, away from stair towers and from 17A; kerbs along drivable roads unless the surface map shows asphalt
  well beyond the edge). `AHKGame::SpawnTrees` spawns them like the trees; kerbs cast no shadow; view distances
  70-600 m. Kerbs and 3D grass sit on `FHKData::VisualHeightWorld` (the drawn terrain triangles), not the bilinear
  game height, which differs by a few cm.
- **3D grass:** `tools/make-grass-atlas.py` cuts five photographed tufts out of Poly Haven's grass_medium_01 atlas
  (alpha-coverage mips keep thin blades); `build_grass.py` makes 16-card clumps; `M_HK_GrassCards` colours them from
  the lawn scan under them. `PerInstanceFadeAmount` is 1 when fully shown (it sinks the cards near the cull distance).
  Tiles reach 15 m, fade 9-13 m.
- **Trees:** `tools/make-leaf-atlas.py` scatters ~1,100 scanned leaves (Poly Haven tree_small_02) over a card.
- **Sky photo:** `tools/make-sky.py` turns the CC0 HDR (sun at 38 degrees like the game's) into `T_SkyPhoto` (BC6H,
  no mips so the u wrap has no seam) and finds the turn (u0) that puts its sun on the game's sun azimuth;
  `tools/unreal/materials_sky.py` puts `M_HK_SkyPhoto` (unlit, "is sky": no fog or aerial perspective, sun capped)
  on the cloud dome and keeps the dome out of the sky light and reflection captures, so the lighting still comes
  from the sky atmosphere. Run it after build_level.py. (Volumetric clouds were tried: 3-5 fps, removed.)
- **Cars:** `tools/blender/car_model.py` builds a subdivided hatchback shell over the web car's profile (cage of
  cross-sections, arches over the web's wheels, glass/pillar/roof faces by band, a lining inside without glass) and
  five-spoke wheels (closed lathe solids, mirrored for the left side); `build_assets.py` swaps them for the web's
  exterior materials of SM_car__body and the wheel meshes, keeping the interior. Other vehicles get `soften()`:
  welded, 12 mm bevel on edges over 35 degrees, weighted normals.
- **Road paint:** `build_details.py` `markings()`: yellow centre dashes (3 m every 12 m) on tertiary and bigger
  roads, zebra crossings (0.5 m stripes) where footpaths cross them, pitched with the ground like the kerbs.
- **Look:** `tools/unreal/look.py` (sun 4.2 with light shafts and contact shadows, sky light 1.2, bloom, lens flares,
  SSR, AO, grading) is used by build_level.py and can be applied to the existing level without re-baking by
  `apply_look.py`. `AHKGame::ApplyQuality` switches SSR, light shafts, lens flares and contact shadows per quality
  (Balanced: SSR and lens flares; Sharp: all). Test switches: `-HKNoGrass`, `-HKNoDetails`, `-HKSkipDetails=`,
  `-HKCmds=cvar:value,...`.

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
