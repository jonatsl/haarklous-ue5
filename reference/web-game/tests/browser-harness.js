/* Browser test harness for haarklous-game.html (load it in the page console:
 *   eval(await (await fetch('tests/browser-harness.js')).text())
 * Drives the game with a simulated DualShock 4 and runs frames on a timer (works in hidden/background tabs).
 * Button indices follow the W3C standard mapping: 0 ✕, 1 ○, 2 □, 3 △, 4 L1, 5 R1, 6 L2, 7 R2, 8 Share, 9 Options,
 * 10 L3, 11 R3, 12-15 D-pad up/down/left/right, 16 PS, 17 touchpad. Axes: 0/1 left stick, 2/3 right stick.
 */
window.requestAnimationFrame = cb => setTimeout(() => cb(performance.now()), 33);
window.fakePad = {id: 'Wireless Controller (STANDARD GAMEPAD Vendor: 054c Product: 05c4)', mapping: 'standard', connected: true, axes: [0, 0, 0, 0], buttons: Array.from({length: 18}, () => ({pressed: false, value: 0}))};
Object.defineProperty(navigator, 'getGamepads', {value: () => [window.fakePad], configurable: true});
window.wait = ms => new Promise(r => setTimeout(r, ms));
window.hold = (i, v = 1) => {window.fakePad.buttons[i] = {pressed: v > .5, value: v};};
window.press = async (i, ms = 80) => {hold(i, 1);await wait(ms);hold(i, 0);await wait(160);};
window.stick = async (lx, ly, ms, rx = 0, ry = 0) => {window.fakePad.axes = [lx, ly, rx, ry];await wait(ms);window.fakePad.axes = [0, 0, 0, 0];await wait(90);};
window.P = () => {const p = haarklousGame.player;return `(${p.x.toFixed(2)}, ${p.z.toFixed(2)}, feet ${p.feet.toFixed(2)}) ${p.mode}`;};
window.promptText = () => document.getElementById('prompt').hidden ? '' : document.getElementById('prompt').textContent;
window.walkTo = async (x, z, maxMs = 15000) => {
  const g = haarklousGame, t0 = performance.now();
  while (performance.now() - t0 < maxMs) {
    const p = g.player, dx = x - p.x, dz = z - p.z, d = Math.hypot(dx, dz);if (d < .35) break;
    p.yaw = Math.atan2(-dx, -dz);window.fakePad.axes = [0, -1, 0, 0];await wait(60);
  }
  window.fakePad.axes = [0, 0, 0, 0];await wait(100);return P();
};
haarklousGame.kick();
