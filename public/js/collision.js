// Collision world for the golf ball, built on Jolt Physics (WebAssembly).
//
// Jolt answers "what does the ball hit along this step?": the terrain (a height field matching
// the rendered ground triangle for triangle), tree trunks, buildings and the flagstick. How the
// ball then bounces, rolls and flies stays in physics.js, which is calibrated against launch
// monitor data; Jolt only supplies the contact point, the surface normal and what was hit.
//
// Jolt works in the renderer's frame (x mirrored) so its height-field triangles split each grid
// cell along the same diagonal as the terrain mesh; this module converts to and from course space.
import { BALL_RADIUS } from './physics.js';

let joltLoading = null;
export function loadJolt() {
  joltLoading ||= import('jolt-physics').then((m) => m.default());
  return joltLoading;
}

const TERRAIN = 0, OBSTACLE = 1, LAYERS = 2;
const POLE_RADIUS = 0.0125, POLE_HEIGHT = 2.3;

export class CollisionWorld {
  // course: from course.js; trees: [{ x, z, trunkR, trunkTop }]; buildings: [{ h, pts: [[x, z]], kind? }]
  // (kind: how the ball bounces off it, physics.js OBSTACLES; 'building' unless given)
  constructor(Jolt, course, { trees = [], buildings = [] } = {}) {
    this.J = Jolt;
    this.course = course;
    // Everything is static, so no pairs of layers ever need to collide with each other
    const pairs = new Jolt.ObjectLayerPairFilterTable(LAYERS);
    const bp = new Jolt.BroadPhaseLayerInterfaceTable(LAYERS, LAYERS);
    this.bpLayers = [new Jolt.BroadPhaseLayer(TERRAIN), new Jolt.BroadPhaseLayer(OBSTACLE)];
    bp.MapObjectToBroadPhaseLayer(TERRAIN, this.bpLayers[TERRAIN]);
    bp.MapObjectToBroadPhaseLayer(OBSTACLE, this.bpLayers[OBSTACLE]);
    const settings = new Jolt.JoltSettings();
    settings.mMaxBodies = Math.max(1024, trees.length + buildings.length + 64);
    settings.mObjectLayerPairFilter = pairs;
    settings.mBroadPhaseLayerInterface = bp;
    settings.mObjectVsBroadPhaseLayerFilter = new Jolt.ObjectVsBroadPhaseLayerFilterTable(bp, LAYERS, pairs, LAYERS);
    this.jolt = new Jolt.JoltInterface(settings);
    Jolt.destroy(settings);
    this.system = this.jolt.GetPhysicsSystem();
    this.bodies = this.system.GetBodyInterface();
    this.things = []; // body user data -> { kind, ref }

    this.addTerrain(course);
    for (const t of trees) this.addTrunk(t);
    for (const b of buildings) this.addBuilding(b);
    this.poleId = this.addBody(this.cylinder(POLE_HEIGHT / 2, POLE_RADIUS + BALL_RADIUS), [0, -1000, 0], OBSTACLE, { kind: 'pole' });
    this.system.OptimizeBroadPhase();

    // Reused query objects
    this.query = this.system.GetNarrowPhaseQuery();
    this.lock = this.system.GetBodyLockInterfaceNoLock();
    this.raySettings = new Jolt.RayCastSettings();
    this.collector = new Jolt.CastRayClosestHitCollisionCollector();
    this.filters = [TERRAIN, OBSTACLE].map((l) => ({ bp: new Jolt.SpecifiedBroadPhaseLayerFilter(this.bpLayers[l]), obj: new Jolt.SpecifiedObjectLayerFilter(l) }));
    this.bodyFilter = new Jolt.BodyFilter();
    this.shapeFilter = new Jolt.ShapeFilter();
    this.origin = new Jolt.RVec3(0, 0, 0);
    this.dir = new Jolt.Vec3(0, 0, 0);
    this.ray = new Jolt.RRayCast(this.origin, this.dir);
    this.hitPos = new Jolt.RVec3(0, 0, 0);
  }

  addBody(shape, [x, y, z], layer, thing) {
    const J = this.J;
    const s = new J.BodyCreationSettings(shape, new J.RVec3(-x, y, z), new J.Quat(0, 0, 0, 1), J.EMotionType_Static, layer);
    s.mUserData = this.things.length;
    this.things.push(thing);
    const body = this.bodies.CreateBody(s);
    J.destroy(s);
    this.bodies.AddBody(body.GetID(), J.EActivation_DontActivate);
    return body.GetID();
  }

  cylinder(halfHeight, radius) {
    const s = new this.J.CylinderShapeSettings(halfHeight, radius, Math.min(0.02, radius * 0.5));
    return s.Create().Get();
  }

  // The ground as a height field: one sample per terrain vertex, padded to a square
  addTerrain(course) {
    const J = this.J;
    const { nx, nz, cell, heights } = course.grid;
    const { minX, minZ } = course.bounds;
    const maxX = minX + (nx - 1) * cell;
    const block = 2, n = Math.ceil(Math.max(nx, nz) / block) * block;
    const hs = new J.HeightFieldShapeSettings();
    hs.mOffset = new J.Vec3(-maxX, 0, minZ);
    hs.mScale = new J.Vec3(cell, 1, cell);
    hs.mSampleCount = n;
    hs.mBlockSize = block;
    hs.mBitsPerSample = 8;
    hs.mHeightSamples.resize(n * n);
    const none = J.HeightFieldShapeConstantValues.prototype.cNoCollisionValue;
    const samples = new Float32Array(J.HEAPF32.buffer, J.getPointer(hs.mHeightSamples.data()), n * n);
    // Sample column X runs along -x (the renderer's +x), so X = nx - 1 - i
    for (let Y = 0; Y < n; Y++) for (let X = 0; X < n; X++) {
      samples[Y * n + X] = X < nx && Y < nz ? heights[Y * nx + (nx - 1 - X)] : none;
    }
    const shape = hs.Create().Get();
    J.destroy(hs);
    // The height field's own origin is its offset; the body sits at the world origin
    const s = new J.BodyCreationSettings(shape, new J.RVec3(0, 0, 0), new J.Quat(0, 0, 0, 1), J.EMotionType_Static, TERRAIN);
    s.mUserData = this.things.length;
    this.things.push({ kind: 'terrain' });
    const body = this.bodies.CreateBody(s);
    J.destroy(s);
    this.bodies.AddBody(body.GetID(), J.EActivation_DontActivate);
  }

  // Trunks reach a metre into the ground so a rolling ball meets them too.
  // Every obstacle is grown by the ball's radius, so a ray along the ball's centre acts as a sphere.
  addTrunk(t) {
    const ground = this.course.heightAt(t.x, t.z);
    if (ground === null) return;
    const bottom = ground - 1, h = t.trunkTop - bottom;
    if (h <= 0) return;
    this.addBody(this.cylinder(h / 2, t.trunkR + BALL_RADIUS), [t.x, bottom + h / 2, t.z], OBSTACLE, { kind: 'trunk', ref: t });
  }

  addBuilding(b) {
    const J = this.J;
    if (!b.pts || b.pts.length < 3) return;
    let ground = Infinity;
    for (const [x, z] of b.pts) { const y = this.course.heightAt(x, z); if (y !== null) ground = Math.min(ground, y); }
    if (!Number.isFinite(ground)) return;
    // Convex outline of the footprint (most buildings around a course are simple shapes)
    const hull = new J.ConvexHullShapeSettings();
    const pad = BALL_RADIUS;
    for (const [x, z] of b.pts) for (const y of [ground - 0.5 - pad, ground - 0.5 + b.h + pad]) hull.mPoints.push_back(new J.Vec3(-x, y, z));
    hull.mMaxConvexRadius = 0.02;
    const res = hull.Create();
    J.destroy(hull);
    if (!res.IsValid()) return;
    this.addBody(res.Get(), [0, 0, 0], OBSTACLE, { kind: b.kind || 'building', ref: b });
  }

  // Move the flagstick to this hole's pin (or out of the way when there is none)
  setPin(pin) {
    const J = this.J;
    const y = pin ? pin.y + POLE_HEIGHT / 2 - 0.1 : -1000;
    if (this.pinKey === `${pin?.x},${y},${pin?.z}`) return;
    this.pinKey = `${pin?.x},${y},${pin?.z}`;
    const p = new J.RVec3(pin ? -pin.x : 0, y, pin ? pin.z : 0);
    this.bodies.SetPosition(this.poleId, p, J.EActivation_DontActivate);
    J.destroy(p);
  }

  // First thing the ball's centre meets going from a to b (course space, [x, y, z] arrays).
  // what: 'terrain' | 'obstacle' | 'all'. Returns { kind, ref, t (0..1), point, normal } or null.
  cast(a, b, what = 'all') {
    let best = null;
    if (what !== 'obstacle') best = this.castLayer(a, b, TERRAIN);
    if (what !== 'terrain') {
      const o = this.castLayer(a, b, OBSTACLE);
      if (o && (!best || o.t < best.t)) best = o;
    }
    return best;
  }

  castLayer(a, b, layer) {
    const dx = b[0] - a[0], dy = b[1] - a[1], dz = b[2] - a[2];
    if (dx === 0 && dy === 0 && dz === 0) return null;
    this.origin.Set(-a[0], a[1], a[2]);
    this.dir.Set(-dx, dy, dz);
    this.ray.mOrigin = this.origin;
    this.ray.mDirection = this.dir;
    const c = this.collector;
    c.Reset();
    const f = this.filters[layer];
    this.query.CastRay(this.ray, this.raySettings, c, f.bp, f.obj, this.bodyFilter, this.shapeFilter);
    if (!c.HadHit()) return null;
    const hit = c.mHit, t = hit.mFraction;
    const body = this.lock.TryGetBody(hit.mBodyID);
    const px = a[0] + dx * t, py = a[1] + dy * t, pz = a[2] + dz * t;
    this.hitPos.Set(-px, py, pz);
    const n = body.GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, this.hitPos);
    const thing = this.things[body.GetUserData()];
    return { kind: thing.kind, ref: thing.ref, t, point: [px, py, pz], normal: [-n.GetX(), n.GetY(), n.GetZ()] };
  }

  // Height of the collision ground under (x, z), or null (used by tests)
  groundAt(x, z) {
    const hit = this.castLayer([x, 5000, z], [x, -5000, z], TERRAIN);
    return hit ? hit.point[1] : null;
  }

  dispose() {
    const J = this.J;
    for (const o of [this.raySettings, this.collector, this.bodyFilter, this.shapeFilter, this.ray, this.origin, this.dir, this.hitPos, ...this.filters.flatMap((f) => [f.bp, f.obj])]) J.destroy(o);
    J.destroy(this.jolt);
    this.jolt = null;
  }
}
