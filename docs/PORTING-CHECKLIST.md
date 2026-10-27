# Porting checklist – what the Unreal version must do

Everything below exists in the web game (`reference/web-game/`) and works the same in Unreal. Each item names the
Unreal class or asset and how it was verified. "Tests" = automation tests in `Source/Haarklous/Private/Tests`
(`tools\run-tests.ps1`, compared against answers recorded from the web code); "play-through" = scripted run with a
simulated DualShock 4 (`tools\run-game.ps1 -Test <name>`, `HKTestScripts.cpp`), screenshots in `tools\out\shots\<name>`.

## World and coordinates
- [x] World units = metres of the apartment model, y up, converted in one place. — `HKMath.h` (`HK::ToUE`, `QuatToUE`,
  `TransformToUE`); test `Haarklous.Core.Math`; Blender→FBX→Unreal axis chain checked with a marker mesh.
- [x] Local map frame (`toWorld`, `toLocal`, theta, offsets; stair door 84.9 m = y −9.975). — `FHKData::ToWorld/ToLocal`;
  test `World.TerrainAndFrame` (200/200 identical).
- [x] Terrain from the Kartverket grid, walkable ±540 m. — `FHKData::HeightWorld` (3000/3000 heights identical to the
  web); terrain meshes `SM_world_near__root_t*` / `SM_world_far__root_t*`; ground material `M_HK_Ground` (grass,
  asphalt, gravel, sand blended by the map's surface mask, tinted by the web's map painting).
- [x] Buildings from OSM (brick with hip roofs, plaster, sheds, own building cut around the flat). — exported web geometry
  rebuilt in Blender (`tools/blender/build_assets.py`), 100 m city tiles; screenshots `shots/08-aerial-home`, `09-aerial-district`.
- [x] Streets, paths, parks, playgrounds, courtyard, lamps, trees and bushes. — ground mask (`exporter-ground.js`),
  courtyard props in the city tiles, trees and bushes as instanced Blender models (`foliage()` in `build_assets.py`,
  same positions and sizes as the web; `Content/Data/instances`), `AHKGame::SpawnTrees`.
- [x] Sky, sun direction, distant hills, Oslofjord. — `build_level.py`: stationary sun along `sunDir`, sky atmosphere,
  clouds from the web sky's formula (`M_HK_Clouds`), hills and fjord from the web's far scene (horizon cut out over the district).

## Player (`AHKGame::UpdateFoot`, `FHKWalkWorld`)
- [x] First person; walk 1.45 / run 3.6 m/s, crouch and prone, jump 3.1 m/s. — `HKGame.cpp` (port of `updateFoot`);
  play-through `walkhome` (street door → 4 storeys → flat, feet 0.00 m on the 4th floor).
- [x] Look sensitivity (controller and mouse), saved. — `AHKGame::SetSens`, `Saved/Haarklous/settings.ini`; play-through
  `menus` (sliders dragged with the mouse to 1.70× and 0.80×, saved to settings.ini; mouse look turns −0.0024 rad per pixel).
- [x] Collision: walls, steps 0.36 m, doors as moving obstacles. — `FHKWalkWorld` (port of `walk-collision.js`): test
  `World.WalkCollision` – 2500 moves (largest difference 0 m), 3000 stand/support/ceiling checks, 2000 hit checks,
  150 spawns, 483 boundary pieces all identical; doors in `UpdateDynamic`.
- [x] Drunk effect (+30 s per beer or sip; blur, double vision, sway, drift). — `M_HK_Drunk` post-process + sway/drift in
  `UpdateFoot`; play-through `walkhome` (beer → 30 s).

## Apartment and stairwells
- [x] The 4th-floor flat exactly as `apartment-data.js` (frozen kitchen and bedroom), doors, lights. — exported web
  geometry (`SM_apartment__*`), rig `apartment` (doors, drinks), static lamps with the web's values; screenshots
  `walkhome/04-hall`, `05-beer`.
- [x] Six beers on the worktop, aquavit (12 sips). — `AHKGame::Drink`; play-through `walkhome`.
- [x] Own stairwell: street door opens, stairs to the 4th floor, flat doors locked except the own. — rig `stair_own`;
  play-through `walkhome` (prompts "□ Open street door", "□ Open entrance door").
- [x] Eight neighbouring stair towers with locked flat doors. — rigs `stair_b0..7`, `Towers`, `TowerAt`.

## Vehicles (`HKVehicles`, `HKAircraft`)
- [x] Honda CBF600S: lean, 6 gears, bars and wheel, hands, pillion. — `FHKMotorcycle`; tests `Vehicles.Motorcycle*`
  (all values within 5e-10 of the web); play-through `vehicles` (50 km/h in 2nd gear after 3 s).
- [x] Cars (7 colours): bicycle steering, LHD wheel ≤ 55°, hands at ten and two, passenger seat. — `FHKCar`; tests
  `Vehicles.Car*`; play-through `vehicles`.
- [x] Excavator: tracks, ISO joysticks, digging and piles. — `FHKExcavator`; test `Vehicles.Excavator`; play-through `vehicles`.
- [x] Helicopter and two-seat plane: Mode 2, spool-up, crashes, chase camera, cockpit hands, passenger seat. —
  `FHKHelicopter`, `FHKPlane`; tests `Vehicles.Helicopter*`, `Vehicles.Plane`; play-through `vehicles` (plane lifts
  off at 92 km/h after 86.9 m; web: about 87 km/h after 76–87 m).
- [x] FPV drone (hold △): angle and acro, battery, OSD, works indoors. — `FHKDrone`; tests `Vehicles.Drone*`;
  play-through `activities` (`06-drone` with the OSD).
- [x] Vehicle list in the web's order (5 cars, bike, excavator, helipad, airstrip, seed 1707: 25 cars + 7 bikes). —
  `HKVehicles::CreateAll`; test `Vehicles.List` (all 41 identical to the web).
- [x] □ in/out (stopped / landed), engine sounds. — `AHKGame::TryDrive/ExitVehicle` (`HKGameModules.cpp`); play-through `vehicles`.

## Activities
- [x] Disc golf: two ~70 m par-3 holes. — `FHKDiscGolf`; test `Minigames.DiscGolf` (51,524 + 102,653 checks identical);
  play-through `activities`.
- [x] Petanque against Arne: rules, scatter, score HUD. — `FHKPetanque`; test `Minigames.Petanque` (231,507 checks);
  play-through `activities`.
- [x] Hostage rescue mission: ring at (4.8, 14.2), hostage, 3–5 hostiles, pistol and health HUD only in the mission,
  Leave keeps you where you are. — `FHKMission`, `AHKMissionFx`, viewmodel; tests `Mission.*` (three recorded web
  missions replay identically, 1,770 frames); play-through `mission`.
- [x] Firepit at (−2.7, 34.7): benches, beer, woodpile, fire levels. — `FHKFire`, `AHKFireEffects`; tests `Fire.*`;
  play-through `activities`.
- [x] Photo camera with gallery, gear menu, wristwatch (PC time), map without vehicles. — `HKGameExtras.cpp`;
  play-through `activities` (`01-watch`, `02`–`04` camera and gallery, `05-gear-menu`, `07-map`).
- [x] Music and sounds, the same as the web. — `HKSynth` (Web Audio engine in C++), `HKAudioImpl`, `HKMusicPlayer`;
  tests `Audio.*` (71/71 sounds match browser renders, music loops 0.00 dB from Chrome).

## Online (built, off by default; tested only against `haarklous-ue5-test`)
- [x] Lobby client with the same protocol, own code, join by code, up to 5 players. — `FHKNet`; tests `Net.*`
  (wire JSON byte-identical, 219/219 session steps identical); live tests on `wrangler dev` and the deployed test server
  (play-through `online`: the real game and the web game in one lobby, twice in a row, 0 failures).
- [x] Remote avatars with name tags and bubbles, smoothed vehicles, one driver and one passenger, shared doors and fire. —
  `FHKMultiplayer`, `HKAvatars`, `AHKOnline`; play-through `online` on the test server: the web player's avatar seen
  walking (up to 1.57 m/s) and seen by the web, Unreal rides in the web player's car (8.0 m), the web player rides
  pillion on Unreal's motorcycle (avatar 0.000 m from the seat), a door opened on each side, a log on the fire from
  each side (same level); test `Net.AvatarPoses`.
- [x] Chat and voice. — chat in `HKGameExtras.cpp`; voice = the web's own `game-voice.js` in a hidden browser page
  (`FHKWebVoice`, `Content/Web/voice`). Play-through `online`: chat both ways, voice both ways (test tone heard by the
  web at RMS 0.21, the web's fake microphone heard at RMS 0.07); tests `Net.Voice`, `Net.VoicePage`; quick chat offline
  in play-through `activities`.
- [x] Settings: lobby code, join by code (controller code pad), name, voice. — `AHKOnline` pause-menu section;
  play-through `menus` with `-HKOnline` on the test server: own code shown, △ code pad joins BAAA, □ back home, typed
  code "cdef" + Enter joins CDEF, "Back to my own lobby" button, name field, voice checkbox turns the microphone on.

## Controls and screens
- [x] DualShock 4 (USB and Bluetooth) with the web mapping, plus keyboard and mouse. — `FHKInputProcessor` (raw HID
  reports, XInput fallback, keyboard/mouse like `readInput`); tests `Input.DualShock4Reports`, `Input.StandardMapping`;
  every play-through drives the game with a simulated DualShock 4; play-through `menus` uses keyboard and mouse
  (Enter starts, W walks 1.45 m/s, Shift + W runs 3.60 m/s, Space jumps, C crouches, Z prone, M map, Escape pause,
  mouse look and clicks). **Still to do by the user: a real DualShock 4.**
- [x] Start screen, pause/settings (sensitivity, music volume, graphics quality), credits. — `HKGameHud.cpp`;
  play-through `menus`: start screen, pause with Escape, D-pad ←/→ music volume ±0.1, sliders, graphics quality
  Balanced → Sharp (100 %) → Fast (65 %) → Balanced (75 %), settings saved to settings.ini, Resume; the map-data credits
  are on the start screen and in the pause menu. The web's "aerial imagery" option is not used: the ground has real
  materials instead.
