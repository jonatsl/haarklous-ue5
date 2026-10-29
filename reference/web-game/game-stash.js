/* Hidden stashes (issue #2): three wooden crates around the neighbourhood, each with lines to snort and a syringe.
 * Using either gives a random 30-second boost (handled in game-main.js): 3× running speed, 3× jump height or bullet
 * time. A used item is back after 90 seconds.
 * GameStash.create({T, lib, places: [{x, y, z, yaw, name}]}) -> {group, items, hitboxes, obstacles, nearest(p, feet), update(dt), use(item)}
 * item: {kind: 'line' | 'needle', place, ready, x, y, z, hit}
 */
(function (root, factory) {
  root.GameStash = factory();
})(typeof window === 'undefined' ? globalThis : window, function () {
  'use strict';
  const COOLDOWN = 90;
  function create({T, lib, places}) {
    const group = new T.Group(), items = [], hitboxes = [], obstacles = [];
    const mesh = (parent, geo, mat, x = 0, y = 0, z = 0) => {const m = new T.Mesh(geo, mat);m.position.set(x, y, z);m.castShadow = true;m.receiveShadow = true;parent.add(m);return m;};
    const crateM = lib.wood(0x8a6a44, .8), slatM = lib.wood(0x6e5232, .85), mirrorM = new T.MeshStandardMaterial({color: 0x15181c, roughness: .08, metalness: .9});
    const powderM = new T.MeshStandardMaterial({color: 0xf4f4f0, roughness: .95}), noteM = new T.MeshStandardMaterial({color: 0x8fae7d, roughness: .8});
    const barrelM = new T.MeshPhysicalMaterial({color: 0xe8f0f4, transparent: true, opacity: .55, roughness: .15}), plungerM = new T.MeshStandardMaterial({color: 0xf2f2f2, roughness: .5});
    const steelM = lib.metal(0xd0d4d8, .25), capM = new T.MeshStandardMaterial({color: 0xe0661c, roughness: .6}), bandM = new T.MeshStandardMaterial({color: 0x9a6a3c, roughness: .7});
    const hitM = new T.MeshBasicMaterial({visible: false});
    for (const place of places) {
      const g = new T.Group();g.position.set(place.x, place.y, place.z);g.rotation.y = place.yaw || 0;group.add(g);
      // Crate (0.5 × 0.36 × 0.38 m) with slats.
      mesh(g, new T.BoxGeometry(.5, .34, .38), crateM, 0, .17, 0);
      for (const y of [.06, .2, .32]) for (const s of [-1, 1]) mesh(g, new T.BoxGeometry(.52, .05, .012), slatM, 0, y, s * .195);
      // Left: a small mirror with three lines and a rolled-up note.
      const tray = new T.Group();tray.position.set(-.11, .345, 0);g.add(tray);
      mesh(tray, new T.BoxGeometry(.24, .008, .17), mirrorM, 0, 0, 0);
      const lines = [-.04, 0, .04].map(z => mesh(tray, new T.BoxGeometry(.11, .004, .007), powderM, .01, .006, z));
      const note = mesh(tray, new T.CylinderGeometry(.005, .005, .075, 8), noteM, -.06, .01, .065);note.rotation.z = Math.PI / 2;note.rotation.y = .4;
      // Right: a syringe with an orange cap and a rubber tourniquet.
      const kit = new T.Group();kit.position.set(.13, .345, 0);g.add(kit);
      const syringe = new T.Group();syringe.rotation.set(0, .5, Math.PI / 2);syringe.position.y = .012;kit.add(syringe);
      mesh(syringe, new T.CylinderGeometry(.009, .009, .085, 12), barrelM, 0, 0, 0);mesh(syringe, new T.CylinderGeometry(.0025, .0025, .05, 6), plungerM, 0, .06, 0);
      mesh(syringe, new T.CylinderGeometry(.012, .012, .004, 12), plungerM, 0, .085, 0);mesh(syringe, new T.CylinderGeometry(.0008, .0008, .03, 4), steelM, 0, -.058, 0);
      mesh(syringe, new T.CylinderGeometry(.004, .003, .032, 8), capM, 0, -.06, 0);
      const band = mesh(kit, new T.TorusGeometry(.04, .005, 6, 20), bandM, -.005, .006, .05);band.rotation.x = Math.PI / 2;band.scale.set(1, .6, 1);
      for (const [kind, holder, parts] of [['line', tray, lines], ['needle', kit, [syringe, band]]]) {
        const hit = mesh(g, new T.BoxGeometry(.24, .2, .26), hitM, holder.position.x, .42, 0);hit.castShadow = hit.receiveShadow = false;
        const item = {kind, place, ready: true, t: 0, parts, hit, x: 0, y: 0, z: 0};hit.userData.stashItem = item;
        g.updateMatrixWorld(true);const w = holder.getWorldPosition(new T.Vector3());item.x = w.x;item.y = w.y;item.z = w.z;
        items.push(item);hitboxes.push(hit);
      }
      // Walkers bump into the crate.
      const c = Math.cos(place.yaw || 0), s = Math.sin(place.yaw || 0), pt = (a, b) => ({x: place.x + a * c + b * s, z: place.z - a * s + b * c});
      obstacles.push({points: [pt(-.27, -.21), pt(.27, -.21), pt(.27, .21), pt(-.27, .21)], minY: place.y, maxY: place.y + .36, kind: 'stash'});
    }
    function use(item) {item.ready = false;item.t = COOLDOWN;item.parts.forEach(p => {p.visible = false;});}
    function update(dt) {for (const it of items) if (!it.ready && (it.t -= dt) <= 0) {it.ready = true;it.parts.forEach(p => {p.visible = true;});}}
    // The stash item closest to a standing player (within reach), ready ones first.
    function nearest(p, feet) {
      let best = null, bd = 1.5;
      for (const it of items) {const d = Math.hypot(it.x - p.x, it.z - p.z) - (it.ready ? .3 : 0);if (d < bd && Math.abs(it.place.y - feet) < 1.2) {bd = d;best = it;}}
      return best;
    }
    return {group, items, hitboxes, obstacles, nearest, use, update, COOLDOWN};
  }
  return {create};
});
