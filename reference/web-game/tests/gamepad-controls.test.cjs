const test = require('node:test');
const assert = require('node:assert/strict');
const G = require('../gamepad-controls.js');
const buttons = (count, on=[]) => Array.from({length:count}, (_, i) => ({pressed:on.includes(i), value:on.includes(i)?1:0}));

test('standard DualShock 4 mapping reads sticks, R2 and face buttons',()=>{
  const pad={id:'Wireless Controller (STANDARD GAMEPAD Vendor: 054c Product: 05c4)',mapping:'standard',axes:[0,-1,.5,0],buttons:buttons(18,[0,9])};
  pad.buttons[7]={pressed:false,value:.4};
  pad.buttons[6]={pressed:false,value:.7};
  const s=G.read(pad);
  assert.equal(s.move.x,0); assert.equal(s.move.y,-1);
  assert.ok(s.look.x>0&&s.look.x<.5); assert.equal(s.run,.4); assert.equal(s.aim,.7);
  assert.equal(s.buttons.cross,true); assert.equal(s.buttons.options,true); assert.equal(s.buttons.square,false);
});

test('dead zone removes stick drift and rescales to full deflection',()=>{
  assert.deepEqual(G.deadZone(.1,-.08),{x:0,y:0});
  const full=G.deadZone(1,0); assert.equal(full.x,1);
  const diagonal=G.deadZone(1,1); assert.ok(Math.hypot(diagonal.x,diagonal.y)<=1+1e-12);
});

test('raw Sony layout maps cross, right stick on axes 2/5 and hat D-pad',()=>{
  const pad={id:'054c-05c4-Wireless Controller',mapping:'',axes:[0,0,-1,0,0,1,0,0,0,-1],buttons:buttons(14,[1])};
  const s=G.read(pad);
  assert.equal(s.buttons.cross,true); assert.equal(s.buttons.square,false);
  assert.ok(s.look.x<0); assert.equal(s.look.y,-s.look.x); assert.equal(s.buttons.up,true);
  pad.axes[9]=1.2857; assert.equal(G.read(pad).buttons.up,false);
});

test('pressedEdges reports only new presses and pickGamepad skips empty slots',()=>{
  const a={buttons:{cross:true,circle:false}},b={buttons:{cross:true,circle:true}};
  assert.deepEqual(G.pressedEdges(null,a),['cross']);
  assert.deepEqual(G.pressedEdges(a,b),['circle']);
  const raw={id:'x',mapping:'',connected:true},std={id:'y',mapping:'standard',connected:true};
  assert.equal(G.pickGamepad([null,raw,std]),std);
  assert.equal(G.pickGamepad([null,null]),null);
});
