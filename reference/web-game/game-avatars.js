/* Other players: a jointed humanoid (jacket colour per player slot) with walk, crouch, prone, sit, drive and ride poses,
 * a beer bottle when drinking, a name tag and a chat bubble; plus a small FPV quad for players flying their drone.
 * GameAvatars.create({T, lib}) -> {avatar(slot, name) -> Avatar, drone() -> Group, SLOT_COLORS}
 * Avatar: {group, setName(n), say(text), pose({stance, speed, dt, beer, seat}), update(dt, camera), dispose()}
 * The group's origin is between the feet and it faces -z (like the camera at yaw 0). For seated poses the origin is the hip.
 */
(function (root, factory) {
  root.GameAvatars = factory();
})(typeof window === 'undefined' ? globalThis : window, function () {
  'use strict';
  const SLOT_COLORS = [0xc0392b, 0x2471a3, 0x1e8449, 0xd68910, 0x7d3c98];
  const SLOT_CSS = ['#e05a4c', '#4d9de0', '#3fbf6f', '#f0a830', '#a768c9'];
  function create({T, lib}) {
    const skin = new T.MeshStandardMaterial({color: 0xd9a988, roughness: .7}), hair = new T.MeshStandardMaterial({color: 0x3b2a1e, roughness: .9});
    const pants = lib.fabric(0x2b3140), shoes = new T.MeshStandardMaterial({color: 0x1c1c1c, roughness: .6}), glass = new T.MeshStandardMaterial({color: 0x5a3a12, roughness: .15, metalness: .1, transparent: true, opacity: .85});
    const cap = (r, len) => new T.CapsuleGeometry(r, len, 4, 10);
    function sprite(width, height) {
      const c = document.createElement('canvas');c.width = 512;c.height = Math.round(512 * height / width);
      const tex = new T.CanvasTexture(c);tex.colorSpace = T.SRGBColorSpace;
      const s = new T.Sprite(new T.SpriteMaterial({map: tex, depthTest: false, transparent: true}));s.scale.set(width, height, 1);s.renderOrder = 10;
      return {s, c, ctx: c.getContext('2d'), tex};
    }
    function roundRect(ctx, x, y, w, h, r) {ctx.beginPath();ctx.moveTo(x + r, y);ctx.arcTo(x + w, y, x + w, y + h, r);ctx.arcTo(x + w, y + h, x, y + h, r);ctx.arcTo(x, y + h, x, y, r);ctx.arcTo(x, y, x + w, y, r);ctx.closePath();}
    function avatar(slot, name) {
      const jacket = lib.fabric(SLOT_COLORS[slot % 5]), g = new T.Group();
      const m = (parent, geo, mat, x = 0, y = 0, z = 0) => {const o = new T.Mesh(geo, mat);o.position.set(x, y, z);o.castShadow = true;parent.add(o);return o;};
      const hips = new T.Group();hips.position.y = .95;g.add(hips);
      m(hips, new T.BoxGeometry(.32, .16, .2), pants, 0, 0, 0);
      const torso = new T.Group();hips.add(torso);
      m(torso, cap(.17, .3), jacket, 0, .28, 0).scale.set(1, 1, .72);
      const neck = new T.Group();neck.position.y = .58;torso.add(neck);m(neck, cap(.05, .05), skin, 0, .04, 0);
      const head = new T.Group();head.position.y = .14;neck.add(head);
      m(head, new T.SphereGeometry(.112, 18, 14), skin, 0, .02, 0).scale.set(.92, 1.08, 1);
      m(head, new T.SphereGeometry(.118, 18, 10, 0, Math.PI * 2, 0, Math.PI * .55), hair, 0, .04, .012);
      m(head, new T.SphereGeometry(.018, 8, 6), skin, 0, 0, -.11);
      for (const s of [-1, 1]) m(head, new T.SphereGeometry(.013, 8, 6), new T.MeshStandardMaterial({color: 0x222222}), s * .04, .03, -.1);
      const arm = s => {
        const sh = new T.Group();sh.position.set(s * .22, .5, 0);torso.add(sh);m(sh, cap(.058, .2), jacket, 0, -.14, 0);
        const el = new T.Group();el.position.y = -.29;sh.add(el);m(el, cap(.05, .19), jacket, 0, -.12, 0);m(el, new T.SphereGeometry(.048, 10, 8), skin, 0, -.27, 0);
        return {sh, el};
      };
      const leg = s => {
        const hip = new T.Group();hip.position.set(s * .1, -.05, 0);hips.add(hip);m(hip, cap(.078, .3), pants, 0, -.21, 0);
        const kn = new T.Group();kn.position.y = -.43;hip.add(kn);m(kn, cap(.062, .32), pants, 0, -.2, 0);m(kn, new T.BoxGeometry(.1, .07, .24), shoes, 0, -.43, -.05);
        return {hip, kn};
      };
      const L = {arm: arm(-1), leg: leg(-1)}, R = {arm: arm(1), leg: leg(1)};
      const bottle = new T.Group();R.arm.el.add(bottle);bottle.position.set(0, -.3, -.04);m(bottle, new T.CylinderGeometry(.03, .032, .16, 12), glass, 0, -.02, 0);m(bottle, new T.CylinderGeometry(.012, .02, .07, 10), glass, 0, .09, 0);bottle.visible = false;
      const tag = sprite(1.3, .26);tag.s.position.y = 2.08;g.add(tag.s);
      const bubble = sprite(2.4, .7);bubble.s.position.y = 2.55;bubble.s.visible = false;g.add(bubble.s);
      let phase = 0, bubbleT = 0, nameNow = '';
      function setName(n) {
        if (n === nameNow) return;nameNow = n;const c = tag.ctx, W = tag.c.width, H = tag.c.height;c.clearRect(0, 0, W, H);
        c.font = 'bold 54px Segoe UI, Arial, sans-serif';const w = Math.min(W - 10, c.measureText(n).width + 50);
        c.fillStyle = 'rgba(10,14,20,.62)';roundRect(c, (W - w) / 2, 8, w, H - 16, 26);c.fill();c.fillStyle = SLOT_CSS[slot % 5];c.fillRect((W - w) / 2 + 16, H / 2 - 8, 16, 16);
        c.fillStyle = '#fff';c.textAlign = 'center';c.textBaseline = 'middle';c.fillText(n, W / 2 + 12, H / 2 + 2);tag.tex.needsUpdate = true;
      }
      function say(text) {
        const c = bubble.ctx, W = bubble.c.width, H = bubble.c.height;c.clearRect(0, 0, W, H);c.font = '600 40px Segoe UI, Arial, sans-serif';
        const words = String(text).split(/\s+/), lines = [];let line = '';
        for (const w of words) {const t = line ? line + ' ' + w : w;if (c.measureText(t).width > W - 60 && line) {lines.push(line);line = w;} else line = t;}if (line) lines.push(line);
        const shown = lines.slice(0, 3), h = 26 + shown.length * 46;c.fillStyle = 'rgba(255,255,255,.93)';roundRect(c, 10, H - h - 18, W - 20, h, 24);c.fill();
        c.beginPath();c.moveTo(W / 2 - 16, H - 19);c.lineTo(W / 2, H - 2);c.lineTo(W / 2 + 16, H - 19);c.fill();
        c.fillStyle = '#16191d';c.textAlign = 'center';c.textBaseline = 'middle';shown.forEach((l, i) => c.fillText(l, W / 2, H - h - 18 + 36 + i * 46));
        bubble.tex.needsUpdate = true;bubble.s.visible = true;bubbleT = 7;
      }
      const set = (o, x = 0, y = 0, z = 0) => o.rotation.set(x, y, z);
      // stance: stand | crouch | prone | sit | drive | ride | pillion | drone ; seat poses put the hip at the group origin.
      function pose({stance = 'stand', speed = 0, dt = 0, beer = false}) {
        phase += speed * dt * 3.2;const sw = Math.sin(phase), amp = Math.min(1, speed / 3.6);
        hips.position.set(0, .95, 0);set(hips);set(torso);set(neck);set(head);
        for (const [s, side] of [[1, L], [-1, R]]) {set(side.leg.hip);set(side.leg.kn);set(side.arm.sh, 0, 0, s * .08);set(side.arm.el);}
        const seated = stance === 'sit' || stance === 'drive' || stance === 'ride' || stance === 'pillion';
        if (seated) {
          hips.position.set(0, 0, 0);
          for (const side of [L, R]) {set(side.leg.hip, stance === 'ride' || stance === 'pillion' ? 1.15 : 1.45);set(side.leg.kn, stance === 'ride' || stance === 'pillion' ? -1.75 : -1.45);}
          if (stance === 'ride' || stance === 'pillion') {L.leg.hip.rotation.z = -.22;R.leg.hip.rotation.z = .22;torso.rotation.x = stance === 'ride' ? -.42 : -.15;}
          if (stance === 'drive' || stance === 'ride') for (const side of [L, R]) {side.arm.sh.rotation.x = stance === 'ride' ? 1.1 : .95;side.arm.el.rotation.x = .45;}
          else for (const side of [L, R]) {side.arm.sh.rotation.x = .35;side.arm.el.rotation.x = .9;}
        } else if (stance === 'crouch') {
          hips.position.y = .62;torso.rotation.x = -.35;
          for (const side of [L, R]) {set(side.leg.hip, 1.25 + sw * .25 * amp);set(side.leg.kn, -1.95);side.arm.sh.rotation.x = .5;side.arm.el.rotation.x = .6;}
        } else if (stance === 'prone') {
          hips.position.y = .17;hips.rotation.x = -Math.PI / 2 + .05;neck.rotation.x = .9;
          L.leg.hip.rotation.x = sw * .25 * amp;R.leg.hip.rotation.x = -sw * .25 * amp;
          for (const side of [L, R]) {side.arm.sh.rotation.x = 2.6;side.arm.el.rotation.x = .9;}
        } else {
          L.leg.hip.rotation.x = sw * .55 * amp;R.leg.hip.rotation.x = -sw * .55 * amp;
          L.leg.kn.rotation.x = -Math.max(0, -sw) * .9 * amp;R.leg.kn.rotation.x = -Math.max(0, sw) * .9 * amp;
          L.arm.sh.rotation.x = -sw * .45 * amp;R.arm.sh.rotation.x = sw * .45 * amp;L.arm.el.rotation.x = R.arm.el.rotation.x = .25 + .3 * amp;
          hips.position.y = .95 + Math.abs(Math.cos(phase)) * .03 * amp;torso.rotation.x = -.06 * amp;
          if (stance === 'drone') for (const side of [L, R]) {side.arm.sh.rotation.x = .8;side.arm.el.rotation.x = .9;}
        }
        bottle.visible = beer;if (beer && !seated) {R.arm.sh.rotation.x = .55;R.arm.el.rotation.x = 1.5;}
        if (beer && seated) {R.arm.sh.rotation.x = .5;R.arm.el.rotation.x = 1.6;}
        tag.s.position.y = seated ? 1.2 : stance === 'prone' ? .7 : stance === 'crouch' ? 1.75 : 2.08;bubble.s.position.y = tag.s.position.y + .45;
      }
      function update(dt, camera) {
        if (bubbleT > 0 && (bubbleT -= dt) <= 0) bubble.s.visible = false;
        const d = camera.position.distanceTo(g.getWorldPosition(tmp));tag.s.visible = d < 90;const k = Math.max(1, d / 14);tag.s.scale.set(1.3 * k, .26 * k, 1);bubble.s.scale.set(2.4 * Math.max(1, d / 18), .7 * Math.max(1, d / 18), 1);
      }
      setName(name);pose({});
      return {group: g, setName, say, pose, update, dispose() {g.traverse(o => {if (o.geometry) o.geometry.dispose();});tag.tex.dispose();bubble.tex.dispose();}};
    }
    const tmp = new T.Vector3();
    function drone() {
      const g = new T.Group(), carbon = new T.MeshStandardMaterial({color: 0x1b1d20, roughness: .5}), propM = new T.MeshBasicMaterial({color: 0x2e3135, transparent: true, opacity: .35, side: T.DoubleSide, depthWrite: false});
      const body = new T.Mesh(new T.BoxGeometry(.09, .04, .14), carbon);g.add(body);
      for (const [x, z] of [[-.1, -.1], [.1, -.1], [-.1, .1], [.1, .1]]) {
        const a = new T.Mesh(new T.BoxGeometry(.018, .012, .15), carbon);a.position.set(x / 2, 0, z / 2);a.rotation.y = Math.atan2(x, z);g.add(a);
        const p = new T.Mesh(new T.CircleGeometry(.065, 16), propM);p.rotation.x = -Math.PI / 2;p.position.set(x, .025, z);g.add(p);
      }
      for (const [c, z] of [[0xff2020, .07], [0x20ff40, -.07]]) {const l = new T.Mesh(new T.SphereGeometry(.008, 6, 4), new T.MeshBasicMaterial({color: c}));l.position.set(0, -.02, z);g.add(l);}
      return g;
    }
    return {avatar, drone, SLOT_COLORS, SLOT_CSS};
  }
  return {create, SLOT_CSS};
});
