// Golf ball flight, bounce and roll on the course: OpenFairway's ball physics (openfairway.js)
// meeting the course's terrain, surfaces, trees, buildings, water and the cup.
//
// Works in course space (Unity-style frame): x/z horizontal, y up, meters.
// A heading of 0 points along +z; "right" of heading h is (cos h, 0, -sin h).

import * as OF from './openfairway.js';

export const BALL_MASS = OF.MASS;           // kg
export const BALL_RADIUS = OF.RADIUS;       // m
const G = OF.G;
export const CUP_RADIUS = 0.054;
const MPH = 0.44704;
const RPM = OF.RAD_PER_RPM;
const DT = OF.DT;

// How each course surface plays (OpenFairway's contact settings: see openfairway.js). Tee, fairway,
// fringe, rough, green and sand are OpenFairway's own presets; the course's other surfaces are
// OpenTeeGolf's, built on them: first cut between fairway and rough, deep rough and heather
// grabbing harder and cushioning the bounce, a cart path that bounces like pavement and runs.
export const SURFACES = {
  green: OF.PRESETS.green,
  fringe: OF.PRESETS.fringe,
  fairway: OF.PRESETS.fairway,
  tee: OF.PRESETS.fairway,
  firstcut: OF.makeSurface(0.56, 0.072, 0.00245, 0.32, 0.74),
  rough: OF.PRESETS.rough,
  deeprough: OF.makeSurface(0.7, 0.18, 0.006, 0.4, 0.6, { restitutionScale: 0.6, impactRetentionScale: 0.6 }),
  native: OF.makeSurface(0.72, 0.2, 0.0065, 0.42, 0.6, { restitutionScale: 0.55, impactRetentionScale: 0.55 }),
  path: OF.makeSurface(0.3, 0.02, 0.0003, 0.25, 0.6, { restitutionScale: 2.0 }),
  sand: OF.PRESETS.sand,
  oob: OF.PRESETS.rough,
};

// Solid objects (Jolt collision world): restitution e (+ up to eVar at random), share of speed
// kept after the scrape (keep) and of spin (spin)
export const OBSTACLES = {
  trunk:    { e: 0.3, eVar: 0.15, keep: 0.6, spin: 0.3 },
  building: { e: 0.45, eVar: 0.1, keep: 0.75, spin: 0.5 },
  pole:     { e: 0.2, eVar: 0.1, keep: 0.5, spin: 0.5 },
  bumper:   { e: 0.62, eVar: 0.06, keep: 0.86, spin: 0.6 }, // putt-putt walls and posts: lively
  blade:    { e: 0.35, eVar: 0.1, keep: 0.7, spin: 0.5 },   // a windmill's sail
};

// How the lie affects the next strike.
export const LIE_EFFECT = {
  tee: { speed: 1, spin: 1 },
  fairway: { speed: 1, spin: 1 },
  fringe: { speed: 0.99, spin: 0.95 },
  green: { speed: 1, spin: 1 },
  firstcut: { speed: 0.97, spin: 0.88 },
  rough: { speed: 0.9, spin: 0.62 },     // grass between club and ball: less spin ("flyers"), shorter
  deeprough: { speed: 0.78, spin: 0.45 }, // under trees / heavy cover
  native: { speed: 0.8, spin: 0.45 },     // heather / tall native grass
  path: { speed: 1, spin: 0.9 },
  sand: { speed: 0.85, spin: 0.8 },
};

// Display names for surfaces
export const SURFACE_NAMES = {
  tee: 'Tee', fairway: 'Fairway', firstcut: 'First cut', fringe: 'Fringe', green: 'Green',
  rough: 'Rough', deeprough: 'Deep rough', native: 'Heather', path: 'Cart path', sand: 'Bunker', water: 'Water', oob: 'Out of bounds',
};

// Ball speed (m/s) that rolls a putt `distanceM` on a level green of this stimp
export const puttSpeed = (distanceM, stimpFt) => OF.puttSpeed(distanceM, stimpFt);

// Air density (kg/m³) from altitude, temperature, humidity and (if known) the actual station
// pressure. Warm, humid, thin air = less drag and lift = the ball flies further.
export function airDensity(altitudeM, tempC = 20, humidity = 0.5, pressureHpa = null) {
  return OF.airDensity(altitudeM, tempC, humidity, pressureHpa ? pressureHpa * 100 : 0);
}

/**
 * Simulate a shot from launch until the ball stops, with OpenFairway's flight, bounce and roll
 * (openfairway.js) on the course's own ground.
 *
 * course: { heightAt(x,z), normalAt(x,z), surfaceAt(x,z) }
 * shot:   { speedMph, vla, hla, backSpin, sideSpin }  (deg / rpm, hla & sideSpin positive = right)
 * opts:   { heading (rad), altitudeM, wind: {mps, fromDeg}, stimp, pin:{x,y,z},
 *           movers: [(from, to, t) -> hit | null] }
 * movers are things that move while the ball does (the putt-putt windmill): each is asked about
 * every step, with the time since the strike, and answers like the collision world does.
 */
export function simulateShot(course, origin, shot, opts) {
  const tempC = opts.tempC ?? 20;
  const back = shot.backSpin || 0, side = shot.sideSpin || 0;
  const launch = OF.resolveLaunch(shot.speedMph, shot.vla, Math.hypot(back, side));
  const env = {
    rho: airDensity(opts.altitudeM ?? 0, tempC, opts.humidity ?? 0.5, opts.pressureHpa ?? null),
    mu: OF.airViscosity(tempC),
    dragScale: launch.dragScale, liftScale: launch.liftScale, launchDeg: launch.launchDeg,
  };
  // Wet ground (rain): softer landings, more grip, slower greens
  const wet = Math.min(1, Math.max(0, opts.wet ?? 0));
  const greenRolling = OF.greenRollingFriction(Math.max(6, (opts.stimp ?? 10) - 1.8 * wet));
  const settings = {};
  const surfaceParams = (surf) => {
    if (settings[surf]) return settings[surf];
    let sp = SURFACES[surf] || SURFACES.rough;
    if (surf === 'green') sp = { ...sp, rollingFriction: greenRolling };
    // (wet, softer turf also takes spin better)
    if (wet) sp = { ...sp, restitutionScale: sp.restitutionScale * (1 - 0.35 * wet), kineticFriction: sp.kineticFriction * (1 + 0.15 * wet), rollingFriction: sp.rollingFriction * (surf === 'green' ? 1 : 1 + 0.6 * wet), grassViscosity: sp.grassViscosity * (1 + 0.8 * wet) };
    return (settings[surf] = sp);
  };
  const heading = opts.heading + (shot.hla * Math.PI) / 180;
  const vla = (shot.vla * Math.PI) / 180;
  const speed = shot.speedMph * MPH;
  const fx = Math.sin(heading), fz = Math.cos(heading);
  const v = [speed * Math.cos(vla) * fx, speed * Math.sin(vla), speed * Math.cos(vla) * fz];
  // Backspin turns about the horizontal axis to the left of the line (forward × up), side spin about
  // up; positive side spin curves right
  const spin = [-back * RPM * fz, side * RPM, back * RPM * fx];
  const tilt = Math.atan2(side, back);
  // Wind "from" direction is a compass heading in course space (0 = from +z).
  const wf = ((opts.wind?.fromDeg ?? 0) * Math.PI) / 180;
  const wm = opts.wind?.mps ?? 0;
  const wind = [-Math.sin(wf) * wm, 0, -Math.cos(wf) * wm];
  const windH = [0, 0, 0];

  const p = [origin.x, origin.y, origin.z];
  const points = [{ t: 0, x: p[0], y: p[1], z: p[2] }];
  let t = 0, apex = p[1], phase = 'air', bounces = 0, firstImpact = true, impactRpm = 0;
  // Putts (and bumps along the green) start rolling immediately: with no measured spin, rolling.
  const startSurf = course.surfaceAt(origin.x, origin.z);
  if ((startSurf === 'green' || startSurf === 'fringe') && shot.vla < 6 && shot.speedMph < 25) {
    phase = 'roll';
    const n = course.normalAt(origin.x, origin.z);
    const vn = v[0] * n[0] + v[1] * n[1] + v[2] * n[2];
    v[0] -= vn * n[0]; v[1] -= vn * n[1]; v[2] -= vn * n[2];
    const s0 = OF.launchSpin(v, spin, n);
    spin[0] = s0[0]; spin[1] = s0[1]; spin[2] = s0[2];
    impactRpm = Math.hypot(...spin) / RPM;
  }
  let carryPoint = null, result = 'rest', waterEntry = null, lastDry = { x: p[0], y: p[1], z: p[2] };
  const pin = opts.pin;
  let sampleAcc = 0;

  // (w: spin rate in rad/s and air: in flight, for drawing the ball turning)
  const push = () => points.push({ t, x: p[0], y: p[1], z: p[2], w: Math.hypot(spin[0], spin[1], spin[2]), air: phase === 'air' });
  const scaleSpin = (k) => { spin[0] *= k; spin[1] *= k; spin[2] *= k; };
  // Keep a rolling ball's velocity along the ground
  const alongGround = () => {
    const n = course.normalAt(p[0], p[2]);
    const vn = v[0] * n[0] + v[1] * n[1] + v[2] * n[2];
    v[0] -= vn * n[0]; v[1] -= vn * n[1]; v[2] -= vn * n[2];
  };
  const cupState = { over: false };
  // Trees (optional): { near(x, z) -> [tree] } from the renderer
  const trees = opts.trees || null;
  // Collision world (optional, Jolt): terrain contact points, trunks, buildings and the flagstick
  const collider = opts.collider || null;
  if (collider) collider.setPin(pin || null);
  const movers = opts.movers?.length ? opts.movers : null;
  const moverHit = () => { for (const m of movers) { const h = m(prev, p, t); if (h) return h; } return null; };
  const prev = [0, 0, 0];
  const treeState = { spinMul: 1, inCrown: new Set(), hits: [] };
  const treeStep = () => {
    if (!trees) return;
    const ev = treeInteract(trees, p, v, DT, treeState, !collider);
    if (ev) treeState.hits.push(ev);
    if (treeState.spinMul !== 1) { scaleSpin(treeState.spinMul); treeState.spinMul = 1; }
  };
  // The ball meets a solid object (trunk, wall, flagstick): bounce off it, losing speed and spin
  const hitObstacle = (hit) => {
    const n = hit.normal, k = OBSTACLES[hit.kind] || OBSTACLES.building;
    const vn = v[0] * n[0] + v[1] * n[1] + v[2] * n[2];
    if (vn < 0) {
      const e = k.e + Math.random() * k.eVar;
      v[0] -= (1 + e) * vn * n[0]; v[1] -= (1 + e) * vn * n[1]; v[2] -= (1 + e) * vn * n[2];
      v[0] *= k.keep; v[1] *= k.keep; v[2] *= k.keep;
      scaleSpin(k.spin);
    }
    p[0] = hit.point[0] + n[0] * 0.005; p[1] = hit.point[1] + n[1] * 0.005; p[2] = hit.point[2] + n[2] * 0.005;
    treeState.hits.push(hit.kind);
  };
  // Animate the ball rolling to the middle of the cup and dropping below the surface
  const dropIntoCup = () => {
    const x0 = p[0], z0 = p[2], y0 = p[1];
    for (let k = 1; k <= 8; k++) {
      t += 0.02;
      const f = k / 8;
      p[0] = x0 + (pin.x - x0) * f; p[2] = z0 + (pin.z - z0) * f;
      p[1] = y0 - 0.03 * f * f;
      push();
    }
  };

  for (let step = 0; step < 120 * 60; step++) {
    if (phase === 'air') {
      // Wind strength depends on how high the ball's centre is above where it was struck
      const wk = OF.windFactor(p[1] - origin.y + BALL_RADIUS);
      windH[0] = wind[0] * wk; windH[2] = wind[2] * wk;
      OF.stepFlight(v, spin, env, windH, DT);
      prev[0] = p[0]; prev[1] = p[1]; prev[2] = p[2];
      p[0] += v[0] * DT; p[1] += v[1] * DT; p[2] += v[2] * DT;
      t += DT;
      // What the ball met along this step (Jolt): solid objects bounce it, the ground ends the flight.
      // `spent` is the share of the step taken before it touched the ground.
      let landed = false, spent = 1;
      if (collider) {
        const hit = collider.cast(prev, p);
        if (hit && hit.kind !== 'terrain') { hitObstacle(hit); push(); continue; }
        if (hit) {
          const n = course.normalAt(hit.point[0], hit.point[2]);
          if (v[0] * n[0] + v[1] * n[1] + v[2] * n[2] < 0) {
            const step = Math.hypot(p[0] - prev[0], p[1] - prev[1], p[2] - prev[2]);
            spent = step > 0 ? Math.min(1, Math.hypot(hit.point[0] - prev[0], hit.point[1] - prev[1], hit.point[2] - prev[2]) / step) : 0;
            p[0] = hit.point[0]; p[1] = hit.point[1]; p[2] = hit.point[2]; landed = true;
          }
        }
      }
      if (movers) { const mh = moverHit(); if (mh) { hitObstacle(mh); push(); continue; } }
      treeStep();
      apex = Math.max(apex, p[1]);
      let ground = course.heightAt(p[0], p[2]);
      if (ground === null) { result = 'oob'; push(); break; }
      // Without Jolt (or if a step slips through), the ball lands once it is at or below the ground:
      // back up to the moment it touched
      if (!landed && p[1] <= ground && v[1] < 0) {
        landed = true;
        const above = prev[1] - (course.heightAt(prev[0], prev[2]) ?? ground);
        spent = above > 0 ? Math.min(1, above / (above - (p[1] - ground))) : 0;
        for (let k = 0; k < 3; k++) p[k] = prev[k] + (p[k] - prev[k]) * spent;
        ground = course.heightAt(p[0], p[2]) ?? ground;
      }
      if (landed) {
        p[1] = ground;
        t -= (1 - spent) * DT;
        const surf = course.surfaceAt(p[0], p[2]);
        if (!carryPoint) carryPoint = { x: p[0], y: p[1], z: p[2], t };
        if (surf === 'water') { result = 'water'; waterEntry = { ...lastDry }; push(); break; }
        // Pitched straight into the cup (a fast one bounces back out)
        if (pin && Math.hypot(p[0] - pin.x, p[2] - pin.z) < CUP_RADIUS - BALL_RADIUS * 0.3 && Math.hypot(v[0], v[1], v[2]) < 7) {
          dropIntoCup(); result = 'holed'; break;
        }
        lastDry = { x: p[0], y: p[1], z: p[2] };
        const n = course.normalAt(p[0], p[2]);
        const sp = surfaceParams(surf);
        if (firstImpact) impactRpm = Math.hypot(...spin) / RPM;
        // The first landing takes the calibrated landing loss and bite; later hops are roll-out bounces
        OF.resolveImpact(v, spin, n, sp, firstImpact, launch.landing);
        firstImpact = false;
        bounces++;
        if (!OF.hasRebound(v, n, sp) || bounces > 30) { phase = 'roll'; alongGround(); }
        push();
        // The rest of the step goes on after the bounce, as OpenFairway times its contacts
        prev[0] = p[0]; prev[1] = p[1]; prev[2] = p[2];
        p[0] += v[0] * (1 - spent) * DT; p[1] += v[1] * (1 - spent) * DT; p[2] += v[2] * (1 - spent) * DT;
        t += (1 - spent) * DT;
        const h = course.heightAt(p[0], p[2]);
        if (h !== null && (phase === 'roll' || p[1] < h)) p[1] = h;
        const wall = collider && collider.cast(prev, p, 'obstacle');
        if (wall) { hitObstacle(wall); if (phase === 'roll') { p[1] = course.heightAt(p[0], p[2]) ?? p[1]; alongGround(); } }
        continue;
      }
    } else {
      // Rolling along the surface
      const surf = course.surfaceAt(p[0], p[2]);
      if (surf === 'water') { result = 'water'; waterEntry = { ...lastDry }; push(); break; }
      if (surf === null || surf === undefined) { result = 'oob'; push(); break; }
      lastDry = { x: p[0], y: p[1], z: p[2] };
      const sp = surfaceParams(surf);
      const n = course.normalAt(p[0], p[2]);
      OF.stepGround(v, spin, n, sp, impactRpm, DT);
      if (OF.canRest(v, spin, n, sp)) {
        // Stopped over the hole: it falls in
        if (pin && Math.hypot(p[0] - pin.x, p[2] - pin.z) < CUP_RADIUS) { dropIntoCup(); result = 'holed'; break; }
        v[0] = v[1] = v[2] = 0; push(); break;
      }
      prev[0] = p[0]; prev[1] = p[1]; prev[2] = p[2];
      // The height follows the terrain
      p[0] += v[0] * DT; p[2] += v[2] * DT;
      treeStep(); // a rolling ball can run into a trunk
      const h = course.heightAt(p[0], p[2]);
      if (h === null) { result = 'oob'; push(); break; }
      p[1] = h;
      if (collider && (v[0] || v[2])) {
        const hit = collider.cast(prev, p, 'obstacle');
        if (hit) {
          // Rolling into the flagstick: it takes some pace off, so a firm putt hitting it square
          // can still drop (up to about 1.6 m/s), less the further off-centre; harder, it bounces off
          if (hit.kind === 'pole' && pin) {
            const sp = Math.hypot(v[0], v[2]), off = Math.abs((prev[0] - pin.x) * v[2] - (prev[2] - pin.z) * v[0]) / (sp || 1);
            if (sp < 1.6 - 1.4 * Math.min(1, off / CUP_RADIUS)) { dropIntoCup(); result = 'holed'; break; }
          }
          hitObstacle(hit);
          p[1] = course.heightAt(p[0], p[2]) ?? p[1];
          alongGround();
          // Crept up against it (a slope pushing it in): it rests there
          if (Math.hypot(v[0], v[1], v[2]) < 0.1) { v[0] = v[1] = v[2] = 0; push(); break; }
        }
      }
      if (movers && (v[0] || v[2])) {
        const mh = moverHit();
        if (mh) { hitObstacle(mh); p[1] = course.heightAt(p[0], p[2]) ?? p[1]; alongGround(); }
      }
      t += DT;
      // The cup: capture, lip-outs and balls curling around the rim
      if (pin) {
        const c = cup(p, v, pin, cupState, DT);
        if (c === 'holed') { dropIntoCup(); result = 'holed'; break; }
      }
    }
    sampleAcc += DT;
    if (sampleAcc >= 1 / 60 - 1e-9) { sampleAcc = 0; push(); }
  }
  const last = points[points.length - 1];
  if (last.t !== t) push();
  const rest = { x: p[0], y: p[1], z: p[2] };
  const hd = (q) => Math.hypot(q.x - origin.x, q.z - origin.z);
  const ox = Math.sin(opts.heading), oz = Math.cos(opts.heading);
  const offline = (q) => (q.x - origin.x) * oz - (q.z - origin.z) * ox;
  return {
    points,
    result,
    rest,
    waterEntry,
    carryPoint,
    carry: carryPoint ? hd(carryPoint) : hd(rest),
    total: hd(rest),
    offline: offline(rest),
    apex: apex - origin.y,
    flightTime: carryPoint ? carryPoint.t : t,
    duration: t,
    treeHits: treeState.hits,
    spinTilt: tilt, // spin axis tilt (rad, from side spin)
    regime: launch.regime,
  };
}

// One step of the ball among trees. Trunks are solid: the ball bounces off, losing most of its
// speed. Crowns are mostly air: each metre of foliage costs some speed and nudges the line a
// little; deeper in (and in denser pines) it can strike a branch, which knocks it in a random
// direction with a big loss of speed and spin. Returns 'trunk' | 'branch' | 'leaves' | null.
// (With a Jolt collision world the trunks are solid bodies there instead: trunks = false.)
function treeInteract(trees, p, v, dt, st, trunks = true) {
  const list = trees.near(p[0], p[2]);
  if (!list.length) return null;
  let event = null;
  for (const t of list) {
    const dx = p[0] - t.x, dz = p[2] - t.z, dh = Math.hypot(dx, dz);
    if (p[1] < t.trunkTop && dh < t.trunkR + BALL_RADIUS) {
      if (!trunks) continue;
      const nx = dh > 1e-6 ? dx / dh : 1, nz = dh > 1e-6 ? dz / dh : 0;
      const vn = v[0] * nx + v[2] * nz;
      if (vn < 0) {
        const e = 0.3 + Math.random() * 0.15;
        v[0] -= (1 + e) * vn * nx; v[2] -= (1 + e) * vn * nz;
        v[0] *= 0.6; v[1] *= 0.6; v[2] *= 0.6;           // bark scrubs speed off the glancing part too
        p[0] = t.x + nx * (t.trunkR + BALL_RADIUS + 0.01); p[2] = t.z + nz * (t.trunkR + BALL_RADIUS + 0.01);
        st.spinMul *= 0.3;
        event = 'trunk';
      }
      continue;
    }
    // How deep inside the crown the ball is (0 at the edge, 1 at the core)
    let depth = -1;
    if (t.coneBase != null) { // conifers: a cone of needles
      const f = (p[1] - t.coneBase) / t.coneH;
      if (f >= 0 && f <= 1) { const R = t.coneR * (1 - f); if (dh < R) depth = 1 - dh / R; }
    } else {
      const q = (dh / t.rh) ** 2 + ((p[1] - t.cy) / t.rv) ** 2;
      if (q < 1) depth = 1 - Math.sqrt(q);
    }
    if (depth < 0) continue;
    const speed = Math.hypot(v[0], v[1], v[2]);
    if (speed < 1e-3) continue;
    // (a palm's head is open fronds and desert trees are airy: a ball goes through them more easily
    // than a leafy crown; a cactus has only its flowers up there; moss and cypress are thick)
    const SPARSE = { palm: 0.6, mesquite: 0.55, paloverde: 0.45, saguaro: 0.15, liveoak: 1.2, cypress: 1.4, magnolia: 1.3 };
    const step = speed * dt, dense = t.kind === 'spruce' ? 1.7 : t.coneBase != null ? 1.5 : SPARSE[t.kind] ?? 1;
    if (!st.inCrown.has(t)) { st.inCrown.add(t); if (!event) event = 'leaves'; }
    // Leaves: drag (exponential speed loss per metre) and a small random knock off line
    const keep = Math.exp(-(0.008 + 0.12 * depth) * dense * step);
    const jit = (0.03 + 0.06 * depth) * Math.sqrt(step) * speed;
    v[0] = v[0] * keep + (Math.random() - 0.5) * jit;
    v[1] = v[1] * keep + (Math.random() - 0.5) * jit;
    v[2] = v[2] * keep + (Math.random() - 0.5) * jit;
    // Branch: likelier deep in the crown
    if (Math.random() < (0.04 + 0.4 * depth * depth) * dense * step) {
      let rx = Math.random() * 2 - 1, ry = Math.random() * 2 - 1.3, rz = Math.random() * 2 - 1;
      const rl = Math.hypot(rx, ry, rz) || 1; rx /= rl; ry /= rl; rz /= rl;
      const ux = v[0] / speed, uy = v[1] / speed, uz = v[2] / speed, mix = 0.5 + Math.random() * 0.4;
      let nx = ux * (1 - mix) + rx * mix, ny = uy * (1 - mix) + ry * mix, nz = uz * (1 - mix) + rz * mix;
      const nl = Math.hypot(nx, ny, nz) || 1;
      const ns = speed * (0.2 + Math.random() * 0.35);
      v[0] = (nx / nl) * ns; v[1] = (ny / nl) * ns; v[2] = (nz / nl) * ns;
      st.spinMul *= 0.2;
      event = 'branch';
    }
  }
  return event;
}

// One rolling step near the cup. Returns 'holed' when the ball drops.
//  - Over the hole, the ball drops if it falls far enough before reaching the far edge
//    (time over the hole = remaining chord / speed): over its own radius and a bit, so it meets
//    the far wall below its middle. Dead centre that's up to about 1.35 m/s, a putt that would
//    have finished 4-5 ft past at stimp 10 (Holmes' measured limit is about 1.3 m/s), and less
//    the further off-centre. Otherwise it catches the far lip and lips out: slowed, and
//    deflected away from the centre the more off-centre it was.
//  - Hanging over the rim, gravity pulls it towards the hole, so slow putts curl in or
//    roll around the lip.
function cup(p, v, pin, state, dt) {
  const dx = p[0] - pin.x, dz = p[2] - pin.z, d = Math.hypot(dx, dz);
  if (d > 0.3) { state.over = false; return null; }
  const speed = Math.hypot(v[0], v[2]);
  const rim = CUP_RADIUS + BALL_RADIUS * 0.6;
  if (d >= CUP_RADIUS) {
    state.over = false;
    if (d < rim && speed > 1e-4) {
      const overlap = (rim - d) / (rim - CUP_RADIUS);
      const a = G * 0.35 * overlap;
      v[0] -= (dx / d) * a * dt; v[2] -= (dz / d) * a * dt;
    }
    return null;
  }
  if (state.over) return null; // already decided this pass over the hole
  state.over = true;
  if (speed < 0.05) return 'holed';
  const ux = v[0] / speed, uz = v[2] / speed;
  const along = dx * ux + dz * uz;                       // negative while approaching the centre
  const b = Math.abs(dx * uz - dz * ux);                 // how far off-centre the line is
  const chord = -along + Math.sqrt(Math.max(0, CUP_RADIUS * CUP_RADIUS - b * b)) - BALL_RADIUS * 0.5;
  const tOver = Math.max(0, chord) / speed;
  const drop = 0.5 * G * tOver * tOver;
  if (drop >= BALL_RADIUS * 1.2) return 'holed';
  // Lip-out: lose speed on the far lip and spin off away from the centre
  const side = Math.sign(dx * uz - dz * ux) || 1;
  const turn = side * (0.25 + 0.9 * (b / CUP_RADIUS)) * (0.6 + 0.4 * Math.random());
  const c = Math.cos(turn), s = Math.sin(turn), keep = 0.45 + 0.2 * (b / CUP_RADIUS);
  const nx = ux * c - uz * s, nz = ux * s + uz * c;
  v[0] = nx * speed * keep; v[2] = nz * speed * keep;
  return 'lipout';
}

// Convert any OpenConnect-style ball data into back/side spin.
export function normalizeSpin(b) {
  let back = b.BackSpin, side = b.SideSpin;
  const total = b.TotalSpin, axis = b.SpinAxis;
  if ((back == null || (back === 0 && side === 0)) && total != null) {
    const ax = ((axis || 0) * Math.PI) / 180;
    back = total * Math.cos(ax);
    side = total * Math.sin(ax);
  }
  return { backSpin: back || 0, sideSpin: side || 0 };
}
