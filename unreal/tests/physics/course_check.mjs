// A real course in the Unreal game's physics: an export's play data (exports/<id>/play.json,
// heightmap.png, surfaces.png) read the way the game reads it (OTGPlayCourse), the same shots from
// every hole - drives, approaches, chips and putts - through the JS physics and the game's C++, which
// must agree exactly. Then, for information, how far the play data's course (the export's smoothed
// ground, surfaces every half metre, no buildings) sends the ball from where the web game's own
// course does.
//
//   node unreal/tests/physics/course_check.mjs [course id]

import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import { execFileSync, execSync } from 'node:child_process';
import { fileURLToPath, pathToFileURL } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, '../../..');
const SRC = path.join(ROOT, 'unreal/OpenTeeGolf/Source/OpenTeeGolf');
const OUT = path.join(HERE, 'build');
const EXE = path.join(OUT, 'course.exe');
const ID = process.argv[2] || 'Majestic_Springs_Golf_Club';
const EXP = path.join(ROOT, 'exports', ID);

const { simulateShot, puttSpeed } = await import(pathToFileURL(path.join(ROOT, 'public/js/physics.js')).href);
const { buildCourse } = await import(pathToFileURL(path.join(ROOT, 'public/js/course.js')).href);

// ---- the play data, read as the game reads it (keep in step with OTGPlayCourse.cpp)
const play = JSON.parse(fs.readFileSync(path.join(EXP, 'play.json'), 'utf8'));
const hm = play.heightmap, sf = play.surfaces, b = play.bounds;
const H = readPng(fs.readFileSync(path.join(EXP, hm.file))).data;
const S = readPng(fs.readFileSync(path.join(EXP, sf.file))).data;
const N = hm.samples;
const heightAt = (x, z) => {
  if (x < b.minX || x > b.maxX || z < b.minZ || z > b.maxZ) return null;
  const fi = (x - hm.x0) / hm.metresPerSample, fj = (hm.z1 - z) / hm.metresPerSample;
  if (!(fi >= 0 && fj >= 0 && fi <= N - 1 && fj <= N - 1)) return null;
  const i = Math.min(N - 2, Math.floor(fi)), j = Math.min(N - 2, Math.floor(fj)), u = fi - i, v = fj - j;
  const a = H[j * N + i], bb = H[j * N + i + 1], c = H[(j + 1) * N + i], d = H[(j + 1) * N + i + 1];
  return hm.heightBaseMetres + (((a * (1 - u) + bb * u) * (1 - v) + (c * (1 - u) + d * u) * v) / 65535) * hm.heightRangeMetres;
};
const dataCourse = {
  heightAt,
  normalAt: (x, z) => {
    const e = 1, hl = heightAt(x - e, z), hr = heightAt(x + e, z), hd = heightAt(x, z - e), hu = heightAt(x, z + e);
    if (hl === null || hr === null || hd === null || hu === null) return [0, 1, 0];
    const nx = hl - hr, nz = hd - hu, ny = 2 * e, l = Math.hypot(nx, ny, nz);
    return [nx / l, ny / l, nz / l];
  },
  surfaceAt: (x, z) => {
    if (x < b.minX || x > b.maxX || z < b.minZ || z > b.maxZ) return null;
    const i = Math.floor((x - sf.minX) / sf.res), j = Math.floor((sf.maxZ - z) / sf.res);
    if (i < 0 || j < 0 || i >= sf.w || j >= sf.h) return null;
    const c = S[j * sf.w + i];
    return c === 0 ? null : sf.codes[c] || 'rough';
  },
};
// trees, as the game's TreeIndex and OTGPlayTrees bucket them
const trees = play.trees.map((t) => (t[5]
  ? { x: t[0], z: t[1], trunkTop: t[2], trunkR: t[3], kind: t[4], coneBase: t[6], coneH: t[7], coneR: t[8] }
  : { x: t[0], z: t[1], trunkTop: t[2], trunkR: t[3], kind: t[4], cy: t[6], rh: t[7], rv: t[8] }));
const index = new Map(), key = (i, j) => i * 100003 + j;
for (const t of trees) {
  const reach = Math.max(t.coneBase != null ? t.coneR : t.rh, t.trunkR, 0);
  for (let i = Math.floor((t.x - reach) / 10); i <= Math.floor((t.x + reach) / 10); i++)
    for (let j = Math.floor((t.z - reach) / 10); j <= Math.floor((t.z + reach) / 10); j++) {
      if (!index.has(key(i, j))) index.set(key(i, j), []);
      index.get(key(i, j)).push(t);
    }
}
const treeIndex = { near: (x, z) => index.get(key(Math.floor(x / 10), Math.floor(z / 10))) || [] };

// ---- shots from every hole: a drive, an approach from the middle, a chip and a putt
const shots = [];
const at = (x, z) => ({ x, y: heightAt(x, z), z });
const towards = (a, p) => Math.atan2(p.x - a.x, p.z - a.z);
for (const h of play.holes) {
  const pin = Object.values(h.pins)[0] || h.greenCenter;
  const tee = Object.values(h.tees)[0];
  if (!pin || !tee || !h.greenCenter) continue;
  const P = at(pin.x, pin.z);
  const add = (from, s, extra = {}) => {
    if (!Number.isFinite(from.y)) return;
    shots.push({ id: shots.length, hole: h.number, origin: from, shot: { speedMph: s[0], vla: s[1], hla: s[2] || 0, backSpin: s[3] || 0, sideSpin: s[4] || 0 }, heading: extra.heading ?? towards(from, P), pin: P, seed: 777 + shots.length * 131, trees: extra.trees !== false });
  };
  const T = at(tee.x, tee.z);
  const d = Math.hypot(P.x - T.x, P.z - T.z);
  add(T, h.par === 3 ? [Math.min(140, 60 + d * 0.45), 18, 0, 6000, 0] : [155, 12, -1, 2700, -200]);
  add(T, [148, 13, 3, 2900, 700]);
  const mid = at(T.x + (P.x - T.x) * 0.6, T.z + (P.z - T.z) * 0.6);
  add(mid, [90, 24, 0, 7000, 0]);
  const g = h.greenCenter, chip = at(g.x + (T.x - g.x) * (12 / d), g.z + (T.z - g.z) * (12 / d) + 6);
  add(chip, [24, 26, 0, 5000, 0]);
  const on = at(P.x + (T.x - P.x) * (7 / d), P.z + (T.z - P.z) * (7 / d));
  if (dataCourse.surfaceAt(on.x, on.z) === 'green') add(on, [puttSpeed(7.3, play.defaultStimp) / 0.44704, 0, 0, 0, 0]);
}

const run = (course, s, withTrees) => {
  const real = Math.random;
  let st = s.seed >>> 0;
  Math.random = () => { st = (Math.imul(st, 1664525) + 1013904223) >>> 0; return st / 4294967296; };
  try {
    return simulateShot(course, s.origin, s.shot, { heading: s.heading, stimp: play.defaultStimp, altitudeM: play.altitudeM, pin: s.pin, trees: withTrees && s.trees ? treeIndex : null });
  } finally { Math.random = real; }
};

// ---- the C++ side
function build() {
  const sources = [path.join(SRC, 'Private/Physics/OTGBallPhysics.cpp'), path.join(SRC, 'Private/Physics/OTGPlayCourse.cpp'), path.join(HERE, 'course_main.cpp')];
  const newest = Math.max(...[...sources, path.join(SRC, 'Public/Physics/OTGBallPhysics.h'), path.join(SRC, 'Public/Physics/OTGPlayCourse.h')].map((f) => fs.statSync(f).mtimeMs));
  if (fs.existsSync(EXE) && fs.statSync(EXE).mtimeMs > newest) return;
  fs.mkdirSync(OUT, { recursive: true });
  const vs = execFileSync('C:\\Program Files (x86)\\Microsoft Visual Studio\\Installer\\vswhere.exe', ['-latest', '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath']).toString().trim();
  console.log('Building the C++ side...');
  execSync(`cmd /d /c "call "${path.join(vs, 'VC/Auxiliary/Build/vcvars64.bat')}" >nul 2>nul && cl /nologo /std:c++17 /O2 /EHsc /fp:precise /I "${path.join(SRC, 'Public')}" ${sources.map((f) => `"${f}"`).join(' ')} /Fe:course.exe"`, { cwd: OUT, stdio: ['ignore', 'ignore', 'inherit'] });
}
function runCpp() {
  const head = new Float64Array([N, hm.x0, hm.z1, hm.metresPerSample, hm.heightBaseMetres, hm.heightRangeMetres, sf.w, sf.h, sf.minX, sf.maxZ, sf.res, b.minX, b.maxX, b.minZ, b.maxZ]);
  fs.writeFileSync(path.join(OUT, 'course.bin'), Buffer.concat([Buffer.from(head.buffer), Buffer.from(new Uint16Array(H).buffer), Buffer.from(S)]));
  fs.writeFileSync(path.join(OUT, 'trees.txt'), play.trees.map((t) => [t[0], t[1], t[2], t[3], t[4] || '-', t[5], t[6], t[7], t[8]].join(' ')).join('\n') + '\n');
  const num = (v) => (Number.isInteger(v) ? String(v) : v.toPrecision(17));
  fs.writeFileSync(path.join(OUT, 'shots.txt'), shots.map((s) => [s.id, s.origin.x, s.origin.y, s.origin.z, s.shot.speedMph, s.shot.vla, s.shot.hla, s.shot.backSpin, s.shot.sideSpin,
    s.heading, play.defaultStimp, play.altitudeM, 1, s.pin.x, s.pin.y, s.pin.z, s.seed, s.trees ? 1 : 0].map(num).join(' ')).join('\n') + '\n');
  const out = {};
  for (const line of execFileSync(EXE, [OUT]).toString().trim().split(/\r?\n/)) {
    const f = line.split(' ');
    out[+f[0]] = { result: f[1], rest: f.slice(2, 5).map(Number), carry: +f[5], total: +f[6], hits: f[7] === '-' ? '' : f[7] };
  }
  return out;
}

build();
const cpp = runCpp();
let failed = 0, worst = 0;
for (const s of shots) {
  const a = run(dataCourse, s, true), c = cpp[s.id];
  const d = Math.hypot(a.rest.x - c.rest[0], a.rest.y - c.rest[1], a.rest.z - c.rest[2]);
  worst = Math.max(worst, d);
  if (a.result !== c.result || d > 0.02 || a.treeHits.join(',') !== c.hits) {
    failed++;
    console.log(`FAIL hole ${s.hole} #${s.id}: ${a.result} vs ${c.result}, ${d.toFixed(3)} m apart, trees ${a.treeHits.join(',') || '-'} vs ${c.hits || '-'}`);
  }
}
console.log(failed ? `${failed} of ${shots.length} shots differ between JS and C++` : `JS and C++ agree on all ${shots.length} shots on ${play.name} (within ${(worst * 100).toFixed(2)} cm)`);

// ---- for information: the play data's course against the web game's own (no trees on either)
const f = path.join(ROOT, 'courses', ID, ID + '.course.json');
if (fs.existsSync(f)) {
  const game = buildCourse(JSON.parse(fs.readFileSync(f, 'utf8')), fs.existsSync(f.replace(/\.course\.json$/, '.real.json')) ? JSON.parse(fs.readFileSync(f.replace(/\.course\.json$/, '.real.json'), 'utf8')) : null);
  const diffs = [], outcome = [];
  for (const s of shots) {
    const o = { ...s.origin, y: game.heightAt(s.origin.x, s.origin.z) };
    const a = run(dataCourse, s, false), g = simulateShot(game, o, s.shot, { heading: s.heading, stimp: play.defaultStimp, altitudeM: play.altitudeM, pin: { ...s.pin, y: game.heightAt(s.pin.x, s.pin.z) } });
    diffs.push(Math.hypot(a.rest.x - g.rest.x, a.rest.z - g.rest.z));
    if (a.result !== g.result) outcome.push(`hole ${s.hole} #${s.id} ${g.result} -> ${a.result}`);
  }
  diffs.sort((x, y) => x - y);
  const q = (k) => diffs[Math.min(diffs.length - 1, Math.floor(diffs.length * k))].toFixed(2);
  console.log(`Play data vs the web game's course: the ball finishes a median ${q(0.5)} m from where the web game puts it (90% within ${q(0.9)} m, worst ${q(1)} m); different outcome on ${outcome.length} of ${shots.length}${outcome.length ? ': ' + outcome.slice(0, 6).join(', ') : ''}`);
}
process.exit(failed ? 1 : 0);

// A greyscale PNG, 8 or 16 bits
function readPng(buf) {
  let o = 8, w = 0, h = 0, depth = 0;
  const idat = [];
  while (o < buf.length) {
    const len = buf.readUInt32BE(o), kind = buf.toString('ascii', o + 4, o + 8), body = buf.subarray(o + 8, o + 8 + len);
    if (kind === 'IHDR') { w = body.readUInt32BE(0); h = body.readUInt32BE(4); depth = body[8]; }
    else if (kind === 'IDAT') idat.push(body);
    else if (kind === 'IEND') break;
    o += 12 + len;
  }
  const bpp = depth / 8, row = w * bpp, raw = zlib.inflateSync(Buffer.concat(idat));
  const data = depth === 16 ? new Uint16Array(w * h) : new Uint8Array(w * h);
  const prev = Buffer.alloc(row), cur = Buffer.alloc(row);
  for (let j = 0; j < h; j++) {
    const ft = raw[j * (row + 1)], src = raw.subarray(j * (row + 1) + 1, (j + 1) * (row + 1));
    for (let i = 0; i < row; i++) {
      const a = i >= bpp ? cur[i - bpp] : 0, up = prev[i], c = i >= bpp ? prev[i - bpp] : 0;
      let pr = 0;
      if (ft === 1) pr = a; else if (ft === 2) pr = up; else if (ft === 3) pr = (a + up) >> 1;
      else if (ft === 4) { const p = a + up - c, pa = Math.abs(p - a), pb = Math.abs(p - up), pc = Math.abs(p - c); pr = pa <= pb && pa <= pc ? a : pb <= pc ? up : c; }
      cur[i] = (src[i] + pr) & 255;
    }
    for (let i = 0; i < w; i++) data[j * w + i] = depth === 16 ? (cur[i * 2] << 8) | cur[i * 2 + 1] : cur[i];
    cur.copy(prev);
  }
  return { w, h, data };
}
