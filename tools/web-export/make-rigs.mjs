// Turn the geometry exports (tools/out/web-export/*.json) into
//   unreal/Haarklous/Content/Data/rigs/<rig>.json  - node trees the C++ code loads (FHKRig::LoadFromJson) and animates
//   tools/out/build-plan/<rig>.json                - which meshes Blender merges into which static mesh asset
// Both use the same asset names, so the C++ rigs find the meshes that Blender and the Unreal import made.
//   node tools/web-export/make-rigs.mjs
// Rules: every named node's own meshes merge into one asset "SM_<rig>__<node>" (one material slot per material).
// Meshes that are hidden at start or carry interaction flags (doors, locked doors, drinks) get their own asset as a
// "part" child node "<node>#<i>", so the game can show/hide them or aim at them on their own.
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
const src = path.join(root, 'tools', 'out', 'web-export'), rigsOut = path.join(root, 'unreal', 'Haarklous', 'Content', 'Data', 'rigs'), planOut = path.join(root, 'tools', 'out', 'build-plan');
fs.mkdirSync(rigsOut, {recursive: true});fs.mkdirSync(planOut, {recursive: true});
const materials = JSON.parse(fs.readFileSync(path.join(src, 'materials.json'), 'utf8')).materials;

// One rig per kind of model. Vehicles of one kind share a rig (cars differ only in paint colour, set at run time).
const rigs = [
  ['car', 'vehicle_0'], ['motorcycle', 'vehicle_5'], ['excavator', 'vehicle_6'], ['helicopter', 'vehicle_7'], ['plane', 'vehicle_8'],
  ['fire', 'fire'], ['discgolf', 'discgolf'], ['petanque', 'petanque'], ['photo_camera', 'photo_camera'], ['watch', 'watch'], ['misc', 'misc'],
  ['apartment', 'apartment', true], ['stair_own', 'stair_own', true], ...Array.from({length: 8}, (_, i) => [`stair_b${i}`, `stair_b${i}`, true]),
  ['world_near', 'world_near', true], ['world_far', 'world_far', true]
];
// Extra rigs added by module ports: tools/web-export/rigs.d/*.json = [[rig, exportFile, levelStatic?], ...]
const extraDir = path.join(path.dirname(fileURLToPath(import.meta.url)), 'rigs.d');
if (fs.existsSync(extraDir)) for (const f of fs.readdirSync(extraDir).filter(f => f.endsWith('.json')).sort()) rigs.push(...JSON.parse(fs.readFileSync(path.join(extraDir, f), 'utf8')));
const safe = s => s.replace(/[^A-Za-z0-9]+/g, '_').replace(/^_+|_+$/g, '');
const special = m => !m.visible || (m.flags && (m.flags.doorId !== undefined || m.flags.lockedDoor || m.flags.drink !== undefined));
const matKey = id => {
  const m = materials[id];if (!m) return 'M_missing';
  const hex = c => c === null || c === undefined ? '' : '_' + c.toString(16).padStart(6, '0');
  return m.hk ? `MI_${m.hk.fn}${m.hk.args.filter(a => typeof a === 'number' && a > 1).map(a => '_' + a.toString(16)).join('')}` : `MI_${m.type.replace('Mesh', '').replace('Material', '')}${hex(m.color)}_${id}`;
};
let total = 0;
for (const [rig, file, levelStatic] of rigs) {
  if (!fs.existsSync(path.join(src, file + '.json'))) {console.log(`missing ${file}`);continue;}
  const j = JSON.parse(fs.readFileSync(path.join(src, file + '.json'), 'utf8'));
  const nodes = j.nodes.map((n, i) => ({name: n.name, parent: n.parent, pos: n.pos, quat: n.quat, scale: n.scale, visible: n.visible, flags: n.flags || undefined, index: i}));
  const plan = [], partNodes = [];
  nodes.forEach((n, ni) => {
    const own = j.meshes.map((m, mi) => ({m, mi})).filter(x => x.m.node === ni);
    const merged = own.filter(x => !special(x.m)), parts = own.filter(x => special(x.m));
    const slotsOf = list => [...new Set(list.flatMap(x => [].concat(x.m.mat)))];
    if (merged.length) {
      const key = `SM_${rig}__${safe(n.name)}`, slots = slotsOf(merged);
      n.mesh = key;n.slots = slots.map(matKey);
      plan.push({asset: key, export: file, node: ni, meshes: merged.map(x => x.mi), slots, slotKeys: n.slots, levelStatic: !!levelStatic && ni === 0});
    }
    parts.forEach((x, k) => {
      const key = `SM_${rig}__${safe(n.name)}__p${k}`, slots = slotsOf([x]);
      partNodes.push({name: `${n.name}#${k}`, parent: ni, pos: [0, 0, 0], quat: [0, 0, 0, 1], scale: [1, 1, 1], visible: x.m.visible, flags: x.m.flags, mesh: key, slots: slots.map(matKey), meshName: x.m.name, geoType: x.m.type});
      plan.push({asset: key, export: file, node: ni, meshes: [x.mi], slots, slotKeys: slots.map(matKey), part: true});
    });
  });
  const allNodes = [...nodes, ...partNodes].map(({index, ...n}) => n);
  const lights = j.lights.map(l => ({node: l.node, type: l.type, color: l.color, intensity: l.intensity, distance: l.distance, decay: l.decay, pos: l.pos, castShadow: l.castShadow, visible: l.visible}));
  fs.writeFileSync(path.join(rigsOut, rig + '.json'), JSON.stringify({rig, source: file, levelStatic: !!levelStatic, nodes: allNodes, lights}, null, 0));
  fs.writeFileSync(path.join(planOut, rig + '.json'), JSON.stringify({rig, source: file, assets: plan}));
  total += plan.length;
  console.log(`${rig}: ${allNodes.length} nodes, ${plan.length} mesh assets`);
}
// Material keys for the Unreal material library (one instance per key).
const keys = {};materials.forEach((m, id) => {const k = matKey(id);(keys[k] = keys[k] || []).push(id);});
fs.writeFileSync(path.join(planOut, 'material-keys.json'), JSON.stringify(Object.entries(keys).map(([key, ids]) => ({key, ids, ...materials[ids[0]]}))));
console.log(`${total} mesh assets, ${Object.keys(keys).length} material keys`);
