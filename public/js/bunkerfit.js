// Bunkers lined up with where they really are. OpenStreetMap's bunker outlines are often the right
// shape but a few metres off (traced from older or differently aligned imagery). The terrain is
// the surest guide: on courses with lidar elevation every bunker is a hollow in the ground, and
// the game draws the sand on that ground, so each bunker is slid to where its outline best fits
// its hollow: how far the ground inside sits below a plane laid through the ground round it (so a
// bunker on a slope still reads as the bowl it is). Most mapped bunkers already sit in theirs;
// only one that doesn't, with a clear bowl close by, is moved. Only on a course whose
// terrain shows no bunker hollows at all (coarser elevation) is the aerial photo's sand used
// instead - where there are hollows, the photo is the thing that's off. The outline keeps its shape.
// Done once per course (the game or the editor, whichever opens it first) and kept with the
// course's map data: fitted bunkers are marked { fit: 2 } with the map's outline kept as osm;
// bunkers the player saves from the editor are { fit: 'user' } and never moved.

const MAX_SHIFT = 8;   // m: furthest a bunker is moved
const RING = 4;        // m: the ground looked at round each bunker
const FIT = 2;         // (bunkers fitted by an earlier version are fitted again)

export const needsBunkerFit = (bunkers) => (bunkers || []).some((f) => !f.hole && f.fit !== FIT && f.fit !== 'user');

// bunkers: [{ pts, hole?, fit?, osm? }] in course metres
// grid: the terrain { minX, minZ, nx, nz, cell, heights } (heights a Float32Array or base64)
// photo (optional): { data, w, h, res: 1, minX, maxZ } RGBA, row 0 at maxZ
// Returns the new list and how many were moved by the terrain and by the photo.
export function fitBunkers(bunkers, grid, photo = null) {
  const todo = bunkers.filter((f) => !f.hole && f.fit !== FIT && f.fit !== 'user' && f.pts.length >= 3);
  if (!todo.length) return { bunkers, moved: 0, byPhoto: 0 };
  const heightAt = gridHeights(grid);
  // (lidar shows a quarter or more of the map's bunkers as bowls already: the terrain is the guide)
  const bowls = heightAt ? todo.filter((f) => hollowDepth(heightAt, polyCells(f.osm || f.pts), 0, 0) >= 0.12).length : 0;
  const sandAt = photo && bowls < todo.length * 0.25 && sandMask(todo, photo);
  let moved = 0, byPhoto = 0;
  const done = new Map();
  for (const f of todo) {
    const osm = f.osm || f.pts;
    const cells = polyCells(osm);
    let shift = null;
    // The hollow at each shift: how far the ground round the outline stands above the ground
    // inside it, on its lowest side (the ring in four quarters round the bunker's middle)
    if (heightAt && cells.inner.length >= 3) {
      const depth = (dx, dz) => hollowDepth(heightAt, cells, dx, dz);
      const d0 = depth(0, 0);
      let best = { d: d0, dx: 0, dz: 0 };
      for (let dz = -MAX_SHIFT; dz <= MAX_SHIFT; dz++) for (let dx = -MAX_SHIFT; dx <= MAX_SHIFT; dx++) {
        const r = Math.hypot(dx, dz);
        if (r > MAX_SHIFT || !r) continue;
        // (a little less credit the further it goes, so it doesn't wander on near-level ground)
        const d = depth(dx, dz) - r * 0.006;
        if (d > best.d) best = { d, dx, dz };
      }
      // (only one the map has outside any bowl, and only into a clear one - 20 cm or more)
      const enough = d0 < 0.1 && best.d >= 0.2 && best.d > d0 + 0.15;
      if (enough) shift = [best.dx, best.dz];
      // (on a course with bowls the terrain decides: the photo is the thing that is off)
      if (!sandAt || best.d >= 0.12 || d0 >= 0.12) { done.set(f, { ...f, pts: move(osm, shift), osm, fit: FIT }); if (shift) moved++; continue; }
    }
    // No hollow to go by: the sand in the photo (mostly sand inside, mostly grass round it)
    if (sandAt) {
      const sand = (dx, dz) => {
        let a = 0, b = 0;
        for (const [x, z] of cells.inner) a += sandAt(x + dx, z + dz);
        for (const [x, z] of cells.ring) b += sandAt(x + dx, z + dz);
        return a / cells.inner.length - b / cells.ring.length;
      };
      const s0 = cells.inner.length ? sand(0, 0) : 0;
      let best = { s: s0, dx: 0, dz: 0 };
      if (cells.inner.length) for (let dz = -MAX_SHIFT; dz <= MAX_SHIFT; dz++) for (let dx = -MAX_SHIFT; dx <= MAX_SHIFT; dx++) {
        const r = Math.hypot(dx, dz);
        if (r > MAX_SHIFT || !r) continue;
        const s = sand(dx, dz) - r * 0.004;
        if (s > best.s) best = { s, dx, dz };
      }
      if (best.s > 0.45 && best.s > s0 + 0.12) { shift = [best.dx, best.dz]; byPhoto++; }
    }
    done.set(f, { ...f, pts: move(osm, shift), osm, fit: FIT });
  }
  return { bunkers: bunkers.map((f) => done.get(f) || f), moved, byPhoto };
}

const r2 = (v) => Math.round(v * 100) / 100;

// How deep a bowl the outline sits in, moved by (dx, dz): a plane fitted to the ground in the ring
// round it, less the ground inside, on average (metres; about 0 on level or evenly sloping ground)
function hollowDepth(heightAt, cells, dx, dz) {
  let n = 0, sx = 0, sz = 0, sh = 0, sxx = 0, szz = 0, sxz = 0, sxh = 0, szh = 0;
  for (const [x0, z0] of cells.ring) {
    const x = x0 + dx, z = z0 + dz, h = heightAt(x, z);
    if (h === null) continue;
    n++; sx += x; sz += z; sh += h; sxx += x * x; szz += z * z; sxz += x * z; sxh += x * h; szh += z * h;
  }
  if (n < 6) return -Infinity;
  const mx = sx / n, mz = sz / n, mh = sh / n;
  const cxx = sxx / n - mx * mx, czz = szz / n - mz * mz, cxz = sxz / n - mx * mz, cxh = sxh / n - mx * mh, czh = szh / n - mz * mh;
  const det = cxx * czz - cxz * cxz;
  if (Math.abs(det) < 1e-9) return -Infinity;
  const gx = (cxh * czz - czh * cxz) / det, gz = (czh * cxx - cxh * cxz) / det;
  let d = 0, k = 0;
  for (const [x0, z0] of cells.inner) {
    const x = x0 + dx, z = z0 + dz, h = heightAt(x, z);
    if (h !== null) { d += mh + gx * (x - mx) + gz * (z - mz) - h; k++; }
  }
  return k ? d / k : -Infinity;
}

const move = (pts, s) => (s ? pts.map(([x, z]) => [r2(x + s[0]), r2(z + s[1])]) : pts);

// Ground height at a point, blended between the grid's points (null off the grid)
function gridHeights(g) {
  if (!g?.heights) return null;
  let H = g.heights;
  if (typeof H === 'string') {
    const bytes = Uint8Array.from(atob(H), (c) => c.charCodeAt(0));
    H = new Float32Array(bytes.buffer);
  }
  const { minX, minZ, nx, nz, cell } = g;
  return (x, z) => {
    const fx = (x - minX) / cell, fz = (z - minZ) / cell, i = Math.floor(fx), j = Math.floor(fz);
    if (i < 0 || j < 0 || i >= nx - 1 || j >= nz - 1) return null;
    const u = fx - i, v = fz - j, k = j * nx + i;
    return H[k] * (1 - u) * (1 - v) + H[k + 1] * u * (1 - v) + H[k + nx] * (1 - u) * v + H[k + nx + 1] * u * v;
  };
}

// Sand in the photo (1 or 0 at a point): what's brighter than the grass and closer in colour to
// the palest part of what the map calls bunkers than to the ground round them
function sandMask(bunkers, photo) {
  const { data, w, h, minX, maxZ } = photo;
  const px = (x, z) => { const i = Math.floor(x - minX), j = Math.floor(maxZ - z); return i >= 0 && j >= 0 && i < w && j < h ? (j * w + i) * 4 : -1; };
  const inside = [], ring = [];
  for (const f of bunkers) {
    const c = polyCells(f.osm || f.pts);
    for (const [x, z] of c.inner) { const k = px(x, z); if (k >= 0 && data[k + 3]) inside.push([data[k], data[k + 1], data[k + 2]]); }
    for (const [x, z] of c.ring) { const k = px(x, z); if (k >= 0 && data[k + 3]) ring.push([data[k], data[k + 1], data[k + 2]]); }
  }
  if (inside.length < 50 || ring.length < 50) return null;
  const lum = (c) => 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2];
  inside.sort((a, b) => lum(b) - lum(a));
  const sand = median3(inside.slice(0, Math.max(20, Math.floor(inside.length * 0.35)))), grass = median3(ring);
  if (lum(sand) - lum(grass) < 25) return null;
  const bright = (lum(sand) + lum(grass)) / 2;
  const dist = (c, r) => Math.hypot(c[0] - r[0], c[1] - r[1], c[2] - r[2]);
  return (x, z) => {
    const k = px(x, z);
    if (k < 0 || !data[k + 3]) return 0;
    const c = [data[k], data[k + 1], data[k + 2]];
    return lum(c) > bright && dist(c, sand) < dist(c, grass) ? 1 : 0;
  };
}
function median3(list) {
  return [0, 1, 2].map((c) => { const v = list.map((p) => p[c]).sort((a, b) => a - b); return v[v.length >> 1]; });
}

// Points a metre apart inside an outline, and those within RING metres outside it
function polyCells(pts) {
  const xs = pts.map((p) => p[0]), zs = pts.map((p) => p[1]);
  const inner = [], ring = [];
  for (let z = Math.floor(Math.min(...zs)) - RING; z <= Math.max(...zs) + RING; z++) for (let x = Math.floor(Math.min(...xs)) - RING; x <= Math.max(...xs) + RING; x++) {
    const cx = x + 0.5, cz = z + 0.5;
    if (inPoly(pts, cx, cz)) inner.push([cx, cz]);
    else if (edgeDist(pts, cx, cz) <= RING) ring.push([cx, cz]);
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
