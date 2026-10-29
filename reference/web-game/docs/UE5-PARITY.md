# Parity with the Unreal version (haarklous-ue5)

Goal (user, 28 Oct 2026): this light HTML game has every feature, minigame and function of the Unreal project
`C:\Users\Lenovo\Documents\haarklous-ue5`, without Unreal and without changing the look of this game.

Checked against: the UE repo's commits up to `3eaf183` **and** its uncommitted work in progress. Its source and data
files were last changed at 21:24 on 28 Oct 2026, and nothing had changed when the tree was checked again at 21:30.
Sources read: `docs/PORTING-CHECKLIST.md`, `docs/REPORT.md`, `docs/ARCHITECTURE.md`, `docs/GAMEPLAY-IDEAS.md` and
`AGENTS.md`, plus the C++ in `Source/Haarklous` (`HKSite*`, `HKPaintball*`, `HKGame*`) and every uncommitted diff.

## 1. The UE checklist: features the Unreal version ported *from* this game

The Unreal project is a 1:1 port of this game's JavaScript (its snapshot `reference/web-game` = this repo at `d3d40ad`).
Each item below is therefore original here. The evidence was taken in the browser on 28 Oct 2026 (built game,
simulated DualShock 4, `window.haarklousGame`).

| UE checklist item | Here | Evidence |
| --- | --- | --- |
| Metres, y up; local map frame; Kartverket terrain; OSM buildings, streets, parks, lamps, trees, bushes; sky, hills, fjord | `surroundings.js`, `surroundings-data.js`, `game-main.js` | game loads, no console errors |
| First person: walk / run / crouch / prone / jump | `game-main.js` `updateFoot` | measured: walk 3.51 m/s (UE: 3.5), run 3.61 (3.6), crouch 0.80 (1.45 × .55) |
| Look sensitivity, saved | `game-main.js` `setSens` | pause-menu sliders |
| Collision: walls, 0.36 m steps, doors as moving obstacles | `walk-collision.js` | `tests/walk-collision.test.cjs`; fence test below |
| Drunk effect, six beers, aquavit 12 sips | `game-main.js` `drink` | drinks `beer ×6, liquor:12`; a beer → 30 s drunk |
| 4th-floor flat (frozen design), doors, lights | `game-apartment.js`, `apartment-data.js` | 15 doors; balcony-door prompt from 10 of 10 places |
| Own stairwell, eight neighbouring towers | `stairwell.js` | 9 towers; `tests/stairwell.test.cjs` |
| Honda CBF600S, cars, excavator | `game-vehicles.js` | 8 motorcycles, 30 cars, 1 excavator in the vehicle list |
| Helicopter, two-seat plane, FPV drone | `game-aircraft.js` | entered the helicopter (HUD shown); the drone launched |
| Vehicle list order, seed 1707 | `game-main.js` | unchanged; the site machines are appended after it |
| Disc golf, petanque | `game-minigames.js` | both present, `near()` prompts |
| Hostage-rescue mission | `case-game.js` | mounted; `tests/case-game.test.cjs` |
| Firepit | `game-fire.js` | fire level 1.84 |
| Photo camera, gallery, gear menu, wristwatch, map | `game-camera.js`, `game-watch.js`, `game-map.js` | map opens and closes |
| Music and sounds | `game-music.js`, `game-audio.js` | music API present |
| Lobbies (own code, join by code), avatars, smoothed vehicles, seats, chat, voice, settings | `game-net.js`, `game-multiplayer.js`, `game-avatars.js`, `game-voice.js`, `cloud/` | 2- and 3-player tests below |
| DualShock 4 + keyboard and mouse; start and pause screens | `gamepad-controls.js`, `index.html` | `tests/gamepad-controls.test.cjs`; simulated pad in every test |

## 2. Features only the Unreal version had (now ported)

| UE feature (source) | Here | Evidence |
| --- | --- | --- |
| Construction site: soil height field, digging, carrying, dumping, slumping to 34°, soil conserved (`HKSite`) | `game-site.js` `DirtField` | `tests/site.test.cjs` (edges, pit, conservation, repose, rock); in the game the excavator dug 0.82 m³ with the volume unchanged |
| Seven machines: dozer, 30 t excavator, wheel loader, articulated hauler, 8x4 tipper, mobile crane, tower crane (`HKSiteVehicles`) | `game-site.js`, models `site-models.js` | each driven with the simulated pad: the loader filled 3.6 m³, the hauler caught 3 m³ and tipped it out, the mobile crane deployed its outriggers and boom, the tower crane hooked and lifted a brick pallet, and the tipper drove out of the gate |
| Crane loads, hook pendulum, ✕ hook / set down | `game-site.js` `Hook` | tower crane: `load 1.1 t`, 3.55 m up |
| Site 140 × 90 m, two gates, gravel aprons, offices, signs, lights (UE work in progress, `make-site.py`) | `site-data.js` (the UE `site.json`, 21:05) | walked through both gates; the fence blocks beside them |
| Cleared ground: no trees, bushes, lamps or path triangles on the site or in the paintball yard (`HKData::Cleared`) | `surroundings.js` `holes`, `cutGround` | screenshot: soil up to the fence, no grass through it |
| See-through cab glass | `site-models.js` glass material | cab screenshot |
| R3 / V machine views: cab, outside, above the hook (`SiteMachineView`) | `game-main.js` `siteMachineView` | tower crane cab ↔ hook; dozer outside view |
| Paintball course, lobby, admin, teams (wishes, 6 a team), TDM with respawns / rounds, CTF, paint / reball, ballistics, splats, hits decided by the victim, score by the admin (`HKPaintball`) | `game-paintball.js`, `paintball-models.js`, `paintball-data.js` | `tests/paintball.test.cjs`; offline game; online with 2 players (a hit across the network, 1–0 on both screens), 3 players on a local server (CTF 2 v 1, a capture seen by all three) and a full lobby of 12 bots, `tests/online-paintball.mjs` (6 v 6, a hit over the network, the score and the feed seen by all 12) |
| Hold △ 2 s to leave a game; an empty game ends after 3 s, back to the lobby (UE work in progress) | `Rules.local`, `Rules.runAdmin` `emptyT` | `tests/paintball.test.cjs` (1.9 s is not enough, 2.1 s leaves, lobby after 3 s); 3 players online left with △ and the game returned to the lobby |
| Aiming lowers the marker to the lower right (UE work in progress) | `game-paintball.js` marker pose | screenshot |
| Others in a game: the marker in their hands, goggles, team armbands (`HKGamePaintball`, `HKAvatars`) | `game-avatars.js` `joints`, `pose({marker})` | screenshot of the other player |
| 12 players a lobby, 12 slot colours (`server/`, `HKAvatars`) | `cloud/src/index.js`, `game-net.js`, `game-avatars.js` | `tests/online-paintball.mjs`: 12 joined one lobby, the 13th was turned away (`full`) |
| Walking 3.5 m/s standing (UE work in progress, `HKFoot::Speed`) | `game-main.js` `WALK_SPEED` | measured 3.51 m/s |
| Balcony-door prompt fix (UE work in progress) | already fine here: the web raycast only tests interactables | prompt shown from 10 of 10 places |
| New sounds: diesel engine, paintball shot, splat, bounce, whistle (`HKAudioImpl`) | `game-audio.js` | used by the machines and paintball |

## 2b. Changed here first (29 Oct 2026), to be copied by the Unreal version

- The construction site moved 96 m south and 72 m west, and the paintball course moved into the valley floor the site left, so HODET N.N. (UE issue #3) can stand at its real place. The data is now made in this repo by `tools/make-site.py` and `tools/make-paintball.py` (ports of the UE scripts with new constants); the UE project should run its own scripts with the same constants.
- The world grew to ±760 m walkable / ±800 m terrain, and the real Muselunden DiscGolfPark (12 holes) was added (`muselunden-data.js`).
- Checked again on 29 Oct 2026 against the UE commits up to `962eded`: `aa26d86` (higher loader cab, low front fenders, seat view at the glass; small tipper mirrors on arms) is ported; `962eded` is an Unreal-only balcony test.
- Fixes found by the Unreal port, done here: petanque after 13, petanque freeze, vehicle obstacles (UE issues #7–#9); see-through barrier ends (UE #1); solid street lamps (UE #2).

## 3. Deliberately not ported

- Graphics only (the user said not to change the graphics): photo-real materials and textures, baked lighting, 3D
  facades, new trees and grass, the photographed sky, the MakeHuman people. They have no gameplay.
- (Fixed here on 29 Oct 2026, no longer an exception.) The web-game bugs listed in the UE `docs/GAMEPLAY-IDEAS.md`
  (petanque after 13, petanque freeze, vehicle obstacles one update late) are fixed in this game; the same fixes are
  issues in haarklous-ue5.

## 4. Not verifiable here

- A real DualShock 4 on the user's PC (the UE project says the user tested it there; here only the simulated pad is tested).
  The controller was switched off, and on 28 Oct 2026 the user chose to skip this test.
- Frame rate in a visible window. Background tabs throttle their timers, so the fps measured in them means nothing.
- 12 real people online at once (12 bots with the game's own rules and protocol were tested on a local server).
