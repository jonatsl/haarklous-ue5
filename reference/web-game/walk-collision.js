/* Pure first-person collision geometry. Distances are metres; y is vertical.
 * createWorld({floors:[{points:[{x,z}],ceilingHeight,y=0,slope:{x,z,origin:{x,z}}}],
 *              obstacles:[{points:[{x,z}],minY,maxY}],radius=.16,clearance=.08})
 *   A floor surface is flat at y, or a ramp (stairs): y + slope.x*(x-origin.x) + slope.z*(z-origin.z).
 *   Or a terrain: height(point) -> y. ceilingHeight is measured from that surface. Obstacle minY/maxY are absolute heights.
 * Every query takes the person's feet height y (default 0), so stacked storeys do not interfere.
 * supportAt(world, point, y) -> {floor, y} the walkable surface under the feet, or null
 * canStand(world, point, eyeHeight, y=0) -> boolean
 * move(world, point, delta, eyeHeight, y=0) -> {x,z,y} (swept/substepped, with sliding; y follows ramps)
 * findSafeSpawn(world, requested, eyeHeight, {maxDistance=20,step=.08,y=0}={}) -> {x,z,y}|null
 * ceilingAt(world, point, y=0) -> lowest clearance above the feet touched by the person's footprint
 * clampEyeHeight(world, point, requested, minimum=.30, y=0) -> safe height|null
 * Worlds snapshot geometry; recreate after walls or room heights change.
 */
(function (root, factory) {
  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.WalkCollision = api;
})(typeof window === 'undefined' ? globalThis : window, function () {
  'use strict';
  const EPS = 1e-8;
  // Largest height change followed in one substep (a stair ramp rises ~3 cm per 4 cm substep),
  // and the tolerance for treating two touching floors as one continuous walking surface.
  const STEP = .36, JOIN = .05, CELL = 16, EMPTY = {obstacles:[],boundary:[]};
  const near = (world,p) => world.cells ? (world.cells.get(Math.floor(p.x/CELL)+','+Math.floor(p.z/CELL)) || EMPTY) : world;
  // Moving solids (door leaves, vehicles, people) live in world.dynamic: set it any time without rebuilding.
  const prepared = list => list.map(o=>o.bounds?o:{...o,bounds:bounds(o.points),minY:o.minY ?? -Infinity,maxY:o.maxY ?? Infinity});
  const solidsNear = (world,p) => world.dynamic && world.dynamic.length ? near(world,p).obstacles.concat(world.dynamicPrepared||(world.dynamicPrepared=prepared(world.dynamic))) : near(world,p).obstacles;
  function setDynamic(world,list){world.dynamic=list;world.dynamicPrepared=prepared(list);}
  const subtract = (a, b) => ({x:a.x-b.x, z:a.z-b.z});
  const dot = (a, b) => a.x*b.x+a.z*b.z;
  const cross = (a, b) => a.x*b.z-a.z*b.x;
  const length = v => Math.hypot(v.x,v.z);
  const lerp = (a,b,t) => ({x:a.x+(b.x-a.x)*t,z:a.z+(b.z-a.z)*t});
  const edges = points => points.map((a,i) => [a,points[(i+1)%points.length]]);
  const bounds = points => ({minX:Math.min(...points.map(p=>p.x)),maxX:Math.max(...points.map(p=>p.x)),minZ:Math.min(...points.map(p=>p.z)),maxZ:Math.max(...points.map(p=>p.z))});
  const nearBounds = (p,b,r=0) => p.x>=b.minX-r && p.x<=b.maxX+r && p.z>=b.minZ-r && p.z<=b.maxZ+r;
  function closestOnSegment(p,a,b) {
    const d=subtract(b,a), squared=dot(d,d);
    return lerp(a,b,squared ? Math.max(0,Math.min(1,dot(subtract(p,a),d)/squared)) : 0);
  }
  const distanceToSegment = (p,a,b) => length(subtract(p,closestOnSegment(p,a,b)));
  function inside(p,points) {
    let result=false;
    for (const [a,b] of edges(points)) {
      if (distanceToSegment(p,a,b)<EPS) return true;
      if ((a.z>p.z)!==(b.z>p.z) && p.x<(b.x-a.x)*(p.z-a.z)/(b.z-a.z)+a.x) result=!result;
    }
    return result;
  }
  function distanceToPolygon(p,polygon) {
    return inside(p,polygon) ? 0 : Math.min(...edges(polygon).map(([a,b])=>distanceToSegment(p,a,b)));
  }
  function surfaceFunction(f) {
    if (typeof f.height==='function') return f.height;
    const base=f.y ?? 0, s=f.slope;
    if (!s || (!s.x && !s.z)) return () => base;
    const o=s.origin || {x:0,z:0};
    return p => base+(s.x||0)*(p.x-o.x)+(s.z||0)*(p.z-o.z);
  }
  // Split every floor edge at other edges and keep the exposed pieces. A piece is open
  // when the floor just across it continues at the same height (touching rooms, a stair
  // meeting its landing); floors stacked on other storeys do not hide each other's edges.
  function exposedBoundary(floors) {
    const segments=floors.flatMap(f=>edges(f.points)).map(([c,e])=>[c,e,Math.min(c.x,e.x),Math.max(c.x,e.x),Math.min(c.z,e.z),Math.max(c.z,e.z)]);
    const covered=(p,height,except)=>floors.some(f=>f!==except && nearBounds(p,f.bounds,EPS) && inside(p,f.points) && Math.abs(f.surface(p)-height)<JOIN);
    const result=[];
    for (const floor of floors) for (const [a,b] of edges(floor.points)) {
      const d=subtract(b,a), dd=dot(d,d), span=length(d);
      if (span<EPS) continue;
      const cuts=[0,1],ex0=Math.min(a.x,b.x)-1e-6,ex1=Math.max(a.x,b.x)+1e-6,ez0=Math.min(a.z,b.z)-1e-6,ez1=Math.max(a.z,b.z)+1e-6;
      for (const [c,e,cx0,cx1,cz0,cz1] of segments) {
        if (cx1<ex0 || cx0>ex1 || cz1<ez0 || cz0>ez1) continue;
        const v=subtract(e,c), ac=subtract(c,a), denominator=cross(d,v);
        if (Math.abs(denominator)>EPS) {
          const t=cross(ac,v)/denominator, u=cross(ac,d)/denominator;
          if (t>EPS && t<1-EPS && u>=-EPS && u<=1+EPS) cuts.push(t);
        } else if (Math.abs(cross(ac,d))<EPS) {
          for (const p of [c,e]) {
            const t=dot(subtract(p,a),d)/dd;
            if (t>EPS && t<1-EPS) cuts.push(t);
          }
        }
      }
      cuts.sort((x,y)=>x-y);
      for (let i=1;i<cuts.length;i++) {
        if (cuts[i]-cuts[i-1]<EPS) continue;
        const start=lerp(a,b,cuts[i-1]), end=lerp(a,b,cuts[i]);
        const middle=lerp(start,end,.5), normal={x:-d.z/span*1e-6,z:d.x/span*1e-6}, height=floor.surface(middle);
        const sides=[{x:middle.x+normal.x,z:middle.z+normal.z},{x:middle.x-normal.x,z:middle.z-normal.z}];
        // Exactly one side lies inside this floor; the edge is a barrier unless the other side continues it.
        const outside=inside(sides[0],floor.points)&&!inside(sides[1],floor.points)?sides[1]:inside(sides[1],floor.points)&&!inside(sides[0],floor.points)?sides[0]:null;
        if (outside && covered(outside,height,floor)) continue;
        if (!outside && floors.some(f=>f!==floor && inside(middle,f.points) && Math.abs(f.surface(middle)-height)<JOIN && inside(sides[0],f.points) && inside(sides[1],f.points))) continue;
        result.push({a:start,b:end,floor});
      }
    }
    return result;
  }
  function createWorld({floors=[],obstacles=[],radius=.16,clearance=.08}={}) {
    const copyPoints=points=>points.map(p=>({x:p.x,z:p.z}));
    const world={
      floors:floors.map(f=>{const copy={points:copyPoints(f.points),bounds:bounds(f.points),ceilingHeight:f.ceilingHeight ?? Infinity,y:f.y ?? 0,slope:f.slope?{x:f.slope.x||0,z:f.slope.z||0,origin:{...(f.slope.origin||{x:0,z:0})}}:null,height:f.height,name:f.name,surfaceKind:f.surfaceKind};copy.surface=surfaceFunction(copy);return copy;}),
      obstacles:obstacles.map(o=>({points:copyPoints(o.points),bounds:bounds(o.points),minY:o.minY ?? -Infinity,maxY:o.maxY ?? Infinity})),
      radius,clearance
    };
    world.boundary=exposedBoundary(world.floors);
    world.dynamic=[];
    // Spatial hash (CELL m) so each query only tests nearby obstacles and boundary pieces.
    world.cells=new Map();
    const pad=world.radius+.8,addTo=(item,b,kind)=>{for(let i=Math.floor((b.minX-pad)/CELL);i<=Math.floor((b.maxX+pad)/CELL);i++)for(let j=Math.floor((b.minZ-pad)/CELL);j<=Math.floor((b.maxZ+pad)/CELL);j++){const key=i+','+j;let c=world.cells.get(key);if(!c){c={obstacles:[],boundary:[]};world.cells.set(key,c);}c[kind].push(item);}};
    world.obstacles.forEach(o=>addTo(o,o.bounds,'obstacles'));
    world.boundary.forEach(piece=>addTo(piece,bounds([piece.a,piece.b]),'boundary'));
    return world;
  }
  function supportAt(world,p,y=0) {
    let best=null,gap=-Infinity;
    for (const f of world.floors) {
      if (!nearBounds(p,f.bounds,EPS) || !inside(p,f.points)) continue;
      // Highest walkable surface within a step: walking into a stair or ramp climbs onto it.
      const height=f.surface(p);
      if (Math.abs(height-y)<=STEP && height>gap) {best={floor:f,y:height};gap=height;}
    }
    return best;
  }
  function ceilingAt(world,p,y=0) {
    let lowest=Infinity;
    for (const f of world.floors) {
      if (!nearBounds(p,f.bounds,world.radius+EPS) || distanceToPolygon(p,f.points)>world.radius+EPS) continue;
      const height=f.surface(closestInside(p,f));
      if (Math.abs(height-y)>STEP*1.7) continue;
      lowest=Math.min(lowest,height+f.ceilingHeight-y);
    }
    return lowest;
  }
  function closestInside(p,f) {
    if (inside(p,f.points)) return p;
    let best=p,distance=Infinity;
    for (const [a,b] of edges(f.points)) {const q=closestOnSegment(p,a,b),d=length(subtract(p,q));if (d<distance) {distance=d;best=q;}}
    return best;
  }
  const overhead = (o,y,eye) => o.maxY>y+.04 && o.minY<y+eye+.10;
  function clampEyeHeight(world,p,requested,minimum=.30,y=0) {
    let maximum=ceilingAt(world,p,y)-Math.max(.10,world.clearance);
    for (const obstacle of solidsNear(world,p)) {
      if (obstacle.minY>y+.04 && nearBounds(p,obstacle.bounds,world.radius) && distanceToPolygon(p,obstacle.points)<world.radius-EPS) maximum=Math.min(maximum,obstacle.minY-y-.10);
    }
    return maximum<minimum ? null : Math.min(maximum,Math.max(minimum,requested));
  }
  const barrierActive = (piece,p,y) => Math.abs(piece.floor.surface(closestOnSegment(p,piece.a,piece.b))-y)<STEP*1.7;
  function canStand(world,p,eyeHeight,y=0) {
    if (!Number.isFinite(p.x) || !Number.isFinite(p.z) || !Number.isFinite(eyeHeight) || eyeHeight<=0) return false;
    const support=supportAt(world,p,y);
    if (!support) return false;
    const feet=support.y;
    if (eyeHeight>ceilingAt(world,p,feet)-Math.max(.10,world.clearance)+EPS) return false;
    const cell=near(world,p);
    if (cell.boundary.some(piece=>distanceToSegment(p,piece.a,piece.b)<world.radius-EPS && barrierActive(piece,p,feet))) return false;
    return !solidsNear(world,p).some(o=>overhead(o,feet,eyeHeight) && nearBounds(p,o.bounds,world.radius) && distanceToPolygon(p,o.points)<world.radius-EPS);
  }
  // Flying objects: does any solid (building, tree, wall, vehicle) come within the world's radius of p between heights y0 and y1?
  function hitsSolid(world,p,y0,y1) {
    if (!Number.isFinite(p.x) || !Number.isFinite(p.z)) return true;
    return solidsNear(world,p).some(o=>o.maxY>y0 && o.minY<y1 && nearBounds(p,o.bounds,world.radius) && (inside(p,o.points) || distanceToPolygon(p,o.points)<world.radius-EPS));
  }
  function move(world,position,delta,eyeHeight,y=0) {
    const start=supportAt(world,position,y);
    if (!start || !canStand(world,position,eyeHeight,y)) return {x:position.x,z:position.z,y};
    let p={x:position.x,z:position.z},feet=start.y;
    const distance=length(delta);
    if (!Number.isFinite(distance)) return {...p,y:feet};
    // Small enough for thin obstacles: circle dilation makes each solid at least
    // 2*radius wide, so an entire collision cannot be skipped in one substep.
    const count=Math.max(1,Math.ceil(distance/Math.min(.04,world.radius*.25)));
    const step={x:delta.x/count,z:delta.z/count};
    const activeEdges=()=>near(world,p).boundary.filter(piece=>barrierActive(piece,p,feet)).map(piece=>[piece.a,piece.b]).concat(solidsNear(world,p).filter(o=>overhead(o,feet,eyeHeight)).flatMap(o=>edges(o.points)));
    const advance=q=>{p=q;feet=supportAt(world,q,feet).y;};
    for (let i=0;i<count;i++) {
      const next={x:p.x+step.x,z:p.z+step.z};
      if (canStand(world,next,eyeHeight,feet)) {advance(next);continue;}
      // Approach contact without leaving the footprint embedded in the wall.
      let low=0,high=1;
      for (let j=0;j<12;j++) {
        const mid=(low+high)/2;
        if (canStand(world,lerp(p,next,mid),eyeHeight,feet)) low=mid; else high=mid;
      }
      const contact=lerp(p,next,low);
      const rest={x:step.x*(1-low),z:step.z*(1-low)};
      const candidates=[{x:rest.x,z:0},{x:0,z:rest.z}];
      for (const [a,b] of activeEdges()) {
        if (distanceToSegment(contact,a,b)>world.radius+length(rest)+.001) continue;
        const edge=subtract(b,a), squared=dot(edge,edge);
        if (squared<EPS) continue;
        const t=dot(rest,edge)/squared;
        candidates.push({x:edge.x*t,z:edge.z*t});
      }
      let best=contact,score=0;
      for (const candidate of candidates) {
        const q={x:contact.x+candidate.x,z:contact.z+candidate.z};
        const progress=dot(candidate,rest);
        if (progress>score && canStand(world,q,eyeHeight,feet)) {best=q;score=progress;}
      }
      advance(best);
    }
    return {...p,y:feet};
  }
  function findSafeSpawn(world,requested,eyeHeight,{maxDistance=20,step=.08,y=0}={}) {
    const ok=p=>canStand(world,p,eyeHeight,y);
    const result=p=>({x:p.x,z:p.z,y:supportAt(world,p,y).y});
    if (!world.floors.some(f=>eyeHeight<=f.ceilingHeight-Math.max(.10,world.clearance)+EPS)) return null;
    if (ok(requested)) return result(requested);
    // Expanding radial search returns a nearby safe point; angular spacing is
    // bounded by step so even a narrow doorway remains discoverable.
    for (let radius=step;radius<=maxDistance;radius+=step) {
      const count=Math.max(12,Math.ceil(2*Math.PI*radius/step));
      for (let i=0;i<count;i++) {
        const angle=2*Math.PI*i/count;
        const p={x:requested.x+radius*Math.cos(angle),z:requested.z+radius*Math.sin(angle)};
        if (ok(p)) return result(p);
      }
    }
    return null;
  }
  return {createWorld,supportAt,canStand,hitsSolid,move,findSafeSpawn,ceilingAt,clampEyeHeight,setDynamic,STEP};
});
