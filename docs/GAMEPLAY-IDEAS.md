# Gameplay ideas and web-game bugs (not changed – they need your OK)

The Unreal version copies the web game's behaviour 1:1, including these bugs. Each one is a small fix in both games
once you say yes.

## Bugs found while porting (fixed in both games, 29 Oct 2026)
The user had them fixed in the web game (web PR #9, Unreal issues #7–#9); the Unreal version has the same fixes:
1. **Petanque never ended after 13** (the end of the last mène was counted every frame). Now no new turn starts once
   the finish timer runs (`FHKPetanque::Update`, `!bHasFinish`), so the score and the 150 kr reward count once.
2. **Petanque could freeze** when no boule was left in the court. Now whoever still has boules plays on
   (`FHKPetanque::PetanqueNext`, the web's `petanqueNext`); test `Haarklous.Minigames.PetanqueNext`.
3. **Vehicle obstacles appeared one update late** in a vehicle world made during play (the bike on its first ride drove
   through parked cars). Now a new world gets the last dynamic list at once (`FHKData::LastVehicleDynamic`).
Also fixed from the web (issue #1): the concrete barriers' end caps were filled as a fan, which left a see-through
gap on the concave profile; `site_geometry.prism()` now uses ear clipping (`tools/tests/test_site_geometry.py`).

## Not in the snapshot yet
- Web pull requests #18 (distant land below the real ground) and #19 (flying with the mouse) were still open on
  29 Oct 2026. They are ported from their branches (Unreal issues #14 and #13); refresh the snapshot once they are
  merged and check that nothing else changed.

## Ideas
- The web's "aerial imagery" ground option is not used in Unreal (the ground has real materials). It could come back
  as a map overlay if you miss it.
- **More parked cars** (realism pass, 2 Nov 2026): in Google Street View the streets around Haarklous plass are lined
  with parked cars, vans and taxis on both sides; the game has the web's 32 cars and 7 motorcycles within 330 m.
  More cars would be obstacles (and, if drivable, new vehicle numbers online), so they change gameplay: they wait
  for your OK. Two ways: more drivable cars in the web's generator (both games, same numbers), or parked-only cars
  that block like lamp posts (Unreal and web).
