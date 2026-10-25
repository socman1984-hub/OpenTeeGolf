// Life in the ponds: how deep each one is (for its clear water), rings spreading where the ball
// splashes in or a fish rises, lily pads in patches near the shore, and koi gliding about under
// the surface. (After Koi Pond Garden by Sourany Phomhome, MIT.)
//
// Ponds: the course's still water bodies (wb.poly, wb.level, wb.style), course coordinates;
// course x is the scene's mirrored.
import * as THREE from 'three/webgpu';
import { Fn, uniform, vec2, vec3, float, sin, cos, exp, length, time, instancedBufferAttribute, positionLocal, positionGeometry, uv, texture } from 'three/tsl';
import { pointInPoly, noise } from './course.js';

// How murky each kind of pond is (0 clear .. 1 thick with silt and algae)
const MURK = { manicured: 0.14, rocky: 0.1, natural: 0.3, reedy: 0.42 };

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

// ---------------------------------------------------------------- koi

// Six varieties side by side in one picture: kohaku (white, red patches), ogon (gold), showa
// (black with red and white), chagoi (brown-orange), asagi (blue-grey, red belly), tancho (white,
// one red spot on the head). Across each sixth: round the body; down: nose (top) to tail.
const VARIETIES = 6;
function koiTexture() {
  const W = 64, H = 128;
  const c = document.createElement('canvas');
  c.width = W * VARIETIES; c.height = H;
  const g = c.getContext('2d');
  const blob = (x, y, rx, ry, col) => { g.fillStyle = col; g.beginPath(); g.ellipse(x, y, rx, ry, 0, 0, 6.3); g.fill(); };
  for (let v = 0; v < VARIETIES; v++) {
    const x0 = v * W;
    g.save(); g.beginPath(); g.rect(x0, 0, W, H); g.clip();
    const base = ['#f4f1ea', '#e8b53a', '#1d1c1e', '#b8662e', '#8fa3b4', '#f4f1ea'][v];
    g.fillStyle = base; g.fillRect(x0, 0, W, H);
    // (the back is the middle of each sixth across: u = 0.5 faces up)
    if (v === 0) for (let k = 0; k < 4; k++) blob(x0 + W / 2 + (Math.random() - 0.5) * 18, 20 + k * 22 + Math.random() * 8, 12 + Math.random() * 8, 9 + Math.random() * 6, '#d8361f');
    if (v === 1) { const gr = g.createLinearGradient(x0, 0, x0 + W, 0); gr.addColorStop(0, 'rgba(255,240,180,0.6)'); gr.addColorStop(0.5, 'rgba(255,255,255,0)'); gr.addColorStop(1, 'rgba(255,240,180,0.6)'); g.fillStyle = gr; g.fillRect(x0, 0, W, H); }
    if (v === 2) for (let k = 0; k < 6; k++) blob(x0 + W / 2 + (Math.random() - 0.5) * 30, 12 + k * 18, 8 + Math.random() * 7, 6 + Math.random() * 5, k % 2 ? '#e9e4da' : '#cf3420');
    if (v === 3) for (let k = 0; k < 40; k++) blob(x0 + Math.random() * W, Math.random() * H, 3, 2, 'rgba(90,40,15,0.35)');
    if (v === 4) { blob(x0 + 4, H / 2, 10, 60, '#c9502a'); blob(x0 + W - 4, H / 2, 10, 60, '#c9502a'); for (let k = 0; k < 60; k++) blob(x0 + W / 2 + (Math.random() - 0.5) * 30, Math.random() * H, 2.5, 2, 'rgba(40,60,90,0.4)'); }
    if (v === 5) blob(x0 + W / 2, 14, 9, 8, '#d8301e');
    // (fins and tail: the last eighth down, pale and see-through-ish)
    g.fillStyle = v === 2 ? 'rgba(40,40,40,0.7)' : 'rgba(255,240,225,0.75)'; g.fillRect(x0, H * 0.9, W, H * 0.1);
    g.restore();
  }
  const t = new THREE.CanvasTexture(c);
  t.colorSpace = THREE.SRGBColorSpace;
  return t;
}

// A koi 1 m long, nose at +z: a rounded body, flattened a little, a forked tail and two front fins.
// uv: x round the body (0.5 on its back), y from nose (1) to tail (0.1); fins at y 0.05
function koiGeometry() {
  const prof = [];
  const N = 14;
  for (let i = 0; i <= N; i++) {
    const t = i / N; // 0 nose .. 1 tail end of body
    const r = Math.sin(Math.min(1, t * 1.25) * Math.PI) ** 0.7 * 0.11 * (1 - t * 0.55) + 0.004;
    prof.push(new THREE.Vector2(r, 0.5 - t * 0.85));
  }
  const body = new THREE.LatheGeometry(prof, 12).rotateX(-Math.PI / 2).rotateZ(Math.PI / 2);
  // (lathe: along y, turned to lie along z; flattened side to side a little and deeper than wide)
  body.scale(0.85, 1, -1);
  const bp = body.attributes.position, buv = body.attributes.uv;
  for (let i = 0; i < bp.count; i++) {
    // uv.y from the lathe runs along the profile: nose 0 .. tail 1 -> 1 .. 0.1
    buv.setY(i, 1 - buv.getY(i) * 0.9);
    buv.setX(i, (buv.getX(i) + 0.25) % 1);
  }
  const fin = (pts, uvy) => {
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.Float32BufferAttribute(pts.flat(), 3));
    g.setAttribute('uv', new THREE.Float32BufferAttribute(pts.flatMap(() => [0.5, uvy]), 2));
    g.computeVertexNormals();
    return g;
  };
  // (the tail fanned out flat: it is mostly seen from above, where an upright one is a line)
  const rl = [0.035, 0, -0.3], rr = [-0.035, 0, -0.3], tl = [0.14, 0, -0.53], tr = [-0.14, 0, -0.53], notch = [0, 0, -0.46];
  const tail = fin([rl, tl, notch, rl, notch, rr, rr, notch, tr], 0.05);
  const pecs = fin([[0.07, -0.02, 0.22], [0.2, -0.04, 0.12], [0.07, -0.02, 0.12], [-0.07, -0.02, 0.22], [-0.07, -0.02, 0.12], [-0.2, -0.04, 0.12]], 0.05);
  const parts = [body.toNonIndexed(), tail, pecs];
  for (const g of parts) for (const k of Object.keys(g.attributes)) if (!['position', 'uv'].includes(k)) g.deleteAttribute(k);
  const pos = [], uvs = [];
  for (const g of parts) { pos.push(...g.attributes.position.array); uvs.push(...g.attributes.uv.array); }
  const out = new THREE.BufferGeometry();
  out.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  out.setAttribute('uv', new THREE.Float32BufferAttribute(uvs, 2));
  out.computeVertexNormals();
  return out;
}

// The koi's material: its variety's part of the picture, and the swimming wave down its body
// koi: each fish's variety, where in its stroke it is, and its sideways direction in the scene
// (x, z, as long as the fish: an InstancedBufferAttribute of 4, kept up to date as it turns)
function koiMaterial(koi) {
  const m = new THREE.MeshStandardNodeMaterial({ roughness: 0.35, side: THREE.DoubleSide });
  const k = instancedBufferAttribute(koi, 'vec4');
  const tex = koiTexture();
  m.colorNode = texture(tex, vec2(uv().x.add(k.x).div(VARIETIES), uv().y)).rgb;
  // (the tail sweeps most; the head barely moves. The wave is worked out along the fish as made,
  // and applied sideways to it where it swims: three.js has already placed it by then)
  const P = positionGeometry, back = float(0.5).sub(P.z).max(0);
  const sway = sin(P.z.mul(7).sub(time.mul(5)).add(k.y)).mul(back.mul(back).mul(0.08).add(0.004));
  m.positionNode = positionLocal.add(vec3(k.z, 0, k.w).mul(sway));
  return m;
}

export class PondLife {
  constructor(ponds, map, { koi = 60, pads = 3000 } = {}) {
    this.group = new THREE.Group();
    this.map = map;
    this.lilies = new THREE.Group();
    addLilies(this.lilies, ponds, map, pads);
    this.group.add(this.lilies);
    // Koi: a few in each pond deep enough for them, more in bigger ones
    this.fish = [];
    ponds.forEach((wb, k) => {
      const xs = wb.poly.map((p) => p[0]), zs = wb.poly.map((p) => p[1]);
      const area = (Math.max(...xs) - Math.min(...xs)) * (Math.max(...zs) - Math.min(...zs));
      const n = Math.min(14, Math.round(area / 250));
      for (let i = 0, tries = 0; i < n && tries < 200; tries++) {
        const x = Math.min(...xs) + Math.random() * (Math.max(...xs) - Math.min(...xs)), z = Math.min(...zs) + Math.random() * (Math.max(...zs) - Math.min(...zs));
        if (map.pondAt(x, z) !== k || map.depthAt(x, z) < 0.7) continue;
        this.fish.push({ k, wb, x, z, heading: Math.random() * 6.28, turn: 0, speed: 0.25, want: 0.3, len: 0.35 + Math.random() ** 1.5 * 0.45, under: 0.25 + Math.random() * 0.35, rise: 5 + Math.random() * 30 });
        i++;
      }
    });
    this.fish.length = Math.min(this.fish.length, koi);
    if (this.fish.length) {
      const geo = koiGeometry();
      const attr = new THREE.InstancedBufferAttribute(new Float32Array(this.fish.length * 4), 4);
      attr.setUsage(THREE.DynamicDrawUsage);
      this.fish.forEach((f, i) => { attr.setXY(i, Math.floor(Math.random() * VARIETIES), Math.random() * 6.28); });
      this.koiData = attr;
      this.koi = new THREE.InstancedMesh(geo, koiMaterial(attr), this.fish.length);
      this.koi.frustumCulled = false;
      this.koi.layers.set(1);
      this.group.add(this.koi);
    }
    this._m = new THREE.Matrix4(); this._q = new THREE.Quaternion(); this._s = new THREE.Vector3(); this._p = new THREE.Vector3(); this._up = new THREE.Vector3(0, 1, 0);
  }

  // Each koi wanders: turning a little this way and that, now cruising, now drifting, and
  // turning away from the shallows; now and then one rises and rings the surface
  update(camera, dt) {
    if (!this.koi) return;
    dt = Math.min(dt, 0.1);
    const cx = -camera.position.x, cz = camera.position.z;
    const D = this.map.depthAt;
    this.fish.forEach((f, i) => {
      if (Math.hypot(f.x - cx, f.z - cz) > 200) return;
      f.turn += (Math.random() - 0.5) * 2 * dt - f.turn * 0.5 * dt;
      if (Math.random() < dt * 0.15) f.want = 0.08 + Math.random() * 0.45;
      f.speed += (f.want - f.speed) * dt * 0.8;
      // (feel ahead for shallow water: turn towards whichever side is deeper)
      const ax = Math.sin(f.heading), az = Math.cos(f.heading), look = 0.8 + f.speed * 3;
      if (D(f.x + ax * look, f.z + az * look) < 0.55) {
        const l = D(f.x + Math.sin(f.heading + 0.8) * look, f.z + Math.cos(f.heading + 0.8) * look);
        const r = D(f.x + Math.sin(f.heading - 0.8) * look, f.z + Math.cos(f.heading - 0.8) * look);
        f.turn = (l >= r ? 1 : -1) * 1.4;
      }
      f.heading += f.turn * dt;
      const nx = f.x + Math.sin(f.heading) * f.speed * dt, nz = f.z + Math.cos(f.heading) * f.speed * dt;
      if (D(nx, nz) > 0.4) { f.x = nx; f.z = nz; } else f.heading += Math.PI * dt;
      f.rise -= dt;
      if (f.rise < 0) { addRipple(f.x, f.z, 0.25); f.rise = 15 + Math.random() * 60; }
      const y = f.wb.level - Math.min(f.under, Math.max(0.15, D(f.x, f.z) - 0.15));
      // (scene: x mirrored, so the heading turns the other way)
      this._q.setFromAxisAngle(this._up, -f.heading);
      this._m.compose(this._p.set(-f.x, y, f.z), this._q, this._s.setScalar(f.len));
      this.koi.setMatrixAt(i, this._m);
      this.koiData.setZ(i, Math.cos(f.heading) * f.len).setW(i, Math.sin(f.heading) * f.len);
    });
    this.koi.instanceMatrix.needsUpdate = true;
    this.koiData.needsUpdate = true;
  }

  dispose() {
    this.group.traverse((o) => { if (o.isMesh) { o.geometry.dispose(); o.material.map?.dispose(); o.material.dispose(); } });
  }
}

