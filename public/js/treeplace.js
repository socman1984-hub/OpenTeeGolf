// Where trees stand: from the canopy height map, from OpenStreetMap, and from what players add in
// the course editor (single trees and tree areas). No 3D here, so the editor uses it too and shows
// exactly what the game will plant.

export const fract1 = (v) => v - Math.floor(v);
export const hash = (x, z, k = 1) => fract1(Math.sin(x * 12.9898 * k + z * 78.233) * 43758.5453);

// Tree tops from a 1 m canopy height grid ({ w, h, minX, maxZ, res, heights: Uint8Array }).
// Each local high point at least 4 m tall is a tree; its crown reaches out until the canopy
// drops well below the top (or rises into a neighbour). allowed(x, z) rules out playing surfaces.
// Trees with open ground beside them are marked edge (the ones a golfer sees).
export function treesFromCanopy(cm, allowed) {
  const { w, h, heights: H } = cm;
  const at = (i, j) => (i < 0 || j < 0 || i >= w || j >= h ? 0 : H[j * w + i]);
  // Light smoothing so one tree has one top
  const S = new Float32Array(w * h);
  for (let j = 1; j < h - 1; j++) for (let i = 1; i < w - 1; i++) {
    const k = j * w + i;
    if (H[k] < 3) continue;
    S[k] = (4 * H[k] + 2 * (H[k - 1] + H[k + 1] + H[k - w] + H[k + w]) + H[k - w - 1] + H[k - w + 1] + H[k + w - 1] + H[k + w + 1]) / 16;
  }
  const cand = [];
  for (let j = 1; j < h - 1; j++) for (let i = 1; i < w - 1; i++) {
    const k = j * w + i, v = S[k];
    if (v < 4) continue;
    if (v < S[k - 1] || v < S[k + 1] || v < S[k - w] || v < S[k + w] || v < S[k - w - 1] || v < S[k - w + 1] || v < S[k + w - 1] || v < S[k + w + 1]) continue;
    cand.push(k);
  }
  cand.sort((a, b) => S[b] - S[a]);
  // Tallest first; a top too close to a taller tree belongs to it
  const cell = 4, grid = new Map(), out = [];
  const key = (gx, gz) => gx * 73856093 ^ gz * 19349663;
  const dirs = Array.from({ length: 8 }, (_, n) => [Math.cos((n * Math.PI) / 4), Math.sin((n * Math.PI) / 4)]);
  for (const k of cand) {
    const i = k % w, j = (k - i) / w;
    const x = cm.minX + (i + 0.5) * cm.res, z = cm.maxZ - (j + 0.5) * cm.res;
    let top = 0;
    for (let dj = -1; dj <= 1; dj++) for (let di = -1; di <= 1; di++) top = Math.max(top, at(i + di, j + dj));
    const minD = Math.max(2.2, top * 0.2);
    const gx = Math.floor(x / cell), gz = Math.floor(z / cell);
    let clash = false;
    for (let a = -2; a <= 2 && !clash; a++) for (let b = -2; b <= 2 && !clash; b++) {
      for (const t of grid.get(key(gx + a, gz + b)) || []) if (Math.hypot(t.x - x, t.z - z) < minD) { clash = true; break; }
    }
    if (clash || !allowed(x, z)) continue;
    // Crown radius: walk out in 8 directions until the canopy falls away or climbs into another tree
    let sum = 0;
    for (const [dx, dz] of dirs) {
      let r = 1, prev = top;
      for (; r < 9; r++) {
        const v = at(Math.round(i + dx * r), Math.round(j - dz * r));
        if (v < top * 0.55 || v > prev + 1) break;
        prev = Math.min(prev, v);
      }
      sum += r;
    }
    const r = Math.min(8, Math.max(1.4, sum / 8));
    // Edge of a wood: open ground (no canopy) a little beyond the crown in some direction
    let edge = false;
    for (const [dx, dz] of dirs) {
      if (at(Math.round(i + dx * (r + 6)), Math.round(j - dz * (r + 6))) < 2 && at(Math.round(i + dx * (r + 12)), Math.round(j - dz * (r + 12))) < 2) { edge = true; break; }
    }
    const t = { x, z, h: top, r, kind: null, edge };
    out.push(t);
    const g = key(gx, gz);
    if (!grid.has(g)) grid.set(g, []);
    grid.get(g).push(t);
  }
  return out;
}

// Tree crowns seen in the aerial photo, to fill the gaps the canopy map and OpenStreetMap leave
// (the canopy map can be years old, or miss whole groups of trees). photo: { data (RGBA), w, h,
// res (m per pixel, about 1), minX, maxZ }. Tree crowns are the darkest, most textured vegetation
// in the photo, with deep shade close by: the cut-offs are learned from the photo itself. Spots on a jittered 3 m grid (the
// same spots for the same course, so the game and the editor agree) that sit well inside crown
// cover become trees, unless a tree already stands within 4 m or allowed(x, z) rules the spot out.
// Returns new trees { x, z, h, r, kind: null, photo: true }; heightAt(x, z) gives a canopy-map
// height where there is one. With a height map, a spot also needs something at least 3 m tall
// within 2 m (the map and the photo don't line up exactly): dark patches on open ground - the
// long shadows of bare trees in a photo taken in winter, dark lush grass - are flat there.
export function treesFromPhoto(photo, { allowed = () => true, existing = [], heightAt = null } = {}) {
  const { data, w, h, res, minX, maxZ } = photo, N = w * h;
  const L = new Float32Array(N), veg = new Uint8Array(N);
  for (let k = 0; k < N; k++) {
    const r = data[k * 4], g = data[k * 4 + 1], b = data[k * 4 + 2];
    L[k] = (r + g + b) / 3;
    veg[k] = g >= r * 0.98 && g >= b * 1.02 && data[k * 4 + 3] > 0 ? 1 : 0;
  }
  // Texture: how much the brightness varies over 5×5 pixels (integral images)
  const W1 = w + 1, I = new Float64Array(W1 * (h + 1)), I2 = new Float64Array(W1 * (h + 1));
  for (let j = 0; j < h; j++) {
    let row = 0, row2 = 0;
    for (let i = 0; i < w; i++) {
      const v = L[j * w + i]; row += v; row2 += v * v;
      I[(j + 1) * W1 + i + 1] = I[j * W1 + i + 1] + row; I2[(j + 1) * W1 + i + 1] = I2[j * W1 + i + 1] + row2;
    }
  }
  const box = (i, j, R) => {
    const i0 = Math.max(0, i - R), i1 = Math.min(w, i + R + 1), j0 = Math.max(0, j - R), j1 = Math.min(h, j + R + 1);
    const n = (i1 - i0) * (j1 - j0), a = (A) => A[j1 * W1 + i1] - A[j0 * W1 + i1] - A[j1 * W1 + i0] + A[j0 * W1 + i0];
    return [a(I) / n, a(I2) / n];
  };
  const sd = (i, j) => { const [m, m2] = box(i, j, 2); return Math.sqrt(Math.max(0, m2 - m * m)); };
  // Brightness of the ground around a spot (about 15 m across)
  const around = (i, j) => box(i, j, Math.round(7 / res))[0];
  // Cut-offs from the vegetation in this photo: darker than most of it, and more textured than its
  // typical (mown or smooth) grass
  const vl = [], vs = [];
  for (let k = 0; k < N; k += 7) if (veg[k]) { vl.push(L[k]); vs.push(sd(k % w, Math.floor(k / w))); }
  if (vl.length < 100) return [];
  vl.sort((a, b) => a - b); vs.sort((a, b) => a - b);
  const dark = vl[Math.floor(vl.length * 0.45)], rough = vs[Math.floor(vs.length * 0.5)];
  // Deep shade: as dark as the darkest quarter of the vegetation. A tree always has some within a
  // few metres (gaps in its crown, its shadow); a patch of dark, lush grass has none, however
  // much darker it is than the grass round it
  const shade = vl[Math.floor(vl.length * 0.25)];
  const shaded = (x, z) => {
    const ci = Math.floor((x - minX) / res), cj = Math.floor((maxZ - z) / res), R = Math.round(4 / res);
    for (let j = Math.max(0, cj - R); j <= Math.min(h - 1, cj + R); j++) for (let i = Math.max(0, ci - R); i <= Math.min(w - 1, ci + R); i++) if (L[j * w + i] < shade) return true;
    return false;
  };
  // A crown: vegetation darker than most, and either clearly darker than the ground around it
  // (a sunlit crown is lighter than a shaded one, and its middle is evenly dark) or textured
  const crown = new Uint8Array(N);
  for (let j = 0; j < h; j++) for (let i = 0; i < w; i++) {
    const k = j * w + i;
    if (veg[k] && L[k] < dark && (L[k] < around(i, j) - 4 || sd(i, j) > rough * 0.9)) crown[k] = 1;
  }
  // Clean up speckle: keep a pixel where most of its 3×3 neighbourhood agrees
  const cover = new Uint8Array(N);
  for (let j = 1; j < h - 1; j++) for (let i = 1; i < w - 1; i++) {
    let n = 0;
    for (let dj = -1; dj <= 1; dj++) for (let di = -1; di <= 1; di++) n += crown[(j + dj) * w + i + di];
    cover[j * w + i] = n >= 4 ? 1 : 0;
  }
  const at = (x, z) => {
    const i = Math.floor((x - minX) / res), j = Math.floor((maxZ - z) / res);
    return i >= 0 && j >= 0 && i < w && j < h ? cover[j * w + i] : 0;
  };
  // Trees already standing, for keeping new ones 4 m clear of them and of each other
  const cell = 8, grid = new Map(), key = (a, b) => a * 73856093 ^ b * 19349663;
  const put = (t) => { const k = key(Math.floor(t.x / cell), Math.floor(t.z / cell)); if (!grid.has(k)) grid.set(k, []); grid.get(k).push(t); };
  const near = (x, z, d) => {
    const gx = Math.floor(x / cell), gz = Math.floor(z / cell);
    for (let a = -1; a <= 1; a++) for (let b = -1; b <= 1; b++) for (const t of grid.get(key(gx + a, gz + b)) || []) if (Math.hypot(t.x - x, t.z - z) < d) return true;
    return false;
  };
  for (const t of existing) put(t);
  const standing = (x, z) => {
    if (!heightAt) return true;
    for (const [dx, dz] of [[0, 0], [2, 0], [-2, 0], [0, 2], [0, -2], [1.4, 1.4], [-1.4, 1.4], [1.4, -1.4], [-1.4, -1.4]]) if ((heightAt(x + dx, z + dz) || 0) >= 3) return true;
    return false;
  };
  // (candidate spots every 3 m, so small single crowns get one near their middle)
  const out = [], STEP = 3;
  const x0 = Math.ceil(minX / STEP) * STEP, z1 = Math.floor(maxZ / STEP) * STEP;
  for (let gz = z1; gz > maxZ - h * res; gz -= STEP) for (let gx = x0; gx < minX + w * res; gx += STEP) {
    const x = gx + (hash(gx, gz, 2.3) - 0.5) * STEP * 0.6, z = gz + (hash(gx, gz, 4.1) - 0.5) * STEP * 0.6;
    // Well inside crown cover: the spot and most of a ring 2 m around it
    let n = at(x, z) * 2;
    for (const [dx, dz] of [[2, 0], [-2, 0], [0, 2], [0, -2], [1.4, 1.4], [-1.4, 1.4], [1.4, -1.4], [-1.4, -1.4]]) n += at(x + dx, z + dz);
    if (n < 7 || near(x, z, 4) || !shaded(x, z) || !standing(x, z) || !allowed(x, z)) continue;
    const canopy = heightAt?.(x, z) || 0, k = hash(x, z, 8.8);
    const t = { x, z, h: canopy >= 4 ? canopy : 9 + k * 8, r: 2.6 + hash(x, z, 9.9) * 1.6, kind: null, photo: true };
    out.push(t); put(t);
  }
  return out;
}

// Trees taken out in the course editor: [[x, z], ...]. A tree found automatically (canopy map,
// OpenStreetMap, aerial photo) within 3 m of one is left out; the player's own trees aren't affected.
export function removeTrees(trees, removed) {
  if (!removed?.length) return trees;
  const cell = 6, grid = new Map(), key = (a, b) => a * 73856093 ^ b * 19349663;
  for (const [x, z] of removed) { const k = key(Math.floor(x / cell), Math.floor(z / cell)); if (!grid.has(k)) grid.set(k, []); grid.get(k).push([x, z]); }
  const gone = (t) => {
    const gx = Math.floor(t.x / cell), gz = Math.floor(t.z / cell);
    for (let a = -1; a <= 1; a++) for (let b = -1; b <= 1; b++) for (const [x, z] of grid.get(key(gx + a, gz + b)) || []) if (Math.hypot(t.x - x, t.z - z) < 3) return true;
    return false;
  };
  return trees.filter((t) => t.user || !gone(t));
}

// Trees people have mapped in OpenStreetMap (single trees, and tree rows sampled every 7 m) added
// to the ones already found (canopy map or imagery). A mapped tree with a found tree within a few
// metres is that tree (OSM positions are often a few metres off), so nothing is added - though a
// needle-leaved tag still makes it a conifer. Otherwise a tree goes in at the mapped spot: as tall
// as the canopy map says there, else the mapped height, else a typical one.
// heightAt(x, z) -> canopy height in m (or 0/null); allowed(x, z) rules out greens, bunkers, etc.
export function addMappedTrees(trees, mapped, rows, { heightAt = () => 0, allowed = () => true } = {}) {
  const cell = 6, grid = new Map(), key = (a, b) => a * 73856093 ^ b * 19349663;
  const put = (t) => { const k = key(Math.floor(t.x / cell), Math.floor(t.z / cell)); if (!grid.has(k)) grid.set(k, []); grid.get(k).push(t); };
  const nearest = (x, z) => {
    let best = null, bd = Infinity;
    const gx = Math.floor(x / cell), gz = Math.floor(z / cell);
    for (let a = -1; a <= 1; a++) for (let b = -1; b <= 1; b++) {
      for (const t of grid.get(key(gx + a, gz + b)) || []) { const d = Math.hypot(t.x - x, t.z - z); if (d < bd) { bd = d; best = t; } }
    }
    return [best, bd];
  };
  for (const t of trees) put(t);
  const spots = [];
  for (const m of mapped || []) spots.push({ x: m[0], z: m[1], h: m[2] ?? null, c: m[3] ?? null, leaf: m[4] ?? null });
  for (const row of rows || []) {
    for (let i = 1; i < row.length; i++) {
      const [ax, az] = row[i - 1], [bx, bz] = row[i], len = Math.hypot(bx - ax, bz - az), n = Math.max(1, Math.round(len / 7));
      for (let k = i === 1 ? 0 : 1; k <= n; k++) spots.push({ x: ax + ((bx - ax) * k) / n, z: az + ((bz - az) * k) / n, row: true });
    }
  }
  let added = 0, matched = 0;
  for (const s of spots) {
    const [near, d] = nearest(s.x, s.z);
    if (near && d < Math.max(s.row ? 4 : 3, near.r * 0.7)) {
      matched++;
      if (s.leaf === 'n' && !near.mapped) near.kind = 'pine';
      continue;
    }
    if (!allowed(s.x, s.z)) continue;
    const n = hash(s.x, s.z, 11.3), canopy = heightAt(s.x, s.z) || 0;
    const h = canopy >= 3 ? canopy : s.h || (s.leaf === 'n' ? 12 : 10) + n * 6;
    const r = s.c ? Math.min(8, Math.max(1.2, s.c / 2)) : Math.min(6, Math.max(1.8, h * (s.leaf === 'n' ? 0.2 : 0.33)));
    const t = { x: s.x, z: s.z, h, r, kind: s.leaf === 'n' ? 'pine' : null, mapped: true };
    trees.push(t);
    put(t);
    added++;
  }
  return { added, matched };
}

const pointInPoly = (poly, x, z) => {
  let inside = false;
  for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) {
    const [xi, zi] = poly[i], [xj, zj] = poly[j];
    if (zi > z !== zj > z && x < ((xj - xi) * (z - zi)) / (zj - zi) + xi) inside = !inside;
  }
  return inside;
};

// Tree areas drawn in the editor: spacing between trees for each density (metres)
export const AREA_SPACING = { sparse: 15, medium: 10, dense: 6.5 };
// Player-placed single trees: size -> height (m)
export const TREE_SIZES = { small: 8, medium: 13, large: 20 };

// The tree species the game can grow (trees.js has their 3D shapes). Players pick one for single
// trees and tree areas in the editor, or a mix; otherwise the game chooses from the photo.
export const TREE_SPECIES = [
  { id: 'oak', label: 'Oak', group: 'broadleaf', about: 'broad, spreading crown' },
  { id: 'maple', label: 'Maple', group: 'broadleaf', about: 'dense, rounded crown' },
  { id: 'ash', label: 'Ash', group: 'broadleaf', about: 'open, airy crown' },
  { id: 'birch', label: 'Birch', group: 'broadleaf', about: 'slender, white bark' },
  { id: 'aspen', label: 'Aspen', group: 'broadleaf', about: 'pale bark, light leaves' },
  { id: 'poplar', label: 'Lombardy poplar', group: 'broadleaf', about: 'tall and narrow (windbreaks)' },
  { id: 'willow', label: 'Weeping willow', group: 'broadleaf', about: 'drooping branches, by water' },
  { id: 'pine', label: 'Pine', group: 'conifer', about: 'tall trunk, crown up top' },
  { id: 'spruce', label: 'Spruce', group: 'conifer', about: 'dense, dark cone' },
  { id: 'palm', label: 'Palm', group: 'palm', about: 'fronds on a tall trunk (warm climates)' },
  { id: 'liveoak', label: 'Live oak', group: 'broadleaf', about: 'low, very wide, with Spanish moss (the South)' },
  { id: 'magnolia', label: 'Southern magnolia', group: 'broadleaf', about: 'dense evergreen oval, white flowers' },
  { id: 'sycamore', label: 'Sycamore', group: 'broadleaf', about: 'big crown, white patchy bark' },
  { id: 'cypress', label: 'Monterey cypress', group: 'conifer', about: 'flat-topped, wind-swept (the coast)' },
  { id: 'baldcypress', label: 'Bald cypress', group: 'conifer', about: 'narrow cone, flared foot, by water' },
  { id: 'mesquite', label: 'Mesquite', group: 'desert', about: 'low, crooked, several stems' },
  { id: 'paloverde', label: 'Palo verde', group: 'desert', about: 'green bark, airy crown' },
  { id: 'saguaro', label: 'Saguaro cactus', group: 'desert', about: 'ribbed column with arms' },
];
// ---- what grows where: the mix of trees the game plants on its own, by the course's region
// (from its latitude and longitude). Each region lists conifers (for crowns that look like one in
// the photo or the height map: narrow, dark blue-green) and the rest, with weights. Species from
// the wrong part of the world never appear: no palms in Ohio, no spruce in Phoenix.
export const TREE_REGIONS = {
  temperate: { label: 'Temperate (northern US, Canada, Europe)', conifer: { pine: 6, spruce: 4 }, other: { oak: 30, maple: 20, ash: 20, birch: 10, aspen: 9, poplar: 2, sycamore: 4 } },
  southeast: { label: 'Southeastern US', conifer: { pine: 10, baldcypress: 1 }, other: { oak: 28, maple: 12, liveoak: 14, magnolia: 12, sycamore: 8, ash: 8, pine: 18 } },
  subtropical: { label: 'Subtropical (Florida, Gulf coast)', conifer: { pine: 7, baldcypress: 3 }, other: { palm: 30, liveoak: 30, magnolia: 10, oak: 8, pine: 22 } },
  desert: { label: 'Desert Southwest', conifer: { pine: 1 }, other: { mesquite: 32, paloverde: 24, palm: 18, pine: 12, saguaro: 0 } },
  sonoran: { label: 'Sonoran Desert (Arizona)', conifer: { pine: 1 }, other: { mesquite: 30, paloverde: 26, saguaro: 14, palm: 18, pine: 12 } },
  calcoast: { label: 'California coast', conifer: { cypress: 6, pine: 4 }, other: { cypress: 30, pine: 25, oak: 25, sycamore: 8, palm: 6, ash: 6 } },
  socal: { label: 'Southern California', conifer: { pine: 6, cypress: 3 }, other: { palm: 30, oak: 20, sycamore: 12, pine: 20, cypress: 10, ash: 8 } },
  mountain: { label: 'Mountain West', conifer: { pine: 6, spruce: 5 }, other: { aspen: 35, pine: 25, spruce: 20, poplar: 10, birch: 10 } },
  pacificnw: { label: 'Pacific Northwest', conifer: { spruce: 6, pine: 4 }, other: { spruce: 30, pine: 20, maple: 20, ash: 10, birch: 10, oak: 10 } },
  mediterranean: { label: 'Mediterranean', conifer: { pine: 7, cypress: 3 }, other: { pine: 35, cypress: 20, oak: 30, palm: 10, ash: 5 } },
  tropical: { label: 'Tropical', conifer: { pine: 1 }, other: { palm: 55, liveoak: 15, magnolia: 15, oak: 15 } },
};
// The region for a course at (lat, lon) degrees
export function treeRegion(lat, lon) {
  if (!Number.isFinite(lat) || !Number.isFinite(lon)) return 'temperate';
  const a = Math.abs(lat);
  if (a < 23.5) return 'tropical';
  // North America (rough boxes; coasts and deserts first)
  if (lon > -170 && lon < -52 && lat > 14) {
    if (lat >= 31 && lat < 34.6 && lon > -114.8 && lon < -109.5) return 'sonoran';
    if (lat >= 31 && lat < 37.5 && lon > -117.6 && lon < -103.5) return 'desert';
    if (lat >= 35 && lat < 39 && lon < -121.2) return 'calcoast';
    if (lat >= 32.4 && lat < 35 && lon < -116.5) return 'socal';
    if (lat >= 41.8 && lon < -116) return 'pacificnw';
    if (lat >= 37.5 && lon < -103.5) return 'mountain';
    if (lat < 30.6 && lon > -98.5) return 'subtropical';
    if (lat < 36.6 && lon > -100 && lon < -75) return 'southeast';
    return 'temperate';
  }
  // Around the Mediterranean (and similar latitudes in Europe and the Middle East)
  if (lat >= 30 && lat < 44.5 && lon > -10 && lon < 42) return 'mediterranean';
  if (a < 30) return 'subtropical';
  return 'temperate';
}
// A species from one of a region's lists, picked by n (0-1)
export function pickFrom(list, n) {
  const entries = Object.entries(list).filter(([, w]) => w > 0), total = entries.reduce((s, [, w]) => s + w, 0);
  let t = n * total;
  for (const [id, w] of entries) { t -= w; if (t < 0) return id; }
  return entries[entries.length - 1][0];
}

// Choices for a tree area or a tree: a mix, or one species
export const TREE_MIXES = [
  { id: 'auto', label: 'Automatic (from the photo)' },
  { id: 'broadleaf', label: 'Mixed broadleaf' },
  { id: 'conifer', label: 'Mixed conifers' },
  { id: 'desert', label: 'Desert (mesquite, palo verde, saguaro)' },
];
// Species for a planted tree from a choice (null = the game decides from the photo)
const BROADLEAF = ['oak', 'oak', 'maple', 'maple', 'ash', 'birch', 'aspen'], CONIFER = ['pine', 'spruce'];
const DESERT = ['mesquite', 'mesquite', 'paloverde', 'paloverde', 'saguaro'];
export function pickSpecies(choice, x, z) {
  if (!choice || choice === 'auto') return null;
  const n = hash(x, z, 7.7);
  if (choice === 'broadleaf') return BROADLEAF[Math.floor(n * BROADLEAF.length)];
  if (choice === 'conifer') return CONIFER[Math.floor(n * CONIFER.length)];
  if (choice === 'desert') return DESERT[Math.floor(n * DESERT.length)];
  return TREE_SPECIES.some((s) => s.id === choice) ? choice : null;
}
// New tree areas: heights between these (m)
export const AREA_HEIGHTS = { min: 10, max: 18 };

// Spots for trees inside an area: a jittered grid (the same every time for the same area)
export function treesInArea(poly, density = 'medium') {
  const step = AREA_SPACING[density] || AREA_SPACING.medium;
  const xs = poly.map((p) => p[0]), zs = poly.map((p) => p[1]);
  const out = [];
  for (let z = Math.min(...zs) + step / 2; z < Math.max(...zs); z += step * 0.87) {
    const row = Math.round(z / step);
    for (let x = Math.min(...xs) + step / 2 + (row % 2 ? step / 2 : 0); x < Math.max(...xs); x += step) {
      const px = x + (hash(x, z, 3.7) - 0.5) * step * 0.7, pz = z + (hash(x, z, 5.9) - 0.5) * step * 0.7;
      if (pointInPoly(poly, px, pz)) out.push([px, pz]);
    }
  }
  return out;
}

// Add the player's trees: single trees ({ x, z, size, kind? }) always go in (unless one is already
// right there); tree areas ({ pts, density, kind?, hMin?, hMax? }) fill with trees except where one
// already stands close by. An area's trees are a species or a mix (pickSpecies) and range in
// height from hMin to hMax (areas from before that setting take the canopy map's height).
// heightAt(x, z) -> canopy height or 0; allowed(x, z) keeps area trees off greens, bunkers, etc.
export function addTreeAreas(trees, single, areas, { heightAt = () => 0, allowed = () => true } = {}) {
  const cell = 8, grid = new Map(), key = (a, b) => a * 73856093 ^ b * 19349663;
  const put = (t) => { const k = key(Math.floor(t.x / cell), Math.floor(t.z / cell)); if (!grid.has(k)) grid.set(k, []); grid.get(k).push(t); };
  const near = (x, z, d) => {
    const gx = Math.floor(x / cell), gz = Math.floor(z / cell);
    for (let a = -1; a <= 1; a++) for (let b = -1; b <= 1; b++) {
      for (const t of grid.get(key(gx + a, gz + b)) || []) if (Math.hypot(t.x - x, t.z - z) < d) return true;
    }
    return false;
  };
  for (const t of trees) put(t);
  let added = 0;
  const plant = (x, z, h, extra = {}) => {
    const n = hash(x, z, 9.1);
    // (crown width: narrow for poplars, conifers, palms and cacti, wide for willows, live oaks
    // and desert trees)
    const shape = { poplar: 0.14, pine: 0.2, spruce: 0.22, palm: 0.22, baldcypress: 0.22, saguaro: 0.12, willow: 0.45, mesquite: 0.7, paloverde: 0.65, cypress: 0.6, liveoak: 0.95 }[extra.kind] ?? 0.33;
    const t = { x, z, h, r: Math.min(extra.kind === 'liveoak' ? 15 : 7, Math.max(1.2, h * shape)) * (0.85 + 0.3 * n), kind: null, mapped: true, ...extra };
    trees.push(t); put(t); added++;
  };
  for (const s of single || []) {
    if (near(s.x, s.z, 1.5)) continue;
    const h = (TREE_SIZES[s.size] || TREE_SIZES.medium) * (0.9 + 0.2 * hash(s.x, s.z, 4.3));
    plant(s.x, s.z, h, { kind: pickSpecies(s.kind, s.x, s.z), user: true });
  }
  for (const a of areas || []) {
    const gap = (AREA_SPACING[a.density] || AREA_SPACING.medium) * 0.6;
    const ranged = Number.isFinite(a.hMin) && Number.isFinite(a.hMax);
    const lo = ranged ? Math.min(a.hMin, a.hMax) : 0, hi = ranged ? Math.max(a.hMin, a.hMax) : 0;
    for (const [x, z] of treesInArea(a.pts, a.density)) {
      if (near(x, z, gap) || !allowed(x, z)) continue;
      const canopy = heightAt(x, z) || 0;
      const h = ranged ? lo + (hi - lo) * hash(x, z, 6.2) : canopy >= 4 ? canopy : 10 + hash(x, z, 6.2) * 8;
      plant(x, z, h, { kind: pickSpecies(a.kind, x, z), user: true });
    }
  }
  return added;
}
