/* Drivable vehicles for the open world: a 2005 Honda CBF600S (blue-grey half fairing, Scorpion silencer,
 * no top box), parked hatchbacks, and a tracked excavator with a working boom, stick and bucket.
 * World (model) coordinates; vehicles follow the terrain via heightAt(x, z) and collide via a collision world.
 * GameVehicles.create({T, lib, audio, heightAt, collision}) -> {Motorcycle, Car, Excavator}
 * Each vehicle: group, x, z, yaw, speed, update(dt, input, active), seat() -> {position, yaw, pitch, roll},
 *   footprint() (obstacle for walkers), enter(), exit() -> exit point.
 * input: {throttle, brake, steer, look, l1, l2, r1, r2, sticks:{lx, ly, rx, ry}, drunk}
 */
(function (root, factory) {
  root.GameVehicles = factory();
})(typeof window === 'undefined' ? globalThis : window, function () {
  'use strict';
  function create({T, lib, audio, heightAt, collision, worldFor}) {
    const V3 = (x, y, z) => new T.Vector3(x, y, z);
    const mesh = (parent, geo, mat, x = 0, y = 0, z = 0) => {const m = new T.Mesh(geo, mat);m.position.set(x, y, z);m.castShadow = true;m.receiveShadow = true;parent.add(m);return m;};
    const box = (parent, w, h, d, x, y, z, mat) => mesh(parent, new T.BoxGeometry(w, h, d), mat, x, y, z);
    const cyl = (parent, r1, r2, h, x, y, z, mat, seg = 16) => mesh(parent, new T.CylinderGeometry(r1, r2, h, seg), mat, x, y, z);
    function rounded(w, h, d, r) {
      const g = new T.BoxGeometry(w, h, d, 5, 5, 5), p = g.attributes.position, v = new T.Vector3(), inner = new T.Vector3(w / 2 - r, h / 2 - r, d / 2 - r);
      for (let i = 0; i < p.count; i++) {v.fromBufferAttribute(p, i);const c = v.clone().clamp(inner.clone().negate(), inner), off = v.clone().sub(c);if (off.lengthSq() > 0) v.copy(c).add(off.normalize().multiplyScalar(r));p.setXYZ(i, v.x, v.y, v.z);}
      g.computeVertexNormals();return g;
    }
    // Side profile (z forward = -z in three, y up) extruded across the width.
    // round > 0 pinches the sides in toward the top and bottom of the profile, giving rounded cross-sections
    // (tank, fairing, car roof tumblehome) instead of slab sides.
    function pinch(g, bb, round) {
      const cy = (bb.max.y + bb.min.y) / 2, hy = (bb.max.y - bb.min.y) / 2 || 1, cz = (bb.max.z + bb.min.z) / 2, hz = (bb.max.z - bb.min.z) / 2 || 1, P = g.attributes.position;
      for (let i = 0; i < P.count; i++) {const ey = Math.pow(Math.min(1, Math.abs((P.getY(i) - cy) / hy)), 2.5), ez = Math.pow(Math.min(1, Math.abs((P.getZ(i) - cz) / hz)), 4);P.setX(i, P.getX(i) * (1 - round * Math.min(1, ey * .8 + ez * .5)));}
    }
    function profile(points, width, bevel = .02, round = .3, ref = null) {
      // Points are (z, y) with forward = -z; the shape is drawn with -z so that after rotateY the front faces -z.
      const s = new T.Shape();points.forEach(([z, y], i) => i ? s.lineTo(-z, y) : s.moveTo(-z, y));s.closePath();
      const g = new T.ExtrudeGeometry(s, {depth: width - 2 * bevel, bevelEnabled: true, bevelThickness: bevel, bevelSize: bevel, bevelSegments: 3, curveSegments: 8});
      g.translate(0, 0, -(width - 2 * bevel) / 2);g.rotateY(Math.PI / 2);
      if (round) {g.computeBoundingBox();pinch(g, ref || g.boundingBox, round);}
      g.computeVertexNormals();return g;
    }
    // A door: a thin wall following the body's cross-section (x = +-outer before the pinch), from z0 to z1.
    function sidePanel(side, z0, z1, y0, y1, outer, thick, ref, round = .3) {
      const s = new T.Shape(), n = 12;
      for (let i = 0; i <= n; i++) {const y = y0 + (y1 - y0) * i / n;i ? s.lineTo(side * outer, y) : s.moveTo(side * outer, y);}
      for (let i = n; i >= 0; i--) s.lineTo(side * (outer - thick), y0 + (y1 - y0) * i / n);
      const g = new T.ExtrudeGeometry(s, {depth: z1 - z0, bevelEnabled: false, steps: 6});g.translate(0, 0, z0);
      pinch(g, ref, round);g.computeVertexNormals();return g;
    }
    function rod(parent, from, to, r, mat) {
      const a = V3(...from), b = V3(...to), dir = b.clone().sub(a), m = mesh(parent, new T.CapsuleGeometry(r, dir.length(), 4, 10), mat);
      m.position.copy(a).add(b).multiplyScalar(.5);m.quaternion.setFromUnitVectors(V3(0, 1, 0), dir.normalize());return m;
    }
    const glove = lib.glove(), sleeve = lib.sleeve();
    // A gloved hand gripping a bar along `axis` (local), fingers wrapped around it.
    function hand(parent, x, y, z, axisRot, left) {
      const g = new T.Group();g.position.set(x, y, z);g.rotation.copy(axisRot);parent.add(g);
      const palm = mesh(g, rounded(.085, .03, .09, .012), glove, 0, .03, .01);palm.rotation.x = -.3;
      for (let i = 0; i < 4; i++) {const f = mesh(g, new T.TorusGeometry(.024, .0085, 8, 12, Math.PI * 1.25), glove, -.03 + i * .02, 0, 0);f.rotation.y = Math.PI / 2;f.rotation.z = Math.PI * .6;}
      const thumb = mesh(g, new T.CapsuleGeometry(.009, .035, 4, 8), glove, left ? .045 : -.045, .018, -.02);thumb.rotation.z = Math.PI / 2;
      return g;
    }
    function forearm(parent, from, to) {
      const a = V3(...from), b = V3(...to), dir = b.clone().sub(a), m = mesh(parent, new T.CapsuleGeometry(.04, dir.length(), 6, 12), sleeve);
      m.position.copy(a).add(b).multiplyScalar(.5);m.quaternion.setFromUnitVectors(V3(0, 1, 0), dir.normalize());return m;
    }
    function wheel(parent, radius, width, rimMat, tyreMat, spokes = 5) {
      const g = new T.Group();parent.add(g);
      const tyre = mesh(g, new T.TorusGeometry(radius - width * .45, width * .45, 12, 28), tyreMat);tyre.rotation.y = Math.PI / 2;
      const rim = mesh(g, new T.CylinderGeometry(radius - width * .75, radius - width * .75, width * .5, 24, 1, true), rimMat);rim.rotation.z = Math.PI / 2;
      const hub = cyl(g, radius * .18, radius * .18, width * .7, 0, 0, 0, rimMat, 12);hub.rotation.z = Math.PI / 2;
      for (let i = 0; i < spokes; i++) {const s = box(g, width * .25, radius * 1.3, .03, 0, 0, 0, rimMat);s.rotation.x = i / spokes * Math.PI * 2;s.translateY(radius * .38);}
      // One mesh per material per wheel (it still spins as a group).
      lib.mergeStatic(g);return g;
    }
    function decal(text, w, h, color = '#ffffff', bg = 'rgba(0,0,0,0)', font = 'bold 72px Georgia, serif') {
      const c = document.createElement('canvas');c.width = 512;c.height = Math.round(512 * h / w);const x = c.getContext('2d');x.fillStyle = bg;x.fillRect(0, 0, c.width, c.height);x.fillStyle = color;x.font = font;x.textAlign = 'center';x.textBaseline = 'middle';x.fillText(text, c.width / 2, c.height / 2);
      const t = new T.CanvasTexture(c);t.colorSpace = T.SRGBColorSpace;return new T.MeshStandardMaterial({map: t, transparent: true, roughness: .4});
    }

    // ---------- Base: terrain following and collision ----------
    class Vehicle {
      constructor(x, z, yaw, radius, length) {this.group = new T.Group();this.x = x;this.z = z;this.yaw = yaw;this.speed = 0;this.radius = radius;this.length = length;this.pitch = 0;this.roll = 0;this.y = heightAt(x, z);this.active = false;}
      forward() {return {x: -Math.sin(this.yaw), z: -Math.cos(this.yaw)};}
      // Two circles (front and rear) must be clear of buildings, trees and hedges.
      clear(x, z) {
        const w = worldFor(this.radius), f = {x: -Math.sin(this.yaw), z: -Math.cos(this.yaw)}, h = this.length / 2 - this.radius, y = heightAt(x, z);
        for (const k of h > 0 ? [-h, h] : [0]) {const p = {x: x + f.x * k, z: z + f.z * k};if (!collision.canStand(w, p, 1.2, heightAt(p.x, p.z))) return false;}
        return Math.abs(y - this.y) < 1.5;
      }
      settle(dt) {
        const f = this.forward(), r = {x: -f.z, z: f.x}, L = this.length * .4, W = .6;
        const hf = heightAt(this.x + f.x * L, this.z + f.z * L), hb = heightAt(this.x - f.x * L, this.z - f.z * L), hl = heightAt(this.x - r.x * W, this.z - r.z * W), hr = heightAt(this.x + r.x * W, this.z + r.z * W);
        this.y = (hf + hb) / 2;const k = Math.min(1, dt * 8);this.pitch += (Math.atan2(hf - hb, 2 * L) - this.pitch) * k;this.terrainRoll = Math.atan2(hl - hr, 2 * W);
      }
      move(dt) {
        const f = this.forward(), nx = this.x + f.x * this.speed * dt, nz = this.z + f.z * this.speed * dt;
        if (Math.abs(this.speed) < .001 || this.clear(nx, nz)) {this.x = nx;this.z = nz;}
        else {if (Math.abs(this.speed) > 2.5) audio.sfx('hit', {gain: Math.min(1, Math.abs(this.speed) / 10)});this.speed = 0;}
      }
      // ---- Multiplayer: the driver's client sends netGet(); everybody else poses the vehicle with netSet() + netAnimate(). ----
      netGet() {return [this.x, this.y, this.z, this.yaw, this.pitch, this.roll || 0, this.speed, ...this.netExtra()].map(v => Math.round(v * 1000) / 1000);}
      netExtra() {return [];}
      netSet(a) {this.x = a[0];this.y = a[1];this.z = a[2];this.yaw = a[3];this.pitch = a[4];this.roll = a[5];this.speed = a[6];this.netApply(a.slice(7));this.place();}
      netApply() {}
      netAnimate() {}
      // Seats: where a seated player's hips go (in `obj`'s frame) and the passenger's eye point (vehicle frame).
      hipAnchor(seat) {const h = this.hips[seat];return h ? {obj: h.obj || this.group, pos: h.pos, stance: h.stance} : null;}
      footprint() {
        const f = this.forward(), r = {x: -f.z, z: f.x}, hl = this.length / 2, hw = this.width / 2;
        return {points: [[hl, hw], [hl, -hw], [-hl, -hw], [-hl, hw]].map(([a, b]) => ({x: this.x + f.x * a + r.x * b, z: this.z + f.z * a + r.z * b})), minY: this.y - .2, maxY: this.y + this.height, kind: 'vehicle'};
      }
      exitPoint(side = -1) {const f = this.forward(), r = {x: -f.z, z: f.x}, d = this.width / 2 + .55;return {x: this.x + r.x * d * side, z: this.z + r.z * d * side};}
      place() {this.group.position.set(this.x, this.y, this.z);this.group.rotation.set(0, 0, 0);this.group.rotateY(this.yaw);this.group.rotateX(this.pitch);this.group.rotateZ(this.roll || 0);}
    }

    // ---------- 2005 Honda CBF600S ----------
    class Motorcycle extends Vehicle {
      constructor(x, z, yaw, color = 0x56657c) {
        super(x, z, yaw, .45, 2.1);this.width = .8;this.height = 1.3;this.kind = 'motorcycle';this.name = 'Honda CBF600S';this.lean = 0;this.bar = 0;this.gear = 1;this.rpm = 1300;this.engine = null;this.wheelSpin = 0;
        this.hips = {d: {pos: V3(0, 1.0, .28), stance: 'ride'}, p: {pos: V3(0, 1.06, .66), stance: 'pillion'}};this.passengerEye = V3(0, 1.74, .76);
        const paint = new T.MeshPhysicalMaterial({color, metalness: .55, roughness: .32, clearcoat: 1, clearcoatRoughness: .08});
        const black = lib.paint(0x17181a, .55), matte = lib.polymer(0x1c1d1f), chrome = lib.chrome(), alu = lib.metal(0xb7bcc0, .3), engineMat = lib.metal(0x6c6f72, .45), rubber = new T.MeshStandardMaterial({color: 0x161616, roughness: .9});
        const g = this.body = new T.Group();this.group.add(g);
        // Wheels: 17" cast wheels, 120/70 front and 160/60 rear (wheelbase 1.48 m).
        this.front = new T.Group();g.add(this.front);
        this.fw = wheel(this.front, .3, .12, black, rubber, 6);this.front.position.set(0, .3, -.68);
        this.rw = wheel(g, .31, .17, black, rubber, 6);this.rw.position.set(0, .31, .74);
        for (const s of [-1, 1]) {const disc = cyl(this.fw, .15, .15, .006, s * .07, 0, 0, alu, 24);disc.rotation.z = Math.PI / 2;}
        const rdisc = cyl(this.rw, .12, .12, .006, -.09, 0, 0, alu, 20);rdisc.rotation.z = Math.PI / 2;
        // Steering head: forks raked about 26°, triple clamps, handlebar with grips and switchgear.
        this.steer = new T.Group();this.steer.position.set(0, .98, -.44);this.steer.rotation.x = .3;g.add(this.steer);
        for (const s of [-1, 1]) {cyl(this.steer, .022, .022, .5, s * .1, -.28, 0, chrome);cyl(this.steer, .028, .028, .32, s * .1, -.62, 0, alu);}
        box(this.steer, .26, .04, .08, 0, -.02, 0, alu);box(this.steer, .26, .035, .08, 0, -.12, 0, alu);
        this.bars = new T.Group();this.bars.position.set(0, .16, .12);this.steer.add(this.bars);
        const barTube = cyl(this.bars, .011, .011, .72, 0, 0, 0, black);barTube.rotation.z = Math.PI / 2;
        for (const s of [-1, 1]) {const grip = cyl(this.bars, .017, .017, .13, s * .3, 0, 0, rubber, 12);grip.rotation.z = Math.PI / 2;box(this.bars, .05, .04, .05, s * .2, .01, .01, matte);const lever = box(this.bars, .17, .012, .02, s * .29, .005, -.06, alu);lever.rotation.y = s * .25;}
        // Front fender (fixed to the fork, so it turns with steering).
        const fender = mesh(this.front, new T.CylinderGeometry(.34, .34, .13, 18, 1, true, Math.PI * .1, Math.PI * .75), paint);fender.rotation.z = Math.PI / 2;
        // Half fairing with headlight, indicators, windscreen and mirrors.
        const fairing = mesh(g, profile([[-.86, .78], [-.84, 1.02], [-.62, 1.24], [-.45, 1.2], [-.45, .86], [-.62, .7]], .62, .04), paint);
        const head = mesh(g, new T.SphereGeometry(.13, 18, 12, 0, Math.PI * 2, 0, Math.PI / 2), new T.MeshStandardMaterial({color: 0xdfe9ef, roughness: .05, metalness: .6, emissive: 0x30363a}), 0, .92, -.86);head.rotation.x = -Math.PI / 2;head.scale.set(1.4, 1, .6);
        mesh(g, profile([[-.66, 1.22], [-.74, 1.45], [-.6, 1.5], [-.5, 1.24]], .4, .005), new T.MeshPhysicalMaterial({color: 0x3a4652, transparent: true, opacity: .22, roughness: .03, metalness: 0, depthWrite: false}));
        for (const s of [-1, 1]) {const arm = cyl(g, .008, .008, .12, s * .3, 1.24, -.62, black, 8);arm.rotation.z = s * .8;const mirror = mesh(g, rounded(.13, .08, .035, .015), black, s * .36, 1.3, -.62);mirror.rotation.y = s * .2;
          mesh(g, new T.BoxGeometry(.04, .03, .07), new T.MeshStandardMaterial({color: 0xff9c2a, emissive: 0x552800, roughness: .3}), s * .3, .92, -.77);}
        // Tank, seat, side panels, tail.
        mesh(g, profile([[-.48, .9], [-.42, 1.13], [-.1, 1.18], [.08, 1.03], [.05, .86], [-.4, .82]], .42, .07), paint);
        const logo = mesh(g, new T.PlaneGeometry(.22, .07), decal('HONDA', .22, .07, '#ffffff', 'rgba(0,0,0,0)', 'bold 120px Georgia, serif'), .216, 1.02, -.62);logo.rotation.y = Math.PI / 2;
        const logo2 = mesh(g, new T.PlaneGeometry(.22, .07), decal('HONDA', .22, .07, '#ffffff', 'rgba(0,0,0,0)', 'bold 120px Georgia, serif'), -.216, 1.02, -.62);logo2.rotation.y = -Math.PI / 2;
        mesh(g, profile([[.05, .86], [.08, .95], [.38, .93], [.5, .99], [.78, 1.01], [.82, .95], [.6, .86]], .3, .05), black);
        mesh(g, profile([[.4, .74], [.42, .9], [.86, .98], [.92, .9], [.7, .74]], .26, .04), paint);
        mesh(g, profile([[.82, .92], [.98, .86], [1.03, .62], [.95, .6], [.82, .78]], .16, .02), black);
        box(g, .2, .04, .06, 0, .9, .94, new T.MeshStandardMaterial({color: 0xb00000, emissive: 0x400000, roughness: .3}));
        // Inline-four engine, frame, swingarm, shock, chain, footpegs.
        box(g, .44, .34, .42, 0, .5, -.12, engineMat);for (let i = 0; i < 5; i++) box(g, .46, .012, .3, 0, .69 + i * .028, -.2, engineMat);
        mesh(g, rounded(.48, .22, .32, .06), engineMat, 0, .33, 0);const cover = cyl(g, .1, .1, .03, .25, .38, .02, alu, 20);cover.rotation.z = Math.PI / 2;
        for (const s of [-1, 1]) {const rail = box(g, .035, .06, .9, s * .17, .78, -.1, black);rail.rotation.x = .25;box(g, .04, .06, .6, s * .13, .32, .5, alu).rotation.x = -.12;}
        cyl(g, .03, .03, .34, 0, .58, .32, new T.MeshStandardMaterial({color: 0xd23a2a, roughness: .4}), 10).rotation.x = .5;
        for (const s of [-1, 1]) {const peg = cyl(g, .012, .012, .12, s * .22, .36, .12, alu, 8);peg.rotation.z = Math.PI / 2;}
        // Exhaust: four headers into one, Scorpion stainless silencer on the right.
        for (let i = 0; i < 4; i++) {const h = cyl(g, .02, .02, .5, -.12 + i * .08, .28, -.2, chrome, 8);h.rotation.x = 1.2;}
        const link = cyl(g, .035, .035, .55, .13, .22, .32, chrome, 12);link.rotation.x = Math.PI / 2 - .12;
        const can = cyl(g, .065, .065, .5, .2, .44, .78, chrome, 24);can.rotation.x = Math.PI / 2 - .2;
        const label = mesh(g, new T.PlaneGeometry(.22, .06), decal('SCORPION', .22, .06, '#ffffff', '#c0101a', 'italic bold 80px Arial, sans-serif'), .267, .45, .74);label.rotation.set(-.2, Math.PI / 2, 0);
        // Rider's gloved hands on the grips, forearms toward the camera.
        this.hands = new T.Group();this.bars.add(this.hands);this.hands.visible = false;
        hand(this.hands, -.3, .015, 0, new T.Euler(0, 0, 0), true);hand(this.hands, .3, .015, 0, new T.Euler(0, 0, 0), false);
        for (const s of [-1, 1]) forearm(this.hands, [s * .29, .04, .03], [s * .2, .22, .55]);
        this.arms = new T.Group();g.add(this.arms);this.arms.visible = false;
        g.traverse(o => {if (o.isMesh) o.castShadow = true;});
        for (const m of [this.front, this.rw, this.steer]) m.userData.noMerge = true;lib.mergeStatic(g);
        this.settle(1);this.place();
      }
      seat() {const f = this.forward(), up = 1.58;return {x: this.x - f.x * .36, y: this.y + up * Math.cos(this.lean), z: this.z - f.z * .36, lateral: Math.sin(this.lean) * up, yaw: this.yaw, pitch: this.pitch, roll: this.lean};}
      enter() {this.active = true;this.hands.visible = true;this.arms.visible = true;audio.sfx('starter');this.engine = audio.engine('bike');this.rpm = 1300;}
      exit() {this.active = false;this.hands.visible = false;this.arms.visible = false;if (this.engine) this.engine.stop();this.engine = null;this.speed = 0;this.lean = 0;return this.exitPoint(-1);}
      update(dt, input) {
        if (this.active) {
          const v = this.speed, wobble = input.drunk ? Math.sin(performance.now() / 380) * .35 * input.drunk : 0;
          const accel = input.throttle * Math.max(1.2, 7.5 - v * .12) - input.brake * 9 * Math.sign(v || 1) - v * .02 - (v > 0 ? .25 : 0);
          // Holding L2 when (almost) stopped walks the bike backwards at about 1.5 m/s instead of braking against it.
          const reversing = input.brake > .5 && v < .3 && input.throttle < .1;
          this.speed = reversing ? Math.max(-1.5, Math.min(0, v) - 2.5 * dt) : Math.max(v > .3 ? 0 : -1.5, Math.min(55, v + accel * dt));
          const steer = Math.max(-1, Math.min(1, input.steer + wobble));
          if (Math.abs(this.speed) < 4) {
            // Low speed: steer the bars directly.
            this.bar += (steer * .55 - this.bar) * Math.min(1, dt * 6);this.yaw -= this.speed * Math.tan(this.bar) / 1.48 * dt;this.lean += (this.bar * this.speed * .08 - this.lean) * Math.min(1, dt * 4);
          } else {
            // Speed: the stick sets the lean; the turn rate follows from lean and speed (counter-steer happens in the bars).
            const targetLean = steer * .75;this.lean += (targetLean - this.lean) * Math.min(1, dt * 3.2);
            const yawRate = 9.81 * Math.tan(this.lean) / this.speed;this.yaw -= yawRate * dt;this.bar += (Math.atan(1.48 * yawRate / this.speed) * .8 - (targetLean - this.lean) * .25 - this.bar) * Math.min(1, dt * 8);
          }
          this.roll = -this.lean;
          // Gearbox model for the engine note: six gears, change up at 10 000 rpm, down at 5 000.
          const kmh = Math.abs(this.speed) * 3.6, tops = [55, 85, 115, 145, 175, 215];
          if (this.rpm > 10000 && this.gear < 6) this.gear++;else if (this.rpm < 5000 && this.gear > 1) this.gear--;
          const target = Math.max(1300, 1300 + (kmh / tops[this.gear - 1]) * 10200) + input.throttle * (kmh < 4 ? 3500 : 400);
          this.rpm += (Math.min(11800, target) - this.rpm) * Math.min(1, dt * 6);if (this.engine) this.engine.set(this.rpm, input.throttle);
        } else {this.speed *= Math.max(0, 1 - dt * 3);this.lean += (-.12 - this.lean) * Math.min(1, dt * 3);this.roll = -this.lean;}
        this.move(dt);this.settle(dt);
        this.wheelSpin += this.speed * dt / .3;this.fw.rotation.x = -this.wheelSpin;this.rw.rotation.x = -this.wheelSpin;
        this.bars.rotation.y = -this.bar;this.front.rotation.y = -this.bar * .9;
        this.place();
      }
      netExtra() {return [this.bar, this.lean];}
      netApply(a) {this.bar = a[0] || 0;this.lean = a[1] || 0;}
      netAnimate(dt) {this.wheelSpin += this.speed * dt / .3;this.fw.rotation.x = -this.wheelSpin;this.rw.rotation.x = -this.wheelSpin;this.bars.rotation.y = -this.bar;this.front.rotation.y = -this.bar * .9;}
    }

    // ---------- Hatchback car (left-hand drive) ----------
    const carColors = [0x111214, 0x9e1b20, 0xb7bcc2, 0xf0f0ec, 0x1f3a63, 0x2f5d3a, 0x6b6f75], carNames = ['Black', 'Red', 'Silver', 'White', 'Blue', 'Green', 'Grey'];
    class Car extends Vehicle {
      constructor(x, z, yaw, model = 0) {
        super(x, z, yaw, .95, 4.2);this.width = 1.8;this.height = 1.45;this.kind = 'car';this.name = carNames[model % carColors.length] + ' hatchback';this.steerAngle = 0;this.rpm = 800;this.engine = null;this.wheelSpin = 0;
        this.hips = {d: {pos: V3(-.37, .62, .28), stance: 'drive'}, p: {pos: V3(.37, .62, .28), stance: 'sit'}};this.passengerEye = V3(.37, 1.24, .22);
        const paint = new T.MeshPhysicalMaterial({color: carColors[model % carColors.length], metalness: .6, roughness: .3, clearcoat: 1, clearcoatRoughness: .06});
        const glassM = new T.MeshPhysicalMaterial({color: 0x1d2328, transparent: true, opacity: .45, roughness: .03, metalness: .2}), trimM = lib.polymer(0x141516), rubber = new T.MeshStandardMaterial({color: 0x151515, roughness: .9}), rimM = lib.metal(0xa8adb2, .3);
        const interior = lib.fabric(0x2a2b2e), dashM = lib.polymer(0x1e1f21), chrome = lib.chrome();
        const g = this.body = new T.Group();this.group.add(g);
        // Body: lower shell, glasshouse with pillars, bumpers, lights.
        const shellRef = (() => {const f = profile([[-2.1, .3], [-2.12, .62], [-1.9, .8], [-.7, .92], [1.75, .95], [2.05, .9], [2.1, .5], [2.0, .3]], 1.78, .08, 0);f.computeBoundingBox();const bb = f.boundingBox.clone();f.dispose();return bb;})();
        mesh(g, profile([[-2.1, .3], [-2.12, .62], [-1.9, .8], [-.7, .92], [-.5, .922], [-.5, .3]], 1.78, .08, .3, shellRef), paint);
        mesh(g, profile([[1.3, .3], [1.3, .944], [1.75, .95], [2.05, .9], [2.1, .5], [2.0, .3]], 1.78, .08, .3, shellRef), paint);
        for (const s of [-1, 1]) mesh(g, sidePanel(s, -.42, 1.22, .24, 1.0, .89, .075, shellRef), paint);
        mesh(g, profile([[-.75, .9], [-.05, 1.42], [1.45, 1.44], [1.98, .95]], 1.6, .06), paint).scale.set(1, 1, 1);
        // Windows (glass slightly inset), dark pillars.
        mesh(g, profile([[-.68, .93], [-.06, 1.37], [.0, 1.37], [-.55, .93]], 1.5, .01), glassM);
        for (const s of [-1, 1]) mesh(g, profile([[-.5, .97], [-.02, 1.36], [1.4, 1.38], [1.85, 1.0]], .02, .005), glassM).position.x = s * .81;
        mesh(g, profile([[1.88, .98], [1.48, 1.38], [1.5, 1.4], [1.95, 1.0]], 1.46, .01), glassM);
        box(g, 1.8, .2, .16, 0, .42, -2.05, trimM);box(g, 1.8, .2, .16, 0, .42, 2.03, trimM);
        for (const s of [-1, 1]) {
          mesh(g, new T.BoxGeometry(.32, .1, .05), new T.MeshStandardMaterial({color: 0xe8eef2, emissive: 0x404448, roughness: .05}), s * .62, .72, -2.1);
          mesh(g, new T.BoxGeometry(.3, .12, .05), new T.MeshStandardMaterial({color: 0xa00000, emissive: 0x300000, roughness: .2}), s * .7, .8, 2.06);
          const mirror = mesh(g, rounded(.18, .1, .08, .02), paint, s * .98, 1.0, -.55);
          const handle = box(g, .02, .03, .16, s * .9, .85, .1, chrome);
        }
        box(g, .5, .11, .02, 0, .48, -2.14, new T.MeshStandardMaterial({color: 0xf2f2ee, roughness: .4}));
        // Wheels (front pair steers).
        this.wheels = [];
        for (const [wx, wz, front] of [[-.78, -1.3, true], [.78, -1.3, true], [-.78, 1.3, false], [.78, 1.3, false]]) {
          const holder = new T.Group();holder.position.set(wx, .31, wz);g.add(holder);const wh = wheel(holder, .31, .2, rimM, rubber, 5);this.wheels.push({holder, wheel: wh, front});
        }
        // Interior (left-hand drive): floor, seats, dashboard, pillars, headliner, steering wheel with the driver's hands.
        const carpet = lib.fabric(0x1c1d1f), liner = lib.fabric(0x8d8a84);
        box(g, 1.36, .05, 1.64, 0, .3, .4, carpet);box(g, .2, .2, .75, 0, .42, -.05, dashM);box(g, 1.3, .42, .03, 0, .5, -.4, dashM);
        for (const sx of [-.38, .38]) {box(g, .5, .12, .5, sx, .52, .1, interior);const back = box(g, .5, .62, .12, sx, .85, .38, interior);back.rotation.x = -.18;box(g, .3, .2, .1, sx, 1.22, .45, interior);}
        box(g, 1.3, .3, .4, 0, .5, 1.1, interior);const rearBack = box(g, 1.3, .5, .1, 0, .88, 1.3, interior);rearBack.rotation.x = -.15;
        mesh(g, profile([[-.78, .72], [-.74, .92], [-.5, .96], [-.4, .76]], 1.4, .03, .05), dashM);
        box(g, 1.36, .02, 1.4, 0, 1.4, .7, liner);box(g, .22, .06, .02, 0, 1.32, -.12, dashM);
        for (const s of [-1, 1]) {rod(g, [s * .72, .97, -.66], [s * .66, 1.37, -.05], .035, dashM);rod(g, [s * .7, 1.02, .5], [s * .66, 1.38, .5], .04, liner);rod(g, [s * .7, 1.02, 1.72], [s * .64, 1.38, 1.42], .045, liner);}
        const cluster = box(g, .34, .12, .02, -.37, .93, -.5, new T.MeshStandardMaterial({color: 0x0b1015, emissive: 0x1a3550, roughness: .2}));cluster.rotation.x = -.35;
        // Column rises from the dash toward the driver; the wheel's local y axis points at the driver, local -z is "up" on the rim.
        this.wheelPivot = new T.Group();this.wheelPivot.position.set(-.37, .93, -.3);this.wheelPivot.rotation.x = 1.15;g.add(this.wheelPivot);
        cyl(this.wheelPivot, .025, .025, .3, 0, -.15, 0, dashM, 10);
        this.steeringWheel = new T.Group();this.wheelPivot.add(this.steeringWheel);
        const rimW = mesh(this.steeringWheel, new T.TorusGeometry(.19, .016, 10, 36), dashM);rimW.rotation.x = Math.PI / 2;
        for (const s of [-1, 1]) box(this.steeringWheel, .14, .012, .03, s * .105, 0, 0, dashM);box(this.steeringWheel, .03, .012, .14, 0, 0, .105, dashM);
        mesh(this.steeringWheel, new T.CylinderGeometry(.055, .055, .03, 16), dashM, 0, .01, 0);
        // Hands at ten and two: back of the hand outward and toward the driver, fingers around the rim, wrists toward the driver.
        this.hands = new T.Group();this.steeringWheel.add(this.hands);this.hands.visible = false;this.wrists = [];
        for (const [phi, left] of [[-1.05, true], [1.05, false]]) {
          const r = V3(Math.sin(phi), 0, -Math.cos(phi)), axis = V3(0, 1, 0), yh = r.clone().multiplyScalar(Math.cos(.7)).addScaledVector(axis, Math.sin(.7)).normalize();
          const zh = axis.clone().addScaledVector(yh, -axis.dot(yh)).normalize(), xh = yh.clone().cross(zh);
          const h = hand(this.hands, r.x * .19, 0, r.z * .19, new T.Euler().setFromRotationMatrix(new T.Matrix4().makeBasis(xh, yh, zh)), left);
          const wrist = new T.Object3D();wrist.position.set(0, .035, .07);h.add(wrist);this.wrists.push(wrist);
        }
        // Forearms in the car frame, solved each frame from the shoulders to the wrists (they follow the wheel).
        this.arms = new T.Group();this.arms.visible = false;g.add(this.arms);this.forearms = [-1, 1].map(() => mesh(this.arms, new T.CapsuleGeometry(.036, .26, 6, 12), sleeve));
        g.traverse(o => {if (o.isMesh && o.material === glassM) o.castShadow = false;});
        for (const w of this.wheels) w.holder.userData.noMerge = true;this.wheelPivot.userData.noMerge = true;this.arms.userData.noMerge = true;lib.mergeStatic(g);
        this.settle(1);this.place();
      }
      seat() {const f = this.forward(), r = {x: -f.z, z: f.x};return {x: this.x + r.x * -.37 - f.x * .22, y: this.y + 1.24, z: this.z + r.z * -.37 - f.z * .22, yaw: this.yaw, pitch: this.pitch, roll: this.terrainRoll || 0};}
      enter() {this.active = true;this.hands.visible = true;this.arms.visible = true;audio.sfx('carDoor');audio.sfx('starter');this.engine = audio.engine('car');}
      exit() {this.active = false;this.hands.visible = false;this.arms.visible = false;if (this.engine) this.engine.stop();this.engine = null;this.speed = 0;audio.sfx('carDoor');return this.exitPoint(-1);}
      update(dt, input) {
        if (this.active) {
          const v = this.speed, wobble = input.drunk ? Math.sin(performance.now() / 420) * .4 * input.drunk : 0, steer = Math.max(-1, Math.min(1, input.steer + wobble));
          const forwardAcc = input.throttle * Math.max(1, 4.2 - Math.abs(v) * .08), braking = input.brake * (v > .3 ? 8 : 0), reverse = input.brake * (v <= .3 ? 2.2 : 0);
          this.speed = Math.max(-5, Math.min(36, v + (forwardAcc - braking - reverse - v * .03 - Math.sign(v) * .2) * dt));
          if (Math.abs(this.speed) < .05 && input.throttle < .05 && input.brake < .05) this.speed = 0;
          this.steerAngle += (steer * .6 * Math.max(.35, 1 - Math.abs(v) / 30) - this.steerAngle) * Math.min(1, dt * 5);
          this.yaw -= this.speed * Math.tan(this.steerAngle) / 2.63 * dt;
          const target = 800 + Math.abs(this.speed) * 3.6 * 38 % 3600 + input.throttle * 1500;this.rpm += (Math.min(6200, target) - this.rpm) * Math.min(1, dt * 5);if (this.engine) this.engine.set(this.rpm, input.throttle);
        } else this.speed *= Math.max(0, 1 - dt * 4);
        this.move(dt);this.settle(dt);this.roll = this.terrainRoll || 0;
        this.wheelSpin += this.speed * dt / .31;
        for (const w of this.wheels) {w.wheel.rotation.x = -this.wheelSpin;if (w.front) w.holder.rotation.y = this.steerAngle;}
        this.steeringWheel.rotation.y = -this.steerAngle * 1.6;
        this.place();
        if (this.active) this.solveArms();
      }
      netExtra() {return [this.steerAngle];}
      netApply(a) {this.steerAngle = a[0] || 0;}
      netAnimate(dt) {this.wheelSpin += this.speed * dt / .31;for (const w of this.wheels) {w.wheel.rotation.x = -this.wheelSpin;if (w.front) w.holder.rotation.y = this.steerAngle;}this.steeringWheel.rotation.y = -this.steerAngle * 1.6;}
      // Two-bone arm: shoulder fixed beside the seat, elbow bends down and outward, forearm ends at the wrist.
      solveArms() {
        this.group.updateMatrixWorld(true);const L1 = .3, L2 = .27;
        this.wrists.forEach((wrist, i) => {
          const side = i ? 1 : -1, S = V3(-.37 + side * .2, 1.05, .27), W = this.body.worldToLocal(wrist.getWorldPosition(V3(0, 0, 0)));
          const D = W.clone().sub(S), dir = D.clone().normalize(), d = Math.min(L1 + L2 - .001, Math.max(.05, D.length()));
          const a = (L1 * L1 - L2 * L2 + d * d) / (2 * d), h = Math.sqrt(Math.max(0, L1 * L1 - a * a)), pole = V3(side, -1.3, .2);pole.addScaledVector(dir, -pole.dot(dir)).normalize();
          const E = S.clone().addScaledVector(dir, a).addScaledVector(pole, h), fd = W.clone().sub(E).normalize(), m = this.forearms[i];
          m.position.copy(W).addScaledVector(fd, -.15);m.quaternion.setFromUnitVectors(V3(0, 1, 0), fd);
        });
      }
    }

    // ---------- Tracked excavator (about 14 t) ----------
    class Excavator extends Vehicle {
      constructor(x, z, yaw) {
        super(x, z, yaw, 1.6, 4.4);this.width = 2.5;this.height = 3;this.kind = 'excavator';this.name = 'Excavator';this.swing = 0;this.boom = .35;this.stickA = -1.35;this.bucketA = -1.0;this.load = 0;this.engine = null;this.vl = 0;this.vr = 0;this.trackPhase = 0;
        const yellow = new T.MeshPhysicalMaterial({color: 0xf0b400, roughness: .45, metalness: .1, clearcoat: .4}), dark = lib.paint(0x1f2022, .6), steel = lib.metal(0x6c7074, .4), chrome = lib.chrome(), glassM = new T.MeshPhysicalMaterial({color: 0x26323a, transparent: true, opacity: .35, roughness: .05});
        const g = this.body = new T.Group();this.group.add(g);
        // Undercarriage: two track frames with links, idlers and sprockets.
        this.tracks = [];
        for (const s of [-1, 1]) {
          const frame = mesh(g, rounded(.6, .7, 4.1, .3), dark, s * 1.0, .4, 0);
          const links = new T.Group();g.add(links);links.position.set(s * 1.0, .4, 0);
          for (let i = 0; i < 26; i++) box(links, .62, .05, .14, 0, .37, -1.95 + i * .156, steel);
          for (let i = 0; i < 26; i++) box(links, .62, .05, .14, 0, -.37, -1.95 + i * .156, steel);
          this.tracks.push(links);
          for (const z of [-1.7, 1.7]) {const w = cyl(g, .33, .33, .5, s * 1.0, .4, z, steel, 18);w.rotation.z = Math.PI / 2;}
        }
        box(g, 1.4, .4, 1.6, 0, .75, 0, dark);
        // Upper structure (swings): house, counterweight, cab, engine hood.
        this.upper = new T.Group();this.upper.position.y = 1.0;g.add(this.upper);
        mesh(this.upper, rounded(2.4, .9, 2.8, .12), yellow, 0, .55, .25);
        mesh(this.upper, rounded(2.4, .8, .7, .25), dark, 0, .55, 1.6);
        box(this.upper, 1.3, .25, 1.3, .5, 1.1, .7, yellow);
        const cab = new T.Group();cab.position.set(-.62, 1.0, -.55);this.upper.add(cab);
        for (const [x, z] of [[-.45, -.55], [.45, -.55], [-.45, .55], [.45, .55]]) box(cab, .06, 1.6, .06, x, .8, z, dark);
        box(cab, .98, .06, 1.2, 0, 1.6, 0, yellow);
        for (const [w, d, x, z] of [[.9, .02, 0, -.56], [.02, 1.1, -.46, 0], [.02, 1.1, .46, 0]]) {const p = box(cab, w, 1.45, d, x, .8, z, glassM);p.castShadow = false;}
        box(cab, .5, .45, .5, 0, .25, .2, lib.fabric(0x202020));box(cab, .5, .6, .1, 0, .65, .45, lib.fabric(0x202020));
        // Control levers in the cab with hands on them.
        this.levers = [];
        for (const s of [-1, 1]) {const lv = new T.Group();lv.position.set(s * .32, .45, -.05);cab.add(lv);cyl(lv, .012, .012, .25, 0, .12, 0, dark, 8);mesh(lv, new T.SphereGeometry(.03, 10, 8), dark, 0, .26, 0);this.levers.push(lv);
          const h = hand(lv, 0, .26, 0, new T.Euler(0, 0, s * .3), s < 0);h.scale.setScalar(.9);h.visible = false;lv.userData.hand = h;}
        for (const s of [-1, 1]) {const pedal = box(cab, .1, .3, .04, s * .12, .2, -.45, steel);pedal.rotation.x = -.4;}
        this.cab = cab;
        // Boom, stick (dipper) and bucket, pinned joints with hydraulic cylinders.
        this.boomPivot = new T.Group();this.boomPivot.position.set(.25, .85, -1.0);this.upper.add(this.boomPivot);
        mesh(this.boomPivot, profile([[0, -.25], [-2.3, .55], [-3.2, .5], [-3.2, .2], [-2.3, .15], [0, .15]], .4, .04), yellow);
        this.stickPivot = new T.Group();this.stickPivot.position.set(0, .35, -3.15);this.boomPivot.add(this.stickPivot);
        mesh(this.stickPivot, profile([[.3, .15], [0, .2], [-2.2, .08], [-2.2, -.12], [0, -.15], [.3, -.1]], .3, .03), yellow);
        this.bucketPivot = new T.Group();this.bucketPivot.position.set(0, 0, -2.2);this.stickPivot.add(this.bucketPivot);
        const bucket = new T.Group();this.bucketPivot.add(bucket);
        mesh(bucket, new T.CylinderGeometry(.45, .45, .9, 18, 1, true, Math.PI * .5, Math.PI * 1.1), dark, 0, -.35, -.2).rotation.z = Math.PI / 2;
        for (const s of [-1, 1]) {const side = mesh(bucket, new T.CircleGeometry(.45, 18, Math.PI * .5, Math.PI * 1.1), dark, s * .45, -.35, -.2);side.rotation.y = s * Math.PI / 2;side.material = dark;}
        for (let i = 0; i < 5; i++) box(bucket, .06, .06, .16, -.36 + i * .18, -.78, -.55, steel);
        this.dirt = mesh(bucket, new T.SphereGeometry(.38, 12, 8), lib.concrete(0x6b5541), 0, -.4, -.25);this.dirt.scale.set(1.1, .55, .9);this.dirt.visible = false;
        this.tip = new T.Object3D();this.tip.position.set(0, -.8, -.62);bucket.add(this.tip);
        // Hydraulic cylinders (rods follow the joints every frame).
        this.rams = [this.ram(this.upper, V3(.25, .45, -.7), this.boomPivot, V3(0, .45, -1.4)), this.ram(this.boomPivot, V3(0, .62, -1.9), this.stickPivot, V3(0, .4, .25)), this.ram(this.stickPivot, V3(0, .25, -.3), this.bucketPivot, V3(0, .15, .1))];
        g.traverse(o => {if (o.isMesh && !o.material.transparent) o.castShadow = true;});
        for (const t of this.tracks) {t.userData.noMerge = true;}this.upper.userData.noMerge = true;lib.mergeStatic(g);
        this.boomPivot.userData.noMerge = true;this.levers.forEach(l => {l.userData.noMerge = true;});this.upper.userData.noMerge = false;lib.mergeStatic(this.upper);this.upper.userData.noMerge = true;
        this.tracks.forEach(t => {t.userData.noMerge = false;lib.mergeStatic(t);t.userData.noMerge = true;});
        this.pose();this.settle(1);this.place();
      }
      ram(parentA, a, parentB, b) {
        const barrel = new T.Mesh(new T.CylinderGeometry(.08, .08, 1, 12), lib.metal(0x2a2b2c, .5)), rod = new T.Mesh(new T.CylinderGeometry(.045, .045, 1, 10), lib.chrome());
        this.group.add(barrel, rod);return {parentA, a, parentB, b, barrel, rod};
      }
      pose() {
        this.upper.rotation.y = this.swing;this.boomPivot.rotation.x = this.boom;this.stickPivot.rotation.x = this.stickA;this.bucketPivot.rotation.x = this.bucketA;
        this.group.updateMatrixWorld(true);const inv = this.group.matrixWorld.clone().invert();
        for (const r of this.rams) {
          const pa = r.parentA.localToWorld(r.a.clone()).applyMatrix4(inv), pb = r.parentB.localToWorld(r.b.clone()).applyMatrix4(inv), d = pb.clone().sub(pa), len = d.length(), q = new T.Quaternion().setFromUnitVectors(V3(0, 1, 0), d.normalize());
          r.barrel.scale.set(1, len * .55, 1);r.barrel.position.copy(pa).addScaledVector(d, len * .275);r.barrel.quaternion.copy(q);
          r.rod.scale.set(1, len * .5, 1);r.rod.position.copy(pb).addScaledVector(d, -len * .25);r.rod.quaternion.copy(q);
        }
      }
      seat() {this.group.updateMatrixWorld(true);const p = this.cab.localToWorld(V3(0, 1.22, .1));return {x: p.x, y: p.y, z: p.z, yaw: this.yaw + this.swing, pitch: this.pitch, roll: this.terrainRoll || 0};}
      enter() {this.active = true;this.levers.forEach(l => {l.userData.hand.visible = true;});audio.sfx('carDoor');audio.sfx('starter');this.engine = audio.engine('excavator');}
      exit() {this.active = false;this.levers.forEach(l => {l.userData.hand.visible = false;});if (this.engine) this.engine.stop();this.engine = null;this.vl = this.vr = 0;audio.sfx('carDoor');return this.exitPoint(-1);}
      update(dt, input) {
        let flow = 0;
        if (this.active) {
          // Tracks: L1 left forward, L2 left back, R1 right forward, R2 right back.
          const tl = (input.l1 ? 1 : 0) - input.l2, tr = (input.r1 ? 1 : 0) - input.r2, k = Math.min(1, dt * 2.5);
          this.vl += (tl * 1.6 - this.vl) * k;this.vr += (tr * 1.6 - this.vr) * k;
          this.speed = (this.vl + this.vr) / 2;this.yaw += (this.vl - this.vr) / 2.0 * dt * .9;
          // ISO joystick pattern: left stick left/right swings, forward pushes the stick (dipper) out, back pulls it in;
          // right stick forward lowers the boom, back raises it; right stick left curls the bucket in, right dumps it.
          const st = input.sticks, dz = v => Math.abs(v) < .12 ? 0 : v;
          const swing = dz(st.lx), stick = dz(st.ly), boom = dz(st.ry), bucket = dz(st.rx);
          this.swing -= swing * .55 * dt;
          this.stickA = Math.max(-2.7, Math.min(-.15, this.stickA - stick * .7 * dt));
          this.boom = Math.max(-1.05, Math.min(.95, this.boom + boom * .45 * dt));
          this.bucketA = Math.max(-2.6, Math.min(.8, this.bucketA + bucket * 1.1 * dt));
          flow = Math.min(1, Math.abs(swing) + Math.abs(stick) + Math.abs(boom) + Math.abs(bucket));
          this.levers[0].rotation.set(stick * .35, 0, -swing * .35);this.levers[1].rotation.set(-boom * .35, 0, -bucket * .35);
          if (this.engine) {this.engine.set(1000 + 1000 * Math.max(flow, Math.abs(this.speed) / 1.6), Math.max(flow, Math.abs(this.speed) / 1.6));this.engine.hydraulics(flow);}
          this.pose();
          // Digging: the bucket fills when its teeth are in the ground while curling in; dumping drops a pile.
          const tipW = this.tip.getWorldPosition(new T.Vector3()), groundY = heightAt(tipW.x, tipW.z);
          if (tipW.y < groundY + .12 && bucket < -.2 && this.load < 1) {this.load = Math.min(1, this.load + dt * 1.8);if (Math.random() < dt * 6) audio.sfx('scrape', {gain: .5});}
          if (tipW.y < groundY - .45) {this.boom = Math.min(.95, this.boom + .5 * dt);this.stickA = Math.max(-2.5, this.stickA - .3 * dt);}
          if (this.load > .25 && this.bucketA > .1) {this.load = 0;audio.sfx('dump');if (this.onDump) this.onDump(tipW.x, tipW.z);}
          this.dirt.visible = this.load > .15;this.dirt.scale.set(1.1 * this.load, .55 * this.load, .9 * this.load);
        } else {this.vl = this.vr = this.speed = 0;}
        this.move(dt);this.settle(dt);this.roll = this.terrainRoll || 0;
        this.trackPhase += dt;this.tracks[0].position.z = ((this.vl * this.trackPhase) % .156);this.tracks[1].position.z = ((this.vr * this.trackPhase) % .156);
        this.place();if (!this.active) this.pose();
      }
      hipAnchor(seat) {return seat === 'd' ? {obj: this.cab, pos: V3(0, .6, .28), stance: 'drive'} : null;}
      netExtra() {return [this.swing, this.boom, this.stickA, this.bucketA, this.load, this.vl, this.vr];}
      netApply(a) {[this.swing, this.boom, this.stickA, this.bucketA, this.load, this.vl, this.vr] = a;this.pose();this.dirt.visible = this.load > .15;this.dirt.scale.set(1.1 * this.load, .55 * this.load, .9 * this.load);}
      netAnimate(dt) {this.trackPhase += dt;this.tracks[0].position.z = ((this.vl * this.trackPhase) % .156);this.tracks[1].position.z = ((this.vr * this.trackPhase) % .156);}
    }
    return {Motorcycle, Car, Excavator};
  }
  return {create};
});
