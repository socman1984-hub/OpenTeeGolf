// Golf ball flight, bounce and roll simulation.
//
// Works in course space (Unity-style frame): x/z horizontal, y up, meters.
// A heading of 0 points along +z; "right" of heading h is (cos h, 0, -sin h).

export const BALL_MASS = 0.04593;           // kg
export const BALL_RADIUS = 0.021335;        // m
const BALL_AREA = Math.PI * BALL_RADIUS * BALL_RADIUS;
const G = 9.81;
export const CUP_RADIUS = 0.054;
const MPH = 0.44704;
const RPM = (2 * Math.PI) / 60;

// Aerodynamic model, calibrated against TrackMan tour averages (see test/physics-test.js).
export const AERO = {
  cd0: 0.22,     // drag at zero spin
  cd1: 0.20,     // drag growth with spin factor
  cdv: 0.08,     // extra drag at lower speeds (Reynolds effect), per 50 m/s below 50 m/s
  cl0: 0.08,     // lift offset
  cl1: 0.80,     // lift slope vs spin factor
  clp: 1.00,     // lift exponent
  clMax: 0.30,
  spinDecay: 0.04, // fraction of spin lost per second
};

// Ground behaviour per surface. e = restitution scale (on Penner's speed-dependent curve),
// fric = Coulomb friction on impact, roll = rolling friction coefficient,
// drag = extra grass drag per m/s above 2 m/s.
export const SURFACES = {
  green:   { e: 1.08, fric: 1.05, roll: null, drag: 0.35 },
  fringe:  { e: 1.00, fric: 1.05, roll: 0.10, drag: 1.8 },
  fairway: { e: 1.20, fric: 1.00, roll: 0.10, drag: 1.5 },
  tee:     { e: 1.20, fric: 1.00, roll: 0.10, drag: 1.5 },
  firstcut:  { e: 1.00, fric: 1.10, roll: 0.16, drag: 2.2 },
  rough:     { e: 0.60, fric: 1.30, roll: 0.35, drag: 4.0 },
  deeprough: { e: 0.35, fric: 1.50, roll: 0.70, drag: 6.0 },
  native:    { e: 0.30, fric: 1.55, roll: 0.80, drag: 6.5 },
  path:      { e: 3.00, fric: 0.30, roll: 0.035, drag: 0.15 }, // pavement: lively bounce, rolls a long way
  sand:      { e: 0.08, fric: 1.60, roll: 1.40, drag: 5.0 },
  oob:       { e: 0.60, fric: 1.30, roll: 0.35, drag: 4.0 },
  water:   { e: 0, fric: 1, roll: 5, drag: 5 },
};

// Solid objects (Jolt collision world): restitution e (+ up to eVar at random), share of speed
// kept after the scrape (keep) and of spin (spin)
export const OBSTACLES = {
  trunk:    { e: 0.3, eVar: 0.15, keep: 0.6, spin: 0.3 },
  building: { e: 0.45, eVar: 0.1, keep: 0.75, spin: 0.5 },
  pole:     { e: 0.2, eVar: 0.1, keep: 0.5, spin: 0.5 },
};

// Turf lets backspin bite harder than a rigid surface would (the ball digs in).
export const GROUND = { spinBite: 2.0 };

const restitution = (vn) => {
  const v = Math.min(Math.abs(vn), 20);
  return 0.51 - 0.0375 * v + 0.000903 * v * v;
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

export function stimpToRoll(stimpFt) {
  // A stimpmeter releases the ball at ~1.83 m/s; it rolls `stimp` feet.
  return (1.83 * 1.83) / (2 * G * stimpFt * 0.3048);
}

// Air density (kg/m³) from altitude, temperature, humidity and (if known) the actual station
// pressure. Warm, humid, thin air = less drag and lift = the ball flies further.
export function airDensity(altitudeM, tempC = 20, humidity = 0.5, pressureHpa = null) {
  const pressure = pressureHpa ? pressureHpa * 100 : 101325 * Math.pow(1 - 2.25577e-5 * altitudeM, 5.25588);
  const T = tempC + 273.15;
  const pv = humidity * 610.78 * Math.pow(10, (7.5 * tempC) / (tempC + 237.3)); // water vapour pressure (Pa)
  return (pressure - pv) / (287.05 * T) + pv / (461.5 * T);
}

// Wind is measured 10 m up; near the ground it is weaker, higher up stronger (log profile, short grass)
export function windAtHeight(h) {
  const z0 = 0.03;
  return Math.min(1.35, Math.max(0.35, Math.log(Math.max(h, 0.5) / z0) / Math.log(10 / z0)));
}

function aero(v, rho, spinRadS, tiltRad, wind, out) {
  // Relative air velocity
  const rx = v[0] - wind[0], ry = v[1] - wind[1], rz = v[2] - wind[2];
  const speed = Math.hypot(rx, ry, rz);
  out[0] = 0; out[1] = -G; out[2] = 0;
  if (speed < 1e-6) return;
  const S = (BALL_RADIUS * spinRadS) / speed;
  const cd = AERO.cd0 + AERO.cd1 * S + (AERO.cdv * (50 - speed)) / 50;
  const cl = S > 0.005 ? Math.min(AERO.clMax, AERO.cl0 + AERO.cl1 * Math.pow(S, AERO.clp)) : 0;
  const q = (0.5 * rho * BALL_AREA * speed * speed) / BALL_MASS;
  const ux = rx / speed, uy = ry / speed, uz = rz / speed;
  // Drag
  out[0] -= q * cd * ux; out[1] -= q * cd * uy; out[2] -= q * cd * uz;
  // Lift: perpendicular to velocity, tilted by spin axis.
  let px = -uy * ux, py = 1 - uy * uy, pz = -uy * uz; // world up minus its velocity component
  const pl = Math.hypot(px, py, pz) || 1;
  px /= pl; py /= pl; pz /= pl;
  const hl = Math.hypot(ux, uz) || 1;
  const sx = uz / hl, sz = -ux / hl;                      // horizontal right of travel
  const c = Math.cos(tiltRad), s = Math.sin(tiltRad);
  out[0] += q * cl * (c * px + s * sx);
  out[1] += q * cl * (c * py);
  out[2] += q * cl * (c * pz + s * sz);
}

/**
 * Simulate a shot from launch until the ball stops.
 *
 * course: { heightAt(x,z), normalAt(x,z), surfaceAt(x,z) }
 * shot:   { speedMph, vla, hla, backSpin, sideSpin }  (deg / rpm, hla & sideSpin positive = right)
 * opts:   { heading (rad), altitudeM, wind: {mps, fromDeg}, stimp, pin:{x,y,z} }
 */
export function simulateShot(course, origin, shot, opts) {
  const rho = airDensity(opts.altitudeM ?? 0, opts.tempC ?? 20, opts.humidity ?? 0.5, opts.pressureHpa ?? null);
  // Wet ground (rain): softer landings, more grip, slower greens
  const wet = Math.min(1, Math.max(0, opts.wet ?? 0));
  const greenRoll = stimpToRoll(Math.max(6, (opts.stimp ?? 10) - 1.8 * wet));
  const surfaceParams = (surf) => {
    const sp = SURFACES[surf] || SURFACES.rough;
    return wet ? { ...sp, e: sp.e * (1 - 0.35 * wet), fric: sp.fric * (1 + 0.15 * wet), roll: sp.roll == null ? null : sp.roll * (1 + 1.0 * wet), drag: sp.drag * (1 + 0.8 * wet) } : sp;
  };
  const windH = [0, 0, 0];
  const heading = opts.heading + (shot.hla * Math.PI) / 180;
  const vla = (shot.vla * Math.PI) / 180;
  const speed = shot.speedMph * MPH;
  const v = [
    speed * Math.cos(vla) * Math.sin(heading),
    speed * Math.sin(vla),
    speed * Math.cos(vla) * Math.cos(heading),
  ];
  const back = shot.backSpin || 0, side = shot.sideSpin || 0;
  let spin = Math.hypot(back, side) * RPM;
  const tilt = Math.atan2(side, back);
  // Wind "from" direction is a compass heading in course space (0 = from +z).
  const wf = ((opts.wind?.fromDeg ?? 0) * Math.PI) / 180;
  const wm = opts.wind?.mps ?? 0;
  const wind = [-Math.sin(wf) * wm, 0, -Math.cos(wf) * wm];

  const p = [origin.x, origin.y, origin.z];
  const points = [{ t: 0, x: p[0], y: p[1], z: p[2] }];
  const a = [0, 0, 0], a2 = [0, 0, 0];
  const dt = 1 / 240;
  let t = 0, apex = p[1], phase = 'air', bounces = 0;
  // Putts (and bumps along the green) start rolling immediately.
  const startSurf = course.surfaceAt(origin.x, origin.z);
  if ((startSurf === 'green' || startSurf === 'fringe') && shot.vla < 6 && shot.speedMph < 25) phase = 'roll';
  let carryPoint = null, result = 'rest', waterEntry = null, lastDry = { x: p[0], y: p[1], z: p[2] };
  const pin = opts.pin;
  let sampleAcc = 0;

  const push = () => points.push({ t, x: p[0], y: p[1], z: p[2] });
  const cupState = { over: false };
  // Trees (optional): { near(x, z) -> [tree] } from the renderer
  const trees = opts.trees || null;
  // Collision world (optional, Jolt): terrain contact points, trunks, buildings and the flagstick
  const collider = opts.collider || null;
  if (collider) collider.setPin(pin || null);
  const prev = [0, 0, 0];
  const treeState = { spinMul: 1, inCrown: new Set(), hits: [] };
  const treeStep = () => {
    if (!trees) return;
    const ev = treeInteract(trees, p, v, dt, treeState, !collider);
    if (ev) treeState.hits.push(ev);
    if (treeState.spinMul !== 1) { spin *= treeState.spinMul; treeState.spinMul = 1; }
  };
  // The ball meets a solid object (trunk, wall, flagstick): bounce off it, losing speed and spin
  const hitObstacle = (hit) => {
    const n = hit.normal, k = OBSTACLES[hit.kind] || OBSTACLES.building;
    const vn = v[0] * n[0] + v[1] * n[1] + v[2] * n[2];
    if (vn < 0) {
      const e = k.e + Math.random() * k.eVar;
      v[0] -= (1 + e) * vn * n[0]; v[1] -= (1 + e) * vn * n[1]; v[2] -= (1 + e) * vn * n[2];
      v[0] *= k.keep; v[1] *= k.keep; v[2] *= k.keep;
      spin *= k.spin;
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

  for (let step = 0; step < 240 * 60; step++) {
    if (phase === 'air') {
      // Midpoint (RK2) integration
      // Wind strength depends on how high the ball is above where it was struck
      const wk = windAtHeight(p[1] - origin.y);
      windH[0] = wind[0] * wk; windH[2] = wind[2] * wk;
      aero(v, rho, spin, tilt, windH, a);
      const vm = [v[0] + a[0] * dt * 0.5, v[1] + a[1] * dt * 0.5, v[2] + a[2] * dt * 0.5];
      aero(vm, rho, spin, tilt, windH, a2);
      prev[0] = p[0]; prev[1] = p[1]; prev[2] = p[2];
      p[0] += vm[0] * dt; p[1] += vm[1] * dt; p[2] += vm[2] * dt;
      v[0] += a2[0] * dt; v[1] += a2[1] * dt; v[2] += a2[2] * dt;
      spin *= 1 - AERO.spinDecay * dt;
      t += dt;
      // What the ball met along this step (Jolt): solid objects bounce it, the ground ends the flight
      let landed = false;
      if (collider) {
        const hit = collider.cast(prev, p);
        if (hit && hit.kind !== 'terrain') { hitObstacle(hit); push(); continue; }
        if (hit) {
          const n = course.normalAt(hit.point[0], hit.point[2]);
          if (v[0] * n[0] + v[1] * n[1] + v[2] * n[2] < 0) { p[0] = hit.point[0]; p[1] = hit.point[1]; p[2] = hit.point[2]; landed = true; }
        }
      }
      treeStep();
      apex = Math.max(apex, p[1]);
      const ground = course.heightAt(p[0], p[2]);
      if (ground === null) { result = 'oob'; push(); break; }
      // Without Jolt (or if a step slips through), the ball lands once it is at or below the ground
      if (!landed && p[1] <= ground && v[1] < 0) landed = true;
      if (landed) {
        p[1] = ground;
        const surf = course.surfaceAt(p[0], p[2]);
        if (!carryPoint) carryPoint = { x: p[0], y: p[1], z: p[2], t };
        if (surf === 'water') { result = 'water'; waterEntry = { ...lastDry }; push(); break; }
        // Pitched straight into the cup (a fast one bounces back out)
        if (pin && Math.hypot(p[0] - pin.x, p[2] - pin.z) < CUP_RADIUS - BALL_RADIUS * 0.3 && Math.hypot(v[0], v[1], v[2]) < 7) {
          dropIntoCup(); result = 'holed'; break;
        }
        lastDry = { x: p[0], y: p[1], z: p[2] };
        const n = course.normalAt(p[0], p[2]);
        const vn = v[0] * n[0] + v[1] * n[1] + v[2] * n[2];
        let tx = v[0] - vn * n[0], ty = v[1] - vn * n[1], tz = v[2] - vn * n[2];
        const tl = Math.hypot(tx, ty, tz);
        const sp = surfaceParams(surf);
        // Impact friction (Penner-style): the contact either keeps sliding, losing mu*(1+e)*|vn|,
        // or friction brings it to rolling at (5v - 2r*w)/7. Backspin (w > 0) can check or spin it back.
        const backRad = spin * Math.cos(tilt);
        const e = Math.min(0.78, sp.e * restitution(vn));
        const sliding = tl - sp.fric * (1 + e) * -vn;
        const rolling = (5 * tl - 2 * GROUND.spinBite * BALL_RADIUS * backRad) / 7;
        let newT;
        if (sliding > rolling) {
          newT = sliding;
          spin = Math.max(0, spin - (2.5 * (tl - newT)) / BALL_RADIUS);
        } else {
          newT = Math.max(rolling, -1.5);                     // limited spin-back
          spin = 0;
        }
        const scale = tl > 1e-6 ? newT / tl : 0;
        tx *= scale; ty *= scale; tz *= scale;
        const bounceV = -vn * e;
        v[0] = tx + bounceV * n[0]; v[1] = ty + bounceV * n[1]; v[2] = tz + bounceV * n[2];
        bounces++;
        if (bounceV < 0.6 || bounces > 12) { phase = 'roll'; p[1] = ground; }
        push();
        continue;
      }
    } else {
      // Rolling along the surface
      const surf = course.surfaceAt(p[0], p[2]);
      if (surf === 'water') { result = 'water'; waterEntry = { ...lastDry }; push(); break; }
      if (surf === null || surf === undefined) { result = 'oob'; push(); break; }
      lastDry = { x: p[0], y: p[1], z: p[2] };
      const sp = surfaceParams(surf);
      const mu = surf === 'green' ? greenRoll : sp.roll;
      const n = course.normalAt(p[0], p[2]);
      // Gravity component along the slope
      const gx = -G * n[1] * n[0], gz = -G * n[1] * n[2];
      // Keep velocity horizontal; the height follows the terrain.
      v[1] = 0;
      const sp2 = Math.hypot(v[0], v[2]);
      const slopeAcc = Math.hypot(gx, gz);
      if (sp2 < 0.03 && slopeAcc < mu * G * 1.2) {
        // Stopped over the hole: it falls in
        if (pin && Math.hypot(p[0] - pin.x, p[2] - pin.z) < CUP_RADIUS) { dropIntoCup(); result = 'holed'; break; }
        v[0] = v[2] = 0; push(); break;
      }
      let ax = gx, az = gz;
      const dec = mu * G + sp.drag * Math.max(0, sp2 - 2);
      if (sp2 > 1e-6) { ax -= (dec * v[0]) / sp2; az -= (dec * v[2]) / sp2; }
      const vx0 = v[0], vz0 = v[2];
      v[0] += ax * dt; v[2] += az * dt;
      // Friction must not reverse the motion where the slope can't hold it moving
      if (v[0] * vx0 + v[2] * vz0 < 0 && slopeAcc < mu * G) v[0] = v[2] = 0;
      prev[0] = p[0]; prev[1] = p[1]; prev[2] = p[2];
      p[0] += v[0] * dt; p[2] += v[2] * dt;
      treeStep(); // a rolling ball can run into a trunk
      const h = course.heightAt(p[0], p[2]);
      if (h === null) { result = 'oob'; push(); break; }
      p[1] = h;
      if (collider && (v[0] || v[2])) {
        const hit = collider.cast(prev, p, 'obstacle');
        if (hit) {
          // Rolling into the flagstick: a gentle one drops, a firm one bounces off it
          if (hit.kind === 'pole' && Math.hypot(v[0], v[2]) < 2.4) { dropIntoCup(); result = 'holed'; break; }
          hitObstacle(hit);
          v[1] = 0;
          p[1] = course.heightAt(p[0], p[2]) ?? p[1];
        }
      }
      t += dt;
      // The cup: capture, lip-outs and balls curling around the rim
      if (pin) {
        const c = cup(p, v, pin, cupState, dt);
        if (c === 'holed') { dropIntoCup(); result = 'holed'; break; }
      }
    }
    sampleAcc += dt;
    if (sampleAcc >= 1 / 60) { sampleAcc = 0; push(); }
  }
  const last = points[points.length - 1];
  if (last.t !== t) push();
  const rest = { x: p[0], y: p[1], z: p[2] };
  const hd = (q) => Math.hypot(q.x - origin.x, q.z - origin.z);
  const fx = Math.sin(opts.heading), fz = Math.cos(opts.heading);
  const offline = (q) => (q.x - origin.x) * fz - (q.z - origin.z) * fx;
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
    if (t.kind === 'pine') {
      const f = (p[1] - t.coneBase) / t.coneH;
      if (f >= 0 && f <= 1) { const R = t.coneR * (1 - f); if (dh < R) depth = 1 - dh / R; }
    } else {
      const q = (dh / t.rh) ** 2 + ((p[1] - t.cy) / t.rv) ** 2;
      if (q < 1) depth = 1 - Math.sqrt(q);
    }
    if (depth < 0) continue;
    const speed = Math.hypot(v[0], v[1], v[2]);
    if (speed < 1e-3) continue;
    const step = speed * dt, dense = t.kind === 'pine' ? 1.5 : 1;
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
//    (time over the hole = remaining chord / speed); otherwise it catches the far lip and
//    lips out: slowed, and deflected away from the centre the more off-centre it was.
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
  if (drop >= BALL_RADIUS * 0.75) return 'holed';
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
