/* Wristwatch: D-pad ↑ (or the ↑ key) raises the left wrist so you can read the time (the PC's local clock).
 * Steel case, leather strap, analogue dial with a sweeping second hand and a small digital window. Rendered in the
 * first-person overlay scene, like the photo camera.
 * GameWatch.create({T, lib}) -> {model, toggle(), hide(), update(dt) -> visible, get raised}
 */
(function (root, factory) {
  root.GameWatch = factory();
})(typeof window === 'undefined' ? globalThis : window, function () {
  'use strict';
  function create({T, lib}) {
    const V3 = (x = 0, y = 0, z = 0) => new T.Vector3(x, y, z);
    const mesh = (parent, geo, mat, x = 0, y = 0, z = 0) => {const m = new T.Mesh(geo, mat);m.position.set(x, y, z);parent.add(m);return m;};
    const model = new T.Group(), wrist = new T.Group();model.add(wrist);model.visible = false;
    // Wrist frame: forearm toward -x, hand toward +x, back of the wrist (watch face) +y, dial 12 o'clock toward -z.
    const glove = lib.glove(), sleeve = lib.sleeve(), steel = lib.metal(0xc9ccd1, .22), strap = new T.MeshStandardMaterial({color: 0x3a2618, roughness: .75, side: T.DoubleSide});
    const arm = mesh(wrist, new T.CapsuleGeometry(.043, .34, 6, 16), sleeve, -.23, -.004, 0);arm.rotation.z = Math.PI / 2;
    const cuff = mesh(wrist, new T.CylinderGeometry(.046, .046, .03, 24), sleeve, -.06, -.004, 0);cuff.rotation.z = Math.PI / 2;
    // Relaxed gloved fist: rounded back of the hand, knuckles, fingers curled under, thumb along the side.
    mesh(wrist, new T.SphereGeometry(1, 20, 14), glove, .064, -.006, 0).scale.set(.043, .022, .041);
    for (let i = 0; i < 4; i++) {
      const z = -.03 + i * .02;mesh(wrist, new T.SphereGeometry(.0105, 12, 8), glove, .1, .0, z);
      const f = mesh(wrist, new T.CapsuleGeometry(.0105, .026, 4, 8), glove, .106, -.02, z);f.rotation.z = -.25;
    }
    const thumb = mesh(wrist, new T.CapsuleGeometry(.0115, .042, 4, 8), glove, .088, -.014, .046);thumb.rotation.set(.35, 0, Math.PI / 2 - .3);
    const band = mesh(wrist, new T.CylinderGeometry(.04, .04, .021, 32, 1, true), strap, .005, -.004, 0);band.rotation.z = Math.PI / 2;
    // Case, bezel, crown, dial, crystal.
    mesh(wrist, new T.CylinderGeometry(.0215, .0225, .01, 48), steel, .005, .039, 0);
    const bezel = mesh(wrist, new T.TorusGeometry(.0205, .0022, 8, 48), steel, .005, .0445, 0);bezel.rotation.x = Math.PI / 2;
    const crown = mesh(wrist, new T.CylinderGeometry(.0028, .0028, .006, 12), steel, .0295, .039, 0);crown.rotation.z = Math.PI / 2;
    for (const s of [-1, 1]) mesh(wrist, new T.BoxGeometry(.012, .006, .03), steel, .005 + s * .022, .036, 0);
    const canvas = document.createElement('canvas');canvas.width = canvas.height = 256;const c = canvas.getContext('2d');
    const tex = new T.CanvasTexture(canvas);tex.colorSpace = T.SRGBColorSpace;tex.anisotropy = 4;
    const dial = mesh(wrist, new T.CircleGeometry(.0188, 48), new T.MeshBasicMaterial({map: tex, toneMapped: false}), .005, .0446, 0);dial.rotation.x = -Math.PI / 2;
    const crystal = mesh(wrist, new T.CircleGeometry(.0195, 48), new T.MeshPhysicalMaterial({color: 0xffffff, transparent: true, opacity: .08, roughness: .02, metalness: 0}), .005, .0452, 0);crystal.rotation.x = -Math.PI / 2;
    model.traverse(o => {o.renderOrder = 2;});
    function drawDial(now) {
      const h = now.getHours(), m = now.getMinutes(), s = now.getSeconds() + now.getMilliseconds() / 1000, R = 128;
      c.setTransform(1, 0, 0, 1, 0, 0);const grd = c.createRadialGradient(R, R * .8, 10, R, R, R);grd.addColorStop(0, '#fbf8f0');grd.addColorStop(1, '#e4dfd2');c.fillStyle = grd;c.fillRect(0, 0, 256, 256);
      c.translate(R, R);
      for (let i = 0; i < 60; i++) {const a = i / 60 * Math.PI * 2, big = i % 5 === 0;c.strokeStyle = '#1b1b1b';c.lineWidth = big ? 6 : 2;c.beginPath();c.moveTo(Math.sin(a) * (R - 8), -Math.cos(a) * (R - 8));c.lineTo(Math.sin(a) * (R - (big ? 26 : 16)), -Math.cos(a) * (R - (big ? 26 : 16)));c.stroke();}
      c.fillStyle = '#1b1b1b';c.font = 'bold 30px Georgia, serif';c.textAlign = 'center';c.textBaseline = 'middle';
      [[12, 0], [3, 1], [6, 2], [9, 3]].forEach(([n, k]) => {const a = k * Math.PI / 2;c.fillText(String(n), Math.sin(a) * (R - 46), -Math.cos(a) * (R - 46));});
      c.font = '600 13px Arial, sans-serif';c.fillText('HAARKLOUS', 0, -38);
      // Digital window (also makes the time easy to read at a glance).
      c.fillStyle = '#20262b';c.fillRect(-40, 26, 80, 30);c.fillStyle = '#d8f5e0';c.font = 'bold 22px Consolas, monospace';c.fillText(`${String(h).padStart(2, '0')}:${String(m).padStart(2, '0')}`, 0, 42);
      const hand = (a, len, w, col) => {c.save();c.rotate(a);c.fillStyle = col;c.beginPath();c.moveTo(-w / 2, 14);c.lineTo(-w * .35, -len);c.lineTo(0, -len - 6);c.lineTo(w * .35, -len);c.lineTo(w / 2, 14);c.closePath();c.fill();c.restore();};
      hand(((h % 12) + m / 60) / 12 * Math.PI * 2, R * .5, 9, '#151515');hand((m + s / 60) / 60 * Math.PI * 2, R * .74, 6, '#151515');
      c.save();c.rotate(s / 60 * Math.PI * 2);c.strokeStyle = '#c0262d';c.lineWidth = 2;c.beginPath();c.moveTo(0, 22);c.lineTo(0, -(R - 14));c.stroke();c.restore();
      c.fillStyle = '#c0262d';c.beginPath();c.arc(0, 0, 5, 0, Math.PI * 2);c.fill();
      tex.needsUpdate = true;
    }
    // Raised pose in the overlay camera's space: dial facing the eye, forearm running down-left out of view.
    const yAxis = V3(0, .3, 1).normalize(), xAxis = V3(.86, .5, -.1), zAxis = V3();xAxis.addScaledVector(yAxis, -xAxis.dot(yAxis)).normalize();zAxis.crossVectors(xAxis, yAxis);
    const raisedQ = new T.Quaternion().setFromRotationMatrix(new T.Matrix4().makeBasis(xAxis, yAxis, zAxis));
    const loweredQ = raisedQ.clone().multiply(new T.Quaternion().setFromEuler(new T.Euler(-.9, 0, .5)));
    const raisedP = V3(-.02, -.05, -.34), loweredP = V3(-.16, -.46, -.36);
    let target = 0, t = 0, holdT = 0, lastDraw = 0;
    function toggle() {target = target ? 0 : 1;holdT = 0;}
    function hide() {target = 0;}
    function update(dt) {
      t += Math.sign(target - t) * Math.min(Math.abs(target - t), dt / .32);
      if (target && (holdT += dt) > 6) target = 0;
      model.visible = t > .001;if (!model.visible) return false;
      const e = t < .5 ? 2 * t * t : 1 - Math.pow(-2 * t + 2, 2) / 2;
      wrist.position.lerpVectors(loweredP, raisedP, e);wrist.quaternion.slerpQuaternions(loweredQ, raisedQ, e);
      const now = performance.now();if (now - lastDraw > 120) {lastDraw = now;drawDial(new Date());}
      return true;
    }
    drawDial(new Date());
    return {model, toggle, hide, update, get raised() {return target > 0;}};
  }
  return {create};
});
