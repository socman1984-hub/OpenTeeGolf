// Streams and rivers drawn from a mapped centre line (lib/builder.js riverRibbon) are stored as
// ribbons in pieces of about 150 m: each piece's outline is its left bank, then its right bank
// backwards. The centre lines are mapped as a few straight stretches, so the bends are sharp
// corners. Here the pieces of each stream are joined back into one centre line, smoothed into
// gentle curves, and cut into pieces again. Each new piece keeps { chain, i0, i1 }: its stretch
// of the smoothed line (samples every 2 m: pts, hw half widths, s distance along), which the game
// uses to lay a proper stream mesh down the channel.
// Water areas mapped as outlines (riverbanks) are only rounded off a little.

const STEP = 2;
const dist = (a, b) => Math.hypot(a[0] - b[0], a[1] - b[1]);

// A ribbon's centre line and half widths, or null if the outline isn't one
function asRibbon(pts) {
  const n = pts.length, m = n / 2;
  if (n < 4 || n % 2) return null;
  const centre = [], hw = [];
  for (let i = 0; i < m; i++) {
    const l = pts[i], r = pts[n - 1 - i];
    centre.push([(l[0] + r[0]) / 2, (l[1] + r[1]) / 2]);
    hw.push(dist(l, r) / 2);
  }
  const sorted = [...hw].sort((a, b) => a - b), med = sorted[m >> 1];
  if (!(med > 0.5) || hw.some((w) => Math.abs(w - med) > med * 0.15)) return null;
  return { centre, hw };
}

// Points (with a value each) every `step` metres along a line
function resample(pts, vals, step) {
  const out = [[...pts[0]]], ov = [vals[0]];
  let carry = 0;
  for (let i = 1; i < pts.length; i++) {
    const a = pts[i - 1], b = pts[i], len = dist(a, b);
    let t = step - carry;
    while (t <= len) {
      const k = t / len;
      out.push([a[0] + (b[0] - a[0]) * k, a[1] + (b[1] - a[1]) * k]); ov.push(vals[i - 1] + (vals[i] - vals[i - 1]) * k);
      t += step;
    }
    carry = len - (t - step);
  }
  if (dist(out[out.length - 1], pts[pts.length - 1]) > step * 0.3) { out.push([...pts[pts.length - 1]]); ov.push(vals[vals.length - 1]); }
  else { out[out.length - 1] = [...pts[pts.length - 1]]; ov[ov.length - 1] = vals[vals.length - 1]; }
  return [out, ov];
}

// Moving average over r samples each side, narrowing at the ends so they stay put (that's where
// a stream meets a pond, or runs off the map)
function smoothLine(pts, vals, r, passes) {
  let p = pts, v = vals;
  for (let pass = 0; pass < passes; pass++) {
    const np = [], nv = [];
    for (let i = 0; i < p.length; i++) {
      const k = Math.min(r, i, p.length - 1 - i);
      let x = 0, z = 0, w = 0;
      for (let j = i - k; j <= i + k; j++) { x += p[j][0]; z += p[j][1]; w += v[j]; }
      const c = 2 * k + 1;
      np.push([x / c, z / c]); nv.push(w / c);
    }
    p = np; v = nv;
  }
  return [p, v];
}

// Corner-cutting for a closed outline
function chaikin(ring, passes) {
  let p = ring;
  for (let k = 0; k < passes; k++) {
    const q = [];
    for (let i = 0; i < p.length; i++) {
      const a = p[i], b = p[(i + 1) % p.length];
      q.push([a[0] * 0.75 + b[0] * 0.25, a[1] * 0.75 + b[1] * 0.25], [a[0] * 0.25 + b[0] * 0.75, a[1] * 0.25 + b[1] * 0.75]);
    }
    p = q;
  }
  return p;
}

// list: the course's mapped water ({ hole, pts, flow }); returns it with the streams smoothed
export function prepareStreams(list) {
  const out = [], ribbons = [];
  for (const f of list || []) {
    if (!f.flow || f.hole) { out.push(f); continue; }
    const r = asRibbon(f.pts);
    if (r) ribbons.push(r);
    else out.push({ ...f, pts: chaikin(f.pts, 2) });
  }
  // Join the pieces: one starts where another ends
  const next = ribbons.map((r) => ribbons.findIndex((o) => o !== r && dist(o.centre[0], r.centre[r.centre.length - 1]) < 0.3));
  const hasPrev = new Set(next.filter((i) => i >= 0));
  const used = new Set();
  const chains = [];
  const follow = (start) => {
    const pts = [], hw = [];
    for (let i = start; i >= 0 && !used.has(i); i = next[i]) {
      used.add(i);
      const r = ribbons[i], skip = pts.length ? 1 : 0;
      pts.push(...r.centre.slice(skip)); hw.push(...r.hw.slice(skip));
    }
    if (pts.length >= 2) chains.push({ pts, hw });
  };
  ribbons.forEach((r, i) => { if (!hasPrev.has(i)) follow(i); });
  ribbons.forEach((r, i) => { if (!used.has(i)) follow(i); }); // (loops)

  for (const c of chains) {
    // Smooth over about 12 m each side: sharp mapped corners become gentle bends
    let [pts, hw] = resample(c.pts, c.hw, 1);
    [pts, hw] = smoothLine(pts, hw, 12, 2);
    [pts, hw] = resample(pts, hw, STEP);
    const s = [0];
    for (let i = 1; i < pts.length; i++) s.push(s[i - 1] + dist(pts[i - 1], pts[i]));
    // Unit normals (to the left), from the neighbouring samples
    const nrm = pts.map((p, i) => {
      const a = pts[Math.max(0, i - 1)], b = pts[Math.min(pts.length - 1, i + 1)], l = dist(a, b) || 1;
      return [-(b[1] - a[1]) / l, (b[0] - a[0]) / l];
    });
    const chain = { pts, hw, s, nrm };
    // Pieces of 75 samples (150 m), each sharing its end row with the next
    for (let i0 = 0; i0 < pts.length - 1; i0 += 75) {
      const i1 = Math.min(pts.length - 1, i0 + 75), left = [], right = [];
      for (let i = i0; i <= i1; i++) {
        const [x, z] = pts[i], [nx, nz] = nrm[i], w = Math.max(1.25, hw[i]);
        left.push([x + nx * w, z + nz * w]); right.push([x - nx * w, z - nz * w]);
      }
      out.push({ hole: false, flow: true, pts: [...left, ...right.reverse()], ribbon: { chain, i0, i1 } });
    }
  }
  return out;
}

// Once each piece knows its water level: which way each stream runs (towards its lower end) and
// how fast at each sample (from the slope over 50 m: barely moving on the flat, about 0.6 m/s at
// 1 in 100, up to 2.5 m/s). Adds to each chain: level, down (distance downstream), speed.
export function streamFlow(pieces) {
  const chains = new Set();
  for (const f of pieces) {
    const { chain, i0, i1 } = f.ribbon;
    chain.level ||= new Float32Array(chain.pts.length);
    for (let i = i0; i <= i1; i++) chain.level[i] = f.levelAt(...chain.pts[i]);
    chains.add(chain);
  }
  for (const c of chains) {
    const n = c.pts.length, len = c.s[n - 1];
    const dir = c.level[0] >= c.level[n - 1] ? 1 : -1;
    c.down = c.s.map((s) => (dir > 0 ? s : len - s));
    const K = Math.round(25 / STEP), raw = [];
    for (let i = 0; i < n; i++) {
      const a = Math.max(0, i - K), b = Math.min(n - 1, i + K);
      const slope = b > a ? ((c.level[a] - c.level[b]) * dir) / (c.s[b] - c.s[a]) : 0;
      raw.push(Math.min(2.5, 0.12 + Math.max(0, slope) * 48));
    }
    [, c.speed] = smoothLine(c.pts, raw, 5, 1);
  }
}
