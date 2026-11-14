// The extra surface masks the Unreal game builds the course's look from, worked out from the export's
// own play data (a port of the old unreal/derive_layers.py, writing the same files in the same
// formats). All on the landscape's square, SIZE pixels a side, row 0
// north, each pixel's centre at x0 + (i + 0.5) * size / SIZE:
//
//   layer_native.png   deep rough and native grass, and the ground under every tree's crown (never on
//                      a fairway, tee, green, fringe, first cut, path, bunker or water): a soft field,
//                      0.5 on the outline
//   layer_path.png     the cart paths, crisp edged; layer_fringe.png the fringes, the same
//   layer_mow.png      on and near the mown surfaces, which way the mower ran (along the nearest hole's
//                      tee -> aims -> green line): red, green = 0.5 + 0.5 x the direction (x east, y south)
//   layer_stream.png   where the water's surface slopes (> STREAM_SLOPE per metre): a soft field
//   boulders.json      stones along the creeks' banks (a few by the ponds), in groups:
//                      [x, y, z (cm: east, south, up from the heightmap's base), size cm, yaw, flat, kind]
//
// Each layer comes back as { file, w, h, channels, data: Uint8Array }; boulders as an array.

export const SIZE = 4096;
const NATIVE = ['deeprough', 'native'];
const KEEP_CLEAR = ['water', 'green', 'fringe', 'fairway', 'tee', 'firstcut', 'path', 'sand'];
const UNDER_CROWN = 0.85;
const SOFT_M = 1.2;
const STREAM_SLOPE = 0.008;
const BOULDER_SPACING_M = { stream: 7.0, pond: 28.0 };
const BOULDER_CHANCE = { stream: 0.5, pond: 0.35 };
const BOULDER_SIZE_M = [0.45, 1.5];
const BOULDER_SMALL = [0, 4];
const BOULDER_SEED = 5;

// surfaces: play.json's surfaces ({ minX, maxZ, res, w, h, codes }) with codes8, its raster;
// trees: play.json's trees; holes: holes.json's (Unreal cm); x0, z1, size: the landscape's
// north-west corner and size (metres); water: { pos, index } of water.glb (metres: x east, y up,
// z south) or null; waterLayer: layer_water.png's bytes (M x M); heights: the heightmap's 16-bit
// values (hn x hn) with heightRange metres; tick: yields to the page
export async function deriveLayers({ surfaces: s, codes8, trees = [], holes = [], x0, z1, size, water, waterLayer, waterM, heights, hn, heightRange, tick = async () => {}, status = () => {} }) {
  const out = [];
  const codes = s.codes;
  const idx = (name) => codes.indexOf(name);
  const table = (names) => { const t = new Uint8Array(256); for (const n of names) if (idx(n) >= 0) t[idx(n)] = 1; return t; };
  const mpp = size / SIZE;
  // each pixel's surface code (255: outside the play data's raster)
  const sub = new Uint8Array(SIZE * SIZE);
  {
    const col = new Int32Array(SIZE);
    for (let i = 0; i < SIZE; i++) {
      const c = Math.floor((x0 + (i + 0.5) * mpp - s.minX) / s.res);
      col[i] = c >= 0 && c < s.w ? c : -1;
    }
    for (let j = 0; j < SIZE; j++) {
      const r = Math.floor((s.maxZ - (z1 - (j + 0.5) * mpp)) / s.res), o = j * SIZE;
      if (r < 0 || r >= s.h) { sub.fill(255, o, o + SIZE); continue; }
      const ro = r * s.w;
      for (let i = 0; i < SIZE; i++) sub[o + i] = col[i] < 0 ? 255 : codes8[ro + col[i]];
    }
  }
  const pick = (t) => { const m = new Float32Array(SIZE * SIZE); for (let q = 0; q < m.length; q++) if (sub[q] !== 255 && t[sub[q]]) m[q] = 1; return m; };
  const bytes = (f) => { const b = new Uint8Array(f.length); for (let q = 0; q < f.length; q++) b[q] = Math.min(255, Math.max(0, Math.floor(f[q] * 255 + 0.5))); return b; };
  const mean = (m) => { let n = 0; for (let q = 0; q < m.length; q++) n += m[q]; return n / m.length; };

  // ---- native grass, and under the trees' crowns
  status('Exporting native grass…');
  await tick();
  {
    const mask = pick(table(NATIVE));
    for (const t of trees) {
      const x = t[0], z = t[1], cone = t[5], b = t[7], c = t[8];
      const r = (cone ? c : b) * UNDER_CROWN;
      if (!(r > 0)) continue;
      const ci = (x - x0) / mpp, cj = (z1 - z) / mpp, rp = r / mpp;
      const i0 = Math.max(0, Math.trunc(ci - rp)), i1 = Math.min(SIZE - 1, Math.trunc(ci + rp) + 1);
      const j0 = Math.max(0, Math.trunc(cj - rp)), j1 = Math.min(SIZE - 1, Math.trunc(cj + rp) + 1);
      if (i0 > i1 || j0 > j1) continue;
      for (let j = j0; j <= j1; j++) for (let i = i0; i <= i1; i++) {
        if ((i + 0.5 - ci) ** 2 + (j + 0.5 - cj) ** 2 <= rp * rp) mask[j * SIZE + i] = 1;
      }
    }
    const clear = table(KEEP_CLEAR);
    for (let q = 0; q < mask.length; q++) if (sub[q] !== 255 && clear[sub[q]]) mask[q] = 0;
    const share = mean(mask);
    const r = Math.max(1, Math.round(SOFT_M / mpp));
    let f = mask;
    for (let k = 0; k < 3; k++) f = boxBlur(f, r);
    out.push({ file: 'layer_native.png', w: SIZE, h: SIZE, channels: 1, data: bytes(f), note: `${(100 * share).toFixed(1)}%` });
  }
  await tick();
  // ---- paths and fringes, crisp edged
  for (const name of ['path', 'fringe']) {
    if (idx(name) < 0) continue;
    let f = pick(table([name]));
    const share = mean(f);
    for (let k = 0; k < 3; k++) f = boxBlur(f, 1);
    out.push({ file: `layer_${name}.png`, w: SIZE, h: SIZE, channels: 1, data: bytes(f), note: `${(100 * share).toFixed(2)}%` });
    await tick();
  }
  // ---- mowing directions
  status('Exporting mowing stripes…');
  await tick();
  out.push(mowLayer(pick(table(['fairway', 'green', 'tee', 'fringe'])), holes, mpp));
  // ---- streams and boulders
  let boulders = null;
  if (water) {
    status('Exporting streams and boulders…');
    await tick();
    const stream = streamField(water, mpp);
    out.push({ file: 'layer_stream.png', w: SIZE, h: SIZE, channels: 1, data: bytes(stream.field), note: `${stream.running} of ${stream.triangles} water triangles run` });
    await tick();
    if (waterLayer) boulders = placeBoulders({ w: waterAt4096(waterLayer, waterM), stream: stream.field, heights, hn, heightRange, size, mpp });
  }
  return { layers: out, boulders };
}

// Box blur, radius r pixels, both ways, edges clamped (derive_layers.py's box_blur)
function boxBlur(a, r) {
  if (r < 1) return a;
  const n = SIZE, k = 2 * r + 1, tmp = new Float32Array(n * n), out = new Float32Array(n * n);
  // along rows (axis 1)
  for (let j = 0; j < n; j++) {
    const o = j * n;
    let s = 0;
    for (let t = -r; t <= r; t++) s += a[o + Math.min(n - 1, Math.max(0, t))];
    for (let i = 0; i < n; i++) {
      tmp[o + i] = s / k;
      s += a[o + Math.min(n - 1, i + r + 1)] - a[o + Math.max(0, i - r)];
    }
  }
  // down columns (axis 0), a row at a time
  const s = new Float64Array(n);
  for (let t = -r; t <= r; t++) { const o = Math.min(n - 1, Math.max(0, t)) * n; for (let i = 0; i < n; i++) s[i] += tmp[o + i]; }
  for (let j = 0; j < n; j++) {
    const o = j * n, add = Math.min(n - 1, j + r + 1) * n, sub = Math.max(0, j - r) * n;
    for (let i = 0; i < n; i++) { out[o + i] = s[i] / k; s[i] += tmp[add + i] - tmp[sub + i]; }
  }
  return out;
}

function mowLayer(mown, holes, mpp) {
  const segs = [];
  for (const h of holes) {
    const tees = Object.values(h.tees || {}).filter(Boolean);
    if (!tees.length || !h.green) continue;
    const g = h.green;
    const tee = tees.reduce((a, t) => ((t.x - g.x) ** 2 + (t.y - g.y) ** 2 > (a.x - g.x) ** 2 + (a.y - g.y) ** 2 ? t : a));
    const pts = [tee, ...(h.aims || []), g];
    for (let k = 0; k + 1 < pts.length; k++) segs.push([pts[k].x / 100, pts[k].y / 100, pts[k + 1].x / 100, pts[k + 1].y / 100]);
  }
  const data = new Uint8Array(SIZE * SIZE * 3);
  for (let q = 0; q < SIZE * SIZE; q++) { data[q * 3] = 255; data[q * 3 + 1] = 128; data[q * 3 + 2] = 128; }
  const layer = { file: 'layer_mow.png', w: SIZE, h: SIZE, channels: 3, data, note: `${segs.length} stretches of ${holes.length} holes` };
  if (!segs.length) return layer;
  // (the mown surfaces and a few metres round them, so the edges have a direction too)
  const near = boxBlur(mown, 6);
  const L = segs.map(([ax, ay, bx, by]) => { const vx = bx - ax, vy = by - ay, l2 = Math.max(vx * vx + vy * vy, 1e-6); return [ax, ay, vx, vy, l2, Math.sqrt(l2)]; });
  // (numpy's round: halves to even)
  const rnd = (v) => { const f = Math.floor(v), d = v - f; return d > 0.5 || (d === 0.5 && f % 2 !== 0) ? f + 1 : f; };
  for (let j = 0; j < SIZE; j++) for (let i = 0; i < SIZE; i++) {
    const q = j * SIZE + i;
    if (!(near[q] > 0)) continue;
    const px = (i + 0.5) * mpp, py = (j + 0.5) * mpp;
    let best = 1e18, dx = 1, dy = 0;
    for (const [ax, ay, vx, vy, l2, l] of L) {
      const t = Math.min(1, Math.max(0, ((px - ax) * vx + (py - ay) * vy) / l2));
      const d2 = (px - ax - t * vx) ** 2 + (py - ay - t * vy) ** 2;
      if (d2 < best) { best = d2; dx = vx / l; dy = vy / l; }
    }
    data[q * 3] = Math.min(255, Math.max(0, rnd(127.5 + 127.5 * dx)));
    data[q * 3 + 1] = Math.min(255, Math.max(0, rnd(127.5 + 127.5 * dy)));
  }
  return layer;
}

function streamField({ pos, index }, mpp) {
  let mask = new Float32Array(SIZE * SIZE), running = 0;
  const mark = (x, z) => {
    const i = Math.min(SIZE - 1, Math.max(0, Math.trunc(x / mpp))), j = Math.min(SIZE - 1, Math.max(0, Math.trunc(z / mpp)));
    mask[j * SIZE + i] = 1;
  };
  const nt = index.length / 3;
  for (let t = 0; t < nt; t++) {
    const a = index[t * 3] * 3, b = index[t * 3 + 1] * 3, c = index[t * 3 + 2] * 3;
    const ux = pos[b] - pos[a], uy = pos[b + 1] - pos[a + 1], uz = pos[b + 2] - pos[a + 2];
    const vx = pos[c] - pos[a], vy = pos[c + 1] - pos[a + 1], vz = pos[c + 2] - pos[a + 2];
    let nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    const len = Math.hypot(nx, ny, nz) + 1e-12;
    nx /= len; ny /= len; nz /= len;
    if (!(Math.hypot(nx, nz) / Math.max(Math.abs(ny), 1e-6) > STREAM_SLOPE)) continue;
    running++;
    mark(pos[a], pos[a + 2]); mark(pos[b], pos[b + 2]); mark(pos[c], pos[c + 2]);
    mark((pos[a] + pos[b] + pos[c]) / 3, (pos[a + 2] + pos[b + 2] + pos[c + 2]) / 3);
  }
  // (the triangles are about a metre: fill between their corners, then soften)
  const b = boxBlur(mask, 2);
  for (let q = 0; q < b.length; q++) mask[q] = b[q] > 0.02 ? 1 : 0;
  let field = mask;
  const r = Math.max(1, Math.round(2.0 / mpp));
  for (let k = 0; k < 3; k++) field = boxBlur(field, r);
  return { field, running, triangles: nt };
}

// layer_water.png's bytes (M x M) on SIZE x SIZE, 0..1 (resized as Pillow's BILINEAR does, when M differs)
function waterAt4096(bytes, M) {
  const w = new Float32Array(SIZE * SIZE);
  if (M === SIZE) { for (let q = 0; q < w.length; q++) w[q] = bytes[q] / 255; return w; }
  const coeffs = (inSize, outSize) => {
    const scale = inSize / outSize, fs = Math.max(1, scale), support = fs, list = [];
    for (let o = 0; o < outSize; o++) {
      const center = (o + 0.5) * scale;
      const lo = Math.max(Math.trunc(center - support + 0.5), 0), hi = Math.min(Math.trunc(center + support + 0.5), inSize);
      const ws = [];
      let sum = 0;
      for (let x = lo; x < hi; x++) { const v = Math.max(0, 1 - Math.abs((x - center + 0.5) / fs)); ws.push(v); sum += v; }
      list.push({ lo, ws: ws.map((v) => (sum ? v / sum : 0)) });
    }
    return list;
  };
  const cx = coeffs(M, SIZE);
  const tmp = new Float32Array(M * SIZE); // (rows of the source, resized across)
  for (let j = 0; j < M; j++) for (let i = 0; i < SIZE; i++) {
    const { lo, ws } = cx[i];
    let s = 0;
    for (let t = 0; t < ws.length; t++) s += bytes[j * M + lo + t] * ws[t];
    tmp[j * SIZE + i] = Math.min(255, Math.max(0, Math.round(s)));
  }
  for (let j = 0; j < SIZE; j++) {
    const { lo, ws } = cx[j];
    for (let i = 0; i < SIZE; i++) {
      let s = 0;
      for (let t = 0; t < ws.length; t++) s += tmp[(lo + t) * SIZE + i] * ws[t];
      w[j * SIZE + i] = Math.min(255, Math.max(0, Math.round(s))) / 255;
    }
  }
  return w;
}

// A seeded random number generator (sfc32, seeded through splitmix32): the same stones every export
function seeded(seed) {
  let s = seed >>> 0;
  const split = () => { s = (s + 0x9e3779b9) >>> 0; let z = s; z = Math.imul(z ^ (z >>> 16), 0x85ebca6b); z = Math.imul(z ^ (z >>> 13), 0xc2b2ae35); return (z ^ (z >>> 16)) >>> 0; };
  let a = split(), b = split(), c = split(), d = split();
  const next = () => {
    const t = (((a + b) | 0) + d) | 0;
    d = (d + 1) | 0; a = b ^ (b >>> 9); b = (c + (c << 3)) | 0; c = (c << 21) | (c >>> 11); c = (c + t) | 0;
    return (t >>> 0) / 4294967296;
  };
  return {
    random: next,
    uniform: (lo, hi) => lo + (hi - lo) * next(),
    integers: (lo, hi) => lo + Math.floor(next() * (hi - lo)), // (hi excluded)
  };
}

function placeBoulders({ w, stream, heights, hn, heightRange, size, mpp }) {
  const rng = seeded(BOULDER_SEED);
  const k = (heightRange * 100) / 65535;
  const height = (xm, zm) => {
    const fx = (xm / size) * (hn - 1), fz = (zm / size) * (hn - 1);
    const i = Math.min(Math.trunc(fx), hn - 2), j = Math.min(Math.trunc(fz), hn - 2), u = fx - i, v = fz - j;
    const h = (a, b) => heights[b * hn + a] * k;
    return h(i, j) * (1 - u) * (1 - v) + h(i + 1, j) * u * (1 - v) + h(i, j + 1) * (1 - u) * v + h(i + 1, j + 1) * u * v;
  };
  // the shore: the water's field near its outline (0.5), a little either side, in a seeded order
  let n = 0;
  for (let q = 0; q < w.length; q++) if (w[q] > 0.42 && w[q] < 0.6) n++;
  const shore = new Int32Array(n);
  n = 0;
  for (let q = 0; q < w.length; q++) if (w[q] > 0.42 && w[q] < 0.6) shore[n++] = q;
  for (let q = n - 1; q > 0; q--) { const r = Math.floor(rng.random() * (q + 1)), t = shore[q]; shore[q] = shore[r]; shore[r] = t; }
  const taken = new Map(), out = [], cell = 4.0;
  const key = (gx, gz) => gx * 65536 + gz;
  const r1 = (v) => Math.round(v * 10) / 10, r2 = (v) => Math.round(v * 100) / 100;
  let groups = 0;
  for (let s = 0; s < n; s++) {
    const q = shore[s], j = Math.floor(q / SIZE), i = q % SIZE;
    const kind = stream[q] > 0.3 ? 'stream' : 'pond';
    if (rng.random() > BOULDER_CHANCE[kind] * 0.02) continue;
    const xm = (i + 0.5) * mpp, zm = (j + 0.5) * mpp;
    const gx = Math.trunc(xm / cell), gz = Math.trunc(zm / cell);
    const sp = BOULDER_SPACING_M[kind], r = Math.ceil(sp / cell);
    let near = false;
    for (let dx = -r; dx <= r && !near; dx++) for (let dz = -r; dz <= r && !near; dz++) {
      for (const [px, pz] of taken.get(key(gx + dx, gz + dz)) || []) if ((px - xm) ** 2 + (pz - zm) ** 2 < sp * sp) { near = true; break; }
    }
    if (near) continue;
    const kk = key(gx, gz);
    if (!taken.has(kk)) taken.set(kk, []);
    taken.get(kk).push([xm, zm]);
    groups++;
    const big = rng.uniform(...BOULDER_SIZE_M) * (kind === 'stream' ? 1.0 : 0.8);
    const group = [[xm, zm, big]];
    const small = rng.integers(BOULDER_SMALL[0], BOULDER_SMALL[1] + 1);
    for (let t = 0; t < small; t++) {
      const ang = rng.uniform(0, 2 * Math.PI), d = rng.uniform(0.5, 1.4) * big + 0.3;
      const sx = xm + Math.cos(ang) * d, sz = zm + Math.sin(ang) * d;
      const pi = Math.trunc(sx / mpp), pj = Math.trunc(sz / mpp);
      if (pi >= 0 && pi < SIZE && pj >= 0 && pj < SIZE && w[pj * SIZE + pi] > 0.36 && w[pj * SIZE + pi] < 0.75) group.push([sx, sz, big * rng.uniform(0.25, 0.6)]);
    }
    for (const [sx, sz, d] of group) {
      // (the pivot is the stone's middle: half of it or a little more below the ground)
      const z = height(sx, sz) - d * 100 * rng.uniform(0.0, 0.12);
      out.push([r1(sx * 100), r1(sz * 100), r1(z), r1(d * 100), r1(rng.uniform(0, 360)), r2(rng.uniform(0.5, 0.85)), rng.integers(0, 6)]);
    }
  }
  out.groups = groups;
  return out;
}
