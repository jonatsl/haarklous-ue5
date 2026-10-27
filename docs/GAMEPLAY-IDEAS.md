# Gameplay ideas and web-game bugs (not changed – they need your OK)

The Unreal version copies the web game's behaviour 1:1, including these bugs. Each one is a small fix in both games
once you say yes.

## Bugs found while porting
1. **Petanque never ends after 13.** When a side reaches 13, `endMene()` in `game-minigames.js` runs again every frame:
   the score keeps climbing ("Arne wins 7–13!", then 7–14 … 7–163) and the 4-second finish timer restarts every frame,
   so the game only ends when you press ○. Fix: stop calling `endMene()` once the finish timer is running.
2. **Petanque can freeze.** If no boule is left in the court and you have none left, `nextThrower()` calls `left(null)`,
   which throws an error; the update stops there every frame until ○. Fix: treat "no boule in the court" as the end of
   the mène.
3. **Vehicle obstacles appear one update late.** `updateDynamic()` only puts doors and vehicles into collision worlds
   that already exist; a world made later (for example the bike's 0.45 m world on the first ride) has no vehicle
   obstacles until the next update. Barely noticeable; fix by building the world before filling it.

## Not in the snapshot yet
- Web pull request #4 (three hidden stashes with 30-second boosts) was open when the snapshot was taken. Refresh the
  snapshot (`tools\refresh-reference.ps1`) after it is merged, then port it.

## Ideas
- The web's "aerial imagery" ground option is not used in Unreal (the ground has real materials). It could come back
  as a map overlay if you miss it.
