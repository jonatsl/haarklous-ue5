const test = require('node:test');
const assert = require('node:assert/strict');
const C = require('../walk-collision.js');
require('../stairwell.js');
const S = globalThis.Stairwell;

test('street, stair flights and every landing connect up to the 4th floor',()=>{
  // The open-world terrain is a floor under and around the tower; the tower walls are solid obstacles.
  const ground={points:[{x:-30,z:-10},{x:30,z:-10},{x:30,z:40},{x:-30,z:40}],y:S.GROUND};
  const world=C.createWorld({floors:[...S.floors(true),ground],obstacles:S.obstacles(true),radius:.16});
  let p={x:.75,z:16,y:S.GROUND};
  const walk=(x,z)=>{for(let i=0;i<500;i++){const dx=x-p.x,dz=z-p.z,d=Math.hypot(dx,dz);if(d<.02)break;const k=Math.min(.05,d)/d;p=C.move(world,p,{x:dx*k,z:dz*k},1.6,p.y);}return p;};
  walk(.75,10.4);assert.ok(Math.abs(p.y-S.GROUND)<1e-6,'through the street door at street level');
  walk(-.6,9.6);walk(-.6,6.6);assert.ok(Math.abs(p.y+8.55)<1e-6,`1st floor ${p.y}`);
  for(let n=1;n<=3;n++){walk(.8,6.9);walk(.8,9.7);walk(-.6,9.7);walk(-.6,6.6);assert.ok(Math.abs(p.y-(n-3)*2.85)<1e-6,`floor ${n+1}: ${p.y}`);}
  assert.ok(Math.abs(p.y)<1e-6,'4th-floor landing is y = 0');
  // Nobody walks under the stairs from the street-level hall.
  const under=C.move(world,{x:.75,z:10.4,y:S.GROUND},{x:0,z:-3},1.6,S.GROUND);assert.ok(under.z>9.0,`stopped before the space under the flights: ${under.z}`);
  // Up the stairs is the only way: no stepping across the stair well between flights.
  const across=C.move(world,{x:.8,z:8.2,y:-8.55+(8.2-7.2)*S.STOREY/2/1.89},{x:-1.4,z:0},1.6,-8.55+(8.2-7.2)*S.STOREY/2/1.89);
  assert.ok(across.x>.2,'blocked by the stair well');
});
