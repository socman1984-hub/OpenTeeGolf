// Physics parity: the Unreal game's C++ ball physics (unreal/OpenTeeGolf/Source/.../OTGBallPhysics.cpp)
// against the web version's (public/js/physics.js). The same shots on the same formula courses
// (a flat hole and a hilly one: tee, fairway, rough, a bunker, water, fringe, a green and its cup),
// with the same seeded random numbers, through both; every result must match.
//
//   node unreal/tests/physics/parity.mjs
//
// Builds the C++ side with Visual Studio's compiler first (vcvars64.bat) whenever its sources are
// newer than the program.

import fs from 'node:fs';
import path from 'node:path';
import { execFileSync, execSync } from 'node:child_process';
import { fileURLToPath, pathToFileURL } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, '../../..');
const SRC = path.join(ROOT, 'unreal/OpenTeeGolf/Source/OpenTeeGolf');
const OUT = path.join(HERE, 'build');
const EXE = path.join(OUT, 'parity.exe');

// ---- the test courses (keep in step with parity_main.cpp)
const inside = (x, z) => Math.abs(x) <= 400 && z >= -50 && z <= 500;
const courseFor = (hills) => ({
  heightAt: (x, z) => (inside(x, z) ? (hills ? 3 * Math.sin(x / 37) + 2 * Math.cos(z / 23) + 0.04 * z : 0) : null),
  normalAt: (x, z) => {
    if (!hills) return [0, 1, 0];
    const dx = (3 * Math.cos(x / 37)) / 37, dz = (-2 * Math.sin(z / 23)) / 23 + 0.04, l = Math.sqrt(dx * dx + 1 + dz * dz);
    return [-dx / l, 1 / l, -dz / l];
  },
  surfaceAt: (x, z) => {
    if (!inside(x, z)) return null;
    if (z >= 180 && z <= 195 && x >= -400 && x <= -25) return 'water';
    if (Math.hypot(x - 15, z - 240) < 6) return 'sand';
    const g = Math.hypot(x, z - 260);
    if (g < 16) return 'green';
    if (g < 18) return 'fringe';
    if (z < 5 && Math.abs(x) < 4) return 'tee';
    if (Math.abs(x) < 22 && z < 250) return 'fairway';
    return 'rough';
  },
});
const COURSES = [courseFor(false), courseFor(true)];
// three trees on both: a leafy crown, a conifer and a palm (keep in step with parity_main.cpp)
const TREES = (course) => {
  const g = (x, z) => COURSES[course].heightAt(x, z);
  return [
    { x: 12, z: 120, trunkTop: g(12, 120) + 3, trunkR: 0.25, rh: 5, rv: 4, cy: g(12, 120) + 7, kind: 'oak' },
    { x: -10, z: 90, trunkTop: g(-10, 90) + 2, trunkR: 0.2, coneBase: g(-10, 90) + 2, coneH: 12, coneR: 3.5, kind: 'spruce' },
    { x: 3, z: 60, trunkTop: g(3, 60) + 9, trunkR: 0.18, rh: 3, rv: 1.5, cy: g(3, 60) + 10, kind: 'palm' },
  ];
};
const treeSet = (course) => { const list = TREES(course); return { near: (x, z) => list.filter((t) => Math.hypot(t.x - x, t.z - z) < 10) }; };

// ---- the shots: drives, irons, wedges, chips, putts and shots into the trouble, still and windy,
// dry and wet
function shots() {
  const list = [];
  let id = 0;
  const add = (course, from, s, o = {}) => {
    const y = COURSES[course].heightAt(from[0], from[1]);
    const pin = o.pin === false ? null : o.pin || [0, 260];
    list.push({
      id: id++, course, origin: { x: from[0], y, z: from[1] }, shot: { speedMph: s[0], vla: s[1], hla: s[2] || 0, backSpin: s[3] || 0, sideSpin: s[4] || 0 },
      opts: { heading: o.heading ?? Math.atan2(0 - from[0], 260 - from[1]), windMps: o.wind?.[0] || 0, windFromDeg: o.wind?.[1] || 0, wet: o.wet || 0, stimp: o.stimp ?? 10, altitudeM: o.alt || 0, tempC: o.temp ?? 20 },
      pin: pin ? { x: pin[0], y: COURSES[course].heightAt(pin[0], pin[1]), z: pin[1] } : null,
      seed: 12345 + id * 7919, trees: !!o.trees,
    });
  };
  for (const course of [0, 1]) {
    // from the tee
    add(course, [0, 0], [167, 10.9, 0, 2600, 0]);
    add(course, [0, 0], [160, 14, 1.5, 2900, 300], { wind: [6, 200] });
    add(course, [0, 0], [150, 12, -2, 3200, -500], { wind: [8, 90] });
    add(course, [0, 0], [140, 16, 0, 4200, 0], { wet: 0.7 });
    add(course, [0, 0], [120, 18, 0, 5500, 0], { alt: 1500, temp: 30 });
    add(course, [0, 0], [100, 24, 0, 7500, 0]);
    add(course, [0, 0], [130, 9, -6, 2500, -1500]);            // hooked into the water
    add(course, [0, 0], [95, 30, 4, 8500, 600], { wind: [10, 0] });
    // approaches and wedges
    add(course, [5, 120], [105, 21, 0, 6500, 0]);
    add(course, [-8, 150], [88, 26, 0, 7800, 0]);
    add(course, [0, 170], [72, 30, 0, 9000, 0]);
    add(course, [10, 200], [60, 34, 0, 9500, 200]);
    add(course, [0, 215], [45, 38, 0, 9800, 0]);
    // chips and bump-and-runs
    add(course, [0, 236], [25, 22, 0, 4500, 0]);
    add(course, [12, 238], [18, 30, 0, 5000, 0], { heading: 0 });
    add(course, [-20, 255], [15, 12, 0, 1800, 0]);
    // from the rough, the bunker and the fringe
    add(course, [30, 180], [80, 28, 0, 7000, 0]);
    add(course, [15, 240], [35, 40, 0, 8000, 0]);
    add(course, [0, 243], [9, 3, 0, 0, 0]);
    // putts
    for (const [from, mph, st] of [[[0, 252], 5.2, 10], [[3, 250], 7.5, 11], [[-6, 262], 3.8, 9], [[0, 248], 9, 12], [[2, 268], 4.4, 10], [[0, 257], 2.2, 10], [[8, 255], 6, 13]])
      add(course, from, [mph, 0, 0, 0, 0], { stimp: st, heading: Math.atan2(0 - from[0], 260 - from[1]) + 0.01 });
    // no pin
    add(course, [0, 0], [155, 12, 0, 2800, 0], { pin: false });
    // into the water (and short of it)
    add(course, [-60, 120], [62, 28, 0, 6500, 0], { heading: 0 });
    add(course, [-60, 150], [40, 30, 0, 6000, 0], { heading: 0 });
    add(course, [-40, 175], [8, 0, 0, 0, 0], { heading: -0.2 });
    // holed: putts at the speed for a little past, dead centre and off-centre (lip-outs)
    for (const [from, past, off] of [[[0, 252], 0.3, 0], [[0, 249], 0.6, 0], [[4, 256], 0.4, 0.03], [[0, 255], 1.5, 0.045], [[-3, 263], 0.2, 0.02], [[0, 258], 2.5, 0]]) {
      const d = Math.hypot(from[0], 260 - from[1]) + past, mps = puttSpeed(d, 10);
      add(course, from, [mps / 0.44704, 0, 0, 0, 0], { heading: Math.atan2(0 - from[0], 260 - from[1]) + Math.atan2(off, d) });
    }
    // through the trees
    add(course, [0, 0], [95, 9, 0, 3000, 0], { heading: Math.atan2(12, 120), trees: true });
    add(course, [0, 0], [80, 12, 0, 3500, 0], { heading: Math.atan2(-10, 90), trees: true });
    add(course, [0, 0], [70, 15, 0, 4000, 0], { heading: Math.atan2(3, 60), trees: true });
    add(course, [12, 113], [30, 4, 0, 800, 0], { heading: 0, trees: true });
    add(course, [12, 98], [50, 18, 0, 5000, 0], { heading: 0, trees: true });
    add(course, [12, 100], [70, 16, 1, 6000, 0], { heading: 0, trees: true });
    add(course, [-10, 70], [60, 22, 0, 6000, 0], { heading: 0, trees: true });
  }
  return list;
}

// ---- the JS side, with Math.random as the same LCG as the C++ side
const lcg = (seed) => { let s = seed >>> 0; return () => { s = (Math.imul(s, 1664525) + 1013904223) >>> 0; return s / 4294967296; }; };

async function runJs(list) {
  const { simulateShot } = await import(path.join(ROOT, 'public/js/physics.js').replace(/\\/g, '/').replace(/^([A-Za-z]):/, 'file:///$1:'));
  const real = Math.random;
  const out = {};
  try {
    for (const s of list) {
      Math.random = lcg(s.seed);
      const r = simulateShot(COURSES[s.course], s.origin, s.shot, { ...s.opts, wind: { mps: s.opts.windMps, fromDeg: s.opts.windFromDeg }, pin: s.pin, trees: s.trees ? treeSet(s.course) : null });
      const n = r.points.length, q = (k) => r.points[Math.floor((n * k) / 4)];
      out[s.id] = { hits: r.treeHits.join(','), result: r.result, rest: [r.rest.x, r.rest.y, r.rest.z], carry: r.carry, total: r.total, apex: r.apex, flightTime: r.flightTime, duration: r.duration, n, path: [1, 2, 3].map((k) => [q(k).x, q(k).y, q(k).z]) };
    }
  } finally { Math.random = real; }
  return out;
}

// ---- the C++ side
function buildCpp() {
  const sources = [path.join(SRC, 'Private/Physics/OTGBallPhysics.cpp'), path.join(SRC, 'Public/Physics/OTGBallPhysics.h'), path.join(HERE, 'parity_main.cpp')];
  const newest = Math.max(...sources.map((f) => fs.statSync(f).mtimeMs));
  if (fs.existsSync(EXE) && fs.statSync(EXE).mtimeMs > newest) return;
  fs.mkdirSync(OUT, { recursive: true });
  const vswhere = 'C:\\Program Files (x86)\\Microsoft Visual Studio\\Installer\\vswhere.exe';
  const vs = execFileSync(vswhere, ['-latest', '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath']).toString().trim();
  const vcvars = path.join(vs, 'VC/Auxiliary/Build/vcvars64.bat');
  const cmd = `call "${vcvars}" >nul && cl /nologo /std:c++17 /O2 /EHsc /fp:precise /I "${path.join(SRC, 'Public')}" "${sources[0]}" "${sources[2]}" /Fe:"${EXE}" /Fo:"${OUT}\\\\"`;
  console.log('Building the C++ side...');
  execSync(`cmd /d /s /c "${cmd}"`, { stdio: ['ignore', 'pipe', 'inherit'], cwd: OUT });
}

function runCpp(list) {
  const file = path.join(OUT, 'shots.txt');
  const num = (v) => (!Number.isFinite(v) ? '0' : Number.isInteger(v) ? String(v) : v.toPrecision(17));
  fs.writeFileSync(file, list.map((s) => [s.id, s.course, s.origin.x, s.origin.y, s.origin.z, s.shot.speedMph, s.shot.vla, s.shot.hla, s.shot.backSpin, s.shot.sideSpin,
    s.opts.heading, s.opts.windMps, s.opts.windFromDeg, s.opts.wet, s.opts.stimp, s.opts.altitudeM, s.opts.tempC, s.pin ? 1 : 0, s.pin?.x ?? 0, s.pin?.y ?? 0, s.pin?.z ?? 0, s.seed, s.trees ? 1 : 0].map((v) => (typeof v === 'number' ? num(v) : v)).join(' ')).join('\n') + '\n');
  const text = execFileSync(EXE, [file]).toString();
  const out = {};
  for (const line of text.trim().split(/\r?\n/)) {
    const f = line.split(' ');
    const v = f.slice(2).map(Number);
    out[+f[0]] = { hits: f[f.length - 1] === '-' ? '' : f[f.length - 1], result: f[1], rest: v.slice(0, 3), carry: v[3], total: v[4], apex: v[5], flightTime: v[6], duration: v[7], n: v[8], path: [v.slice(9, 12), v.slice(12, 15), v.slice(15, 18)] };
  }
  return out;
}

// ---- compare: the same outcome, the ball within 2 cm, the same distances within 1 cm
const TOL_M = 0.02, TOL_D = 0.01;
const { puttSpeed } = await import(pathToFileURL(path.join(ROOT, 'public/js/physics.js')).href);
globalThis.puttSpeed = puttSpeed;
const list = shots();
const js = await runJs(list);
buildCpp();
const cpp = runCpp(list);
let failed = 0, worst = 0;
for (const s of list) {
  const a = js[s.id], b = cpp[s.id];
  const dist = (p, q) => Math.hypot(p[0] - q[0], p[1] - q[1], p[2] - q[2]);
  const problems = [];
  if (!b) problems.push('no C++ result');
  else {
    if (a.result !== b.result) problems.push(`result ${a.result} vs ${b.result}`);
    if (a.hits !== b.hits) problems.push(`tree hits ${a.hits} vs ${b.hits}`);
    const d = dist(a.rest, b.rest);
    worst = Math.max(worst, d);
    if (d > TOL_M) problems.push(`rest ${d.toFixed(3)} m apart`);
    for (const k of ['carry', 'total', 'apex', 'flightTime']) if (Math.abs(a[k] - b[k]) > TOL_D) problems.push(`${k} ${a[k].toFixed(3)} vs ${b[k].toFixed(3)}`);
    a.path.forEach((p, i) => { const e = dist(p, b.path[i]); if (e > TOL_M) problems.push(`path point ${i + 1} ${e.toFixed(3)} m apart`); });
  }
  const label = `#${s.id} ${s.course ? 'hills' : 'flat '} ${s.shot.speedMph} mph ${s.shot.vla}°`;
  if (problems.length) { failed++; console.log(`FAIL ${label}: ${problems.join(', ')}`); }
  else console.log(`ok   ${label}: ${a.result}, ${a.total.toFixed(1)} m${a.hits ? ' (' + a.hits + ')' : ''}`);
}
console.log(failed ? `${failed} of ${list.length} shots differ` : `all ${list.length} shots match (rest positions within ${(worst * 100).toFixed(2)} cm)`);
process.exit(failed ? 1 : 0);
