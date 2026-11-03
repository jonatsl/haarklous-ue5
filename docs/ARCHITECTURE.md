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
- **Horizon:** the web drew its far scene (18 km plane, hills, fjord) in a separate pass behind the district. Since the
  web fix for floating land (web PR #18, issue #14, 29 Oct 2026) the plane lies at the lowest terrain value - 2.5 m,
  under every bit of real ground, so it no longer needs a hole; `horizon_and_apron()` (build_assets.py) adds the web's
  apron: a ring from the district terrain's +-800 m edge (8 m ticks, the edge heights - 0.5) out to +3 km, easing to
  the plane's level (screenshots: `-hktest=places`, 14 and 15).
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
- **Trees:** `tools/make-leaf-atlas.py` scatters ~1,100 scanned leaves (Poly Haven tree_small_02) over a card;
  `tools/blender/tree_model.py` builds the crowns in the web's normalised crown space: leader, 5-7 limbs, side
  twigs, fill masses, 12-15 leaf cards per mass (normals blend crown and mass direction). `build_trees.py` +
  `import_trees.py` replace just the crown meshes (same asset names, so the web's placements stay).
- **Sky photo:** `tools/make-sky.py` turns the CC0 HDR (sun at 38 degrees like the game's) into `T_SkyPhoto` (BC6H,
  no mips so the u wrap has no seam) and finds the turn (u0) that puts its sun on the game's sun azimuth;
  `tools/unreal/materials_sky.py` puts `M_HK_SkyPhoto` (unlit, "is sky": no fog or aerial perspective, sun capped)
  on the cloud dome and keeps the dome out of the sky light and reflection captures, so the lighting still comes
  from the sky atmosphere. Run it after build_level.py. (Volumetric clouds were tried: 3-5 fps, removed.)
- **Cars:** replaced by the real car of the realism pass 2 below. Other vehicles get `soften()`:
  welded, 12 mm bevel on edges over 35 degrees, weighted normals.
- **3D walls:** `tools/blender/facade_model.py` lists the brick-block walls within 270 m of home (the web's near set) (not near stair
  towers, not shared with another footprint or 17A, at least 3.4 m) and builds them with window openings in the
  web's 3 m bays (u from the web's walls() order), sill 0.876 m / head 2.301 m per 2.85 m storey; vertex colour =
  the building's tint / 2.5 (alpha 1 = plaster) for `M_HK_Wall3D`; glass `M_HK_Glass3D` (interior mapping from
  uv0 room cell, uv1 window, vertex red = seed). `build_facades.py` writes 50 m tiles; `build_assets.py` drops
  the web's flat facade triangles on those walls from world_near; `tools/unreal/import_facades.py` makes the
  materials, imports (lightmap 512) and places static actors (also called by build_level.py). Re-bake after.
- **Sky light:** `make-sky.py` also writes `T_SkyAmbient.hdr` (the photo with its sun clipped at 12x the sky
  median); `materials_sky.py` makes it the sky light's specified cube map (turned like the dome). look.py no
  longer sets the sky light.
- **Gutters:** `build_details.py` puts a stretched half-round gutter along each eave of the hipped roofs.
  Cornices (stepped, 0.32 m out, casting shadow) along every wall top and concrete ledges on brick plinths,
  stretched per wall, overlapping at corners; skipped next to 17A's own stair tower.
- **Road paint:** `build_details.py` `markings()`: yellow centre dashes (3 m every 12 m) on tertiary and bigger
  roads, zebra crossings (0.5 m stripes) where footpaths cross them, pitched with the ground like the kerbs.
- **Look:** `tools/unreal/look.py` (sun 4.2 with light shafts and contact shadows, sky light 1.2, bloom, lens flares,
  SSR, AO, grading) is used by build_level.py and can be applied to the existing level without re-baking by
  `apply_look.py`. `AHKGame::ApplyQuality` switches SSR, light shafts, lens flares and contact shadows per quality
  (Balanced: SSR and lens flares; Sharp: all). Test switches: `-HKNoGrass`, `-HKNoDetails`, `-HKSkipDetails=`,
  `-HKCmds=cvar:value,...`.

### Realism pass 2 (2 Nov 2026: flicker fix, real car, real brick, 3D walls to 400 m)
The user: buildings "twitch" or flicker when you look at them; then make it look like real life within about 400 m
(Google Street View's vibe), real-looking buildings, cars that are copies of real cars with paint that glares.
- **Flicker (cause and fix):** the web draws its far scene in a separate render pass under the near one, and that
  far scene holds a copy of every building within NEAR_REACH (820 m) and every tree (surroundings.js:
  `targets = [farSet, nearCell]`). Unreal draws one scene, so the copies lay exactly on the near buildings (and on the
  3D walls near home, whose window openings they covered): the two surfaces fought over the same pixels as the
  camera moved. `build_assets.py drop_far_duplicates()` leaves out every world_far triangle that is also in the near
  export (37,478; matched by corners rounded to 2 cm), the district terrain under the walkable ground (71,672) and
  the far tree copies within 820 m (4,688 of 5,994 instances, `far_tree_kept()`). Cornices and plinth ledges
  overlap at outside corners: neighbouring pieces now differ by a few millimetres in depth, height and top
  (`build_details.py`), so equal faces no longer fight. Check: `-hktest=look` shots f1-f6 (each view three times,
  turned 0.1 degree and moved 3 cm).
- **Real car** (`tools/blender/car_model.py`): the web car's sizes (4.2 x 1.8 x 1.45 m, 0.31 m wheels at
  z +-1.3, x +-0.78) are a seventh-generation VW Golf's, Oslo's commonest hatchback, so the body is a Golf Mk7 in the
  web car frame: a cage of 58 cross-sections (floor, arch liners, sill, door, the sharp shoulder line, belt ledge,
  side glass leaning in, roof rail, roof / bonnet / tailgate deck) from its side, plan and section curves (`TOP`,
  `FLOOR`, `WIDTH`, `SHOULDER`, `BELT`, `RAIL`, `RAIL_W`), subdivided twice. The windows are cut into the subdivided
  surface along exact planes (`windows()`, `cut_windows()`: glass, a 2 cm rubber seal outside it, the windscreen and
  rear window's black frit inside, the black B pillar), so their edges are clean. Lights, grilles, badges (a plain
  ring, no maker's logo), reflectors, intakes and the panel gaps are decals: 2D outlines in a front, rear or side
  view, cut into small triangles and ray-cast onto the body 1.5-6 mm above it (`decal()`). Separate parts: door
  mirrors (bevelled housing, mirror glass, amber repeater), door handles, wipers, Norwegian number plates. Wheels
  (`golf_wheel()`): 205/55 R16 tyres with four tread grooves, a ten-spoke alloy, brake disc, bolts. The web's
  interior stays; its cabin pillar rods (which stood in the new windows) are dropped (`build_assets.car_parts`).
  Lighter bodies `SM_car__body_LOD1` (subdivided once, no gaps or lining) and `_LOD2` (the cage, lights only) are
  drawn by distance (`AHKRigActor`: the node mesh to 22 m, LOD1 to 60 m, LOD2 beyond; the commandlet has no static
  mesh editor subsystem to make them real LODs unless the script first calls `unreal.load_module('StaticMeshEditor')`,
  found later). The details stay on every level, so nothing fights when it
  switches.
- **Second car:** `tools/blender/car_id3.py`, a VW ID.3-style electric hatchback (nearly the Golf's footprint, very
  common in Oslo): its own curves, stations, windows and decals, built by the same code through
  `car_model.variant(car_id3.OVERRIDES)` as `SM_car__body_B` (+ `_LOD1`, `_LOD2`). `AHKRigActor::MeshVariant = "_B"`
  (set in `HKGameModules.cpp`) uses it for the cars whose web colour is silver, white or blue; the web's model number
  still only picks the colour, so nothing else changes.
- **Car materials** (`tools/unreal/materials_cars.py`, after materials_realism.py): `M_HK_CarPaint` (clear coat 1,
  clear-coat roughness 0.03 through a material-attributes node, metal flake amount from the colour, road dust on
  the lowest 30 cm), `M_HK_CarGlass` (tinted, lit as a surface, see-through from inside), `M_HK_CarPart` (all other
  parts: `MI_HK_Car*`). Number plates: `tools/make-car-textures.py` draws eight Norwegian plates (white, the blue band
  with the flag and N, Bahnschrift lettering) in one atlas; `AHKRigActor::SetPaint` gives each painted vehicle the
  next one in the web's vehicle order, so all players see the same plate on the same car.
- **Flicker in the flat, the stairwells, on the paintball course and the site** (the user, 3 Nov 2026):
  - The web settles some layered surfaces with polygonOffset (the flat's walls pushed back, game-apartment.js R())
    or draw order; Unreal ignores both. `tools/blender/coplanar.py` finds triangles of different materials that lie
    in one plane and overlap, and moves each material off the plane by 1.2 mm times its rank (the largest surface
    stays): build_assets.py runs it for the apartment, the stairwells and the firepit (514 / ~600 / 161 triangles).
  - Two of the flat's material slots (and one lamp material in each stairwell, one on the firepit) pointed at web
    material numbers that a re-export had moved (29 Oct): Unreal drew its default grey grid there, the "tiles" on the
    fridge and kitchen fronts. `tools/unreal/fix_material_slots.py` (after import_assets.py) repairs such slots by
    the key of the same type and colour, without touching geometry.
  - The Heras fence (`M_HK_SiteMesh`, also the paintball net) and the balcony railings (`M_HK_Railing`) drew their
    wires with hard `step()` edges: they aliased and shimmered. Now the wires are filtered by their size on screen,
    fade to their average cover when thinner than a pixel, and are dithered (interleaved gradient noise that moves
    every frame) so TAA turns them into a steady haze. The net and fence panels had a second face in the same place
    (the material is two-sided): removed.
  - The site's topsoil scan (Poly Haven farm_soil, a ploughed field) tiled into a shimmering barcode: now
    gravelly_sand.
  - The streaky bands on the site's soil seen from eye height (also in the 28 Oct screenshots) were the uvs, not the
    soil: Unreal keeps mesh uvs as half floats (11 significant bits) unless a mesh asks for full precision, and the
    procedural soil's uvs (local metres / 2.5, about 150) snapped to 1/8 of a tile, so rows of cells got no texture
    change and the texture smeared. `M_HK_Dirt` now takes its top projection from the world position like its side
    projections. The same snapping bent the textures of 310 imported meshes laid out in world metres (all near-world
    tiles, up to uv 698; the 3D facades, up to 238; the stairwells, 71; the site's gravel track; the paintball walls;
    the tower crane's deck): `tools/blender/uv_ranges.py` lists every mesh whose uvs pass 16, and
    `tools/unreal/full_precision_uvs.py` gives those past 32 full-precision uvs (step 7 of build-assets.ps1; the
    commandlet loads the StaticMeshEditor module for its subsystem). Found by a probe play-through: the bands stayed
    with the soil alone on screen and went away with a plain material. (The soil heights in site.json are whole
    centimetres above the terrain, a 1 cm staircase; smoothing it for drawing changed nothing, so it was left as is.)
  - The `fps` and `look` play-throughs looked at the paintball course and the site from their places before the move
    of 29 Oct (the paintball view was 2.7 m under the new yard): fixed.
- **Street parking** (3 Nov 2026, off unless `-HKParkedCars`, waits for the user's OK): `HKParking::Create` parks up
  to 220 `FHKCar`s nose to tail (5.3-7 m apart, 12 % gaps) along residential, tertiary, secondary, unclassified,
  service and living streets within 400 m, nearest first, with the web parking's checks (10 m from other roads, level,
  `CanStand`, clear of the web's vehicles, helipad, firepit and runway) plus 4 m clear of footpath crossings. On the
  asphalt by the kerb on streets 7 m or wider, else beside the street like the web's. They are not in `VehicleList`
  (online numbering unchanged): `AHKGame::ParkedCars`, still models (`AHKRigActor` synced once, no tick), footprints
  once into `ParkedObstacles`, which `UpdateDynamic` adds for walking and driving. Check: `look` with
  `-Extra "-HKParkedCars -hkout=..."` ("parked cars block walking").
- **Reflections:** 67 sphere reflection captures 2.5 m over the streets within 350 m (`build_level.py
  street_captures()`), so paint and glass reflect the houses and trees around them, not only the sky photo.
- **Brick:** Street View shows red-brown brick with dark, recessed mortar; the scan's mortar was light beige and
  OSM's colour for the blocks (#cb9c89) pale salmon, so walls looked pink. `tools/make-brick.py` darkens the joints
  (found as long valleys in the scan's height map) and spreads the bricks' colours; `BRICK_AVG` in
  materials_photoreal.py and facade_model.py is its new average. `make-building-map.py`: OSM-salmon blocks get
  `BRICK` (150, 98, 80); the mix for untagged blocks adds orange-red brick with white windows (`ORANGE`) and more red
  tile roofs (`TILE`).
- **Street View survey of the unknown blocks** (3 Nov 2026): 178 buildings within 400 m have no colour in OSM and
  were a dice roll, half of them brick. Each of their 54 blocks was looked at in Street View from the nearest street
  (47 clear views; spot, heading and the buildings in view worked out from `surroundings.json`): about 70 % are
  plaster, mostly pale yellow, sand, cream and yellow-ochre with white windows, some white functionalist blocks with
  blue or dark frames (Åsengata, Carl Jeppesens gate, Lilleborggata); 17 % red-brown brick (Sinsen, Knut Alvssons
  vei, Siloveien, Kyrre Grepps gate 19); 8 % new dark brick blocks. Even the block next to home, Sigurd Lies gate 17
  and 21, is yellow plaster (it was brick). `tools/street-view-looks.json` keeps each observation (place, style,
  colour, frames, building centres in the map frame, so a re-export cannot mix them up);
  `make-building-map.py` uses it, snapped to the nearest colour of the mix (the palette holds 255 looks), and the
  mix for the rest is now the one seen there. After changing it: build_details.py, build_facades.py, then
  materials_photoreal.py, import_facades.py, material_usage.py, full_precision_uvs.py, build_level.py,
  materials_sky.py, and bake the lighting. The yellow blocks first looked khaki: the cause was the cool colour grade
  (`look.py`, now white balance 6600 K), not the walls (a first guess that their colours clipped was wrong: the
  vertex colour holds tint / ColourScale; `facade_model.py` keeps a guard that never triggers today). Two looks the
  walls could not show: tan brick (Siloveien; a colour of the mix only the survey uses) and plaster over a red-brick
  ground floor (Nordkappgata 38, Agathe Grøndahls gate 48: `ground: brick` in the survey, vertex alpha 0.5,
  `M_HK_Wall3D` draws red brick up to the first floor's sills).
- **3D walls** out to 400 m (`facade_model.RADIUS`, 793 walls instead of 433), with weathering in `M_HK_Wall3D`
  (rain streaks under the sills, darker near the plinth, large soft stains). Bug fixed on the way: facade_model.py
  read the building palette with PIL, which Blender's Python lacks, so every 3D wall had the fallback pink (0.8, 0.6,
  0.55) whatever its building; make-building-map.py now writes each building's colour, frames and variant into
  building-looks.json, which facade_model reads. (Unreal passes vertex colours to the material as stored: no sRGB
  decoding.) Plaster blocks' balcony fronts are pale
  green frosted glass (`MI_HK_DetailPanel`).

## Construction site (new feature, 28 Oct 2026; not in the web game)
The user asked for one big construction site with dozers, excavators, wheel loaders, big trucks and two cranes that
drive like real machines, and soil that can be moved realistically.
- Moved on 29 Oct 2026 (issue #10, the same as web PR #12): 96 m south and 72 m west, so HODET N.N. can stand at its
  real place on the old north fence. Field x -160..-16, s 368..464; fence x -158..-18, s 371..461; gates where
  footway 97 crosses the fence (north x -120.0, south x -121.8); platform -29.97 m, grade 5 % / -1.5 %
  (`tools/make-site.py` DX, DS; the web's site-data.js is identical). The numbers below are the first place.
- Place (made bigger at the user's request, 28 Oct 2026: "140x90 meters"): a 140 x 90 m fence (local x -86..54,
  s 275..365) inside a 144 x 96 m soil field (x -88..56, s 272..368; world centre about (-178.1, -256.0)), on the far
  terrain's 8 m grid. It takes in the first 64 x 64 m site (x -80..-16, s 304..368) and the slope east of it. No free
  140 x 90 m area exists near home (a search over the map: every spot within 450 m has paths and some trees), so the
  field is cleared ground: `build_assets.py` (`drop_site_terrain`) leaves out the terrain triangles under it and every
  other triangle whose centre is on it (72 path and lamp triangles), `FHKData::Cleared` (the site's and the paintball
  yard's `rect`) leaves out the park's trees, bushes and their collision shapes there (also in `AHKGame::SpawnTrees`).
  `tools/make-site.py` writes `Content/Data/site.json`: the soil (0.25 m cells: surface, compacted level "firm"), a
  graded platform (a plane fitted to the ground, 5 % along x and -3 % along s) that meets the terrain over the outer
  12 m (cut banks up to 7 m on the east hillside, fill up to 8 m in the south-west), a 30 x 16 m foundation pit 3 m
  deep with a 1:6 ramp, five loose stockpiles, the machines' places, fence panels, two gates, site offices, toilet,
  signs, pallets, pipes, skip, floodlights and four crane loads.
- Soil (`HKSite`): `FHKDirtField` height field; tools cut (soil above an oriented rectangle's bottom) and deposit;
  loose soil slides to 34 degrees; volume is conserved (what is cut is carried until it is put down). `AHKSiteActor`
  draws it as a procedural mesh in 32 x 32-cell tiles (only changed tiles rebuilt; vertex colour: loose / subsoil);
  `M_HK_Dirt` is tri-planar (no smearing on pit walls). `FHKData::GroundWorld` = the soil inside the site, the terrain
  elsewhere (walking, placing, vehicles); `HeightWorld` is unchanged so the web-checked tests and the vehicle
  numbering stay the same.
- Machines (`HKSiteVehicles`, rigs `site_*` from `tools/blender/build_machines.py`): Cat D6-class dozer (hydrostatic
  tracks, blade cuts and pushes), 30 t excavator (ISO controls like the web excavator, backhoe bucket fills when the
  teeth drag through soil, spills when its opening turns down), articulated wheel loader (self-levelling bucket),
  articulated hauler and 8x4 tipper (tipping bodies), all-terrain crane (outriggers, slew, luff, telescope, hoist),
  tower crane (slewing inertia, trolley, hook pendulum); power, grip, rolling resistance by ground, slope, brakes and
  gearboxes. Machines come after the web's vehicle list, so web players simply do not see them.
- Way in and out (user, 28 Oct 2026: "there must be an entrance to the site so i can drive in and out"): two 10.5 m
  gates where the long valley path met the new fence, north (s0 side, local x 2.8) and south (s1 side, x -9.7), so
  the path now runs in through one and out through the other; each has its leaves swung open outwards, the site signs
  beside it and a 4.5 m gravel apron (`tracks` in site.json, one mesh `SM_Site_track` in world coordinates, drawn by
  `HKGameSite.cpp`) from the path outside to 10 m inside. The soil meets the ground outside within 0.3 m at both.
  The fence panels are walk and vehicle obstacles (`AHKGame::SiteObstacles`, added in `UpdateDynamic`), so the gates
  are the only way in for people and machines; the paintball yard's walls, net and cover stop vehicles too. The
  tipper waits just inside the north gate, facing out. (The first site had one gate on its north-west side.)
- Views (user, 28 Oct 2026: the crane was hard to see from, the cabs had bad visibility): R3 (keyboard V) cycles the
  view of a site machine (`AHKGame::SiteMachineView`, `Player.SiteView`): from the cab; from outside (behind and above
  along the operator's heading, the mouse turns it round the machine); for the two cranes also straight down from
  above the hook (between the hook and the jib, at most 16 m up, the jib's direction up the screen). Entering a
  machine starts in the cab. Cabs: glass roof in the mobile crane's operator cab, a low sloped dozer hood (blade
  visible), a higher loader cab with short low front fenders (the bucket's top edge visible), small tipper mirrors on
  arms; `-hktest=sitecabs` boards every machine and takes one screenshot from each seat.
- Cab glass: `M_HK_SiteGlass` is translucent, two-sided, lit as a surface (opacity 0.12 a face, dark tint, glossy),
  so you can see out of every cab and the site offices' windows (user, 28 Oct 2026: the opaque windows made the
  machines unusable from the cab).
- Test: `tools\run-game.ps1 -Test site` (each machine driven with the simulated pad; soil conserved; the tipper drives
  out through the gate and reverses back in; the fence blocks people and vehicles beside the gate; fps).

## People (28 Oct 2026)
- MakeHuman figures (CC0) made headless with the MPFB 2 add-on in a private Blender profile (`tools\setup-people.ps1`,
  `tools/blender/build_people.py`), imported by `tools/unreal/import_people.py`.
- Players: twelve people, one per player slot (lobbies hold 12): skeletal meshes
  `/Game/Haarklous/People/avatar_<slot>/SK_avatar_<slot>` (`FHKRig::Person`; the rig file stays avatar_<slot % 5>;
  slots 6-12 have their own sweater and name-tag colours in `HKAvatars.cpp`) (game_engine skeleton), rest pose
  = the web avatar rig with every joint at zero (arms 6 degrees out, relaxed hands). `AHKRigActor` drives the bones
  from the rig's joints (hips, torso, neck, head, shoulders, elbows, hips, knees) every frame and hides the rig's own
  body parts, so all web poses (walk, crouch, prone, sit, drive, ride, beer, drone) work unchanged. Sweater tinted
  with the slot's jacket colour, trousers the web's dark blue.
- Mission figures: static meshes in the web mannequins' poses replace the `body` meshes of `mission_hostage` (kneeling,
  hands on head) and `mission_hostile` (balaclava, red armband, pistol in both hands). Hit boxes are unchanged.
- Test: `tools\run-game.ps1 -Test people`.

## Paintball course (new feature, 28 Oct 2026; not in the web game)
The user asked for an urban paintball / reball course to play with friends, up to 6 v 6, normal paintball rules, team
deathmatch and capture the flag, about 60 x 40 m, with a lobby at the course (the first one in is the admin).
- Moved on 29 Oct 2026 (issue #10): into the valley floor the site left; yard x -72..-8, s 296..344 (floor -27.05 m),
  play area x -70..-10, s 298..338, gate x -51..-47, lobby x -61..-33, s 339..356 (`tools/make-paintball.py`; the
  web's paintball-data.js is identical). The numbers below are the first place.
- Place: the park valley next to the construction site; a 64 x 48 m paved yard (local x -112..-48, s 376..424, on the
  far terrain's 8 m grid, cut open by `build_assets.py`), one level at the median height with retaining walls; the
  60 x 40 m play area inside a 5 m net; the lobby on the lawn outside the gate (where the lawn is level with the yard).
  Two park trees inside the yard are left out. `tools/make-paintball.py` -> `Content/Data/paintball.json` (course
  coordinates u, v from the play area's centre; point-symmetric cover: containers, roofless building shells, walls
  with windows, jersey barriers, a car wreck, cable spools, sandbags, barrels, pallets, tyres; bases, spawn points and
  flag stands; collision boxes). Models: `tools/blender/build_paintball.py` (rig `paintball` + `paintball_parts.fbx`:
  lobby furniture, flags, marker, goggles, armbands, balls, splats); materials and import:
  `tools/unreal/materials_paintball.py`; sign textures: `tools/make-signs.py`.
- Rules and sessions (`HKPaintball`): members (lobby or game) with their join time; the admin is the earliest member
  (then the lowest id) and runs the game: mode (team deathmatch with respawns, team deathmatch in rounds, capture the
  flag), paint or reball, teams (wishes first, 6 a team, the rest balanced), countdown, time limit, score limits,
  flags. A ball that breaks on a player (or his marker) puts him out, also from his own team; bounces do not count
  (reball: every hit counts). Balls fly with gravity and air drag (90 m/s, half the speed after about 30 m),
  semi-automatic at most 10 a second, 200 in the hopper. Each player decides whether a ball hit him (his own view, so
  lag never puts anybody out unseen); the admin counts the score and moves the flags.
- Online: everything rides in the players' own 'st' messages (field `pb`): lobby time, team wish, game, alive,
  eliminations suffered and by whom, round, the balls fired since the last message, and the admin's game. The server
  relays 'st' unchanged and web players ignore the field. 6 v 6 needs 12 players a lobby: the TEST server's limit is
  12 (allowed by the user on 28 Oct 2026); the live server still has 5 (launching paintball online needs the same
  change there, the user's decision).
- Game glue (`HKGamePaintball.cpp`): the course rig, lobby furniture, flags, balls (instanced), paint splats (400 a
  team, a clean course each game), the marker in our hands, the gear of other players in a game (marker, goggles,
  team armbands); other players in a game hold the marker (`FHKPoseArgs::bMarker`). The yard is walkable ground
  (`FHKData::SiteGround` chain), its walls, net and cover block walking (`UpdateDynamic`). In a game R2 fires, L2 aims,
  □ reloads, hold △ leaves; in the lobby ↑↓ team, ←→ game (admin), △ paint / reball (admin), hold ✕ start (admin).
  Aiming (L2) zooms in and lowers the marker to the lower right, so the hopper stays under the sight line (user,
  28 Oct 2026: the gun hid the sight). A game with nobody left in it (all left with △ or went offline) ends after 3 s
  and the lobby opens again (`FHKPaintball::RunAdmin`, `EmptyT`; user, 28 Oct 2026: an empty game ran on until its
  time was up, and nobody could start a new one).
- Tests: `tools\run-game.ps1 -Test paintball` (five simulated friends through the same JSON, both modes, aiming, the
  empty game ending);
  `tools\net-test\run-paintball.ps1 -Mode local|deployed` (the real game with seven bot clients, `pb-bots.mjs`, on the
  test server: 8 in one lobby, a 4 v 4 game).

## Catching up with the web game (29 Oct 2026, GitHub issues #1-#14)
The web game ported this project's site, paintball and 12-player lobbies and added features of its own; issue #12 lists
them. The snapshot is web `main` at d4ec404; web PRs #18 and #19 (still open) were read from their branches.
- **Export** (`tools/web-export`): the web's copies of our site (fence, machines) and paintball course are left out of
  game.json and the geometry (`exporter.js`, `game-data.js`); game.json gets the obstacle kinds (tree, building, lamp,
  bush, shop ...), `stash`, `shops`, `sculpture` and `muselunden`; `exporter-places.js` exports the rigs `sculpture`
  (level), `stash`, `shops` (level, sliding doors as nodes; the product facings as one vertex-coloured mesh) and
  `muselunden`. Walk world obstacles: [apartment, towers, surroundings, firepit, stashes, shops, sculpture]; the
  vehicle worlds take all but the apartment and the stash crates (`FHKData::Load`).
- **Material numbers:** the web material keys carry export ids; a new export can move them (29 Oct 2026: +1 to +3).
  `import_assets.py` deletes and remakes the web material instances and textures when materials.json changed; the few
  keys named in scripts (car body, motorcycle tyre, terrain) were updated in `car_model.py` / `build_assets.py`.
- **Bigger world (#11):** walkable +-760 m (`playHalf`), terrain +-800 m; the web's outer terrain (300-760 m, 4 m) is
  in world_near with its own ground mask (`exporter-ground.js` near / outer / far, `materials_ground.py`); the city is
  tiled out to 1200 m; Lightmass importance +-1100 m, -70..+70 m high.
- **Muselunden (#11):** `FHKDiscGolf` takes a course (name, holes, rig); `FHKMinigames` has Torshovdalen, Muselunden
  (game.json `muselunden`, rig `muselunden`) and petanque; prompts, HUD and end text carry the course name; rewards
  par 50 / birdie 100 / eagle 200 kr (`FHKMinigamesContext::Reward`).
- **Street lamps (#2):** `SM_StreetLamp` (`build_details.py`: tapered pole, collar, shade, opal diffuser; the web's
  footprint and 5.1 m height) at all 389 lamp spots except the site and the yard, spawned like the details; the web's
  plain poles are dropped from world_near (`LAMP_KEY`). 359 lamp posts are solid (game.json, kind `lamp`). No light at
  night (the web's sun is fixed).
- **HODET N.N. (#3):** the web's sculpture (game-sculpture.js: bell, neck, head with the face, 28 stone steps) is
  level geometry from the export; `materials_places.py` makes its bronze; its ring and steps come with the walk world.
- **Stashes and boosts (#6):** `HKStash` (crates, items, 90 s cooldown, hit boxes) and `HKGameShops.cpp`
  (`StashPrompt`, `TakeBoost`, `BoostKind`/`BoostT`, `WorldScale()` = 0.35 in bullet time for parked vehicles, the
  fire, the site soil, the minigames and the mission's hostiles); speed x3 and FOV 82, jump x sqrt 3; the badge, the
  pulsing screen edge (`DrawShopsHud`), camera colour grading (`BoostLook`) and `FHKAudio::Muffle` (a low-pass after
  the master bus). Online: each player's boost is their own (it only changes their own movement and view).
- **Shops and money (#5):** `HKShops` (products, Kiwi prices, beer hours, `FHKWallet` saved in `Saved/HKWallet.json`,
  shop frames, door animation, the cashier's turning) and `HKGameShops.cpp` (prompts, basket, paying, the alarm, the
  bag, the bottle return, `Earn` with the coin sound; rewards from the mission 400 kr, disc golf and petanque). Shop
  lights are baked lamps (`build_level.py`); the cashiers are realistic people tinted in the shop colour
  (`AHKRigActor::TintPerson`). Aiming at items uses the exported hit boxes (`FHKHitBox::Ray`, in `LookTarget`).
- **Jumps (#4):** `FHKGroundVehicle::Settle` with gravity for cars and motorcycles (see the vehicle tests
  `CarJump`, `MotorcycleJump`, `Jump`). **Mouse flying (#13):** `AHKGame::FlightSticks` / `FlightLook`,
  `FHKInputFrame::bFreeLook`, `FHKVehicleInput::bHold` (play-through `mouseflight`, test `Vehicles.MouseHold`).
- **Fixes (#1, #7, #8, #9):** see `docs/GAMEPLAY-IDEAS.md`.
- **Checks:** `tools
un-game.ps1 -Test places` (every issue's in-game checks with screenshots), `mouseflight`,
  `vehicles` (a jump over the hump at local (392, -392)), the automation tests and `python tools/tests/test_site_geometry.py`.

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
| `HKRigActor` | shows an `FHKRig` with its meshes, copies node poses to Unreal every frame; drives the people's skeletons |
| `HKSite`, `HKSiteVehicles` | the construction site's soil and machines |
| `HKPaintball` | the paintball course: rules, sessions, balls, sync (`HKGamePaintball.cpp` shows it) |
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
