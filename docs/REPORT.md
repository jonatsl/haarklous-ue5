# Haarklous online in Unreal Engine 5 – report

27 October 2026. The whole game is rebuilt in Unreal Engine 5.8 with assets made by Blender scripts. Every item in the
porting checklist is built and ticked; the game logic is a line-by-line C++ port of the web game, checked against the
web code's own answers; every feature was played through with a simulated DualShock 4; online play was tested
together with the web game on the separate test server, including voice with your real microphone. What still needs
you: a test with the real DualShock 4 (none was connected).

## How to start the game

- **From the project:** `"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" unreal\Haarklous\Haarklous.uproject -game -dx11`
  (from `C:\Users\Lenovo\Documents\haarklous-ue5`). Add `-fullscreen` for full screen.
- **Packaged build:** double-click `dist\Haarklous-UE5\Windows\Haarklous.exe` (Shipping build, 1.1 GB, made with
  `powershell -File tools\package-game.ps1`; checked by running the screenshot tour in it). `dist` is not in git;
  the script makes it again in a few minutes.
- Plug in the DualShock 4 (USB or Bluetooth) before starting; keyboard and mouse always work.
- Online is **off**. To try it, start with `-HKOnline` (it only knows the test server
  `wss://haarklous-ue5-test.server-smoke.workers.dev`). The live game is never contacted.

## What was built

The checklist (`docs/PORTING-CHECKLIST.md`) names the Unreal class or asset for every item. In short:

- **Game logic in C++, ported 1:1 from the JavaScript** (`unreal/Haarklous/Source/Haarklous`): player and collision
  (`FHKWalkWorld`), apartment, doors, drinks and the drunk effect, stairwells (`AHKGame`), motorcycle, cars, excavator,
  helicopter, plane and FPV drone (`HKVehicles`, `HKAircraft`), disc golf and petanque (`HKMinigames`), firepit
  (`HKFire`), hostage-rescue mission (`HKMission`), photo camera, gallery, watch, map, chat (`HKGameExtras`), sound
  and music (`HKSynth`: the web's Web Audio sounds rebuilt in C++), menus and HUD (`HKGameHud`), DualShock 4 by raw
  HID with the web's mapping plus keyboard and mouse (`HKInput`), online play (`HKNet`, `HKMultiplayer`, `AHKOnline`,
  voice through the web's own `game-voice.js`).
- **Same numbers as the web:** all speeds, physics constants, rules, coordinates and the vehicle list (seed 1707)
  are taken from the JavaScript. Nothing about gameplay was changed.
- **New graphics (made by scripts, rebuildable):** the web's geometry is exported from the web game in headless
  Chrome, rebuilt in Blender (`tools/blender`) and imported into Unreal (`tools/unreal`). New ground material
  (grass, asphalt, gravel and sand blended by the map), new 3D trees and bushes at the web's exact spots, a physical
  sky with the web's clouds, distant hills and the fjord, a sun with soft shadows, baked indirect light in the
  apartment, stairwells and streets, glowing lamps, fire with flames, sparks and smoke.
- **Laptop settings:** no Lumen, Nanite, virtual shadow maps or ray tracing; baked lighting (Lightmass), cascaded
  sun shadows, DirectX 11, screen percentage 75 % (graphics quality "Balanced"; "Fast" uses 65 %, "Sharp" 100 %).

![Start screen (same text and controls as the web)](img/shots--00-start-screen.jpg)
![Home: the street door of 17A with the motorcycle and the mission ring](img/shots--01-player-start.jpg)
![The firepit](img/shots--03-firepit.jpg)
![The living room (apartment layout frozen, only the finishes are new)](img/shots--05-apartment-living.jpg)
![The kitchen with the six beers](img/shots--06-apartment-kitchen.jpg)
![The stairwell](img/shots--07-stairwell.jpg)
![Torshov from the air](img/shots--08-aerial-home.jpg)
![The district, hills and fjord haze](img/shots--09-aerial-district.jpg)
![Sky and clouds](img/shots--11-sky.jpg)

## How it was tested

### Automated tests (the C++ port against the web code's own answers)

`tools\run-tests.ps1` runs 49 automation tests; **all 49 pass** (last run 27 Oct 2026). The reference answers were recorded by running the web game's own JavaScript in
headless Chrome with the same inputs and fixed time steps.

| Area | What is compared | Result |
| --- | --- | --- |
| Collision | 2,500 walking moves, 3,000 stand/support/ceiling checks, 2,000 hit checks, 150 spawns, 483 boundary pieces | identical (largest difference 0 m) |
| World | terrain heights (3,000 points), map frame, surface classes | identical |
| Vehicles | motorcycle, cars, excavator, helicopter, plane, drone driven with recorded inputs; vehicle list (41 vehicles) | within 5e-10 of the web |
| Minigames | disc golf (154,177 checks), petanque (231,507 checks) | identical |
| Mission | three recorded web missions replayed (1,770 frames) | identical |
| Sound | 71 sounds against browser renders, music loops | 71 / 71 match |
| Online | wire messages byte for byte, a 219-step lobby session | identical |
| Controls | DualShock 4 report parsing (USB and Bluetooth), keyboard and mouse mapping | as the web's standard mapping |

### Play-throughs with a simulated DualShock 4

Each one starts the real game (`tools\run-game.ps1 -Test <name>`), presses buttons and moves sticks like a player,
checks the game state and takes screenshots. The last full round (27 Oct 2026, after the final lighting bake) passed
every check in every play-through.

| Play-through | What it does | Result |
| --- | --- | --- |
| `walkhome` | street door, up four storeys to the 4th floor (feet at 0.00 m), entrance door, into the flat, a beer → 30 s drunk, a sip of aquavit (12 → 11 sips, +30 s) | 8 / 8 checks, 7 screenshots |
| `stairwells` | in each of the 9 stair towers (17A and the eight neighbours) a stranger's flat door offers only "□ Try the door" and answers "Locked" | 19 / 19 checks |
| `vehicles` | motorcycle (50 km/h in 2nd gear after 3 s), car (47 km/h), excavator (5.2 km/h), helicopter lift-off to 11 m and landing, plane lift-off at 92 km/h after 86.9 m (web: about 87 km/h after 76–87 m) | 9 / 9 checks, 8 screenshots |
| `activities` | watch, photo camera and gallery, gear menu, FPV drone, map, quick chat, log on the fire, sitting and a beer, disc golf, petanque | 11 / 11 checks, 12 screenshots |
| `mission` | prompt in the red ring, briefing, play, aim, fire, pause, leave | 3 / 3 checks, 6 screenshots |
| `drunk` | the drunk blur and double image at 30 s and 90 s | 7 screenshots, checked by eye |
| `menus` | keyboard and mouse: Enter starts, W walks at 1.45 m/s and Shift + W runs at 3.60 m/s (measured), Space jumps, C crouches, Z lies prone, mouse look, M map, Escape pause; pause menu: D-pad music volume, three sliders dragged with the mouse, graphics quality Fast/Balanced/Sharp (65/75/100 %), settings saved to settings.ini, Resume | 21 / 21 checks offline |
| `menus` with online on | the same, plus the lobby section on the test server: own code, controller code pad joins BAAA, □ back home, typing "cdef" + Enter joins CDEF, "Back to my own lobby", name field, voice checkbox turns the microphone on | 28 / 28 checks |
| `shots` | tour of the world for checking the graphics | 14 screenshots, checked by eye |

![Walking home: the street door prompt](img/walkhome--01-street-door.jpg)
![The motorcycle](img/vehicles--01-motorcycle.jpg)
![Helicopter, chase camera](img/vehicles--06-helicopter-chase.jpg)
![Plane, chase camera](img/vehicles--08-plane-chase.jpg)
![Wristwatch with the PC time](img/activities--01-watch.jpg)
![FPV drone with the on-screen display](img/activities--06-drone.jpg)
![Map](img/activities--07-map.jpg)
![Sitting by the fire after a beer (drunk effect on)](img/activities--09-firepit-sitting.jpg)
![Disc golf](img/activities--11-discgolf-flight.jpg)
![Petanque against Arne](img/activities--12-petanque.jpg)
![Hostage rescue: aiming](img/mission--03-aim.jpg)
![Pause menu with the lobby section (online, test server)](img/menus--04-lobby-section.jpg)
![The controller code pad](img/menus--05-code-pad.jpg)

### Online, on the test server only

The cloud server was copied unchanged to `server/` with only the Worker name changed to `haarklous-ue5-test`,
tested with `npx wrangler dev`, then deployed to your Cloudflare account at
`haarklous-ue5-test.server-smoke.workers.dev`. The live game `haarklous-online` was never touched or contacted.
The real Unreal game and the web game served by the test Worker joined one lobby by its code and ran a scripted
session (`tools
et-test
un-live.ps1 -Mode deployed -Game`) three times, each with 0 failures; the last run was on
the final build (27 Oct 2026, lobby BG28):

- **Join by code:** both in one lobby (G3M8, BNU7 and BG28 in the three runs).
- **Chat** both ways.
- **Doors:** the web opened a door and Unreal showed it open; Unreal opened one and the web showed it open.
- **Firepit:** one log from each side; both sides show the same fire level.
- **See each other move:** the web player walked 6.1 m and Unreal saw the avatar move at up to 1.57 m/s; Unreal
  walked and the web saw it.
- **Passenger, web drives:** Unreal got "□ Ride along with Web tester (passenger)", took the seat, and rode 8.0 m
  with the camera in the seat.
- **Passenger, Unreal drives:** the web player rode pillion on Unreal's motorcycle for 5.5 m (avatar exactly at the
  pillion seat on both sides).
- **Voice:** Unreal's microphone (a 440 Hz test tone) was heard by the web game (RMS 0.21, about 434 Hz), and Unreal
  heard the web's microphone (RMS 0.07). Voice uses the web's own `game-voice.js` in a hidden browser inside the game,
  so it is the same WebRTC voice as the web version.
- **Voice with your real microphone:** while you talked to the laptop, the game opened its microphone ("Mikrofonrekke
  (Realtek(R) Audio)") and the web game heard your voice (RMS 0.068, pass mark 0.05); the game heard the web side at
  0.205 (`run-live.ps1 -Mode deployed -Voice -RealMic`, lobby R83B, all 8 checks). This test also found and fixed two
  things: switching the microphone off (or quitting after using it) crashed the game, and the game's microphone had no
  automatic gain (the web asks the browser for automatic gain and noise suppression); it now has both a gain and a
  noise gate.
- **Pause-menu lobby controls** (play-through `menus` with online on, see above): the code pad, typing a code, going
  back home, the name field and the microphone checkbox all work against the test server. This run also found and
  fixed a real bug: Unreal's config reader cut the server address at the `//` in `wss://`, so online switched on in the
  config file (rather than on the command line) could not connect.

![Unreal sees the web player walking](img/net--A1-game-sees-web-walking.jpg)
![Unreal rides in the car the web player drives](img/net--B1-game-passenger-seat-web-driving.jpg)
![Unreal sees the web player on its pillion](img/net--C1-game-sees-web-on-pillion.jpg)
![The web game sees the Unreal player walking](img/net--web-deployed-A2-web-sees-game-walking.jpg)
![The web player on the pillion behind the Unreal rider (web view)](img/net--web-deployed-C3-web-pillion-behind-game-rider.jpg)

### Checklist, item by item

Every row of `docs/PORTING-CHECKLIST.md`, with what it is in Unreal, the tests that cover it (automation tests
`Haarklous.<name>`; play-throughs `tools\run-game.ps1 -Test <name>`), and a picture from those tests. All passed on
the final build.

| Item | Built in Unreal | Tested by | Picture |
| --- | --- | --- | --- |
| World units and the one conversion | `HKMath.h` (`HK::ToUE`, `QuatToUE`) | `Core.Math`; marker mesh through Blender → FBX → Unreal | ![](img/shots--08-aerial-home.jpg) |
| Local map frame (toWorld / toLocal) | `FHKData::ToWorld/ToLocal` | `World.TerrainAndFrame` (200/200 identical) | ![](img/shots--09-aerial-district.jpg) |
| Terrain, walkable ±540 m | `FHKData::HeightWorld`, `SM_world_*`, `M_HK_Ground` | `World.TerrainAndFrame` (3,000 heights identical) | ![](img/fps--fps-park.jpg) |
| Buildings from OSM | `SM_world_near_*` city tiles (Blender) | `shots` tour, `walkhome` | ![](img/shots--02-front-of-17A.jpg) |
| Streets, paths, parks, lamps, trees, bushes | ground mask, `AHKGame::SpawnTrees` | `shots` tour, `fps` | ![](img/shots--03-firepit.jpg) |
| Sky, sun, hills, fjord | `build_level.py`, `M_HK_Clouds`, `world_far` | `shots` (11-sky, 09-aerial) | ![](img/shots--11-sky.jpg) |
| Walk 1.45 / run 3.6 m/s, crouch, prone, jump 3.1 m/s | `AHKGame::UpdateFoot` | `menus` (1.45 and 3.60 m/s measured, jump, crouch, prone); `walkhome` | ![](img/walkhome--01-street-door.jpg) |
| Look sensitivity, saved | `AHKGame::SetSens`, settings.ini | `menus` (sliders, mouse look, file) | ![](img/menus--03-settings-changed.jpg) |
| Collision: walls, steps, doors | `FHKWalkWorld` | `World.WalkCollision` (all identical); `walkhome` up four storeys | ![](img/walkhome--03-entrance-door.jpg) |
| Drunk effect | `M_HK_Drunk`, sway in `UpdateFoot` | `walkhome` (+30 s), `drunk` (30 s, 90 s) | ![](img/drunk--03-drunk-90s.jpg) |
| The flat (frozen kitchen and bedroom), doors, lights | `SM_apartment__*`, rig `apartment` | `walkhome`, `shots` (living room, kitchen, bedroom) | ![](img/shots--05-apartment-living.jpg) |
| Six beers, aquavit (12 sips) | `AHKGame::Drink` | `walkhome`: beer → 30 s; aquavit 12 → 11 sips, +30 s | ![](img/walkhome--07-aquavit.jpg) |
| Own stairwell, street door, own flat door | rig `stair_own` | `walkhome` (street door, 4th floor, entrance door); `stairwells` (other flats locked) | ![](img/walkhome--02-door-open.jpg) |
| Eight neighbouring stair towers, locked flat doors | rigs `stair_b0..7`, `TowerAt` | `stairwells`: a locked flat door in each of the 9 towers, "□ Try the door" → "Locked" | ![](img/stairwells--01-locked-door.jpg) |
| Motorcycle | `FHKMotorcycle` | `Vehicles.Motorcycle*` (within 5e-10); `vehicles` (50 km/h, gear 2) | ![](img/vehicles--01-motorcycle.jpg) |
| Cars | `FHKCar` | `Vehicles.Car*`; `vehicles` (47 km/h) | ![](img/vehicles--03-car.jpg) |
| Excavator | `FHKExcavator` | `Vehicles.Excavator`; `vehicles` (5.2 km/h) | ![](img/vehicles--04-excavator.jpg) |
| Helicopter and plane | `FHKHelicopter`, `FHKPlane` | `Vehicles.Helicopter*`, `Vehicles.Plane`; `vehicles` (lift-off, landing) | ![](img/vehicles--08-plane-chase.jpg) |
| FPV drone | `FHKDrone` | `Vehicles.Drone*`; `activities` | ![](img/activities--06-drone.jpg) |
| Vehicle list (seed 1707) | `HKVehicles::CreateAll` | `Vehicles.List` (41 identical) | ![](img/shots--04-helipad.jpg) |
| □ in/out, engine sounds | `TryDrive/ExitVehicle`, `HKAudioImpl` | `vehicles`; `Audio.*` | ![](img/vehicles--05-helicopter.jpg) |
| Disc golf | `FHKDiscGolf` | `Minigames.DiscGolf`; `activities` | ![](img/activities--11-discgolf-flight.jpg) |
| Petanque against Arne | `FHKPetanque` | `Minigames.Petanque`; `activities` | ![](img/activities--12-petanque.jpg) |
| Hostage-rescue mission | `FHKMission`, `AHKMissionFx` | `Mission.*` (3 web missions replayed); `mission` | ![](img/mission--03-aim.jpg) |
| Firepit | `FHKFire`, `AHKFireEffects` | `Fire.*`; `activities` (log, sit, beer) | ![](img/activities--09-firepit-sitting.jpg) |
| Photo camera, gallery, gear menu, watch, map | `HKGameExtras.cpp` | `activities`; `menus` (M map) | ![](img/activities--04-gallery.jpg) |
| Music and sounds | `HKSynth`, `HKMusicPlayer` | `Audio.*` (71/71 match the browser) | (sound) |
| Lobby client, own code, join by code | `FHKNet` | `Net.*`; `online` with the web game; `menus` online | ![](img/menus--04-lobby-section.jpg) |
| Remote avatars, passengers, shared doors and fire | `FHKMultiplayer`, `HKAvatars`, `AHKOnline` | `online` (both ways) | ![](img/net--B1-game-passenger-seat-web-driving.jpg) |
| Chat and voice | chat in `HKGameExtras.cpp`, `FHKWebVoice` | `online` (both ways), `Net.Voice`, real-microphone voice test (your voice heard by the web game); `activities` quick chat | ![](img/net--C1-game-sees-web-on-pillion.jpg) |
| Lobby settings: code pad, name, voice | `AHKOnline` pause-menu section | `menus` online (code pad, typed code, home, name, microphone) | ![](img/menus--05-code-pad.jpg) |
| DualShock 4 plus keyboard and mouse | `FHKInputProcessor` | `Input.*`; simulated DualShock 4 in every play-through; `menus` keyboard and mouse. **Real controller: you.** | ![](img/menus--02-pause.jpg) |
| Start screen, pause, settings, credits | `HKGameHud.cpp` | `menus` (start, pause, sliders, quality, saved, Resume) | ![](img/shots--00-start-screen.jpg) |

## Realism pass (27 Oct 2026)

Your follow-up goal: make everything look more realistic, still only graphics and still 20 fps or more. What changed
(details in `docs/ARCHITECTURE.md`, all made by scripts):

- **City blocks:** real brick at true size (240 × 66 mm bricks, mortar, worn edges, colour variation) instead of the
  web's painted brick; the web's own windows (same places) now have painted frames, concrete sills and glass with a
  fake room behind it, so each window has depth: walls, ceiling, floor, doors, sofas, curtains and blinds, varied
  from window to window, under real sky reflections.
- **Roofs** standing-seam zinc with streaks; **plaster** walls with fine grain (indoors clean paint); **plinths** in
  darker brick.
- **Grass:** a more natural lawn texture (dry patches, clover, soil) plus 3D grass tufts around you that sway in the
  wind; **asphalt** with hairline cracks and sealed patches.
- **Trees:** leaves let sunlight through and the trees sway in the wind.
- **Cars and the motorcycle:** clear-coat paint that reflects the sky and the buildings.
- **Light:** softer sun shadows, more sky light in the shade, a warm glow in the haze toward the sun, reflection
  captures, tighter ambient occlusion, gentle colour grading and image sharpening.

Cost: about 2–3 fps (the 3D grass) on top of the materials; all places stay above 20 fps (see below).

![Before: the front of 17A](img/before-realism--02-front-of-17A.jpg)
![After: real brick, windows with rooms behind them, frames and sills](img/shots--02-front-of-17A.jpg)
![Before: the street door](img/before-realism--01-player-start.jpg)
![After: glass with reflections and curtains, brick close up](img/shots--01-player-start.jpg)
![Before: the firepit](img/before-realism--03-firepit.jpg)
![After: lawn with dry patches and 3D grass tufts](img/shots--03-firepit.jpg)
![Before: by the helipad](img/before-realism--04-helipad.jpg)
![After: clear-coat car paint, brick and windows](img/shots--04-helipad.jpg)
![Before: Torshov from the air](img/before-realism--08-aerial-home.jpg)
![After: zinc roofs and brick from the air](img/shots--08-aerial-home.jpg)

## Frame rates on this laptop

Measured on 27 Oct 2026, after the realism pass, in **the packaged game you start**
(`dist\Haarklous-UE5\Windows\Haarklous.exe -hktest=fps -fullscreen -ResX=2240 -ResY=1400`, results in
`tools\out\shots\fps-packaged\fps.txt`): full screen at the laptop's own resolution (2240 × 1400), graphics quality
"Balanced" (the default, 75 % screen percentage), baked lighting, 3D grass on, nothing else running. At each place
the camera turns one full circle in 12 seconds, so every direction counts. Two runs:

| Place | Average (run 1 / run 2) | Slowest 1 % of frames | Slowest single frame |
| --- | --- | --- | --- |
| Home (apartment, looking into the living room and out of the windows) | **37.3 / 35.8 fps** | 28.7 / 28.3 fps | 23.3 / 18.7 fps |
| Firepit (fire, smoke, cars, grass and the block behind) | **29.7 / 29.9 fps** | 22.7 / 23.4 fps | 18.0 / 20.8 fps |
| The park (Torshovdalen, the most trees and grass) | **29.8 / 29.7 fps** | 23.1 / 23.2 fps | 20.8 / 18.9 fps |
| The street outside 17A | **30.0 / 30.1 fps** | 23.7 / 21.9 fps | 20.5 / 19.8 fps |
| Flying (110 m above the district) | **33.8 / 34.1 fps** | 26.4 / 27.4 fps | 15.2 / 18.0 fps |

Every place stays above the 20 fps goal on average and in the slowest 1 % of frames; single frames dip to 15–19 fps
now and then. Before the realism pass the same test gave 32–40 fps on average (home 39.5, firepit 32.1, park 32.2,
street 31.8, flying 35.3), so the new look costs about 2–3 fps. The editor build
(`tools\run-game.ps1 -Test fps -Res 2240x1400 -Fullscreen`) gives the same picture: home 36.5, firepit 30.2, park
30.2, street 30.9, flying 34.4 fps. An earlier realism build had one 8 fps stall in the park when the grass around
the camera was rebuilt; the grass tiles are now reused instead of thrown away, and that stall is gone.

The lower numbers in the online test (about 16–20 fps) were measured while a second game, the web version in a
headless Chrome that draws in software, ran on the same laptop to play against; they are not the game's own speed.
The "Fast" (65 %, no shadows) and "Sharp" (100 %) settings were not measured.

![Frame-rate test: the park](img/fps--fps-park.jpg)
![Frame-rate test: home](img/fps--fps-home.jpg)
![Frame-rate test: flying](img/fps--fps-flying.jpg)

## What still needs you

- **Test with your real DualShock 4** (USB and Bluetooth). The controller code reads the same raw reports the
  browser uses and is tested with recorded reports and a simulated pad, but not with the real controller.
- **No DualShock 4 was connected** while I worked (checked on 27 Oct 2026; you confirmed it is not here), so the real
  controller is still untested. When you have it: plug it in (or pair it), start the game and play; the mapping is the
  web version's. If a button does something unexpected, tell me which one.
- **Decide on the web-game bugs** in `docs/GAMEPLAY-IDEAS.md` (they are copied 1:1, as the rules say):
  petanque never ends after 13 points, petanque can freeze when no boule is left, and vehicle obstacles appear one
  update late. Fixing them is a small change in both games once you say yes.
- Web pull request #4 (hidden stashes with boosts) was not in the snapshot; refresh the snapshot when it is merged.
- Decide whether you want the web's "aerial imagery" checkbox back. The web only shows Esri aerial photos when it is
  online; Unreal draws real ground materials instead, so the option was left out. Bringing it back means downloading
  the photos while the game runs.

## Differences from the web that are only about looks

- The ground uses real materials instead of the web's painted map; the web's "aerial imagery" option (online Esri
  photos) is not used.
- Clouds come from the web's own sky formula but sit over a physical sky; the far hills and fjord are the web's
  shapes.
- The pause menu shrinks to fit the window (the web's panel scrolled instead), so Resume is always visible.
- Lamp globes do not cast shadows, the apartment rooms get soft fill light (the web used a hemisphere light that
  shines through walls), and the stairwell lamps are lit on every landing (baked) instead of one lamp following you.
