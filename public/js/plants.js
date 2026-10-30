// Shrubs, flowers and grasses around the course, beyond the trees and their undergrowth
// (trees.js) and the reeds (scene.js):
//   woods edges   - flowering shrubs on the open side of the trees along a wood: azaleas and hollies
//                   in the Southeast, rhododendrons in the Northwest, saw palmetto in Florida,
//                   creosote, prickly pear and yucca in the desert
//   woods floor   - ferns (and palmetto in the South) under the trees
//   tees          - a flower bed behind the back tee of some holes, with grasses or boxwood behind it
//   greens        - a few ornamental grasses on the mounds behind some greens
//   clubhouse     - boxwood hedges along its walls, flower beds in front of them, grasses at the
//                   corners; a few foundation shrubs round the houses near the holes
//   ponds         - ornamental grass clumps and irises on the bank (not in the line of play)
//   desert rough  - sagebrush, creosote, yucca and prickly pear scattered through the native areas
//                   of courses in dry country
// Never on fairways, greens, tees, bunkers, paths or water (the scene's ok() decides).
//
// Every plant is a few leaf cards cut from one picture (public/textures/plants/plants.png, drawn
// by scripts/plant-textures.py), drawn with the trees' leaf material, so a whole kind is one
// instanced mesh per 250 m chunk of the course, and chunks further from the camera than the kind
// is worth drawing are skipped. Cartoon draws the same spots as small toy shapes in flat colours.
import * as THREE from 'three/webgpu';
import { mergeGeometries } from 'three/addons/utils/BufferGeometryUtils.js';
import { leafMaterial, shapeCrown } from './trees.js';
import { hash, holeDistance } from './treeplace.js';
import { classifyBuildings } from './buildingtypes.js';
import { SHARED_INSTANCES } from './grass.js';
import { PLANTS, PLANT_REGIONS, ARID, courseMixes } from './plants-data.js';
export { PLANTS, PLANT_REGIONS, PLANT_GROUPS, plantsHere, coursePlants } from './plants-data.js';

const T = (x, y, z) => new THREE.Vector3(-x, y, z);
const pointInPoly = (poly, x, z) => {
  let inside = false;
  for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) {
    const [xi, zi] = poly[i], [xj, zj] = poly[j];
    if (zi > z !== zj > z && x < ((xj - xi) * (z - zi)) / (zj - zi) + xi) inside = !inside;
  }
  return inside;
};

const pick = (mix, n) => {
  const e = Object.entries(mix || {}), total = e.reduce((s, [, w]) => s + w, 0);
  let t = n * total;
  for (const [k, w] of e) { t -= w; if (t < 0) return k; }
  return e.length ? e[e.length - 1][0] : null;
};

// ------------------------------------------------------------------ where they grow

// ctx: { region, amount (the quality setting times the course's own), off / extra (Sets: the course's
// choices, see coursePlants), trees, holes, buildings, buildingTypes,
// shore: { perimeter, shoreSpot, nearPlay } | null, bounds, heightAt(x, z), ok(x, z, mown): a plant
// may grow here, at least `mown` m from mown grass, surface(x, z) }
// Returns [{ kind, x, z, y, w, h, rot, tint }]
export function placePlants(ctx) {
  const base = PLANT_REGIONS[ctx.region] || PLANT_REGIONS.temperate, A = ctx.amount ?? 1;
  const off = ctx.off || new Set(), R = courseMixes(base, off, ctx.extra || new Set());
  const out = [], taken = new Map(), key = (a, b) => a * 73856093 ^ b * 19349663;
  const free = (x, z, r) => {
    const gx = Math.floor(x / 4), gz = Math.floor(z / 4);
    for (let a = -1; a <= 1; a++) for (let b = -1; b <= 1; b++) for (const [px, pz, pr] of taken.get(key(gx + a, gz + b)) || []) if (Math.hypot(px - x, pz - z) < (r + pr) * 0.6) return false;
    return true;
  };
  // (size: the kind's, varied, times scale; rot: turned, or along a wall for hedges; open: not under a
  // tree's crown - flower beds and ornamental grasses are planted in the sun)
  const add = (kind, x, z, { scale = 1, rot = null, w = null, mown = 1.5, check = true, open = false } = {}) => {
    const P = PLANTS[kind];
    if (!P || off.has(kind)) return false;
    const s = scale * (0.8 + 0.45 * hash(x, z, 41.3)), ww = w ?? P.w * s;
    if (check && (!ctx.ok(x, z, mown) || !free(x, z, Math.min(ww, P.w * s) / 2))) return false;
    if (open && around(x, z, 16).some((t) => Math.hypot(t.x - x, t.z - z) < (t.rh || t.coneR || t.r || 3) + 0.5)) return false;
    const y = ctx.heightAt(x, z);
    if (y === null || y === undefined) return false;
    const k = key(Math.floor(x / 4), Math.floor(z / 4));
    if (!taken.has(k)) taken.set(k, []);
    taken.get(k).push([x, z, Math.min(ww, P.w * s) / 2]);
    out.push({ kind, x, z, y, w: ww, d: P.w * s, h: P.h * s * (0.9 + 0.2 * hash(x, z, 42.9)), rot: rot ?? hash(x, z, 43.7) * Math.PI * 2, tint: hash(x, z, 44.1) });
    return true;
  };
  const near = holeDistance((ctx.holes || []).map((h) => h.path)) || (() => 0);

  // ---- woods edges and floor
  const trees = ctx.trees || [];
  const cell = 14, grid = new Map();
  for (const t of trees) { const k = key(Math.floor(t.x / cell), Math.floor(t.z / cell)); if (!grid.has(k)) grid.set(k, []); grid.get(k).push(t); }
  const around = (x, z, r) => {
    const res = [], gx = Math.floor(x / cell), gz = Math.floor(z / cell);
    for (let a = -1; a <= 1; a++) for (let b = -1; b <= 1; b++) for (const o of grid.get(key(gx + a, gz + b)) || []) if (Math.hypot(o.x - x, o.z - z) < r) res.push(o);
    return res;
  };
  // (A: how many - the graphics setting times the course's own amount; at 1 the usual)
  let edges = 0, floor = 0;
  const maxEdge = 6000 * A, maxFloor = 9000 * A;
  for (const t of trees) {
    if (near(t.x, t.z) > 170) continue;
    const r = t.rh || t.coneR || t.r || 3;
    if (t.edge && R.edge && edges < maxEdge && hash(t.x, t.z, 31.7) < Math.min(0.95, 0.5 * A)) {
      // the open side: away from the trees round it
      let ox = 0, oz = 0;
      for (const o of around(t.x, t.z, cell)) if (o !== t) { const d = Math.hypot(o.x - t.x, o.z - t.z) || 1; ox -= (o.x - t.x) / d; oz -= (o.z - t.z) / d; }
      const ol = Math.hypot(ox, oz);
      if (ol < 0.3) continue;
      ox /= ol; oz /= ol;
      // (each stretch of woods leans to one kind, as they're planted or spread)
      const patch = hash(Math.floor(t.x / 45), Math.floor(t.z / 45), 32.9);
      const n = 1 + Math.floor(hash(t.x, t.z, 33.1) * 3);
      for (let i = 0; i < n; i++) {
        const turn = (hash(t.x + i, t.z, 34.3) - 0.5) * 2.2, c = Math.cos(turn), s = Math.sin(turn);
        const d = r * (0.6 + hash(t.x, t.z + i, 35.5) * 0.7) + 0.6;
        const kind = pick(R.edge, hash(t.x, t.z, 36.1 + i) < 0.75 ? patch : hash(t.x + i, t.z, 36.7));
        if (add(kind, t.x + (ox * c - oz * s) * d, t.z + (ox * s + oz * c) * d, { mown: 2.5, scale: 0.9 + hash(t.x, t.z, 37.3 + i) * 0.5 })) edges++;
      }
    } else if (!t.edge && R.floor && floor < maxFloor && near(t.x, t.z) < 130 && hash(t.x, t.z, 38.1) < Math.min(0.95, 0.55 * A)) {
      const n = 1 + Math.floor(hash(t.x, t.z, 38.9) * 4);
      for (let i = 0; i < n; i++) {
        const a = hash(t.x + i, t.z, 39.3) * Math.PI * 2, d = r * (0.25 + hash(t.x, t.z + i, 39.9) * 0.8);
        if (add(pick(R.floor, hash(t.x - i, t.z, 40.7)), t.x + Math.cos(a) * d, t.z + Math.sin(a) * d, { mown: 2 })) floor++;
      }
    }
  }

  // ---- flower beds behind the back tees, grasses behind greens
  const bedTypes = R.beds || ['bedred'];
  (ctx.holes || []).forEach((h, hi) => {
    const tees = Object.values(h.tees || {}).filter((p) => p && Number.isFinite(p.x));
    const next = h.path?.[1] || h.greenCenter;
    if (tees.length && next && hash(hi, 7.1, 45.3) < (R.beds_p ?? 0.5) * Math.min(1.6, A)) {
      // the tee furthest from the green, and which way the hole plays from it
      const g = h.greenCenter || next;
      const tee = tees.reduce((a, b) => (Math.hypot(b.x - g.x, b.z - g.z) > Math.hypot(a.x - g.x, a.z - g.z) ? b : a));
      let fx = next.x - tee.x, fz = next.z - tee.z;
      const fl = Math.hypot(fx, fz) || 1; fx /= fl; fz /= fl;
      const sx = fz, sz = -fx;
      // (behind the tee, a little to one side, so it's in the picture from the tee but not in play)
      const back = 9 + hash(hi, 1, 46.1) * 4, side = (hash(hi, 2, 46.9) - 0.5) * 6;
      const cx = tee.x - fx * back + sx * side, cz = tee.z - fz * back + sz * side;
      const len = 4 + hash(hi, 3, 47.3) * 4, dep = 1.6 + hash(hi, 4, 47.9) * 0.8;
      const colour = bedTypes[Math.floor(hash(hi, 5, 48.3) * bedTypes.length)];
      const second = bedTypes[Math.floor(hash(hi, 6, 48.7) * bedTypes.length)];
      for (let u = -len / 2; u <= len / 2; u += 1.0) for (let v = -dep / 2; v <= dep / 2; v += 1.0) {
        if ((u / (len / 2)) ** 2 + (v / (dep / 2 + 0.3)) ** 2 > 1.15) continue;
        // (a border of a second colour along the front of some beds)
        const kind = v > dep / 2 - 0.9 && second !== colour && hash(hi, 9, 49.1) < 0.5 ? second : colour;
        add(kind, cx + sx * u - fx * v, cz + sz * u - fz * v, { mown: 1.2, rot: Math.atan2(sz, sx) + (hash(u, v, 49.9) - 0.5) * 0.6, scale: 0.95, open: true });
      }
      // grasses or a low hedge behind the bed
      const behind = pick(R.grass, hash(hi, 10, 50.3));
      for (let u = -len / 2; u <= len / 2 + 0.1; u += 1.6) add(hash(hi, 11, 50.9) < 0.35 ? 'boxwood' : behind, cx + sx * u - fx * (dep / 2 + 1.1), cz + sz * u - fz * (dep / 2 + 1.1), { mown: 1.2, open: true });
    }
    if (h.greenCenter && h.greenDir && R.grass && hash(hi, 12, 51.3) < Math.min(0.9, 0.45 * A)) {
      const [dx, dz] = h.greenDir, gx = h.greenCenter.x, gz = h.greenCenter.z, gr = h.greenR || 12;
      const n = 2 + Math.floor(hash(hi, 13, 51.9) * 3), kind = pick(R.grass, hash(hi, 14, 52.3));
      for (let i = 0; i < n; i++) {
        const a = (hash(hi, 15 + i, 52.9) - 0.5) * 1.1, c = Math.cos(a), s = Math.sin(a), d = gr * 1.3 + 9 + hash(hi, 20 + i, 53.3) * 7;
        add(kind, gx + (dx * c - dz * s) * d, gz + (dx * s + dz * c) * d, { mown: 3.5, open: true });
      }
    }
  });

  // ---- the clubhouse: hedges along the walls, beds in front, grasses at the corners; houses near
  // the holes get a few shrubs by their walls
  const blds = ctx.buildings || [];
  if (blds.length) {
    const h = ctx.holes || [], teeOf = (k) => h[k] && Object.values(h[k].tees || {})[0], green = (k) => h[k]?.greenCenter;
    const start = [teeOf(0), green(8), teeOf(9), green(17)].filter((p) => p && Number.isFinite(p.x));
    const kinds = classifyBuildings(blds, start, ctx.buildingTypes || {});
    blds.forEach((b, bi) => {
      const type = kinds[bi]?.type, pts = b.pts;
      if (!pts || pts.length < 4 || (type !== 'clubhouse' && type !== 'house')) return;
      let cx = 0, cz = 0;
      for (const [x, z] of pts) { cx += x; cz += z; }
      cx /= pts.length; cz /= pts.length;
      if (type === 'house' && (near(cx, cz) > 220 || hash(cx, cz, 54.1) > 0.6)) return;
      const club = type === 'clubhouse';
      const n = pts.length - (pts[0][0] === pts[pts.length - 1][0] && pts[0][1] === pts[pts.length - 1][1] ? 1 : 0);
      for (let i = 0; i < n; i++) {
        const [ax, az] = pts[i], [bx, bz] = pts[(i + 1) % n], L = Math.hypot(bx - ax, bz - az);
        if (L < 2.5) continue;
        const ex = (bx - ax) / L, ez = (bz - az) / L;
        // (the outward side of this wall)
        let nx = ez, nz = -ex;
        if (pointInPoly(pts, (ax + bx) / 2 + nx, (az + bz) / 2 + nz)) { nx = -nx; nz = -nz; }
        const along = Math.atan2(-ez, -ex); // (local x along the wall, in the mirrored scene)
        if (club) {
          const bed = hash(ax, az, 55.3) < 0.6, colour = bedTypes[Math.floor(hash(ax, az, 55.9) * bedTypes.length)];
          for (let s = 0.8; s < L - 0.6; s += 0.9) {
            const x = ax + ex * s, z = az + ez * s;
            add('hedge', x + nx * 1.0, z + nz * 1.0, { rot: along, w: 1.05, mown: 1, scale: 1 });
            if (bed && s > 1.5 && s < L - 1.5) add(colour, x + nx * 2.3, z + nz * 2.3, { rot: along, mown: 1, scale: 0.9, open: true });
          }
          if (R.grass) add(pick(R.grass, hash(ax, az, 56.3)), ax + (nx - ex) * 2.2, az + (nz - ez) * 2.2, { mown: 1, open: true });
        } else if (hash(ax, az, 57.1) < 0.5) {
          for (let s = 1; s < L - 1; s += 1.6 + hash(ax + s, az, 57.7) * 2) {
            add(pick(R.edge || { boxwood: 1 }, hash(cx, cz, 58.3 + s)), ax + ex * s + nx * 1.0, az + ez * s + nz * 1.0, { mown: 1, scale: 0.75 });
          }
        }
      }
    });
  }

  // ---- pond banks: grass clumps and irises in stretches, off the line of play
  const sh = ctx.shore;
  if (sh?.perimeter && R.bank) {
    const want = Math.min(sh.perimeter * 0.05, 900) * A;
    let n = 0;
    for (let tries = 0; tries < want * 6 && n < want; tries++) {
      const s = sh.shoreSpot();
      if (!s || s.wb.sea || (s.wb.style === 'manicured' && hash(s.x, s.z, 59.3) < 0.7)) continue;
      if (hash(Math.floor(s.x / 30), Math.floor(s.z / 30), 59.9) > 0.4 || sh.nearPlay(s.x, s.z, s.nx, s.nz)) continue;
      const kind = pick(R.bank, hash(s.x, s.z, 60.3));
      const d = PLANTS[kind].form === 'flowers' ? 0.5 + hash(s.x, s.z, 60.9) * 0.7 : 1.5 + hash(s.x, s.z, 61.3) * 2.5;
      if (add(kind, s.x + s.nx * d, s.z + s.nz * d, { mown: 2.5, open: true })) n++;
    }
  }

  // ---- dry country: scrub through the native areas (and the rough itself, in the desert)
  if (R.rough && Object.keys(R.rough).length && ctx.bounds) {
    const { minX, maxX, minZ, maxZ } = ctx.bounds, STEP = 7, max = 14000 * A;
    const arid = base === ARID || ctx.region === 'socal';
    const SCRUB = arid ? new Set(['rough', 'deeprough', 'native', 'oob', null]) : new Set(['native', 'oob']);
    let n = 0;
    for (let gz = Math.floor(minZ / STEP); gz <= Math.ceil(maxZ / STEP) && n < max; gz++) for (let gx = Math.floor(minX / STEP); gx <= Math.ceil(maxX / STEP) && n < max; gx++) {
      const x = (gx + hash(gx, gz, 62.3)) * STEP, z = (gz + hash(gx, gz, 62.9)) * STEP;
      const dist = near(x, z);
      if (dist > 140) continue;
      // patchy: some stretches thick with scrub, others nearly bare
      const dense = hash(Math.floor(x / 35), Math.floor(z / 35), 63.3);
      if (hash(gx, gz, 63.9) > (arid ? 0.25 + dense * 0.6 : dense * 0.25) * Math.sqrt(A)) continue;
      if (!SCRUB.has(ctx.surface(x, z))) continue;
      if (add(pick(R.rough, hash(gx, gz, 64.3) < 0.7 ? dense : hash(gx, gz, 64.9)), x, z, { mown: 4 })) n++;
    }
  }
  return out;
}

// ------------------------------------------------------------------ the pictures on cards

function rng(seed) {
  let a = seed | 0;
  return () => { a = (a + 0x6D2B79F5) | 0; let t = Math.imul(a ^ (a >>> 15), 1 | a); t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t; return ((t ^ (t >>> 14)) >>> 0) / 4294967296; };
}
// A quad from a, b (bottom corners) up to c, d (top), showing cell (0-15) of the picture (rows
// from the top, as the picture is laid out; with the usual flipped textures v = 1 is its top)
function quad(parts, a, b, c, d, cellIx, v0 = 0, v1 = 1) {
  const e = 0.004, u0 = (cellIx % 4) / 4 + e, u1 = (cellIx % 4 + 1) / 4 - e, row = Math.floor(cellIx / 4);
  const vb = 1 - (row + 1) / 4 + e, vt = 1 - row / 4 - e, va = vb + (vt - vb) * v0, vz = vb + (vt - vb) * v1;
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute([...a, ...b, ...c, ...d], 3));
  g.setAttribute('uv', new THREE.Float32BufferAttribute([u0, va, u1, va, u1, vz, u0, vz], 2));
  g.setIndex([0, 1, 2, 0, 2, 3]);
  parts.push(g);
}
// An upright card w wide and h tall standing at (x, z), facing along angle a, leaning out by lean
function upright(parts, x, z, a, w, h, cellIx, lean = 0, y = 0) {
  const sx = Math.cos(a) * w / 2, sz = Math.sin(a) * w / 2, ox = -Math.sin(a) * Math.sin(lean) * h, oz = Math.cos(a) * Math.sin(lean) * h, th = Math.cos(lean) * h;
  quad(parts, [x - sx, y, z - sz], [x + sx, y, z + sz], [x + sx + ox, y + th, z + sz + oz], [x - sx + ox, y + th, z - sz + oz], cellIx);
}
const done = (parts) => { const g = mergeGeometries(parts); g.computeVertexNormals(); shapeCrown(g); return g; };

// Each form about 1 across and 1 tall, standing on y = 0 (hedges: 1 long along x)
const FORMS = {
  // leafy cards over a dome, facing out (as trees.js's undergrowth)
  bush(cellIx, cards = 30) {
    const r = rng(cellIx * 31 + cards), parts = [], o = new THREE.Object3D();
    for (let i = 0; i < cards; i++) {
      const az = (i / cards) * Math.PI * 2 + r() * 0.6, el = r() ** 0.8 * 1.25, rad = 0.22 + r() * 0.18;
      const cx = Math.cos(el) * Math.cos(az) * rad, cy = 0.28 + Math.sin(el) * 0.42, cz = Math.cos(el) * Math.sin(az) * rad, s = 0.55 + r() * 0.3;
      const g = new THREE.PlaneGeometry(s, s);
      const e = 0.004, row = Math.floor(cellIx / 4), uv = g.attributes.uv;
      for (let k = 0; k < uv.count; k++) uv.setXY(k, (cellIx % 4 + e + uv.getX(k) * (1 - 2 * e)) / 4, (3 - row + e + uv.getY(k) * (1 - 2 * e)) / 4);
      o.position.set(cx, cy, cz); o.rotation.set(0, 0, 0);
      o.lookAt(cx * 3, cy * 1.6 + 0.2, cz * 3);
      o.rotateZ(r() * Math.PI * 2);
      o.updateMatrix();
      g.applyMatrix4(o.matrix);
      parts.push(g.toNonIndexed());
    }
    return done(parts.map((g) => { g.setIndex(null); return g; }));
  },
  // a clipped hedge 1 m long: cards over a rounded box, overlapping the next length a little
  hedge(cellIx) {
    const r = rng(cellIx * 17 + 5), parts = [], o = new THREE.Object3D();
    for (let i = 0; i < 36; i++) {
      const x = -0.72 + r() * 1.44, side = r(), s = 0.55 + r() * 0.2;
      let p, n;
      if (side < 0.35) { p = [x, 0.92 + r() * 0.06, (r() - 0.5) * 0.7]; n = [0, 1, 0]; }
      else { const z = side < 0.67 ? 0.4 : -0.4; p = [x, 0.2 + r() * 0.7, z]; n = [0, 0.15, Math.sign(z)]; }
      const g = new THREE.PlaneGeometry(s, s);
      const e = 0.004, row = Math.floor(cellIx / 4), uv = g.attributes.uv;
      for (let k = 0; k < uv.count; k++) uv.setXY(k, (cellIx % 4 + e + uv.getX(k) * (1 - 2 * e)) / 4, (3 - row + e + uv.getY(k) * (1 - 2 * e)) / 4);
      o.position.set(...p); o.rotation.set(0, 0, 0);
      o.lookAt(p[0] + n[0], p[1] + n[1], p[2] + n[2]);
      o.rotateZ(r() * Math.PI * 2);
      o.updateMatrix();
      g.applyMatrix4(o.matrix);
      parts.push(g.toNonIndexed());
    }
    const g = mergeGeometries(parts);
    g.computeVertexNormals();
    // (lit as a long box rather than a ball: normals straight out of the nearest face)
    const pos = g.attributes.position, nor = g.attributes.normal, ao = new Float32Array(pos.count);
    for (let i = 0; i < pos.count; i++) {
      const y = pos.getY(i), z = pos.getZ(i);
      const top = y > 0.85, v = new THREE.Vector3(0, top ? 1 : 0.2, top ? z * 0.6 : Math.sign(z) || 1).normalize();
      nor.setXYZ(i, v.x, v.y, v.z);
      ao[i] = 0.6 + 0.4 * Math.min(1, y);
    }
    g.setAttribute('leafAo', new THREE.BufferAttribute(ao, 1));
    return g;
  },
  // crossed upright cards
  clump(cellIx) {
    const parts = [];
    for (let i = 0; i < 3; i++) upright(parts, 0, 0, (i / 3) * Math.PI, 1, 1, cellIx);
    return done(parts);
  },
  // fronds arching out from the middle, each bent in two
  fern(cellIx) {
    const r = rng(cellIx * 7 + 3), parts = [];
    for (let i = 0; i < 11; i++) {
      const a = (i / 11) * Math.PI * 2 + r() * 0.4, ca = Math.cos(a), sa = Math.sin(a), w = 0.3, px = -sa * w / 2, pz = ca * w / 2;
      const up = 0.7 + r() * 0.3, out = 0.8 + r() * 0.3;
      const p0 = [0, 0, 0], p1 = [ca * 0.3 * out, 0.8 * up, sa * 0.3 * out], p2 = [ca * 0.72 * out, 0.6 * up, sa * 0.72 * out];
      quad(parts, [p0[0] - px * 0.3, p0[1], p0[2] - pz * 0.3], [p0[0] + px * 0.3, p0[1], p0[2] + pz * 0.3], [p1[0] + px, p1[1], p1[2] + pz], [p1[0] - px, p1[1], p1[2] - pz], cellIx, 0, 0.55);
      quad(parts, [p1[0] - px, p1[1], p1[2] - pz], [p1[0] + px, p1[1], p1[2] + pz], [p2[0] + px * 0.4, p2[1] - 0.35, p2[2] + pz * 0.4], [p2[0] - px * 0.4, p2[1] - 0.35, p2[2] - pz * 0.4], cellIx, 0.55, 1);
    }
    return done(parts);
  },
  // saw palmetto: stiff fans on stalks, leaning out all round
  palmetto(cellIx) {
    const r = rng(cellIx * 13 + 1), parts = [];
    for (let i = 0; i < 7; i++) {
      const a = (i / 7) * Math.PI * 2 + r() * 0.5;
      upright(parts, Math.cos(a) * 0.05, Math.sin(a) * 0.05, a + Math.PI / 2, 0.75, 0.95, cellIx, 0.25 + r() * 0.5);
    }
    return done(parts);
  },
  // a patch of flowers, about 1 m square: little crossed clumps
  flowers(cellIx) {
    const r = rng(cellIx * 19 + 7), parts = [];
    for (let i = 0; i < 12; i++) {
      const x = (r() - 0.5) * 0.85, z = (r() - 0.5) * 0.85, a = r() * Math.PI, s = 0.4 + r() * 0.25;
      upright(parts, x, z, a, s, s * 1.15, cellIx);
      upright(parts, x, z, a + Math.PI / 2, s, s * 1.15, cellIx);
    }
    return done(parts);
  },
};

// The same as toys for Cartoon: a few dozen flat-shaded triangles each
function toonForm(form) {
  const ball = (r, x, y, z, sy = 1) => new THREE.IcosahedronGeometry(r, 1).scale(1, sy, 1).translate(x, y, z);
  const blade = (a, lean, h, w) => new THREE.ConeGeometry(w, h, 4, 1).translate(0, h / 2, 0).rotateZ(lean).rotateY(a);
  const parts = {
    bush: () => [ball(0.5, 0, 0.45, 0, 0.85)],
    hedge: () => [new THREE.BoxGeometry(1.05, 1, 1, 1, 1, 1).translate(0, 0.5, 0)],
    clump: () => Array.from({ length: 7 }, (_, i) => blade((i / 7) * Math.PI * 2, 0.35, 1, 0.08)),
    fern: () => Array.from({ length: 7 }, (_, i) => blade((i / 7) * Math.PI * 2, 0.9, 0.75, 0.1)),
    palmetto: () => Array.from({ length: 6 }, (_, i) => new THREE.CircleGeometry(0.4, 6, 0, Math.PI).rotateX(-1.1).translate(0, 0.6, -0.15).rotateY((i / 6) * Math.PI * 2)),
    flowers: () => Array.from({ length: 6 }, (_, i) => ball(0.16, Math.cos(i * 2.4) * 0.3, 0.22, Math.sin(i * 2.4) * 0.3)),
  }[form]();
  const geos = parts.map((g) => (g.index ? g.toNonIndexed() : g));
  // (keeping each shape's own normals: smooth over the balls, so they shade as round toys)
  for (const g of geos) g.deleteAttribute('uv');
  return mergeGeometries(geos);
}

// ------------------------------------------------------------------ the meshes

const CHUNK = 250;
let atlas = null;
async function loadAtlas() {
  if (atlas) return atlas;
  const t = await new THREE.TextureLoader().loadAsync('/textures/plants/plants.png');
  t.colorSpace = THREE.SRGBColorSpace;
  t.premultiplyAlpha = true;
  atlas = t;
  return t;
}

export class Plants {
  constructor() {
    this.group = new THREE.Group();
    this.chunks = [];
    this.count = 0;
    this.lastCam = null;
  }

  // spots: from placePlants; toon: Cartoon's toys; envApply(material): the scene's reflections
  async build(spots, { toon = false, envApply = () => {} } = {}) {
    this.group.clear();
    this.chunks = [];
    this.count = spots.length;
    this.spots = spots; // (kept for looking things up from the console)
    this.lastCam = null;
    if (!spots.length) return;
    const map = toon ? null : await loadAtlas();
    const geos = new Map(), mats = new Map();
    const geoOf = (kind) => {
      const P = PLANTS[kind], k = toon ? P.form : `${P.form}:${P.cell}:${P.cards || ''}`;
      if (!geos.has(k)) geos.set(k, toon ? toonForm(P.form) : FORMS[P.form](P.cell, P.cards));
      return geos.get(k);
    };
    const matOf = () => {
      if (!mats.has('m')) {
        const m = toon ? new THREE.MeshStandardMaterial({ roughness: 0.85, metalness: 0, side: THREE.DoubleSide, flatShading: false }) : leafMaterial({ map, alphaTest: 0.5 }, { raw: true });
        envApply(m);
        mats.set('m', m);
      }
      return mats.get('m');
    };
    const groups = new Map();
    for (const p of spots) {
      const k = `${p.kind}|${Math.floor(p.x / CHUNK)}|${Math.floor(p.z / CHUNK)}`;
      if (!groups.has(k)) groups.set(k, []);
      groups.get(k).push(p);
    }
    const m = new THREE.Matrix4(), q = new THREE.Quaternion(), s = new THREE.Vector3(), up = new THREE.Vector3(0, 1, 0), c = new THREE.Color();
    // Dark mulch under the flower beds (one small disc per patch; the beds' own cells get no rough
    // grass, see the scene's addPlants)
    const beds = spots.filter((p) => PLANTS[p.kind].form === 'flowers');
    if (beds.length) {
      const mulch = new THREE.MeshStandardMaterial({ color: toon ? 0x7a5236 : 0x3e2c20, roughness: 1, metalness: 0, polygonOffset: true, polygonOffsetFactor: -2, polygonOffsetUnits: -2 });
      mulch.envMapIntensity = 0.1; // (bark mulch hardly reflects the sky)
      envApply(mulch);
      const disc = new THREE.CircleGeometry(0.5, 12).rotateX(-Math.PI / 2);
      const mesh = new THREE.InstancedMesh(disc, mulch, beds.length);
      beds.forEach((p, i) => mesh.setMatrixAt(i, m.compose(T(p.x, p.y + 0.04, p.z), q.setFromAxisAngle(up, p.rot), s.set(p.w * 1.25, 1, p.d * 1.25))));
      mesh.receiveShadow = true;
      mesh.computeBoundingSphere();
      this.group.add(mesh);
    }
    for (const [k, list] of groups) {
      const [kind, cx, cz] = k.split('|'), P = PLANTS[kind];
      const room = Math.max(SHARED_INSTANCES, list.length);
      const mesh = new THREE.InstancedMesh(geoOf(kind), matOf(), room);
      mesh.instanceColor = new THREE.InstancedBufferAttribute(new Float32Array(room * 3), 3);
      list.forEach((p, i) => {
        q.setFromAxisAngle(up, p.rot);
        m.compose(T(p.x, p.y - 0.05, p.z), q, s.set(p.w, p.h, p.d));
        mesh.setMatrixAt(i, m);
        // each its own shade: the picture's own colours a little lighter or darker (Cartoon: the
        // kind's colour)
        const v = 0.85 + p.tint * 0.3, t = P.tint || [1, 1, 1];
        if (toon) c.setHex(P.toon).multiplyScalar(0.9 + p.tint * 0.2);
        else c.setRGB(v * t[0], v * t[1], v * t[2]);
        mesh.setColorAt(i, c);
      });
      mesh.count = list.length;
      mesh.castShadow = P.shadow; mesh.receiveShadow = true;
      mesh.computeBoundingSphere();
      mesh.layers.set(1); // (not drawn into the pond reflections)
      this.group.add(mesh);
      this.chunks.push({ kind, mesh, x: (+cx + 0.5) * CHUNK, z: (+cz + 0.5) * CHUNK, far: P.far + CHUNK * 0.71 });
    }
  }

  // Chunks within their kind's distance of the camera are drawn (only again once it has moved)
  update(camera) {
    if (!this.chunks.length) return;
    const x = -camera.position.x, z = camera.position.z;
    if (this.lastCam && Math.hypot(x - this.lastCam.x, z - this.lastCam.z) < 8) return;
    this.lastCam = { x, z };
    for (const c of this.chunks) c.mesh.visible = Math.hypot(c.x - x, c.z - z) < c.far;
  }

  triangles() {
    let n = 0;
    for (const c of this.chunks) if (c.mesh.visible) n += ((c.mesh.geometry.index?.count ?? c.mesh.geometry.attributes.position.count) / 3) * c.mesh.count;
    return n;
  }
}
