const test = require('node:test');
const assert = require('node:assert/strict');
const C = require('../walk-collision.js');
const rect = (x,z,w,d) => [{x,z},{x:x+w,z},{x:x+w,z:z+d},{x,z:z+d}];
const floor = (x,z,w,d,ceilingHeight=2.4) => ({points:rect(x,z,w,d),ceilingHeight});
const obstacle = (x,z,w,d,minY=0,maxY=2.4) => ({points:rect(x,z,w,d),minY,maxY});
const close = (a,b,tolerance=.001) => assert.ok(Math.abs(a-b)<tolerance,`${a} not close to ${b}`);

test('solid thin walls stop large steps and permit sliding',()=>{
  const world=C.createWorld({floors:[floor(0,0,5,5)],obstacles:[obstacle(2,0,.02,5)]});
  const straight=C.move(world,{x:1,z:1},{x:3,z:0},1.6);
  close(straight.x,1.84);
  const diagonal=C.move(world,{x:1,z:1},{x:3,z:2},1.6);
  close(diagonal.x,1.84); close(diagonal.z,3);
  assert.ok(C.canStand(world,diagonal,1.6));
});

test('door opening is passable but leaf/frame and window are hard',()=>{
  const world=C.createWorld({floors:[floor(0,0,5,5)],obstacles:[
    obstacle(2,0,.1,1), obstacle(2,2,.1,3), obstacle(2,1,.1,1,2.04),
    obstacle(2.1,1.8,.7,.035), // open leaf projecting into the room
  ]});
  close(C.move(world,{x:1,z:1.5},{x:2,z:0},1.6).x,3);
  assert.ok(C.move(world,{x:1,z:1.95},{x:2,z:0},1.6).x<2);
  assert.ok(C.move(world,{x:2.5,z:1.3},{x:0,z:1},1.6).z<1.65);
  const windowWorld=C.createWorld({floors:[floor(0,0,5,5)],obstacles:[obstacle(2,0,.02,5,.9,2.1)]});
  assert.ok(C.move(windowWorld,{x:1,z:1},{x:3,z:0},1.6).x<2);
});

test('touching and overlapping room floors have no false seam barrier',()=>{
  for (const offset of [2,1.8]) {
    const world=C.createWorld({floors:[floor(0,0,2,2),floor(offset,0,2,2)]});
    close(C.move(world,{x:1,z:1},{x:2,z:0},1.6).x,3);
    assert.ok(!C.canStand(world,{x:.1,z:1},1.6));
  }
});

test('concave exterior corners and tiny floor holes cannot be crossed',()=>{
  const world=C.createWorld({floors:[floor(0,0,3,1),floor(0,1,1,2)]});
  assert.equal(C.canStand(world,{x:.95,z:.95},1.6),false);
  const p=C.move(world,{x:.6,z:.6},{x:2,z:2},1.6);
  assert.ok(C.canStand(world,p,1.6));
  const hole=C.createWorld({floors:[floor(0,0,3,1),floor(0,1.05,3,2),floor(0,1,1,.05),floor(1.05,1,1.95,.05)]});
  assert.equal(C.canStand(hole,{x:.95,z:1},1.6),false);
});

test('ceiling steps constrain movement, height changes and overhead lintels',()=>{
  const world=C.createWorld({floors:[floor(0,0,2,3,2.4),floor(2,0,2,3,2.1)],obstacles:[obstacle(.5,.5,1,1,2.04,2.4)]});
  assert.equal(C.clampEyeHeight(world,{x:3,z:1},3),2);
  close(C.clampEyeHeight(world,{x:1,z:1},2.3),1.94);
  assert.ok(C.move(world,{x:1.7,z:2},{x:1,z:0},2.2).x<=1.841);
  close(C.move(world,{x:1.7,z:2},{x:1,z:0},1.6).x,2.7);
  assert.equal(C.canStand(world,{x:1,z:1},2),false);
});

test('rotated obstacles allow tangent sliding without penetration',()=>{
  const polygon=[{x:2,z:0},{x:4,z:2},{x:3.9,z:2.1},{x:1.9,z:.1}];
  const world=C.createWorld({floors:[floor(0,0,5,5)],obstacles:[{points:polygon,minY:0,maxY:2.4}]});
  const start={x:2,z:1};
  const p=C.move(world,start,{x:2,z:0},1.6);
  assert.ok(C.canStand(world,p,1.6));
  assert.ok(p.x>2.7 && p.z>1.2,JSON.stringify(p));
});

test('spawn relocates out of walls and rejects impossible rooms',()=>{
  const world=C.createWorld({floors:[floor(0,0,3,3)],obstacles:[obstacle(1,0,.1,3)]});
  const safe=C.findSafeSpawn(world,{x:1.05,z:1},1.6);
  assert.ok(safe && C.canStand(world,safe,1.6));
  assert.ok(Math.hypot(safe.x-1.05,safe.z-1)<.4);
  assert.equal(C.findSafeSpawn(world,{x:1,z:1},3,{maxDistance:.5}),null);
});

test('stacked storeys do not block each other and ramps carry the walker up',()=>{
  const lower={points:rect(0,0,4,2),y:0,ceilingHeight:2.5},upper={points:rect(0,0,4,2),y:2.85,ceilingHeight:2.5};
  const ramp={points:rect(0,2,4,2),y:0,slope:{x:0,z:2.85/2,origin:{x:0,z:2}},ceilingHeight:2.3},top={points:rect(0,4,4,2),y:2.85,ceilingHeight:2.5};
  const world=C.createWorld({floors:[lower,upper,ramp,top],obstacles:[obstacle(1,.5,1,1,2.85,5.2)]});
  // The obstacle stands on the upper storey only: free below, blocking above.
  assert.ok(C.canStand(world,{x:1.5,z:1},1.6,0));
  assert.equal(C.canStand(world,{x:1.5,z:1},1.6,2.85),false);
  const p=C.move(world,{x:3,z:1},{x:0,z:4},1.6,0);
  close(p.z,5);close(p.y,2.85);
  // Walking off the end of a landing into the stair void is not possible.
  const edge=C.createWorld({floors:[upper]});
  close(C.move(edge,{x:2,z:1},{x:0,z:3},1.6,2.85).z,2-.16,.01);
});

test('obstacle heights are absolute, so an overhead lintel clamps relative eye height',()=>{
  const world=C.createWorld({floors:[{points:rect(0,0,3,3),y:-2.85,ceilingHeight:2.6}],obstacles:[obstacle(1,1,1,1,-2.85+2.04,-.25)]});
  close(C.clampEyeHeight(world,{x:1.5,z:1.5},2.3,.3,-2.85),1.94);
  assert.equal(C.supportAt(world,{x:1.5,z:1.5},-2.85).y,-2.85);
});

test('hitsSolid finds solids around flying objects without needing a floor',()=>{
  const world=C.createWorld({floors:[],obstacles:[obstacle(10,10,10,10,-10,8)],radius:3});
  assert.equal(C.hitsSolid(world,{x:15,z:15},0,2),true);          // inside the building
  assert.equal(C.hitsSolid(world,{x:8,z:15},0,2),true);           // 2 m from the wall, radius 3
  assert.equal(C.hitsSolid(world,{x:5,z:15},0,2),false);          // 5 m away
  assert.equal(C.hitsSolid(world,{x:15,z:15},9,12),false);        // above the roof
  C.setDynamic(world,[{points:rect(0,0,2,2),minY:0,maxY:2}]);
  assert.equal(C.hitsSolid(world,{x:1,z:1},1,3),true);            // dynamic obstacles count too
});
