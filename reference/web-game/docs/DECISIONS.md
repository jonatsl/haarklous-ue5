# Decisions and features log

Every change and every feature gets an entry here, in the same commit or pull request as the change (the user's rule, 29 Oct 2026).
Newest first. Each entry says **what** changed, **why** (the decision and who made it), **where** (files), and **status** (branch / PR, on `main`, live or not).
Technical detail for continuing the work lives in `docs/HANDOFF.md`; this file is the plain record.

Live game: https://haarklous-online.server-smoke.workers.dev · Source: https://github.com/jonatsl/haarklous-online · Unreal version: https://github.com/jonatsl/haarklous-ue5

## Features (what the game has now)

On `main`:
- Open world around Haarklous plass 17A, Torshov: real buildings and streets (OpenStreetMap), real ground heights (Kartverket), optional aerial photos; walkable out to about 760 m (540 m before PR #13), drawn properly everywhere you can walk.
- Your 4th-floor apartment (your kitchen + bedroom design) and stairwell; the street door and stairs work without the mission; eight neighbouring stairwells.
- Walking (3.5 m/s), running, crouch (tap ○), prone (hold ○), jump (✕); PS4 controller (USB / Bluetooth) plus keyboard and mouse; look sensitivity setting.
- Vehicles: Honda CBF600S motorcycle and 7 more motorcycles, 30 cars, excavator, helicopter, two-seat plane; hands on bars / wheel / sticks.
- Cars and motorcycles follow gravity (9.81 m/s²) and fly over crests and bumps taken fast (PR #5).
- FPV drone (hold △), photo camera with gallery (tap △), wristwatch (D-pad ↑), map (touchpad).
- Minigames: the real Muselunden DiscGolfPark (12 holes, PR #13) and two disc golf holes in Torshovdalen, petanque against Arne; hostage-rescue mission (start at the red ring by the entrance); stay home after the mission.
- Firepit with benches, beer crate and woodpile (fire size shared online); beer and aquavit in the apartment (30 s drunk each).
- Joker and Kiwi shops at their real places with a cashier; money in kroner (start 500 kr, earned from the mission, minigames and bottle deposit), Norwegian beer hours, a bag of bought items (PR #6).
- Three hidden stashes with 30-second boosts: 3× running speed, 3× jump height or bullet time.
- HODET N.N., Marianne Heske's 7 m bronze doll's head, at its real place at the north end of Torshovdalen, with its stone steps (PR #12).
- Construction site (140 × 90 m, two gates, in the south-west of Torshovdalen since PR #12) with seven machines: dozer, 30 t excavator, wheel loader, articulated hauler, 8x4 tipper, mobile crane, tower crane; diggable soil that keeps its volume; R3 / V changes the machine view (PR #8).
- Paintball course on the valley floor between the sculpture and the site (since PR #12): lobby, admin, teams up to 6 v 6, team deathmatch with respawns or in rounds, capture the flag, paint or reball (PR #8).
- Multiplayer on Cloudflare (lobby codes, up to 12 players), passengers in motorcycle / cars / helicopter / plane, text chat with quick chat, lobby-wide voice chat.
- Soundtrack: 2.5-minute open-world loop plus one loop per minigame and the mission; ambient sound by location.

Live site: runs `main` (deployed 29 Oct 2026 after PRs #8–#14, version fd96bf7e), so everything above is online.

## Log

### 2026-10-29 · `main` is live
- **What:** the game on `main` (PRs #5–#14: jumps, shops, stashes, the Unreal features, the fixes, HODET N.N., Muselunden and the bigger world) is deployed to https://haarklous-online.server-smoke.workers.dev (Cloudflare Worker version fd96bf7e). Before, the live site ran the older branch `feat/ue5-features`.
- **Why:** `AGENTS.md`: after changing game files, redeploy so the online version matches. `main` now holds everything the old live branch had.
- **Checked:** the live page is byte-for-byte the local build (`cloud/public/index.html`) and contains the shops, paintball, the sculpture, the Muselunden course and the ±760 m world.
- **Status:** live.

### 2026-10-29 · Unreal repo issues brought up to date
- **What:** haarklous-ue5 now has an issue for every web change and fix it lacks, each with the web numbers, files and checks.
  - New: #6 stashes, #7–#9 the three bugs, #10 the site and course move, #11 Muselunden and the bigger world, and #12, a start-here list in working order.
  - Updated with full details: #1 paintball barriers, #2 street lamps, #3 HODET N.N.
- **Why:** the user's goal (29 Oct 2026): both repos should have the same features and fixes, so another agent can be pointed at the Unreal repo.
- **Status:** done on GitHub; no code in this repo.

### 2026-10-29 · Clearer cab views in the wheel loader and the tipper (from the Unreal version)
- **What:**
  - **Wheel loader:** the cab is higher (1.75 m), the front fenders are short and low, and the seat view is at the front glass looking down (seat 0, 3.15, 0.3, pitch −0.36), so you see the bucket's edge and corners.
  - **Tipper:** the mirrors are smaller and out on arms, away from the windscreen.
- **Why:** the Unreal version made these changes after the last parity check (haarklous-ue5 commit aa26d86, 28 Oct 2026, 22:53). The user's goal: the same features and fixes in both games. Its other later commit, 962eded, is an Unreal-only balcony test.
- **Where:** `site-models.js` (loader cab, fenders, lights; tipper mirrors), `game-site.js` (loader seat and view), `docs/UE5-PARITY.md`.
- **Checked:** from the loader seat the bucket lip is in the middle of the view (screen −0.03, −0.21), and the eye is 3.1 m above the ground. Node tests 44/44; no console errors.
- **Status:** PR #14 (branch `fix/site-cab-views-from-ue5`, on top of PR #13). Merged into `main` on 29 Oct 2026 (the user approved PRs #8–#14 together).

### 2026-10-29 · The real Muselunden DiscGolfPark, and a bigger world
- **What:**
  - **Muselunden DiscGolfPark**, the real 12-hole course by Sinsenkrysset, is in the game at its real place, about 470–800 m north-east of home.
    - It is the course's current layout, "Muselunden 12 hull (2022)": all par 3, lengths 106, 53, 68, 63, 35, 50, 65, 64, 76, 77, 52 and 47 m (par 36).
    - Each hole has a tee pad, a sign ("HULL n · Par 3 · length") and a basket. You play it like the Torshovdalen holes; the HUD shows the course name. The holes are on the map.
  - **The world is bigger** so you can reach it: you can walk to ±760 m (was ±540 m), and the ground data reaches ±800 m (was ±600 m). The new ring has its real ground heights, and its OpenStreetMap trees (1,791 more) and street lamps (306 more) are added. Buildings, trees and lamp posts are solid out to the new edge.
  - Nothing inside the old world moved: the old terrain values, trees and lamps are kept as they were, and the street vehicles start where they did.
- **Why:**
  - The user's goal (29 Oct 2026): "implement the real muselunden disc golf park at its real location with all the holes", using UDisc, maps and other sources.
  - UDisc gives the current layout, lengths and par. Its hole map needs a login, so I did not sign in for you; the places come from the club's own map instead.
  - The club's map is muselunden.no "Midlertidig banekart 2021". It has the same 12 holes and is georeferenced to the two football pitches in OpenStreetMap (RMS 2.2 m).
  - The map is a sketch, so each hole is scaled to UDisc's length about its midpoint (ends move up to 8 m). Hole 6's tee then sat on a service road, so that hole slid 3 m along its own line.
  - The club's older 12-hole sign (lengths 107 … 148 m, one par 4) is a previous layout and was not used.
- **Where:**
  - New: `muselunden-data.js`, `tools/make-muselunden.py`, `tools/extend-surroundings.py`, `assets/osm/torshov-nodes-800m.json`, `tests/muselunden.test.cjs`.
  - Changed: `game-minigames.js` (`discGolf(course)` builds any course), `game-main.js` (second course, prompts, map, HUD, music), `surroundings.js` (`PLAY_HALF` 760, `FAR_EXT` 800, obstacles in the square), `surroundings-data.js` (bigger terrain, more trees and lamps), `generate-surroundings.py` (±800 m, reads the new nodes), `index.html`, `README.md`, `docs/HANDOFF.md`.
- **Checked:**
  - Node tests 44/44. New: 12 holes, par 36, UDisc lengths ±0.15 m; every tee and basket is in the OSM course outline (or within 10 m of it), in the walkable world, not in a building and not on a car road.
  - The old terrain grid inside ±600 m is byte-for-byte the same, and the old trees and lamps are unchanged.
  - In the game:
    - the 41 street vehicles start where they did;
    - walked at Muselunden;
    - the prompt shows at the tees;
    - played hole 1 into the basket, and the game moved on to hole 2 with "Muselunden DiscGolfPark · Hull 2/12";
    - no console errors.
  - The frame rate was not measured in a visible window (the test tab is hidden).
- **Status:** PR #13 (branch `feat/muselunden-disc-golf`, on top of PR #12). For the Unreal version: https://github.com/jonatsl/haarklous-ue5/issues/11. Merged into `main` on 29 Oct 2026 (the user approved PRs #8–#14 together).

### 2026-10-29 · HODET N.N. at its real place; the construction site and the paintball course move
- **What:**
  - **HODET N.N.** (Marianne Heske, 2014), the 7 m bronze doll's head, stands at its real place at the north end of Torshovdalen (OSM node 2965223021, local 9.1, 275.5).
    - It faces south down the valley, as in the photos (checked against a 2014 Wikimedia Commons photo and an aerial photo).
    - It has a short neck and a flat-topped shoulder plate, and the face has lowered eyelids, a small nose, full cheeks and a small mouth.
    - Four rows of stone steps on its east side, like the small amphitheatre there. You can climb and stand on them.
    - It is solid and is on the map.
  - **The construction site** moves 96 m south and 72 m west: fence x −158..−18, s 371..461. Its two gates are where a park footway crosses the fence. Everything inside keeps its place relative to the fence: the pit, stockpiles, offices and machines. The platform is fitted to the new ground.
  - **The paintball course** moves into the valley floor that the site left (yard x −72..−8, s 296..344). It is 17 m west of the sculpture. The gate is where the lawn is level with the yard, and the lobby is on the lawn south of it.
  - The site and course data is now made in this repo by `tools/make-site.py` and `tools/make-paintball.py`. These are the Unreal project's generators, ported. With the old numbers they give the old files exactly.
- **Why:**
  - The Unreal issue #3 asked for the Torshovdalen sculpture.
  - Its real place was on the construction site's north fence, beside the gate. The user chose: the sculpture at its real place, the site about 100 m south, and the paintball course somewhere it does not cross buildings or the sculpture ("remove a block if needed").
  - Straight 100 m south, the site would have crossed the main road along the park and buildings 531 and 539. The nearest clear place is 96 m south and 72 m west (multiples of 8 m, the Unreal far terrain's grid).
  - The paintball course fits in the freed valley floor, so no block had to be removed.
  - Torshovdalen also has a smaller concrete sculpture, "Tors bukker" (Anna Widén, 2017). HODET N.N. was taken as "the sculpture".
- **Where:** `game-sculpture.js` (new), `game-main.js` (place, collision, map), `index.html`, `tools/make-site.py` and `tools/make-paintball.py` (new), `site-data.js` and `paintball-data.js` (regenerated), `tests/places.test.cjs` (new), `README.md`, `docs/HANDOFF.md`, `docs/UE5-PARITY.md`.
- **Checked:**
  - Node tests 42/42. New: the sculpture is at the OSM node; the site, paintball yard, lobby and sculpture overlap neither each other nor any building or car road; the face is symmetric.
  - In the game:
    - the 41 street vehicles start where they did, and the 7 site machines are inside the new site;
    - walked in through the site's north gate, and the fence blocks beside it;
    - paintball lobby, game start in the new yard (floor −27.05), firing, and leaving with △;
    - walking into the sculpture stops you at its shoulder plate; you can stand on the steps; the view from the south matches the photo;
    - every ray at the sculpture hits a front face; no console errors.
  - Not repeated here: digging with the 30 t excavator. In the hidden test tab the simulated stick and bucket did not move. The soil logic does not depend on the place and is covered by `tests/site.test.cjs`.
- **Status:** PR #12 (branch `feat/hodet-nn-sculpture`, on top of PR #11). For the Unreal version: https://github.com/jonatsl/haarklous-ue5/issues/3 and https://github.com/jonatsl/haarklous-ue5/issues/10 (the move). Merged into `main` on 29 Oct 2026 (the user approved PRs #8–#14 together).

### 2026-10-29 · Street lamp posts are solid
- **What:** the 82 street lamps (OpenStreetMap `highway=street_lamp`, already drawn as a 5 m grey pole with a round head) now block walkers and vehicles like trees do. Before, you could walk and drive straight through them. The map data has 83; the one at local (51.1, 288) stands inside the construction site and is left out, as before.
- **Why:** the Unreal version has no street lamps yet (haarklous-ue5 issue #2, "Add street lamps", 28 Oct 2026). The user's goal is the same features in both games. The web game already had the lamps, so the only gameplay gap was collision. The look is unchanged (no light at night: neither game has a night).
- **Where:** `surroundings.js` (lamp list and `obstacles()`).
- **Checked:** the 48 vehicles start at exactly the same places (the parked-car seed 1707 is unaffected, which matters for online play with the Unreal version). Walking into a post stops you 0.32 m from its centre. A car at 30 km/h stops with its centre about 2.4 m from the post. Node tests 38/38.
- **Status:** PR #11 (branch `feat/solid-street-lamps`, on top of PR #10). Spec for the Unreal version: https://github.com/jonatsl/haarklous-ue5/issues/2. Merged into `main` on 29 Oct 2026 (the user approved PRs #8–#14 together).

### 2026-10-29 · Paintball: the concrete barriers' ends are no longer see-through
- **What:** each end of the concrete (jersey) barriers had a small see-through gap and a thin sliver sticking out past its outline. The barrier's cross-section is narrower above its shoulder (a concave shape), and the end caps were filled as a fan, which only works for convex shapes. One triangle in each cap was wound backwards (so it was not drawn) and one reached outside the outline. The caps are now filled by ear clipping, which works for any simple outline.
- **Why:** the user reported see-through obstacles in the Unreal paintball course (haarklous-ue5 issue #1, 28 Oct 2026). The web course is built from the same Python, and the same fault was found here. The user's goal: the same fixes in both games.
- **Where:** `paintball-models.js` (`capTris`, `prism`), `tests/paintball.test.cjs` (new test: caps cover the outline exactly, every triangle the same way round).
- **Checked:** rays from 384 standing and crouching spots in the course, 15,360 in all, including rings round every barrier end. Before: 9 rays first hit a face from behind (a culled, see-through face), all on the barriers. After: 0. No other cover piece (containers, walls, building shells, car wreck, spools, sandbags, barrels, pallets, tyres) had any. The old fan had one reversed triangle per cap. Node tests 38/38; no console errors.
- **Status:** PR #10 (branch `fix/paintball-see-through`, on top of PR #9). Details for the Unreal version: https://github.com/jonatsl/haarklous-ue5/issues/1. Merged into `main` on 29 Oct 2026 (the user approved PRs #8–#14 together).

### 2026-10-29 · Three small bugs fixed (found by the Unreal port)
- **What:**
  - Petanque now ends once a side has 13. Before, the last mène was counted again every frame: the score kept climbing (13–12, 14–12 …), the game only ended with ○, and the 150 kr win reward was paid again every frame.
  - Petanque no longer freezes when every boule in the court has been knocked or thrown out. Whoever still has boules plays on; the mène ends when both sides are out.
  - A vehicle's collision world made during play (for example the motorcycle's, on its first ride) gets the other vehicles at once. Before, it had none until the next door or vehicle change, so the bike could drive through parked cars.
- **Why:** the Unreal port found these three bugs (`docs/GAMEPLAY-IDEAS.md` in haarklous-ue5) and copied them on purpose. The user's goal (29 Oct 2026) is the same features and fixes in both games. For the freeze, "whoever has boules plays on" was chosen over the Unreal note's "end the mène", so the side with boules left still gets to throw them.
- **Where:** `game-minigames.js` (`petanqueNext`, the finish check in the petanque update), `game-main.js` (`worldFor`, `updateDynamic`), `tests/minigames.test.cjs` (new).
- **Checked:** Node tests 37/37. In the game: petanque from 12–12 played to "You win 13–12!"; the score stayed 13–12, the game ended by itself after 4 s, and exactly 150 kr was paid. The motorcycle on its first ride: before the fix 5 of 6 parked cars did not block it, after the fix all 6 do; the spot right in front of the bike is still free.
- **Status:** PR #9 (branch `fix/three-bugs-from-ue5`, on top of PR #8). Same fixes for the Unreal version: haarklous-ue5 issues #7, #8, #9. Merged into `main` on 29 Oct 2026 (the user approved PRs #8–#14 together).

### 2026-10-29 · One version again: the Unreal features join `main`
- **What:** the branch `feat/ue5-features` (construction site, paintball, 12 players per lobby, walking 3.5 m/s, see `docs/UE5-PARITY.md`) is merged with `main` (vehicle jumps, shops, stashes, far-away fix). The two versions had grown apart; now one branch has everything. Small fixes while merging: the cashier keeps their own shirt colour with the 12 slot colours; the map shows the shops, the site and the paintball course; the multiplayer screen says "up to 12 players".
- **Why:** the user's goal (29 Oct 2026): both repos should have the same features and fixes. The fixes for the Unreal issues (paintball, street lamps, sculpture) need the paintball code, which was only on `feat/ue5-features`.
- **Where:** merge of `feat/ue5-features` into `feat/align-with-ue5`; conflicts solved in `game-main.js` (ground, obstacles, map markers, test handle), `game-avatars.js`, `index.html`, `style.css`.
- **Checked:** Node tests 35/35. In the game: walked into Joker and paid for two items (36,80 kr), joined the paintball lobby, started and left a game (hold △), drove a site machine; no console errors.
- **Status:** PR #8 (branch `feat/align-with-ue5`), `feat/ue5-features` is no longer needed. Merged into `main` on 29 Oct 2026 (the user approved PRs #8–#14 together).

### 2026-10-29 · Joker and Kiwi shops, cashier and money
- **What:** the two real grocery shops near Haarklous plass are in the game at their OpenStreetMap places: Joker Torshov (node 1506608498, shop front about 120 m south-west of the flat, in building 888) and Kiwi Torshov (node 2476542841, about 250 m north, in building 156). Each shop is cut into the ground floor of its building (the building outline gets a 10 × 10 m notch, the facade above is rebuilt). Inside: glass front with sliding doors and the brand sign, opening hours on the door, fridges, shelves, firewood sacks, a bottle-return machine and a till with a cashier who greets you and turns toward you. 12 products with real Norwegian prices (Kiwi about 5 % cheaper). □ puts an item in the basket, □ at the till pays. Money in kroner: start 500 kr, shown top right; earn it from the hostage rescue (400), disc golf (par 50, birdie 100, eagle 200), petanque wins (150) and bottle deposit (2 kr per bottle, also for the beers in the flat and the crate). Bought items go in a bag (D-pad ↓ / ↓): drinks, food (sobers you up), Red Bull (15 s of 3× speed), firewood for the firepit (+3 logs).
- **Why:** the user asked for both shops at their real places, buying items, a cashier and a money system. Norwegian rules chosen: beer only Mon–Fri 08–20 and Sat 08–18 (by the PC clock), never on Sunday (the cashier takes it back); 2 kr deposit (every drink sold is 0.5 l or smaller; bigger bottles would be 3 kr). Opening hours are shown but not enforced, so the shops can always be visited. Leaving with unpaid items sets off the alarm and the items go back (no stealing). Money is saved in the browser per player, not shared online.
- **Where:** `game-shops.js` (new: shops, products, notch, wallet), `game-main.js` (prompts, basket, paying, bag, earning, HUD), `game-minigames.js` and `case-game.js` (rewards), `game-avatars.js` (shirt colour), `game-audio.js` (coin, till, alarm, bottle machine, door sounds), `index.html`, `style.css`, `README.md`, `tests/shops.test.cjs`.
- **Checked:** walked in through both doors, took all 8 kinds of shelf, fridge and floor items by aiming and pressing □, paid at the till, beer refused on a Sunday and after 18 on a Saturday, card declined, alarm on leaving, bottle return, bag (open, choose, use, close, long list scrolls), firewood on the firepit (+3), reward notice. Extra frame time in front of the shop about 3 ms. Node tests 28/28.
- **Status:** PR https://github.com/jonatsl/haarklous-online/pull/6, merged into `main` on 29 Oct 2026 (the user approved). Not live yet: the live site runs `feat/ue5-features`. Same feature for the Unreal version: https://github.com/jonatsl/haarklous-ue5/issues/5.

### 2026-10-29 · Cars and motorcycles jump over crests (gravity 9.81 m/s²)
- **What:** cars and motorcycles have real up-and-down movement. Where the ground falls away faster than gravity (9.81 m/s²) can pull the vehicle down, it flies on a ballistic path until it lands. No grip, steering or brakes in the air; the nose follows the flight path; landing gives a thud and a short dip of the view (crash sound on hard landings). In the air only obstacles at the vehicle's height block it. The excavator still hugs the ground.
- **Why:** the user's wish: vehicles should jump "when physics says so", with gravity 9.81 m/s². Millimetre hops over the 4 m height grid are ignored for control (more than 0.06 s and 3 cm needed) so steering stays normal on ordinary roads.
- **Where:** `game-vehicles.js` (`Vehicle.settle`, `clear`, `land`; car and motorcycle `update`), `README.md`.
- **Checked:** grass hump at local (392, −392): car 50 km/h stays down; car 90 km/h flies 0.57 s / 14 m (matches a hand calculation); motorcycle 110 km/h flies 0.68 s / 21 m; measured gravity 9.80–9.83 m/s².
- **Status:** PR https://github.com/jonatsl/haarklous-online/pull/5. Same feature for the Unreal version: https://github.com/jonatsl/haarklous-ue5/issues/4.

### 2026-10-29 · This log
- **What:** `docs/DECISIONS.md` records every change and feature; `AGENTS.md` tells every session to keep it up to date.
- **Why:** the user asked to keep track of every change and feature in a decisions file.
- **Status:** PR #5.

### 2026-10-28 · Issues #1 and #2 merged into `main`
- **What:** PR #3 (far-away buildings and ground) and PR #4 (stashes) merged; issues #1 and #2 closed.
- **Why:** the user asked to implement the two issues and approved the merges.
- **Status:** on `main`. The live site runs `feat/ue5-features`, which has the stashes but not yet the far-away fix.

### 2026-10-28 · Features ported from the Unreal version
- **What:** construction site (seven machines, diggable soil that keeps its volume), paintball, 12 players per lobby, walking speed 3.5 m/s.
- **Why:** the user's goal: feature parity with the Unreal project, in the web style (never its graphics). Record: `docs/UE5-PARITY.md` on that branch.
- **Where:** `game-site.js`, `site-models.js`, `site-data.js`, `game-paintball.js`, `paintball-*.js`, `cloud/src/index.js`, `game-main.js`.
- **Status:** branch `feat/ue5-features` (no PR yet), deployed live.

### 2026-10-26 · Hidden stashes with 30-second boosts (issue #2)
- **What:** three crates (a neighbour's top-floor stairwell, the courtyard behind the building, a bush in the park north of the block), each with lines and a syringe; □ gives a random 30 s boost: 3× running speed, 3× jump height or bullet time (the world, including the mission's hostiles, at 0.35× speed). Items come back after 90 s.
- **Why:** issue #2. "Something else" was chosen as bullet time because it also matters in the mission.
- **Where:** `game-stash.js`, `game-main.js`, `case-game.js` (`ctx.timeScale`), `game-audio.js` (`sniff`, `needle`, `muffle`).
- **Status:** PR #4, merged.

### 2026-10-26 · Buildings and ground drawn properly far from home (issue #1)
- **What:** everything you can walk to (±540 m) is drawn in the near pass: buildings and trees in 270 m tiles, ground out to the edge on the same 4 m grid you walk on. The far layer only fills the distance.
- **Why:** issue #1: beyond about 280–300 m the far layer (which clips anything closer than 15 m) made buildings see-through and the ground go missing.
- **Where:** `surroundings.js`. Speed measured about the same as before.
- **Status:** PR #3, merged.

### 2026-10-25 · Voice chat for the whole lobby; GitHub repo
- **What:** voice is no longer by distance: everyone in the lobby hears everyone at full volume. Fixed voice that never arrived when someone turned the mic on after joining (one two-way audio channel per pair; the mic only swaps the track). Source moved to the private repo `jonatsl/haarklous-online`.
- **Why:** the user's wish (voice) and request (repo).
- **Where:** `game-voice.js`, `game-multiplayer.js`.

### 2026-10-24 · Rename to "Haarklous online"; home without the mission
- **What:** the online game is called Haarklous online (Worker `haarklous-online`; the old address was removed). The street door opens normally; the mission starts from a red ring by the entrance; leaving a mission keeps you where you are (at home if you are in the flat).
- **Why:** the user's wishes.
- **Where:** `index.html`, `cloud/wrangler.toml`, `game-net.js`, `game-main.js`.

### 2026-10-24 · Multiplayer, chat, passengers, firepit, more vehicles
- **What:** Cloudflare Worker + Durable Object lobbies (codes in the settings, join by code or `?lobby=CODE`), avatars, text chat and quick chat, voice chat, one passenger in motorcycle / car / helicopter / plane (plane made a two-seater), firepit with benches, beer and logs, 25 more cars and 7 more motorcycles placed the same way for every player.
- **Why:** the user's goal (Cloudflare, no GitHub at the time, no own domain).
- **Where:** `cloud/`, `game-net.js`, `game-multiplayer.js`, `game-avatars.js`, `game-voice.js`, `game-fire.js`, `game-main.js`.

### 2026-10-24 · Soundtrack
- **What:** procedural music: a 2.5-minute calm open-world loop and one loop each for disc golf, petanque and the mission; music volume in the settings.
- **Where:** `game-music.js`.

### 2026-10-24 · Helicopter, plane, FPV drone, wristwatch, gear menu
- **What:** helicopter and plane flown with Mode 2 sticks (Battlefield / RC style), FPV drone (angle and acro modes), hold-△ menu for camera or drone, wristwatch on D-pad ↑.
- **Where:** `game-aircraft.js`, `game-watch.js`, `game-main.js`, `game-audio.js`.

### 2026-10-24 · Fixes from play-testing
- Motorcycle leans into the turn and its bars turn the right way; you can ride again after getting off (a vehicle no longer collides with its own parked outline); L2 reverses the bike.
- Car: open cabin, steering wheel tilted the right way, hands at ten and two that turn with the wheel.
- Disc golf disc always comes to rest; birds less frequent and almost silent indoors.

### 2026-10-24 · Open-world game
- **What:** the apartment viewer became an open-world game: neighbourhood, stairwells, mission, motorcycle, cars, excavator, disc golf, petanque, drinks, camera, map, ambient sound. Old viewer files were moved to `C:\Users\Lenovo\Documents\haarklous-plass-archive-2026-10-24`.
- **Why:** the user's goal.
