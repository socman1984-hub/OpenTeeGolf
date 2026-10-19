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
];
// Choices for a tree area or a tree: a mix, or one species
export const TREE_MIXES = [
  { id: 'auto', label: 'Automatic (from the photo)' },
  { id: 'broadleaf', label: 'Mixed broadleaf' },
  { id: 'conifer', label: 'Mixed conifers' },
];
// Species for a planted tree from a choice (null = the game decides from the photo)
const BROADLEAF = ['oak', 'oak', 'maple', 'maple', 'ash', 'birch', 'aspen'], CONIFER = ['pine', 'spruce'];
export function pickSpecies(choice, x, z) {
  if (!choice || choice === 'auto') return null;
  const n = hash(x, z, 7.7);
  if (choice === 'broadleaf') return BROADLEAF[Math.floor(n * BROADLEAF.length)];
  if (choice === 'conifer') return CONIFER[Math.floor(n * CONIFER.length)];
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
    // (crown width: narrow for poplars and conifers, wide for willows)
    const shape = { poplar: 0.14, pine: 0.2, spruce: 0.22, willow: 0.45 }[extra.kind] ?? 0.33;
    const t = { x, z, h, r: Math.min(7, Math.max(1.2, h * shape)) * (0.85 + 0.3 * n), kind: null, mapped: true, ...extra };
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
