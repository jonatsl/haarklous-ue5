/* Runs inside haarklous-game.html after the game has built its world (driven by tools/web-export/export.mjs).
 * Collects the exact geometry, materials and gameplay data of the web version so the Unreal version can use the same map:
 *   window.__hkFiles = {name: {json: string, bin: Uint8Array|null}}
 * Geometry exports ("rigs"): nodes (root + moving parts, local matrices, three.js column-major, y up, metres),
 * meshes (positions/normals/uvs/colours/indices in their owner node's space, stored in the .bin), lights, materials.
 * Coordinates stay in the web game's frame; tools/blender and the C++ code convert to Unreal in one place.
 */
(() => {
  'use strict';
  const G = window.haarklousGame, T = window.THREE;
  const files = window.__hkFiles = {};
  const log = window.__hkLog = [];

  // ---------- Materials and textures (shared by all exports) ----------
  const matIds = new Map(), mats = [], texIds = new Map(), texs = [];
  function texRef(t) {
    if (!t) return null;
    if (!texIds.has(t)) {
      const id = texs.length;texIds.set(t, id);
      let png = null;const img = t.image;
      try {
        if (img && img.toDataURL) png = img.toDataURL('image/png');
        else if (img && img.width) {const c = document.createElement('canvas');c.width = img.width;c.height = img.height;c.getContext('2d').drawImage(img, 0, 0);png = c.toDataURL('image/png');}
      } catch (e) {log.push('texture ' + id + ': ' + e.message);}
      texs.push({id, w: img ? img.width : 0, h: img ? img.height : 0, png, srgb: t.colorSpace === T.SRGBColorSpace, repeat: [t.repeat.x, t.repeat.y], offset: [t.offset.x, t.offset.y], rotation: t.rotation, wrap: [t.wrapS === T.RepeatWrapping, t.wrapT === T.RepeatWrapping], flipY: t.flipY});
    }
    return texIds.get(t);
  }
  const hex = c => c && c.isColor ? c.getHex() : null;
  function matRef(m) {
    if (!m) return -1;
    if (!matIds.has(m)) {
      const id = mats.length;matIds.set(m, id);
      mats.push({id, type: m.type, name: m.name || '', hk: m.userData.hk || null, color: hex(m.color), roughness: m.roughness, metalness: m.metalness, opacity: m.opacity, transparent: m.transparent, side: m.side,
        emissive: hex(m.emissive), emissiveIntensity: m.emissiveIntensity, vertexColors: !!m.vertexColors, alphaTest: m.alphaTest, depthWrite: m.depthWrite, depthTest: m.depthTest,
        clearcoat: m.clearcoat, clearcoatRoughness: m.clearcoatRoughness, transmission: m.transmission, envMapIntensity: m.envMapIntensity, normalScale: m.normalScale ? [m.normalScale.x, m.normalScale.y] : null,
        blending: m.blending, toneMapped: m.toneMapped, flatShading: !!m.flatShading, fog: m.fog, polygonOffset: !!m.polygonOffset,
        map: texRef(m.map), normalMap: texRef(m.normalMap), roughnessMap: texRef(m.roughnessMap), alphaMap: texRef(m.alphaMap), emissiveMap: texRef(m.emissiveMap),
        bumpMap: texRef(m.bumpMap), aoMap: texRef(m.aoMap), metalnessMap: texRef(m.metalnessMap)});
    }
    return matIds.get(m);
  }

  // ---------- Binary buffer ----------
  class Bin {
    constructor() {this.chunks = [];this.size = 0;}
    add(arr) {const off = this.size, bytes = new Uint8Array(arr.buffer, arr.byteOffset, arr.byteLength).slice();this.chunks.push(bytes);this.size += bytes.byteLength;return {off, n: arr.length, t: arr instanceof Float32Array ? 'f32' : 'u32'};}
    bytes() {const out = new Uint8Array(this.size);let o = 0;for (const c of this.chunks) {out.set(c, o);o += c.byteLength;}return out;}
  }
  const M = a => Array.from(a.elements).map(v => Math.round(v * 1e6) / 1e6);
  function decompose(m) {const p = new T.Vector3(), q = new T.Quaternion(), s = new T.Vector3();m.decompose(p, q, s);return {pos: p.toArray(), quat: q.toArray(), scale: s.toArray()};}

  function geometry(g, matrix, bin) {
    const P = g.attributes.position;if (!P) return null;
    const n = P.count, pos = new Float32Array(n * 3), v = new T.Vector3(), nm = new T.Matrix3().getNormalMatrix(matrix);
    for (let i = 0; i < n; i++) {v.fromBufferAttribute(P, i).applyMatrix4(matrix);pos[i * 3] = v.x;pos[i * 3 + 1] = v.y;pos[i * 3 + 2] = v.z;}
    const out = {count: n, pos: bin.add(pos)};
    const N = g.attributes.normal;
    if (N) {const nor = new Float32Array(n * 3);for (let i = 0; i < n; i++) {v.fromBufferAttribute(N, i).applyMatrix3(nm).normalize();nor[i * 3] = v.x;nor[i * 3 + 1] = v.y;nor[i * 3 + 2] = v.z;}out.nor = bin.add(nor);}
    const U = g.attributes.uv;
    if (U) {const uv = new Float32Array(n * 2);for (let i = 0; i < n; i++) {uv[i * 2] = U.getX(i);uv[i * 2 + 1] = U.getY(i);}out.uv = bin.add(uv);}
    const C = g.attributes.color;
    if (C) {const col = new Float32Array(n * 3);for (let i = 0; i < n; i++) {col[i * 3] = C.getX(i);col[i * 3 + 1] = C.getY(i);col[i * 3 + 2] = C.getZ(i);}out.col = bin.add(col);}
    if (g.index) out.idx = bin.add(Uint32Array.from(g.index.array));
    if (g.groups && g.groups.length) out.groups = g.groups.map(x => ({start: x.start, count: x.count, mat: x.materialIndex}));
    out.flip = matrix.determinant() < 0;
    return out;
  }
  const userFlags = o => {const u = o.userData || {}, f = {};for (const k of ['doorId', 'lockedDoor', 'partName', 'keep', 'merged']) if (u[k] !== undefined) f[k] = u[k];if (u.drink) f.drink = G.apt.drinks.indexOf(u.drink);return Object.keys(f).length ? f : undefined;};

  /* One export: root object, a Map of moving parts (Object3D -> name). Every mesh belongs to its nearest named ancestor. */
  function exportRig(name, root, keys = new Map(), extra = {}) {
    root.updateMatrixWorld(true);
    const bin = new Bin(), nodes = [], nodeOf = new Map(), meshes = [], lights = [], skipped = {};
    nodes.push({name: 'root', parent: -1, matrix: M(root.matrixWorld), ...decompose(root.matrixWorld), visible: root.visible, type: root.type, objName: root.name});nodeOf.set(root, 0);
    root.traverse(o => {
      if (o === root || !keys.has(o)) return;
      let p = o.parent;while (p && !nodeOf.has(p)) p = p.parent;
      const owner = p || root, local = new T.Matrix4().copy(owner.matrixWorld).invert().multiply(o.matrixWorld);
      let vis = o.visible;for (let q = o.parent; q && q !== owner; q = q.parent) if (!q.visible) vis = false;
      nodes.push({name: keys.get(o), parent: nodeOf.get(owner), matrix: M(local), ...decompose(local), visible: vis, type: o.type, objName: o.name, flags: userFlags(o)});nodeOf.set(o, nodes.length - 1);
    });
    root.traverse(o => {
      if (o.isLight) {
        let p = o;while (p && !nodeOf.has(p)) p = p.parent;const owner = p || root, local = new T.Matrix4().copy(owner.matrixWorld).invert().multiply(o.matrixWorld);
        lights.push({node: nodeOf.get(owner), type: o.type, color: hex(o.color), intensity: o.intensity, distance: o.distance, decay: o.decay, angle: o.angle, penumbra: o.penumbra, castShadow: o.castShadow, visible: o.visible, ...decompose(local)});
        return;
      }
      if (!o.isMesh) {if (o.isLine || o.isPoints || o.isSprite) skipped[o.type] = (skipped[o.type] || 0) + 1;return;}
      let p = o;while (p && !nodeOf.has(p)) p = p.parent;
      const owner = p || root, ni = nodeOf.get(owner), local = new T.Matrix4().copy(owner.matrixWorld).invert().multiply(o.matrixWorld);
      let vis = true;for (let q = o; q && q !== owner; q = q.parent) if (!q.visible) vis = false;
      const mat = Array.isArray(o.material) ? o.material.map(matRef) : matRef(o.material);
      const entry = {node: ni, name: o.name || '', type: o.geometry.type, params: o.geometry.parameters || null, mat, visible: vis, cast: o.castShadow, receive: o.receiveShadow, renderOrder: o.renderOrder, flags: userFlags(o)};
      if (o.isInstancedMesh) {
        entry.geo = geometry(o.geometry, new T.Matrix4(), bin);
        const im = new Float32Array(o.count * 16), tmp = new T.Matrix4();
        for (let i = 0; i < o.count; i++) {o.getMatrixAt(i, tmp);tmp.premultiply(local);im.set(tmp.elements, i * 16);}
        entry.instances = bin.add(im);entry.instanceCount = o.count;
        if (o.instanceColor) entry.instanceColors = bin.add(new Float32Array(o.instanceColor.array));
      } else entry.geo = geometry(o.geometry, local, bin);
      if (entry.geo) meshes.push(entry);
    });
    files[name] = {json: JSON.stringify({name, nodes, meshes, lights, skipped, ...extra}), bin: bin.bytes()};
    log.push(`${name}: ${nodes.length} nodes, ${meshes.length} meshes, ${lights.length} lights, ${(bin.size / 1048576).toFixed(1)} MB`);
  }
  // Moving parts of a game object: its own Object3D properties (and arrays / small objects of them) inside the root.
  function keysOf(obj, root, prefix = '', keys = new Map(), depth = 0) {
    const within = o => {for (let p = o; p; p = p.parent) if (p === root) return true;return false;};
    for (const [k, v] of Object.entries(obj)) {
      if (!v || typeof v !== 'object' || k === 'group' || k === 'spawn' || k === 'netCur') continue;
      if (v.isObject3D) {if (v !== root && within(v) && !keys.has(v)) keys.set(v, prefix + k);}
      else if (Array.isArray(v)) v.forEach((e, i) => {if (e && e.isObject3D && e !== root && within(e) && !keys.has(e)) keys.set(e, `${prefix}${k}[${i}]`);else if (e && typeof e === 'object' && !e.isObject3D && depth < 2) keysOf(e, root, `${prefix}${k}[${i}].`, keys, depth + 1);});
      else if (!v.isMaterial && !v.isBufferGeometry && !v.isVector3 && !v.isQuaternion && depth < 2 && Object.getPrototypeOf(v) === Object.prototype) keysOf(v, root, prefix + k + '.', keys, depth + 1);
    }
    return keys;
  }

  // ---------- Geometry exports ----------
  const W = G.world, apt = G.apt;
  exportRig('world_near', W.near);
  exportRig('world_far', W.far);
  {
    const keys = new Map();
    G.doors.filter(d => apt.doors.includes(d)).forEach(d => keys.set(d.hinge, 'door:' + d.id));
    apt.drinks.forEach((d, i) => {keys.set(d.object, 'drink:' + i);if (d.object.userData.liquid) keys.set(d.object.userData.liquid, 'liquid:' + i);});
    exportRig('apartment', apt.group, keys);
  }
  G.towers.forEach((t, i) => {
    const keys = new Map();t.stair.doors.forEach(d => keys.set(d.hinge, 'door:' + d.id));
    exportRig(i === 0 ? 'stair_own' : 'stair_b' + (i - 1), t.stair.group, keys, {tower: i, own: !!t.own});
  });
  exportRig('fire', G.fire.group, keysOf(G.fire, G.fire.group));
  G.vehicles.forEach((v, i) => exportRig('vehicle_' + i, v.group, keysOf(v, v.group), {kind: v.kind, vname: v.name}));
  exportRig('drone', G.drone.group || G.drone.model || new T.Group(), G.drone.group ? keysOf(G.drone, G.drone.group) : new Map());
  exportRig('discgolf', G.mini.discgolf.group, keysOf(G.mini.discgolf, G.mini.discgolf.group));
  exportRig('petanque', G.mini.petanque.group, keysOf(G.mini.petanque, G.mini.petanque.group));
  exportRig('photo_camera', G.photo.model, keysOf(G.photo, G.photo.model));
  exportRig('watch', G.watch.model, keysOf(G.watch, G.watch.model));
  // Everything else directly in the scene that no export above covers (mission marker, dirt piles ...).
  {
    const covered = new Set([W.near, apt.group, G.fire.group, G.mini.discgolf.group, G.mini.petanque.group, ...G.vehicles.map(v => v.group), ...G.towers.map(t => t.stair.group)]);
    const misc = new T.Group(), list = [];
    for (const o of G.scene.children) {let c = false;o.traverse(x => {if (covered.has(x)) c = true;});if (!c && !o.isLight && !o.isCamera) list.push(o);}
    const keys = new Map();list.forEach((o, i) => keys.set(o, 'misc:' + i + ':' + (o.name || o.type)));
    // Temporarily reparent under one root for the export, then restore.
    const parents = list.map(o => o.parent);list.forEach(o => misc.add(o));G.scene.add(misc);
    exportRig('misc', misc, keys);
    list.forEach((o, i) => parents[i].add(o));G.scene.remove(misc);
  }
  files.materials = {json: JSON.stringify({materials: mats, textures: texs.map(t => ({...t, png: undefined}))}), bin: null};
  texs.forEach(t => {if (t.png) files['tex_' + t.id] = {json: t.png, bin: null};});
  log.push(`materials: ${mats.length}, textures: ${texs.length}`);
  window.__hkDone = true;
})();
