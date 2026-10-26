const test = require('node:test');
const assert = require('node:assert/strict');
const C = require('../case-game.js');
// Free spots anywhere inside a 6 x 10 m apartment rectangle.
const sampler = rng => ({x: rng() * 6, z: rng() * 10, area: 'Room'});

test('seeded generator repeats exactly and differs between seeds',()=>{
  const a=C.createRng(42),b=C.createRng(42),c=C.createRng(43);
  const seq=r=>Array.from({length:5},r);
  assert.deepEqual(seq(a),seq(b)); assert.notDeepEqual(seq(C.createRng(42)),seq(c));
});

test('every mission has 3–5 hostiles split between apartment and stairwell, one hostage in the apartment',()=>{
  const counts={3:0,4:0,5:0};const spots=new Set();
  const zoned=(rng,zone)=>zone==='stair'?{x:rng()*2.6,z:5+rng()*2,y:-2.85*Math.floor(rng()*4),area:'Stair'}:{x:rng()*6,z:rng()*10,y:0,area:'Room'};
  for(let seed=1;seed<=300;seed++){
    const plan=C.planCase(C.createRng(seed),zoned);
    const hostiles=plan.actors.filter(a=>a.role==='hostile'),hostages=plan.actors.filter(a=>a.role==='hostage');
    assert.ok(hostiles.length>=3&&hostiles.length<=5);assert.equal(hostiles.length,plan.hostileCount);
    assert.equal(hostages.length,1);assert.equal(hostages[0].zone,'apartment');
    assert.ok(hostiles.some(a=>a.zone==='stair'),'at least one hostile in the stairwell');
    assert.ok(hostiles.some(a=>a.zone==='apartment'),'at least one hostile in the apartment');
    counts[hostiles.length]++;
    for(const a of plan.actors)spots.add(`${a.x.toFixed(2)},${a.z.toFixed(2)}`);
  }
  assert.ok(counts[3]>50&&counts[4]>50&&counts[5]>50,JSON.stringify(counts));
  assert.ok(spots.size>1000,'positions change from mission to mission');
});

test('planning fails loudly instead of placing actors in invalid spots',()=>{
  assert.throws(()=>C.planCase(C.createRng(1),()=>null,{maxTries:20}),/No free position/);
});

test('hostiles need two body hits or one head hit; a hostage falls to one hit',()=>{
  const body={role:'hostile',health:2,alive:true};C.applyHit(body,false);assert.equal(body.alive,true);C.applyHit(body,false);assert.equal(body.alive,false);
  const head={role:'hostile',health:2,alive:true};C.applyHit(head,true);assert.equal(head.alive,false);
  const civilian={role:'hostage',health:1,alive:true};C.applyHit(civilian,false);assert.equal(civilian.alive,false);
});

test('case succeeds only when all hostiles are down and fails when a hostage is shot',()=>{
  const h1={role:'hostile',alive:true},h2={role:'hostile',alive:true},hostage={role:'hostage',alive:true};
  assert.equal(C.outcome([h1,h2,hostage]),null);
  h1.alive=false;assert.equal(C.outcome([h1,h2,hostage]),null);
  h2.alive=false;assert.equal(C.outcome([h1,h2,hostage]),'success');
  hostage.alive=false;assert.equal(C.outcome([h1,h2,hostage]),'fail');
  assert.equal(C.outcome([{role:'hostile',alive:true},{role:'hostage',alive:false}]),'fail');
});

test('polygon test used for spawn areas handles concave rooms',()=>{
  const L=[{x:0,z:0},{x:4,z:0},{x:4,z:1},{x:1,z:1},{x:1,z:4},{x:0,z:4}];
  assert.equal(C.insidePolygon({x:.5,z:3},L),true); assert.equal(C.insidePolygon({x:3,z:3},L),false);
});

test('hostile hit chance falls with distance and player movement, within fixed bounds',()=>{
  const close=C.hostileHitChance(1.5,0),far=C.hostileHitChance(8,0),running=C.hostileHitChance(1.5,3.2);
  assert.ok(close>.7&&close<=.85,`close ${close}`); assert.ok(far<close); assert.ok(running<close-.25);
  assert.equal(C.hostileHitChance(40,4),.12); assert.ok(C.hostileHitChance(0,0)<=.85);
});

test('four hostile hits put the player down and fail the case',()=>{
  const player={health:C.PLAYER_HEALTH,alive:true},hostile={role:'hostile',alive:true};
  for(let i=0;i<3;i++)C.damagePlayer(player);
  assert.equal(player.health,C.PLAYER_HEALTH-3*C.HOSTILE_DAMAGE); assert.equal(player.alive,true);
  assert.equal(C.outcome([hostile],player),null);
  C.damagePlayer(player); assert.equal(player.health,0); assert.equal(player.alive,false);
  assert.equal(C.outcome([hostile],player),'fail');
  C.damagePlayer(player); assert.equal(player.health,0);
});

test('crouching and lying prone make the player harder to hit',()=>{
  const stand=C.hostileHitChance(3,0,'stand'),crouch=C.hostileHitChance(3,0,'crouch'),prone=C.hostileHitChance(3,0,'prone');
  assert.ok(stand>crouch&&crouch>prone,`${stand} ${crouch} ${prone}`);
  assert.ok(prone>=.08);
});
