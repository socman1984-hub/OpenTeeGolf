// The sea (or a big lake) beside a course when OpenStreetMap doesn't map it as water: the map
// shows the sea only as a coastline line, so a seaside course would have dry land where the ocean
// is. Found from the elevation data and the aerial photo together:
//   - the ground is at sea level (within 1 m of the lowest ground, itself near 0: the elevation
//     data is flat over the sea), and
//   - the photo looks like water there (blue-grey or dark, not green or sand-coloured; white surf
//     counts right at sea level), and
//   - the area runs off the edge of the map or is large (not a low hollow inland)
// Mapped greens, fairways, tees, bunkers and water are left as they are. The result is outlines
// (course x, z), with any islands in it as cut-outs.
import { fetchPhotoRGB } from './imagery.js';

const STEP = 4;          // m per mask cell
const LOW = 1.0;         // m above sea level
const MIN_AREA = 5000;   // m², for water not touching the edge of the map

// grid: { minX, minZ, nx, nz, cell }; heightAt(x, z): height in metres above sea level;
// toLL([x, z]) -> [lat, lon]; keepOut(x, z): true where a mapped feature is
export async function findSea({ grid, heightAt, toLL, keepOut = () => false, log = () => {} }) {
  const maxX = grid.minX + (grid.nx - 1) * grid.cell, maxZ = grid.minZ + (grid.nz - 1) * grid.cell;
  const W = Math.ceil((maxX - grid.minX) / STEP), H = Math.ceil((maxZ - grid.minZ) / STEP);
  // Sea level: the lowest ground on the map, when that's within 5 m of the sea's (elevation data
  // puts the sea a little above or below 0 depending on its datum and the tide). None: inland.
  let lowest = Infinity;
  for (let j = 0; j < H; j += 2) for (let i = 0; i < W; i += 2) lowest = Math.min(lowest, heightAt(grid.minX + (i + 0.5) * STEP, grid.minZ + (j + 0.5) * STEP));
  if (!(Math.abs(lowest) < 5)) return [];
  const sea = (h) => h - lowest;
  // Anything low enough to be sea at all? (most courses: nothing, and no photo is needed)
  let low = 0;
  for (let j = 0; j < H; j += 2) for (let i = 0; i < W; i += 2) if (sea(heightAt(grid.minX + (i + 0.5) * STEP, grid.minZ + (j + 0.5) * STEP)) < LOW) low++;
  if (low * 4 * STEP * STEP < MIN_AREA) return [];
  log('Looking for the sea next to the course…');
  // The photo over the same area, about a pixel per cell
  const corners = [[grid.minX, grid.minZ], [maxX, grid.minZ], [grid.minX, maxZ], [maxX, maxZ]].map(toLL);
  const bb = { minLat: Math.min(...corners.map((c) => c[0])), maxLat: Math.max(...corners.map((c) => c[0])), minLon: Math.min(...corners.map((c) => c[1])), maxLon: Math.max(...corners.map((c) => c[1])) };
  let photo = null;
  try { photo = await fetchPhotoRGB(bb, Math.min(2048, W), Math.min(2048, H)); } catch (err) { log(`Photo for finding the sea skipped (${err.message})`); }
  const watery = (x, z) => {
    if (!photo) return true;
    const [lat, lon] = toLL([x, z]);
    const i = Math.floor(((lon - bb.minLon) / (bb.maxLon - bb.minLon)) * photo.w), j = Math.floor(((bb.maxLat - lat) / (bb.maxLat - bb.minLat)) * photo.h);
    if (i < 0 || j < 0 || i >= photo.w || j >= photo.h) return false;
    const o = (j * photo.w + i) * 3, r = photo.rgb[o], g = photo.rgb[o + 1], b = photo.rgb[o + 2];
    const lum = (r + g + b) / 3;
    if (b >= r && g <= b * 1.12 && lum < 200) return true;      // blue-grey water
    if (lum < 55 && g <= r * 1.15) return true;                  // dark, deep water
    if (lum > 200 && Math.max(r, g, b) - Math.min(r, g, b) < 25) return 'surf'; // white surf
    return false;
  };
  // The mask: low, and watery (surf only right at sea level)
  const m = new Uint8Array(W * H);
  for (let j = 0; j < H; j++) for (let i = 0; i < W; i++) {
    const x = grid.minX + (i + 0.5) * STEP, z = grid.minZ + (j + 0.5) * STEP, h = sea(heightAt(x, z));
    if (!(h < LOW) || keepOut(x, z)) continue;
    const w = watery(x, z);
    if (w === true || (w === 'surf' && h < 0.5)) m[j * W + i] = 1;
  }
  // Tidy speckle (majority of 3 x 3, twice), then keep areas touching the edge or large
  for (let pass = 0; pass < 2; pass++) {
    const c = m.slice();
    for (let j = 1; j < H - 1; j++) for (let i = 1; i < W - 1; i++) {
      let n = 0;
      for (let b = -1; b <= 1; b++) for (let a = -1; a <= 1; a++) n += c[(j + b) * W + i + a];
      m[j * W + i] = n >= 5 ? 1 : 0;
    }
  }
  const comp = new Int32Array(W * H).fill(-1), keep = [];
  for (let k0 = 0; k0 < W * H; k0++) {
    if (!m[k0] || comp[k0] >= 0) continue;
    const id = keep.length, stack = [k0];
    let n = 0, edge = false;
    comp[k0] = id;
    while (stack.length) {
      const k = stack.pop(), i = k % W, j = (k - i) / W;
      n++;
      if (i === 0 || j === 0 || i === W - 1 || j === H - 1) edge = true;
      for (const [a, b] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) {
        const ii = i + a, jj = j + b, kk = jj * W + ii;
        if (ii >= 0 && jj >= 0 && ii < W && jj < H && m[kk] && comp[kk] < 0) { comp[kk] = id; stack.push(kk); }
      }
    }
    keep.push(edge || n * STEP * STEP >= MIN_AREA);
  }
  for (let k = 0; k < W * H; k++) if (m[k] && !keep[comp[k]]) m[k] = 0;
  const outlines = traceOutlines(m, W, H).map((loop) => loop.map(([i, j]) => [grid.minX + i * STEP, grid.minZ + j * STEP]));
  // Outer outlines and islands (an outline inside another is an island: a cut-out)
  const out = [];
  for (const pts of outlines) {
    const s = simplify(pts, 1.5);
    if (s.length < 3 || Math.abs(signedArea(s)) < 400) continue;
    const inside = outlines.some((o) => o !== pts && Math.abs(signedArea(o)) > Math.abs(signedArea(pts)) && pointInPoly(o, pts[0][0], pts[0][1]));
    out.push({ pts: s, hole: inside });
  }
  const area = out.filter((o) => !o.hole).reduce((a, o) => a + Math.abs(signedArea(o.pts)), 0);
  if (out.length) log(`Found the sea: ${Math.round(area / 10000)} ha of water the map leaves out.`);
  return out;
}

// Marching squares over a 0/1 mask (cell centres at i + 0.5, j + 0.5): closed outlines in cell units
function traceOutlines(m, W, H) {
  const at = (i, j) => (i < 0 || j < 0 || i >= W || j >= H ? 0 : m[j * W + i]);
  // (edges as an undirected graph of edge midpoints; each point has two neighbours, except at
  // the saddle squares, which are split the same way every time)
  const adj = new Map(), pt = new Map(), key = (p) => `${p[0]},${p[1]}`;
  const link = (a, b) => {
    for (const [p, q] of [[a, b], [b, a]]) { const k = key(p); pt.set(k, p); if (!adj.has(k)) adj.set(k, []); adj.get(k).push(key(q)); }
  };
  for (let j = -1; j < H; j++) for (let i = -1; i < W; i++) {
    const tl = at(i, j), tr = at(i + 1, j), br = at(i + 1, j + 1), bl = at(i, j + 1);
    const c = tl * 8 + tr * 4 + br * 2 + bl;
    if (c === 0 || c === 15) continue;
    const x = i + 0.5, y = j + 0.5;
    const T = [x + 0.5, y], R = [x + 1, y + 0.5], B = [x + 0.5, y + 1], L = [x, y + 0.5];
    switch (c) {
      case 1: case 14: link(B, L); break; case 2: case 13: link(R, B); break; case 3: case 12: link(R, L); break;
      case 4: case 11: link(T, R); break; case 6: case 9: link(T, B); break; case 7: case 8: link(T, L); break;
      case 5: link(T, L); link(B, R); break; case 10: link(T, R); link(B, L); break;
    }
  }
  const used = new Set(), loops = [];
  for (const start of adj.keys()) {
    if (used.has(start)) continue;
    const loop = [];
    let prev = null, cur = start;
    while (cur && !used.has(cur)) {
      used.add(cur); loop.push(pt.get(cur));
      const nb = adj.get(cur).filter((k) => k !== prev && !used.has(k));
      prev = cur; cur = nb[0];
    }
    if (loop.length >= 3) loops.push(loop);
  }
  return loops;
}

function simplify(pts, tol) {
  if (pts.length < 4) return pts;
  const keep = new Uint8Array(pts.length); keep[0] = keep[pts.length - 1] = 1;
  const stack = [[0, pts.length - 1]];
  while (stack.length) {
    const [a, b] = stack.pop(), [ax, az] = pts[a], [bx, bz] = pts[b], dx = bx - ax, dz = bz - az, l = Math.hypot(dx, dz) || 1;
    let far = -1, fd = tol;
    for (let i = a + 1; i < b; i++) { const d = Math.abs((pts[i][0] - ax) * dz - (pts[i][1] - az) * dx) / l; if (d > fd) { fd = d; far = i; } }
    if (far >= 0) { keep[far] = 1; stack.push([a, far], [far, b]); }
  }
  return pts.filter((_, i) => keep[i]).map(([x, z]) => [Math.round(x * 10) / 10, Math.round(z * 10) / 10]);
}
const signedArea = (p) => { let a = 0; for (let i = 0; i < p.length; i++) { const [x0, z0] = p[i], [x1, z1] = p[(i + 1) % p.length]; a += x0 * z1 - x1 * z0; } return a / 2; };
function pointInPoly(poly, x, z) {
  let inside = false;
  for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) {
    const [xi, zi] = poly[i], [xj, zj] = poly[j];
    if ((zi > z) !== (zj > z) && x < ((xj - xi) * (z - zi)) / (zj - zi) + xi) inside = !inside;
  }
  return inside;
}
