/* Extra geometry exports for the minigames and the firepit (runs after exporter.js, see export.mjs).
 * rigs.d/minigames.json maps the rigs "discgolf", "petanque" and "fire" onto these exports, so those rigs get every part
 * that the C++ code (HKMinigames.cpp, HKFire.cpp) moves as its own node, the way the JavaScript moves its Object3Ds:
 *   discgolf: "disc" (hidden until thrown; position and spin set every frame)
 *   petanque: "npc" (Arne: turns), its children "armL" and "armR" (throwing arm); hidden templates "jack", "boule_you",
 *             "boule_npc" (the game makes its seven balls from them each mène, like makeBall())
 *   fire:     "charred" (the six glowing logs, emissive follows the fire level), hidden template "log" (thrown logs, like
 *             throwLog()). The flames (shader cards), embers and smoke (points) and the point light are left out: AHKFireEffects
 *             draws them in Unreal.
 * Nothing in the page is changed for good: temporary objects are removed and moved objects put back afterwards.
 */
(() => {
  'use strict';
  const {exportRig, log} = window.__hkExport, G = window.haarklousGame, T = window.THREE, lib = window.__hkLib;
  const cyl = (o, top) => o.isMesh && o.geometry.type === 'CylinderGeometry' && Math.abs(o.geometry.parameters.radiusTop - top) < 1e-9;
  const mesh = (geo, mat) => {const m = new T.Mesh(geo, mat);m.castShadow = m.receiveShadow = true;return m;};

  // ---------- Disc golf: the disc is the one mesh left after mergeStatic that is not merged ----------
  {
    const g = G.mini.discgolf.group, disc = g.children.find(o => cyl(o, .105));
    if (!disc) throw new Error('disc golf: disc not found');
    exportRig('mg_discgolf', g, new Map([[disc, 'disc']]));
  }

  // ---------- Petanque: Arne and his arms; ball templates with the same geometry and materials as makeBall() ----------
  {
    const g = G.mini.petanque.group, npc = g.children.find(o => o.isGroup), arms = npc ? npc.children.filter(o => o.isGroup) : [];
    if (!npc || arms.length !== 2) throw new Error('petanque: Arne not found');
    const jack = mesh(new T.SphereGeometry(.015, 20, 14), lib.paint(0xe8b400, .35)), you = mesh(new T.SphereGeometry(.037, 20, 14), lib.metal(0x9aa0a4, .25)), them = mesh(new T.SphereGeometry(.037, 20, 14), lib.metal(0x8a6a48, .3));
    const temps = [jack, you, them];temps.forEach(m => {m.visible = false;g.add(m);});
    exportRig('mg_petanque', g, new Map([[npc, 'npc'], [arms[0], 'armL'], [arms[1], 'armR'], [jack, 'jack'], [you, 'boule_you'], [them, 'boule_npc']]));
    temps.forEach(m => {g.remove(m);m.geometry.dispose();});
  }

  // ---------- Firepit ----------
  {
    const g = G.fire.group, kids = g.children.slice();
    const flames = kids.filter(o => o.isMesh && o.material && o.material.type === 'ShaderMaterial'), effects = kids.filter(o => o.isPoints || o.isLight);
    const logs = kids.filter(o => cyl(o, .045));
    let bark = null;g.traverse(o => {if (!bark && cyl(o, .075)) bark = o.material;});
    if (flames.length !== 6 || logs.length !== 6 || !bark) throw new Error(`fire: ${flames.length} flames, ${logs.length} logs, bark ${!!bark}`);
    // Out of the export: flames, embers, smoke, light. The six charred logs under one identity node "charred".
    [...flames, ...effects].forEach(o => g.remove(o));
    const charred = new T.Group();charred.name = 'charred';g.add(charred);logs.forEach(l => charred.add(l));
    const logTemplate = mesh(new T.CylinderGeometry(.07, .07, .8, 10), bark);logTemplate.receiveShadow = false;logTemplate.visible = false;g.add(logTemplate);
    exportRig('mg_fire', g, new Map([[charred, 'charred'], [logTemplate, 'log']]));
    // Put everything back in the original order.
    g.remove(logTemplate);logTemplate.geometry.dispose();g.remove(charred);
    while (g.children.length) g.remove(g.children[0]);
    kids.forEach(o => g.add(o));
  }
  log.push('exporter-minigames: disc, Arne, ball templates, charred logs, log template');
})();
