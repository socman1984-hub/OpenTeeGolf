// Car parks: where the bays are, laid out from OpenStreetMap ({ lots, aisles, spaces }, see
// lib/parking.js), and what colour a parked car is when there's no photo to say.
//
// Bays are 2.6 m wide and 5.2 m deep, as in real car parks. Bays mapped one by one are used as
// they are. Otherwise, where aisles are mapped through a lot, bays line both sides of each aisle;
// a lot with neither is filled with the usual pattern of a row of bays, a 6.5 m aisle, two rows
// back to back, an aisle, and so on, along its longer side.
// A bay: { x, z (centre), ux, uz (unit vector along its depth, pointing away from its aisle;
// either way for bays mapped one by one, aisle: false), w, d, lot }.

const BAY_W = 2.6, BAY_D = 5.2, AISLE = 6.5;

const pointInPoly = (poly, x, z) => {
  let inside = false;
  for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) {
    const [xi, zi] = poly[i], [xj, zj] = poly[j];
    if (zi > z !== zj > z && x < ((xj - xi) * (z - zi)) / (zj - zi) + xi) inside = !inside;
  }
  return inside;
};

// Main direction of a shape (its longer side), as a unit vector
function principalAxis(poly) {
  let cx = 0, cz = 0;
  for (const [x, z] of poly) { cx += x; cz += z; }
  cx /= poly.length; cz /= poly.length;
  let sxx = 0, szz = 0, sxz = 0;
  for (const [x, z] of poly) { const dx = x - cx, dz = z - cz; sxx += dx * dx; szz += dz * dz; sxz += dx * dz; }
  const a = 0.5 * Math.atan2(2 * sxz, sxx - szz);
  return [Math.cos(a), Math.sin(a)];
}

// The four corners of a bay
export function bayCorners(b) {
  const vx = -b.uz, vz = b.ux; // across the bay
  const hw = b.w / 2, hd = b.d / 2;
  return [[-1, -1], [1, -1], [1, 1], [-1, 1]].map(([s, t]) => [b.x + vx * hw * s + b.ux * hd * t, b.z + vz * hw * s + b.uz * hd * t]);
}

// Whether two bays overlap (by more than a touch along shared lines): separating axis test on
// their sides, each shrunk by 10 cm
function overlap(a, b) {
  const ca = bayCorners({ ...a, w: a.w - 0.2, d: a.d - 0.2 }), cb = bayCorners({ ...b, w: b.w - 0.2, d: b.d - 0.2 });
  for (const [ux, uz] of [[a.ux, a.uz], [-a.uz, a.ux], [b.ux, b.uz], [-b.uz, b.ux]]) {
    const pa = ca.map(([x, z]) => x * ux + z * uz), pb = cb.map(([x, z]) => x * ux + z * uz);
    if (Math.max(...pa) < Math.min(...pb) || Math.max(...pb) < Math.min(...pa)) return false;
  }
  return true;
}

export function layoutParking(parking) {
  const bays = [];
  if (!parking?.lots?.length) return bays;
  // A new bay may not overlap one already laid out (bays along crossing aisles would cross)
  const taken = (nb) => bays.some((b) => Math.hypot(b.x - nb.x, b.z - nb.z) < BAY_D + 0.5 && overlap(b, nb));
  const fits = (b, lot) => bayCorners(b).every(([x, z]) => pointInPoly(lot.pts, x, z));
  parking.lots.forEach((lot, li) => {
    const inLot = (pts) => pts.some(([x, z]) => pointInPoly(lot.pts, x, z));
    // 1. Bays mapped one by one
    const spaces = (parking.spaces || []).filter((s) => inLot(s.pts));
    for (const s of spaces) {
      let cx = 0, cz = 0;
      for (const [x, z] of s.pts) { cx += x; cz += z; }
      cx /= s.pts.length; cz /= s.pts.length;
      const [ux, uz] = principalAxis(s.pts);
      bays.push({ x: cx, z: cz, ux, uz, w: BAY_W, d: BAY_D, lot: li, aisle: false });
    }
    if (spaces.length) return;
    // 2. Along the mapped aisles, on both sides (if none fit - an aisle just passing a small
    // lot - the standard pattern below instead)
    const before = bays.length;
    const aisles = (parking.aisles || []).filter((a) => inLot(a.pts));
    for (const a of aisles) {
      for (let i = 1; i < a.pts.length; i++) {
        const [ax, az] = a.pts[i - 1], [bx, bz] = a.pts[i], len = Math.hypot(bx - ax, bz - az);
        if (len < BAY_W) continue;
        const tx = (bx - ax) / len, tz = (bz - az) / len;
        for (const side of [-1, 1]) {
          const nx = -tz * side, nz = tx * side, off = AISLE / 2 + BAY_D / 2;
          for (let s = BAY_W / 2; s <= len - BAY_W / 2; s += BAY_W) {
            const b = { x: ax + tx * s + nx * off, z: az + tz * s + nz * off, ux: nx, uz: nz, w: BAY_W, d: BAY_D, lot: li };
            if (fits(b, lot) && !taken(b)) bays.push(b);
          }
        }
      }
    }
    if (bays.length > before) return;
    // 3. The standard pattern along the lot's longer side
    const [ux, uz] = principalAxis(lot.pts), vx = -uz, vz = ux;
    const us = lot.pts.map(([x, z]) => x * ux + z * uz), vs = lot.pts.map(([x, z]) => x * vx + z * vz);
    const u0 = Math.min(...us), u1 = Math.max(...us), v0 = Math.min(...vs), v1 = Math.max(...vs);
    const period = 2 * BAY_D + AISLE;
    // Rows: one facing each way per period (the second backs onto the next period's first).
    // (u points away from the bay's aisle, as for bays along mapped aisles)
    for (let base = v0 + 0.5; base < v1; base += period) {
      for (const [dv, dir] of [[BAY_D / 2, -1], [BAY_D + AISLE + BAY_D / 2, 1]]) {
        const v = base + dv;
        for (let u = u0 + 0.5 + BAY_W / 2; u < u1; u += BAY_W) {
          const b = { x: ux * u + vx * v, z: uz * u + vz * v, ux: vx * dir, uz: vz * dir, w: BAY_W, d: BAY_D, lot: li };
          if (fits(b, lot)) bays.push(b);
        }
      }
    }
  });
  return bays;
}

// Car colours seen in a typical car park (white, black, greys and silver most of all)
const PALETTE = [
  [0.92, 0.92, 0.9, 22], [0.08, 0.08, 0.09, 19], [0.55, 0.56, 0.58, 17], [0.75, 0.76, 0.78, 13], [0.28, 0.29, 0.31, 9],
  [0.08, 0.16, 0.42, 8], [0.55, 0.06, 0.06, 7], [0.36, 0.3, 0.22, 3], [0.15, 0.3, 0.18, 2],
];
export function carColour(n) {
  const total = PALETTE.reduce((s, p) => s + p[3], 0);
  let k = n * total;
  for (const p of PALETTE) { if ((k -= p[3]) < 0) return p.slice(0, 3); }
  return PALETTE[0].slice(0, 3);
}
