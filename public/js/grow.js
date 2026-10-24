// Trees grown from a seed by the game itself, one set of numbers per species (SPECIES below):
//   liveoak      - southern live oak: a short, massive trunk, limbs reaching out nearly level into
//                  a crown far wider than it is tall, hung with Spanish moss
//   cypress      - Monterey cypress: a few twisting limbs under a flat, wind-sheared top
//   baldcypress  - bald cypress: one straight trunk to the top, a buttressed foot and "knees"
//                  around it, short level branches in a narrow cone of feathery needles
//   mesquite     - several crooked stems from the ground under a low, open umbrella of fine leaves
//   paloverde    - like mesquite, but smooth green bark, many fine green twigs and few leaves
//   sycamore     - a big tree: a tall trunk forking into crooked limbs, large leaves, white
//                  camouflage bark
//   magnolia     - southern magnolia: branched from near the ground in a dense evergreen oval of
//                  big glossy leaves, with white flowers
//   saguaro      - the cactus: a ribbed column, usually with arms that bend up, flowers on top
// The output has the same form as an ez-tree model (see sugarmaple.js, which this generalizes):
// bark (position, normal, uv already tiled) and leaves (cards), foot at y = 0, about 60 units
// tall. Pictures: public/textures/trees/<kind>_leaves.png and _bark_*.jpg (laid out by
// scripts/tree-textures.py; real leaves put in by scripts/scan-leaves.py), whose leaf picture has four quarters: foliage in the bottom two,
// extras (moss, flowers) in the top two. bark.tint darkens a species' bark picture. Bark u, v: the
// metres the picture covers round and along a branch - most are photo scans (see NOTICE.md), some
// twice as tall as wide.
//
// lod 'mid' is the same tree (everything random is drawn before the level matters): fewer sides
// and rings on each branch, the finest twigs' bark left out, and about a third as many leaf
// cards, each larger.
import * as THREE from 'three/webgpu';

const V3 = THREE.Vector3, UP = new V3(0, 1, 0), X = new V3(1, 0, 0);
const MODEL_HEIGHT = 60;
const smooth = (a, b, x) => { const t = Math.min(1, Math.max(0, (x - a) / (b - a))); return t * t * (3 - 2 * t); };
const perp = (d) => new V3().crossVectors(d, Math.abs(d.y) < 0.9 ? UP : X).normalize();
function rng32(a) {
  return () => {
    a |= 0; a = (a + 0x6D2B79F5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

// Branch growth, by depth (1 = main limbs): up - how much they climb; jit - how crooked; lat -
// chance of a side branch at each step; droop - how much the finest twigs hang (from depth
// droopFrom); out - how much they lean out from the middle (from depth 2). Forks at each
// branch end: 3 ways up to depth fork3, then 3 with chance p3, else 2. Leaves on twigs from
// depth twigFrom, and (cover) on bigger limbs too where they reach the top of the crown. Bark is drawn below depth barkDepth (near; one less for mid).
const BRANCH = {
  step: 0.75, up: [0, 0.05, 0.05, 0.012, 0.012, 0.012, 0.012], jit: [0, 0.06, 0.13, 0.13, 0.13, 0.13, 0.13],
  lat: [0, 0.42, 0.28, 0.14, 0, 0, 0], latAng: [0.8, 1.15], latLen: [0.45, 0.62], out: 0.035, droop: 0.035, droopFrom: 4,
  fork3: 1, p3: 0.3, forkAng: [0.32, 0.62], forkLen: [0.62, 0.78], taper: 0.7, maxDepth: 6, twigFrom: 5, barkDepth: 5, budget: 3200,
};
const sp = (b) => ({ ...BRANCH, ...b });

export const SPECIES = {
  liveoak: {
    trunk: { h: [2.2, 3.2], r: 0.68, flare: 0.5, lean: 0.08 },
    leaders: { n: [3, 5], tilt: [0.85, 1.2], len: [6.5, 8.5], rad: 0.5 },
    crown: { cy: 7.8, rx: 13.5, ry: 5.2 },
    branch: sp({ step: 0.8, up: [0, 0.004, 0.012, 0.02, 0.02, 0.01, 0], jit: [0, 0.1, 0.14, 0.15, 0.15, 0.15, 0.15], lat: [0, 0.5, 0.35, 0.18, 0, 0, 0], latLen: [0.45, 0.6], forkAng: [0.35, 0.65], forkLen: [0.6, 0.76], twigFrom: 4, cover: 0.6, budget: 3600 }),
    leaves: { mode: 'twig', size: 1.0, cards: 5600 },
    moss: { from: 2, to: 4, p: 0.17, len: [1.2, 2.8] },
    bark: { u: 1.0, v: 2.0, tint: 0.6 },
  },
  cypress: {
    trunk: { h: [1.8, 3.0], r: 0.55, flare: 0.4, lean: 0.15 },
    leaders: { n: [2, 4], tilt: [0.55, 0.95], len: [5, 7], rad: 0.42 },
    crown: { cy: 8.5, rx: 9, ry: 3.8, flat: 0.55, shear: [1.8, 3.0] },
    branch: sp({ step: 0.7, up: [0, 0.03, 0.02, 0.01, 0, 0, 0], jit: [0, 0.12, 0.14, 0.14, 0.14, 0.14], out: 0.05, lat: [0, 0.4, 0.3, 0.15, 0, 0], maxDepth: 5, twigFrom: 4, barkDepth: 4, droopFrom: 9 }),
    leaves: { mode: 'pad', size: 1.25, cards: 3000 },
    bark: { u: 0.9, v: 1.8 },
  },
  baldcypress: {
    central: { h: [17, 23], r: 0.5, flare: 1.0, lobes: 6, lobe: 0.3, from: 0.2, every: 0.45, p: 0.9, len: [1.2, 4.4], tilt: [1.15, 1.45], rad: [0.12, 0.04] },
    crown: { cone: true },
    branch: sp({ step: 0.6, up: [0, 0, 0, 0.01, 0, 0], jit: [0, 0.08, 0.08, 0.1, 0.1, 0.1], lat: [0, 0, 0.35, 0.2, 0, 0], droop: 0.03, droopFrom: 3, out: 0.02, maxDepth: 5, twigFrom: 4, barkDepth: 4, forkAng: [0.3, 0.55], forkLen: [0.6, 0.75] }),
    leaves: { mode: 'twig', size: 0.95, cards: 3200 },
    knees: { n: [6, 12], r: [1.3, 3.8], h: [0.25, 0.7] },
    bark: { u: 1.0, v: 1.0 },
  },
  mesquite: {
    stems: { n: [2, 4], tilt: [0.55, 0.95], len: [2.4, 3.4], rad: 0.18 },
    crown: { cy: 3.9, rx: 6, ry: 2.2 },
    branch: sp({ step: 0.5, up: [0, 0.02, 0.03, 0.02, 0, 0, 0], jit: [0, 0.2, 0.22, 0.2, 0.2, 0.2, 0.2], lat: [0, 0.5, 0.35, 0.2, 0, 0, 0], droop: 0.04, forkLen: [0.6, 0.75], budget: 2600 }),
    leaves: { mode: 'twig', size: 0.75, cards: 2600 },
    bark: { u: 0.8, v: 0.8 },
  },
  paloverde: {
    stems: { n: [2, 3], tilt: [0.3, 0.65], len: [2.0, 2.8], rad: 0.16 },
    crown: { cy: 4.0, rx: 4.5, ry: 2.6 },
    branch: sp({ step: 0.45, up: [0, 0.03, 0.04, 0.03, 0.01, 0, 0], jit: [0, 0.14, 0.16, 0.16, 0.16, 0.16, 0.16], lat: [0, 0.5, 0.4, 0.25, 0.1, 0, 0], droop: 0.02, forkLen: [0.62, 0.76], barkDepth: 7, budget: 3000 }),
    leaves: { mode: 'twig', size: 0.75, cards: 1900 },
    bark: { u: 0.8, v: 1.2 },
  },
  sycamore: {
    trunk: { h: [5, 7], r: 0.72, flare: 0.45, lean: 0.12 },
    leaders: { n: [2, 4], tilt: [0.35, 0.7], len: [6, 8], rad: 0.52 },
    crown: { cy: 15, rx: 10, ry: 9 },
    branch: sp({ up: [0, 0.04, 0.03, 0.012, 0.01, 0.01, 0.01], jit: [0, 0.1, 0.16, 0.16, 0.16, 0.16, 0.16], lat: [0, 0.4, 0.3, 0.15, 0, 0, 0], step: 0.85, budget: 3400 }),
    leaves: { mode: 'twig', size: 1.35, cards: 2600 },
    bark: { u: 1.6, v: 1.6 },
  },
  magnolia: {
    central: { h: [11, 16], r: 0.32, flare: 0.4, from: 0.08, every: 0.4, p: 0.95, len: [1.0, 4.0], tilt: [0.75, 1.15], rad: [0.1, 0.04] },
    crown: { oval: true },
    branch: sp({ step: 0.55, up: [0, 0, 0.03, 0.02, 0.01, 0, 0], jit: [0, 0.08, 0.1, 0.1, 0.1, 0.1], lat: [0, 0, 0.35, 0.2, 0, 0], maxDepth: 5, twigFrom: 4, barkDepth: 4, droopFrom: 9 }),
    leaves: { mode: 'twig', size: 1.1, cards: 3000, flowers: 0.035 },
    bark: { u: 1.0, v: 2.0 },
  },
  saguaro: { cactus: { h: [7, 11], r: 0.3, arms: [1, 4] }, bark: { u: 1, v: 1.0 } },
};

// ------------------------------------------------------------------ the skeleton

function growSkeleton(S, seed) {
  const rng = rng32(seed * 2654435761 + 17);
  const rr = (a, b) => a + (b - a) * rng(), ri = ([a, b]) => Math.floor(rr(a, b + 1));
  const B = S.branch;
  const tubes = [], twigs = [], hang = [], extra = [];
  let budget = B.budget;
  const randUnit = () => { const u = rr(-1, 1), th = rr(0, Math.PI * 2), s = Math.sqrt(1 - u * u); return new V3(s * Math.cos(th), u, s * Math.sin(th)); };
  const rotateAway = (d, angle, az) => d.clone().applyAxisAngle(perp(d).applyAxisAngle(d, az), angle).normalize();

  // The crown's shape: how far outside it a point is (1 on its edge)
  let ell;
  const C = S.crown, jit = rr(0.92, 1.08);
  let centre;
  if (C.cone || C.oval) {
    const Ht = rr(...S.central.h);
    S = { ...S, _H: Ht };
    const R = C.cone ? Ht * 0.28 : Ht * 0.36;
    centre = new V3(0, Ht * 0.55, 0);
    ell = (p) => {
      const f = Math.min(1, Math.max(0, p.y / Ht));
      const r = C.cone ? R * Math.min(1, (1 - f) * 1.6) + 0.3 : R * Math.sin(Math.PI * Math.min(1, f * 0.95 + 0.05)) ** 0.7 + 0.3;
      return Math.max(Math.hypot(p.x, p.z) / r, p.y > Ht ? 1 + (p.y - Ht) : 0);
    };
  } else {
    const shAz = rr(0, Math.PI * 2), sh = C.shear ? rr(...C.shear) : 0;
    centre = new V3(Math.cos(shAz) * sh, C.cy * jit, Math.sin(shAz) * sh);
    const rx = C.rx * jit, ry = C.ry * jit, flat = C.flat ? centre.y + ry * C.flat : Infinity;
    ell = (p) => {
      const e = Math.sqrt(((p.x - centre.x) / rx) ** 2 + ((p.y - centre.y) / ry) ** 2 + ((p.z - centre.z) / rx) ** 2);
      return p.y > flat ? Math.max(e, 1 + (p.y - flat) / 0.8) : e;
    };
  }

  function grow(p, dir, len, rad, depth, root) {
    // (out of branches: leaves where this one would have grown, so no limb is left bare)
    if (budget-- <= 0) { twigs.push({ pos: p.clone(), dir: dir.clone() }); return; }
    const steps = Math.max(2, Math.ceil(len / B.step)), step = len / steps;
    const back = root ? p.clone().addScaledVector(root.dir, -root.len) : p.clone().addScaledVector(dir, -Math.min(rad * 1.6, 0.7));
    const pts = [back, p.clone()], rads = [root ? root.rad : rad * 1.12, rad];
    const d = dir.clone(), pos = p.clone();
    const endRad = Math.max(rad * B.taper, 0.012);
    for (let s = 1; s <= steps; s++) {
      const out = new V3(pos.x, 0, pos.z); const ol = out.length(); if (ol > 1e-3) out.multiplyScalar(1 / ol);
      d.addScaledVector(UP, B.up[depth] ?? 0);
      if (depth >= 2) d.addScaledVector(out, B.out);
      if (depth >= B.droopFrom) d.y -= B.droop;
      d.addScaledVector(randUnit(), B.jit[depth] ?? 0.13).normalize();
      const e = ell(pos);
      if (e > 0.88) d.addScaledVector(centre.clone().sub(pos).normalize(), (e - 0.88) * 1.6).normalize();
      pos.addScaledVector(d, step);
      const r = rad + (endRad - rad) * (s / steps);
      pts.push(pos.clone()); rads.push(r);
      if (s < steps && rng() < (B.lat[depth] ?? 0)) grow(pos.clone(), rotateAway(d, rr(...B.latAng), rng() * Math.PI * 2), len * rr(...B.latLen), Math.max(r * 0.5, 0.015), depth + 1);
      if (depth >= B.twigFrom && s < steps) twigs.push({ pos: pos.clone(), dir: d.clone() });
      // (cover: leaves on any limb running through the top of the crown, so none shows bare there)
      else if (B.cover && depth >= 2 && pos.y > centre.y && e > B.cover) twigs.push({ pos: pos.clone(), dir: d.clone() });
      if (S.moss && depth >= S.moss.from && depth <= S.moss.to && rng() < S.moss.p) hang.push({ pos: pos.clone(), len: rr(...S.moss.len), pick: rng() });
    }
    pts.push(pos.clone().addScaledVector(d, endRad * 0.9)); rads.push(0.002);
    tubes.push({ pts, rads, depth });
    if (depth >= B.maxDepth || endRad < 0.02 || ell(pos) > 1.08) { twigs.push({ pos: pos.clone(), dir: d.clone() }); return; }
    const k = depth <= B.fork3 ? 3 : (rng() < B.p3 ? 3 : 2);
    const az0 = rng() * Math.PI * 2, cr = endRad * Math.pow(1 / k, 1 / 2.4);
    for (let i = 0; i < k; i++) grow(pos.clone(), rotateAway(d, rr(...B.forkAng), az0 + (i * Math.PI * 2) / k + rr(-0.4, 0.4)), len * rr(...B.forkLen), cr, depth + 1);
  }

  let trunkR = 0.3, lobes = null;
  if (S.trunk) {
    // One trunk forking into a few limbs at its top (as the sugar maple)
    const T = S.trunk, H = rr(...T.h);
    const lean = new V3(rr(-T.lean, T.lean), 1, rr(-T.lean, T.lean)).normalize();
    const tp = [], tr = [], n = 8;
    for (let i = 0; i <= n; i++) {
      const y = -0.4 + ((H + 0.4) * i) / n;
      const p = lean.clone().multiplyScalar(y);
      p.x += Math.sin(y * 1.3 + seed) * 0.05; p.z += Math.cos(y * 1.1 + seed) * 0.05;
      tp.push(p); tr.push(T.r * (1 - 0.15 * i / n) * (1 + T.flare * Math.exp(-Math.max(y, 0) * 2.4)) * (1 - 0.15 * smooth(H - 0.8, H, y)));
    }
    tp.push(tp[n].clone().addScaledVector(lean, 0.3)); tr.push(0.01);
    tubes.push({ pts: tp, rads: tr, depth: 0 });
    trunkR = T.r;
    const L = S.leaders, top = tp[n - 1];
    const root = { dir: lean, len: Math.min(1.1, H * 0.4), rad: tr[n - 2] * 0.8 };
    const nl = ri(L.n), az0 = rng() * Math.PI * 2;
    for (let i = 0; i < nl; i++) {
      const az = az0 + (i * Math.PI * 2) / nl + rr(-0.3, 0.3), tilt = rr(...L.tilt);
      grow(top.clone(), new V3(Math.sin(tilt) * Math.cos(az), Math.cos(tilt), Math.sin(tilt) * Math.sin(az)), rr(...L.len), T.r * L.rad * rr(0.85, 1.1) * 1.4, 1, root);
    }
  } else if (S.stems) {
    // Several stems straight from the ground (desert trees)
    const T = S.stems, n = ri(T.n), az0 = rng() * Math.PI * 2;
    for (let i = 0; i < n; i++) {
      const az = az0 + (i * Math.PI * 2) / n + rr(-0.4, 0.4), tilt = rr(...T.tilt);
      const dir = new V3(Math.sin(tilt) * Math.cos(az), Math.cos(tilt), Math.sin(tilt) * Math.sin(az));
      const r = T.rad * rr(0.8, 1.15);
      grow(new V3(Math.cos(az) * 0.08, 0.15, Math.sin(az) * 0.08), dir, rr(...T.len), r, 1, { dir, len: 0.6, rad: r * 1.5 });
    }
    trunkR = T.rad * 2;
  } else if (S.central) {
    // One trunk all the way up, with level side branches along it, longest at the bottom
    const T = S.central, H = S._H;
    const tp = [], tr = [], n = 14;
    for (let i = 0; i <= n; i++) {
      const y = -0.3 + ((H + 0.3) * i) / n, f = Math.max(0, y) / H;
      tp.push(new V3(Math.sin(y * 0.4 + seed) * 0.08, y, Math.cos(y * 0.33 + seed) * 0.08));
      tr.push(T.r * (1 - 0.82 * f) * (1 + T.flare * Math.exp(-Math.max(0, y) / 1.2)));
    }
    tp.push(tp[n].clone().add(new V3(0, 0.3, 0))); tr.push(0.005);
    tubes.push({ pts: tp, rads: tr, depth: 0 });
    trunkR = T.r;
    if (T.lobes) lobes = { n: T.lobes, amp: T.lobe, phase: rr(0, 6.28) };
    let az = rng() * Math.PI * 2;
    for (let y = H * T.from; y < H - 0.4; y += T.every * rr(0.7, 1.3)) {
      if (rng() > T.p) continue;
      const f = y / H;
      az += 2.39996 + rr(-0.3, 0.3);
      const tilt = rr(...T.tilt) - 0.25 * f;
      const dir = new V3(Math.sin(tilt) * Math.cos(az), Math.cos(tilt), Math.sin(tilt) * Math.sin(az));
      const len = (T.len[0] + (T.len[1] - T.len[0]) * Math.pow(1 - f, 1.1)) * rr(0.8, 1.15);
      const at = new V3(Math.sin(y * 0.4 + seed) * 0.08, y, Math.cos(y * 0.33 + seed) * 0.08);
      const r = T.rad[1] + (T.rad[0] - T.rad[1]) * (1 - f);
      grow(at, dir, len, r, 2, { dir, len: T.r * (1 - 0.82 * f), rad: r });
    }
  }
  if (S.knees) {
    // Bald cypress knees: woody cones standing up out of the ground round the trunk
    for (let i = 0, n = ri(S.knees.n); i < n; i++) {
      const a = rr(0, 6.28), r = rr(...S.knees.r), h = rr(...S.knees.h), w = rr(0.1, 0.18);
      const b = new V3(Math.cos(a) * r, -0.1, Math.sin(a) * r);
      extra.push({ pts: [b, b.clone().add(new V3(0, h * 0.6, 0)), b.clone().add(new V3(rr(-0.05, 0.05), h, rr(-0.05, 0.05)))], rads: [w, w * 0.7, 0.01], depth: 9 });
    }
  }
  let top = 0;
  for (const t of tubes) for (const p of t.pts) top = Math.max(top, p.y);
  return { tubes: tubes.concat(extra), twigs, hang, top, trunkR, lobes, rng };
}

// ------------------------------------------------------------------ the saguaro

function growCactus(S, seed) {
  const rng = rng32(seed * 2654435761 + 29);
  const rr = (a, b) => a + (b - a) * rng();
  const T = S.cactus, H = rr(...T.h), R = T.r * rr(0.9, 1.1);
  const column = (pts, r0) => {
    // rounded off at the top
    const rads = pts.map((p, i) => { const left = pts.slice(i).reduce((s, q, k, a) => s + (k ? q.distanceTo(a[k - 1]) : 0), 0); return r0 * Math.sqrt(Math.min(1, left / r0)); });
    return { pts, rads, depth: 0, ribs: true };
  };
  const tubes = [], tips = [];
  const main = [];
  for (let y = -0.2; y <= H - 0.1; y += 0.35) main.push(new V3(Math.sin(y * 0.3 + seed) * 0.05, y, 0));
  main.push(new V3(main[main.length - 1].x, H, 0));
  tubes.push(column(main, R));
  tips.push(main[main.length - 1].clone());
  // Arms: out from the column, bending up in a curve, then straight up (most have 1-3)
  const nArms = Math.min(T.arms[1], 1 + Math.floor(rng() * rng() * 4.5));
  let az = rr(0, 6.28);
  for (let i = 0; i < nArms; i++) {
    az += rr(1.6, 3.4);
    const y0 = H * rr(0.42, 0.68), out = rr(0.35, 0.7), bend = rr(0.35, 0.55), up = rr(1.3, Math.max(1.6, H - y0 - 0.6));
    const hd = new V3(Math.cos(az), 0, Math.sin(az)), pts = [];
    for (let s = -0.15; s <= out; s += 0.15) pts.push(hd.clone().multiplyScalar(s).add(new V3(0, y0 + s * 0.15, 0)));
    const c = hd.clone().multiplyScalar(out).add(new V3(0, y0 + out * 0.15 + bend, 0));
    for (let k = 1; k <= 6; k++) { const a = (k / 6) * Math.PI / 2; pts.push(c.clone().addScaledVector(hd, Math.sin(a) * bend).add(new V3(0, -Math.cos(a) * bend, 0))); }
    const base = pts[pts.length - 1];
    for (let s = 0.35; s <= up - 0.1; s += 0.35) pts.push(base.clone().add(new V3(0, s, 0)));
    pts.push(base.clone().add(new V3(0, up, 0)));
    tubes.push(column(pts, R * rr(0.72, 0.85)));
    tips.push(pts[pts.length - 1].clone());
  }
  return { tubes, tips, top: H, trunkR: R, flowers: rng() < 0.5 ? 'tl' : 'tr', rng };
}

// ------------------------------------------------------------------ geometry

class Builder {
  constructor(normals) { this.p = []; this.n = normals ? [] : null; this.uv = []; this.i = []; }
  v(p, n, u, v) { this.p.push(p.x, p.y, p.z); if (this.n) this.n.push(n.x, n.y, n.z); this.uv.push(u, v); return this.p.length / 3 - 1; }
  quad(a, b, c, d) { this.i.push(a, b, c, a, c, d); }
  geometry(scale) {
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.BufferAttribute(new Float32Array(this.p.map((x) => x * scale)), 3));
    if (this.n) g.setAttribute('normal', new THREE.BufferAttribute(new Float32Array(this.n), 3));
    g.setAttribute('uv', new THREE.BufferAttribute(new Float32Array(this.uv), 2));
    g.setIndex(new THREE.BufferAttribute(new Uint32Array(this.i), 1));
    if (!this.n) g.computeVertexNormals();
    return g;
  }
}

// A tube along pts; bark tiled tile.u metres round and tile.v along. shape(theta, y): radius factor
// (buttress lobes, cactus ribs). urep: how many times the picture goes round (default: by size).
function tube(b, pts, rads, sides, tile, shape, urep) {
  const n = pts.length;
  const reps = urep || Math.max(1, Math.round((rads[0] * Math.PI * 2) / tile.u));
  const T = new V3(), N = new V3(), B = new V3(), prevT = new V3(), d = new V3(), q = new THREE.Quaternion();
  const base = b.p.length / 3;
  let along = 0;
  for (let i = 0; i < n; i++) {
    if (i === 0) T.subVectors(pts[1], pts[0]); else if (i === n - 1) T.subVectors(pts[i], pts[i - 1]); else T.subVectors(pts[i + 1], pts[i - 1]);
    T.normalize();
    if (i === 0) N.copy(perp(T)); else { q.setFromUnitVectors(prevT, T); N.applyQuaternion(q); along += pts[i].distanceTo(pts[i - 1]) / tile.v; }
    B.crossVectors(T, N).normalize(); prevT.copy(T);
    for (let j = 0; j <= sides; j++) {
      const a = (j / sides) * Math.PI * 2;
      d.copy(N).multiplyScalar(Math.cos(a)).addScaledVector(B, Math.sin(a));
      const r = rads[i] * (shape ? shape(a, pts[i].y) : 1);
      b.v(pts[i].clone().addScaledVector(d, r), d, (j / sides) * reps, along);
    }
  }
  for (let i = 0; i < n - 1; i++) for (let j = 0; j < sides; j++) {
    const a = base + i * (sides + 1) + j, c = a + sides + 1;
    b.i.push(a, c, a + 1, a + 1, c, c + 1);
  }
}

function barkGeometry(S, sk, lod, scale) {
  const mid = lod === 'mid', b = new Builder(true);
  if (S.cactus) {
    // 16 ribs, matching the 16 in the picture (which goes round once)
    for (const t of sk.tubes) {
      const pts = mid ? t.pts.filter((_, i) => i % 2 === 0 || i === t.pts.length - 1) : t.pts;
      const rads = mid ? t.rads.filter((_, i) => i % 2 === 0 || i === t.rads.length - 1) : t.rads;
      tube(b, pts, rads, mid ? 16 : 32, S.bark, (a) => 1 - 0.07 * Math.cos(a * 16), 1);
    }
    return b.geometry(scale);
  }
  const deep = S.branch.barkDepth - (mid ? 1 : 0);
  const sides = (r) => { const s = r > 0.3 ? 12 : r > 0.12 ? 7 : r > 0.05 ? 4 : 3; return mid ? Math.max(3, Math.round(s * 0.5)) : s; };
  for (const t of sk.tubes) {
    if (t.depth < 9 && t.depth >= deep) continue;
    if (t.depth === 9 && mid) continue; // (knees: near only)
    let pts = t.pts, rads = t.rads;
    if (t.depth > 0 && t.depth < 9 && rads[1] < 0.07) { pts = pts.slice(1, -1); rads = rads.slice(1, -1); }
    if (t.depth >= 2 && t.depth < 9 && pts.length > 3) {
      const keep = pts.map((_, i) => i === 0 || i === pts.length - 1 || i % 2 === 0);
      pts = pts.filter((_, i) => keep[i]); rads = rads.filter((_, i) => keep[i]);
    }
    const L = sk.lobes, shape = t.depth === 0 && L ? (a, y) => 1 + L.amp * Math.exp(-Math.max(0, y) / 1.0) * Math.max(0, Math.cos(a * L.n + L.phase)) : null;
    tube(b, pts, rads, t.depth === 0 && L ? (mid ? 12 : 24) : sides(rads[0]), S.bark, shape);
  }
  return b.geometry(scale);
}

// Leaf cards: a picture (one quarter of the leaf picture) on two crossed quads, its base at p,
// standing along up
const QUARTER = { bl: [0, 0], br: [0.5, 0], tl: [0, 0.5], tr: [0.5, 0.5] };
function card(b, p, up, side, s, quarter, cross = Math.PI / 2) {
  const [u0, v0] = QUARTER[quarter], e = 0.004, h = 0.5 - 2 * e;
  for (const turn of [0, cross]) {
    const w = side.clone().applyAxisAngle(up, turn).multiplyScalar(s * 0.5);
    const a = b.v(p.clone().sub(w), null, u0 + e, v0 + e), c = b.v(p.clone().add(w), null, u0 + e + h, v0 + e);
    const top = p.clone().addScaledVector(up, s);
    const d = b.v(top.clone().add(w), null, u0 + e + h, v0 + e + h), f = b.v(top.clone().sub(w), null, u0 + e, v0 + e + h);
    b.quad(a, c, d, f);
  }
}

function leafGeometry(S, sk, seed, lod, scale) {
  const mid = lod === 'mid', b = new Builder(false);
  const rng = rng32(seed * 7919 + (mid ? 2 : 1));
  const rr = (a, x) => a + (x - a) * rng();
  if (S.cactus) {
    // Flowers (or red fruit) on the top of the column and each arm
    for (const tip of sk.tips) {
      const s = 0.75, up = UP.clone(), side = new V3(Math.cos(rr(0, 6.28)), 0, Math.sin(rr(0, 6.28))).normalize();
      card(b, tip.clone().add(new V3(0, -0.4, 0)), up, side.lengthSq() ? side : X.clone(), s, sk.flowers, Math.PI / 2);
    }
    return b.geometry(scale);
  }
  const Lf = S.leaves, twigs = sk.twigs;
  const per = (Lf.cards * (mid ? 0.32 : 1)) / Math.max(1, twigs.length), size = Lf.size * (mid ? 1.7 : 1);
  let ctr = new V3();
  for (const t of twigs) ctr.add(t.pos);
  ctr = twigs.length ? ctr.multiplyScalar(1 / twigs.length) : ctr;
  for (const t of twigs) {
    let k = Math.floor(per) + (rng() < per % 1 ? 1 : 0);
    while (k-- > 0) {
      const p = t.pos.clone().add(new V3(rr(-0.5, 0.5), rr(-0.5, 0.5), rr(-0.5, 0.5)));
      const out = p.clone().sub(ctr).normalize();
      const flower = Lf.flowers && rng() < Lf.flowers;
      const quarter = flower ? 'tl' : rng() < 0.5 ? 'bl' : 'br';
      const s = (flower ? 0.45 : size) * rr(0.8, 1.2);
      if (Lf.mode === 'pad') {
        // Flat sprays lying nearly level, facing the sky (cypress)
        const h = new V3(t.dir.x, 0, t.dir.z).add(new V3(out.x, 0, out.z).multiplyScalar(0.8)).add(new V3(rr(-0.5, 0.5), 0, rr(-0.5, 0.5)));
        if (h.lengthSq() < 1e-4) h.set(1, 0, 0);
        h.normalize().y = rr(-0.15, 0.25);
        h.normalize();
        const side = new V3(-h.z, 0, h.x).normalize();
        card(b, p.addScaledVector(h, -s * 0.4), h, side, s, quarter, rr(0.5, 0.8));
      } else {
        const up = t.dir.clone().multiplyScalar(0.55).addScaledVector(out, 0.6).addScaledVector(UP, 0.25)
          .add(new V3(rr(-0.5, 0.5), rr(-0.5, 0.5), rr(-0.5, 0.5))).normalize();
        card(b, p, up, perp(up).applyAxisAngle(up, rr(0, Math.PI * 2)), s, quarter);
      }
    }
  }
  // Spanish moss: strands hanging straight down from the limbs (the top of the picture at the limb)
  const hang = mid ? sk.hang.filter((_, i) => i % 3 === 0) : sk.hang;
  for (const m of hang) {
    const len = m.len * (mid ? 1.4 : 1), side = new V3(Math.cos(m.pick * 40), 0, Math.sin(m.pick * 40));
    card(b, m.pos.clone().add(new V3(0, -len, 0)), UP, side, len, m.pick < 0.5 ? 'tl' : 'tr');
  }
  return b.geometry(scale);
}

// The model for trees.js: { bark, leaves } (see the top of this file)
export function buildGrown(kind, seed, lod = 'near') {
  const S = SPECIES[kind];
  const sk = S.cactus ? growCactus(S, seed) : growSkeleton(S, seed);
  const scale = MODEL_HEIGHT / Math.max(1, sk.top + (S.leaves ? S.leaves.size * 0.5 : 0.4));
  // (trunk: its radius for the ball to hit, in model units; trees.js would otherwise measure it
  // at the foot, where knees and spreading stems would make it far too wide)
  return { bark: barkGeometry(S, sk, lod, scale), leaves: leafGeometry(S, sk, seed, lod, scale), trunk: (sk.trunkR ?? S.cactus.r) * scale, tint: S.bark.tint ?? 1 };
}
