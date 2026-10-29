# Haarklous online

A first-person open-world game set around Haarklous plass 17A, Torshov, Oslo: your 4th-floor apartment (your kitchen + bedroom design), its stairwell, the real neighbourhood (OpenStreetMap buildings and streets on Kartverket height data), vehicles, two minigames and a hostage-rescue mission.

**Play online with friends:** open **https://haarklous-online.server-smoke.workers.dev** (or the local file – it connects to the same server). Up to twelve players per lobby.

**Play:** double-click `PLAY-Controller-Windows.cmd` (fullscreen Chrome/Edge window, sound allowed for controller play) or open `haarklous-game.html` in Chrome, Edge or Firefox. Everything runs offline; only the optional aerial imagery loads online.

## Controls (PS4 controller · keyboard and mouse)

| Action | Controller | Keyboard / mouse |
| --- | --- | --- |
| Move / look (walking 3.5 m/s) | left stick / right stick | WASD / mouse (click to capture) |
| Run | R2, or click L3 to toggle | Shift |
| Jump | ✕ | Space |
| Crouch / lie prone | tap ○ / hold ○ (again to stand) | C / Z |
| Use (doors, drinks, vehicles, games, mission, shop items, till) | □ | E |
| Photo camera | tap △ (R2 shoot, L2 viewfinder, right stick zoom, □ gallery, ←/→ browse, ○ back) | tap F (left mouse shoot, right mouse viewfinder, G gallery, arrows browse) |
| Gear menu: camera or FPV drone | hold △, choose with ←/→ (or the left stick), release | hold F, choose with the arrows or the mouse, release |
| Check the time on your wristwatch | D-pad ↑ (again to lower it) | ↑ |
| Bag (what you have bought): open, choose, use, close | D-pad ↓, then ↑↓, ✕, ○ | ↓, then ↑↓, Space, C |
| Map | touchpad | M |
| Pause, settings (sensitivity, music volume, aerial imagery, graphics) | Options (D-pad ←/→ = music volume) | Esc |

**Motorcycle (2005 Honda CBF600S) and cars:** □ to get on/in; R2 throttle, L2 brake (hold when stopped to reverse), left stick steers (the bike leans at speed), right stick looks around. Your gloved hands hold the bars or the wheel, which turn with you. The bike has a six-speed gearbox model and an inline-four engine note. Cars and motorcycles follow real gravity (9.81 m/s²): take a crest or bump faster than gravity can hold you on the ground and you fly until you land – no grip, steering or brakes in the air, a thud and a dip when you touch down.

**Helicopter** (Airbus H125 style, on the grass beside Haarklous plass) and **plane** (single-seat sport plane at the north end of the long grass strip in Torshovdalen; both marked on the map as Helipad and Airstrip): □ to get in. They fly with "Mode 2" sticks, the Battlefield default and the usual RC layout:
- Helicopter: left stick ↑↓ collective (centred = hover; lift it a little to get off the ground), ←→ pedals (yaw); right stick pitch and roll (it gently levels itself when you let go). The rotor needs a few seconds to spool up.
- Plane: left stick ↑ adds throttle and ↓ takes it off (it stays where you leave it), ←→ rudder and nose-wheel steering; right stick pitch and roll; L2 wheel brakes. Full throttle, pull back at about 85 km/h, and it lifts off after roughly 80 m.
- R3 (or V) switches between the cockpit and a chase camera. The HUD shows speed, altitude and climb (plus rotor or throttle). Hitting the ground hard or a building fast is a crash: the aircraft is put back on its pad. □ gets out once you have landed and stopped.

**FPV drone:** hold △, choose *FPV drone* and release. You see through its camera with an on-screen display (mode, battery, flight time, altitude, throttle, speed, distance home). Left stick ↑↓ thrust (in angle mode it sets the climb rate and holds altitude when centred), ←→ yaw; right stick tilt. L1 (or Q) switches between *angle* (self-levelling) and *acro* (the right stick sets rotation rates, for flips and rolls, as in real freestyle FPV). It flies indoors too. ✕ resets it after a crash; ○ or △ packs it away. The battery lasts about five minutes.

**Wristwatch:** D-pad ↑ raises your left wrist. The watch shows the PC's local time (analogue hands and a small digital window) and lowers itself after a few seconds.

**Excavator** (by the street outside the building): □ to climb in. Tracks: L1/L2 left track forward/back, R1/R2 right track forward/back (counter-rotate to turn on the spot). ISO joysticks: left stick left/right swings the house, forward/back pushes the stick (dipper) out/in; right stick forward/back lowers/raises the boom, left/right curls/dumps the bucket. Dig with the teeth in the ground while curling; dump to drop a pile.

**Construction site** (140 × 90 m, fenced, in the south-west part of Torshovdalen, on the map; a park footpath runs in through the north gate and out through the south gate): a graded platform, a 30 × 16 m foundation pit with a ramp, five stockpiles, site offices and seven machines that drive like the real ones (engine power, grip, gears, rolling resistance on soil, slopes, hydraulics) and soil you can really move – buckets and blades cut it, carry it, pour it into a truck or onto the ground, and loose soil slides to its natural slope (nothing is lost). □ to climb in; on a keyboard the arrow keys work as the right stick.
- Dozer (Cat D6 class): R2 forward, L2 reverse, left stick steer; right stick back/forward raises/lowers the blade, ←→ tilts it. Drive into a heap with the blade low to push soil.
- Excavator (30 t): L1/L2 left track, R1/R2 right track; left stick swing and stick, right stick boom and bucket (the same ISO pattern as the street excavator). Teeth in the soil while curling digs; turn the bucket over a truck to load it.
- Wheel loader: R2 throttle, L2 brake/reverse, left stick steers (articulated); right stick back/forward lifts/lowers, ← rolls the bucket back, → dumps. Drive into a stockpile with the bucket low to scoop.
- Articulated hauler and tipper truck: R2 / L2 / left stick drive; right stick back tips the body, forward lowers it (with the body up they only creep).
- Mobile crane: drive it like a truck; stopped, L1 lowers the outriggers (L1 again raises them, folding the boom first). On the outriggers: left stick ←→ slew, ↑↓ telescope out/in, right stick back/forward boom up/down, R2 / L2 hoist up/down, ✕ hooks the load under the hook or sets it down.
- Tower crane (climb in at the foot of the mast): left stick ←→ slew (it has inertia), ↑↓ trolley out/in; right stick back/forward hoists up/down; ✕ hooks or sets down a load (brick pallets, steel beams, a concrete skip).
R3 (keyboard V) switches the view of a machine: from the cab, from outside (the mouse turns it round the machine) and, for the cranes, straight down from above the hook. The HUD shows speed and gear, the bucket or body load in m³, the blade height, or the crane's boom, radius and hook.

**Paintball** (an urban course on the valley floor of Torshovdalen, between the sculpture and the construction site, 60 × 40 m inside a net: containers, roofless buildings, walls with windows, barriers, a car wreck, cable spools, sandbags, barrels, pallets and tyres – the same for both teams). Walk into the lobby on the lawn outside the gate: the first one there is the admin; others choose a team with ↑↓ (any, blue or red) and wait. The admin picks the game with ←→ (team deathmatch with respawns, team deathmatch in rounds, or capture the flag), paint or reball with △, and holds ✕ to start a game for everyone in the lobby (up to 6 v 6; wishes first, the rest balanced). Normal paintball rules: a ball that breaks on you (or your marker) puts you out, also from your own team; a bounce does not count (reball: every hit counts). Semi-automatic marker, 10 balls a second at most, 200 in the hopper. R2 (left mouse) fires, L2 (right mouse) aims, □ (R) reloads, hold △ for 2 s to leave the game. Team deathmatch: first to 20 eliminations (back in at your base after 5 s) or, in rounds, out is out until one team is left, first to 3 rounds. Capture the flag: bring their flag to your base, first to 3 captures; a carrier who is hit drops it. Games last at most 10 minutes; paint stays on the cover until the next game. Alone you can practise; online everyone in the lobby plays together.

**HODET N.N.** (Marianne Heske, 2014): the 7 m bronze doll's head stands at its real place at the north end of Torshovdalen, facing south down the valley, with stone steps on its east side that you can climb and sit on. It is on the map.

**Disc golf**: two courses. **Muselunden DiscGolfPark**, the real 12-hole course at its real place by Sinsenkrysset, about 700 m north-east of home (layout "Muselunden 12 hull (2022)", all par 3, 35–106 m; on the map), and two par-3 holes of about 70 m in Torshovdalen. Stand on a tee pad, □ to play. Aim with the camera, hold R2 for power and release to throw; left stick ←/→ adds hyzer/anhyzer. Within 11 m you putt. Chains catch slow discs; scorecard on screen. ○ quits.

**Petanque** (gravel court in the park): □ to play singles against Arne (3 boules each, first to 13). The jack must land 6–10 m away; the player not holding the point throws next; at the end of each mène the leader scores one point per boule closer than the opponent's best; the next circle is where the jack ended. Hold R2 for distance and release; hold L2 to shoot (flat throw that knocks boules). Throws land roughly where you aim, with scatter. The HUD shows the score, mène, boules left, who holds the point and by how much, and distance labels over the boules.

**Home:** the street door of 17A is an ordinary door – go in, up the stairs to the 4th floor and through your front door, no mission needed. **Hostage rescue:** stand in the red ring with the sign beside the entrance and press □. After a mission (finished or not), choosing *Leave* keeps you where you are – if you are in the apartment, you simply stay at home. A hostage is in the 4th-floor apartment; 3–5 armed hostiles are in the apartment and in the stairwell. The pistol, health bar and hostile/hostage counter exist only during the mission. L2 aims, R2 fires, □ opens the door in front or reloads, Options pauses.

**Stashes:** three hidden crates – on the top landing of a neighbouring stairwell, in the courtyard behind the building, and by the bushes in the park north of the block. Each has lines to snort and a syringe: □ gives a random 30-second boost – 3× running speed, 3× jump height or bullet time (the world, including the mission's hostiles, slows to about a third while you keep full speed). A used item is back after 90 seconds.

**Shops and money:** Joker Torshov (about 120 m south-west of the flat) and Kiwi Torshov (about 250 m north) are at their real places from OpenStreetMap, built into the ground floor of their buildings, with a glass front, sliding doors, shelves, fridges, firewood, a bottle-return machine and a cashier at the till (both on the map, 🛒). The opening hours are on the door but the shops are always open in the game. Walk in, look at an item and press □ to put it in your basket, then pay at the till with □. You start with 500 kr; the top-right corner shows your money and empty bottles, and the basket shows what you are carrying. Kiwi is about 5 % cheaper. Norwegian rules: beer is only sold Monday–Friday 08–20 and Saturday 08–18 (PC clock), never on Sunday – the cashier takes the beer back. Without enough money the card is declined. Walking out with unpaid items sets off the door alarm and they go back. Bought items go in your bag (D-pad ↓): drinks give an empty bottle that the machine takes for 2 kr, food sobers you up, Red Bull gives 15 s of 3× speed, a beer is +30 s drunk (the beers in the flat and the firepit crate leave an empty bottle too), and a sack of firewood can be emptied onto the firepit (+3 logs). Earn money: hostage rescue 400 kr, disc golf par 50 / birdie 100 / eagle 200 kr, petanque win 150 kr, bottle deposit 2 kr. Money, bottles and bag are saved in the browser.

**Drinks:** six beers on the kitchen worktop and a bottle of aquavit on the TV bench. Each beer or sip adds 30 s of drunkenness: blurred, doubled, swaying vision and drifting walking/steering.

**Soundtrack** (generated live, no sound files, loops seamlessly; volume in the pause menu):
- Open world: *Torshov Evening*, a calm lo-fi/ambient piece (electric piano, warm pad, soft bass, brushed drums, bells), exactly 2:30.
- Disc golf: *Fairway Breeze*, breezy acoustic guitar picking with shaker and a whistled tune (1:20).
- Petanque: *Café du parc*, a French musette waltz on accordion (1:12).
- Hostage rescue: *Stairwell*, a tense pulse with drone, heartbeat drums, strings and low brass (1:17).
Tracks crossfade when you start or leave a game; the open-world track picks up where it left off.

**Multiplayer:** every player has an own 4-character lobby code (pause menu, with a share link `…/?lobby=CODE`). Friends join with the code (pause menu: type it and *Join lobby*, or on the controller press △ and pick the letters with the D-pad, ✕ to join; □ takes you back to your own lobby). Up to twelve players. You see each other (name tags, jacket colour per player), doors and the firepit are shared, and vehicles driven by someone move for everyone. Next to a vehicle someone else is driving, □ rides along as passenger (motorcycle pillion, cars, helicopter, plane – one passenger each). **Chat:** Enter or T on the keyboard; the controller's Share button opens quick chat (choose with ↑↓, ✕ sends). Chat lines also appear as bubbles above the speaker. **Voice chat:** tick *Voice chat* in the pause menu – everyone in the lobby hears you at full volume, wherever they are on the map (you hear the others even with your own microphone off).

**Firepit** (in the park in front of the building, on the map): □ by a bench to sit down (R2 for a beer from the crate, □ to stand up), □ at the woodpile throws a log on the fire – it gets bigger, brighter and louder, and burns down again over a few minutes. More cars (30) and motorcycles (8) are parked around the neighbourhood.

**Exploring:** walk the whole neighbourhood (about 1 km square). Eight neighbouring buildings have a working stair tower you can walk up; their flat doors are locked.

## Sources and licences

- Map data © OpenStreetMap contributors (ODbL): `assets/osm/torshov-osm.json`, Torshovdalen park outline `assets/osm/torshovdalen.json`.
- Height data © Kartverket (DTM, CC BY 4.0): `assets/osm/kartverket-dtm-2m.tif` (1.6 km square, 2 m grid).
- Aerial imagery (optional, live only, never stored): Esri World Imagery.
- Three.js (MIT): `vendor/`.

## Development

Edit the source files, then `python build.py` to rebuild `haarklous-game.html`. `python generate-surroundings.py` regenerates `surroundings-data.js` from the OSM and DTM files (about two minutes). `node --test tests/*.cjs` runs the tests; `node tests/online-paintball.mjs` plays a full 12-player paintball lobby against a local lobby server (`npx wrangler dev --port 8787` in `cloud/`, never the live one); `tests/browser-harness.js` drives the game in a browser with a simulated controller. `python package-portable.py` writes `dist/Haarklous-online.zip`.

**Server:** `cloud/` is a Cloudflare Worker (account 46fd597da1c5fe9de4c3ebbaae86f906) that serves the game (`cloud/public/index.html`, written by `build.py`) and runs one Durable Object per lobby (`cloud/src/index.js`). After `python build.py`, deploy from `cloud/` with `npx wrangler deploy` (Wrangler is logged in on this PC). For local testing: `npx wrangler dev --port 8787` and open `http://127.0.0.1:8787/?server=ws://127.0.0.1:8787`.

Modules: `game-main.js` (loop, input, HUD), `game-site.js` + `site-models.js` + `site-data.js` (construction site: soil and machines), `game-paintball.js` + `paintball-models.js` + `paintball-data.js` (paintball course, rules, lobby and games), `game-net.js` (lobby connection), `game-multiplayer.js` (remote players, vehicles, seats, chat), `game-avatars.js`, `game-voice.js` (WebRTC voice), `game-fire.js` (firepit), `game-aircraft.js` (helicopter, plane, FPV drone), `game-watch.js` (wristwatch), `game-music.js` (soundtrack), `game-apartment.js`, `stairwell.js`, `surroundings.js`, `game-vehicles.js`, `game-minigames.js`, `game-camera.js`, `game-map.js`, `game-audio.js`, `case-game.js` (mission), `viewmodel.js` (pistol and hands), `materials.js`, `furnishings.js`, `walk-collision.js`, `gamepad-controls.js`.
