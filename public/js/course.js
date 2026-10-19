// Builds a playable course model from a course file (.course.json).
//
// The course file holds tees, pins, aim points, penalty areas and the out-of-bounds line in
// course space (meters, y up). From those it derives:
//   - a smooth heightfield interpolated through every surveyed point
//   - greens, fringes, tee boxes and fairway corridors along each hole's aim line
//   - water (penalty areas) and OOB
// These drive both the physics (heightAt / normalAt / surfaceAt) and the rendering.

import { courseMowing } from './mowing.js';

export const TEE_TYPES = ['Black', 'Blue', 'White', 'Green', 'Yellow', 'Red', 'Junior', 'Par3'];
export const PIN_DAYS = ['Thursday', 'Friday', 'Saturday', 'Sunday'];

const CELL = 4; // heightfield resolution (m) for generated terrain
const MARGIN = 80;

const valid = (p) => p && !(p.x === 0 && p.y === 0 && p.z === 0);
const dist2 = (ax, az, bx, bz) => (ax - bx) ** 2 + (az - bz) ** 2;

function hash(x, z) {
  const s = Math.sin(x * 127.1 + z * 311.7) * 43758.5453;
  return s - Math.floor(s);
}
// Smooth 2D value noise in [-1, 1]
export function noise(x, z) {
  const xi = Math.floor(x), zi = Math.floor(z);
  const xf = x - xi, zf = z - zi;
  const u = xf * xf * (3 - 2 * xf), v = zf * zf * (3 - 2 * zf);
  const a = hash(xi, zi), b = hash(xi + 1, zi), c = hash(xi, zi + 1), d = hash(xi + 1, zi + 1);
  return (a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v) * 2 - 1;
}
const smooth = (e0, e1, x) => {
  const t = Math.max(0, Math.min(1, (x - e0) / (e1 - e0)));
  return t * t * (3 - 2 * t);
};

// Round off a polygon's corners (Chaikin corner cutting, 3 passes). Each cut is at most a quarter
// of the edge and at most 5 m from the corner, so long straight edges get gentle curves at their
// ends without the shape shrinking much. Outlines that are already finely drawn are left alone.
export function smoothPoly(pts) {
  let P = pts.slice();
  const same = (a, b) => Math.abs(a[0] - b[0]) < 1e-6 && Math.abs(a[1] - b[1]) < 1e-6;
  if (P.length > 1 && same(P[0], P[P.length - 1])) P.pop();
  if (P.length < 3) return pts;
  let per = 0;
  for (let i = 0; i < P.length; i++) per += Math.hypot(P[(i + 1) % P.length][0] - P[i][0], P[(i + 1) % P.length][1] - P[i][1]);
  if (per / P.length < 2.2) return P;
  for (let pass = 0; pass < 3; pass++) {
    const out = [];
    for (let i = 0; i < P.length; i++) {
      const p0 = P[i], p1 = P[(i + 1) % P.length];
      const len = Math.hypot(p1[0] - p0[0], p1[1] - p0[1]);
      const f = len > 0 ? Math.min(0.25, 5 / len) : 0.25;
      out.push([p0[0] + (p1[0] - p0[0]) * f, p0[1] + (p1[1] - p0[1]) * f]);
      out.push([p1[0] - (p1[0] - p0[0]) * f, p1[1] - (p1[1] - p0[1]) * f]);
    }
    P = out;
  }
  return P;
}

// Bunkers are shaped with flowing, rounded edges, but mapped outlines are often a few points or a
// wobbly trace. Resample the outline every half metre, smooth out bumps smaller than a few metres
// (repeated neighbour averaging, i.e. a Gaussian blur along the edge), then scale it back up
// about its centre to the original area so the bunker doesn't shrink.
export function roundOutline(pts, sigma = 1.6) {
  let P = pts.slice();
  if (P.length > 1 && Math.abs(P[0][0] - P[P.length - 1][0]) < 1e-6 && Math.abs(P[0][1] - P[P.length - 1][1]) < 1e-6) P.pop();
  if (P.length < 3) return pts;
  const area = (Q) => { let a = 0; for (let i = 0; i < Q.length; i++) { const p = Q[i], q = Q[(i + 1) % Q.length]; a += p[0] * q[1] - q[0] * p[1]; } return a / 2; };
  const a0 = area(P);
  const step = 0.5, R = [];
  for (let i = 0; i < P.length; i++) {
    const p = P[i], q = P[(i + 1) % P.length], n = Math.max(1, Math.round(Math.hypot(q[0] - p[0], q[1] - p[1]) / step));
    for (let k = 0; k < n; k++) R.push([p[0] + ((q[0] - p[0]) * k) / n, p[1] + ((q[1] - p[1]) * k) / n]);
  }
  if (R.length < 8) return P;
  // Each [1/4, 1/2, 1/4] pass adds half a sample² of variance
  const passes = Math.min(80, Math.round(2 * (sigma / step) ** 2));
  let S = R;
  for (let k = 0; k < passes; k++) {
    S = S.map((p, i) => {
      const a = S[(i - 1 + S.length) % S.length], b = S[(i + 1) % S.length];
      return [p[0] * 0.5 + (a[0] + b[0]) * 0.25, p[1] * 0.5 + (a[1] + b[1]) * 0.25];
    });
  }
  const a1 = area(S);
  if (!a1 || Math.sign(a1) !== Math.sign(a0)) return P;
  const f = Math.sqrt(Math.abs(a0 / a1));
  const cx = S.reduce((s, p) => s + p[0], 0) / S.length, cz = S.reduce((s, p) => s + p[1], 0) / S.length;
  // (every second point is plenty once smooth)
  return S.filter((_, i) => i % 2 === 0).map(([x, z]) => [cx + (x - cx) * f, cz + (z - cz) * f]);
}

function decodeF32(b64) {
  const bytes = typeof Buffer !== 'undefined'
    ? Uint8Array.from(Buffer.from(b64, 'base64'))
    : Uint8Array.from(atob(b64), (c) => c.charCodeAt(0));
  return new Float32Array(bytes.buffer);
}

export function pointInPoly(poly, x, z) {
  let inside = false;
  for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) {
    const [xi, zi] = poly[i], [xj, zj] = poly[j];
    if (zi > z !== zj > z && x < ((xj - xi) * (z - zi)) / (zj - zi) + xi) inside = !inside;
  }
  return inside;
}

function distToPolyEdge(poly, x, z) {
  let best = Infinity;
  for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) {
    best = Math.min(best, segDist(x, z, poly[j][0], poly[j][1], poly[i][0], poly[i][1]).d);
  }
  return best;
}

function segDist(px, pz, ax, az, bx, bz) {
  const dx = bx - ax, dz = bz - az;
  const l2 = dx * dx + dz * dz;
  let t = l2 > 0 ? ((px - ax) * dx + (pz - az) * dz) / l2 : 0;
  t = Math.max(0, Math.min(1, t));
  const cx = ax + dx * t, cz = az + dz * t;
  return { d: Math.hypot(px - cx, pz - cz), t, len: Math.sqrt(l2) };
}

function parseHole(h) {
  const tees = {};
  const aims = [];
  let greenCenter = null;
  for (const t of h.Tees) {
    if (!valid(t.Position)) continue;
    if (t.TeeType.startsWith('AimPoint')) aims.push({ ...t.Position, n: +t.TeeType.slice(8) });
    else if (t.TeeType === 'GreenCenterPoint') greenCenter = { ...t.Position };
    else tees[t.TeeType] = { ...t.Position, distance: t.Distance };
  }
  aims.sort((a, b) => a.n - b.n);
  const pins = {};
  for (const p of h.Pins) if (valid(p.Position)) pins[p.Day] = { ...p.Position };
  const pinList = Object.values(pins);
  if (!greenCenter && pinList.length) {
    greenCenter = {
      x: pinList.reduce((s, p) => s + p.x, 0) / pinList.length,
      y: pinList.reduce((s, p) => s + p.y, 0) / pinList.length,
      z: pinList.reduce((s, p) => s + p.z, 0) / pinList.length,
    };
  }
  // Back-most tee starts the hole's centerline.
  const backTee = TEE_TYPES.map((t) => tees[t]).filter(Boolean)
    .sort((a, b) => (b.distance || 0) - (a.distance || 0))[0];
  const path = [backTee, ...aims, greenCenter].filter(Boolean);
  const cum = [0];
  for (let i = 1; i < path.length; i++) cum.push(cum[i - 1] + Math.hypot(path[i].x - path[i - 1].x, path[i].z - path[i - 1].z));
  const last = path[path.length - 2] || backTee;
  const gdx = greenCenter.x - last.x, gdz = greenCenter.z - last.z, gl = Math.hypot(gdx, gdz) || 1;
  const maxPin = Math.max(0, ...pinList.map((p) => Math.hypot(p.x - greenCenter.x, p.z - greenCenter.z)));
  const greenR = Math.min(24, Math.max(12, maxPin / 0.82 + 3));
  return {
    number: h.HoleNumber,
    par: h.Par,
    index: h.Index,
    enabled: h.Enabled,
    tees,
    aims,
    pins,
    greenCenter,
    greenDir: [gdx / gl, gdz / gl],
    greenR,
    path,
    cum,
    length: cum[cum.length - 1],
    seed: h.HoleNumber * 7.31,
  };
}

export function buildCourse(courseData, real = null) {
  const holes = courseData.Holes.filter((h) => h.Enabled !== false).map(parseHole);
  const hazards = (courseData.Hazards || []).filter((h) => h.coords?.length > 2).map((h) => {
    const ys = h.coords.map((c) => c.y).sort((a, b) => a - b);
    return {
      poly: smoothPoly(h.coords.map((c) => [c.x, c.z])),
      coords: h.coords,
      level: ys[Math.floor(ys.length * 0.1)] - 0.25,
      innerOOB: !!h.innerOOB,
      dropZone: h.hasDZ && valid(h.DZpos) ? { ...h.DZpos } : null,
    };
  });
  const oob = courseData.pOOB?.coords?.length > 2 ? courseData.pOOB.coords.map((c) => [c.x, c.z]) : null;

  // ---- bounds
  let minX = Infinity, maxX = -Infinity, minZ = Infinity, maxZ = -Infinity, cell = CELL, nx, nz;
  if (real) {
    // Real terrain comes with its own grid (already in course coordinates)
    ({ minX, minZ, cell, nx, nz } = real.grid);
    maxX = minX + (nx - 1) * cell; maxZ = minZ + (nz - 1) * cell;
  } else {
    const ext = (x, z) => { minX = Math.min(minX, x); maxX = Math.max(maxX, x); minZ = Math.min(minZ, z); maxZ = Math.max(maxZ, z); };
    if (oob) oob.forEach(([x, z]) => ext(x, z));
    for (const h of holes) { h.path.forEach((p) => ext(p.x, p.z)); Object.values(h.tees).forEach((p) => ext(p.x, p.z)); }
    minX = Math.floor((minX - MARGIN) / cell) * cell; minZ = Math.floor((minZ - MARGIN) / cell) * cell;
    maxX = Math.ceil((maxX + MARGIN) / cell) * cell; maxZ = Math.ceil((maxZ + MARGIN) / cell) * cell;
    nx = Math.round((maxX - minX) / cell) + 1; nz = Math.round((maxZ - minZ) / cell) + 1;
  }

  // ---- spatial buckets so surface lookups only test nearby features
  const BUCKET = 50;
  const bnx = Math.ceil((maxX - minX) / BUCKET) + 1, bnz = Math.ceil((maxZ - minZ) / BUCKET) + 1;
  const buckets = Array.from({ length: bnx * bnz }, () => ({ holes: [], hazards: [], feats: [], paths: [] }));
  const addBox = (x0, z0, x1, z1, key, idx) => {
    const i0 = Math.max(0, Math.floor((x0 - minX) / BUCKET)), i1 = Math.min(bnx - 1, Math.floor((x1 - minX) / BUCKET));
    const j0 = Math.max(0, Math.floor((z0 - minZ) / BUCKET)), j1 = Math.min(bnz - 1, Math.floor((z1 - minZ) / BUCKET));
    for (let j = j0; j <= j1; j++) for (let i = i0; i <= i1; i++) buckets[j * bnx + i][key].push(idx);
  };
  holes.forEach((h, idx) => {
    const pts = [...h.path, ...Object.values(h.tees)];
    const xs = pts.map((p) => p.x), zs = pts.map((p) => p.z);
    addBox(Math.min(...xs) - 45, Math.min(...zs) - 45, Math.max(...xs) + 45, Math.max(...zs) + 45, 'holes', idx);
  });
  hazards.forEach((hz, idx) => {
    const xs = hz.poly.map((p) => p[0]), zs = hz.poly.map((p) => p[1]);
    hz.bbox = [Math.min(...xs), Math.min(...zs), Math.max(...xs), Math.max(...zs)];
    addBox(hz.bbox[0] - 8, hz.bbox[1] - 8, hz.bbox[2] + 8, hz.bbox[3] + 8, 'hazards', idx);
  });
  // Mapped golf features (OpenStreetMap), already converted to course coordinates
  const FEATURE_SURFACE = { water: 'water', bunker: 'sand', green: 'green', tee: 'tee', fairway: 'fairway', rough: 'rough' };
  const feats = [];
  if (real) {
    for (const [kind, list] of Object.entries(real.features || {})) {
      const surface = FEATURE_SURFACE[kind];
      if (!surface) continue;
      for (const f of list) {
        // Mapped outlines are often a few straight segments; real turf edges are curved
        const poly = surface === 'sand' ? roundOutline(f.pts) : smoothPoly(f.pts);
        const xs = poly.map((p) => p[0]), zs = poly.map((p) => p[1]);
        // (river pieces are already smooth ribbons; smoothing would pull their ends apart)
        if (f.flow) poly.splice(0, poly.length, ...f.pts);
        feats.push({ surface, poly, raw: f.pts, inner: !!f.hole, flow: !!f.flow, bbox: [Math.min(...xs), Math.min(...zs), Math.max(...xs), Math.max(...zs)] });
      }
    }
    feats.forEach((f, idx) => addBox(f.bbox[0] - 2, f.bbox[1] - 2, f.bbox[2] + 2, f.bbox[3] + 2, 'feats', idx));
  }
  // Cart paths: line segments with a width (OpenStreetMap)
  const pathSegs = [];
  for (const pth of real?.paths || []) {
    for (let i = 1; i < pth.pts.length; i++) {
      const [ax, az] = pth.pts[i - 1], [bx, bz] = pth.pts[i], hw = pth.width / 2;
      pathSegs.push({ ax, az, bx, bz, hw });
      addBox(Math.min(ax, bx) - hw, Math.min(az, bz) - hw, Math.max(ax, bx) + hw, Math.max(az, bz) + hw, 'paths', pathSegs.length - 1);
    }
  }
  const bucketAt = (x, z) => {
    const i = Math.floor((x - minX) / BUCKET), j = Math.floor((z - minZ) / BUCKET);
    if (i < 0 || j < 0 || i >= bnx || j >= bnz) return null;
    return buckets[j * bnx + i];
  };

  // ---- per-hole shape tests
  const greenTest = (h, x, z) => {
    const dx = x - h.greenCenter.x, dz = z - h.greenCenter.z;
    const u = dx * h.greenDir[0] + dz * h.greenDir[1];
    const w = dx * h.greenDir[1] - dz * h.greenDir[0];
    const th = Math.atan2(w, u);
    const wob = 1 + 0.07 * Math.sin(3 * th + h.seed) + 0.04 * Math.sin(5 * th + h.seed * 2);
    const r = Math.sqrt((u / (h.greenR * 1.12)) ** 2 + (w / (h.greenR * 0.92)) ** 2) / wob;
    return r; // < 1 green, < 1.15 fringe
  };
  const teeTest = (h, x, z) => {
    for (const t of Object.values(h.tees)) {
      const dx = x - t.x, dz = z - t.z;
      const next = h.path[1] || h.greenCenter;
      const fx = next.x - t.x, fz = next.z - t.z, fl = Math.hypot(fx, fz) || 1;
      const u = (dx * fx + dz * fz) / fl, w = (dx * fz - dz * fx) / fl;
      if (Math.abs(u) < 7 && Math.abs(w) < 5) return true;
    }
    return false;
  };
  const fairwayTest = (h, x, z, extra = 0) => {
    // Closest point on the hole's centerline
    let best = { d: Infinity, s: 0 };
    for (let i = 1; i < h.path.length; i++) {
      const a = h.path[i - 1], b = h.path[i];
      const r = segDist(x, z, a.x, a.z, b.x, b.z);
      if (r.d < best.d) best = { d: r.d, s: h.cum[i - 1] + r.t * r.len };
    }
    const end = h.length - h.greenR * 0.6;
    let start, half;
    if (h.par >= 4) {
      start = Math.min(170, Math.max(90, h.length * 0.3));
      half = 15 + 3 * Math.sin(best.s / 37 + h.seed) + 2 * noise(x / 25, z / 25);
      // Taper into the green
      half = Math.min(half, 10 + (end - best.s) * 0.3);
    } else {
      start = h.length - h.greenR - 28; // apron in front of par 3 greens
      half = 9 + noise(x / 20, z / 20);
    }
    return best.s >= start - extra && best.s <= end && best.d < half + extra;
  };

  // Surfaces detected from the aerial photo: 1 = mown fairway the map is missing, 2 = under trees,
  // 3 = heather / native grass, 4 = cart path
  let overlay = null;
  const overlayAt = (x, z) => {
    if (!overlay) return 0;
    const i = Math.floor((x - overlay.minX) / overlay.res), j = Math.floor((overlay.maxZ - z) / overlay.res);
    if (i < 0 || j < 0 || i >= overlay.w || j >= overlay.h) return 0;
    return overlay.codes[j * overlay.w + i];
  };

  function surfaceAt(x, z) {
    if (x < minX || x > maxX || z < minZ || z > maxZ) return null;
    const b = bucketAt(x, z);
    if (b) {
      for (const i of b.hazards) {
        const hz = hazards[i];
        if (x < hz.bbox[0] || x > hz.bbox[2] || z < hz.bbox[1] || z > hz.bbox[3]) continue;
        if (pointInPoly(hz.poly, x, z)) return hz.innerOOB ? 'oob' : 'water';
      }
    }
    if (oob && !pointInPoly(oob, x, z)) return 'oob';
    if (!b) return 'rough';
    if (real) return realSurface(b, x, z);
    let best = 'rough';
    for (const i of b.holes) {
      const h = holes[i];
      const g = greenTest(h, x, z);
      if (g < 1) return 'green';
      if (g < 1.15) best = 'fringe';
    }
    if (best === 'fringe') return best;
    for (const i of b.holes) if (teeTest(holes[i], x, z)) return 'tee';
    for (const i of b.holes) if (fairwayTest(holes[i], x, z)) return 'fairway';
    for (const i of b.holes) if (fairwayTest(holes[i], x, z, 2.5)) return 'firstcut';
    return best;
  }

  function realSurface(b, x, z) {
    const inside = {}, cut = {};
    for (const i of b.feats) {
      const f = feats[i];
      if (x < f.bbox[0] || x > f.bbox[2] || z < f.bbox[1] || z > f.bbox[3]) continue;
      if (pointInPoly(f.poly, x, z)) (f.inner ? cut : inside)[f.surface] = true;
    }
    const has = (s) => inside[s] && !cut[s];
    if (has('water')) return 'water';
    if (has('sand')) return 'sand';
    if (has('green')) return 'green';
    // Cart path (mapped, or seen in the aerial photo)
    for (const i of b.paths) {
      const sg = pathSegs[i];
      if (segDist(x, z, sg.ax, sg.az, sg.bx, sg.bz).d < sg.hw) return 'path';
    }
    if (overlayAt(x, z) === 4) return 'path';
    // Holes whose pins don't sit on a mapped green fall back to a generated green
    for (const i of b.holes) {
      const h = holes[i];
      if (!h.synthGreen) continue;
      const g = greenTest(h, x, z);
      if (g < 1) return 'green';
      if (g < 1.15) return 'fringe';
    }
    // A collar of fringe around every green
    for (const i of b.feats) {
      const f = feats[i];
      if (f.surface !== 'green' || f.inner) continue;
      if (x < f.bbox[0] - 1.5 || x > f.bbox[2] + 1.5 || z < f.bbox[1] - 1.5 || z > f.bbox[3] + 1.5) continue;
      if (distToPolyEdge(f.poly, x, z) < 1.5) return 'fringe';
    }
    if (has('tee')) return 'tee';
    if (has('fairway')) return 'fairway';
    const ov = overlayAt(x, z);
    if (ov === 1) return 'fairway';
    // First cut: a 2 m band of slightly longer grass around fairways
    for (const i of b.feats) {
      const f = feats[i];
      if (f.surface !== 'fairway' || f.inner) continue;
      if (x < f.bbox[0] - 2 || x > f.bbox[2] + 2 || z < f.bbox[1] - 2 || z > f.bbox[3] + 2) continue;
      if (distToPolyEdge(f.poly, x, z) < 2) return 'firstcut';
    }
    if (overlay && (overlayAt(x + 2, z) === 1 || overlayAt(x - 2, z) === 1 || overlayAt(x, z + 2) === 1 || overlayAt(x, z - 2) === 1)) return 'firstcut';
    if (ov === 2) return 'deeprough';
    if (ov === 3) return 'native';
    return 'rough';
  }
  if (real) {
    // A pin that smoothing put just off its green: give that green its original outline back
    for (const h of holes) for (const p of Object.values(h.pins)) {
      const pb = bucketAt(p.x, p.z);
      if (!pb || pb.feats.some((i) => feats[i].surface === 'green' && !feats[i].inner && pointInPoly(feats[i].poly, p.x, p.z))) continue;
      for (const i of pb.feats) {
        const f = feats[i];
        if (f.surface === 'green' && !f.inner && f.raw && pointInPoly(f.raw, p.x, p.z)) f.poly = f.raw;
      }
    }
    for (const h of holes) {
      const b = bucketAt(h.greenCenter.x, h.greenCenter.z);
      h.synthGreen = !Object.values(h.pins).every((p) => {
        const pb = bucketAt(p.x, p.z);
        return pb && pb.feats.some((i) => feats[i].surface === 'green' && !feats[i].inner && pointInPoly(feats[i].poly, p.x, p.z));
      }) || !b;
    }
  }

  // ---- heightfield
  const heights = real ? decodeF32(real.grid.heights) : new Float32Array(nx * nz);
  if (real) {
    // Sit mapped water below its banks so the water surface covers it. Ponds and lakes lie flat;
    // rivers and streams slope: their level at any point comes from the banks within 40 m.
    const flowing = feats.filter((f) => f.surface === 'water' && !f.inner && f.flow);
    const bankPts = flowing.flatMap((f) => f.poly.map(([x, z]) => [x, z, sampleGrid(x, z)]).filter((p) => p[2] !== null));
    const bankLevel = (x, z) => {
      let hs = bankPts.filter((p) => Math.abs(p[0] - x) < 40 && Math.abs(p[1] - z) < 40 && Math.hypot(p[0] - x, p[1] - z) < 40).map((p) => p[2]);
      if (hs.length < 3) hs = bankPts.map((p) => [Math.hypot(p[0] - x, p[1] - z), p[2]]).sort((a, b) => a[0] - b[0]).slice(0, 6).map((p) => p[1]);
      hs.sort((a, b) => a - b);
      return hs.length ? hs[Math.floor(hs.length * 0.2)] - 0.1 : 0;
    };
    for (const f of feats) {
      if (f.surface !== 'water' || f.inner) continue;
      if (f.flow && bankPts.length) {
        f.levelAt = coarseLevels(f.bbox, bankLevel);
        const ls = f.poly.map(([x, z]) => f.levelAt(x, z));
        f.level = ls.reduce((a, b) => a + b, 0) / ls.length;
        carveShore(f.poly, f.bbox, f.levelAt);
        continue;
      }
      const bank = f.poly.map(([x, z]) => sampleGrid(x, z)).filter((v) => v !== null).sort((a, b) => a - b);
      if (!bank.length) continue;
      f.level = bank[Math.floor(bank.length * 0.2)] - 0.1;
      carveShore(f.poly, f.bbox, f.level);
    }
  } else buildSyntheticHeights();

  // Shape the ground around a pond into a bank that meets the water exactly on its outline:
  // sloping down into the water inside, rising gently outside. An abrupt step at the edge would
  // cut the water surface in a saw-tooth along the terrain mesh instead of a smooth shoreline.
  // A level function over a box from a coarse 8 m grid of samples (bilinear between them)
  function coarseLevels(bbox, fn) {
    const S = 8, x0 = bbox[0] - 8, z0 = bbox[1] - 8;
    const w = Math.ceil((bbox[2] + 8 - x0) / S) + 1, h = Math.ceil((bbox[3] + 8 - z0) / S) + 1;
    const g = new Float32Array(w * h);
    for (let j = 0; j < h; j++) for (let i = 0; i < w; i++) g[j * w + i] = fn(x0 + i * S, z0 + j * S);
    return (x, z) => {
      const fx = Math.min(w - 1.001, Math.max(0, (x - x0) / S)), fz = Math.min(h - 1.001, Math.max(0, (z - z0) / S));
      const i = Math.floor(fx), j = Math.floor(fz), u = fx - i, v = fz - j;
      const a = g[j * w + i], b = g[j * w + i + 1], c = g[(j + 1) * w + i], d = g[(j + 1) * w + i + 1];
      return (a * (1 - u) + b * u) * (1 - v) + (c * (1 - u) + d * u) * v;
    };
  }

  // (level: a number, or a function of position for sloping water)
  function carveShore(poly, bbox, level) {
    const pad = 7;
    const levelAt = typeof level === 'function' ? level : () => level;
    const i0 = Math.max(0, Math.floor((bbox[0] - pad - minX) / cell)), i1 = Math.min(nx - 1, Math.ceil((bbox[2] + pad - minX) / cell));
    const j0 = Math.max(0, Math.floor((bbox[1] - pad - minZ) / cell)), j1 = Math.min(nz - 1, Math.ceil((bbox[3] + pad - minZ) / cell));
    for (let j = j0; j <= j1; j++) for (let i = i0; i <= i1; i++) {
      const x = minX + i * cell, z = minZ + j * cell;
      const e = distToPolyEdge(poly, x, z);
      const inside = pointInPoly(poly, x, z);
      if (!inside && e > pad) continue;
      const k = j * nx + i;
      const L = levelAt(x, z);
      const target = inside ? L - Math.min(1.8, 0.12 + 0.35 * e) : L + 0.3 * e;
      heights[k] = Math.min(heights[k], target);
    }
  }

  function sampleGrid(x, z) {
    const i = Math.round((x - minX) / cell), j = Math.round((z - minZ) / cell);
    if (i < 0 || j < 0 || i >= nx || j >= nz) return null;
    return heights[j * nx + i];
  }

  function buildSyntheticHeights() {
    const ctrl = [];
    const addCtrl = (p, w = 1) => valid(p) && ctrl.push([p.x, p.y, p.z, w]);
    for (const h of holes) {
      Object.values(h.tees).forEach((p) => addCtrl(p, 2));
      h.aims.forEach((p) => addCtrl(p));
      addCtrl(h.greenCenter, 3);
      Object.values(h.pins).forEach((p) => addCtrl(p, 3));
    }
    for (const hz of hazards) hz.coords.forEach((p) => addCtrl(p, 0.5));
    (courseData.pOOB?.coords || []).forEach((p) => addCtrl(p, 0.5));
    if (valid(courseData.RangeTee)) addCtrl(courseData.RangeTee);
    const cx = new Float32Array(ctrl.map((c) => c[0])), cy = new Float32Array(ctrl.map((c) => c[1]));
    const cz = new Float32Array(ctrl.map((c) => c[2])), cw = new Float32Array(ctrl.map((c) => c[3]));

    // Distance to the nearest green/tee to keep putting surfaces true to the surveyed points.
    const flatSpots = [];
    for (const h of holes) {
      flatSpots.push([h.greenCenter.x, h.greenCenter.z, h.greenR + 6, h.greenR + 30]);
      Object.values(h.tees).forEach((t) => flatSpots.push([t.x, t.z, 9, 30]));
    }
    for (let j = 0; j < nz; j++) {
      const z = minZ + j * cell;
      for (let i = 0; i < nx; i++) {
        const x = minX + i * cell;
        let sw = 0, sh = 0;
        for (let k = 0; k < cx.length; k++) {
          const d2 = (x - cx[k]) ** 2 + (z - cz[k]) ** 2 + 64;
          const w = cw[k] / (d2 * d2);
          sw += w; sh += w * cy[k];
        }
        let h = sh / sw;
        // Natural undulation away from greens and tees
        let fade = 1;
        for (const f of flatSpots) {
          const d = Math.sqrt(dist2(x, z, f[0], f[1]));
          if (d < f[3]) fade = Math.min(fade, smooth(f[2], f[3], d));
        }
        h += fade * (0.9 * noise(x / 55, z / 55) + 0.35 * noise(x / 17 + 11, z / 17 - 3));
        heights[j * nx + i] = h;
      }
    }
    // Carve ponds below their water level
    for (const hz of hazards) if (!hz.innerOOB) carveShore(hz.poly, hz.bbox, hz.level);
  }

  function heightAt(x, z) {
    const fx = (x - minX) / cell, fz = (z - minZ) / cell;
    if (fx < 0 || fz < 0 || fx >= nx - 1 || fz >= nz - 1) return null;
    const i = Math.floor(fx), j = Math.floor(fz), u = fx - i, v = fz - j;
    const k = j * nx + i;
    const a = heights[k], b = heights[k + 1], c = heights[k + nx], d = heights[k + nx + 1];
    // Same two triangles per cell as the rendered terrain mesh (split along b–c), so the ball
    // sits exactly on the surface you see
    return u + v <= 1 ? a + (b - a) * u + (c - a) * v : d + (c - d) * (1 - u) + (b - d) * (1 - v);
  }
  function normalAt(x, z) {
    const e = 1;
    const hl = heightAt(x - e, z), hr = heightAt(x + e, z), hd = heightAt(x, z - e), hu = heightAt(x, z + e);
    if (hl === null || hr === null || hd === null || hu === null) return [0, 1, 0];
    const nxv = hl - hr, nzv = hd - hu, ny = 2 * e;
    const l = Math.hypot(nxv, ny, nzv);
    return [nxv / l, ny / l, nzv / l];
  }

  return {
    name: courseData.CourseName,
    location: courseData.Location,
    designer: courseData.Designer,
    altitudeM: (courseData.altitudeV2 || courseData.altitude || 0) * (courseData.altitudeV2 ? 0.3048 : 1),
    defaultStimp: courseData.DefaultStimp || 10,
    mowing: courseMowing(courseData),
    holes,
    hazards,
    oob,
    real: !!real,
    sources: real?.source || null,
    // Water drawn on screen: mapped ponds when available, otherwise the penalty areas
    waterBodies: real && feats.some((f) => f.surface === 'water' && f.level != null)
      ? feats.filter((f) => f.surface === 'water' && !f.inner && f.level != null).map((f) => ({ poly: f.poly, level: f.level, levelAt: f.levelAt || null }))
      : hazards.filter((h) => !h.innerOOB).map((h) => ({ poly: h.poly, level: h.level })),
    // Trees mapped in OpenStreetMap: single trees ([x, z, height?, crown?, leaf?]) and tree rows (lines)
    trees: real?.trees || [],
    treeRows: real?.treeRows || [],
    // Trees added in the course editor: single trees ({ x, z, size }) and tree areas ({ pts, density })
    userTrees: real?.userTrees || [],
    treeAreas: real?.treeAreas || [],
    // Mapped surface polygons ({ surface, poly, inner }) for painting the terrain
    features: feats,
    paths: real?.paths || [],
    bounds: { minX, maxX, minZ, maxZ },
    grid: { nx, nz, cell, heights },
    heightAt,
    normalAt,
    surfaceAt,
    // Install surfaces detected from imagery: { minX, maxZ, res, w, h, codes }
    setOverlay: (o) => { overlay = o; },
    rangeTee: valid(courseData.RangeTee) ? courseData.RangeTee : null,
    rangeAim: valid(courseData.RangeAim) ? courseData.RangeAim : null,
  };
}
