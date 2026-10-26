/* Pure gamepad reading for the first-person walkthrough (PS4 DualShock 4 and
 * other controllers). No DOM access; app.js polls navigator.getGamepads().
 * read(gamepad) -> {move:{x,y}, look:{x,y}, run:R2 0..1, aim:L2 0..1, buttons:{name:boolean}}
 *   move/look: stick values after radial dead zone, y positive = stick down.
 * pressedEdges(previous, current) -> names newly pressed since the previous read
 * pickGamepad(list) -> first connected controller, preferring standard mapping
 * Browsers expose a controller only after a button press while the page is open.
 */
(function (root, factory) {
  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.GamepadControls = api;
})(typeof window === 'undefined' ? globalThis : window, function () {
  'use strict';
  const DEAD_ZONE = .14;
  // W3C standard mapping, used by Chrome/Edge for the DualShock 4.
  const STANDARD = {cross:0,circle:1,square:2,triangle:3,l1:4,r1:5,l2:6,r2:7,share:8,options:9,l3:10,r3:11,up:12,down:13,left:14,right:15,ps:16,touchpad:17};
  // Raw DirectInput order for Sony controllers without standard mapping (e.g. Firefox on Windows).
  const SONY_RAW = {square:0,cross:1,circle:2,triangle:3,l1:4,r1:5,l2:6,r2:7,share:8,options:9,l3:10,r3:11,ps:12,touchpad:13};
  const isSony = id => /054c|sony|dualshock|wireless controller/i.test(id || '');

  function deadZone(x, y) {
    const length = Math.hypot(x || 0, y || 0);
    if (length <= DEAD_ZONE) return {x:0, y:0};
    // Rescale so output starts at 0 just outside the dead zone and is capped at 1.
    const scaled = Math.min(1, (length - DEAD_ZONE) / (1 - DEAD_ZONE)) / length;
    return {x:x*scaled, y:y*scaled};
  }
  const value = button => !button ? 0 : typeof button === 'object' ? (button.value || (button.pressed ? 1 : 0)) : Number(button) || 0;
  const pressed = button => !!button && (typeof button === 'object' ? button.pressed || button.value > .5 : Number(button) > .5);

  function read(gamepad) {
    const axes = gamepad.axes || [], raw = gamepad.buttons || [];
    const standard = gamepad.mapping === 'standard' || !isSony(gamepad.id);
    const map = standard ? STANDARD : SONY_RAW;
    const buttons = {};
    for (const [name, index] of Object.entries(map)) buttons[name] = pressed(raw[index]);
    // Raw Sony layout reports right stick on axes 2/5 and the D-pad as a hat on axis 9.
    const look = standard || axes.length < 6 ? deadZone(axes[2], axes[3]) : deadZone(axes[2], axes[5]);
    if (!standard) {
      // Neutral hat reads about 1.29, outside the -1..1 direction range.
      const hat = axes.length > 9 && Math.abs(axes[9]) <= 1.01 ? Math.round((axes[9] + 1) * 3.5) : -1;
      Object.assign(buttons, {up:[0,1,7].includes(hat), right:[1,2,3].includes(hat), down:[3,4,5].includes(hat), left:[5,6,7].includes(hat)});
    }
    return {move:deadZone(axes[0], axes[1]), look, run:value(raw[map.r2]), aim:value(raw[map.l2]), buttons};
  }
  function pressedEdges(previous, current) {
    return Object.keys(current.buttons).filter(name => current.buttons[name] && !(previous && previous.buttons[name]));
  }
  function pickGamepad(list) {
    const pads = Array.from(list || []).filter(pad => pad && pad.connected !== false);
    return pads.find(pad => pad.mapping === 'standard') || pads[0] || null;
  }
  return {read, pressedEdges, pickGamepad, deadZone, DEAD_ZONE};
});
