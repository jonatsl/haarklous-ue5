/* Extra geometry export for the first-person overlay models (runs after exporter.js, see export.mjs):
 * the wristwatch with its moving wrist group and the dial face (drawn live in Unreal), and the photo camera.
 */
(() => {
  'use strict';
  const G = window.haarklousGame, X = window.__hkExport;
  const model = G.watch.model, wrist = model.children[0];
  const keys = new Map([[wrist, 'wrist']]);
  // The dial is the circle with a canvas texture (MeshBasicMaterial); give it its own node so Unreal can draw on it.
  wrist.traverse(o => {if (o.isMesh && o.material && o.material.map && o.material.type === 'MeshBasicMaterial') keys.set(o, 'dial');});
  const vis = model.visible;model.visible = true;
  X.exportRig('watch', model, keys);
  model.visible = vis;
  const pv = G.photo.model.visible;G.photo.model.visible = true;
  X.exportRig('photo_camera', G.photo.model, new Map());
  G.photo.model.visible = pv;
})();
