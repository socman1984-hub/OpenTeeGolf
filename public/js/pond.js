// Life in the ponds: how deep each one is (for the water you see into), rings spreading where the
// ball splashes in or a fish rises, the splash itself, lily pads in patches near the shore, the
// fish of the region swimming about, and turtles paddling at the surface or sunning on a log.
// (After Koi Pond Garden by Sourany Phomhome, MIT.)
//
// Ponds: the course's still water bodies (wb.poly, wb.level, wb.style), course coordinates;
// course x is the scene's mirrored.
import * as THREE from 'three/webgpu';
import { Fn, uniform, vec2, vec3, float, sin, cos, exp, length, time, mix, select, attribute, instancedBufferAttribute, positionLocal, positionGeometry, positionWorld, cameraPosition, uv, texture } from 'three/tsl';
import { pointInPoly, noise } from './course.js';

// How murky each kind of pond is (0 clear .. 1 thick with silt and algae)
const MURK = { manicured: 0.3, rocky: 0.22, natural: 0.5, reedy: 0.65 };

// ---------------------------------------------------------------- depth

// Depth (m) of every pond at each point, and its murk, over all the ponds: a texture for the water
// shader (red: depth, green: murk) and lookups for placing things.
export function pondDepthMap(ponds, groundAt) {
  if (!ponds.length) return null;
  let minX = Infinity, maxX = -Infinity, minZ = Infinity, maxZ = -Infinity;
  for (const wb of ponds) for (const [x, z] of wb.poly) { minX = Math.min(minX, x); maxX = Math.max(maxX, x); minZ = Math.min(minZ, z); maxZ = Math.max(maxZ, z); }
  minX -= 4; maxX += 4; minZ -= 4; maxZ += 4;
  const res = Math.max(0.5, Math.max(maxX - minX, maxZ - minZ) / 1024);
  const w = Math.ceil((maxX - minX) / res), h = Math.ceil((maxZ - minZ) / res);
  const depth = new Float32Array(w * h), murk = new Float32Array(w * h), which = new Int16Array(w * h).fill(-1);
  ponds.forEach((wb, k) => {
    const xs = wb.poly.map((p) => p[0]), zs = wb.poly.map((p) => p[1]);
    const i0 = Math.max(0, Math.floor((Math.min(...xs) - minX) / res)), i1 = Math.min(w - 1, Math.ceil((Math.max(...xs) - minX) / res));
    const j0 = Math.max(0, Math.floor((maxZ - Math.max(...zs)) / res)), j1 = Math.min(h - 1, Math.ceil((maxZ - Math.min(...zs)) / res));
    const m = MURK[wb.style] ?? 0.35;
    for (let j = j0; j <= j1; j++) for (let i = i0; i <= i1; i++) {
      const x = minX + (i + 0.5) * res, z = maxZ - (j + 0.5) * res;
      if (!pointInPoly(wb.poly, x, z)) continue;
      const g = groundAt(x, z);
      const o = j * w + i;
      depth[o] = g === null ? 1.5 : Math.max(0, wb.level - g);
      murk[o] = m; which[o] = k;
    }
  });
  // (the murk spread a few cells past the shore, so the edge of the water doesn't blend it to 0)
  const half = new Uint16Array(w * h * 2);
  for (let o = 0; o < w * h; o++) {
    half[o * 2] = THREE.DataUtils.toHalfFloat(depth[o]);
    half[o * 2 + 1] = THREE.DataUtils.toHalfFloat(which[o] >= 0 ? murk[o] : 0.35);
  }
  const tex = new THREE.DataTexture(half, w, h, THREE.RGFormat, THREE.HalfFloatType);
  tex.magFilter = tex.minFilter = THREE.LinearFilter;
  tex.generateMipmaps = false;
  tex.needsUpdate = true;
  const cell = (x, z) => {
    const i = Math.floor((x - minX) / res), j = Math.floor((maxZ - z) / res);
    return i < 0 || j < 0 || i >= w || j >= h ? -1 : j * w + i;
  };
  return {
    tex, minX, maxZ, width: w * res, height: h * res,
    depthAt: (x, z) => { const o = cell(x, z); return o < 0 ? 0 : depth[o]; },
    pondAt: (x, z) => { const o = cell(x, z); return o < 0 ? -1 : which[o]; },
  };
}

// The depth map's texture coordinates for a point of the scene (TSL; scene x is course -x)
export function depthUv(map, p) {
  return vec2(p.x.negate().sub(map.minX).div(map.width), float(map.maxZ).sub(p.z).div(map.height));
}

// ---------------------------------------------------------------- ripples

// Rings spreading out on the ponds: up to RINGS at once, each a centre (scene x, z), when it
// started and how strong. The water shader adds their slopes to its ripples.
const RINGS = 8;
export const ripples = { now: uniform(0), rings: Array.from({ length: RINGS }, () => uniform(new THREE.Vector4(0, 0, -100, 0))), next: 0 };

// Start a ring at course (x, z): strength 1 for a golf ball, less for a fish
export function addRipple(x, z, strength = 1) {
  const u = ripples.rings[ripples.next];
  ripples.next = (ripples.next + 1) % RINGS;
  u.value.set(-x, z, ripples.now.value, strength);
}

// The rings' slope at a point of the water (TSL vec2, scene x and z): a train of small waves
// round a front moving out at ~0.5 m/s, spreading and dying away over a few seconds
export const rippleSlope = Fn(([p]) => {
  let s = vec2(0, 0);
  for (const r of ripples.rings) {
    const age = ripples.now.sub(r.z).max(0);
    const d = length(p.sub(r.xy)).add(0.001);
    const front = age.mul(0.5).add(0.05);
    const spread = age.mul(0.18).add(0.12);
    const off = d.sub(front);
    const env = exp(off.mul(off).div(spread.mul(spread)).negate()).mul(exp(age.mul(-0.9))).mul(r.w).div(d.mul(0.8).add(1));
    // (waves 30 cm long; the slope is radial)
    const k = (2 * Math.PI) / 0.3;
    s = s.add(p.sub(r.xy).div(d).mul(cos(off.mul(k)).mul(env).mul(0.7)));
  }
  return s;
});

// ---------------------------------------------------------------- lily pads

function padTexture() {
  const c = document.createElement('canvas');
  c.width = c.height = 128;
  const g = c.getContext('2d');
  const grad = g.createRadialGradient(64, 64, 4, 64, 64, 62);
  grad.addColorStop(0, '#5f8a35'); grad.addColorStop(0.75, '#4c7a2c'); grad.addColorStop(0.95, '#6f8f3a'); grad.addColorStop(1, '#8a6a3a');
  g.fillStyle = grad; g.fillRect(0, 0, 128, 128);
  // (veins running out from the middle)
  g.strokeStyle = 'rgba(160,190,90,0.35)'; g.lineWidth = 1.2;
  for (let a = 0; a < 18; a++) { const t = (a / 18) * Math.PI * 2; g.beginPath(); g.moveTo(64, 64); g.lineTo(64 + Math.cos(t) * 62, 64 + Math.sin(t) * 62); g.stroke(); }
  const t = new THREE.CanvasTexture(c);
  t.colorSpace = THREE.SRGBColorSpace;
  return t;
}

// A water lily flower about 1 unit across: two rings of pointed petals round a yellow middle
function flowerGeometry() {
  const parts = [];
  const petal = (len, wid, tilt, turn, lift) => {
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.Float32BufferAttribute([0, 0, -wid, len * 0.55, 0, -wid * 0.9, len, 0, 0, 0, 0, wid, len * 0.55, 0, wid * 0.9], 3));
    g.setIndex([0, 1, 2, 0, 2, 4, 0, 4, 3]);
    g.rotateZ(tilt); g.translate(0, lift, 0); g.rotateY(turn);
    g.setAttribute('color', new THREE.Float32BufferAttribute(new Array(15).fill(1), 3));
    return g;
  };
  for (let i = 0; i < 8; i++) parts.push(petal(0.5, 0.11, 0.35, (i / 8) * Math.PI * 2, 0));
  for (let i = 0; i < 7; i++) parts.push(petal(0.36, 0.09, 0.8, ((i + 0.5) / 7) * Math.PI * 2, 0.03));
  const middle = new THREE.CylinderGeometry(0.08, 0.06, 0.12, 8).translate(0, 0.08, 0).toNonIndexed();
  middle.deleteAttribute('uv'); middle.deleteAttribute('normal');
  const n = middle.attributes.position.count;
  middle.setAttribute('color', new THREE.Float32BufferAttribute(new Array(n).fill(0).flatMap(() => [1, 0.78, 0.2]), 3));
  const flat = parts.map((g) => g.toNonIndexed()).concat(middle);
  const merged = new THREE.BufferGeometry();
  const pos = [], col = [];
  for (const g of flat) { pos.push(...g.attributes.position.array); col.push(...g.attributes.color.array); }
  merged.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  merged.setAttribute('color', new THREE.Float32BufferAttribute(col, 3));
  merged.computeVertexNormals();
  return merged;
}

// Lily pads in patches near the shores of natural and reedy ponds (now and then on the others),
// where the water is between 25 cm and 1.4 m deep; a few with a flower
function addLilies(group, ponds, map, budget) {
  const pads = [], flowers = [];
  const share = { natural: 1, reedy: 1.3, rocky: 0.3, manicured: 0.12 };
  ponds.forEach((wb, k) => {
    const xs = wb.poly.map((p) => p[0]), zs = wb.poly.map((p) => p[1]);
    const x0 = Math.min(...xs), x1 = Math.max(...xs), z0 = Math.min(...zs), z1 = Math.max(...zs);
    const want = Math.min(500, ((x1 - x0) * (z1 - z0) * 0.06 * (share[wb.style] ?? 0.5)) | 0);
    for (let tries = 0; tries < want * 12 && want > 0; tries++) {
      const x = x0 + Math.random() * (x1 - x0), z = z0 + Math.random() * (z1 - z0);
      if (map.pondAt(x, z) !== k) continue;
      const d = map.depthAt(x, z);
      if (d < 0.25 || d > 1.4) continue;
      // (in patches: thick in some, none in most)
      if (noise(x * 0.08 + k * 3.1, z * 0.08 - k * 1.7) < 0.25) continue;
      pads.push({ x, z, y: wb.level, r: 0.14 + Math.random() ** 1.5 * 0.3 });
      if (Math.random() < 0.06) flowers.push({ x: x + (Math.random() - 0.5) * 0.3, z: z + (Math.random() - 0.5) * 0.3, y: wb.level, r: 0.12 + Math.random() * 0.06, pink: Math.random() < 0.4 });
      if (pads.length >= want * (k + 1) || pads.length >= budget) break;
    }
  });
  if (!pads.length) return;
  // (a disc with its notch, lying flat)
  const padGeo = new THREE.CircleGeometry(1, 24, 0.22, Math.PI * 2 - 0.44).rotateX(-Math.PI / 2);
  const padMat = new THREE.MeshStandardNodeMaterial({ map: padTexture(), roughness: 0.42, side: THREE.DoubleSide });
  const padMesh = new THREE.InstancedMesh(padGeo, padMat, pads.length);
  const m = new THREE.Matrix4(), q = new THREE.Quaternion(), s = new THREE.Vector3(), p = new THREE.Vector3(), up = new THREE.Vector3(0, 1, 0), c = new THREE.Color();
  padMesh.instanceColor = new THREE.InstancedBufferAttribute(new Float32Array(pads.length * 3), 3);
  pads.forEach((pd, i) => {
    // (each a little above the last, so overlapping pads don't flicker)
    m.compose(p.set(-pd.x, pd.y + 0.008 + (i % 7) * 0.0015, pd.z), q.setFromAxisAngle(up, Math.random() * 6.28), s.set(pd.r, 1, pd.r));
    padMesh.setMatrixAt(i, m);
    const v = 0.8 + Math.random() * 0.35;
    padMesh.setColorAt(i, c.setRGB(v * (0.95 + Math.random() * 0.15), v, v * 0.9));
  });
  padMesh.receiveShadow = true;
  group.add(padMesh);
  if (flowers.length) {
    const fMat = new THREE.MeshStandardNodeMaterial({ vertexColors: true, roughness: 0.6, side: THREE.DoubleSide });
    const fMesh = new THREE.InstancedMesh(flowerGeometry(), fMat, flowers.length);
    fMesh.instanceColor = new THREE.InstancedBufferAttribute(new Float32Array(flowers.length * 3), 3);
    flowers.forEach((f, i) => {
      m.compose(p.set(-f.x, f.y + 0.02, f.z), q.setFromAxisAngle(up, Math.random() * 6.28), s.setScalar(f.r * 2));
      fMesh.setMatrixAt(i, m);
      fMesh.setColorAt(i, f.pink ? c.setRGB(1, 0.68, 0.8) : c.setRGB(1, 0.98, 0.94));
    });
    fMesh.castShadow = true;
    group.add(fMesh);
  }
  for (const o of group.children) { o.layers.set(1); o.frustumCulled = false; }
}

// ---------------------------------------------------------------- the water's own look

// What a metre of pond water takes out of the light (red, green, blue) for a murk of 0..1: even the
// clearest is a green-brown you see a metre or so into; the murkiest hides anything below half a
// metre. And the light it scatters back, which is all you see where it's deep (TSL).
export const pondAbsorb = (murk) => mix(vec3(1.5, 1.0, 1.3), vec3(3.4, 2.8, 3.2), murk);
export const pondGlow = (murk) => mix(vec3(0.025, 0.06, 0.05), vec3(0.045, 0.058, 0.03), murk);

// A material for things under the water's surface (fish, a turtle swimming), drawn after the
// water: as much of it shows as the water between it and the eye lets through, tinted, so a fish
// near the top shows wherever it is and one near the bottom only where it's shallow.
// level: an InstancedBufferAttribute of 2 for each one: the water level, the pond's murk.
function underwater(m, level) {
  const lv = instancedBufferAttribute(level, 'vec2');
  const toEye = cameraPosition.sub(positionWorld).normalize();
  const below = lv.x.sub(positionWorld.y).max(0);
  const through = exp(pondAbsorb(lv.y).mul(below.div(toEye.y.max(0.25))).negate());
  // (and less again at a glancing angle, where the surface mirrors the sky instead; anything out
  // of the water - a turtle on a log - all there)
  const surface = select(below.greaterThan(0.01), float(1).sub(float(1).sub(toEye.y.clamp(0, 1)).pow(5).mul(0.98)), float(1));
  m.transparent = true;
  m.opacityNode = through.y.mul(surface);
  // (tinted as the water takes out red first; the light down there dimmer)
  m.colorNode = m.colorNode.mul(through.div(through.y.max(0.001)).min(1.4)).mul(exp(below.mul(-0.4)).mul(0.4).add(0.6));
  return m;
}

// ---------------------------------------------------------------- fish

// The fish of golf-course ponds, by region: what's stocked or turns up there. Sizes (m), body
// width and depth (as parts of the length), colours (back, flank, belly, markings, fins), the
// markings, how deep the water they like is (bottom depth, m), how far under the surface they swim,
// how fast they cruise; some swim in little groups.
const SPECIES = {
  bass: { len: [0.25, 0.5], w: 0.19, h: 0.24, back: '#36421f', side: '#7c8a48', belly: '#e0dcc0', mark: 'band', ink: '#1f2612', fin: '#58623a', zone: [0.4, 1.8], under: [0.15, 0.7], speed: 0.35 },
  bluegill: { len: [0.12, 0.22], w: 0.16, h: 0.46, back: '#2a3528', side: '#56684a', belly: '#d88a38', mark: 'bars', ink: '#1c2319', fin: '#3a4436', zone: [0.25, 1.0], under: [0.08, 0.4], speed: 0.25, school: 5 },
  grasscarp: { len: [0.5, 0.9], w: 0.17, h: 0.19, back: '#454a36', side: '#8f9579', belly: '#dad8c6', mark: 'scales', ink: '#363a29', fin: '#64654f', zone: [0.4, 9], under: [0.04, 0.35], speed: 0.3 },
  carp: { len: [0.4, 0.7], w: 0.2, h: 0.27, back: '#55451f', side: '#a8883f', belly: '#e2cd8e', mark: 'scales', ink: '#45361a', fin: '#7a5a30', zone: [0.4, 9], under: [0.06, 0.8], speed: 0.25 },
  catfish: { len: [0.35, 0.65], w: 0.2, h: 0.17, back: '#363c44', side: '#77818c', belly: '#e8e8e2', mark: 'spots', ink: '#1e2228', fin: '#454b53', zone: [0.6, 9], under: [0.5, 3], speed: 0.2, bottom: true },
  tilapia: { len: [0.18, 0.32], w: 0.16, h: 0.4, back: '#444b42', side: '#899187', belly: '#d2d6ce', mark: 'bars', ink: '#545a52', fin: '#7c5a50', zone: [0.25, 1.1], under: [0.08, 0.45], speed: 0.25, school: 3 },
  perch: { len: [0.15, 0.3], w: 0.16, h: 0.28, back: '#475020', side: '#c2b44a', belly: '#ece6c8', mark: 'bars', ink: '#2b3218', fin: '#d0682a', zone: [0.4, 1.8], under: [0.2, 0.8], speed: 0.3, school: 4 },
  trout: { len: [0.25, 0.45], w: 0.16, h: 0.22, back: '#3b4734', side: '#a2a990', belly: '#ececE4', mark: 'trout', ink: '#1c2216', fin: '#6a705e', zone: [0.6, 9], under: [0.2, 1.0], speed: 0.4 },
  roach: { len: [0.15, 0.28], w: 0.15, h: 0.3, back: '#364644', side: '#b9c1bd', belly: '#eaeeec', mark: 'none', ink: '#364644', fin: '#c4532a', zone: [0.3, 1.5], under: [0.08, 0.5], speed: 0.3, school: 5 },
  tench: { len: [0.25, 0.45], w: 0.2, h: 0.25, back: '#2a361b', side: '#566828', belly: '#a8a456', mark: 'none', ink: '#2a361b', fin: '#2a361b', zone: [0.4, 1.6], under: [0.4, 1.2], speed: 0.2 },
};
const KINDS = Object.keys(SPECIES);
const FISH_REGIONS = {
  temperate: { bluegill: 4, bass: 3, grasscarp: 2, carp: 1, catfish: 1, perch: 1.5 },
  southeast: { bluegill: 4, bass: 3, grasscarp: 2.5, catfish: 1.5, carp: 1 },
  subtropical: { bluegill: 3, bass: 3, tilapia: 3, grasscarp: 2, catfish: 1 },
  tropical: { tilapia: 4, bass: 2, bluegill: 2, carp: 1 },
  desert: { bass: 3, bluegill: 3, grasscarp: 3, carp: 2, catfish: 1, tilapia: 1 },
  sonoran: { bass: 3, bluegill: 3, grasscarp: 3, carp: 2, catfish: 1, tilapia: 1 },
  socal: { bass: 3, bluegill: 3, grasscarp: 3, carp: 2, catfish: 1 },
  calcoast: { bass: 3, bluegill: 3, grasscarp: 2, carp: 2, catfish: 1 },
  mountain: { trout: 3, bass: 2, bluegill: 2, perch: 2, grasscarp: 1 },
  pacificnw: { trout: 3, bass: 2, bluegill: 2, perch: 2 },
  europe: { carp: 4, roach: 3, perch: 2, tench: 2 },
};
const pick = (mix) => {
  const e = Object.entries(mix), total = e.reduce((s, [, w]) => s + w, 0);
  let t = Math.random() * total;
  for (const [k, w] of e) { t -= w; if (t < 0) return k; }
  return e[0][0];
};

// Each kind's colouring side by side in one picture. Across each column: round the body (the
// middle is its back, the edges its belly); down: nose (top) to tail, the bottom tenth its fins.
function fishTexture() {
  const W = 64, H = 128;
  const c = document.createElement('canvas');
  c.width = W * KINDS.length; c.height = H;
  const g = c.getContext('2d');
  const dot = (x, y, r, col) => { g.fillStyle = col; g.beginPath(); g.ellipse(x, y, r, r * 0.8, 0, 0, 6.3); g.fill(); };
  KINDS.forEach((kind, v) => {
    const S = SPECIES[kind], x0 = v * W;
    g.save(); g.beginPath(); g.rect(x0, 0, W, H); g.clip();
    const grad = g.createLinearGradient(x0, 0, x0 + W, 0);
    grad.addColorStop(0, S.belly); grad.addColorStop(0.22, S.side); grad.addColorStop(0.5, S.back); grad.addColorStop(0.78, S.side); grad.addColorStop(1, S.belly);
    g.fillStyle = grad; g.fillRect(x0, 0, W, H);
    g.fillStyle = S.ink; g.strokeStyle = S.ink;
    if (S.mark === 'band') for (const s of [0.3, 0.7]) for (let y = 14; y < H * 0.88; y += 6) dot(x0 + W * s + (Math.random() - 0.5) * 6, y, 3.5 + Math.random() * 2, S.ink);
    if (S.mark === 'bars') for (let y = 22; y < H * 0.85; y += 13) { g.globalAlpha = 0.55; g.fillRect(x0 + W * 0.12, y, W * 0.76, 6); g.globalAlpha = 1; }
    if (S.mark === 'scales') { g.globalAlpha = 0.35; g.lineWidth = 1; for (let y = 6; y < H * 0.88; y += 5) for (let x = (y / 5) % 2 ? 2 : 5; x < W; x += 6) { g.beginPath(); g.arc(x0 + x, y, 3, 0.2, Math.PI - 0.2); g.stroke(); } g.globalAlpha = 1; }
    if (S.mark === 'spots') for (let k = 0; k < 30; k++) dot(x0 + W * 0.15 + Math.random() * W * 0.7, 10 + Math.random() * H * 0.75, 1.5, S.ink);
    if (S.mark === 'trout') {
      g.globalAlpha = 0.5; g.fillStyle = '#c86a78'; g.fillRect(x0 + W * 0.22, 10, W * 0.1, H * 0.78); g.fillRect(x0 + W * 0.68, 10, W * 0.1, H * 0.78); g.globalAlpha = 1;
      for (let k = 0; k < 50; k++) dot(x0 + W * 0.2 + Math.random() * W * 0.6, 8 + Math.random() * H * 0.85, 1.6, S.ink);
    }
    // (the head a little darker, the fins and tail at the bottom)
    g.globalAlpha = 0.25; g.fillStyle = S.ink; g.fillRect(x0, 0, W, H * 0.12); g.globalAlpha = 1;
    g.fillStyle = S.fin; g.fillRect(x0, H * 0.9, W, H * 0.1);
    g.restore();
  });
  const t = new THREE.CanvasTexture(c);
  t.colorSpace = THREE.SRGBColorSpace;
  return t;
}

// A fish 1 m long, nose at +z: a rounded body, a fanned tail and two front fins (made 0.19 wide
// and 0.22 deep; each kind is stretched to its own shape). uv: x round the body (0.5 on its back),
// y from nose (1) to tail (0.1); fins at y 0.05
function fishGeometry() {
  const prof = [];
  const N = 14;
  for (let i = 0; i <= N; i++) {
    const t = i / N; // 0 nose .. 1 tail end of body
    const r = Math.sin(Math.min(1, t * 1.25) * Math.PI) ** 0.7 * 0.11 * (1 - t * 0.55) + 0.004;
    prof.push(new THREE.Vector2(r, 0.5 - t * 0.85));
  }
  const body = new THREE.LatheGeometry(prof, 12).rotateX(-Math.PI / 2).rotateZ(Math.PI / 2);
  // (lathe: along y, turned to lie along z; flattened side to side a little)
  body.scale(0.85, 1, -1);
  const buv = body.attributes.uv;
  for (let i = 0; i < buv.count; i++) {
    // (uv.y from the lathe runs along the profile: nose 0 .. tail 1 -> 1 .. 0.1)
    buv.setY(i, 1 - buv.getY(i) * 0.9);
    buv.setX(i, (buv.getX(i) + 0.25) % 1);
  }
  const fin = (pts) => {
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.Float32BufferAttribute(pts.flat(), 3));
    g.setAttribute('uv', new THREE.Float32BufferAttribute(pts.flatMap(() => [0.5, 0.05]), 2));
    return g;
  };
  // (the tail fanned out flat: it is mostly seen from above, where an upright one is a line)
  const rl = [0.035, 0, -0.3], rr = [-0.035, 0, -0.3], tl = [0.13, 0, -0.52], tr = [-0.13, 0, -0.52], notch = [0, 0, -0.45];
  const tail = fin([rl, tl, notch, rl, notch, rr, rr, notch, tr]);
  const pecs = fin([[0.07, -0.02, 0.22], [0.18, -0.04, 0.12], [0.07, -0.02, 0.12], [-0.07, -0.02, 0.22], [-0.07, -0.02, 0.12], [-0.18, -0.04, 0.12]]);
  const dorsal = fin([[0, 0.1, 0.12], [0, 0.17, -0.02], [0, 0.08, -0.12]]);
  const parts = [body.toNonIndexed(), tail, pecs, dorsal];
  for (const g of parts) for (const k of Object.keys(g.attributes)) if (!['position', 'uv'].includes(k)) g.deleteAttribute(k);
  const pos = [], uvs = [];
  for (const g of parts) { pos.push(...g.attributes.position.array); uvs.push(...g.attributes.uv.array); }
  const out = new THREE.BufferGeometry();
  out.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  out.setAttribute('uv', new THREE.Float32BufferAttribute(uvs, 2));
  out.computeVertexNormals();
  return out;
}

// The fishes' material: its kind's part of the picture, the swimming wave down its body, and the
// water in front of it. data: each fish's kind, where in its stroke it is, and its sideways
// direction in the scene (x, z, as long as the fish), kept up to date as it turns.
function fishMaterial(data, level) {
  // (seen from above a fish is mostly its dark back, with little shine)
  const m = new THREE.MeshStandardNodeMaterial({ roughness: 0.7, side: THREE.DoubleSide, envMapIntensity: 0.35 });
  const k = instancedBufferAttribute(data, 'vec4');
  m.colorNode = texture(fishTexture(), vec2(uv().x.add(k.x).div(KINDS.length), uv().y)).rgb.mul(0.7);
  // (the tail sweeps most; the head barely moves. The wave is worked out along the fish as made,
  // and applied sideways to it where it swims: three.js has already placed it by then)
  const P = positionGeometry, back = float(0.5).sub(P.z).max(0);
  const sway = sin(P.z.mul(7).sub(time.mul(5)).add(k.y)).mul(back.mul(back).mul(0.08).add(0.004));
  m.positionNode = positionLocal.add(vec3(k.z, 0, k.w).mul(sway));
  return underwater(m, level);
}

// ---------------------------------------------------------------- turtles

// A turtle about 1 unit long (shell 0.75), nose at +z: a domed shell, olive-black with a yellow
// rim, a striped head and four flippers. 'limb': 0 shell, 1-4 the flippers, 5 the head.
function turtleGeometry() {
  const parts = [];
  const add = (g, col, limb) => {
    g = g.toNonIndexed();
    for (const k of Object.keys(g.attributes)) if (k !== 'position') g.deleteAttribute(k);
    const n = g.attributes.position.count;
    g.setAttribute('color', new THREE.Float32BufferAttribute(Array.from({ length: n }, () => col).flat(), 3));
    g.setAttribute('limb', new THREE.Float32BufferAttribute(new Array(n).fill(limb), 1));
    parts.push(g);
  };
  const shell = new THREE.SphereGeometry(0.5, 16, 8, 0, Math.PI * 2, 0, Math.PI / 2).scale(0.75, 0.38, 0.85).translate(0, 0.02, 0);
  // (the shell's colour varies from point to point: dark plates, lighter seams)
  const sp = shell.toNonIndexed(); const spn = sp.attributes.position.count;
  const sc = [];
  for (let i = 0; i < spn; i++) { const x = sp.attributes.position.getX(i), y = sp.attributes.position.getY(i), z = sp.attributes.position.getZ(i); const seam = Math.abs(Math.sin(x * 14) * Math.sin(z * 12)) < 0.12 ? 1 : 0, rim = y < 0.06 ? 1 : 0; sc.push(rim ? [0.5, 0.42, 0.12] : seam ? [0.22, 0.24, 0.12] : [0.09 + Math.random() * 0.03, 0.11, 0.06]); }
  for (const k of Object.keys(sp.attributes)) if (k !== 'position') sp.deleteAttribute(k);
  sp.setAttribute('color', new THREE.Float32BufferAttribute(sc.flat(), 3));
  sp.setAttribute('limb', new THREE.Float32BufferAttribute(new Array(spn).fill(0), 1));
  parts.push(sp);
  add(new THREE.CircleGeometry(0.5, 16).rotateX(Math.PI / 2).scale(0.75, 1, 0.85).translate(0, 0.02, 0), [0.55, 0.48, 0.2], 0);
  const skin = [0.12, 0.14, 0.08];
  add(new THREE.SphereGeometry(0.09, 8, 6).scale(0.9, 0.8, 1.3).translate(0, 0.04, 0.5), [0.16, 0.18, 0.09], 5);
  add(new THREE.SphereGeometry(0.03, 4, 3).scale(0.4, 1, 3).translate(0.04, 0.07, 0.5), [0.75, 0.62, 0.15], 5);
  add(new THREE.SphereGeometry(0.03, 4, 3).scale(0.4, 1, 3).translate(-0.04, 0.07, 0.5), [0.75, 0.62, 0.15], 5);
  const flipper = (x, z, l) => add(new THREE.SphereGeometry(0.1, 6, 4).scale(1.4, 0.25, 0.7).translate(x, 0.0, z), skin, l);
  flipper(0.38, 0.25, 1); flipper(-0.38, 0.25, 2); flipper(0.3, -0.3, 3); flipper(-0.3, -0.3, 4);
  add(new THREE.ConeGeometry(0.04, 0.15, 4).rotateX(-Math.PI / 2).translate(0, 0.02, -0.47), skin, 0);
  const out = new THREE.BufferGeometry();
  for (const a of ['position', 'color', 'limb']) {
    const arr = []; for (const g of parts) arr.push(...g.attributes[a].array);
    out.setAttribute(a, new THREE.Float32BufferAttribute(arr, a === 'limb' ? 1 : 3));
  }
  out.computeVertexNormals();
  return out;
}

// data: each turtle's paddling (0 still .. 1), where in its stroke it is, and its forward direction
// in the scene (x, z, as long as the turtle)
function turtleMaterial(data, level) {
  const m = new THREE.MeshStandardNodeMaterial({ vertexColors: true, roughness: 0.55 });
  const k = instancedBufferAttribute(data, 'vec4');
  m.colorNode = attribute('color', 'vec3');
  // (flippers swept back and forth, opposite corners together; the head bobbing a little)
  const limb = attribute('limb', 'float');
  const isFlipper = limb.greaterThan(0.5).and(limb.lessThan(4.5));
  const phase = select(limb.lessThan(1.5).or(limb.greaterThan(3.5).and(limb.lessThan(4.5))), float(0), float(Math.PI));
  const stroke = sin(time.mul(2.4).add(k.y).add(phase)).mul(0.07).mul(k.x);
  const nod = select(limb.greaterThan(4.5), sin(time.mul(0.7).add(k.y)).mul(0.03), float(0));
  m.positionNode = positionLocal.add(vec3(k.z, 0, k.w).mul(select(isFlipper, stroke, nod)));
  return underwater(m, level);
}

// A log lying from the bank out into the water, for turtles to sun themselves on
function logGeometry() {
  const g = new THREE.CylinderGeometry(1, 1.1, 1, 10, 1).rotateX(Math.PI / 2);
  const n = g.attributes.position.count, col = [];
  for (let i = 0; i < n; i++) { const v = 0.75 + Math.random() * 0.3; col.push(0.26 * v, 0.19 * v, 0.12 * v); }
  g.setAttribute('color', new THREE.Float32BufferAttribute(col, 3));
  return g;
}

// ---------------------------------------------------------------- splash

// A ball hitting the water: a white crown of water rising round the hole and droplets thrown up
// and out from it, a jet shooting up from the middle a moment later, and a ring of foam spreading
// and fading on the surface
function droplet() { return new THREE.IcosahedronGeometry(1, 1); }
function crownTexture() {
  const c = document.createElement('canvas');
  c.width = 128; c.height = 64;
  const g = c.getContext('2d');
  // (streaks of water up the crown, thinning and breaking up towards its rim at the top)
  for (let i = 0; i < 90; i++) {
    const x = Math.random() * 128, w = 1 + Math.random() * 3, top = Math.random() * 26;
    const grad = g.createLinearGradient(0, top, 0, 64);
    grad.addColorStop(0, 'rgba(255,255,255,0)'); grad.addColorStop(0.3, 'rgba(255,255,255,0.75)'); grad.addColorStop(1, 'rgba(255,255,255,0.35)');
    g.fillStyle = grad; g.fillRect(x, top, w, 64 - top);
  }
  return new THREE.CanvasTexture(c);
}
function ringTexture() {
  const c = document.createElement('canvas');
  c.width = c.height = 128;
  const g = c.getContext('2d');
  for (let i = 0; i < 420; i++) {
    const a = Math.random() * 6.28, r = 40 + (Math.random() - 0.5) * 30;
    g.fillStyle = `rgba(255,255,255,${0.35 + Math.random() * 0.55})`;
    g.beginPath(); g.arc(64 + Math.cos(a) * r, 64 + Math.sin(a) * r, 1 + Math.random() * 4, 0, 6.3); g.fill();
  }
  return new THREE.CanvasTexture(c);
}

export class Splashes {
  constructor() {
    this.group = new THREE.Group();
    const MAX = 220;
    // (water thrown into the air is white with all its broken surfaces: a little light of its own
    // so it reads against a bright sky's reflection too)
    this.drops = new THREE.InstancedMesh(droplet(), new THREE.MeshStandardNodeMaterial({ color: 0xf2f7f8, emissive: 0x9aa8ac, roughness: 0.25, transparent: true, opacity: 0.92 }), MAX);
    this.ring = new THREE.Mesh(new THREE.PlaneGeometry(1, 1).rotateX(-Math.PI / 2), new THREE.MeshBasicNodeMaterial({ map: ringTexture(), transparent: true, depthWrite: false }));
    this.crown = new THREE.Mesh(new THREE.CylinderGeometry(1, 0.55, 1, 28, 1, true).translate(0, 0.5, 0), new THREE.MeshBasicNodeMaterial({ map: crownTexture(), color: 0xe4ecee, transparent: true, depthWrite: false, side: THREE.DoubleSide }));
    for (const m of [this.drops, this.ring, this.crown]) { m.frustumCulled = false; m.renderOrder = 6; m.layers.set(1); this.group.add(m); }
    this.drops.count = 0; this.ring.visible = this.crown.visible = false;
    this.items = []; this.max = MAX; this.ringAt = null;
    this._m = new THREE.Matrix4(); this._q = new THREE.Quaternion(); this._s = new THREE.Vector3(); this._p = new THREE.Vector3();
  }

  // At course (x, z), the water's surface at y
  add(x, y, z) {
    const put = (vx, vy, vz, size, delay = 0) => { if (this.items.length < this.max) this.items.push({ x: -x, y, z, vx, vy, vz, size, delay, level: y }); };
    // (thrown off the crown's rim, up and out)
    for (let i = 0; i < 80; i++) {
      const a = Math.random() * 6.28, h = 0.6 + Math.random() * 1.3;
      put(Math.cos(a) * h, 1.6 + Math.random() * 1.9, Math.sin(a) * h, 0.022 + Math.random() * 0.035);
    }
    // (the jet: a column of drops shooting up as the hole closes, the first the highest)
    for (let i = 0; i < 34; i++) {
      const a = Math.random() * 6.28, h = Math.random() * 0.2;
      put(Math.cos(a) * h, 3.4 + Math.random() * 2, Math.sin(a) * h, 0.025 + Math.random() * 0.04, 0.16 + i * 0.006);
    }
    // (fine spray)
    for (let i = 0; i < 40; i++) {
      const a = Math.random() * 6.28, h = 1.2 + Math.random() * 1.8;
      put(Math.cos(a) * h, 0.8 + Math.random() * 1.5, Math.sin(a) * h, 0.008 + Math.random() * 0.01);
    }
    this.ringAt = { x: -x, y: y + 0.015, z, age: 0 };
  }

  update(dt) {
    dt = Math.min(dt, 0.05);
    let n = 0;
    this.items = this.items.filter((d) => {
      if (d.delay > 0) { d.delay -= dt; return true; }
      d.vy -= 9.8 * dt; d.x += d.vx * dt; d.y += d.vy * dt; d.z += d.vz * dt;
      if (d.y < d.level && d.vy < 0) return false;
      // (stretched along its fall when it moves fast)
      const sp = Math.hypot(d.vx, d.vy, d.vz);
      this._q.setFromUnitVectors(_up, this._p.set(d.vx / sp, d.vy / sp, d.vz / sp));
      this._m.compose(this._p.set(d.x, d.y, d.z), this._q, this._s.set(d.size, d.size * (1 + sp * 0.4), d.size));
      this.drops.setMatrixAt(n++, this._m);
      return true;
    });
    this.drops.count = n;
    this.drops.instanceMatrix.needsUpdate = true;
    const r = this.ringAt;
    if (r) {
      r.age += dt;
      const s = 0.3 + r.age * 1.1;
      this.ring.visible = r.age < 2.2;
      this.ring.position.set(r.x, r.y, r.z);
      this.ring.scale.set(s, 1, s);
      this.ring.material.opacity = Math.max(0, 1 - r.age / 2.2);
      // (the crown: rising and widening for a fifth of a second, then falling back)
      const c = r.age / 0.6;
      this.crown.visible = c < 1;
      this.crown.position.set(r.x, r.y - 0.02, r.z);
      const rad = 0.06 + Math.sqrt(c) * 0.32, tall = Math.sin(Math.min(1, c) * Math.PI) * 0.38;
      this.crown.scale.set(rad, Math.max(0.001, tall), rad);
      this.crown.material.opacity = Math.max(0, 1 - c * c) * 0.95;
      if (!this.ring.visible) this.ringAt = null;
    }
  }

  dispose() {
    for (const m of [this.drops, this.ring, this.crown]) { m.geometry.dispose(); m.material.map?.dispose(); m.material.dispose(); }
  }
}
const _up = new THREE.Vector3(0, 1, 0);

// ---------------------------------------------------------------- the ponds' life

// A spot in pond k where the bottom is between lo and hi metres down, or null
function spotIn(map, wb, k, lo, hi, tries = 80) {
  const xs = wb.poly.map((p) => p[0]), zs = wb.poly.map((p) => p[1]);
  const x0 = Math.min(...xs), x1 = Math.max(...xs), z0 = Math.min(...zs), z1 = Math.max(...zs);
  for (let i = 0; i < tries; i++) {
    const x = x0 + Math.random() * (x1 - x0), z = z0 + Math.random() * (z1 - z0);
    if (map.pondAt(x, z) !== k) continue;
    const d = map.depthAt(x, z);
    if (d >= lo && d <= hi) return [x, z];
  }
  return null;
}

// region: the course's region (treeplace.js treeRegion, or 'europe')
export class PondLife {
  constructor(ponds, map, { fish = 60, pads = 3000, turtles = 24, region = 'temperate' } = {}) {
    this.group = new THREE.Group();
    this.map = map;
    this.lilies = new THREE.Group();
    addLilies(this.lilies, ponds, map, pads);
    this.group.add(this.lilies);
    const mix = FISH_REGIONS[region] || FISH_REGIONS.temperate;
    // Fish: a few in each pond, more in bigger ones, each where its kind likes to be
    this.fish = [];
    ponds.forEach((wb, k) => {
      const xs = wb.poly.map((p) => p[0]), zs = wb.poly.map((p) => p[1]);
      const area = (Math.max(...xs) - Math.min(...xs)) * (Math.max(...zs) - Math.min(...zs));
      const want = Math.min(16, Math.round(area / 220));
      const murk = MURK[wb.style] ?? 0.4;
      for (let made = 0, tries = 0; made < want && tries < 40; tries++) {
        const kind = pick(mix), S = SPECIES[kind];
        const at = spotIn(map, wb, k, S.zone[0], S.zone[1]);
        if (!at) continue;
        // (some kinds in little groups, close together and much the same size)
        const group = S.school ? 2 + Math.floor(Math.random() * S.school) : 1, size = S.len[0] + Math.random() ** 1.4 * (S.len[1] - S.len[0]);
        const heading = Math.random() * 6.28;
        for (let g = 0; g < group && made < want; g++, made++) {
          this.fish.push({ kind, S, k, wb, murk, x: at[0] + (Math.random() - 0.5) * 1.2, z: at[1] + (Math.random() - 0.5) * 1.2, heading: heading + (Math.random() - 0.5) * 0.6, turn: 0, speed: S.speed * 0.6, want: S.speed,
            len: size * (0.85 + Math.random() * 0.3), under: S.under[0] + Math.random() * (S.under[1] - S.under[0]), y: wb.level - S.under[0], rise: 10 + Math.random() * 60 });
        }
      }
    });
    this.fish.length = Math.min(this.fish.length, fish);
    if (this.fish.length) {
      const n = this.fish.length;
      this.fishData = new THREE.InstancedBufferAttribute(new Float32Array(n * 4), 4).setUsage(THREE.DynamicDrawUsage);
      const lv = new THREE.InstancedBufferAttribute(new Float32Array(n * 2), 2);
      this.fish.forEach((f, i) => { this.fishData.setXY(i, KINDS.indexOf(f.kind), Math.random() * 6.28); lv.setXY(i, f.wb.level, f.murk); });
      this.fishMesh = new THREE.InstancedMesh(fishGeometry(), fishMaterial(this.fishData, lv), n);
      this.fishMesh.renderOrder = 3; // (after the ponds' water: see underwater())
      this.fishMesh.frustumCulled = false;
      this.fishMesh.layers.set(1);
      this.group.add(this.fishMesh);
    }
    // Turtles: in about half the natural and reedy ponds (and the odd other one): one or two
    // swimming, and a log by the bank with a couple sunning themselves on it
    this.turtles = [];
    const logs = [];
    ponds.forEach((wb, k) => {
      const odds = { natural: 0.55, reedy: 0.7, rocky: 0.2, manicured: 0.12 }[wb.style] ?? 0.3;
      if (Math.random() > odds) return;
      const murk = MURK[wb.style] ?? 0.4;
      const swim = 1 + Math.floor(Math.random() * 2.5);
      for (let i = 0; i < swim; i++) {
        const at = spotIn(map, wb, k, 0.5, 9);
        if (at) this.turtles.push({ k, wb, murk, x: at[0], z: at[1], heading: Math.random() * 6.28, turn: 0, speed: 0.1, len: 0.2 + Math.random() * 0.12, dive: 0, until: 20 + Math.random() * 60, y: wb.level - 0.05 });
      }
      // (the log: from shallows about 30 cm deep, pointing out from the bank)
      const at = spotIn(map, wb, k, 0.2, 0.45, 200);
      if (!at) return;
      const D = map.depthAt, e = 0.8;
      const gx = D(at[0] + e, at[1]) - D(at[0] - e, at[1]), gz = D(at[0], at[1] + e) - D(at[0], at[1] - e), gl = Math.hypot(gx, gz);
      if (gl < 0.02) return;
      const ux = gx / gl, uz = gz / gl, L = 2.4 + Math.random() * 1.6, r = 0.14 + Math.random() * 0.08;
      const log = { x: at[0], z: at[1], ux, uz, L, r, y: wb.level + r * 0.25 };
      logs.push(log);
      const bask = 1 + Math.floor(Math.random() * 3);
      for (let i = 0; i < bask; i++) {
        const along = (i / Math.max(1, bask - 1) - 0.5) * L * 0.5 + 0.2;
        this.turtles.push({ k, wb, murk, basking: true, x: at[0] + ux * along, z: at[1] + uz * along, heading: Math.atan2(ux, uz) + (Math.random() < 0.5 ? 0 : Math.PI) + (Math.random() - 0.5) * 0.4, len: 0.18 + Math.random() * 0.1, y: log.y + r * 0.95 });
      }
    });
    this.turtles.length = Math.min(this.turtles.length, turtles);
    if (logs.length) {
      const logMat = new THREE.MeshStandardNodeMaterial({ vertexColors: true, roughness: 0.92 });
      logMat.colorNode = attribute('color', 'vec3');
      const mesh = new THREE.InstancedMesh(logGeometry(), logMat, logs.length);
      logs.forEach((l, i) => {
        // (lying along the slope of the bed, its shore end a little higher)
        const q = new THREE.Quaternion().setFromUnitVectors(new THREE.Vector3(0, 0, 1), new THREE.Vector3(-l.ux, -0.08, l.uz).normalize());
        mesh.setMatrixAt(i, new THREE.Matrix4().compose(new THREE.Vector3(-l.x, l.y, l.z), q, new THREE.Vector3(l.r, l.r, l.L)));
      });
      mesh.castShadow = mesh.receiveShadow = true;
      mesh.layers.set(1);
      this.group.add(mesh);
    }
    if (this.turtles.length) {
      const n = this.turtles.length;
      this.turtleData = new THREE.InstancedBufferAttribute(new Float32Array(n * 4), 4).setUsage(THREE.DynamicDrawUsage);
      const lv = new THREE.InstancedBufferAttribute(new Float32Array(n * 2), 2);
      this.turtles.forEach((t, i) => { this.turtleData.setXY(i, t.basking ? 0 : 1, Math.random() * 6.28); lv.setXY(i, t.wb.level, t.murk); });
      this.turtleMesh = new THREE.InstancedMesh(turtleGeometry(), turtleMaterial(this.turtleData, lv), n);
      this.turtleMesh.renderOrder = 3;
      this.turtleMesh.frustumCulled = false;
      this.turtleMesh.castShadow = true;
      this.turtleMesh.layers.set(1);
      this.group.add(this.turtleMesh);
    }
    this._m = new THREE.Matrix4(); this._q = new THREE.Quaternion(); this._s = new THREE.Vector3(); this._p = new THREE.Vector3(); this._up = new THREE.Vector3(0, 1, 0);
    this.place(true);
  }

  // A fish or turtle steering towards water of the depth it likes: when what's ahead is too
  // deep or too shallow, it turns towards whichever side is nearer what it wants
  steer(f, lo, hi, dt) {
    const D = this.map.depthAt;
    f.turn += (Math.random() - 0.5) * 2 * dt - f.turn * 0.5 * dt;
    const look = 0.8 + f.speed * 3, ahead = D(f.x + Math.sin(f.heading) * look, f.z + Math.cos(f.heading) * look);
    if (ahead < Math.max(0.25, lo) || ahead > hi) {
      const mid = (Math.max(0.25, lo) + Math.min(hi, 3)) / 2;
      const l = D(f.x + Math.sin(f.heading + 0.8) * look, f.z + Math.cos(f.heading + 0.8) * look);
      const r = D(f.x + Math.sin(f.heading - 0.8) * look, f.z + Math.cos(f.heading - 0.8) * look);
      f.turn = (Math.abs(l - mid) <= Math.abs(r - mid) ? 1 : -1) * 1.2;
    }
    f.heading += f.turn * dt;
    const nx = f.x + Math.sin(f.heading) * f.speed * dt, nz = f.z + Math.cos(f.heading) * f.speed * dt;
    if (D(nx, nz) > 0.2) { f.x = nx; f.z = nz; } else f.heading += Math.PI * dt;
  }

  // (scene: x mirrored, so a heading turns the other way)
  set(mesh, i, x, y, z, heading, sx, sy, sz) {
    this._q.setFromAxisAngle(this._up, -heading);
    this._m.compose(this._p.set(-x, y, z), this._q, this._s.set(sx, sy, sz));
    mesh.setMatrixAt(i, this._m);
  }

  place(all = false, cx = 0, cz = 0) {
    const D = this.map.depthAt;
    this.fish.forEach((f, i) => {
      if (!all && Math.hypot(f.x - cx, f.z - cz) > 200) return;
      // (bottom feeders keep near the bottom; the rest at their own depth, never through it)
      const floor = D(f.x, f.z), want = f.S.bottom ? Math.max(0.1, floor - 0.15) : Math.min(f.under, Math.max(0.08, floor - 0.12));
      f.y += (f.wb.level - want - f.y) * 0.05;
      this.set(this.fishMesh, i, f.x, f.y, f.z, f.heading, f.len * f.S.w / 0.19, f.len * f.S.h / 0.22, f.len);
      this.fishData.setZ(i, Math.cos(f.heading) * f.len).setW(i, Math.sin(f.heading) * f.len);
    });
    this.turtles.forEach((t, i) => {
      if (!all && Math.hypot(t.x - cx, t.z - cz) > 200) return;
      this.set(this.turtleMesh, i, t.x, t.y, t.z, t.heading, t.len, t.len, t.len);
      this.turtleData.setZ(i, Math.sin(t.heading) * -t.len).setW(i, Math.cos(t.heading) * t.len);
    });
    if (this.fishMesh) { this.fishMesh.instanceMatrix.needsUpdate = true; this.fishData.needsUpdate = true; }
    if (this.turtleMesh) { this.turtleMesh.instanceMatrix.needsUpdate = true; this.turtleData.needsUpdate = true; }
  }

  // Fish wander, now cruising, now idling, keeping to the depths their kind likes; now and then
  // one comes up and rings the surface, or goes off somewhere else. Turtles paddle slowly about at
  // the surface, dive for a while now and then, and come up again.
  update(camera, dt) {
    dt = Math.min(dt, 0.1);
    const cx = -camera.position.x, cz = camera.position.z;
    for (const f of this.fish) {
      if (Math.hypot(f.x - cx, f.z - cz) > 200) continue;
      if (Math.random() < dt * 0.12) f.want = f.S.speed * (0.2 + Math.random() * 1.1);
      if (Math.random() < dt * 0.03) f.under = f.S.under[0] + Math.random() * (f.S.under[1] - f.S.under[0]);
      f.speed += (f.want - f.speed) * dt * 0.8;
      this.steer(f, f.S.zone[0], f.S.zone[1], dt);
      f.rise -= dt;
      if (f.rise < 0 && !f.S.bottom) { addRipple(f.x, f.z, 0.2 + f.len * 0.3); f.rise = 20 + Math.random() * 90; }
    }
    for (const t of this.turtles) {
      if (t.basking || Math.hypot(t.x - cx, t.z - cz) > 200) continue;
      t.until -= dt;
      if (t.until < 0) { t.dive = t.dive ? 0 : 1; t.until = t.dive ? 15 + Math.random() * 40 : 30 + Math.random() * 90; }
      t.speed += ((t.dive ? 0.18 : 0.09) - t.speed) * dt;
      this.steer(t, 0.5, 99, dt);
      const want = t.dive ? t.wb.level - Math.min(1.2, Math.max(0.3, this.map.depthAt(t.x, t.z) - 0.2)) : t.wb.level - 0.05;
      t.y += (want - t.y) * dt * 0.6;
    }
    this.place(false, cx, cz);
  }

  dispose() {
    this.group.traverse((o) => { if (o.isMesh) { o.geometry.dispose(); o.material.map?.dispose(); o.material.dispose(); } });
  }
}
