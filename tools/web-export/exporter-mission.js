/* Runs inside haarklous-game.html after exporter.js (tools/web-export/export.mjs): the hostage mission's figures.
 * Rigs (tools/web-export/rigs.d/mission.json):
 *   mission_hostile, mission_hostage  root at the feet (no pose), "body" (tips over when hit down), hostile: "muzzleFlash"
 *   mission_viewmodel                 root = the viewmodel camera (case-game.js vmScene, with its lights), "gun" (aim, bob,
 *                                     recoil, reload pose), "slide" (recoils), "flash" (muzzle flash)
 * The figures exist only while a mission runs, so the mission is started, its figures exported in a neutral pose and the
 * mission stopped again. The player's pose and the HUD are restored, so the exporters that follow see the page unchanged.
 */
(() => {
  'use strict';
  const X = window.__hkExport, G = window.haarklousGame, T = window.THREE, cg = G.caseGame;
  if (!cg) {X.log.push('mission: no case game in this page');return;}
  if (cg.active) {X.log.push('mission: already running, not exported');return;}
  const P = G.player, keep = ['x', 'z', 'feet', 'yaw', 'pitch', 'stance', 'eye', 'eyeNow', 'jumpY', 'jumpV', 'run', 'mode'], saved = {};
  keep.forEach(k => {saved[k] = P[k];});
  // The viewmodel scene is private to case-game.js: catch it when the mission draws its overlay once.
  let vmScene = null;
  const U = T.Object3D.prototype.updateMatrixWorld;
  T.Object3D.prototype.updateMatrixWorld = function (force) {
    if (this.isScene && this !== G.scene && !vmScene && this.children.some(c => c.isHemisphereLight) && this.children.some(c => c.isGroup)) vmScene = this;
    return U.call(this, force);
  };
  try {
    cg.start(false);
    cg.renderOverlay(G.camera);
  } finally {T.Object3D.prototype.updateMatrixWorld = U;}
  try {
    const actors = [...cg.debug.actors.values()];
    const exportActor = (name, a) => {
      if (!a) {X.log.push(`mission: no ${name}`);return;}
      const p = a.root.position.clone(), r = a.root.rotation.clone();
      a.root.position.set(0, 0, 0);a.root.rotation.set(0, 0, 0);
      const keys = new Map([[a.body, 'body']]);
      if (a.muzzleFlash) keys.set(a.muzzleFlash, 'muzzleFlash');
      X.exportRig(name, a.root, keys, {role: a.role});
      a.root.position.copy(p);a.root.rotation.copy(r);a.root.updateMatrixWorld(true);
    };
    exportActor('mission_hostile', actors.find(a => a.role === 'hostile'));
    exportActor('mission_hostage', actors.find(a => a.role === 'hostage'));
    if (!vmScene) X.log.push('mission: viewmodel scene not found');
    else {
      const gun = vmScene.children.find(c => c.isGroup), slide = gun.children.find(c => c.isGroup);
      const flash = gun.children.find(c => c.isMesh && c.geometry.type === 'ConeGeometry');
      const gp = gun.position.clone(), gr = gun.rotation.clone(), sp = slide.position.clone();
      gun.position.set(0, 0, 0);gun.rotation.set(0, 0, 0);slide.position.set(0, 0, 0);
      X.exportRig('mission_viewmodel', vmScene, new Map([[gun, 'gun'], [slide, 'slide'], [flash, 'flash']]), {fov: 62, near: .01, far: 5});
      gun.position.copy(gp);gun.rotation.copy(gr);slide.position.copy(sp);
    }
  } finally {
    cg.stop();
    keep.forEach(k => {P[k] = saved[k];});
    const n = document.getElementById('notice');if (n) n.hidden = true;
  }
})();
