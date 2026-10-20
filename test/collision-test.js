// Checks the Jolt collision world against the game's own terrain and the calibrated physics.
// Usage: node test/collision-test.js [courses/<name>/<name>.course.json]   (a real course is optional)
import fs from 'node:fs';
import { loadJolt, CollisionWorld } from '../public/js/collision.js';
import { simulateShot } from '../public/js/physics.js';

let failed = 0;
const check = (ok, msg) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${msg}`); if (!ok) failed++; };

// A rolling synthetic course with the same triangle split as course.js / the terrain mesh
function syntheticCourse() {
  const nx = 301, nz = 401, cell = 2, minX = -300, minZ = -100;
  const heights = new Float32Array(nx * nz);
  for (let j = 0; j < nz; j++) for (let i = 0; i < nx; i++) {
    const x = minX + i * cell, z = minZ + j * cell;
    heights[j * nx + i] = 4 * Math.sin(x / 37) + 3 * Math.cos(z / 23) + 0.02 * z;
  }
  const heightAt = (x, z) => {
    const fx = (x - minX) / cell, fz = (z - minZ) / cell;
    if (fx < 0 || fz < 0 || fx >= nx - 1 || fz >= nz - 1) return null;
    const i = Math.floor(fx), j = Math.floor(fz), u = fx - i, v = fz - j, k = j * nx + i;
    const a = heights[k], b = heights[k + 1], c = heights[k + nx], d = heights[k + nx + 1];
    return u + v <= 1 ? a + (b - a) * u + (c - a) * v : d + (c - d) * (1 - u) + (b - d) * (1 - v);
  };
  const normalAt = (x, z) => {
    const e = 1, hl = heightAt(x - e, z), hr = heightAt(x + e, z), hd = heightAt(x, z - e), hu = heightAt(x, z + e);
    if ([hl, hr, hd, hu].includes(null)) return [0, 1, 0];
    const a = hl - hr, b = 2 * e, c = hd - hu, l = Math.hypot(a, b, c);
    return [a / l, b / l, c / l];
  };
  return {
    grid: { nx, nz, cell, heights }, bounds: { minX, maxX: minX + (nx - 1) * cell, minZ, maxZ: minZ + (nz - 1) * cell },
    heightAt, normalAt, surfaceAt: (x, z) => (heightAt(x, z) === null ? null : 'fairway'),
  };
}

function compareGround(course, world, label) {
  let worst = 0, missing = 0;
  const { minX, maxX, minZ, maxZ } = course.bounds;
  for (let k = 0; k < 4000; k++) {
    const x = minX + 1 + Math.random() * (maxX - minX - 2), z = minZ + 1 + Math.random() * (maxZ - minZ - 2);
    const h = course.heightAt(x, z), g = world.groundAt(x, z);
    if (h === null) continue;
    if (g === null) { missing++; continue; }
    worst = Math.max(worst, Math.abs(h - g));
  }
  check(missing === 0 && worst < 0.03, `${label}: Jolt ground matches the terrain (worst ${(worst * 100).toFixed(1)} cm, ${missing} misses)`);
}

const Jolt = await loadJolt();
const flat = syntheticCourse();

// 1. Terrain heights line up triangle for triangle
let t = Date.now();
const world = new CollisionWorld(Jolt, flat, {
  trees: [{ x: 0, z: 40, trunkR: 0.3, trunkTop: flat.heightAt(0, 40) + 3 }],
  buildings: [{ h: 8, pts: [[-170, 300], [-130, 300], [-130, 320], [-170, 320]] }],
});
console.log(`world built in ${Date.now() - t} ms`);
compareGround(flat, world, 'synthetic course');

// 2. Same carry and roll with and without Jolt on open ground
const origin = { x: 100, y: flat.heightAt(100, 0), z: 0 };
const shot = { speedMph: 150, vla: 12, hla: 0, backSpin: 2700, sideSpin: 0 };
const opts = { heading: 0, altitudeM: 0, stimp: 10 };
const plain = simulateShot(flat, origin, shot, opts);
const jolt = simulateShot(flat, origin, shot, { ...opts, collider: world });
check(Math.abs(plain.carry - jolt.carry) < 0.5 && Math.abs(plain.total - jolt.total) < 1.5,
  `drive: carry ${plain.carry.toFixed(1)} / ${jolt.carry.toFixed(1)} m, total ${plain.total.toFixed(1)} / ${jolt.total.toFixed(1)} m (without / with Jolt)`);

// 3. A trunk straight down the line knocks the ball off its line (on this rolling ground it then
// runs on downhill, so where it ends depends on the slopes: it just mustn't end where it would
// have without the tree)
const lowShot = { speedMph: 80, vla: 3, hla: 0, backSpin: 1500, sideSpin: 0 }, lowFrom = { x: 0, y: flat.heightAt(0, 0), z: 0 };
const toTree = simulateShot(flat, lowFrom, lowShot, { ...opts, collider: world });
const noTree = simulateShot(flat, lowFrom, lowShot, { ...opts, collider: new CollisionWorld(Jolt, flat, {}) });
const moved = Math.hypot(toTree.rest.x - noTree.rest.x, toTree.rest.z - noTree.rest.z);
check(toTree.treeHits.includes('trunk') && moved > 5, `low shot into a trunk is knocked off its line (ends ${moved.toFixed(1)} m from where it would without the tree, hits: ${toTree.treeHits.join(', ')})`);

// 4. A building wall stops a low runner
const toWall = simulateShot(flat, { x: -150, y: flat.heightAt(-150, 280), z: 280 }, { speedMph: 60, vla: 3, hla: 0, backSpin: 500, sideSpin: 0 }, { ...opts, collider: world });
check(toWall.treeHits.includes('building') && toWall.rest.z < 300, `ball runs into a building and stays out (rest z = ${toWall.rest.z.toFixed(1)})`);

// 5. The flagstick
const pin = { x: 150, y: flat.heightAt(150, 20), z: 20 };
const atPole = simulateShot(flat, { x: 150, y: flat.heightAt(150, 0), z: 0 }, { speedMph: 60, vla: 4, hla: 0, backSpin: 800, sideSpin: 0 }, { ...opts, pin, collider: world });
check(atPole.treeHits.includes('pole') || atPole.result === 'holed', `low shot at the pin hits the flagstick (${atPole.result}, hits: ${atPole.treeHits.join(', ') || 'none'})`);
world.dispose();

// 6. Optional: a real course from the builder
const file = process.argv[2];
if (file && fs.existsSync(file)) {
  const { buildCourse } = await import('../public/js/course.js');
  const data = JSON.parse(fs.readFileSync(file, 'utf8'));
  const realFile = file.replace(/\.course\.json$/, '.real.json');
  const course = buildCourse(data, fs.existsSync(realFile) ? JSON.parse(fs.readFileSync(realFile, 'utf8')) : null);
  t = Date.now();
  const w2 = new CollisionWorld(Jolt, course);
  console.log(`${data.CourseName}: world built in ${Date.now() - t} ms (${course.grid.nx} × ${course.grid.nz} grid)`);
  compareGround(course, w2, data.CourseName);
  w2.dispose();
}

if (failed) { console.log(`${failed} check(s) failed`); process.exit(1); }
console.log('all collision checks passed');
