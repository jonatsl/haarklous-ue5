# Porting checklist – what the Unreal version must do

Everything below exists in the web game (`reference/web-game/`) and must work the same in Unreal. Take exact values from the files named; the numbers here are only reminders. Tick items off (`[x]`) as they are ported and verified, and note the Unreal class or asset next to them.

## World and coordinates
- [ ] World units = metres of the apartment model: the 4th-floor apartment floor is y = 0, the street at the front door about y = −10 (web uses y up; convert to Unreal's z up, centimetres, and left-handed axes in one place only).
- [ ] Local map frame: x = metres east, s = metres south of the address node 59.936404 N, 10.776657 E; rotation `geo.theta` and offsets in `surroundings-data.js` / `surroundings.js` (`toWorld`, `toLocal`). The stair-door height 84.9 m a.s.l. maps to y −9.975.
- [ ] Terrain: Kartverket DTM (`assets/osm/kartverket-dtm-2m.tif`, and the 4 m grid ±600 m baked into `surroundings-data.js`). Walkable area ±540 m around the address.
- [ ] Buildings: OpenStreetMap footprints and heights (`surroundings-data.js` `buildings`, generator `generate-surroundings.py`); brick blocks with hip roofs, plaster and sheds; the own building is special (`data.own`, squeezed to 10.32 m depth, cut around the apartment and stair tower).
- [ ] Streets, paths, parks, playgrounds, courtyard (benches, hedges, trees), street lamps, trees and bushes from the data; aerial imagery was only a live web texture – use real materials instead.
- [ ] Sky, sun direction (`sunDir` in `game-main.js`), distant hills, Oslofjord to the south.

## Player (`game-main.js` `updateFoot`, `walk-collision.js`)
- [ ] First person; walk 1.45 m/s, run 3.6 m/s (R2 held or L3 toggle), crouch (tap ○) and prone (hold ○) with their speed factors and eye heights, jump (✕).
- [ ] Look sensitivity settings (controller and mouse), saved.
- [ ] Collision: walls solid, step onto stairs and kerbs (step 0.36 m), doors as moving obstacles.
- [ ] Drunk effect: each beer or sip +30 s; blur, double vision, sway, drifting walking and steering (`drunkLevel`).

## Apartment and stairwells (`apartment-data.js`, `game-apartment.js`, `furnishings.js`, `stairwell.js`)
- [ ] The 4th-floor apartment exactly as in `apartment-data.js` (frozen kitchen + bedroom design), living room and bathroom furnishing, opening doors, lights.
- [ ] Drinks: six beers on the kitchen worktop, aquavit on the TV bench (12 sips).
- [ ] Own stairwell: street door of 17A opens normally, stairs to the 4th floor, one flat door left and right per landing, only the own front door opens.
- [ ] Eight neighbouring buildings with enterable stairwells (`data.enterable`), their flat doors locked.

## Vehicles (`game-vehicles.js`, `game-aircraft.js`)
- [ ] Honda CBF600S (2005, no top box): lean physics (leans into the turn), six-gear engine note, bars and front wheel turn the right way, hands on the grips, pillion seat.
- [ ] Cars (hatchbacks, 7 colours): bicycle-model steering, LHD wheel turning at most about 55°, hands at ten and two, passenger seat.
- [ ] Excavator: L1/L2 left track, R1/R2 right track, ISO joysticks (left stick swing/stick, right stick boom/bucket), digging fills the bucket, dumping makes piles.
- [ ] Helicopter (H125 style) and two-seat sport plane: Mode 2 sticks, rotor spool-up, crash and reset rules, rotor-strike check, chase camera (R3/V), cockpit hands, passenger seat.
- [ ] FPV drone from the hold-△ menu: angle and acro modes, battery, on-screen display, works indoors.
- [ ] Vehicle placement: 5 cars, motorcycle, excavator, helipad (26, 14), airstrip (−116.5, 0) plus 25 cars and 7 motorcycles generated with the seeded generator in `game-main.js` (seed 1707). **Keep the exact list and order**: multiplayer identifies vehicles by their index.
- [ ] □ to get in/out (only when stopped; aircraft only when landed), engines and sounds.

## Activities
- [ ] Disc golf: two ~70 m par-3 holes in Torshovdalen (`game-minigames.js`).
- [ ] Petanque against the waiting opponent Arne: real rules, aim scatter, score HUD.
- [ ] Hostage rescue mission (`case-game.js`): start at the red ring beside the entrance (world 4.8, 14.2), hostage in the apartment, 3–5 hostiles in the stairwell and apartment who shoot back, pistol and health HUD only during the mission; Leave keeps you where you are.
- [ ] Firepit in the park in front of the building (world −2.7, 34.7): five benches (sit, R2 for a beer), woodpile (throw a log: bigger fire, burns down 1 level per 90 s, max 6), beer crate.
- [ ] Photo camera with gallery (tap △), hold-△ menu (camera or FPV drone), wristwatch on D-pad ↑ showing the PC time, map on the touchpad (no vehicles on it).
- [ ] Music (`game-music.js`): calm 2:30 open-world loop plus one loop each for disc golf, petanque and the mission; ambience and sounds (`game-audio.js`). Keep how they sound unless the user wants new ones.

## Online (built, off by default, not deployed)
- [ ] Lobby client speaking the protocol in `cloud/src/index.js` (`hello`, `welcome`, `st`, `vs`, `claim`, `release`, `chat`, `door`, `fire`, `name`, `mic`, `rtc`, `ping`) over WebSockets; own 4-character lobby code, join by code, up to 5 players (`game-net.js`).
- [ ] Remote players as avatars with name tags and chat bubbles, remote-driven vehicles smoothed, one driver and one passenger per vehicle, shared doors and fire (`game-multiplayer.js`, `game-avatars.js`).
- [ ] Chat (Enter/T, Share for quick chat) and lobby-wide voice chat (`game-voice.js`; voice may need a different technique in Unreal – ask before choosing).
- [ ] Settings: see your lobby code, join another by code (controller code pad), name, voice on/off.

## Controls and screens
- [ ] DualShock 4 (USB and Bluetooth) mapping as in `reference/web-game/README.md` and the start/pause screens of `index.html`, keyboard and mouse too.
- [ ] Start screen, pause/settings (sensitivity, music volume, graphics quality), credits for OpenStreetMap and Kartverket.
