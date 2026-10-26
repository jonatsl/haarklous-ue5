/* First-person pistol and gloved hands for the case mode (drawn in their own scene by case-game.js).
 * Gun frame: bore axis on y = 0, muzzle toward -z (front of slide at z = -0.165), sight tops at y = 0.029,
 * so aiming down the sights centres the front post in the rear notch. Sizes follow a compact 9 mm service pistol.
 * Viewmodel.build({T, lib, gun, slide}) adds meshes to the gun group (static) and the slide group (recoils).
 */
(function (root, factory) {
  root.Viewmodel = factory();
})(typeof window === 'undefined' ? globalThis : window, function () {
  'use strict';
  function build({T, lib, gun, slide}) {
    const nitride = new T.MeshStandardMaterial({color: 0x2b2d31, metalness: .75, roughness: .32});
    const barrelMat = new T.MeshStandardMaterial({color: 0x6d6a63, metalness: .85, roughness: .28});
    const frameMat = lib.polymer(0x232528), grip = lib.polymer(0x1f2124), dark = new T.MeshStandardMaterial({color: 0x0b0b0c, roughness: .6});
    const dot = new T.MeshStandardMaterial({color: 0xf4f1e2, emissive: 0x6a6a5a, roughness: .4});
    const glove = lib.glove(), sleeve = lib.sleeve(), cuffMat = new T.MeshStandardMaterial({color: 0x17191b, roughness: .7});
    const add = (parent, geometry, material, x = 0, y = 0, z = 0) => {const m = new T.Mesh(geometry, material);m.position.set(x, y, z);parent.add(m);return m;};
    // Extrude a side profile given as (u = -z, v = y) points across the x axis, centred on x = 0.
    function profile(points, width, bevel, curves = []) {
      const shape = new T.Shape();points.forEach(([u, v], i) => i ? shape.lineTo(u, v) : shape.moveTo(u, v));shape.closePath();
      curves.forEach(c => shape.holes.push(c));
      const g = new T.ExtrudeGeometry(shape, {depth: width - 2 * bevel, bevelEnabled: bevel > 0, bevelThickness: bevel, bevelSize: bevel, bevelSegments: 3, curveSegments: 10});
      g.translate(0, 0, -(width - 2 * bevel) / 2);g.rotateY(Math.PI / 2);g.computeVertexNormals();return g;
    }
    // ---- Slide (moves on recoil).
    add(slide, profile([[-.021, -.008], [.165, -.008], [.165, .011], [.156, .0215], [-.014, .0215], [-.021, .015]], .0255, .0016), nitride);
    for (let i = 0; i < 7; i++) for (const sx of [-1, 1]) add(slide, new T.BoxGeometry(.0012, .017, .0013), dark, sx * .0129, .007, .004 + i * .0028);
    // Ejection port with the barrel hood showing on the right.
    add(slide, new T.BoxGeometry(.0105, .006, .032), barrelMat, .006, .0205, -.07);
    add(slide, new T.BoxGeometry(.0006, .009, .034), dark, .0129, .016, -.07);
    // Sights: front post with a white dot, rear notch block with two dots.
    add(slide, new T.BoxGeometry(.0035, .0075, .006), nitride, 0, .0255, -.152);
    add(slide, new T.CylinderGeometry(.0011, .0011, .001, 10), dot, 0, .0262, -.1488).rotation.x = Math.PI / 2;
    for (const sx of [-1, 1]) {add(slide, new T.BoxGeometry(.0055, .0075, .008), nitride, sx * .0047, .0255, .012);add(slide, new T.CylinderGeometry(.0011, .0011, .001, 10), dot, sx * .0047, .0262, .0078).rotation.x = Math.PI / 2;}
    // Muzzle: barrel crown inside the slide front.
    const crown = add(slide, new T.CylinderGeometry(.0058, .0058, .004, 18), barrelMat, 0, 0, -.1655);crown.rotation.x = Math.PI / 2;
    const bore = add(slide, new T.CylinderGeometry(.0045, .0045, .0045, 14), dark, 0, 0, -.1658);bore.rotation.x = Math.PI / 2;
    // ---- Frame: dust cover with accessory rail, trigger guard, trigger, grip and magazine base.
    add(gun, profile([[-.022, -.008], [.152, -.008], [.155, -.016], [.149, -.029], [.07, -.03], [.06, -.026], [.002, -.026], [-.022, -.02]], .0235, .0015), frameMat);
    for (let i = 0; i < 3; i++) add(gun, new T.BoxGeometry(.024, .003, .004), dark, 0, -.0305, -.1 - i * .012);
    const guard = new T.Shape();guard.moveTo(.004, -.026);guard.lineTo(.07, -.026);guard.quadraticCurveTo(.08, -.03, .077, -.045);guard.quadraticCurveTo(.072, -.06, .055, -.06);guard.lineTo(.006, -.058);guard.lineTo(.004, -.026);
    const hole = new T.Path();hole.moveTo(.012, -.031);hole.lineTo(.064, -.031);hole.quadraticCurveTo(.071, -.034, .069, -.045);hole.quadraticCurveTo(.066, -.054, .054, -.054);hole.lineTo(.012, -.052);hole.lineTo(.012, -.031);guard.holes.push(hole);
    const guardGeo = new T.ExtrudeGeometry(guard, {depth: .009, bevelEnabled: true, bevelThickness: .0015, bevelSize: .0012, bevelSegments: 2, curveSegments: 10});guardGeo.translate(0, 0, -.0045);guardGeo.rotateY(Math.PI / 2);
    add(gun, guardGeo, frameMat);
    const trigger = add(gun, new T.BoxGeometry(.0055, .021, .006), dark, 0, -.041, -.036);trigger.rotation.x = .35;
    // Grip with finger grooves, raked about 15 degrees.
    const gripShape = [[.006, -.026], [.004, -.05], [.0, -.056], [-.002, -.07], [-.006, -.077], [-.008, -.09], [-.012, -.097], [-.015, -.112], [-.02, -.128], [-.058, -.128], [-.05, -.06], [-.046, -.03], [-.052, -.02], [-.03, -.02]];
    add(gun, profile(gripShape, .03, .0028), grip);
    add(gun, new T.BoxGeometry(.032, .008, .042), frameMat, 0, -.131, .039);
    // ---- Gloved hands (tubes for fingers along curves) and sleeves.
    const fingerTube = (pts, radius, material = glove) => {
      const curve = new T.CatmullRomCurve3(pts.map(p => new T.Vector3(...p)));
      add(gun, new T.TubeGeometry(curve, 24, radius, 10), material);
      add(gun, new T.SphereGeometry(radius, 12, 10), material, ...pts[pts.length - 1]);
      return curve;
    };
    // Rounded box: a subdivided box whose vertices are pulled onto a rounded shell (hand backs, palms).
    function roundBox(w, h, d, r) {
      const g = new T.BoxGeometry(w, h, d, 6, 6, 6), p = g.attributes.position, v = new T.Vector3(), inner = new T.Vector3(w / 2 - r, h / 2 - r, d / 2 - r);
      for (let i = 0; i < p.count; i++) {
        v.fromBufferAttribute(p, i);const c = v.clone().clamp(inner.clone().negate(), inner), off = v.clone().sub(c);
        if (off.lengthSq() > 0) v.copy(c).add(off.normalize().multiplyScalar(r));p.setXYZ(i, v.x, v.y, v.z);
      }
      g.computeVertexNormals();return g;
    }
    // Grip front edge (z) at height y, used to wrap fingers around it.
    const front = y => -.005 + (-.03 - y) * .29;
    // Right hand: palm on the back strap, three fingers around the front, index finger on the trigger, thumb along the left.
    const palm = add(gun, roundBox(.05, .078, .034, .012), glove, .006, -.079, .05);palm.rotation.x = -.26;
    for (let k = 0; k < 4; k++) add(gun, new T.SphereGeometry(.0085, 10, 8), glove, .022, -.052 - k * .019, .03 + k * .0055);
    for (const [y, r] of [[-.062, .0088], [-.083, .0085], [-.103, .0078]]) {
      const f = front(y);
      fingerTube([[.02, y + .004, f + .03], [.018, y, f + .004], [.006, y - .002, f - .011], [-.009, y - .002, f - .009], [-.018, y, f + .006]], r);
      add(gun, new T.SphereGeometry(r * 1.15, 10, 8), glove, .021, y + .004, f + .032);
    }
    fingerTube([[.02, -.036, .018], [.017, -.039, -.012], [.009, -.042, -.03], [.003, -.046, -.037]], .0085);
    fingerTube([[-.02, -.05, .036], [-.021, -.038, .01], [-.019, -.031, -.022]], .0095);
    // Left (support) hand wraps over the right fingers, thumb forward under the slide.
    const left = add(gun, roundBox(.028, .074, .062, .011), glove, -.031, -.084, .02);left.rotation.x = -.26;
    for (let k = 0; k < 4; k++) add(gun, new T.SphereGeometry(.009, 10, 8), glove, -.04, -.066 - k * .018, -.012 + k * .005);
    for (const [y, r] of [[-.07, .0092], [-.09, .009], [-.108, .0082]]) {
      const f = front(y) - .016;
      fingerTube([[-.034, y + .004, f + .036], [-.03, y, f + .008], [-.014, y - .003, f - .01], [.006, y - .003, f - .008], [.022, y + .001, f + .012]], r);
    }
    fingerTube([[-.035, -.055, .006], [-.026, -.036, -.03], [-.019, -.029, -.062]], .01);
    // Wrists, glove cuffs and sleeves toward the shoulders.
    function arm(from, to, cuffAt) {
      const a = new T.Vector3(...from), b = new T.Vector3(...to), dir = b.clone().sub(a), len = dir.length();
      const sleeveMesh = add(gun, new T.CapsuleGeometry(.037, len, 8, 18), sleeve);sleeveMesh.position.copy(a).add(b).multiplyScalar(.5);sleeveMesh.quaternion.setFromUnitVectors(new T.Vector3(0, 1, 0), dir.clone().normalize());
      // Fabric folds near the cuff.
      for (const t of [.06, .13, .2]) {const ring = add(gun, new T.TorusGeometry(.037, .004, 8, 20), sleeve);ring.position.copy(a).addScaledVector(dir, t);ring.quaternion.setFromUnitVectors(new T.Vector3(0, 0, 1), dir.clone().normalize());}
      const c = new T.Vector3(...cuffAt), cuff = add(gun, new T.CylinderGeometry(.029, .031, .05, 18), cuffMat);cuff.position.copy(c);cuff.quaternion.setFromUnitVectors(new T.Vector3(0, 1, 0), dir.clone().normalize());
      const wrist = add(gun, new T.CapsuleGeometry(.026, .03, 6, 14), glove);wrist.position.copy(c).addScaledVector(dir.clone().normalize(), -.035);wrist.quaternion.copy(cuff.quaternion);
    }
    arm([.03, -.15, .1], [.22, -.33, .38], [.022, -.13, .085]);
    arm([-.045, -.15, .085], [-.25, -.33, .36], [-.04, -.13, .066]);
  }
  return {build};
});
