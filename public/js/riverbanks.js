// Rivers and streams mapped in OpenStreetMap as a centre line only become ribbons of one width
// (the mapped width tag, or a typical one: 12 m for a river). Real rivers are rarely that: the Bow
// at Banff is 40-80 m across, with gravel bars and a channel that wanders off the mapped line.
// Here each ribbon is fitted to the water in the aerial photo: every 2 m down the river, the
// photo is read across the channel, out each side from the centre, until it stops looking like
// the river's own water; the banks found are then smoothed so the outline doesn't jitter.
// Where the photo can't tell (a bridge, deep shade, trees hanging over), the mapped width stays.
// A side channel beyond a gravel bar or an island becomes a stream of its own. Banks stop at any
// other water (a pond beside the river, another river), and a stream ends where it runs into one,
// so no two water surfaces overlap.

import { streamChains } from './streams.js';

const STEP = 2;            // metres between cross-sections down the river
const SCAN = 0.5;          // metres between photo samples across it
const MAX_OUT = 110;       // furthest a bank is looked for from the centre line (m)
const DRY = 3;             // this many metres of not-water in a row is the bank
const SIDE = 60;           // how far past a bank (over a gravel bar, an island) a side channel is looked for

const median = (a) => { const s = [...a].sort((x, y) => x - y); return s.length ? s[s.length >> 1] : NaN; };

// Streams in a water list still to be fitted
export const needsBanks = (water) => (water || []).some((f) => f.flow && !f.hole && !f.banks);

// photo: { data (RGBA), w, h, res, minX, maxZ }. water: the course's water features. Returns the
// list with every stream ribbon looked at (marked banks: true, refitted where the photo shows the
// water clearly; a stream it can't make out keeps its mapped width), or null if there were none.
export function fitRiverBanks(water, photo) {
  const { chains, rest } = streamChains(water);
  if (!chains.length) return null;
  const { data, w, h, res, minX, maxZ } = photo;
  const px = (x, z) => {
    const i = Math.floor((x - minX) / res), j = Math.floor((maxZ - z) / res);
    if (i < 0 || j < 0 || i >= w || j >= h) return null;
    const o = (j * w + i) * 4;
    return data[o + 3] ? [data[o], data[o + 1], data[o + 2]] : null;
  };
  const out = [...rest], fits = [];
  let fitted = 0;
  for (const c of chains) {
    if (c.banks) { out.push(...c.pieces); continue; } // (fitted already)
    const { pts, hw, nrm } = c, n = pts.length;
    // The river's own colour near each cross-section: the middle of the mapped channel over
    // 30 m up and down stream (most of it is on the water even where the line wanders)
    const core = pts.map((p, i) => {
      const r = Math.min(4, hw[i] * 0.6), cols = [];
      for (let d = -r; d <= r; d += 1) {
        const q = px(p[0] + nrm[i][0] * d, p[1] + nrm[i][1] * d);
        if (q) cols.push(q);
      }
      return cols;
    });
    const ref = pts.map((_, i) => {
      const cols = [];
      for (let k = Math.max(0, i - 7); k <= Math.min(n - 1, i + 7); k++) cols.push(...core[k]);
      if (cols.length < 6) return null;
      const m = [0, 1, 2].map((ch) => median(cols.map((q) => q[ch])));
      // how much the water itself varies (ripples, glints): the tolerance grows with it
      const spread = median(cols.map((q) => Math.hypot(q[0] - m[0], q[1] - m[1], q[2] - m[2])));
      return { m, tol: Math.min(46, Math.max(20, spread * 3.2)) };
    });
    const far = (q, m) => Math.hypot(q[0] - m[0], q[1] - m[1], q[2] - m[2]);
    // Water: like the river's own colour, or (shallows shade off into a lighter, greener colour
    // over the gravel) like the water just crossed and not too unlike the river's
    const wet = (q, r, run) => q && (far(q, r.m) < r.tol || (run && far(q, run) < r.tol * 0.7 && far(q, r.m) < r.tol * 2.2));
    const mix = (run, q) => (run ? run.map((v, k) => v * 0.8 + q[k] * 0.2) : [...q]);
    // Out from the centre on one side (s = +1 left, -1 right): distance to the bank (or NaN), and
    // a side channel past it as [inner, outer] distances (or null)
    const reach = (i, s) => {
      const r = ref[i];
      if (!r) return [NaN, null];
      const [x, z] = pts[i], [nx, nz] = nrm[i];
      let last = -1, dry = 0, seen = 0, run = null, d = 0;
      for (; d <= MAX_OUT; d += SCAN) {
        const q = px(x + nx * d * s, z + nz * d * s);
        if (!q) break;
        if (wet(q, r, run)) { last = d; dry = 0; seen++; run = mix(run, q); } else if ((dry += SCAN) >= DRY) break;
      }
      // (nothing like the river's water at the line itself: the photo can't tell here)
      if (seen < 2) return [NaN, null];
      const bank = last + SCAN / 2;
      // A side channel: at least 6 m of the river's own colour again (strictly: shade under the
      // trees on the far bank mustn't pass for water), within SIDE of the bank
      let a = -1, b = -1;
      dry = 0;
      for (d = bank + DRY; d <= bank + SIDE; d += SCAN) {
        const q = px(x + nx * d * s, z + nz * d * s);
        if (!q) break;
        if (far(q, r.m) < r.tol * 0.85) { if (a < 0) a = d; b = d; dry = 0; } else if (a >= 0 && (dry += SCAN) >= DRY) break;
      }
      return [bank, a >= 0 && b - a >= 6 ? [a, b + SCAN / 2] : null];
    };
    const Ls = pts.map((_, i) => reach(i, 1)), Rs = pts.map((_, i) => reach(i, -1));
    let L = Ls.map((v) => v[0]), R = Rs.map((v) => v[0]);
    const good = L.filter((v, i) => Number.isFinite(v) && Number.isFinite(R[i])).length;
    // Most of the river has to read clearly for the photo to be trusted at all
    fitted++;
    if (good < n * 0.4) { out.push(...c.pieces.map((f) => ({ ...f, banks: true }))); continue; }
    // (a stream only a few metres wide: whatever the photo shows next to it - grass the same
    // colour as a weedy ditch - it doesn't grow into a river)
    const cap = (i) => (hw[i] <= 3 ? hw[i] * 3 : MAX_OUT);
    // Gaps (a bridge, shade) take the mapped half width; then a running median (over 30 m)
    // throws out single odd readings, and an average (over 20 m) rounds the banks off
    const fill = (A) => A.map((v, i) => (Number.isFinite(v) ? v : hw[i]));
    const runMedian = (A, r) => A.map((_, i) => median(A.slice(Math.max(0, i - r), i + r + 1)));
    const runMean = (A, r) => A.map((_, i) => { const s = A.slice(Math.max(0, i - r), i + r + 1); return s.reduce((a, b) => a + b, 0) / s.length; });
    L = runMean(runMedian(fill(L), 7), 5).map((v, i) => Math.min(v, cap(i)));
    R = runMean(runMedian(fill(R), 7), 5).map((v, i) => Math.min(v, cap(i)));
    // (side channels only beside a river, not a narrow stream)
    fits.push({ c, L, R, side: hw.some((v) => v > 3) ? [sides(Ls, 1), sides(Rs, -1)] : [] });
  }
  if (!fitted) return null;

  // Other water, drawn at 1 m: ponds and lakes, and the rivers already laid down
  // (the longest river first: those after it stop at its banks)
  fits.sort((a, b) => b.c.pts.length - a.c.pts.length);
  const done = [];
  const others = () => {
    const cv = document.createElement('canvas');
    cv.width = w; cv.height = h;
    const g = cv.getContext('2d', { willReadFrequently: true });
    g.setTransform(1 / res, 0, 0, -1 / res, -minX / res, maxZ / res);
    g.fillStyle = '#fff';
    const fill = (poly) => { g.beginPath(); poly.forEach(([x, z], k) => (k ? g.lineTo(x, z) : g.moveTo(x, z))); g.closePath(); g.fill(); };
    for (const f of out) if (!f.hole) fill(f.pts);
    for (const f of done) fill(f.pts);
    const a = g.getImageData(0, 0, w, h).data;
    return (x, z) => {
      const i = Math.floor((x - minX) / res), j = Math.floor((maxZ - z) / res);
      return i >= 0 && j >= 0 && i < w && j < h && a[(j * w + i) * 4 + 3] > 127;
    };
  };
  for (const o of fits) {
    const { c: { pts, nrm }, L, R } = o;
    const wetElse = others();
    // Banks pulled in to where other water starts; a cross-section that starts in other water is
    // left out (the stream has run into it)
    const keep = pts.map(([x, z], i) => {
      if (wetElse(x, z)) return false;
      for (const [A, s] of [[L, 1], [R, -1]]) {
        for (let d = SCAN; d < A[i]; d += SCAN) if (wetElse(x + nrm[i][0] * d * s, z + nrm[i][1] * d * s)) { A[i] = Math.max(1, d - SCAN); break; }
      }
      return true;
    });
    for (const [i0, i1] of runs(keep, 3)) ribbon(done, pts, nrm, i0, i1, (i) => [L[i], -R[i]]);
    // Side channels, also kept clear of other water
    for (const sd of o.side) for (const [i0, i1, iv] of sd) {
      const ok = [];
      for (let i = i0; i <= i1; i++) {
        const m = ((iv[i][0] + iv[i][1]) / 2) * iv.s;
        ok.push(!wetElse(pts[i][0] + nrm[i][0] * m, pts[i][1] + nrm[i][1] * m));
      }
      for (const [a, b] of runs(ok, 10)) ribbon(done, pts, nrm, i0 + a, i0 + b, (i) => (iv.s > 0 ? [iv[i][1], iv[i][0]] : [-iv[i][0], -iv[i][1]]));
    }
  }
  return out.concat(done);
}

// Stretches [i0, i1] where keep[i] holds, at least min long
function runs(keep, min) {
  const found = [];
  for (let i = 0; i < keep.length;) {
    if (!keep[i]) { i++; continue; }
    let j = i;
    while (j + 1 < keep.length && keep[j + 1]) j++;
    if (j - i + 1 >= min) found.push([i, j]);
    i = j + 1;
  }
  return found;
}
// Ribbon pieces of up to 75 cross-sections (150 m) from i0 to i1, each sharing its end row with the
// next, outlined like the builder's: left bank, then right bank backwards. off(i) -> [left, right]
// signed distances along the normal (left > right)
function ribbon(out, pts, nrm, i0, i1, off) {
  for (let a = i0; a < i1; a += 75) {
    const b = Math.min(i1, a + 75), left = [], right = [];
    for (let i = a; i <= b; i++) {
      const [x, z] = pts[i], [nx, nz] = nrm[i];
      let [l, r] = off(i);
      if (l - r < 2) { const m = (l + r) / 2; l = m + 1; r = m - 1; }
      left.push([r2(x + nx * l), r2(z + nz * l)]); right.push([r2(x + nx * r), r2(z + nz * r)]);
    }
    out.push({ hole: false, flow: true, banks: true, pts: [...left, ...right.reverse()] });
  }
}
// Side channels on one side (s = +1 left, -1 right) from reach()'s results: stretches of 30 m or
// more, smoothed, as [i0, i1, iv] with iv[i] = [inner, outer] (and iv.s the side)
function sides(res, s) {
  const has = res.map((v) => !!v[1]);
  // (gaps of a few metres bridged)
  const filled = has.map((v, i) => v || (has.slice(Math.max(0, i - 3), i).some(Boolean) && has.slice(i + 1, i + 4).some(Boolean)));
  // (seen at most of the cross-sections along it, not now and then)
  return runs(filled, 15).filter(([i0, i1]) => has.slice(i0, i1 + 1).filter(Boolean).length >= (i1 - i0 + 1) * 0.75).map(([i0, i1]) => {
    const raw = [];
    let last = null;
    for (let i = i0; i <= i1; i++) { if (res[i][1]) last = res[i][1]; raw.push(res[i][1] || last || res.slice(i).find((v) => v[1])[1]); }
    const sm = (k) => raw.map((_, i) => { const t = raw.slice(Math.max(0, i - 5), i + 6); return t.reduce((acc, v) => acc + v[k], 0) / t.length; });
    const inner = sm(0), outer = sm(1);
    const iv = [];
    for (let i = i0; i <= i1; i++) iv[i] = [inner[i - i0], outer[i - i0]];
    iv.s = s;
    return [i0, i1, iv];
  });
}
const r2 = (v) => Math.round(v * 100) / 100;

// The aerial photo (1 m a pixel) over the course's streams and as far out as a bank is looked for.
// imagery: { manifest, url(file) } as the game loads it. Null without streams or photos.
export const riverPhoto = (imagery, water) => photoAround(imagery, (water || []).filter((f) => f.flow && !f.hole), MAX_OUT + 10);

// The aerial photo (1 m a pixel) round some shapes ({ pts }), pad metres beyond them
export async function photoAround(imagery, shapes, pad) {
  const m = imagery?.manifest;
  if (!m?.chunks?.length || !m.geo) return null;
  let x0 = Infinity, x1 = -Infinity, z0 = Infinity, z1 = -Infinity;
  for (const f of shapes) for (const [x, z] of f.pts) { x0 = Math.min(x0, x); x1 = Math.max(x1, x); z0 = Math.min(z0, z); z1 = Math.max(z1, z); }
  if (!(x1 > x0)) return null;
  const minX = Math.floor(x0 - pad), maxZ = Math.ceil(z1 + pad), w = Math.ceil(x1 + pad - minX), h = Math.ceil(maxZ - (z0 - pad));
  if (w * h > 40e6) return null; // (a river kilometres long beside a small course: too big a picture)
  const [a0, a1, a2] = m.geo.lat, [b0, b1, b2] = m.geo.lon, det = a1 * b2 - a2 * b1;
  const toCourse = (lat, lon) => { const dl = lat - a0, dn = lon - b0; return [(b2 * dl - a2 * dn) / det, (a1 * dn - b1 * dl) / det]; };
  const c = document.createElement('canvas');
  c.width = w; c.height = h;
  const ctx = c.getContext('2d', { willReadFrequently: true });
  // (only the photos that overlap)
  const over = m.chunks.filter((ch) => {
    const e = ch.extent, a = toCourse(e.minLat, e.minLon), b = toCourse(e.maxLat, e.maxLon);
    return Math.max(a[0], b[0]) > minX && Math.min(a[0], b[0]) < minX + w && Math.max(a[1], b[1]) > maxZ - h && Math.min(a[1], b[1]) < maxZ;
  });
  const imgs = await Promise.all(over.map((ch) => new Promise((resolve) => {
    const img = new Image();
    img.onload = () => resolve({ ...ch, img });
    img.onerror = () => resolve(null);
    img.src = imagery.url(ch.file);
  })));
  for (const ch of imgs) {
    if (!ch) continue;
    const e = ch.extent;
    const map = (u, v) => {
      const [cx, cz] = toCourse(e.maxLat - (v / ch.height) * (e.maxLat - e.minLat), e.minLon + (u / ch.width) * (e.maxLon - e.minLon));
      return [cx - minX, maxZ - cz];
    };
    const p0 = map(0, 0), pu = map(1, 0), pv = map(0, 1);
    ctx.setTransform(pu[0] - p0[0], pu[1] - p0[1], pv[0] - p0[0], pv[1] - p0[1], p0[0], p0[1]);
    ctx.drawImage(ch.img, 0, 0);
  }
  return { data: ctx.getImageData(0, 0, w, h).data, w, h, res: 1, minX, maxZ };
}
