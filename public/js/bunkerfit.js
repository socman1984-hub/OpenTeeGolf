// Bunkers lined up with the aerial photo. OpenStreetMap's bunker outlines are often the right
// shape but a few metres off (traced from older or differently aligned imagery). Each bunker is
// slid to where it best covers the sand the photo shows, and where the photo shows a clear patch
// of sand there of much the same size, its outline is traced from the photo instead.
// Done once per course (the game or the editor, whichever opens it first with the photo) and kept
// with the course's map data: fitted bunkers are marked { fit: true }, as are any the player draws
// or changes in the editor, which are never moved.

const MAX_SHIFT = 10;   // m: furthest a bunker is moved
const RING = 3;         // m: the grass looked at round each bunker

export const needsBunkerFit = (bunkers) => (bunkers || []).some((f) => !f.hole && !f.fit);

// bunkers: [{ pts, hole?, fit? }] in course metres; photo: { data, w, h, res: 1, minX, maxZ }
// (RGBA, row 0 at maxZ). Returns the new list, every outer bunker marked fit, and how many moved
// or were redrawn.
export function fitBunkers(bunkers, photo) {
  const { data, w, h, minX, maxZ } = photo;
  const cell = (x, z) => [Math.floor(x - minX), Math.floor(maxZ - z)];
  const rgb = (i, j) => { const k = (j * w + i) * 4; return [data[k], data[k + 1], data[k + 2], data[k + 3]]; };
  const outer = bunkers.filter((f) => !f.hole && !f.fit && f.pts.length >= 3);
  if (!outer.length) return { bunkers, moved: 0, traced: 0 };

  // What sand and grass look like on this course: the brightest, palest part of what the map calls
  // bunkers (most of each is sand even when it's off by a few metres), and the ring round them
  const inside = [], ring = [];
  for (const f of outer) {
    const cells = polyCells(f.pts, minX, maxZ, w, h);
    for (const [i, j] of cells.inner) { const c = rgb(i, j); if (c[3]) inside.push(c); }
    for (const [i, j] of cells.ring) { const c = rgb(i, j); if (c[3]) ring.push(c); }
  }
  if (inside.length < 50 || ring.length < 50) return { bunkers, moved: 0, traced: 0 };
  const lum = (c) => 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2];
  inside.sort((a, b) => lum(b) - lum(a));
  const sand = median3(inside.slice(0, Math.max(20, Math.floor(inside.length * 0.35))));
  const grass = median3(ring);
  // (no clear difference - a photo in deep shade, or no sand in the bunkers - leave them be)
  if (lum(sand) - lum(grass) < 25) return { bunkers, moved: 0, traced: 0 };
  // Sand: closer to the sand colour than to the grass, and clearly brighter than the grass
  const dist = (c, r) => Math.hypot(c[0] - r[0], c[1] - r[1], c[2] - r[2]);
  const mask = new Uint8Array(w * h);
  const bright = (lum(sand) + lum(grass)) / 2;
  for (let k = 0; k < w * h; k++) {
    const c = [data[k * 4], data[k * 4 + 1], data[k * 4 + 2]];
    if (data[k * 4 + 3] && lum(c) > bright && dist(c, sand) < dist(c, grass)) mask[k] = 1;
  }
  const at = (i, j) => (i >= 0 && j >= 0 && i < w && j < h ? mask[j * w + i] : 0);

  let moved = 0, traced = 0;
  const done = new Map();
  for (const f of outer) {
    const cells = polyCells(f.pts, minX, maxZ, w, h);
    if (cells.inner.length < 4) { done.set(f, { ...f, fit: true }); continue; }
    // How well the outline fits the sand slid by (di, dj) cells: sand inside less sand round it
    const score = (di, dj) => {
      let a = 0, b = 0;
      for (const [i, j] of cells.inner) a += at(i + di, j + dj);
      for (const [i, j] of cells.ring) b += at(i + di, j + dj);
      return a / cells.inner.length - b / cells.ring.length;
    };
    const s0 = score(0, 0);
    let best = { s: s0, di: 0, dj: 0 };
    for (let dj = -MAX_SHIFT; dj <= MAX_SHIFT; dj++) for (let di = -MAX_SHIFT; di <= MAX_SHIFT; di++) {
      if (di * di + dj * dj > MAX_SHIFT * MAX_SHIFT) continue;
      const s = score(di, dj);
      // (a small penalty per metre, so a flat score doesn't send it wandering)
      if (s - Math.hypot(di, dj) * 0.004 > best.s) best = { s: s - Math.hypot(di, dj) * 0.004, di, dj };
    }
    let pts = f.pts;
    // (only when the photo clearly agrees: mostly sand inside, mostly grass round it, and better
    // than where the map has it)
    if ((best.di || best.dj) && best.s > 0.45 && best.s > s0 + 0.12) {
      pts = f.pts.map(([x, z]) => [r2(x + best.di), r2(z - best.dj)]);
      moved++;
    }
    // The sand itself: the patch under the (moved) outline, if it's about the bunker's size
    const outline = traceSand(pts, mask, w, h, minX, maxZ);
    if (outline) { pts = outline; traced++; }
    done.set(f, { ...f, pts, fit: true });
  }
  return { bunkers: bunkers.map((f) => done.get(f) || f), moved, traced };
}

const r2 = (v) => Math.round(v * 100) / 100;
function median3(list) {
  return [0, 1, 2].map((c) => { const v = list.map((p) => p[c]).sort((a, b) => a - b); return v[v.length >> 1]; });
}

// The cells inside an outline, and those within RING metres outside it
function polyCells(pts, minX, maxZ, w, h) {
  const xs = pts.map((p) => p[0]), zs = pts.map((p) => p[1]);
  const i0 = Math.floor(Math.min(...xs) - minX) - RING - 1, i1 = Math.ceil(Math.max(...xs) - minX) + RING + 1;
  const j0 = Math.floor(maxZ - Math.max(...zs)) - RING - 1, j1 = Math.ceil(maxZ - Math.min(...zs)) + RING + 1;
  const inner = [], ring = [];
  for (let j = Math.max(0, j0); j <= Math.min(h - 1, j1); j++) for (let i = Math.max(0, i0); i <= Math.min(w - 1, i1); i++) {
    const x = minX + i + 0.5, z = maxZ - j - 0.5;
    if (inPoly(pts, x, z)) inner.push([i, j]);
    else if (edgeDist(pts, x, z) <= RING) ring.push([i, j]);
  }
  return { inner, ring };
}
function inPoly(poly, x, z) {
  let ins = false;
  for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) {
    const [xi, zi] = poly[i], [xj, zj] = poly[j];
    if (zi > z !== zj > z && x < ((xj - xi) * (z - zi)) / (zj - zi) + xi) ins = !ins;
  }
  return ins;
}
function edgeDist(poly, x, z) {
  let d = Infinity;
  for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) {
    const [ax, az] = poly[j], [bx, bz] = poly[i], dx = bx - ax, dz = bz - az, l2 = dx * dx + dz * dz;
    const t = l2 ? Math.max(0, Math.min(1, ((x - ax) * dx + (z - az) * dz) / l2)) : 0;
    d = Math.min(d, Math.hypot(x - ax - dx * t, z - az - dz * t));
  }
  return d;
}

// The outline of the sand under a bunker outline: the patch of sand touching its inside (within 3 m
// of it, so a neighbouring bunker or a sandy path isn't swept in), smoothed and traced at the half
// way mark between sand and grass, simplified to points about half a metre apart. Kept only if it
// is much the same size and place as the outline (otherwise the map's shape is the better guess).
function traceSand(pts, mask, w, h, minX, maxZ) {
  const PAD = 4;
  const xs = pts.map((p) => p[0]), zs = pts.map((p) => p[1]);
  const i0 = Math.max(0, Math.floor(Math.min(...xs) - minX) - PAD), i1 = Math.min(w - 1, Math.ceil(Math.max(...xs) - minX) + PAD);
  const j0 = Math.max(0, Math.floor(maxZ - Math.max(...zs)) - PAD), j1 = Math.min(h - 1, Math.ceil(maxZ - Math.min(...zs)) + PAD);
  const W = i1 - i0 + 1, H = j1 - j0 + 1;
  if (W < 4 || H < 4) return null;
  // sand cells allowed: within 3 m of the outline (inside or out); seeded from those inside
  const ok = new Uint8Array(W * H), seen = new Uint8Array(W * H), stack = [];
  let polyArea = 0;
  for (let j = 0; j < H; j++) for (let i = 0; i < W; i++) {
    const x = minX + i0 + i + 0.5, z = maxZ - j0 - j - 0.5, ins = inPoly(pts, x, z);
    if (ins) polyArea++;
    if (!mask[(j0 + j) * w + i0 + i] || !(ins || edgeDist(pts, x, z) <= 3)) continue;
    ok[j * W + i] = 1;
    if (ins) { seen[j * W + i] = 1; stack.push(j * W + i); }
  }
  // the connected patch
  const patch = new Uint8Array(W * H);
  let area = 0, overlap = 0;
  while (stack.length) {
    const k = stack.pop(); patch[k] = 1; area++;
    const i = k % W, j = (k - i) / W;
    if (inPoly(pts, minX + i0 + i + 0.5, maxZ - j0 - j - 0.5)) overlap++;
    for (const [di, dj] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) {
      const ii = i + di, jj = j + dj, kk = jj * W + ii;
      if (ii >= 0 && jj >= 0 && ii < W && jj < H && ok[kk] && !seen[kk]) { seen[kk] = 1; stack.push(kk); }
    }
  }
  // (same size within a half, and mostly the same place)
  if (!polyArea || area < polyArea * 0.6 || area > polyArea * 1.6 || overlap / polyArea < 0.6) return null;
  // a soft copy (3x3 blur, edges padded with grass), traced at 0.5 by marching squares
  const G = W + 2, Hh = H + 2, soft = new Float32Array(G * Hh);
  for (let j = 0; j < H; j++) for (let i = 0; i < W; i++) {
    let s = 0;
    for (let dj = -1; dj <= 1; dj++) for (let di = -1; di <= 1; di++) { const ii = i + di, jj = j + dj; if (ii >= 0 && jj >= 0 && ii < W && jj < H) s += patch[jj * W + ii]; }
    soft[(j + 1) * G + i + 1] = s / 9;
  }
  const ring = largestContour(soft, G, Hh, 0.5);
  if (!ring || ring.length < 6) return null;
  // grid corners → course metres (the soft copy is one cell larger all round)
  const out = simplify(ring.map(([u, v]) => [minX + i0 + u - 1 + 0.5, maxZ - j0 - v + 1 - 0.5]), 0.25);
  return out.length >= 5 ? out.map(([x, z]) => [r2(x), r2(z)]) : null;
}

// Marching squares: the longest closed line where the field crosses the level (points on cell
// edges, in grid units)
function largestContour(f, W, H, level) {
  const segs = new Map(); // start point key -> [end point]
  const key = (p) => `${p[0].toFixed(3)},${p[1].toFixed(3)}`;
  const lerp = (a, b, va, vb) => { const t = (level - va) / (vb - va); return [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t]; };
  for (let j = 0; j < H - 1; j++) for (let i = 0; i < W - 1; i++) {
    const v = [f[j * W + i], f[j * W + i + 1], f[(j + 1) * W + i + 1], f[(j + 1) * W + i]];
    const c = [[i, j], [i + 1, j], [i + 1, j + 1], [i, j + 1]];
    const idx = (v[0] > level ? 1 : 0) | (v[1] > level ? 2 : 0) | (v[2] > level ? 4 : 0) | (v[3] > level ? 8 : 0);
    if (idx === 0 || idx === 15) continue;
    const e = [lerp(c[0], c[1], v[0], v[1]), lerp(c[1], c[2], v[1], v[2]), lerp(c[2], c[3], v[2], v[3]), lerp(c[3], c[0], v[3], v[0])];
    // (edges entered and left, keeping the sand on one side; saddles split the usual way)
    const TABLE = { 1: [[3, 0]], 2: [[0, 1]], 3: [[3, 1]], 4: [[1, 2]], 5: [[3, 0], [1, 2]], 6: [[0, 2]], 7: [[3, 2]], 8: [[2, 3]], 9: [[2, 0]], 10: [[0, 1], [2, 3]], 11: [[2, 1]], 12: [[1, 3]], 13: [[1, 0]], 14: [[0, 3]] };
    for (const [a, b] of TABLE[idx]) segs.set(key(e[a]), e[b]);
  }
  let best = null;
  const used = new Set();
  for (const [k0] of segs) {
    if (used.has(k0)) continue;
    const ring = [];
    let k = k0, p = null;
    while (!used.has(k) && segs.has(k)) {
      used.add(k);
      p = segs.get(k);
      ring.push(p);
      k = key(p);
    }
    if (k === k0 && (!best || ring.length > best.length)) best = ring;
  }
  return best;
}

// Douglas-Peucker on a closed outline
function simplify(pts, tol) {
  const n = pts.length;
  if (n < 6) return pts;
  // split at the two points furthest apart
  let a = 0, b = 0, far = -1;
  for (let i = 0; i < n; i++) { const d = Math.hypot(pts[i][0] - pts[0][0], pts[i][1] - pts[0][1]); if (d > far) { far = d; b = i; } }
  const run = (s, e) => {
    const seg = [];
    for (let i = s; ; i = (i + 1) % n) { seg.push(pts[i]); if (i === e) break; }
    return dp(seg, tol);
  };
  const first = run(a, b), second = run(b, a);
  return [...first.slice(0, -1), ...second.slice(0, -1)];
}
function dp(pts, tol) {
  if (pts.length < 3) return pts;
  const [ax, az] = pts[0], [bx, bz] = pts[pts.length - 1], dx = bx - ax, dz = bz - az, l = Math.hypot(dx, dz) || 1;
  let worst = 0, wi = 0;
  for (let i = 1; i < pts.length - 1; i++) {
    const d = Math.abs((pts[i][0] - ax) * dz - (pts[i][1] - az) * dx) / l;
    if (d > worst) { worst = d; wi = i; }
  }
  if (worst <= tol) return [pts[0], pts[pts.length - 1]];
  return [...dp(pts.slice(0, wi + 1), tol).slice(0, -1), ...dp(pts.slice(wi), tol)];
}
