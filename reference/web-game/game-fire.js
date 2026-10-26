/* Firepit in the park in front of Haarklous plass 17A: a stone ring with burning logs, five benches around it (ten seats),
 * a woodpile to throw more logs on, and a crate of beer. The fire level (0–6) sets flame size, light, embers, smoke and
 * sound; it burns down by one level every 90 s (the level is shared in multiplayer).
 * GameFire.create({T, lib, heightAt, x, z}) -> {group, center, spots, woodpile, crate, obstacles, update(dt, level, camera),
 *   throwLog(from), DECAY_S, MAX}
 */
(function (root, factory) {
  root.GameFire = factory();
})(typeof window === 'undefined' ? globalThis : window, function () {
  'use strict';
  const DECAY_S = 90, MAX = 6;
  function create({T, lib, heightAt, x, z}) {
    const y0 = heightAt(x, z), group = new T.Group();group.position.set(x, y0, z);
    const mesh = (parent, geo, mat, px = 0, py = 0, pz = 0) => {const m = new T.Mesh(geo, mat);m.position.set(px, py, pz);m.castShadow = m.receiveShadow = true;parent.add(m);return m;};
    let seed = 7;const rnd = () => (seed = (seed * 16807) % 2147483647) / 2147483647;
    // Stone ring, ash bed, charred logs.
    const stone = lib.concrete(0x807b73), ash = new T.MeshStandardMaterial({color: 0x2b2826, roughness: 1}), charred = new T.MeshStandardMaterial({color: 0x2a1c12, roughness: .9, emissive: 0x6a1800, emissiveIntensity: 0});
    for (let i = 0; i < 15; i++) {const a = i / 15 * Math.PI * 2, s = mesh(group, new T.DodecahedronGeometry(.15 + rnd() * .06, 0), stone, Math.cos(a) * .78, .09, Math.sin(a) * .78);s.rotation.set(rnd() * 3, rnd() * 3, rnd() * 3);s.scale.set(1.2, .8, 1);}
    const bed = mesh(group, new T.CircleGeometry(.7, 24), ash, 0, .03, 0);bed.rotation.x = -Math.PI / 2;
    for (let i = 0; i < 6; i++) {const a = i / 6 * Math.PI * 2, l = mesh(group, new T.CylinderGeometry(.045, .055, .85, 8), charred, Math.cos(a) * .22, .3, Math.sin(a) * .22);l.rotation.set(0, -a, 0);l.rotateZ(.55);}
    // Flames: camera-facing cards with a noise shader; embers; smoke.
    const flameMat = new T.ShaderMaterial({transparent: true, depthWrite: false, blending: T.AdditiveBlending, side: T.DoubleSide,
      uniforms: {time: {value: 0}, power: {value: 1}},
      vertexShader: 'varying vec2 vUv; void main(){vUv=uv; gl_Position=projectionMatrix*modelViewMatrix*vec4(position,1.0);}',
      fragmentShader: `uniform float time; uniform float power; varying vec2 vUv;
        float h(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}
        float n(vec2 p){vec2 i=floor(p),f=fract(p);f=f*f*(3.0-2.0*f);return mix(mix(h(i),h(i+vec2(1,0)),f.x),mix(h(i+vec2(0,1)),h(i+vec2(1,1)),f.x),f.y);}
        void main(){vec2 uv=vUv;float t=time;float nz=n(vec2(uv.x*4.0,uv.y*3.0-t*2.6))*.6+n(vec2(uv.x*9.0,uv.y*7.0-t*4.1))*.4;
          float w=(1.0-uv.y)*.5+.08;float shape=smoothstep(w,w*.35,abs(uv.x-.5+(nz-.5)*.25*uv.y));shape*=smoothstep(1.0,.25,uv.y+nz*.45);shape*=smoothstep(0.0,.08,uv.y);
          vec3 col=mix(vec3(1.0,.92,.55),vec3(1.0,.42,.08),smoothstep(.05,.6,uv.y));col=mix(col,vec3(.8,.15,.02),smoothstep(.55,1.0,uv.y));
          gl_FragColor=vec4(col*shape*power*1.6,shape*power);}`});
    const flames = [];
    for (let i = 0; i < 6; i++) {const f = new T.Mesh(new T.PlaneGeometry(1, 1.6), flameMat.clone());f.material.uniforms.time.value = i * 3.1;f.userData.k = .7 + rnd() * .5;f.userData.off = [(rnd() - .5) * .35, (rnd() - .5) * .35];f.renderOrder = 3;group.add(f);flames.push(f);}
    const embersGeo = new T.BufferGeometry(), EN = 70, ep = new Float32Array(EN * 3), ev = [];
    for (let i = 0; i < EN; i++) {ev.push({life: rnd() * 2, v: [0, 0, 0]});}
    embersGeo.setAttribute('position', new T.BufferAttribute(ep, 3));
    const embers = new T.Points(embersGeo, new T.PointsMaterial({color: 0xffa040, size: .045, transparent: true, opacity: .9, blending: T.AdditiveBlending, depthWrite: false}));group.add(embers);
    const smokeTex = (() => {const c = document.createElement('canvas');c.width = c.height = 64;const g = c.getContext('2d'), r = g.createRadialGradient(32, 32, 2, 32, 32, 32);r.addColorStop(0, 'rgba(255,255,255,.55)');r.addColorStop(1, 'rgba(255,255,255,0)');g.fillStyle = r;g.fillRect(0, 0, 64, 64);return new T.CanvasTexture(c);})();
    const SN = 26, sp = new Float32Array(SN * 3), sv = [];for (let i = 0; i < SN; i++) sv.push({life: rnd() * 6});
    const smokeGeo = new T.BufferGeometry();smokeGeo.setAttribute('position', new T.BufferAttribute(sp, 3));
    const smoke = new T.Points(smokeGeo, new T.PointsMaterial({map: smokeTex, color: 0x9a9a9a, size: 1.6, transparent: true, opacity: .32, depthWrite: false}));group.add(smoke);
    const light = new T.PointLight(0xff8a3a, 0, 16, 1.6);light.position.set(0, .9, 0);group.add(light);
    // Benches (five, facing the fire) with two seats each.
    const plank = lib.wood(0x7a5233, .7), legM = lib.wood(0x4e3826, .8), spots = [], obstacles = [];
    const local2world = (lx, lz) => ({x: x + lx, z: z + lz});
    const rect = (cx, cz, along, len, wid, minY, maxY) => {const c = Math.cos(along), s = Math.sin(along), pts = [[len / 2, wid / 2], [len / 2, -wid / 2], [-len / 2, -wid / 2], [-len / 2, wid / 2]].map(([a, b]) => local2world(cx + c * a - s * b, cz + s * a + c * b));return {points: pts, minY: y0 + minY, maxY: y0 + maxY, kind: 'firepit'};};
    const benchAngles = [0, 1, 2, 3, 4].map(i => i / 5 * Math.PI * 2 + .3);
    for (const a of benchAngles) {
      const R = 2.75, bx = Math.cos(a) * R, bz = Math.sin(a) * R, b = new T.Group();b.position.set(bx, 0, bz);b.rotation.y = -a + Math.PI / 2;group.add(b);
      for (let k = 0; k < 3; k++) mesh(b, new T.BoxGeometry(1.7, .045, .12), plank, 0, .45, -.13 + k * .13);
      for (const s of [-1, 1]) {mesh(b, new T.BoxGeometry(.08, .43, .38), legM, s * .7, .22, 0);}
      const ta = Math.atan2(-bz, -bx);// towards the fire
      for (const off of [-.42, .42]) {const along = -a + Math.PI / 2, px = bx + Math.cos(along) * off * 1, pz = bz - Math.sin(along) * off;spots.push({x: x + px, z: z + pz, y: y0 + .47, yaw: Math.atan2(bx, bz), id: spots.length});}
      obstacles.push(rect(bx, bz, a - Math.PI / 2, 1.75, .42, 0, .5));void ta;
    }
    obstacles.push(rect(0, 0, 0, 1.9, 1.9, 0, .6));
    // Woodpile and beer crate between two benches, outside the ring.
    const wa = (benchAngles[0] + benchAngles[1]) / 2, wx = Math.cos(wa) * 4.1, wz = Math.sin(wa) * 4.1, pile = new T.Group();pile.position.set(wx, 0, wz);pile.rotation.y = -wa;group.add(pile);
    const bark = lib.bark(), cut = lib.wood(0xc9a477, .8);
    for (let row = 0; row < 3; row++) for (let i = 0; i < 5 - row; i++) {const l = mesh(pile, new T.CylinderGeometry(.075, .075, .9, 10), bark, 0, .08 + row * .14, (i - (4 - row) / 2) * .16);l.rotation.z = Math.PI / 2;for (const s of [-1, 1]) {const e = mesh(pile, new T.CircleGeometry(.074, 10), cut, s * .451, .08 + row * .14, (i - (4 - row) / 2) * .16);e.rotation.y = s * Math.PI / 2;}}
    obstacles.push(rect(wx, wz, wa, 1.0, .9, 0, .5));
    const ca = (benchAngles[2] + benchAngles[3]) / 2, cx = Math.cos(ca) * 3.9, cz = Math.sin(ca) * 3.9, crate = new T.Group();crate.position.set(cx, 0, cz);crate.rotation.y = -ca;group.add(crate);
    const crateM = new T.MeshStandardMaterial({color: 0x1d5c2e, roughness: .6}), bottleM = new T.MeshStandardMaterial({color: 0x5a3a12, roughness: .15, transparent: true, opacity: .9}), capM = lib.metal(0xc8b060, .3);
    mesh(crate, new T.BoxGeometry(.42, .3, .3), crateM, 0, .15, 0);
    for (let i = 0; i < 4; i++) for (let j = 0; j < 3; j++) {mesh(crate, new T.CylinderGeometry(.028, .03, .2, 10), bottleM, -.15 + i * .1, .36, -.09 + j * .09);mesh(crate, new T.CylinderGeometry(.014, .014, .02, 8), capM, -.15 + i * .1, .47, -.09 + j * .09);}
    obstacles.push(rect(cx, cz, ca, .5, .4, 0, .5));
    // A log flying into the fire.
    const flying = [];
    function throwLog(from) {
      const l = new T.Mesh(new T.CylinderGeometry(.07, .07, .8, 10), bark);l.castShadow = true;group.add(l);
      const start = new T.Vector3(from.x - x, (from.y ?? y0 + 1.2) - y0, from.z - z);flying.push({m: l, t: 0, start, spin: new T.Vector3(rnd() * 6, rnd() * 6, rnd() * 6)});
    }
    let time = 0, flick = 1;
    function update(dt, level, camera) {
      time += dt;const L = Math.max(0, Math.min(MAX, level)), p = L <= 0 ? 0 : .25 + L / MAX * .95;
      flick += ((.8 + Math.random() * .4) - flick) * Math.min(1, dt * 12);
      light.intensity = L <= 0 ? .4 : (3 + L * 4.2) * flick;light.distance = 9 + L * 3;
      charred.emissiveIntensity = .6 + Math.min(1, L / 2) * 1.6;
      const camYaw = Math.atan2(camera.position.x - x, camera.position.z - z);
      flames.forEach((f, i) => {const k = f.userData.k * p;f.visible = k > .02;f.scale.set(.65 * k + .2 * p, 1.2 * k + .3 * p, 1);f.position.set(f.userData.off[0] * (1 + L * .08), .1 + f.scale.y * .78, f.userData.off[1] * (1 + L * .08));f.rotation.set(0, camYaw, 0);f.material.uniforms.time.value += dt * (1 + i * .07);f.material.uniforms.power.value = .5 + p * .7;});
      for (let i = 0; i < EN; i++) {const e = ev[i];e.life -= dt;if (e.life <= 0) {e.life = .8 + Math.random() * 1.8;ep[i * 3] = (Math.random() - .5) * .5;ep[i * 3 + 1] = .3;ep[i * 3 + 2] = (Math.random() - .5) * .5;e.v = [(Math.random() - .5) * .4, 1 + Math.random() * 1.5 * (.4 + p), (Math.random() - .5) * .4];}ep[i * 3] += e.v[0] * dt;ep[i * 3 + 1] += e.v[1] * dt;ep[i * 3 + 2] += e.v[2] * dt;}
      embers.visible = L > .1;embersGeo.attributes.position.needsUpdate = true;embers.material.size = .03 + p * .03;
      for (let i = 0; i < SN; i++) {const s = sv[i];s.life -= dt;if (s.life <= 0) {s.life = 4 + Math.random() * 3;sp[i * 3] = (Math.random() - .5) * .4;sp[i * 3 + 1] = .6 + p;sp[i * 3 + 2] = (Math.random() - .5) * .4;}sp[i * 3] += (.25 + Math.sin(time * .3 + i) * .15) * dt;sp[i * 3 + 1] += (.55 + p * .5) * dt;sp[i * 3 + 2] += .12 * dt;}
      smokeGeo.attributes.position.needsUpdate = true;smoke.material.opacity = .12 + p * .22;
      for (let i = flying.length - 1; i >= 0; i--) {const f = flying[i];f.t += dt / .7;const t = Math.min(1, f.t);f.m.position.set(f.start.x * (1 - t), f.start.y * (1 - t) + .35 * t + Math.sin(t * Math.PI) * 1.1, f.start.z * (1 - t));f.m.rotation.set(f.spin.x * t, f.spin.y * t, f.spin.z * t);if (t >= 1) {group.remove(f.m);f.m.geometry.dispose();flying.splice(i, 1);}}
    }
    return {group, center: {x, y: y0, z}, spots, woodpile: {x: x + wx, z: z + wz}, crate: {x: x + cx, z: z + cz}, obstacles, update, throwLog, DECAY_S, MAX};
  }
  return {create, DECAY_S, MAX};
});
