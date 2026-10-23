// Practice areas: a driving range, target greens and a putt-putt course.
//
// They are generated rather than mapped. Each is a course file plus the map data a real course
// brings (a height grid and the outlines of its tees, fairways, greens and bunkers), so course.js,
// the scene, the physics and the Jolt collisions treat it like any other course. What only a
// practice area has (a flag on every target, distance boards, the putt-putt walls, posts,
// windmill and tunnel) is added to the scene by decorate(). The mountains on the horizon are made
// here too, in the same shape as a real course's distant land (lib/farland.js), so nothing has to
// be downloaded.
import * as THREE from 'three/webgpu';
import { noise, pointInPoly } from './course.js';
import { BALL_RADIUS } from './physics.js';

export const PRACTICE_AREAS = {
  range: { name: 'Driving range', blurb: 'Open range below the mountains. Carry, total and launch data on every shot.' },
  targets: { name: 'Target greens', blurb: 'Seven greens from wedge to long iron, all from one tee.' },
  putt: { name: 'Putt-putt', blurb: 'Six holes of mini golf: ramps, a tunnel and a windmill.' },
};

const YD = 0.9144;
const T = (x, y, z) => new THREE.Vector3(-x, y, z);
const smooth = (e0, e1, x) => { const t = Math.max(0, Math.min(1, (x - e0) / (e1 - e0))); return t * t * (3 - 2 * t); };
const lerp = (a, b, t) => a + (b - a) * t;
const FLAG_COLOURS = ['#e53935', '#f5f5f5', '#1e88e5', '#fdd835', '#43a047', '#fb8c00', '#8e24aa'];
// Where the generated land "is" (only for which trees grow: the Rocky Mountains' conifers)
const LAT = 39.6, LON = -106.0;

// ------------------------------------------------------------------ shapes

const rect = (x0, z0, x1, z1) => [[x0, z0], [x1, z0], [x1, z1], [x0, z1]];
// A rounded blob (greens, bunkers): radii rx across, rz along, a gentle wobble
function blob(cx, cz, rx, rz, seed = 0, n = 40) {
  const pts = [];
  for (let k = 0; k < n; k++) {
    const a = (k / n) * Math.PI * 2, w = 1 + 0.07 * Math.sin(3 * a + seed) + 0.04 * Math.sin(5 * a + seed * 2.3);
    pts.push([cx + Math.cos(a) * rx * w, cz + Math.sin(a) * rz * w]);
  }
  return pts;
}
const move = (poly, [ox, oz]) => poly.map(([x, z]) => [x + ox, z + oz]);
const area = (poly) => { let a = 0; for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) a += poly[j][0] * poly[i][1] - poly[i][0] * poly[j][1]; return a / 2; };

// Grow a convex outline outward by d (each edge moved out, corners mitred)
function grow(poly, d) {
  const s = Math.sign(area(poly)) || 1, n = poly.length, lines = [];
  for (let i = 0; i < n; i++) {
    const [ax, az] = poly[i], [bx, bz] = poly[(i + 1) % n], l = Math.hypot(bx - ax, bz - az) || 1;
    const nx = ((bz - az) / l) * s, nz = (-(bx - ax) / l) * s;
    lines.push([ax + nx * d, az + nz * d, bx - ax, bz - az]);
  }
  return lines.map((L, i) => {
    const M = lines[(i + n - 1) % n];
    const den = M[2] * L[3] - M[3] * L[2];
    if (Math.abs(den) < 1e-9) return [L[0], L[1]];
    const t = ((L[0] - M[0]) * L[3] - (L[1] - M[1]) * L[2]) / den;
    return [M[0] + M[2] * t, M[1] + M[3] * t];
  });
}

function heightGrid(minX, minZ, maxX, maxZ, cell, heightAt) {
  const nx = Math.round((maxX - minX) / cell) + 1, nz = Math.round((maxZ - minZ) / cell) + 1;
  const heights = new Float32Array(nx * nz);
  for (let j = 0; j < nz; j++) for (let i = 0; i < nx; i++) heights[j * nx + i] = heightAt(minX + i * cell, minZ + j * cell);
  return { minX, minZ, cell, nx, nz, heights };
}

// Woods on the land round a practice area: clumps of trees wherever open(x, z) is false, thicker
// further from the open ground (out to the edge of the height grid) [[x, z, height]]
function woods(grid, open, seed = 3, step = 6.5) {
  const out = [], { minX, minZ, cell, nx, nz } = grid, maxX = minX + (nx - 1) * cell, maxZ = minZ + (nz - 1) * cell;
  const rnd = () => ((seed = (seed * 16807) % 2147483647) / 2147483647);
  for (let z = minZ + 4; z < maxZ - 4; z += step) for (let x = minX + 4; x < maxX - 4; x += step) {
    const px = x + (rnd() - 0.5) * step, pz = z + (rnd() - 0.5) * step, o = open(px, pz);
    if (o > 0) continue;
    if (rnd() > Math.min(0.85, 0.3 + -o / 40 + 0.35 * noise(px / 45, pz / 45))) continue;
    out.push([px, pz, 9 + rnd() * 13]);
  }
  return out;
}

// One hole of the course file: a white tee, the pin (the same every day), aim points between
const holeData = (number, par, tee, pin, aims = []) => {
  const P = ([x, y, z]) => ({ x, y, z });
  const path = [tee, ...aims, pin];
  let len = 0;
  for (let i = 1; i < path.length; i++) len += Math.hypot(path[i][0] - path[i - 1][0], path[i][2] - path[i - 1][2]);
  return {
    HoleNumber: number, Par: par, Index: number, Enabled: true,
    Tees: [
      { TeeType: 'White', Position: P(tee), Distance: len },
      ...aims.map((a, i) => ({ TeeType: `AimPoint${i + 1}`, Position: P(a), Distance: 0 })),
      { TeeType: 'GreenCenterPoint', Position: P(pin), Distance: 0 },
    ],
    Pins: ['Thursday', 'Friday', 'Saturday', 'Sunday'].map((Day) => ({ Day, Position: P(pin) })),
  };
};
const courseFile = (name, holes, extra = {}) => ({
  CourseName: name, Location: 'Practice', Designer: 'OpenTeeGolf', Holes: holes, Hazards: [],
  DefaultStimp: 10, CourseLat: LAT, CourseLong: LON, altitude: 0, Mowing: { fairway: 'stripes', green: 'checker' }, ...extra,
});

// ------------------------------------------------------------------ the three areas

export async function makePractice(kind, { units = 'yd' } = {}) {
  const spot = kind === 'putt' ? puttPutt() : kind === 'targets' ? targetGreens(units) : drivingRange(units);
  spot.kind = kind;
  spot.name = PRACTICE_AREAS[kind].name;
  spot.farland = await mountains();
  return spot;
}

// The range: a wide mown fan from a long tee line out to a bank at 330 m, a flag on a little
// green at every 50 (yards or metres), boards with the distance down both sides, trees along the
// edges and the valley sides rising to the mountains.
function drivingRange(units) {
  const U = units === 'm' ? 1 : YD, word = units === 'm' ? 'm' : 'yd';
  const BASE = 2, END = 330;
  const hw = (z) => 40 + Math.max(0, z) * 0.2;
  const greens = [50, 100, 150, 200, 250].map((d, i) => ({ d, x: [-9, 13, -16, 15, -5][i], z: d * U, r: 4.5 + d * U * 0.022, label: `${d} ${word}`, colour: FLAG_COLOURS[i] }));
  const heightAt = (x, z) => {
    let h = BASE + 0.12 * noise(x / 35, z / 35);
    const out = Math.max(Math.abs(x) - hw(z), z - END, -28 - z);
    if (out > 0) h += 0.035 * out ** 1.3 + smooth(0, 30, out) * (1.4 * noise(x / 60 + 3, z / 60 - 2) + 0.5 * noise(x / 19, z / 19));
    for (const g of greens) h += 0.45 * (1 - smooth(g.r * 0.6, g.r * 1.9, Math.hypot(x - g.x, z - g.z)));
    const tee = Math.max(Math.abs(x) - 32, z - 5, -12 - z);
    return lerp(BASE, h, smooth(-1, 7, tee));
  };
  const edge = [];
  for (let z = 6; z <= END; z += 12) edge.push([-hw(z), z]);
  const fan = [...edge, ...edge.slice().reverse().map(([x, z]) => [-x, z])];
  const pinG = greens[2];
  const pin = [pinG.x, heightAt(pinG.x, pinG.z), pinG.z];
  const rows = [-1, 1].map((s) => { const r = []; for (let z = -20; z <= END + 20; z += 7) r.push([s * (hw(z) + 16), z]); return r; });
  const marks = [];
  for (let d = 50; d * U <= END; d += 50) marks.push(d);
  const grid = heightGrid(-260, -90, 260, 450, 2, heightAt);
  // (woods from 14 m beyond the mown range, and behind the tee)
  const trees = woods(grid, (x, z) => Math.min(hw(z) + 14 - Math.abs(x), z + 45, END + 30 - z));
  return {
    data: courseFile('Driving Range', [holeData(1, 3, [0, BASE, 0], pin)]),
    real: {
      grid,
      features: {
        tee: [{ pts: rect(-32, -12, 32, 5) }],
        fairway: [{ pts: fan }],
        green: greens.map((g, i) => ({ pts: blob(g.x, g.z, g.r, g.r * 0.9, i * 1.7) })),
        bunker: [{ pts: blob(greens[1].x + greens[1].r + 4.5, greens[1].z - 2, 4.5, 2.6, 2) }, { pts: blob(greens[3].x - greens[3].r - 5, greens[3].z + 3, 5, 3, 5) }],
      },
      trees,
      treeRows: rows,
    },
    targets: greens,
    solids: [],
    decorate(scene, course) {
      const g = new THREE.Group();
      this.flags = greens.map((t, i) => {
        const f = flag(t.colour);
        f.position.copy(T(t.x, course.heightAt(t.x, t.z) ?? BASE, t.z));
        f.visible = i !== 2; // (the 150 has the game's own flag)
        g.add(f);
        return f;
      });
      g.add(mats([-24, -20, -16, -12, -8, -4, 0, 4, 8, 12, 16, 20, 24], BASE));
      for (const d of marks) for (const s of [-1, 1]) {
        const z = d * U, x = s * (hw(z) + 5);
        const b = board([String(d), units === 'm' ? 'METRES' : 'YARDS'], 3.2, 1.6, 1.3);
        b.position.copy(T(x, course.heightAt(x, z) ?? BASE, z));
        b.rotation.y = Math.atan2(x, -z); // (facing the tee)
        g.add(b);
      }
      add(scene, g);
    },
  };
}

// Target greens: seven raised greens with bunkers, spread in a fan from one tee, from a wedge to a
// long iron away. Each green is a "hole" of the course, so the flag, the distance and the club
// follow whichever one is aimed at.
function targetGreens(units) {
  const U = units === 'm' ? 1 : YD, word = units === 'm' ? 'm' : 'yd';
  // (the tee stands on a bank RISE above the field, so the greens can be seen from it)
  const BASE = 2, RISE = 4;
  const greens = [50, 75, 100, 125, 150, 175, 200].map((d, i) => {
    const a = ([-14, 10, -6, 13, -10, 5, -2][i] * Math.PI) / 180, D = d * U;
    return { d, x: D * Math.sin(a), z: D * Math.cos(a), r: 6 + D * 0.03, label: `${d} ${word}`, colour: FLAG_COLOURS[i] };
  });
  const heightAt = (x, z) => {
    let h = BASE + 0.4 * noise(x / 50, z / 50) + 0.18 * noise(x / 17 + 5, z / 17);
    const side = Math.max(Math.abs(x) - 110, z - 250, -40 - z);
    if (side > 0) h += 0.04 * side ** 1.3 + smooth(0, 30, side) * 1.2 * noise(x / 45, z / 45 + 9);
    // the tee's bank, and each green on a low, smooth pedestal
    h = lerp(BASE + RISE, h, smooth(0, 22, Math.max(Math.abs(x) - 12, z - 4, -8 - z)));
    for (const g of greens) {
      const d = Math.hypot(x - g.x, z - g.z), k = 1 - smooth(g.r * 0.9, g.r * 2.2, d);
      h = lerp(h, BASE + 0.5 + 0.04 * (z - g.z) / g.r, k);
    }
    return h;
  };
  const pins = greens.map((g, i) => {
    const [px, pz] = [[0.2, 0.25], [-0.3, -0.1], [0.25, -0.3], [-0.2, 0.3], [0.3, 0.1], [-0.25, -0.25], [0, 0.35]][i];
    const x = g.x + px * g.r, z = g.z + pz * g.r;
    return [x, heightAt(x, z), z];
  });
  const bunkers = [];
  greens.forEach((g, i) => {
    const s = i % 2 ? 1 : -1;
    bunkers.push({ pts: blob(g.x + s * (g.r + 3.5), g.z - g.r * 0.3, 3.2 + g.r * 0.15, 2.2 + g.r * 0.1, i) });
    if (i >= 3) bunkers.push({ pts: blob(g.x - s * g.r * 0.4, g.z - g.r - 3.2, 2.6 + g.r * 0.25, 1.9, i + 4) });
  });
  const aprons = greens.map((g, i) => ({ pts: blob(g.x - (g.x / Math.hypot(g.x, g.z)) * g.r * 0.9, g.z - g.r * 0.9, g.r * 0.9, g.r * 1.2, i + 9) }));
  const grid = heightGrid(-210, -100, 210, 300, 2, heightAt);
  // (an open field out past the longest green; woods round it)
  const trees = woods(grid, (x, z) => Math.min(95 - Math.abs(x) + 0.1 * z, z + 40, 250 - z), 7);
  return {
    data: courseFile('Target Greens', greens.map((g, i) => holeData(i + 1, 3, [0, BASE + RISE, 0], pins[i]))),
    real: {
      grid,
      features: {
        tee: [{ pts: rect(-12, -8, 12, 4) }],
        fairway: [{ pts: blob(0, 22, 17, 20, 3) }, ...aprons],
        green: greens.map((g, i) => ({ pts: blob(g.x, g.z, g.r, g.r * 0.92, i * 1.3) })),
        bunker: bunkers,
      },
      trees,
      treeRows: [],
    },
    targets: greens,
    solids: [],
    decorate(scene, course) {
      const g = new THREE.Group();
      g.add(mats([0], BASE + RISE));
      this.flags = greens.map((t, i) => {
        const p = pins[i], f = flag(t.colour);
        f.position.copy(T(p[0], course.heightAt(p[0], p[2]) ?? p[1], p[2]));
        g.add(f);
        // a board with the distance just short of the green's near edge, facing the tee
        const bx = t.x - (t.r + 2.5) * (i % 2 ? 1 : -1), bz = t.z - t.r * 0.6;
        const b = board([t.label], 2.2, 1.0, 0.7, t.colour);
        b.position.copy(T(bx, course.heightAt(bx, bz) ?? BASE, bz));
        b.rotation.y = Math.atan2(bx, -bz);
        g.add(b);
        return f;
      });
      add(scene, g);
    },
    // The game's flag stands on the green being aimed at; the others keep their own
    setActive(i) { this.flags?.forEach((f, k) => (f.visible = k !== i)); },
  };
}

// ------------------------------------------------------------------ putt-putt

// Six holes laid out in two rows inside a fenced lawn, all played towards the mountains. Each
// lane is an outline (local metres, tee at the origin, playing along +z) moved to its plot:
// felt where the ball rolls ('green'), a 10 cm board all round it, and its obstacles.
const PUTT_HOLES = [
  { name: 'Warm-up', par: 2, at: [-12, 0], lane: rect(-0.6, -0.8, 0.6, 8.6), cup: [0, 7.7], posts: [[-0.24, 3.4], [0.26, 5.4]] },
  { name: 'Dogleg', par: 3, at: [-2.5, 0], lane: [[-0.6, -0.8], [0.6, -0.8], [0.6, 5.6], [4.6, 5.6], [4.6, 7.9], [-0.6, 7.9]], cup: [3.8, 6.75], aim: [0, 6.6],
    blocks: [[[-0.6, 6.2], [1.1, 7.9], [-0.6, 7.9]]], posts: [[2.3, 7.3]] },
  { name: 'The ramp', par: 2, at: [9, 0], lane: rect(-0.6, -0.8, 0.6, 10.2), cup: [0, 9.0], ramp: { z0: 3.6, z1: 5.8, rise: 0.28, end: 10.2 }, posts: [[-0.34, 7.3], [0.34, 7.3]] },
  { name: 'The windmill', par: 3, at: [-12, 15], cup: [0, 10.8], windmill: { z: 5.2, depth: 1.6, half: 0.17 },
    lane: [[-0.6, -0.8], [0.6, -0.8], [0.6, 3.9], [0.17, 5.0], [0.17, 6.9], [0.6, 7.8], [0.6, 12], [-0.6, 12], [-0.6, 7.8], [-0.17, 6.9], [-0.17, 5.0], [-0.6, 3.9]] },
  { name: 'Tunnel and volcano', par: 3, at: [-1, 15], cup: [0, 9.6], tunnel: { z0: 2.2, z1: 4.6 }, volcano: { x: 0, z: 9.6, r: 1.6, h: 0.12 }, lane: volcanoLane(0.65, 9.6, 1.9) },
  { name: 'Zig-zag', par: 3, at: [10.5, 15], lane: [[-0.6, -0.8], [0.6, -0.8], [0.6, 4.4], [-2.4, 4.4], [-2.4, 10], [-3.6, 10], [-3.6, 3.2], [-0.6, 3.2]], cup: [-3.0, 9.0], aim: [-0.66, 3.9], posts: [[-3.0, 6.4]] },
];
const LAWN = rect(-16.5, -3.5, 15, 29.5);
const WALL = { t: 0.06, up: 0.1, down: 0.05 };
// Windmill sails: how fast they turn (rad/s) and how far either side of straight down one blocks
// the tunnel (rad)
const SAIL_SPEED = 1.1, SAIL_BLOCK = 0.2, HOLE_COLOURS = [0xd84a3a, 0x2f7fd8, 0xe8b628, 0xe0752d, 0x8e4fc4, 0x22a39a];

function volcanoLane(hw, cz, r) {
  const zc = cz - Math.sqrt(r * r - hw * hw), a0 = Math.atan2(hw, zc - cz), pts = [[-hw, -0.8], [hw, -0.8], [hw, zc]];
  for (let k = 1; k < 40; k++) { const a = a0 - (2 * a0 * k) / 40; pts.push([r * Math.sin(a), cz + r * Math.cos(a)]); }
  pts.push([-hw, zc]);
  return pts;
}

function puttPutt() {
  const holes = PUTT_HOLES.map((h) => ({
    ...h,
    laneAbs: move(h.lane, h.at),
    tee: [h.at[0], h.at[1]],
    cupAbs: [h.cup[0] + h.at[0], h.cup[1] + h.at[1]],
    aimAbs: h.aim ? [h.aim[0] + h.at[0], h.aim[1] + h.at[1]] : null,
  }));
  const lawnOut = (x, z) => Math.max(LAWN[0][0] - x, x - LAWN[1][0], LAWN[0][1] - z, z - LAWN[2][1]);
  const heightAt = (x, z) => {
    let h = 0;
    const out = lawnOut(x, z);
    if (out > 0) h += smooth(0, 8, out) * (0.5 * noise(x / 14, z / 14) + 0.2 * noise(x / 5 + 3, z / 5) + 0.012 * out ** 1.4);
    for (const H of holes) {
      const lx = x - H.at[0], lz = z - H.at[1];
      if (H.ramp) {
        const R = H.ramp, across = 1 - smooth(0.75, 1.9, Math.abs(lx)), along = 1 - smooth(R.end + 0.1, R.end + 1.3, lz);
        h += R.rise * smooth(R.z0, R.z1, lz) * across * along;
      }
      if (H.volcano) {
        const V = H.volcano, d = Math.hypot(lx - V.x, lz - V.z);
        if (d < V.r) h += V.h * Math.cos((Math.PI / 2) * (d / V.r)) ** 2;
      }
    }
    return h;
  };
  const courseHoles = holes.map((H, i) => holeData(i + 1, H.par,
    [H.tee[0], heightAt(...H.tee), H.tee[1]], [H.cupAbs[0], heightAt(...H.cupAbs), H.cupAbs[1]],
    H.aimAbs ? [[H.aimAbs[0], heightAt(...H.aimAbs), H.aimAbs[1]]] : []));
  // Solid things for the collision world (physics.js OBSTACLES 'bumper'): every board (a little
  // thicker on the inside by the ball's radius, as the collisions use the ball's centre), posts,
  // blocks and the windmill's walls
  const solids = [];
  const R = BALL_RADIUS, solid = (pts) => solids.push({ pts, h: 0.5 + WALL.up, kind: 'bumper' });
  for (const H of holes) {
    for (const [a, b, n] of edges(H.laneAbs)) {
      const len = Math.hypot(b[0] - a[0], b[1] - a[1]), k = Math.max(1, Math.ceil(len / 0.5));
      const ux = (b[0] - a[0]) / len, uz = (b[1] - a[1]) / len, ext = R + 0.01;
      for (let s = 0; s < k; s++) {
        const s0 = (s / k) * len - (s === 0 ? ext : 0), s1 = ((s + 1) / k) * len + (s === k - 1 ? ext : 0);
        const p = (d, o) => [a[0] + ux * d + n[0] * o, a[1] + uz * d + n[1] * o];
        solid([p(s0, -R), p(s1, -R), p(s1, WALL.t), p(s0, WALL.t)]);
      }
    }
    for (const [px, pz] of H.posts || []) {
      const r = (0.06 + R) / Math.cos(Math.PI / 8), o = [];
      for (let k = 0; k < 8; k++) o.push([px + H.at[0] + r * Math.cos((k * Math.PI) / 4), pz + H.at[1] + r * Math.sin((k * Math.PI) / 4)]);
      solid(o);
    }
    for (const b of H.blocks || []) solid(grow(move(b, H.at), R));
    if (H.windmill) {
      const W = H.windmill, z0 = H.at[1] + W.z, z1 = z0 + W.depth;
      solid(rect(H.at[0] - 0.9, z0, H.at[0] - W.half - 0.01, z1));
      solid(rect(H.at[0] + W.half + 0.01, z0, H.at[0] + 0.9, z1));
    }
  }
  const trees = [];
  let seed = 11;
  const rnd = () => ((seed = (seed * 16807) % 2147483647) / 2147483647);
  // a ring of trees round the lawn, gaps here and there
  for (let k = 0; k < 64; k++) {
    const a = (k / 64) * Math.PI * 2 + rnd() * 0.05, rx = 26 + rnd() * 6, rz = 25 + rnd() * 6;
    const x = -0.75 + Math.cos(a) * rx, z = 13 + Math.sin(a) * rz;
    if (rnd() < 0.25 || (z > 20 && Math.abs(x) < 9)) continue; // (open towards the mountains)
    trees.push([x, z, 7 + rnd() * 9]);
  }
  const grid = heightGrid(-44, -24, 44, 54, 0.2, heightAt);
  // (and woods beyond them, but not in front: that's where the mountains are)
  trees.push(...woods(grid, (x, z) => Math.max(31 - Math.hypot(x + 0.75, z - 13), z - 36), 13));
  const spot = {
    data: courseFile('Putt-Putt', courseHoles, { Mowing: { fairway: 'stripes', green: 'none' }, DefaultStimp: 9 }),
    real: {
      grid,
      features: {
        tee: [{ pts: LAWN, exact: true }],
        green: holes.map((H) => ({ pts: H.laneAbs, exact: true })),
      },
      trees,
      treeRows: [],
    },
    holes: holes.map((H) => ({ name: H.name, aim: H.aimAbs ? { x: H.aimAbs[0], z: H.aimAbs[1] } : null, lane: H.laneAbs })),
    solids,
    shadowSpan: 45,
    // The windmill's sail angle at a time on the practice clock
    sail: (t) => t * SAIL_SPEED,
    // Movers for the physics: the sails sweeping across the windmill's tunnel, from the moment
    // of the strike (t0, practice clock)
    movers(t0) {
      const out = [];
      for (const H of holes) {
        if (!H.windmill) continue;
        const gx = H.at[0], gz = H.at[1] + H.windmill.z - 0.1, half = H.windmill.half;
        out.push((a, b, t) => {
          const da = a[2] - gz, db = b[2] - gz;
          if ((da < 0) === (db < 0) || da === db) return null;
          const f = da / (da - db), x = a[0] + (b[0] - a[0]) * f, y = a[1] + (b[1] - a[1]) * f;
          if (Math.abs(x - gx) > half + 0.03 || y - heightAt(x, gz) > 0.35) return null;
          const q = Math.PI / 2, m = (((spot.sail(t0 + t) % q) + q) % q);
          if (Math.min(m, q - m) > SAIL_BLOCK) return null;
          const dir = Math.sign(db - da);
          return { kind: 'blade', normal: [0, 0, -dir], point: [x, y, gz - dir * 0.01] };
        });
      }
      return out;
    },
    update(t) { if (spot.sails) spot.sails.rotation.z = spot.sail(t); },
    decorate(scene, course) { decoratePutt(scene, course, holes, spot); },
  };
  return spot;
}

// Each edge of an outline with its outward normal: [a, b, [nx, nz]]
function edges(poly) {
  const s = Math.sign(area(poly)) || 1, out = [];
  for (let i = 0; i < poly.length; i++) {
    const a = poly[i], b = poly[(i + 1) % poly.length], l = Math.hypot(b[0] - a[0], b[1] - a[1]) || 1;
    out.push([a, b, [((b[1] - a[1]) / l) * s, (-(b[0] - a[0]) / l) * s]]);
  }
  return out;
}

function decoratePutt(scene, course, holes, spot) {
  const g = new THREE.Group();
  const H = (x, z) => course.heightAt(x, z) ?? 0;
  const felt = new THREE.MeshStandardMaterial({ map: feltTexture(), roughness: 0.95, metalness: 0 });
  const boardGeo = new THREE.BoxGeometry(1, 1, 1);
  const pieces = [];
  holes.forEach((h, i) => {
    g.add(drape(h.laneAbs, H, 0.006, felt));
    for (const [a, b, n] of edges(h.laneAbs)) {
      const len = Math.hypot(b[0] - a[0], b[1] - a[1]), k = Math.max(1, Math.ceil(len / 0.4));
      for (let s = 0; s < k; s++) {
        const f0 = s / k, f1 = (s + 1) / k;
        const p0 = [a[0] + (b[0] - a[0]) * f0 + (n[0] * WALL.t) / 2, a[1] + (b[1] - a[1]) * f0 + (n[1] * WALL.t) / 2];
        const p1 = [a[0] + (b[0] - a[0]) * f1 + (n[0] * WALL.t) / 2, a[1] + (b[1] - a[1]) * f1 + (n[1] * WALL.t) / 2];
        pieces.push({ p0, p1, ext: (s === 0 ? WALL.t / 2 : 0) + (s === k - 1 ? WALL.t / 2 : 0), colour: HOLE_COLOURS[i] });
      }
    }
    const post = new THREE.MeshStandardMaterial({ color: 0xf2f2f2, roughness: 0.5 });
    const band = new THREE.MeshStandardMaterial({ color: HOLE_COLOURS[i], roughness: 0.5 });
    for (const [px, pz] of h.posts || []) {
      const x = px + h.at[0], z = pz + h.at[1], y = H(x, z);
      const m = new THREE.Mesh(new THREE.CylinderGeometry(0.06, 0.06, 0.24, 20), post);
      m.position.copy(T(x, y + 0.07, z));
      const b = new THREE.Mesh(new THREE.CylinderGeometry(0.062, 0.062, 0.06, 20), band);
      b.position.copy(T(x, y + 0.12, z));
      m.castShadow = b.castShadow = true;
      g.add(m, b);
    }
    for (const b of h.blocks || []) g.add(prism(move(b, h.at), H, -0.03, WALL.up + 0.01, new THREE.MeshStandardMaterial({ color: HOLE_COLOURS[i], roughness: 0.6 })));
    // tee mat and the hole's sign
    const [tx, tz] = h.tee;
    const mat = new THREE.Mesh(new THREE.PlaneGeometry(0.5, 0.34), new THREE.MeshStandardMaterial({ color: 0x2b2d2f, roughness: 0.9 }));
    mat.rotation.x = -Math.PI / 2;
    mat.position.copy(T(tx, H(tx, tz) + 0.009, tz - 0.05));
    mat.receiveShadow = true;
    g.add(mat);
    // (beside the lane a little way up, turned to face the player on the tee)
    const minX = Math.min(...h.laneAbs.map((p) => p[0]));
    const sx = minX - 0.9, sz = tz + 1.4;
    const sign = board([`${i + 1}`, h.name, `Par ${h.par}`], 0.7, 0.48, 0.4, `#${HOLE_COLOURS[i].toString(16).padStart(6, '0')}`);
    sign.position.copy(T(sx, H(sx, sz), sz));
    sign.rotation.y = Math.atan2(sx - tx, -(sz - tz + 2.6));
    g.add(sign);
    if (h.windmill) g.add(windmill(h, H, spot));
    if (h.tunnel) g.add(tunnel(h, H));
  });
  // The boards: one instanced box each piece, tilted to follow ramps
  const boards = new THREE.InstancedMesh(boardGeo, new THREE.MeshStandardMaterial({ color: 0xffffff, roughness: 0.55 }), pieces.length);
  const m4 = new THREE.Matrix4(), col = new THREE.Color();
  pieces.forEach((pc, k) => {
    const y0 = H(...pc.p0), y1 = H(...pc.p1);
    const A = T(pc.p0[0], y0, pc.p0[1]), B = T(pc.p1[0], y1, pc.p1[1]);
    const X = B.clone().sub(A), len = X.length();
    X.normalize();
    const Zh = new THREE.Vector3(-X.z, 0, X.x).normalize(), Y = new THREE.Vector3().crossVectors(Zh, X);
    const mid = A.clone().add(B).multiplyScalar(0.5);
    // (the first and last piece of each side reach past the corner to meet the next side)
    m4.makeBasis(X, Y, Zh).scale(new THREE.Vector3(len + pc.ext, WALL.up + WALL.down, WALL.t));
    m4.setPosition(mid.x, mid.y + (WALL.up - WALL.down) / 2, mid.z);
    boards.setMatrixAt(k, m4);
    boards.setColorAt(k, col.set(pc.colour));
  });
  boards.castShadow = true;
  boards.receiveShadow = true;
  g.add(boards);
  g.add(fence(H), flowers(H, holes));
  const welcome = board(['PUTT-PUTT', 'OpenTeeGolf'], 3.2, 1.2, 1.2, '#d84a3a');
  welcome.position.copy(T(-0.75, H(-0.75, -5.5), -5.5));
  welcome.rotation.y = Math.PI;
  g.add(welcome);
  add(scene, g);
  scene.setShadowSpan(spot.shadowSpan);
  scene.setFlagHeight(1.1);
}

// The lane's felt: the outline cut into 25 cm squares (each clipped to the outline), every corner
// set on the ground, so it follows the ramps and the volcano
function drape(poly, heightAt, lift, material) {
  const xs = poly.map((p) => p[0]), zs = poly.map((p) => p[1]);
  const C = 0.25, pos = [], uv = [];
  const put = (x, z) => { pos.push(-x, heightAt(x, z) + lift, z); uv.push(x * 0.5, z * 0.5); };
  for (let z = Math.floor(Math.min(...zs) / C) * C; z < Math.max(...zs); z += C) {
    for (let x = Math.floor(Math.min(...xs) / C) * C; x < Math.max(...xs); x += C) {
      const piece = clipBox(poly, x, z, x + C, z + C);
      if (piece.length < 3) continue;
      const tris = THREE.ShapeUtils.triangulateShape(piece.map(([a, b]) => new THREE.Vector2(a, b)), []);
      for (const t of tris) for (const k of t) put(...piece[k]);
    }
  }
  const geo = new THREE.BufferGeometry();
  geo.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  geo.setAttribute('uv', new THREE.Float32BufferAttribute(uv, 2));
  // every triangle facing up (the mirrored x turns some of them over)
  const p = geo.attributes.position.array;
  for (let i = 0; i < p.length; i += 9) {
    const ax = p[i + 3] - p[i], az = p[i + 5] - p[i + 2], bx = p[i + 6] - p[i], bz = p[i + 8] - p[i + 2];
    if (az * bx - ax * bz < 0) for (let c = 0; c < 3; c++) { const t = p[i + 3 + c]; p[i + 3 + c] = p[i + 6 + c]; p[i + 6 + c] = t; }
  }
  geo.computeVertexNormals();
  const mesh = new THREE.Mesh(geo, material);
  mesh.receiveShadow = true;
  return mesh;
}

// Sutherland-Hodgman: an outline (any shape) cut to a box
function clipBox(poly, x0, z0, x1, z1) {
  let out = poly;
  const planes = [[(p) => p[0] >= x0, (a, b) => [x0, a[1] + ((b[1] - a[1]) * (x0 - a[0])) / (b[0] - a[0])]],
    [(p) => p[0] <= x1, (a, b) => [x1, a[1] + ((b[1] - a[1]) * (x1 - a[0])) / (b[0] - a[0])]],
    [(p) => p[1] >= z0, (a, b) => [a[0] + ((b[0] - a[0]) * (z0 - a[1])) / (b[1] - a[1]), z0]],
    [(p) => p[1] <= z1, (a, b) => [a[0] + ((b[0] - a[0]) * (z1 - a[1])) / (b[1] - a[1]), z1]]];
  for (const [inside, cut] of planes) {
    const src = out;
    out = [];
    for (let i = 0; i < src.length; i++) {
      const a = src[(i + src.length - 1) % src.length], b = src[i];
      if (inside(b)) { if (!inside(a)) out.push(cut(a, b)); out.push(b); } else if (inside(a)) out.push(cut(a, b));
    }
    if (!out.length) return out;
  }
  return out;
}

// A block standing on the ground: its outline from bottom to top above the lowest ground under it
function prism(poly, heightAt, bottom, top, material) {
  const g0 = Math.min(...poly.map(([x, z]) => heightAt(x, z)));
  const shape = new THREE.Shape(poly.map(([x, z]) => new THREE.Vector2(-x, -z)));
  const geo = new THREE.ExtrudeGeometry(shape, { depth: top - bottom, bevelEnabled: false });
  geo.rotateX(-Math.PI / 2);  // (the shape's y is -z; extruded upward from the bottom)
  geo.translate(0, bottom, 0);
  const m = new THREE.Mesh(geo, material);
  m.position.y = g0;
  m.castShadow = m.receiveShadow = true;
  return m;
}

// The windmill: a little house astride the lane with the tunnel through it, red roof, four sails
// turning across the tunnel's mouth
function windmill(h, heightAt, spot) {
  const W = h.windmill, cx = h.at[0], z0 = h.at[1] + W.z, z1 = z0 + W.depth, g = new THREE.Group();
  const y = heightAt(cx, (z0 + z1) / 2) - 0.02, HT = 1.5;
  const wall = new THREE.MeshStandardMaterial({ color: 0x5d8fbf, roughness: 0.8 });
  const trim = new THREE.MeshStandardMaterial({ color: 0x7a4a2a, roughness: 0.7 });
  const box = (x0, x1, y0, y1, mat) => {
    const m = new THREE.Mesh(new THREE.BoxGeometry(x1 - x0, y1 - y0, W.depth), mat);
    m.position.copy(T(cx + (x0 + x1) / 2, y + (y0 + y1) / 2, (z0 + z1) / 2));
    m.castShadow = m.receiveShadow = true;
    g.add(m);
  };
  box(-0.9, -W.half, 0, HT, wall);
  box(W.half, 0.9, 0, HT, wall);
  box(-W.half, W.half, 0.26, HT, wall);
  box(-0.95, 0.95, HT, HT + 0.08, trim);
  const roof = new THREE.Mesh(new THREE.ConeGeometry(1.35, 1.0, 4), new THREE.MeshStandardMaterial({ color: 0xb8402e, roughness: 0.7 }));
  roof.rotation.y = Math.PI / 4;
  roof.scale.z = (W.depth + 0.2) / 1.9;
  roof.position.copy(T(cx, y + HT + 0.08 + 0.5, (z0 + z1) / 2));
  roof.castShadow = true;
  g.add(roof);
  for (const s of [-1, 1]) {
    const win = new THREE.Mesh(new THREE.PlaneGeometry(0.28, 0.32), new THREE.MeshStandardMaterial({ color: 0x3b5b7a, roughness: 0.3 }));
    win.position.copy(T(cx + s * 0.5, y + 0.95, z0 - 0.005));
    win.rotation.y = Math.PI;
    g.add(win);
  }
  // sails: hub at the front of the house; each sail reaches to 3 cm off the lane
  const hubY = y + 0.02 + 1.0;
  const pivot = new THREE.Group();
  pivot.position.copy(T(cx, hubY, z0 - 0.1));
  const sailMat = new THREE.MeshStandardMaterial({ color: 0xf6f3ea, roughness: 0.6 });
  const tipMat = new THREE.MeshStandardMaterial({ color: 0xc8402e, roughness: 0.6 });
  for (let k = 0; k < 4; k++) {
    const arm = new THREE.Group();
    arm.rotation.z = (k * Math.PI) / 2;
    const sail = new THREE.Mesh(new THREE.BoxGeometry(0.15, 0.78, 0.02), sailMat);
    sail.position.y = -0.47;
    const tip = new THREE.Mesh(new THREE.BoxGeometry(0.15, 0.12, 0.022), tipMat);
    tip.position.y = -0.91;
    const spar = new THREE.Mesh(new THREE.BoxGeometry(0.03, 0.97, 0.03), trim);
    spar.position.y = -0.485;
    sail.castShadow = tip.castShadow = true;
    arm.add(sail, tip, spar);
    pivot.add(arm);
  }
  const hub = new THREE.Mesh(new THREE.CylinderGeometry(0.07, 0.07, 0.08, 16), trim);
  hub.rotation.x = Math.PI / 2;
  pivot.add(hub);
  g.add(pivot);
  spot.sails = pivot;
  return g;
}

// The tunnel: a stone arch over the lane
function tunnel(h, heightAt) {
  const Tn = h.tunnel, cx = h.at[0], len = Tn.z1 - Tn.z0, outer = 0.82, inner = 0.72;
  const shape = new THREE.Shape();
  for (let k = 0; k <= 24; k++) { const a = Math.PI - (k / 24) * Math.PI; shape[k ? 'lineTo' : 'moveTo'](outer * Math.cos(a), outer * Math.sin(a)); }
  for (let k = 0; k <= 24; k++) { const a = (k / 24) * Math.PI; shape.lineTo(inner * Math.cos(a), inner * Math.sin(a)); }
  const geo = new THREE.ExtrudeGeometry(shape, { depth: len, bevelEnabled: false });
  const m = new THREE.Mesh(geo, new THREE.MeshStandardMaterial({ map: stoneTexture(), roughness: 0.9 }));
  m.position.copy(T(cx, heightAt(cx, h.at[1] + Tn.z0) - 0.04, h.at[1] + Tn.z0));
  m.castShadow = m.receiveShadow = true;
  return m;
}

// A white picket fence round the lawn, open at the front for the way in
function fence(heightAt) {
  const posts = [];
  const [[x0, z0], [x1], [, z1]] = [LAWN[0], LAWN[1], LAWN[2]];
  const sides = [[[x0, z0], [x1, z0]], [[x1, z0], [x1, z1]], [[x1, z1], [x0, z1]], [[x0, z1], [x0, z0]]];
  for (const [a, b] of sides) {
    const len = Math.hypot(b[0] - a[0], b[1] - a[1]), n = Math.round(len / 0.22);
    for (let k = 0; k <= n; k++) {
      const x = a[0] + ((b[0] - a[0]) * k) / n, z = a[1] + ((b[1] - a[1]) * k) / n;
      if (z === z0 && Math.abs(x + 0.75) < 1.6) continue; // the gate
      posts.push([x, z, Math.abs(b[0] - a[0]) > Math.abs(b[1] - a[1])]);
    }
  }
  const im = new THREE.InstancedMesh(new THREE.BoxGeometry(0.07, 0.75, 0.025), new THREE.MeshStandardMaterial({ color: 0xf4f1ea, roughness: 0.7 }), posts.length);
  const m4 = new THREE.Matrix4();
  posts.forEach(([x, z, alongX], k) => {
    m4.makeRotationY(alongX ? 0 : Math.PI / 2).setPosition(-x, heightAt(x, z) + 0.33, z);
    im.setMatrixAt(k, m4);
  });
  im.castShadow = true;
  return im;
}

// Flower beds between the holes
function flowers(heightAt, holes) {
  const spots = [];
  let seed = 5;
  const rnd = () => ((seed = (seed * 16807) % 2147483647) / 2147483647);
  const clear = (x, z) => holes.every((h) => !pointInPoly(h.laneAbs, x, z) && !nearLane(h.laneAbs, x, z, 0.5));
  const beds = [[-7.3, 4], [3.5, 3], [-7, 19.5], [4.5, 20], [-15.2, 12.5], [13.5, 12.5], [-3, 12.2], [8.5, 27.5], [-8, 27.8]];
  for (const [bx, bz] of beds) {
    for (let k = 0; k < 70; k++) {
      const a = rnd() * Math.PI * 2, r = Math.sqrt(rnd()) * 1.1, x = bx + Math.cos(a) * r * 1.3, z = bz + Math.sin(a) * r;
      if (clear(x, z)) spots.push([x, z, rnd()]);
    }
  }
  const PAL = [0xe53950, 0xffd23f, 0xf3f3f3, 0xb04fd8, 0xff7a3d, 0x3f7fe0];
  const g = new THREE.Group();
  const bush = new THREE.InstancedMesh(new THREE.IcosahedronGeometry(0.13, 1), new THREE.MeshStandardMaterial({ color: 0x2f6a2a, roughness: 0.9 }), spots.length);
  const bloom = new THREE.InstancedMesh(new THREE.IcosahedronGeometry(0.05, 0), new THREE.MeshStandardMaterial({ roughness: 0.6 }), spots.length);
  const m4 = new THREE.Matrix4(), c = new THREE.Color();
  spots.forEach(([x, z, r], k) => {
    const y = heightAt(x, z);
    m4.makeScale(1, 0.8, 1).setPosition(-x, y + 0.06, z);
    bush.setMatrixAt(k, m4);
    m4.makeTranslation(-x + (r - 0.5) * 0.08, y + 0.17, z + (r - 0.5) * 0.06);
    bloom.setMatrixAt(k, m4);
    bloom.setColorAt(k, c.set(PAL[Math.floor(r * PAL.length)]));
  });
  bush.castShadow = true;
  g.add(bush, bloom);
  return g;
}
function nearLane(poly, x, z, d) {
  for (const [a, b] of edges(poly)) {
    const dx = b[0] - a[0], dz = b[1] - a[1], l2 = dx * dx + dz * dz || 1;
    const t = Math.max(0, Math.min(1, ((x - a[0]) * dx + (z - a[1]) * dz) / l2));
    if (Math.hypot(x - a[0] - dx * t, z - a[1] - dz * t) < d) return true;
  }
  return false;
}

// ------------------------------------------------------------------ pieces

function add(scene, obj) {
  scene.applyEnvIntensity(obj);
  scene.courseGroup.add(obj);
}

// Hitting mats along the tee line (at these x), the ball teed on the one at 0
function mats(xs, y) {
  const im = new THREE.InstancedMesh(new THREE.BoxGeometry(1.5, 0.012, 1.5), new THREE.MeshStandardMaterial({ color: 0x2a4a2b, roughness: 1, envMapIntensity: 0.2 }), xs.length);
  const m4 = new THREE.Matrix4();
  xs.forEach((x, k) => { m4.makeTranslation(-x, y + 0.002, 0.2); im.setMatrixAt(k, m4); });
  im.receiveShadow = true;
  return im;
}

function flag(colour) {
  const g = new THREE.Group();
  const pole = new THREE.Mesh(new THREE.CylinderGeometry(0.02, 0.02, 2.4, 8), new THREE.MeshStandardMaterial({ color: 0xf2f2f2 }));
  pole.position.y = 1.2;
  const geo = new THREE.PlaneGeometry(0.7, 0.45);
  geo.translate(0.35, 0, 0);
  const cloth = new THREE.Mesh(geo, new THREE.MeshStandardMaterial({ color: colour, side: THREE.DoubleSide, roughness: 0.8 }));
  cloth.position.y = 2.15;
  pole.castShadow = cloth.castShadow = true;
  g.add(pole, cloth);
  return g;
}

// A sign board on two posts: lines of text (the first big), width x height, its bottom edge
// `lift` above the ground; colour: a stripe along the top
function board(lines, w, h, lift, colour = '#2f8f4e') {
  const c = document.createElement('canvas');
  c.width = 512; c.height = Math.round((512 * h) / w);
  const x = c.getContext('2d');
  x.fillStyle = '#16241a'; x.fillRect(0, 0, c.width, c.height);
  x.fillStyle = colour; x.fillRect(0, 0, c.width, c.height * 0.12);
  x.strokeStyle = '#f4f1ea'; x.lineWidth = 10; x.strokeRect(5, 5, c.width - 10, c.height - 10);
  x.fillStyle = '#f4f1ea'; x.textAlign = 'center'; x.textBaseline = 'middle';
  // (the lines stacked in the middle of the board below the stripe: the first big, the rest small)
  const top = c.height * 0.12, body = c.height - top - 10;
  const big = body * (lines.length > 2 ? 0.4 : lines.length > 1 ? 0.5 : 0.62), small = body * (lines.length > 2 ? 0.17 : 0.2);
  const sizes = lines.map((_, i) => (i ? small : big)), total = sizes.reduce((a, b) => a + b * 1.1, 0);
  let y = top + (body - total) / 2 + 5;
  lines.forEach((l, i) => {
    x.font = `${i ? 600 : 800} ${sizes[i]}px system-ui, sans-serif`;
    x.fillText(l, c.width / 2, y + sizes[i] * 0.55, c.width * 0.9);
    y += sizes[i] * 1.1;
  });
  const tex = new THREE.CanvasTexture(c);
  tex.colorSpace = THREE.SRGBColorSpace;
  tex.anisotropy = 8;
  const g = new THREE.Group();
  const face = new THREE.Mesh(new THREE.BoxGeometry(w, h, 0.05), [
    ...Array(4).fill(new THREE.MeshStandardMaterial({ color: 0x16241a })),
    new THREE.MeshStandardMaterial({ map: tex, roughness: 0.7 }),
    new THREE.MeshStandardMaterial({ color: 0x16241a }),
  ]);
  face.position.y = lift + h / 2;
  face.castShadow = true;
  g.add(face);
  const postMat = new THREE.MeshStandardMaterial({ color: 0x5a4632 });
  for (const s of [-1, 1]) {
    const p = new THREE.Mesh(new THREE.BoxGeometry(Math.max(0.05, w * 0.04), lift + h * 0.9, 0.06), postMat);
    p.position.set(s * w * 0.38, (lift + h * 0.9) / 2, -0.05);
    p.castShadow = true;
    g.add(p);
  }
  return g;
}

function feltTexture() {
  const c = document.createElement('canvas');
  c.width = c.height = 256;
  const x = c.getContext('2d'), img = x.createImageData(256, 256);
  for (let i = 0; i < img.data.length; i += 4) {
    const n = (Math.random() - 0.5) * 18;
    img.data[i] = 34 + n; img.data[i + 1] = 128 + n * 1.4; img.data[i + 2] = 58 + n; img.data[i + 3] = 255;
  }
  x.putImageData(img, 0, 0);
  const t = new THREE.CanvasTexture(c);
  t.wrapS = t.wrapT = THREE.RepeatWrapping;
  t.colorSpace = THREE.SRGBColorSpace;
  return t;
}

function stoneTexture() {
  const c = document.createElement('canvas');
  c.width = c.height = 256;
  const x = c.getContext('2d');
  x.fillStyle = '#6f685f'; x.fillRect(0, 0, 256, 256);
  for (let r = 0; r < 8; r++) for (let k = 0; k < 5; k++) {
    const l = 120 + Math.random() * 40;
    x.fillStyle = `rgb(${l},${l - 6},${l - 14})`;
    x.fillRect(((k + (r % 2) * 0.5) * 256) / 5 - 24, r * 32 + 2, 256 / 5 - 4, 28);
  }
  const t = new THREE.CanvasTexture(c);
  t.wrapS = t.wrapT = THREE.RepeatWrapping;
  t.repeat.set(2, 2);
  t.colorSpace = THREE.SRGBColorSpace;
  return t;
}

// ------------------------------------------------------------------ mountains

// The land to the horizon, in the shape of a real course's distant land (lib/farland.js): an
// 80 km square of heights round the practice areas (Int16 metres, rows north to south) and two
// pictures of it. Made up: a wide valley, foothills, and a range of snowy peaks 10-30 km away
// beyond the far end of the range (+z), lower ridges round the sides. Made once per visit.
let mountainJob = null;
export function mountains() {
  mountainJob ||= new Promise((resolve) => setTimeout(() => resolve(buildMountains()), 0));
  return mountainJob;
}

// Smooth value noise in [-1, 1] like course.js's, with an integer hash: the mountains take some
// twenty million samples, and this is several times quicker than hashing with sin
function vnoise(x, z) {
  const xi = Math.floor(x), zi = Math.floor(z), xf = x - xi, zf = z - zi;
  const u = xf * xf * (3 - 2 * xf), v = zf * zf * (3 - 2 * zf);
  const hash = (i, j) => { let h = Math.imul(i, 374761393) + Math.imul(j, 668265263) | 0; h = Math.imul(h ^ (h >>> 13), 1274126177); return ((h ^ (h >>> 16)) >>> 0) / 4294967295; };
  const a = hash(xi, zi), b = hash(xi + 1, zi), c = hash(xi, zi + 1), d = hash(xi + 1, zi + 1);
  return (a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v) * 2 - 1;
}

function buildMountains() {
  const HALF = 40000, N = 1024;
  const mLat = 111320, mLon = 111320 * Math.cos((LAT * Math.PI) / 180);
  const extent = { minLat: LAT - HALF / mLat, maxLat: LAT + HALF / mLat, minLon: LON - HALF / mLon, maxLon: LON + HALF / mLon };
  const geo = { lat: [LAT, 0, 1 / mLat], lon: [LON, 1 / mLon, 0] };
  const ridged = (x, z) => {
    let s = 0, a = 1, f = 1 / 9000, w = 0;
    for (let o = 0; o < 5; o++) {
      const n = 1 - Math.abs(vnoise(x * f + o * 17.3, z * f - o * 9.1));
      s += n * n * a; w += a; a *= 0.5; f *= 2.07;
    }
    return s / w;
  };
  // (a flat valley floor out to a couple of kilometres, wooded foothills, then the peaks)
  const land = (x, z) => {
    const d = Math.hypot(x, z), ahead = z / (d || 1);
    const sector = 0.3 + 0.7 * smooth(-0.3, 0.75, ahead);
    const rise = smooth(7000, 17000, d) * (1 - 0.3 * smooth(26000, 39000, d));
    const peaks = rise * sector * (5400 * ridged(x, z) - 900);
    const hills = smooth(1500, 6500, d) * (190 + 160 * sector) * (vnoise(x / 2200, z / 2200) + 0.6 * vnoise(x / 700 + 4, z / 700) + 1.2);
    return Math.max(0, peaks) + hills;
  };
  const heights = new Int16Array(N * N);
  const cellM = (2 * HALF) / N;
  for (let j = 0; j < N; j++) {
    const z = HALF - (j + 0.5) * cellM;
    for (let i = 0; i < N; i++) heights[j * N + i] = Math.round(land(-HALF + (i + 0.5) * cellM, z));
  }
  // steepness (rise over run) at every height sample
  const slope = new Float32Array(N * N);
  for (let j = 1; j < N - 1; j++) for (let i = 1; i < N - 1; i++) {
    const k = j * N + i;
    slope[k] = Math.hypot(heights[k + 1] - heights[k - 1], heights[k + N] - heights[k - N]) / (2 * cellM);
  }
  const sample = (grid, x, z) => {
    const fi = (x + HALF) / cellM - 0.5, fj = (HALF - z) / cellM - 0.5;
    const i = Math.max(0, Math.min(N - 2, Math.floor(fi))), j = Math.max(0, Math.min(N - 2, Math.floor(fj)));
    const u = Math.min(1, Math.max(0, fi - i)), v = Math.min(1, Math.max(0, fj - j));
    const k = j * N + i;
    return (grid[k] * (1 - u) + grid[k + 1] * u) * (1 - v) + (grid[k + N] * (1 - u) + grid[k + N + 1] * u) * v;
  };
  // Colour from height and steepness: meadows and forest low down, rock above the trees, snow on
  // the tops (not on the steepest faces)
  const paint = (half, side) => {
    const c = document.createElement('canvas');
    c.width = c.height = side;
    const ctx = c.getContext('2d'), img = ctx.createImageData(side, side), D = img.data, res = (2 * half) / side;
    const mix3 = (a, b, t) => [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t];
    const MEADOW = [94, 132, 54], FOREST = [34, 64, 30], ALPINE = [110, 112, 80], ROCK = [112, 106, 100], SNOW = [238, 242, 247];
    for (let j = 0; j < side; j++) {
      const z = half - (j + 0.5) * res;
      for (let i = 0; i < side; i++) {
        const x = -half + (i + 0.5) * res;
        const h = sample(heights, x, z), sl = sample(slope, x, z);
        const n1 = vnoise(x / 700, z / 700), n2 = vnoise(x / 160 + 7, z / 160);
        // (meadows on the valley floor, woods up the slopes and in clumps)
        let col = mix3(MEADOW, FOREST, smooth(-0.2, 0.35, n1 + 0.3 * n2 + h / 900 - 0.15));
        col = mix3(col, ALPINE, smooth(1900, 2500, h + 250 * n1));
        col = mix3(col, ROCK, Math.max(smooth(0.45, 0.9, sl), smooth(2300, 2900, h + 200 * n2)));
        col = mix3(col, SNOW, smooth(2650, 2950, h + 300 * n1 + 120 * n2) * (1 - smooth(0.7, 1.1, sl)));
        const b = 1 + 0.08 * n2 + (res < 10 ? 0.07 * vnoise(x / 35, z / 35) : 0);
        const o = (j * side + i) * 4;
        D[o] = col[0] * b; D[o + 1] = col[1] * b; D[o + 2] = col[2] * b; D[o + 3] = 255;
      }
    }
    ctx.putImageData(img, 0, 0);
    return new Promise((resolve) => c.toBlob((blob) => resolve(URL.createObjectURL(blob)), 'image/jpeg', 0.9));
  };
  const square = (half) => ({ minLat: LAT - half / mLat, maxLat: LAT + half / mLat, minLon: LON - half / mLon, maxLon: LON + half / mLon });
  const layer = (half, side, file) => ({ half, imagery: [{ file, extent: square(half), width: side, height: side }] });
  return Promise.all([paint(6000, 2048), paint(HALF, 2048)]).then(([near, far]) => ({
    half: HALF, extent, w: N, h: N, heights, geo,
    layers: [layer(6000, 2048, 'near'), layer(HALF, 2048, 'far')],
    url: (f) => (f === 'near' ? near : far),
  }));
}
