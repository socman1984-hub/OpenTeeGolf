// Palms, grown from a seed, in the four kinds most often planted on golf courses:
//   sabal         - cabbage palm (Florida, the Carolinas, the Gulf): a round ball of fan fronds on
//                   a straight trunk covered in criss-cross old leaf bases ("boots")
//   washingtonia  - Mexican fan palm (Arizona, California, Las Vegas): very tall and slender,
//                   swollen at the foot, a small head of fan fronds over a skirt of dead ones
//   date          - Canary Island date palm (desert and California courses): a thick, short,
//                   diamond-patterned trunk under a huge sphere of arching feather fronds
//   coconut       - coconut palm (Hawaii, the Caribbean, south Florida): a curving, leaning ringed
//                   trunk, drooping feather fronds and a cluster of coconuts
// Used by trees.js for the 'palm' variants. The output has the same form as an ez-tree model
// (see sugarmaple.js): bark (position, normal, uv) and leaves (frond cards), foot at y = 0,
// about 60 units tall. Both use the pictures made by scripts/palm-textures.py, whose layout this
// file depends on: palm_leaves.png holds four fronds (feather and fan, green and dead) in its
// four quarters; palm_bark_*.jpg holds three bands (ringed trunk, boots, flat colour).
//
// lod 'near' is the full palm; 'mid' is the same palm (everything random is drawn in plan(),
// before the level matters) with fewer sides on the trunk, fewer bends in each frond and no
// coconuts.
import * as THREE from 'three/webgpu';

const V3 = THREE.Vector3, UP = new V3(0, 1, 0), X = new V3(1, 0, 0);
const MODEL_HEIGHT = 60;            // units, as ez-tree's models
const BARK_M = 2;                   // metres of trunk per repeat of the bark picture (down it)
const BAND = { rings: { u0: 0, w: 0.4 }, boots: { u0: 0.4, w: 0.4 }, aroundM: 0.8 }; // (each band is 0.8 m round)
const FIBRE = [0.9, 0.25], NUT = [0.9, 0.75]; // flat-colour spots in the bark picture

function rng32(a) {
  return () => {
    a |= 0; a = (a + 0x6D2B79F5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}
const perp = (d) => new V3().crossVectors(d, Math.abs(d.y) < 0.9 ? UP : X).normalize();

// The kinds: trunk height (m) and radius, swelling at the foot, how far the top leans, bark band;
// fronds: how many, how high and low they point (degrees), and their sizes
export const PALMS = {
  sabal: {
    h: [7, 10], r: 0.2, flare: 1.35, lean: 0.35, curve: 1, bark: 'boots', bulb: 1.3,
    fan: { n: [46, 54], up: 86, down: -60, stalk: [1.3, 1.7], size: [2.0, 2.3], bend: 0.7, cup: 0.35, roll: 0.8 },
    deadFan: { n: [4, 6], stalk: 1.0, size: 1.9 },
  },
  washingtonia: {
    h: [15, 20], r: 0.17, flare: 2.2, top: 0.82, lean: 0.6, curve: 1.5, bark: 'rings', bulb: 1.6,
    fan: { n: [24, 30], up: 84, down: -40, stalk: [0.9, 1.2], size: [1.6, 1.9], bend: 0.6, cup: 0.28, roll: 0.8 },
    skirt: { n: [20, 26], drop: [1.4, 2.4], size: 1.1 },
  },
  date: {
    h: [5, 8], r: 0.42, flare: 1.2, lean: 0.2, curve: 1, bark: 'boots', bulb: 1.3, pineapple: 0.9,
    feather: { n: [56, 66], up: 70, down: -30, len: [4.2, 5.2], half: 0.68, droop: [0.5, 0.9] },
    deadFeather: { n: [3, 5] },
  },
  coconut: {
    h: [10, 14], r: 0.17, flare: 1.8, lean: 2.0, curve: 2.2, bark: 'rings', bulb: 1.4,
    feather: { n: [26, 30], up: 45, down: -55, len: [4.0, 4.8], half: 0.55, droop: [0.9, 1.3] },
    deadFeather: { n: [2, 3] }, nuts: [8, 12],
  },
};

// ------------------------------------------------------------------ the plan (all the randomness)

function plan(kind, seed) {
  const K = PALMS[kind], rng = rng32(seed * 2654435761);
  const rr = (a, b) => a + (b - a) * rng(), ri = ([a, b]) => Math.floor(rr(a, b + 1));
  const H = rr(...K.h), leanAz = rr(0, Math.PI * 2), lean = K.lean * rr(0.6, 1.1);
  const wob = [rr(0, 6.28), rr(0, 6.28)];
  // The trunk's centre line: leaning further the higher it gets (curve > 1: bowed, as coconuts)
  const at = (y) => {
    const f = Math.max(0, y) / H, o = lean * Math.pow(f, K.curve);
    return new V3(Math.cos(leanAz) * o + Math.sin(f * 5 + wob[0]) * 0.04, y, Math.sin(leanAz) * o + Math.sin(f * 4 + wob[1]) * 0.04);
  };
  const top = at(H);
  const fronds = [], golden = 2.39996;
  const az0 = rr(0, 6.28);
  const spread = (n, upDeg, downDeg, i) => {
    const f = (i + rr(-0.3, 0.3)) / n;
    return { az: az0 + i * golden + rr(-0.15, 0.15), elev: THREE.MathUtils.degToRad(upDeg + (downDeg - upDeg) * Math.pow(Math.min(1, Math.max(0, f)), 0.85)) };
  };
  if (K.fan) {
    const n = ri(K.fan.n);
    for (let i = 0; i < n; i++) {
      const s = spread(n, K.fan.up, K.fan.down, i);
      fronds.push({ type: 'fan', ...s, base: top.clone().add(new V3(0, rr(-0.15, 0.25), 0)), stalk: rr(...K.fan.stalk), size: rr(...K.fan.size), roll: rr(-K.fan.roll, K.fan.roll), bend: K.fan.bend * rr(0.7, 1.3), cup: K.fan.cup });
    }
  }
  if (K.deadFan) {
    for (let i = 0, n = ri(K.deadFan.n); i < n; i++) {
      fronds.push({ type: 'fan', dead: true, az: rr(0, 6.28), elev: THREE.MathUtils.degToRad(rr(-85, -65)), base: top.clone().add(new V3(0, rr(-0.4, 0), 0)), stalk: K.deadFan.stalk, size: K.deadFan.size, roll: rr(-0.3, 0.3), bend: 0.2, cup: 0.2 });
    }
  }
  if (K.skirt) {
    // Dead fronds hanging flat against the trunk, in layers under the head (trimmed short, as on a course)
    const n = ri(K.skirt.n), drop = rr(...K.skirt.drop), r = K.r * (K.top || 1);
    for (let i = 0; i < n; i++) {
      const az = az0 + i * golden, y = H - 0.2 - (i / n) * drop;
      const c = at(y);
      fronds.push({ type: 'fan', dead: true, az, elev: THREE.MathUtils.degToRad(rr(-88, -78)), base: c.add(new V3(Math.cos(az) * r, 0, Math.sin(az) * r)), stalk: 0.25, size: K.skirt.size * rr(0.85, 1.1), roll: rr(-0.15, 0.15), bend: 0.05, cup: 0.5, flat: true });
    }
  }
  if (K.feather) {
    const n = ri(K.feather.n);
    for (let i = 0; i < n; i++) {
      const s = spread(n, K.feather.up, K.feather.down, i);
      const out = new V3(Math.cos(s.az), 0, Math.sin(s.az)).multiplyScalar(rr(0.05, 0.25));
      fronds.push({ type: 'feather', ...s, base: top.clone().add(out).add(new V3(0, rr(-0.2, 0.3), 0)), len: rr(...K.feather.len), half: K.feather.half * rr(0.85, 1.1), droop: rr(...K.feather.droop) * (1 + Math.max(0, -s.elev) * 0.4), roll: rr(-0.25, 0.25) });
    }
  }
  if (K.deadFeather) {
    for (let i = 0, n = ri(K.deadFeather.n); i < n; i++) {
      fronds.push({ type: 'feather', dead: true, az: rr(0, 6.28), elev: THREE.MathUtils.degToRad(rr(-80, -60)), base: top.clone().add(new V3(0, rr(-0.5, -0.1), 0)), len: rr(...K.feather.len) * 0.85, half: K.feather.half * 0.8, droop: 0.2, roll: rr(-0.3, 0.3) });
    }
  }
  const nuts = [];
  if (K.nuts) for (let i = 0, n = ri(K.nuts); i < n; i++) {
    const a = rr(0, 6.28), r = rr(0.15, 0.35);
    nuts.push(top.clone().add(new V3(Math.cos(a) * r, rr(-0.55, -0.15), Math.sin(a) * r)));
  }
  // How tall the whole palm stands (for the model's scale): the head reaches about this far up
  const reach = K.fan ? K.fan.stalk[1] + K.fan.size[1] * 0.75 : K.feather.len[1] * 0.6;
  return { K, H, at, top, fronds, nuts, scale: MODEL_HEIGHT / (H + reach) };
}

// ------------------------------------------------------------------ geometry helpers

class Builder {
  constructor(normals) { this.p = []; this.n = normals ? [] : null; this.uv = []; this.i = []; }
  v(p, n, u, v) { this.p.push(p.x, p.y, p.z); if (this.n) this.n.push(n.x, n.y, n.z); this.uv.push(u, v); return this.p.length / 3 - 1; }
  tri(a, b, c) { this.i.push(a, b, c); }
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

// A tube along pts. uvAt(jFrac, ringIndex, along metres) -> [u, v]. reps: how many times the
// band repeats round the tube (the seam vertices are doubled so each repeat starts at u0).
function tube(b, pts, rads, sides, reps, uvAt) {
  const sp = Math.max(2, Math.ceil(sides / reps)), ring = reps * (sp + 1);
  const T = new V3(), N = new V3(), B = new V3(), prevT = new V3(), d = new V3(), q = new THREE.Quaternion();
  let along = 0;
  const first = b.p.length / 3;
  for (let i = 0; i < pts.length; i++) {
    if (i === 0) T.subVectors(pts[1], pts[0]); else if (i === pts.length - 1) T.subVectors(pts[i], pts[i - 1]); else T.subVectors(pts[i + 1], pts[i - 1]);
    T.normalize();
    if (i === 0) N.copy(perp(T)); else { q.setFromUnitVectors(prevT, T); N.applyQuaternion(q); along += pts[i].distanceTo(pts[i - 1]); }
    B.crossVectors(T, N).normalize(); prevT.copy(T);
    for (let k = 0; k < reps; k++) for (let j = 0; j <= sp; j++) {
      const a = ((k + j / sp) / reps) * Math.PI * 2;
      d.copy(N).multiplyScalar(Math.cos(a)).addScaledVector(B, Math.sin(a));
      const [u, v] = uvAt(j / sp, i, along);
      b.v(pts[i].clone().addScaledVector(d, rads[i]), d, u, v);
    }
  }
  for (let i = 0; i < pts.length - 1; i++) for (let k = 0; k < reps; k++) for (let j = 0; j < sp; j++) {
    const a = first + i * ring + k * (sp + 1) + j, c = a + ring;
    b.tri(a, c, a + 1); b.tri(a + 1, c, c + 1);
  }
}

// ------------------------------------------------------------------ bark

function barkGeometry(P, lod) {
  const { K, H, at, top } = P, mid = lod === 'mid';
  const b = new Builder(true);
  // Trunk: from just under the ground to the head, swollen at the foot
  const n = mid ? 8 : 16, pts = [], rads = [];
  for (let i = 0; i <= n; i++) {
    const y = -0.3 + ((H + 0.3) * i) / n, f = Math.max(0, y) / H;
    pts.push(at(y));
    rads.push(K.r * (1 + (K.flare - 1) * Math.exp(-Math.max(0, y) / 0.7)) * (1 - (1 - (K.top || 1)) * f));
  }
  const band = BAND[K.bark], reps = Math.max(1, Math.round((2 * Math.PI * K.r * K.flare) / BAND.aroundM));
  const sides = mid ? 6 : 12;
  tube(b, pts, rads, sides, reps, (j, i, along) => [band.u0 + 0.004 + (band.w - 0.008) * j, along / BARK_M]);
  // The head: where the fronds grow, a swelling of leaf bases (a big "pineapple" of them on the
  // date palm, trimmed as they are on courses), closed off at the top
  const dir = top.clone().sub(at(H - 0.5)).normalize();
  const hb = K.pineapple || 0.6, R0 = rads[n], R1 = K.r * K.bulb * (K.pineapple ? 1.3 : 1);
  const hp = [top.clone().addScaledVector(dir, -0.3), top.clone().addScaledVector(dir, hb * 0.4), top.clone().addScaledVector(dir, hb), top.clone().addScaledVector(dir, hb + 0.15)];
  const hr = [R0, R1, R1 * 0.75, 0.02];
  const fibre = K.bark === 'boots' || K.pineapple;
  tube(b, hp, hr, sides, fibre ? reps : 1, (j, i, along) => (fibre
    ? [BAND.boots.u0 + 0.004 + (BAND.boots.w - 0.008) * j, along / BARK_M]
    : [0.82 + 0.16 * j, 0.05 + 0.1 * i]));
  // Coconuts: little round fruit under the head (near only)
  if (!mid) {
    const ball = new THREE.IcosahedronGeometry(0.14, 1), p = ball.attributes.position, nn = ball.attributes.normal;
    for (const c of P.nuts) {
      const base = b.p.length / 3;
      for (let i = 0; i < p.count; i++) b.v(new V3(p.getX(i), p.getY(i) * 1.15, p.getZ(i)).add(c), new V3(nn.getX(i), nn.getY(i), nn.getZ(i)), NUT[0], NUT[1]);
      for (let i = 0; i < p.count; i += 3) b.tri(base + i, base + i + 1, base + i + 2);
    }
  }
  return b.geometry(P.scale);
}

// ------------------------------------------------------------------ fronds

// A feather frond: a strip along its arching stalk, folded into a shallow V (leaflets rise from
// the stalk on both sides). Picture: the bottom-left (green) or bottom-right (dead) quarter.
function featherFrond(b, f, segs) {
  const u0 = f.dead ? 0.5 : 0;
  const hz = new V3(Math.cos(f.az), 0, Math.sin(f.az)), side0 = new V3(-hz.z, 0, hz.x);
  const fold = 0.42, p = f.base.clone();
  let ang = f.elev, prev = null;
  for (let s = 0; s <= segs; s++) {
    const t = s / segs;
    const dir = hz.clone().multiplyScalar(Math.cos(ang)).addScaledVector(UP, Math.sin(ang));
    const up = hz.clone().multiplyScalar(-Math.sin(ang)).addScaledVector(UP, Math.cos(ang));
    const side = side0.clone().multiplyScalar(Math.cos(f.roll)).addScaledVector(up, Math.sin(f.roll));
    const nrm = up.clone().multiplyScalar(Math.cos(f.roll)).addScaledVector(side0, -Math.sin(f.roll));
    const w = f.half * (1 - 0.15 * t);
    const L = p.clone().addScaledVector(side, -w * Math.cos(fold)).addScaledVector(nrm, w * Math.sin(fold));
    const R = p.clone().addScaledVector(side, w * Math.cos(fold)).addScaledVector(nrm, w * Math.sin(fold));
    const v = 0.004 + 0.492 * t;
    const row = [b.v(L, null, u0 + 0.004, v), b.v(p, null, u0 + 0.25, v), b.v(R, null, u0 + 0.496, v)];
    if (prev) { b.quad(prev[0], prev[1], row[1], row[0]); b.quad(prev[1], prev[2], row[2], row[1]); }
    prev = row;
    p.addScaledVector(dir, f.len / segs);
    ang -= (f.droop / segs) * (0.4 + 1.2 * t); // (bending more towards the tip)
    void dir;
  }
}

// A fan frond: a narrow strip for its stalk, then the fan, which curves down along its middle
// and droops at both sides. Picture: the top-left (green) or top-right (dead) quarter, where the
// stalk runs up to 0.36 of the quarter's height and the fan fills 0.36 to 0.98.
function fanFrond(b, f, lod) {
  const mid = lod === 'mid';
  const u0 = f.dead ? 0.5 : 0, cellV = (cv) => 0.5 + 0.004 + 0.492 * cv, cellU = (cu) => u0 + 0.004 + 0.492 * cu;
  const hz = new V3(Math.cos(f.az), 0, Math.sin(f.az)), side0 = new V3(-hz.z, 0, hz.x);
  const dirOf = (a) => hz.clone().multiplyScalar(Math.cos(a)).addScaledVector(UP, Math.sin(a));
  const upOf = (a) => hz.clone().multiplyScalar(-Math.sin(a)).addScaledVector(UP, Math.cos(a));
  const rollSide = (a) => side0.clone().multiplyScalar(Math.cos(f.roll)).addScaledVector(upOf(a), Math.sin(f.roll));
  const rollUp = (a) => upOf(a).multiplyScalar(Math.cos(f.roll)).addScaledVector(side0, -Math.sin(f.roll));
  const S = f.size / 0.9; // the quarter's width in metres (the fan spans 0.9 of it)
  // Stalk
  const end = f.base.clone().addScaledVector(dirOf(f.elev), f.stalk), sw = 0.03 * S, s0 = rollSide(f.elev);
  const a = b.v(f.base.clone().addScaledVector(s0, -sw), null, cellU(0.47), cellV(0)), c = b.v(f.base.clone().addScaledVector(s0, sw), null, cellU(0.53), cellV(0));
  const d = b.v(end.clone().addScaledVector(s0, sw), null, cellU(0.53), cellV(0.36)), e = b.v(end.clone().addScaledVector(s0, -sw), null, cellU(0.47), cellV(0.36));
  b.quad(a, c, d, e);
  // Fan
  const rows = mid ? [0.36, 0.67, 0.98] : [0.36, 0.53, 0.7, 0.85, 0.98];
  const cols = mid ? [0.02, 0.5, 0.98] : [0.02, 0.26, 0.5, 0.74, 0.98];
  // (a fan pressed against the trunk keeps facing out from it)
  const p = end.clone();
  let ang = f.elev, prev = null;
  for (let r = 0; r < rows.length; r++) {
    if (r > 0) {
      p.addScaledVector(dirOf(ang), (rows[r] - rows[r - 1]) * S);
      ang -= f.bend * (rows[r] - rows[r - 1]) / 0.62 * (0.5 + (rows[r] - 0.36) * 1.6);
    }
    const side = rollSide(ang), nrm = f.flat ? hz : rollUp(ang);
    const row = cols.map((cu) => {
      const x = (cu - 0.5) * 2;
      const q = p.clone().addScaledVector(side, x * 0.5 * S).addScaledVector(nrm, -f.cup * x * x * 0.25 * S);
      return b.v(q, null, cellU(cu), cellV(rows[r]));
    });
    if (prev) for (let k = 0; k < cols.length - 1; k++) b.quad(prev[k], prev[k + 1], row[k + 1], row[k]);
    prev = row;
  }
}

function leafGeometry(P, lod) {
  const b = new Builder(false);
  for (const f of P.fronds) {
    if (f.type === 'feather') featherFrond(b, f, lod === 'mid' ? 4 : 9);
    else fanFrond(b, f, lod);
  }
  return b.geometry(P.scale);
}

// The model for trees.js: { bark, leaves } (see the top of this file)
export function buildPalm(kind, seed, lod = 'near') {
  const P = plan(kind, seed);
  return { bark: barkGeometry(P, lod), leaves: leafGeometry(P, lod) };
}
