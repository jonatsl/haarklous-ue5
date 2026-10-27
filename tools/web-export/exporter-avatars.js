/* Extra geometry export (run by export.mjs after exporter.js): the other players' avatars and their FPV drone,
 * made by the web's own game-avatars.js. One rig per player slot (the jacket material differs), named like the C++
 * port (HKAvatars.cpp) finds them: hips, torso, neck, head, L.arm.sh, L.arm.el, R.arm.sh, R.arm.el, L.leg.hip,
 * L.leg.kn, R.leg.hip, R.leg.kn, bottle. The name tag and chat bubble sprites are drawn by the Unreal HUD instead.
 */
(() => {
  'use strict';
  const X = window.__hkExport, T = window.THREE, log = X.log;
  const A = window.GameAvatars.create({T, lib: window.__hkLib});
  // The avatar's groups are closure variables in the web code; they are found by the fixed build order of avatar().
  const groups = o => o.children.filter(c => !c.isMesh && !c.isSprite);
  for (let slot = 0; slot < 5; slot++) {
    const av = A.avatar(slot, 'Player'), g = av.group;
    const [hips] = groups(g), [torso, hipL, hipR] = groups(hips), [neck, shL, shR] = groups(torso), [head] = groups(neck);
    const [elL] = groups(shL), [elR] = groups(shR), [knL] = groups(hipL), [knR] = groups(hipR), [bottle] = groups(elR);
    const near = (v, x, y, z) => Math.abs(v.x - x) < 1e-9 && Math.abs(v.y - y) < 1e-9 && Math.abs(v.z - z) < 1e-9;
    const ok = near(hips.position, 0, .95, 0) && near(neck.position, 0, .58, 0) && near(head.position, 0, .14, 0) && near(shL.position, -.22, .5, 0) && near(shR.position, .22, .5, 0) &&
      near(elL.position, 0, -.29, 0) && near(hipL.position, -.1, -.05, 0) && near(hipR.position, .1, -.05, 0) && near(knL.position, 0, -.43, 0) && near(bottle.position, 0, -.3, -.04) && !bottle.visible;
    if (!ok) throw new Error('game-avatars.js changed its build order: update exporter-avatars.js and HKAvatars.cpp');
    const keys = new Map([[hips, 'hips'], [torso, 'torso'], [neck, 'neck'], [head, 'head'], [shL, 'L.arm.sh'], [elL, 'L.arm.el'], [shR, 'R.arm.sh'], [elR, 'R.arm.el'],
      [hipL, 'L.leg.hip'], [knL, 'L.leg.kn'], [hipR, 'R.leg.hip'], [knR, 'R.leg.kn'], [bottle, 'bottle']]);
    X.exportRig('avatar_' + slot, g, keys, {slot, jacket: A.SLOT_COLORS[slot], tag: A.SLOT_CSS[slot]});
    av.dispose();
  }
  X.exportRig('avatar_drone', A.drone(), new Map());
  log.push('avatars: 5 slots + drone');
})();
