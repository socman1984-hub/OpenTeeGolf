// A clean copy of the aerial photo for courses coloured by it (Surface: photo): the shadows of
// trees, buildings and carts, the tree crowns themselves, and small bright things on the grass
// (carts, people) are found by comparing each pixel with the grass around it, and painted over
// with the colour of the grass nearby. Only the picture the ground is painted with changes: tree
// spotting, car spotting and surface detection all read their own copies of the original photo.
//
// ctx: the photo (w x h pixels, res metres a pixel), changed in place. keepOut(ctx, cell): draws
// in white the areas to leave alone (water, sand) on a small canvas whose pixels are cell metres.

// Pixels darker than this share of the grass around them are shadow (or a tree crown)
const DARK = 0.72;
// Pixels brighter than this share (and nearly grey/white) are carts, people, litter
const BRIGHT = 1.5;

export function removeShadows(ctx, w, h, res, keepOut) {
  const img = ctx.getImageData(0, 0, w, h), d = img.data;
  // ---- the areas left alone, on a 2 m grid (also the grid the fill colours are worked out on)
  const G = Math.max(1, Math.round(2 / res)), gw = Math.ceil(w / G), gh = Math.ceil(h / G);
  const kc = document.createElement('canvas');
  kc.width = gw; kc.height = gh;
  const kx = kc.getContext('2d');
  kx.fillStyle = '#000'; kx.fillRect(0, 0, gw, gh);
  kx.fillStyle = '#fff'; keepOut(kx, G * res);
  const kd = kx.getImageData(0, 0, gw, gh).data, off = new Uint8Array(gw * gh);
  for (let i = 0; i < off.length; i++) off[i] = kd[i * 4] > 127 ? 1 : 0;
  const offAt = (x, y) => off[((y / G) | 0) * gw + ((x / G) | 0)];
  const lum = (k) => d[k] * 0.3 + d[k + 1] * 0.59 + d[k + 2] * 0.11;

  // ---- 1. how bright the grass is, in 24 m blocks: the 70th percentile of each block (shadows and
  // crowns are the darker part of it), raised to the median of its neighbours where a block is
  // mostly in shadow
  const B = Math.max(8, Math.round(24 / res)), bw = Math.ceil(w / B), bh = Math.ceil(h / B);
  const p70 = new Float32Array(bw * bh), hist = new Uint32Array(64);
  for (let by = 0; by < bh; by++) for (let bx = 0; bx < bw; bx++) {
    hist.fill(0);
    let n = 0;
    for (let y = by * B; y < Math.min(h, (by + 1) * B); y += 2) for (let x = bx * B; x < Math.min(w, (bx + 1) * B); x += 2) {
      if (offAt(x, y)) continue;
      hist[Math.min(63, lum((y * w + x) * 4) >> 2)]++; n++;
    }
    let t = n * 0.7, b = 0;
    while (b < 63 && (t -= hist[b]) > 0) b++;
    p70[by * bw + bx] = n > 20 ? b * 4 + 2 : -1;
  }
  const bg = new Float32Array(bw * bh), near = [];
  for (let by = 0; by < bh; by++) for (let bx = 0; bx < bw; bx++) {
    near.length = 0;
    for (let j = -1; j <= 1; j++) for (let i = -1; i <= 1; i++) {
      const v = p70[Math.min(bh - 1, Math.max(0, by + j)) * bw + Math.min(bw - 1, Math.max(0, bx + i))];
      if (v > 0) near.push(v);
    }
    near.sort((a, b) => a - b);
    const med = near.length ? near[near.length >> 1] : 0;
    bg[by * bw + bx] = Math.max(p70[by * bw + bx], med);
  }

  // ---- 2. what to paint over
  let M = new Uint8Array(w * h);
  for (let y = 0; y < h; y++) {
    const fy = Math.min(bh - 1, Math.max(0, y / B - 0.5)), y0 = fy | 0, y1 = Math.min(bh - 1, y0 + 1), ty = fy - y0;
    for (let x = 0; x < w; x++) {
      if (offAt(x, y)) continue;
      const fx = Math.min(bw - 1, Math.max(0, x / B - 0.5)), x0 = fx | 0, x1 = Math.min(bw - 1, x0 + 1), tx = fx - x0;
      const b = (bg[y0 * bw + x0] * (1 - tx) + bg[y0 * bw + x1] * tx) * (1 - ty) + (bg[y1 * bw + x0] * (1 - tx) + bg[y1 * bw + x1] * tx) * ty;
      if (b < 20) continue;
      const k = (y * w + x) * 4, L = lum(k);
      const grey = Math.max(d[k], d[k + 1], d[k + 2]) - Math.min(d[k], d[k + 1], d[k + 2]) < 45;
      if (L < b * DARK) M[y * w + x] = 1;
      else if (L > b * BRIGHT && L > 140 && grey) M[y * w + x] = 2;
    }
  }
  // (bright things only when small - carts, people - never roofs, car parks or paths)
  const most = Math.round(8 / (res * res)), stack = [], seen = [];
  for (let s = 0; s < M.length; s++) {
    if (M[s] !== 2) continue;
    stack.length = 0; seen.length = 0;
    stack.push(s); M[s] = 3;
    while (stack.length) {
      const p = stack.pop(), x = p % w;
      seen.push(p);
      for (const q of [x > 0 ? p - 1 : -1, x < w - 1 ? p + 1 : -1, p - w, p + w]) if (q >= 0 && q < M.length && M[q] === 2) { M[q] = 3; stack.push(q); }
    }
    const v = seen.length <= most ? 1 : 0;
    for (const p of seen) M[p] = v;
  }
  // (single specks are just the grass's own texture; what's left grows a little past its edge,
  // where shadows fade out, then softens into its surroundings)
  let S = boxSum(M, w, h, 1);
  for (let i = 0; i < M.length; i++) M[i] = S[i] >= 5 ? 1 : 0;
  const grow = Math.max(1, Math.round(0.6 / res));
  S = boxSum(M, w, h, grow);
  for (let i = 0; i < M.length; i++) M[i] = S[i] > 0 ? 1 : 0;
  const soft = Math.max(1, Math.round(0.5 / res)), softN = (2 * soft + 1) ** 2;
  const A = boxSum(M, w, h, soft);
  M = null;

  // ---- 3. the colour of the grass round about: the untouched pixels averaged in 2 m cells,
  // holes filled from coarser and coarser averages (pull-push)
  let lv = { w: gw, h: gh, c: new Float32Array(gw * gh * 3), n: new Float32Array(gw * gh) };
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
    const i = y * w + x;
    if (A[i] || offAt(x, y)) continue;
    const c = ((y / G) | 0) * gw + ((x / G) | 0), k = i * 4;
    lv.c[c * 3] += d[k]; lv.c[c * 3 + 1] += d[k + 1]; lv.c[c * 3 + 2] += d[k + 2]; lv.n[c]++;
  }
  for (let c = 0; c < lv.n.length; c++) if (lv.n[c]) { const n = lv.n[c]; lv.c[c * 3] /= n; lv.c[c * 3 + 1] /= n; lv.c[c * 3 + 2] /= n; lv.n[c] = 1; }
  const levels = [lv];
  while (lv.w > 1 || lv.h > 1) {
    const nw = Math.ceil(lv.w / 2), nh = Math.ceil(lv.h / 2), up = { w: nw, h: nh, c: new Float32Array(nw * nh * 3), n: new Float32Array(nw * nh) };
    for (let y = 0; y < lv.h; y++) for (let x = 0; x < lv.w; x++) {
      const s = y * lv.w + x;
      if (!lv.n[s]) continue;
      const t = (y >> 1) * nw + (x >> 1);
      up.c[t * 3] += lv.c[s * 3]; up.c[t * 3 + 1] += lv.c[s * 3 + 1]; up.c[t * 3 + 2] += lv.c[s * 3 + 2]; up.n[t]++;
    }
    for (let t = 0; t < up.n.length; t++) if (up.n[t]) { const n = up.n[t]; up.c[t * 3] /= n; up.c[t * 3 + 1] /= n; up.c[t * 3 + 2] /= n; up.n[t] = 1; }
    levels.push(up);
    lv = up;
  }
  for (let l = levels.length - 2; l >= 0; l--) {
    const f = levels[l], c = levels[l + 1];
    for (let y = 0; y < f.h; y++) for (let x = 0; x < f.w; x++) {
      const s = y * f.w + x;
      if (f.n[s]) continue;
      sample(c, x / 2 - 0.25, y / 2 - 0.25, f.c, s * 3);
      f.n[s] = 1;
    }
  }
  const fill = levels[0];

  // ---- 4. paint it in, with a little grain so the patches don't look smooth
  const col = new Float32Array(3);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
    const i = y * w + x;
    let a = A[i] / softN;
    if (!a) continue;
    sample(fill, x / G - 0.5, y / G - 0.5, col, 0);
    // (only on grass: round buildings, car parks and the like the photo stays as it is)
    const green = (col[1] - Math.max(col[0], col[2]) * 1.1) / 12;
    if (green <= 0) continue;
    a *= Math.min(1, green);
    const k = i * 4, grain = 1 + (hash(x, y) - 0.5) * 0.08;
    for (let ch = 0; ch < 3; ch++) d[k + ch] = d[k + ch] * (1 - a) + col[ch] * grain * a;
  }
  ctx.putImageData(img, 0, 0);
}

// Bilinear sample of a level (colour grid) at (fx, fy) cells into out[o..o+2]
function sample(lv, fx, fy, out, o) {
  fx = Math.min(lv.w - 1, Math.max(0, fx)); fy = Math.min(lv.h - 1, Math.max(0, fy));
  const x0 = fx | 0, y0 = fy | 0, x1 = Math.min(lv.w - 1, x0 + 1), y1 = Math.min(lv.h - 1, y0 + 1), tx = fx - x0, ty = fy - y0;
  const a = (y0 * lv.w + x0) * 3, b = (y0 * lv.w + x1) * 3, c = (y1 * lv.w + x0) * 3, e = (y1 * lv.w + x1) * 3;
  for (let ch = 0; ch < 3; ch++) out[o + ch] = (lv.c[a + ch] * (1 - tx) + lv.c[b + ch] * tx) * (1 - ty) + (lv.c[c + ch] * (1 - tx) + lv.c[e + ch] * tx) * ty;
}

// How many of the (2r+1)^2 pixels round each one are set (sliding sums, rows then columns)
function boxSum(M, w, h, r) {
  const row = new Uint16Array(w * h), out = new Uint16Array(w * h);
  for (let y = 0; y < h; y++) {
    const o = y * w;
    let s = 0;
    for (let x = 0; x <= Math.min(r, w - 1); x++) s += M[o + x];
    for (let x = 0; x < w; x++) {
      row[o + x] = s;
      if (x + r + 1 < w) s += M[o + x + r + 1];
      if (x - r >= 0) s -= M[o + x - r];
    }
  }
  for (let x = 0; x < w; x++) {
    let s = 0;
    for (let y = 0; y <= Math.min(r, h - 1); y++) s += row[y * w + x];
    for (let y = 0; y < h; y++) {
      out[y * w + x] = s;
      if (y + r + 1 < h) s += row[(y + r + 1) * w + x];
      if (y - r >= 0) s -= row[(y - r) * w + x];
    }
  }
  return out;
}

function hash(x, y) {
  const s = Math.sin(x * 12.9898 + y * 78.233) * 43758.5453;
  return s - Math.floor(s);
}
