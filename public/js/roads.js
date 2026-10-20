// Public roads round a course (OpenStreetMap, see lib/roads.js): { yellowCentre, roads: [{ kind,
// width, lanes, oneway, pts }] }. The road surface is painted on the ground; this works out its
// painted lines (laid on the ground as thin strips, so they stay crisp up close) and which spots
// are on a road.

const MAJOR = new Set(['motorway', 'trunk', 'primary', 'secondary', 'tertiary', 'link']);

// The lines painted on a road: [{ offset (m left of the centre line), width, dash: [on, off] or
// null, yellow }]. Main roads get white edge lines, a centre line (double yellow where two-way
// roads have one, as in the Americas; dashed white elsewhere) and dashed lines between lanes;
// side streets, lanes and driveways have none.
export function roadLines(r, yellowCentre) {
  const out = [], w = r.width;
  if (!MAJOR.has(r.kind) || w < 5) return out;
  const edge = w / 2 - 0.3, dash = yellowCentre ? [3, 9] : [3, 6];
  if (w >= 6) out.push({ offset: -edge, width: 0.12, dash: null }, { offset: edge, width: 0.12, dash: null });
  if (r.oneway) {
    const n = r.lanes || Math.max(1, Math.round(w / 3.6));
    for (let k = 1; k < n; k++) out.push({ offset: -edge + (2 * edge * k) / n, width: 0.12, dash });
  } else {
    if (yellowCentre) out.push({ offset: -0.1, width: 0.1, dash: null, yellow: true }, { offset: 0.1, width: 0.1, dash: null, yellow: true });
    else out.push({ offset: 0, width: 0.12, dash: r.kind === 'motorway' || r.kind === 'trunk' ? null : dash });
    const each = r.lanes ? Math.floor(r.lanes / 2) : 1; // (lanes each way)
    for (let k = 1; k < each; k++) out.push({ offset: (edge * k) / each, width: 0.12, dash }, { offset: -(edge * k) / each, width: 0.12, dash });
  }
  return out;
}

// A road's centre line resampled every 2 m or less: [{ x, z, s (distance along), nx, nz (unit
// normal, to the left) }]
export function roadSamples(pts, step = 2) {
  const out = [];
  let s = 0;
  for (let i = 1; i < pts.length; i++) {
    const [ax, az] = pts[i - 1], [bx, bz] = pts[i], len = Math.hypot(bx - ax, bz - az);
    if (len < 1e-3) continue;
    const n = Math.max(1, Math.ceil(len / step));
    for (let k = out.length ? 1 : 0; k <= n; k++) out.push({ x: ax + ((bx - ax) * k) / n, z: az + ((bz - az) * k) / n, s: s + (len * k) / n });
    s += len;
  }
  // (normals from the neighbouring samples, so bends turn smoothly)
  for (let i = 0; i < out.length; i++) {
    const a = out[Math.max(0, i - 1)], b = out[Math.min(out.length - 1, i + 1)];
    const dx = b.x - a.x, dz = b.z - a.z, l = Math.hypot(dx, dz) || 1;
    out[i].nx = -dz / l; out[i].nz = dx / l;
  }
  return out;
}

// Triangles (x, z pairs, 6 per triangle) for one painted line along the samples, appended to out
export function stripTriangles(sm, offset, width, dash = null, out = []) {
  const hw = width / 2;
  const at = (a, b, t) => {
    let nx = a.nx + (b.nx - a.nx) * t, nz = a.nz + (b.nz - a.nz) * t;
    const l = Math.hypot(nx, nz) || 1; nx /= l; nz /= l;
    const x = a.x + (b.x - a.x) * t + nx * offset, z = a.z + (b.z - a.z) * t + nz * offset;
    return [x + nx * hw, z + nz * hw, x - nx * hw, z - nz * hw];
  };
  const piece = (a, b, t0, t1) => {
    const [l0x, l0z, r0x, r0z] = at(a, b, t0), [l1x, l1z, r1x, r1z] = at(a, b, t1);
    out.push(l0x, l0z, r0x, r0z, l1x, l1z, l1x, l1z, r0x, r0z, r1x, r1z);
  };
  for (let i = 1; i < sm.length; i++) {
    const a = sm[i - 1], b = sm[i], len = b.s - a.s;
    if (len <= 0) continue;
    if (!dash) { piece(a, b, 0, 1); continue; }
    const [on, off] = dash, period = on + off;
    for (let s = Math.floor(a.s / period) * period; s < b.s; s += period) {
      const lo = Math.max(a.s, s), hi = Math.min(b.s, s + on);
      if (hi > lo) piece(a, b, (lo - a.s) / len, (hi - a.s) / len);
    }
  }
  return out;
}

// (x, z) => whether the spot is on a road or within pad metres of its edge
export function onRoad(roads, pad = 0) {
  const cell = 20, grid = new Map(), key = (a, b) => a * 73856093 ^ b * 19349663;
  const segs = [];
  for (const r of roads || []) {
    for (let i = 1; i < r.pts.length; i++) {
      const [ax, az] = r.pts[i - 1], [bx, bz] = r.pts[i], hw = r.width / 2 + pad;
      const sg = { ax, az, bx, bz, hw };
      segs.push(sg);
      for (let gx = Math.floor((Math.min(ax, bx) - hw) / cell); gx <= Math.floor((Math.max(ax, bx) + hw) / cell); gx++) {
        for (let gz = Math.floor((Math.min(az, bz) - hw) / cell); gz <= Math.floor((Math.max(az, bz) + hw) / cell); gz++) {
          const k = key(gx, gz);
          if (!grid.has(k)) grid.set(k, []);
          grid.get(k).push(sg);
        }
      }
    }
  }
  return (x, z) => {
    for (const sg of grid.get(key(Math.floor(x / cell), Math.floor(z / cell))) || []) {
      const dx = sg.bx - sg.ax, dz = sg.bz - sg.az, l2 = dx * dx + dz * dz;
      const t = l2 ? Math.max(0, Math.min(1, ((x - sg.ax) * dx + (z - sg.az) * dz) / l2)) : 0;
      if (Math.hypot(x - sg.ax - dx * t, z - sg.az - dz * t) < sg.hw) return true;
    }
    return false;
  };
}

// A cart path's centre line from the map made smooth and straight: small zigzags (mapping jitter,
// under 0.6 m) taken out, so straight runs are straight, then the corners rounded off over at most
// 4 m each side (a few passes of corner cutting). The ends stay where they are, so paths still meet.
export function smoothLine(pts, tolerance = 0.6) {
  if (!pts || pts.length < 3) return pts;
  // Douglas-Peucker: keep only the points the line really bends at
  const keep = new Uint8Array(pts.length); keep[0] = keep[pts.length - 1] = 1;
  const stack = [[0, pts.length - 1]];
  while (stack.length) {
    const [a, b] = stack.pop(), [ax, az] = pts[a], [bx, bz] = pts[b], dx = bx - ax, dz = bz - az, l = Math.hypot(dx, dz) || 1;
    let far = -1, fd = tolerance;
    for (let i = a + 1; i < b; i++) {
      const d = Math.abs((pts[i][0] - ax) * dz - (pts[i][1] - az) * dx) / l;
      if (d > fd) { fd = d; far = i; }
    }
    if (far >= 0) { keep[far] = 1; stack.push([a, far], [far, b]); }
  }
  let line = pts.filter((_, i) => keep[i]);
  // Corner cutting: each corner replaced by two points, a quarter of the way along each side
  // (no more than 4 m), three times over
  for (let pass = 0; pass < 3; pass++) {
    const out = [line[0]];
    for (let i = 1; i < line.length - 1; i++) {
      const [px, pz] = line[i - 1], [cx, cz] = line[i], [nx, nz] = line[i + 1];
      const l0 = Math.hypot(cx - px, cz - pz) || 1, l1 = Math.hypot(nx - cx, nz - cz) || 1;
      const c0 = Math.min(0.25, 4 / l0), c1 = Math.min(0.25, 4 / l1);
      out.push([cx + (px - cx) * c0, cz + (pz - cz) * c0], [cx + (nx - cx) * c1, cz + (nz - cz) * c1]);
    }
    out.push(line[line.length - 1]);
    line = out;
  }
  return line;
}
