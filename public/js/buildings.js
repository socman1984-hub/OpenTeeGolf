// Buildings from OpenStreetMap footprints, drawn as real buildings rather than plain blocks. What
// each is comes from buildingtypes.js (the player's choice in the editor, the map, or a guess):
//   house      - one or two storeys, a gabled or hipped roof with eaves, siding, brick or stucco
//                walls, windows on every floor, a front door, sometimes a chimney
//   clubhouse  - one tall storey (or what the map says), big windows in bands, a broad hipped
//                roof with deep eaves, warm stucco, brick or white siding
//   garage     - a low gabled roof, siding, a garage door
//   shed       - small, plank walls, a plank door, a low roof (often metal)
//   barn       - tall plank walls (barn red, grey or white), a steep metal roof, big doors
//   porch      - a roof on posts, open at the sides (shelters, carports, pavilions)
//   commercial - shops, offices, schools: glass bands, a low hipped or flat roof
// What the map says about the building itself wins (storeys, roof shape, roof and wall colours,
// material); the rest is a fixed random choice per building so it looks the same every time.
// Roof colours come from the aerial photo where it shows one.
// Everything is merged into two meshes (walls, roofs); windows, doors, siding, bricks and
// shingles are drawn by the shaders, so a building costs only a few dozen triangles.
import * as THREE from 'three/webgpu';
import {
  attribute, uv, vec2, vec3, float, floor, fract, abs, step, smoothstep, select, mix, max, min, sin, dot, Fn,
} from 'three/tsl';

import { classifyBuildings } from './buildingtypes.js';

// Wall styles (the shader draws each): 0 siding, 1 brick, 2 stucco, 3 large (glass bands), 4 shed (planks)
const STYLE = { siding: 0, brick: 1, stucco: 2, large: 3, shed: 4 };
// Roof styles: 0 shingles, 1 flat membrane, 2 trim (fascia), 3 metal (standing seams)
const ROOF = { shingle: 0, flat: 1, trim: 2, metal: 3 };

const hash = (a, b, k = 0) => { const s = Math.sin(a * 12.9898 + b * 78.233 + k * 37.719) * 43758.5453; return s - Math.floor(s); };
const pick = (list, r) => list[Math.floor(r * list.length) % list.length];
const srgb = (hex) => new THREE.Color().setHex(hex); // (three converts sRGB hex to linear)
const PALETTE = {
  siding: [0xf2f0ea, 0xe9e4d6, 0xd7d9d6, 0xc9d2d6, 0xb8c2b0, 0xd8c9a8, 0xeee2b8, 0xa9b4bd, 0x8e9a8a, 0xe6dccb],
  brick: [0x9a4a36, 0x8a3f30, 0xa65b42, 0x7d4334, 0xb06a4f, 0x94573f],
  stucco: [0xe8dcc4, 0xddd0b4, 0xf0ebe0, 0xcfc4ae, 0xe2d6bf],
  large: [0xd8d2c4, 0xc8c4bc, 0xe0d8c8, 0xb8b4ac, 0x9a5a44],
  shed: [0x8e3a2e, 0x9a7a58, 0x7a5c40, 0x7c8a78, 0x6e6458, 0xa8483a, 0x8a8478],
  barn: [0x8a2c22, 0x9a3324, 0x7a2a22, 0x8a8478, 0x6e5a44, 0x9b3a2a],
  club: [0xece4d2, 0xe0d4bc, 0xf4f1ea, 0xa55a42, 0xd6c8ae],
  roof: [0x3c3d40, 0x4a4542, 0x5a4a40, 0x2e2f33, 0x5e5a54, 0x4d5550, 0x6b4a3a],
  metal: [0x7a7f84, 0x5f6b5c, 0x8a3a30, 0x4c5a6a, 0x9a9c9a],
  flat: [0x8a8a86, 0x9c9a94, 0x6e6e6c],
  door: [0x6a2e24, 0x2e3a4c, 0x3a4a3a, 0x5a4030, 0xf0eee8, 0x222222],
};

// ------------------------------------------------------------------ footprints

function cleanFootprint(pts) {
  let p = pts.slice();
  if (p.length > 1 && p[0][0] === p[p.length - 1][0] && p[0][1] === p[p.length - 1][1]) p.pop();
  // (drop tiny edges and near-straight corners)
  for (let pass = 0; pass < 2; pass++) {
    const out = [];
    for (let i = 0; i < p.length; i++) {
      const a = out.length ? out[out.length - 1] : p[p.length - 1], b = p[i], c = p[(i + 1) % p.length];
      if (Math.hypot(b[0] - a[0], b[1] - a[1]) < 0.3) continue;
      const ux = b[0] - a[0], uz = b[1] - a[1], vx = c[0] - b[0], vz = c[1] - b[1];
      const s = Math.abs(ux * vz - uz * vx) / ((Math.hypot(ux, uz) * Math.hypot(vx, vz)) || 1);
      if (s < 0.12 && ux * vx + uz * vz > 0) continue;
      out.push(b);
    }
    p = out;
  }
  if (p.length < 3) return null;
  let area = 0;
  for (let i = 0; i < p.length; i++) { const a = p[i], b = p[(i + 1) % p.length]; area += a[0] * b[1] - b[0] * a[1]; }
  if (area < 0) p.reverse();
  return { pts: p, area: Math.abs(area) / 2 };
}

// Smallest rectangle round the footprint (one side along one of its edges): centre, long axis
// (unit), half length A along it and half width B across
function orientedBox(pts) {
  let best = null;
  for (let i = 0; i < pts.length; i++) {
    const a = pts[i], b = pts[(i + 1) % pts.length], l = Math.hypot(b[0] - a[0], b[1] - a[1]);
    if (l < 1e-3) continue;
    const ux = (b[0] - a[0]) / l, uz = (b[1] - a[1]) / l;
    let u0 = Infinity, u1 = -Infinity, v0 = Infinity, v1 = -Infinity;
    for (const [x, z] of pts) { const u = x * ux + z * uz, v = -x * uz + z * ux; u0 = Math.min(u0, u); u1 = Math.max(u1, u); v0 = Math.min(v0, v); v1 = Math.max(v1, v); }
    const area = (u1 - u0) * (v1 - v0);
    if (!best || area < best.area) best = { area, ux, uz, u0, u1, v0, v1 };
  }
  if (!best) return null;
  let { ux, uz, u0, u1, v0, v1 } = best;
  const cu = (u0 + u1) / 2, cv = (v0 + v1) / 2;
  let A = (u1 - u0) / 2, B = (v1 - v0) / 2;
  const cx = cu * ux - cv * uz, cz = cu * uz + cv * ux;
  if (B > A) { [A, B] = [B, A]; [ux, uz] = [-uz, ux]; }
  return { cx, cz, ux, uz, A, B, area: 4 * A * B };
}

// ------------------------------------------------------------------ geometry builder

// Geometry for one mesh: positions, normals, colours, uvs, a (vec4) and b (float) per vertex
function makeMesh() { return { pos: [], nrm: [], col: [], uv: [], a: [], b: [] }; }
function pushTri(g, P, UV, n, col, a, b) {
  // P: three course-space points; UV: three [u, v]; n: course-space normal
  const W = (p) => [-p[0], p[1], p[2]];
  let [p0, p1, p2] = P.map(W), [t0, t1, t2] = UV;
  const e1 = [p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]], e2 = [p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]];
  const fn = [e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]];
  const N = [-n[0], n[1], n[2]];
  if (fn[0] * N[0] + fn[1] * N[1] + fn[2] * N[2] < 0) { [p1, p2] = [p2, p1]; [t1, t2] = [t2, t1]; }
  g.pos.push(...p0, ...p1, ...p2);
  g.uv.push(...t0, ...t1, ...t2);
  for (let i = 0; i < 3; i++) { g.nrm.push(...N); g.col.push(col.r, col.g, col.b); g.a.push(...a); g.b.push(b); }
}
const quad = (g, P, UV, n, col, a, b) => { pushTri(g, [P[0], P[1], P[2]], [UV[0], UV[1], UV[2]], n, col, a, b); pushTri(g, [P[0], P[2], P[3]], [UV[0], UV[2], UV[3]], n, col, a, b); };
const norm = (v) => { const l = Math.hypot(...v) || 1; return v.map((x) => x / l); };

// ------------------------------------------------------------------ one building

// kind: the building's type (buildingtypes.js)
function plan(b, kind, photo) {
  const fp = cleanFootprint(b.pts);
  if (!fp || fp.area < 4) return null;
  const box = orientedBox(fp.pts);
  if (!box) return null;
  const [sx, sz] = fp.pts[0], r = (k) => hash(sx, sz, k);
  const rect = fp.area / box.area;
  const type = kind === 'commercial' ? 'large' : kind === 'clubhouse' ? 'club' : kind || 'house';
  // Storeys and the height of the walls (to the eaves)
  const lv = b.levels;
  const wallH = {
    house: () => (lv || (fp.area > 130 && r(1) > 0.45 ? 2 : 1)) * 2.8 + 0.2,
    club: () => (lv ? lv * 3.4 : 4.2),
    garage: () => 2.7,
    shed: () => Math.min(2.6, Math.max(2.2, (b.h || 3) - 1)),
    barn: () => Math.max(4.2, Math.min(6, (b.h || 7) - 3)),
    porch: () => 2.6,
    large: () => (lv ? lv * 3.6 : Math.max(3.8, Math.min((b.h || 6) - 1.5, 9))),
  }[type]();
  // Roof shape (the map's, else the usual one for the type; a pitched roof needs a footprint close
  // to a rectangle - it's drawn over that rectangle)
  const tag = String(b.roof || '').toLowerCase();
  const pitchedOk = rect > (type === 'club' || type === 'porch' ? 0.5 : 0.62) && box.B > 1;
  let roof;
  if (tag === 'flat') roof = 'flat';
  else if (['gabled', 'skillion', 'saltbox', 'gambrel', 'mansard'].includes(tag)) roof = pitchedOk ? 'gabled' : 'flat';
  else if (['hipped', 'pyramidal', 'half-hipped'].includes(tag)) roof = pitchedOk ? 'hipped' : 'flat';
  else if (!pitchedOk) roof = 'flat';
  else roof = {
    house: r(3) < 0.55 ? 'gabled' : 'hipped', club: 'hipped', garage: 'gabled', shed: 'gabled', barn: 'gabled',
    porch: r(3) < 0.6 ? 'gabled' : 'hipped', large: fp.area < 2500 && r(2) > 0.25 ? 'hipped' : 'flat',
  }[type];
  const deg = { house: 28 + r(4) * 14, club: 24, garage: 24, shed: 20, barn: 38, porch: 20, large: 18 }[type];
  const pitch = deg * Math.PI / 180;
  // Walls
  const mat = String(b.material || '').toLowerCase();
  const style = type === 'shed' || type === 'barn' ? STYLE.shed : type === 'large' || type === 'club' ? STYLE.large
    : /brick/.test(mat) ? STYLE.brick : /wood|timber/.test(mat) ? STYLE.siding : /plaster|stucco|concrete|cement/.test(mat) ? STYLE.stucco
    : type === 'garage' ? STYLE.siding : r(5) < 0.55 ? STYLE.siding : r(5) < 0.8 ? STYLE.brick : STYLE.stucco;
  const pal = { large: PALETTE.large, club: PALETTE.club, shed: PALETTE.shed, barn: PALETTE.barn, porch: PALETTE.siding }[type]
    || (style === STYLE.brick ? PALETTE.brick : style === STYLE.stucco ? PALETTE.stucco : PALETTE.siding);
  const wallCol = parseColour(b.wallColour) || srgb(pick(pal, r(6)));
  // Roof colour: the map's, else the aerial photo's (a few spots inside the roof), else a usual one
  const roofKind = roof === 'flat' ? ROOF.flat : type === 'barn' ? ROOF.metal : type === 'shed' && r(7) < 0.6 ? ROOF.metal
    : (type === 'large' || type === 'porch') && r(7) < 0.4 ? ROOF.metal : ROOF.shingle;
  let roofCol = parseColour(b.roofColour);
  if (!roofCol && photo) {
    let rr = 0, gg = 0, bb = 0, n = 0;
    for (const [fu, fv] of [[0, 0], [0.4, 0.4], [-0.4, 0.4], [0.4, -0.4], [-0.4, -0.4]]) {
      const x = box.cx + box.ux * box.A * fu - box.uz * box.B * fv, z = box.cz + box.uz * box.A * fu + box.ux * box.B * fv;
      const c = photo(x, z);
      if (c) { rr += c[0]; gg += c[1]; bb += c[2]; n++; }
    }
    if (n) {
      rr /= n; gg /= n; bb /= n;
      // (not where a tree hangs over it, or the photo is in deep shadow)
      if (!(gg > rr * 1.08 && gg > bb * 1.05) && rr + gg + bb > 60) {
        roofCol = new THREE.Color().setRGB(rr / 255, gg / 255, bb / 255, THREE.SRGBColorSpace);
        // (the photo is lit and hazy: a little darker and richer)
        const hsl = {}; roofCol.getHSL(hsl); roofCol.setHSL(hsl.h, Math.min(1, hsl.s * 1.15), hsl.l * 0.8);
      }
    }
  }
  if (!roofCol) roofCol = srgb(pick(roofKind === ROOF.flat ? PALETTE.flat : roofKind === ROOF.metal ? PALETTE.metal : PALETTE.roof, r(8)));
  // Doors on the longest wall: a front door, a garage door, a plank door, big barn doors
  const door = { house: 1, club: 1, large: 1, garage: 2, shed: 3, barn: 4, porch: 0 }[type];
  return { fp, box, rect, type, wallH, roof, pitch, style, wallCol, roofCol, roofKind, door, seed: r(9), chimney: type === 'house' && roof !== 'flat' && r(10) < 0.45, doorCol: srgb(pick(PALETTE.door, r(11))) };
}

function parseColour(s) {
  if (!s) return null;
  try { const c = new THREE.Color(); c.setStyle(String(s).trim()); return c; } catch { return null; }
}

function addBuilding(W, R, P, groundAt) {
  const { fp, box, type, wallH, roof, pitch, style, wallCol, roofCol, roofKind, seed } = P;
  // The footprint drawn: the real one for flat roofs, its rectangle under a pitched roof
  const { cx, cz, ux, uz, A, B } = box;
  const at = (u, v) => [cx + ux * u - uz * v, cz + uz * u + ux * v];
  const pts = roof === 'flat' ? fp.pts : [at(-A, -B), at(A, -B), at(A, B), at(-A, B)];
  let gMin = Infinity, gMax = -Infinity;
  for (const [x, z] of pts) { const y = groundAt(x, z); if (y !== null) { gMin = Math.min(gMin, y); gMax = Math.max(gMax, y); } }
  if (!Number.isFinite(gMin)) return false;
  // (a plinth up to the highest ground under it, then the floor)
  // (sheds and barns sit nearly on the ground: just a low concrete curb)
  const floorY = gMax + (type === 'large' || type === 'club' ? 0.15 : type === 'porch' || type === 'shed' || type === 'barn' || type === 'garage' ? 0.1 : 0.35), base = gMin - 0.6, eave = floorY + wallH;
  const parapet = roof === 'flat' ? (type === 'large' || type === 'club' ? 0.7 : type === 'porch' ? 0.25 : 0.35) : 0;
  const top = eave + parapet;
  // The front (or garage) door goes on the longest wall, usually the front of a house
  let doorWall = -1, longest = 0;
  for (let i = 0; i < pts.length; i++) { const a = pts[i], c = pts[(i + 1) % pts.length], l = Math.hypot(c[0] - a[0], c[1] - a[1]); if (l > longest) { longest = l; doorWall = i; } }
  const doorKind = P.door === 2 && longest < 2.6 ? 3 : P.door;
  if (type === 'porch') {
    // A roof on posts: a concrete slab, a post at each corner and every 3 m or so between, and a
    // beam along the top of each side; no walls
    const slab = srgb(0x9a9890), wood = srgb(P.seed > 0.5 ? 0xe8e4da : 0x6a5040);
    const contour = pts.map(([x, z]) => new THREE.Vector2(x, z));
    for (const [i0, i1, i2] of THREE.ShapeUtils.triangulateShape(contour, [])) pushTri(R, [i0, i1, i2].map((k) => [pts[k][0], floorY, pts[k][1]]), [i0, i1, i2].map((k) => [pts[k][0], pts[k][1]]), [0, 1, 0], slab, [ROOF.flat, seed, 0, 0], 0);
    const box4 = (x, z, w, y0, y1, col) => {
      const c4 = [[-w, -w], [w, -w], [w, w], [-w, w]];
      for (let k = 0; k < 4; k++) {
        const [a0, b0] = c4[k], [a1, b1] = c4[(k + 1) % 4];
        quad(R, [[x + a0, y0, z + b0], [x + a1, y0, z + b1], [x + a1, y1, z + b1], [x + a0, y1, z + b0]], [[0, 0], [1, 0], [1, 1], [0, 1]], norm([a0 + a1, 0, b0 + b1]), col, [ROOF.trim, seed, 0, 0], 0);
      }
    };
    for (let i = 0; i < pts.length; i++) {
      const a = pts[i], c = pts[(i + 1) % pts.length], len = Math.hypot(c[0] - a[0], c[1] - a[1]), n = Math.max(1, Math.round(len / 3));
      for (let k = 0; k < n; k++) box4(a[0] + ((c[0] - a[0]) * k) / n, a[1] + ((c[1] - a[1]) * k) / n, 0.09, floorY, eave, wood);
      const nx = (c[1] - a[1]) / (len || 1), nz = -(c[0] - a[0]) / (len || 1);
      for (const s2 of [1, -1]) quad(R, [[a[0] + nx * 0.06 * s2, eave - 0.25, a[1] + nz * 0.06 * s2], [c[0] + nx * 0.06 * s2, eave - 0.25, c[1] + nz * 0.06 * s2], [c[0] + nx * 0.06 * s2, eave, c[1] + nz * 0.06 * s2], [a[0] + nx * 0.06 * s2, eave, a[1] + nz * 0.06 * s2]], [[0, 0], [1, 0], [1, 1], [0, 1]], [nx * s2, 0, nz * s2], wood, [ROOF.trim, seed, 0, 0], 0);
    }
  }
  for (let i = 0; i < pts.length && type !== 'porch'; i++) {
    const a = pts[i], c = pts[(i + 1) % pts.length], dx = c[0] - a[0], dz = c[1] - a[1], len = Math.hypot(dx, dz);
    if (len < 0.05) continue;
    const n = [dz / len, 0, -dx / len];
    const f = [len, style, seed + i * 0.137, wallH];
    const v0 = base - floorY, v1 = top - floorY;
    quad(W, [[a[0], base, a[1]], [c[0], base, c[1]], [c[0], top, c[1]], [a[0], top, a[1]]], [[0, v0], [len, v0], [len, v1], [0, v1]], n, wallCol, f, i === doorWall ? doorKind : 0);
  }
  // Roof
  if (roof === 'flat') {
    const contour = pts.map(([x, z]) => new THREE.Vector2(x, z));
    const tris = THREE.ShapeUtils.triangulateShape(contour, []);
    // (a porch's flat roof is a thin slab at the top of its posts)
    const y = type === 'porch' ? eave + 0.2 : top - parapet * 0.7;
    for (const [i0, i1, i2] of tris) pushTri(R, [i0, i1, i2].map((k) => [pts[k][0], y, pts[k][1]]), [i0, i1, i2].map((k) => [pts[k][0], pts[k][1]]), [0, 1, 0], roofCol, [ROOF.flat, seed, 0, 0], 0);
    // (the parapet's top, a coping in the wall's colour, a little lighter)
    const cap = wallCol.clone().multiplyScalar(1.1);
    for (let i = 0; i < pts.length; i++) {
      const a = pts[i], c = pts[(i + 1) % pts.length], dx = c[0] - a[0], dz = c[1] - a[1], len = Math.hypot(dx, dz);
      if (len < 0.05) continue;
      const nx = dz / len * 0.25, nz = -dx / len * 0.25;
      quad(R, [[a[0], top, a[1]], [c[0], top, c[1]], [c[0] - nx, top, c[1] - nz], [a[0] - nx, top, a[1] - nz]], [[0, 0], [len, 0], [len, 0.25], [0, 0.25]], [0, 1, 0], cap, [ROOF.trim, seed, 0, 0], 0);
    }
    return true;
  }
  const o = { large: 0.6, club: 0.9, shed: 0.3, barn: 0.4, porch: 0.3 }[type] ?? 0.45, t = Math.tan(pitch);
  const ridge = eave + B * t, low = eave - o * t;
  const P3 = (u, v, y) => { const [x, z] = at(u, v); return [x, y, z]; };
  const slopeLen = (B + o) / Math.cos(pitch);
  const nSide = (s) => { const nn = norm([-uz * s * Math.sin(pitch), Math.cos(pitch), ux * s * Math.sin(pitch)]); return nn; };
  const rk = [roofKind, seed, 0, 0];
  if (roof === 'gabled') {
    for (const s of [-1, 1]) {
      // each side: eave (v = s(B+o)) up to the ridge (v = 0), from one gable to the other (+ overhang)
      quad(R, [P3(-A - o, s * (B + o), low), P3(A + o, s * (B + o), low), P3(A + o, 0, ridge), P3(-A - o, 0, ridge)],
        [[0, 0], [2 * (A + o), 0], [2 * (A + o), slopeLen], [0, slopeLen]], nSide(s), roofCol, rk, 0);
    }
    // gable ends: wall triangles up to the ridge (with the wall's pattern, no windows above the
    // eaves); a porch's are boarded in with vertical planks above its beams
    const gableCol = type === 'porch' ? srgb(P.seed > 0.5 ? 0xd8cdb4 : 0x7a5a40) : P.wallCol, gableStyle = type === 'porch' ? STYLE.shed : style;
    for (const s of [-1, 1]) {
      const n = [ux * s, 0, uz * s];
      const len = 2 * B;
      pushTri(W, [P3(s * A, -B, eave), P3(s * A, B, eave), P3(s * A, 0, ridge)], [[0, eave - floorY], [len, eave - floorY], [B, ridge - floorY]], n, gableCol, [len, gableStyle, seed + 7.7 + s, wallH], 0);
    }
    // fascia boards along the eaves and up the verges (white or dark trim)
    const trim = srgb(P.seed > 0.5 ? 0xf2f2ee : 0x3a3a3a);
    for (const s of [-1, 1]) {
      quad(R, [P3(-A - o, s * (B + o), low), P3(A + o, s * (B + o), low), P3(A + o, s * (B + o), low - 0.18), P3(-A - o, s * (B + o), low - 0.18)], [[0, 0], [1, 0], [1, 1], [0, 1]], [-uz * s, 0, ux * s], trim, [ROOF.trim, seed, 0, 0], 0);
      for (const e of [-1, 1]) quad(R, [P3(e * (A + o), s * (B + o), low), P3(e * (A + o), 0, ridge), P3(e * (A + o), 0, ridge - 0.18), P3(e * (A + o), s * (B + o), low - 0.18)], [[0, 0], [1, 0], [1, 1], [0, 1]], [ux * e, 0, uz * e], trim, [ROOF.trim, seed, 0, 0], 0);
    }
  } else {
    // hipped: a ridge along the middle (a point for a square), four slopes
    const hr = Math.max(0, A - B);
    for (const s of [-1, 1]) {
      quad(R, [P3(-A - o, s * (B + o), low), P3(A + o, s * (B + o), low), P3(hr, 0, ridge), P3(-hr, 0, ridge)],
        [[0, 0], [2 * (A + o), 0], [A + o + hr, slopeLen], [A + o - hr, slopeLen]], nSide(s), roofCol, rk, 0);
      const ne = norm([ux * s * Math.sin(pitch), Math.cos(pitch), uz * s * Math.sin(pitch)]);
      pushTri(R, [P3(s * (A + o), -B - o, low), P3(s * (A + o), B + o, low), P3(s * hr, 0, ridge)], [[0, 0], [2 * (B + o), 0], [B + o, slopeLen]], ne, roofCol, rk, 0);
    }
    const trim = srgb(P.seed > 0.5 ? 0xf2f2ee : 0x3a3a3a);
    const c4 = [[-1, -1], [1, -1], [1, 1], [-1, 1]];
    for (let k = 0; k < 4; k++) {
      const [ua, va] = c4[k], [ub, vb] = c4[(k + 1) % 4];
      const pa = P3(ua * (A + o), va * (B + o), low), pb = P3(ub * (A + o), vb * (B + o), low);
      const mu = (ua + ub) / 2, mv = (va + vb) / 2;
      quad(R, [pa, pb, [pb[0], low - 0.18, pb[2]], [pa[0], low - 0.18, pa[2]]], [[0, 0], [1, 0], [1, 1], [0, 1]], [ux * mu - uz * mv, 0, uz * mu + ux * mv], trim, [ROOF.trim, seed, 0, 0], 0);
    }
  }
  // Chimney: brick, through the roof near one end of the ridge
  if (P.chimney) {
    const cu = (seed > 0.5 ? 1 : -1) * Math.max(0, Math.min(A - 1.2, A * 0.55)), cv = (seed * 7 % 1 > 0.5 ? 0.3 : -0.3), w = 0.35;
    const y0 = ridge - 1.2, y1 = ridge + 0.9;
    const brick = srgb(pick(PALETTE.brick, seed * 3 % 1));
    const corners = [[-w, -w], [w, -w], [w, w], [-w, w]];
    for (let k = 0; k < 4; k++) {
      const [a0, b0] = corners[k], [a1, b1] = corners[(k + 1) % 4];
      const n = [ux * (a0 + a1) - uz * (b0 + b1), 0, uz * (a0 + a1) + ux * (b0 + b1)];
      quad(W, [P3(cu + a0, cv + b0, y0), P3(cu + a1, cv + b1, y0), P3(cu + a1, cv + b1, y1), P3(cu + a0, cv + b0, y1)], [[0, -8], [0.7, -8], [0.7, -8 + 2.1], [0, -8 + 2.1]], norm(n), brick, [0.7, STYLE.brick, seed, 0], 0);
    }
    quad(R, [P3(cu - w, cv - w, y1), P3(cu + w, cv - w, y1), P3(cu + w, cv + w, y1), P3(cu - w, cv + w, y1)], [[0, 0], [1, 0], [1, 1], [0, 1]], [0, 1, 0], srgb(0x2a2a2a), [ROOF.trim, seed, 0, 0], 0);
  }
  return true;
}

// ------------------------------------------------------------------ materials

const clampT = (x) => min(max(x, float(0)), float(1));
const h21 = (p) => fract(sin(dot(p, vec2(12.9898, 78.233))).mul(43758.5453));

// Walls: uv = metres along the wall and up from the floor; a = [wall length, style, seed, wall
// height]; b = door (1 front door, 2 garage door, 3 shed door, 4 barn doors)
function wallMaterial() {
  const m = new THREE.MeshStandardNodeMaterial({ vertexColors: true, roughness: 0.85, metalness: 0 });
  const t = uv(), u = t.x, v = t.y;
  // (seed rounded: it's the same at every corner of a wall, but blended across the triangles it
  // differs in the last digits from pixel to pixel, which the random hashes blow up into speckle)
  const a = attribute('a', 'vec4'), len = a.x, style = a.y, seed = floor(a.z.mul(256).add(0.5)).div(256), wallH = a.w, door = attribute('b', 'float');
  const is = (s) => step(abs(style.sub(s)), 0.5);
  const isLarge = is(STYLE.large), isShed = is(STYLE.shed);
  // Windows: evenly along each wall (one every ~3 m on houses, ~2.4 m on large buildings), on every
  // floor, a few left out at random; none on sheds, short walls, or above the eaves
  const floorH = mix(float(2.8), float(3.6), isLarge);
  const target = mix(float(3.2), float(2.4), isLarge);
  const nWin = max(floor(len.div(target)), 1), spacing = len.div(nWin);
  const wi = floor(u.div(spacing)), du = fract(u.div(spacing)).sub(0.5).mul(spacing);
  const fi = floor(v.div(floorH)), fv = v.sub(fi.mul(floorH));
  const winW = mix(float(1.05), float(1.9), isLarge).mul(min(spacing.div(target), 1)), winH = mix(float(1.35), float(2.1), isLarge), sill = mix(float(0.95), float(0.55), isLarge);
  const keep = step(0.14, h21(vec2(wi.add(seed.mul(91)), fi.add(seed.mul(17)))));
  const inside = (hw, y0, y1) => step(abs(du), hw).mul(step(y0, fv)).mul(step(fv, y1));
  const allowed = keep.mul(step(2.2, len)).mul(isShed.oneMinus()).mul(step(0, v)).mul(step(v, wallH.sub(0.35)));
  // (not over the front door)
  const doorZone = step(0.5, door).mul(step(abs(u.sub(len.mul(0.5))), 1.3)).mul(step(fi, 0.5));
  const win = allowed.mul(doorZone.oneMinus());
  const glass = inside(winW.mul(0.5), sill, sill.add(winH)).mul(win);
  const frame = inside(winW.mul(0.5).add(0.09), sill.sub(0.09), sill.add(winH).add(0.09)).mul(win).sub(glass).max(0);
  // muntins on house windows: a cross; big windows are split like storefront glazing: a mullion
  // down the middle and a transom with a short top light, in thicker bars
  const mid = sill.add(winH.mul(0.5));
  const houseBars = step(abs(du), 0.025).max(step(abs(fv.sub(mid)), 0.025));
  const transom = sill.add(winH.mul(0.78));
  const bigBars = step(abs(du), 0.05).max(step(abs(fv.sub(transom)), 0.05));
  const muntin = glass.mul(mix(houseBars, bigBars, isLarge));
  // Doors
  const front = step(abs(door.sub(1)), 0.5).mul(step(abs(u.sub(len.mul(0.5))), 0.48)).mul(step(0, v)).mul(step(v, 2.1));
  const gw = min(float(1.4), len.mul(0.38));
  const garage = step(abs(door.sub(2)), 0.5).mul(step(abs(u.sub(len.mul(0.5))), gw)).mul(step(v, 2.15));
  const plank = step(abs(door.sub(3)), 0.5).mul(step(abs(u.sub(len.mul(0.5))), 0.55)).mul(step(v, 1.95));
  // barn doors: a big double door in the middle, white-trimmed with a cross brace on each leaf
  const bw = min(float(2.2), len.mul(0.3)), bu0 = u.sub(len.mul(0.5)), bh = min(float(3.6), wallH.sub(0.4));
  const barnDoor = step(abs(door.sub(4)), 0.5).mul(step(abs(bu0), bw)).mul(step(v, bh)).mul(step(0, v));
  const leaf = abs(bu0).div(bw), lv = v.div(bh); // (0-1 across each leaf, 0-1 up)
  const brace = max(step(abs(leaf.sub(lv)), 0.035), step(abs(leaf.add(lv).sub(1)), 0.035))
    .max(step(leaf, 0.03)).max(step(0.97, leaf)).max(step(lv, 0.03)).max(step(0.97, lv)).mul(barnDoor);
  // Wall surface: siding (lap boards every 20 cm), brick courses, plain stucco, large (a band at
  // each floor), shed (vertical planks); a grey concrete plinth below the floor; corner boards
  const lap = smoothstep(0.0, 0.035, fract(v.div(0.2))).mul(0.14).add(0.86);
  const course = floor(v.div(0.075)), bu = u.add(course.mul(0.11)).div(0.23);
  const mortar = max(step(fract(v.div(0.075)), 0.13), step(fract(bu), 0.06));
  const brickVar = h21(vec2(floor(bu), course)).mul(0.22).add(0.89);
  const base = attribute('color', 'vec3');
  const planks = smoothstep(0, 0.04, fract(u.div(0.3))).mul(0.12).add(0.88);
  // Board and batten (barns, sheds): 30 cm boards, each its own shade, with a narrow batten over
  // each joint (lit on one edge, shadowed on the other), wood grain running up the boards, and
  // weathering - darker and streaked towards the ground
  const bi = floor(u.div(0.3)), bx = fract(u.div(0.3));
  const boardShade = h21(vec2(bi, seed.mul(31))).mul(0.22).add(0.86);
  const batten = step(bx, 0.07).max(step(0.93, bx));
  const battenEdge = smoothstep(0.07, 0.1, bx).oneMinus().mul(step(0.07, bx)).mul(0.25);
  const grain = sin(v.mul(9).add(sin(v.mul(1.7).add(bi.mul(3.1))).mul(2.5)).add(bx.mul(14))).mul(0.045)
    .add(sin(v.mul(31).add(bi.mul(7.3))).mul(0.02)).add(1);
  const weather = smoothstep(-0.2, 1.4, v).mul(0.25).add(0.75).mul(h21(vec2(bi, floor(v.mul(1.3)))).mul(0.08).add(0.96));
  const boards = base.mul(boardShade).mul(grain).mul(weather).mul(mix(float(1), float(1.08), batten)).mul(battenEdge.oneMinus());
  const band = step(fract(v.div(floorH)), 0.06).mul(0.12);
  let wall = base.toVar();
  wall = select(is(STYLE.siding).greaterThan(0.5), base.mul(lap), wall);
  wall = select(is(STYLE.brick).greaterThan(0.5), mix(base.mul(brickVar), vec3(0.42, 0.4, 0.36), mortar), wall);
  wall = select(isShed.greaterThan(0.5), boards, wall);
  wall = select(isLarge.greaterThan(0.5), base.mul(band.oneMinus()), wall);
  // (corner boards: siding, and barns and sheds too, in white or dark trim)
  const corner = max(is(STYLE.siding), isShed).mul(max(step(u, 0.14), step(len.sub(0.14), u))).mul(step(0, v));
  wall = mix(wall, vec3(0.85, 0.85, 0.82), corner);
  const plinth = step(v, 0).mul(step(-4, v)); // (chimneys use v below -4: no plinth there)
  wall = mix(wall, vec3(0.2, 0.2, 0.19), plinth);
  // windows: dark glass with a hint of blue (the reflection does the rest), white or dark frames
  // (big windows: dark bronze or white frames)
  const trim = mix(mix(vec3(0.8, 0.8, 0.78), vec3(0.06, 0.06, 0.06), step(0.7, seed.fract())), mix(vec3(0.07, 0.055, 0.045), vec3(0.78, 0.78, 0.76), step(0.6, seed.fract())), isLarge);
  // glass: dark, a little lighter towards the top of each pane where it catches the sky
  const up = clampT(fv.sub(sill).div(winH));
  const glassCol = mix(vec3(0.02, 0.028, 0.036), vec3(0.09, 0.12, 0.15), up.mul(up)).mul(h21(vec2(wi, fi.add(seed))).mul(0.4).add(0.8));
  wall = mix(wall, trim, frame.max(muntin));
  wall = mix(wall, glassCol, glass.sub(muntin).max(0));
  const doorCol = vec3(0.12, 0.05, 0.035).mul(h21(vec2(seed, 3.1)).mul(0.8).add(0.6));
  wall = mix(wall, doorCol, front);
  const gLines = smoothstep(0, 0.03, fract(v.div(0.53))).mul(0.15).add(0.85);
  wall = mix(wall, vec3(0.75, 0.74, 0.7).mul(gLines), garage);
  wall = mix(wall, base.mul(0.6).mul(planks), plank);
  wall = mix(wall, base.mul(0.82).mul(planks), barnDoor);
  wall = mix(wall, vec3(0.86, 0.85, 0.82), brace);
  m.colorNode = wall;
  const isGlass = glass.sub(muntin).max(0);
  // (glass a little rough: mirror-smooth, it sparkled with speckles from the shadow map and sky)
  m.roughnessNode = mix(float(0.88), float(0.22), isGlass);
  m.metalnessNode = float(0);
  return m;
}

// Roofs: uv = metres along the eaves and up the slope (flat roofs: across); a = [style, seed]
function roofMaterial() {
  const m = new THREE.MeshStandardNodeMaterial({ vertexColors: true, roughness: 0.9, metalness: 0, side: THREE.DoubleSide });
  const t = uv(), a = attribute('a', 'vec4'), style = a.x, seed = floor(a.y.mul(256).add(0.5)).div(256);
  const is = (s) => step(abs(style.sub(s)), 0.5);
  const base = attribute('color', 'vec3');
  // shingles: courses every 25 cm, tabs staggered, each a little different
  const row = floor(t.y.div(0.25)), tab = floor(t.x.div(0.45).add(row.mul(0.5)));
  const shade = h21(vec2(tab, row.add(seed.mul(13)))).mul(0.24).add(0.88);
  const edge = smoothstep(0, 0.05, fract(t.y.div(0.25))).mul(0.25).add(0.75);
  const shingle = base.mul(shade).mul(edge);
  // metal: standing seams every 50 cm up the slope
  const seam = smoothstep(0.9, 1, abs(fract(t.x.div(0.5)).sub(0.5)).mul(2)).mul(-0.2).add(1);
  const metal = base.mul(seam);
  // flat: a membrane with faint patches
  const flat = base.mul(h21(floor(t.mul(1.5))).mul(0.1).add(0.95));
  let col = base.toVar();
  col = select(is(ROOF.shingle).greaterThan(0.5), shingle, col);
  col = select(is(ROOF.metal).greaterThan(0.5), metal, col);
  col = select(is(ROOF.flat).greaterThan(0.5), flat, col);
  m.colorNode = col;
  m.roughnessNode = select(is(ROOF.metal).greaterThan(0.5), float(0.5), float(0.92));
  m.metalnessNode = select(is(ROOF.metal).greaterThan(0.5), float(0.35), float(0));
  return m;
}

// ------------------------------------------------------------------ all of them

// buildings: [{ h, pts, kind?, levels?, roof?, roofColour?, wallColour?, material?, use? }];
// groundAt(x, z) -> height or null; photo(x, z) -> [r, g, b] (0-255) or null.
// Returns { meshes, count, types } (meshes in the scene's mirrored space).
// start: where the round starts and turns (for finding the clubhouse); types: the player's choices
export function buildBuildings(buildings, groundAt, photo, { start = [], types = {} } = {}) {
  const W = makeMesh(), R = makeMesh();
  const kinds = classifyBuildings(buildings, start, types), counts = {};
  let count = 0;
  buildings.forEach((b, i) => {
    if (!b.pts || b.pts.length < 4) return;
    const P = plan(b, kinds[i].type, photo);
    if (!P) return;
    if (addBuilding(W, R, P, groundAt)) { count++; counts[kinds[i].type] = (counts[kinds[i].type] || 0) + 1; }
  });
  const meshes = [];
  for (const [g, mat] of [[W, wallMaterial()], [R, roofMaterial()]]) {
    if (!g.pos.length) continue;
    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.Float32BufferAttribute(g.pos, 3));
    geo.setAttribute('normal', new THREE.Float32BufferAttribute(g.nrm, 3));
    geo.setAttribute('color', new THREE.Float32BufferAttribute(g.col, 3));
    geo.setAttribute('uv', new THREE.Float32BufferAttribute(g.uv, 2));
    geo.setAttribute('a', new THREE.Float32BufferAttribute(g.a, 4));
    geo.setAttribute('b', new THREE.Float32BufferAttribute(g.b, 1));
    const mesh = new THREE.Mesh(geo, mat);
    mesh.castShadow = true; mesh.receiveShadow = true;
    meshes.push(mesh);
  }
  return { meshes, count, types: counts };
}
