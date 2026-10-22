// Sugar maple (Acer saccharum), grown from a seed: a straight grey trunk with a root flare that
// forks into a few upright limbs, then into a dense, rounded-oval crown. Used by trees.js for the
// 'maple' variants in place of a reshaped ez-tree oak.
//
// The output has the same form as an ez-tree model, so the forest treats it like any other tree
// (instancing, the near / mid / impostor levels, wind, collision, the see-through near the ball):
//   bark   - one indexed geometry: position, normal, uv (uv already tiled, so the bark texture is
//            used with repeat 1)
//   leaves - leafy-twig cards (two crossed quads each, uv 0..1, twig base at v = 0), for the
//            twig pictures in public/textures/trees/*_leaves.png
// Models stand about 60 units tall with their foot at y = 0, as ez-tree's do (trees.js scales
// each instance to its real height, and the wind sway is tuned to that size).
//
// lod 'near' is the full tree. lod 'mid' is the same tree (the skeleton is grown from the same
// numbers either way), with more of the fine twigs' bark left out, fewer sides on each branch, and
// about a third as many leaf cards, each larger. Only the leaf placement draws its own numbers,
// so the two levels and the impostor pictures baked from the near one always match.
import * as THREE from 'three/webgpu';

const UNITS = 3; // the skeleton is grown in metres (about 20 m tall); models are 3x that
const BARK_TILE = { u: 1.1, v: 1.8 }; // metres of bark per texture repeat, round and along

function rng32(a) {
  return () => {
    a |= 0; a = (a + 0x6D2B79F5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

const UP = new THREE.Vector3(0, 1, 0), X = new THREE.Vector3(1, 0, 0);
const smooth = (a, b, x) => { const t = Math.min(1, Math.max(0, (x - a) / (b - a))); return t * t * (3 - 2 * t); };
const perp = (d) => new THREE.Vector3().crossVectors(d, Math.abs(d.y) < 0.9 ? UP : X).normalize();

// ------------------------------------------------------------------ the skeleton

// Branches as tubes: { pts, rads, depth } (depth 0 trunk, 1 main limbs, ... 6 finest twigs), and
// twigs: { pos, dir, depth } where leaves grow
export function growSkeleton(seed) {
  const rng = rng32(seed);
  const rr = (a, b) => a + (b - a) * rng();
  const tubes = [], twigs = [];
  const crown = { cy: rr(11, 12.2), rx: rr(6.6, 8.0), ry: rr(6.4, 7.4) };
  const ell = (p) => Math.sqrt((p.x / crown.rx) ** 2 + ((p.y - crown.cy) / crown.ry) ** 2 + (p.z / crown.rx) ** 2);
  const randUnit = () => { const u = rr(-1, 1), th = rr(0, Math.PI * 2), s = Math.sqrt(1 - u * u); return new THREE.Vector3(s * Math.cos(th), u, s * Math.sin(th)); };
  const rotateAway = (d, angle, az) => d.clone().applyAxisAngle(perp(d).applyAxisAngle(d, az), angle).normalize();
  let budget = 3200;

  // root: where a limb starts inside its parent (main limbs start down inside the trunk, along its
  // axis, so the fork reads as one piece of wood); otherwise a short collar inside the parent
  function grow(p, dir, len, rad, depth, root) {
    if (budget-- <= 0) return;
    const steps = Math.max(2, Math.ceil(len / 0.75)), step = len / steps;
    const back = root ? p.clone().addScaledVector(root.dir, -root.len) : p.clone().addScaledVector(dir, -Math.min(rad * 1.6, 0.7));
    const pts = [back, p.clone()], rads = [root ? root.rad : rad * 1.12, rad];
    const d = dir.clone(), pos = p.clone();
    const endRad = Math.max(rad * 0.7, 0.012);
    for (let s = 1; s <= steps; s++) {
      const out = new THREE.Vector3(pos.x, 0, pos.z); const ol = out.length(); if (ol > 1e-3) out.multiplyScalar(1 / ol);
      // Sugar maple limbs climb (an upright oval crown); the finest twigs droop a little
      d.addScaledVector(UP, depth <= 2 ? 0.05 : 0.012);
      if (depth >= 2) d.addScaledVector(out, 0.035);
      if (depth >= 4) d.y -= 0.035;
      d.addScaledVector(randUnit(), depth <= 1 ? 0.06 : 0.13).normalize();
      // ...and stay inside the crown's dense oval
      const e = ell(pos);
      if (e > 0.88) d.addScaledVector(new THREE.Vector3(0, crown.cy, 0).sub(pos).normalize(), (e - 0.88) * 1.6).normalize();
      pos.addScaledVector(d, step);
      const r = rad + (endRad - rad) * (s / steps);
      pts.push(pos.clone()); rads.push(r);
      const lp = depth === 1 ? 0.42 : depth === 2 ? 0.28 : depth === 3 ? 0.14 : 0;
      if (s < steps && rng() < lp) grow(pos.clone(), rotateAway(d, rr(0.8, 1.15), rng() * Math.PI * 2), len * rr(0.45, 0.62), Math.max(r * 0.5, 0.015), depth + 1);
      if (depth >= 5 && s < steps) twigs.push({ pos: pos.clone(), dir: d.clone(), depth });
    }
    // (closed in a short cone, so no open rim shows where the children leave)
    pts.push(pos.clone().addScaledVector(d, endRad * 0.9)); rads.push(0.002);
    tubes.push({ pts, rads, depth });
    if (depth >= 6 || endRad < 0.02 || ell(pos) > 1.08) { twigs.push({ pos: pos.clone(), dir: d.clone(), depth }); return; }
    const k = depth <= 1 ? 3 : (rng() < 0.3 ? 3 : 2);
    const az0 = rng() * Math.PI * 2;
    const cr = endRad * Math.pow(1 / k, 1 / 2.4);
    for (let i = 0; i < k; i++) grow(pos.clone(), rotateAway(d, rr(0.32, 0.62), az0 + (i * Math.PI * 2) / k + rr(-0.4, 0.4)), len * rr(0.62, 0.78), cr, depth + 1);
  }

  // The trunk: straight, clear of limbs for the lower third, flaring into the ground
  const trunkH = rr(4.6, 5.8);
  const lean = new THREE.Vector3(rr(-0.06, 0.06), 1, rr(-0.06, 0.06)).normalize();
  const tp = [], trad = [];
  const n = 10;
  for (let i = 0; i <= n; i++) {
    const y = -0.4 + ((trunkH + 0.4) * i) / n;
    const p = lean.clone().multiplyScalar(y);
    p.x += Math.sin(y * 1.3 + seed) * 0.05; p.z += Math.cos(y * 1.1 + seed) * 0.05;
    const neck = 1 - 0.2 * smooth(trunkH - 1.0, trunkH, y);
    tp.push(p); trad.push((0.78 - (0.2 * i) / n) * (1 + 0.45 * Math.exp(-Math.max(y, 0) * 2.4)) * neck);
  }
  tp.push(tp[n].clone().addScaledVector(tp[n].clone().sub(tp[n - 1]).normalize(), 0.35)); trad.push(0.01);
  tubes.push({ pts: tp, rads: trad, depth: 0 });
  const top = tp[n - 1];
  const root = { dir: top.clone().sub(tp[n - 3]).normalize(), len: 1.1, rad: trad[n - 3] * 0.8 };
  const nLead = 3 + Math.floor(rng() * 3);
  const az0 = rng() * Math.PI * 2;
  for (let i = 0; i < nLead; i++) {
    const az = az0 + (i * Math.PI * 2) / nLead + rr(-0.3, 0.3), tilt = rr(0.3, 0.55);
    grow(top.clone(), new THREE.Vector3(Math.sin(tilt) * Math.cos(az), Math.cos(tilt), Math.sin(tilt) * Math.sin(az)), rr(4.6, 6.0), 0.44, 1, root);
  }
  if (rng() < 0.6) grow(top.clone(), new THREE.Vector3(rr(-0.1, 0.1), 1, rr(-0.1, 0.1)).normalize(), rr(5, 6.2), 0.4, 1, root);
  return { tubes, twigs, crown };
}

// ------------------------------------------------------------------ geometry

function barkGeometry(tubes, lod) {
  const mid = lod === 'mid';
  const sides = (r) => {
    const s = r > 0.3 ? 12 : r > 0.12 ? 7 : r > 0.05 ? 4 : 3;
    return mid ? Math.max(3, Math.round(s * 0.5)) : s;
  };
  // The twig pictures draw the finest stems, so the last levels' bark is left out (near: the
  // last two; mid: the last three). Thin branches go without the collar and end cap (they only
  // show close up on thick ones), and the smaller limbs keep every other ring.
  const thin = (t) => {
    let pts = t.pts, rads = t.rads;
    if (t.depth > 0 && rads[1] < 0.07) { pts = pts.slice(1, -1); rads = rads.slice(1, -1); }
    if (t.depth >= 2 && pts.length > 3) {
      const keep = pts.map((_, i) => i === 0 || i === pts.length - 1 || i % 2 === 0);
      pts = pts.filter((_, i) => keep[i]); rads = rads.filter((_, i) => keep[i]);
    }
    return { pts, rads, depth: t.depth };
  };
  const list = tubes.filter((t) => t.depth < (mid ? 4 : 5)).map(thin);
  let nv = 0, ni = 0;
  for (const t of list) { const s = sides(t.rads[0]); nv += t.pts.length * (s + 1); ni += (t.pts.length - 1) * s * 6; }
  const pos = new Float32Array(nv * 3), nor = new Float32Array(nv * 3), uv = new Float32Array(nv * 2), idx = new Uint32Array(ni);
  const T = new THREE.Vector3(), N = new THREE.Vector3(), B = new THREE.Vector3(), prevT = new THREE.Vector3(), dv = new THREE.Vector3(), q = new THREE.Quaternion();
  let v = 0, ii = 0;
  for (const { pts, rads } of list) {
    const n = pts.length, s = sides(rads[0]), base = v;
    const urep = Math.max(1, Math.round((rads[0] * Math.PI * 2) / BARK_TILE.u));
    let along = 0;
    for (let i = 0; i < n; i++) {
      if (i === 0) T.subVectors(pts[1], pts[0]); else if (i === n - 1) T.subVectors(pts[i], pts[i - 1]); else T.subVectors(pts[i + 1], pts[i - 1]);
      T.normalize();
      // (each ring turned on from the last, so the bark doesn't twist round the branch)
      if (i === 0) N.copy(perp(T)); else { q.setFromUnitVectors(prevT, T); N.applyQuaternion(q); along += pts[i].distanceTo(pts[i - 1]) / BARK_TILE.v; }
      B.crossVectors(T, N).normalize(); prevT.copy(T);
      for (let j = 0; j <= s; j++) {
        const a = (j / s) * Math.PI * 2;
        dv.copy(N).multiplyScalar(Math.cos(a)).addScaledVector(B, Math.sin(a));
        const r = rads[i];
        pos[v * 3] = (pts[i].x + dv.x * r) * UNITS; pos[v * 3 + 1] = (pts[i].y + dv.y * r) * UNITS; pos[v * 3 + 2] = (pts[i].z + dv.z * r) * UNITS;
        nor[v * 3] = dv.x; nor[v * 3 + 1] = dv.y; nor[v * 3 + 2] = dv.z;
        uv[v * 2] = (j / s) * urep; uv[v * 2 + 1] = along;
        v++;
      }
    }
    for (let i = 0; i < n - 1; i++) for (let j = 0; j < s; j++) {
      const a = base + i * (s + 1) + j, b = a + s + 1;
      idx[ii++] = a; idx[ii++] = b; idx[ii++] = a + 1; idx[ii++] = a + 1; idx[ii++] = b; idx[ii++] = b + 1;
    }
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.BufferAttribute(pos, 3));
  g.setAttribute('normal', new THREE.BufferAttribute(nor, 3));
  g.setAttribute('uv', new THREE.BufferAttribute(uv, 2));
  g.setIndex(new THREE.BufferAttribute(idx, 1));
  return g;
}

// Leafy-twig cards at the twigs: each card two crossed quads, its base at the twig, standing out
// along the twig and away from the middle of the crown
function leafGeometry(skel, seed, lod) {
  const mid = lod === 'mid';
  const rng = rng32(seed * 7919 + (mid ? 2 : 1));
  const rr = (a, b) => a + (b - a) * rng();
  const { twigs, crown } = skel;
  const perTwig = mid ? 0.9 : 2.8, size = (mid ? 1.7 : 1) * 1.25; // card size in metres
  const centre = new THREE.Vector3(0, crown.cy, 0);
  const cards = [];
  for (const t of twigs) {
    let k = Math.floor(perTwig) + (rng() < perTwig % 1 ? 1 : 0);
    while (k-- > 0) {
      const p = t.pos.clone().add(new THREE.Vector3(rr(-0.5, 0.5), rr(-0.5, 0.5), rr(-0.5, 0.5)));
      const out = p.clone().sub(centre).normalize();
      const up = t.dir.clone().multiplyScalar(0.55).addScaledVector(out, 0.6).addScaledVector(UP, 0.25)
        .add(new THREE.Vector3(rr(-0.5, 0.5), rr(-0.5, 0.5), rr(-0.5, 0.5))).normalize();
      cards.push({ p, up, side: perp(up).applyAxisAngle(up, rr(0, Math.PI * 2)), s: size * rr(0.8, 1.2) });
    }
  }
  const pos = new Float32Array(cards.length * 8 * 3), uv = new Float32Array(cards.length * 8 * 2), idx = new Uint32Array(cards.length * 12);
  const c = new THREE.Vector3(), w = new THREE.Vector3();
  let v = 0, ii = 0;
  for (const { p, up, side, s } of cards) {
    for (let k = 0; k < 2; k++) {
      // (the second quad turned a quarter round the twig)
      w.copy(side).applyAxisAngle(up, (k * Math.PI) / 2).multiplyScalar(s * 0.5);
      const corners = [[-1, 0, 0, 0], [1, 0, 1, 0], [1, 1, 1, 1], [-1, 1, 0, 1]];
      const base = v;
      for (const [sx, sy, u, vv] of corners) {
        c.copy(p).addScaledVector(w, sx).addScaledVector(up, sy * s);
        pos.set([c.x * UNITS, c.y * UNITS, c.z * UNITS], v * 3);
        uv.set([u, vv], v * 2);
        v++;
      }
      idx.set([base, base + 1, base + 2, base, base + 2, base + 3], ii); ii += 6;
    }
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.BufferAttribute(pos, 3));
  g.setAttribute('uv', new THREE.BufferAttribute(uv, 2));
  g.setIndex(new THREE.BufferAttribute(idx, 1));
  g.computeVertexNormals();
  return g;
}

// The model for trees.js: { bark, leaves } (see the top of this file)
export function buildSugarMaple(seed, lod = 'near') {
  const skel = growSkeleton(seed);
  return { bark: barkGeometry(skel.tubes, lod), leaves: leafGeometry(skel, seed, lod) };
}
