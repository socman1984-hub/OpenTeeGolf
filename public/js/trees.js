// Trees: where they stand (from tree-height maps, or the aerial photo as a fallback) and how
// they're drawn (procedural trees from ez-tree with bark and leaf textures).
//
// Drawing tens of thousands of full trees isn't possible, so each species exists twice:
// a detailed version used within NEAR metres of the camera and a light one beyond. Trees between
// the camera and the ball fade to a see-through stipple so they never hide your shot.
import * as THREE from 'three/webgpu';
import {
  Fn, uniform, vec2, vec3, float, positionWorld, positionLocal, cameraPosition, clamp, dot, length, mix,
  smoothstep, step, fract, screenCoordinate, sin, cos, time, instanceIndex,
} from 'three/tsl';
import { Tree } from 'ez-tree';

const NEAR = 70;
const T = (x, y, z) => new THREE.Vector3(-x, y, z);

// ------------------------------------------------------------------ where trees stand

// Tree tops from a 1 m canopy height grid ({ w, h, minX, maxZ, res, heights: Uint8Array }).
// Each local high point at least 4 m tall is a tree; its crown reaches out until the canopy
// drops well below the top (or rises into a neighbour). allowed(x, z) rules out playing surfaces.
export function treesFromCanopy(cm, allowed) {
  const { w, h, heights: H } = cm;
  const at = (i, j) => (i < 0 || j < 0 || i >= w || j >= h ? 0 : H[j * w + i]);
  // Light smoothing so one tree has one top
  const S = new Float32Array(w * h);
  for (let j = 1; j < h - 1; j++) for (let i = 1; i < w - 1; i++) {
    const k = j * w + i;
    if (H[k] < 3) continue;
    S[k] = (4 * H[k] + 2 * (H[k - 1] + H[k + 1] + H[k - w] + H[k + w]) + H[k - w - 1] + H[k - w + 1] + H[k + w - 1] + H[k + w + 1]) / 16;
  }
  const cand = [];
  for (let j = 1; j < h - 1; j++) for (let i = 1; i < w - 1; i++) {
    const k = j * w + i, v = S[k];
    if (v < 4) continue;
    if (v < S[k - 1] || v < S[k + 1] || v < S[k - w] || v < S[k + w] || v < S[k - w - 1] || v < S[k - w + 1] || v < S[k + w - 1] || v < S[k + w + 1]) continue;
    cand.push(k);
  }
  cand.sort((a, b) => S[b] - S[a]);
  // Tallest first; a top too close to a taller tree belongs to it
  const cell = 4, grid = new Map(), out = [];
  const key = (gx, gz) => gx * 73856093 ^ gz * 19349663;
  const dirs = Array.from({ length: 8 }, (_, n) => [Math.cos((n * Math.PI) / 4), Math.sin((n * Math.PI) / 4)]);
  for (const k of cand) {
    const i = k % w, j = (k - i) / w;
    const x = cm.minX + (i + 0.5) * cm.res, z = cm.maxZ - (j + 0.5) * cm.res;
    let top = 0;
    for (let dj = -1; dj <= 1; dj++) for (let di = -1; di <= 1; di++) top = Math.max(top, at(i + di, j + dj));
    const minD = Math.max(2.2, top * 0.2);
    const gx = Math.floor(x / cell), gz = Math.floor(z / cell);
    let clash = false;
    for (let a = -2; a <= 2 && !clash; a++) for (let b = -2; b <= 2 && !clash; b++) {
      for (const t of grid.get(key(gx + a, gz + b)) || []) if (Math.hypot(t.x - x, t.z - z) < minD) { clash = true; break; }
    }
    if (clash || !allowed(x, z)) continue;
    // Crown radius: walk out in 8 directions until the canopy falls away or climbs into another tree
    let sum = 0;
    for (const [dx, dz] of dirs) {
      let r = 1, prev = top;
      for (; r < 9; r++) {
        const v = at(Math.round(i + dx * r), Math.round(j - dz * r));
        if (v < top * 0.55 || v > prev + 1) break;
        prev = Math.min(prev, v);
      }
      sum += r;
    }
    const t = { x, z, h: top, r: Math.min(8, Math.max(1.4, sum / 8)), kind: null };
    out.push(t);
    const g = key(gx, gz);
    if (!grid.has(g)) grid.set(g, []);
    grid.get(g).push(t);
  }
  return out;
}

// Species: narrow crowns or dark blue-green canopy in the photo read as conifers; the rest is a
// deterministic mix of broadleaf trees. photo(x, z) -> [r, g, b] or null.
export function assignSpecies(trees, photo) {
  for (const t of trees) {
    const c = photo?.(t.x, t.z);
    const lum = c ? (c[0] + c[1] + c[2]) / 3 : 100;
    const bluish = c ? c[2] > c[0] * 0.95 && lum < 80 : false;
    const n = fract1(Math.sin(t.x * 12.9898 + t.z * 78.233) * 43758.5453);
    if (t.r / t.h < 0.16 || bluish || n < 0.06) t.kind = 'pine';
    else t.kind = t.h > 17 ? (n < 0.65 ? 'oakLarge' : 'ash') : n < 0.55 ? 'oak' : 'ash';
  }
  return trees;
}
const fract1 = (v) => v - Math.floor(v);

// ------------------------------------------------------------------ how they look

const SPECIES = {
  oak: 'Oak Medium', oakLarge: 'Oak Large', ash: 'Ash Medium', pine: 'Pine Medium',
};

// Camera-to-ball see-through: fragments near the line from the camera to the ball are dropped in
// a fine screen pattern (most of them, not all, so you still see the tree is there)
export const fade = { focus: uniform(new THREE.Vector3(0, -1e6, 0)), look: uniform(new THREE.Vector3(0, -1e6, 1)), on: uniform(0) };
const seeThroughMask = Fn(() => {
  const C = cameraPosition, v = fade.focus.sub(C);
  const t = dot(positionWorld.sub(C), v).div(dot(v, v).max(1e-4));
  const tc = clamp(t, 0, 1);
  const d = length(positionWorld.sub(C.add(v.mul(tc))));
  const R = mix(float(3.2), float(1.3), tc);
  const toBall = smoothstep(R.mul(0.55), R, d).oneMinus().mul(step(t, 0.97)).mul(step(-0.02, t));
  // ...and along the view towards the target, but only near you (further trees are real obstacles)
  const dir = fade.look.sub(C).normalize(), along = dot(positionWorld.sub(C), dir);
  const d2 = length(positionWorld.sub(C.add(dir.mul(along)))), R2 = along.mul(0.22).add(1.8);
  const ahead = smoothstep(R2.mul(0.6), R2, d2).oneMinus().mul(step(0, along)).mul(step(along, length(v).add(7)));
  const amount = toBall.max(ahead).mul(fade.on).mul(0.85);
  const noise = fract(float(52.9829189).mul(fract(dot(screenCoordinate.xy, vec2(0.06711056, 0.00583715)))));
  return noise.greaterThanEqual(amount);
});

// Distant stand-in for a model: its trunk as a tapered cylinder and ~28 big leaf cards (each two
// crossed quads with the same leaf texture) where the detailed tree has foliage. ~130 triangles.
function buildFarModel(near, cards = 56) {
  const trunkH = Math.max(0.25, near.crownBase + 0.15) * near.height;
  const trunk = new THREE.CylinderGeometry(near.trunk * 0.55, near.trunk, trunkH, 6, 1, true).translate(0, trunkH / 2, 0);
  const lp = near.leaves.attributes.position, pos = [], uv = [], nrm = [];
  const size = near.crown * 0.9;
  let seed = 11;
  const rnd = () => ((seed = (seed * 16807) % 2147483647) / 2147483647);
  for (let c = 0; c < cards; c++) {
    const v = Math.floor(rnd() * lp.count);
    const cx = lp.getX(v) * 0.85, cy = lp.getY(v), cz = lp.getZ(v) * 0.85;
    const a = rnd() * Math.PI, s = size * (0.75 + rnd() * 0.5);
    for (const ang of [a, a + Math.PI / 2]) {
      const dx = Math.cos(ang) * s / 2, dz = Math.sin(ang) * s / 2, h = s / 2;
      const q = [[cx - dx, cy - h, cz - dz, 0, 0], [cx + dx, cy - h, cz + dz, 1, 0], [cx + dx, cy + h, cz + dz, 1, 1], [cx - dx, cy + h, cz - dz, 0, 1]];
      // Normals point out from the trunk (and a little up), like a rounded crown
      const nl = Math.hypot(cx, near.crown * 0.6, cz) || 1;
      for (const k of [0, 1, 2, 0, 2, 3]) { pos.push(q[k][0], q[k][1], q[k][2]); uv.push(q[k][3], q[k][4]); nrm.push(cx / nl, (near.crown * 0.6) / nl, cz / nl); }
    }
  }
  const leaves = new THREE.BufferGeometry();
  leaves.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  leaves.setAttribute('normal', new THREE.Float32BufferAttribute(nrm, 3));
  leaves.setAttribute('uv', new THREE.Float32BufferAttribute(uv, 2));
  return { ...near, bark: trunk, leaves };
}

// A species model: ez-tree geometry centred on its base, plus its size facts
function buildModel(preset) {
  const tree = new Tree();
  tree.loadPreset(preset);
  const o = tree.options;
  // The presets use oversized leaves; at real tree sizes they'd be 70 cm across. Smaller, more of them.
  o.leaves.size *= 0.7;
  o.leaves.count = Math.round(o.leaves.count * 2.4);
  tree.generate();
  const bark = tree.branchesMesh.geometry, leaves = tree.leavesMesh.geometry;
  bark.computeBoundingBox(); leaves.computeBoundingBox();
  const bb = bark.boundingBox, lb = leaves.boundingBox;
  const height = Math.max(bb.max.y, lb.max.y) - bb.min.y;
  const crown = Math.max(lb.max.x - lb.min.x, lb.max.z - lb.min.z) / 2;
  for (const g of [bark, leaves]) g.translate(0, -bb.min.y, 0);
  // Trunk radius near the ground
  const p = bark.attributes.position;
  let trunk = 0;
  for (let i = 0; i < p.count; i++) if (p.getY(i) < height * 0.03) trunk = Math.max(trunk, Math.hypot(p.getX(i), p.getZ(i)));
  return {
    bark, leaves, height, crown, trunk: trunk || height * 0.02, crownBase: (lb.min.y - bb.min.y) / height,
    barkMat: tree.branchesMesh.material, leafMat: tree.leavesMesh.material,
  };
}

function barkMaterial(src) {
  const m = new THREE.MeshStandardNodeMaterial({ map: src.map || null, normalMap: src.normalMap || null, roughness: 0.95, metalness: 0 });
  m.envMapIntensity = 0.5;
  if (src.color) m.color.copy(src.color);
  m.maskNode = seeThroughMask();
  return m;
}
function leafMaterial(src) {
  const m = new THREE.MeshStandardNodeMaterial({
    map: src.map || null, alphaTest: Math.max(0.35, src.alphaTest || 0.5), side: THREE.DoubleSide, roughness: 0.82, metalness: 0,
  });
  m.color.setRGB(0.6, 0.68, 0.5); // shaded inside the crown, and a richer summer green
  m.envMapIntensity = 0.45;
  // Leaves sway a little, the tops most, each tree in its own rhythm
  const phase = float(instanceIndex).mul(1.618).fract().mul(6.283);
  const sway = positionLocal.y.mul(0.0035);
  m.positionNode = positionLocal.add(vec3(sin(time.mul(1.7).add(phase)), 0, cos(time.mul(1.3).add(phase.mul(1.7)))).mul(sway));
  m.maskNode = seeThroughMask();
  return m;
}

export class Forest {
  // quality: { trees: 0..1 share, shadows: boolean for far trees }
  constructor(quality = {}) {
    this.quality = quality;
    this.group = new THREE.Group();
    this.models = {};
    this.species = {};
    this.lastCam = null;
  }

  // trees: [{ x, z, h, r, kind }]; heightAt(x, z) for the ground
  build(trees, heightAt, envApply = () => {}) {
    this.group.clear();
    this.species = {};
    const bySpecies = {};
    for (const t of trees) (bySpecies[t.kind] ||= []).push(t);
    const m = new THREE.Matrix4(), q = new THREE.Quaternion(), s = new THREE.Vector3(), up = new THREE.Vector3(0, 1, 0);
    for (const [kind, list] of Object.entries(bySpecies)) {
      const near = (this.models[`${kind}:near`] ||= buildModel(SPECIES[kind]));
      const far = (this.models[`${kind}:far`] ||= buildFarModel(near));
      // One transform (and leaf tint) per tree, shared by both levels of detail
      const mats = new Float32Array(list.length * 16), tints = new Float32Array(list.length * 3), pos = new Float32Array(list.length * 2);
      list.forEach((t, i) => {
        const y = heightAt(t.x, t.z) ?? 0;
        const sy = t.h / near.height;
        // Crown width from the map, but never stretched far from the model's own shape
        const sxz = Math.min(sy * 1.5, Math.max(sy * 0.7, t.r / Math.max(0.1, near.crown)));
        const rot = fract1(Math.sin(t.x * 7.31 + t.z * 3.17) * 9137.1) * Math.PI * 2;
        m.compose(T(t.x, y - 0.15, t.z), q.setFromAxisAngle(up, rot), s.set(sxz, sy, sxz));
        m.toArray(mats, i * 16);
        const n = fract1(Math.sin(t.x * 3.7 + t.z * 1.3) * 5137.7);
        const n2 = fract1(n * 7.77);
        // Each tree a slightly different green (the leaf texture carries the colour)
        tints[i * 3] = 0.82 + 0.3 * n; tints[i * 3 + 1] = 0.9 + 0.18 * n2; tints[i * 3 + 2] = 0.78 + 0.2 * n2;
        pos[i * 2] = -t.x; pos[i * 2 + 1] = t.z;
        // Collision shape for the ball, matching what's drawn
        t.y = y;
        t.trunkR = Math.max(0.12, near.trunk * sxz);
        const crownBase = y + near.crownBase * t.h;
        if (kind === 'pine') Object.assign(t, { trunkTop: crownBase + 0.6, coneBase: crownBase, coneH: t.h - near.crownBase * t.h, coneR: near.crown * sxz });
        else Object.assign(t, { trunkTop: crownBase + 0.3, cy: (crownBase + y + t.h) / 2, rh: near.crown * sxz, rv: (y + t.h - crownBase) / 2 });
      });
      // Both levels of detail share one bark and one leaf material
      const barkM = barkMaterial(near.barkMat), leafM = leafMaterial(near.leafMat);
      envApply(barkM); envApply(leafM);
      const make = (model, cap, castShadow) => {
        const b = new THREE.InstancedMesh(model.bark, barkM, Math.max(1, cap));
        const l = new THREE.InstancedMesh(model.leaves, leafM, Math.max(1, cap));
        for (const im of [b, l]) { im.count = 0; im.castShadow = castShadow; im.receiveShadow = true; im.frustumCulled = false; this.group.add(im); }
        l.instanceColor = new THREE.InstancedBufferAttribute(new Float32Array(Math.max(1, cap) * 3), 3);
        return { bark: b, leaves: l };
      };
      this.species[kind] = {
        list, mats, tints, pos,
        near: make(near, Math.min(list.length, 600), true),
        far: make(far, list.length, this.quality.farShadows !== false),
      };
    }
    this.lastCam = null;
    return trees;
  }

  // Re-sort trees into near and far as the camera moves
  update(cam, force = false) {
    if (!force && this.lastCam && Math.hypot(cam.x - this.lastCam.x, cam.z - this.lastCam.z) < 15) return;
    this.lastCam = { x: cam.x, z: cam.z };
    const r2 = NEAR * NEAR;
    for (const sp of Object.values(this.species)) {
      let nn = 0, nf = 0;
      const nearCap = sp.near.bark.instanceMatrix.count;
      const nb = sp.near.bark.instanceMatrix.array, nl = sp.near.leaves.instanceMatrix.array, nc = sp.near.leaves.instanceColor.array;
      const fb = sp.far.bark.instanceMatrix.array, fl = sp.far.leaves.instanceMatrix.array, fc = sp.far.leaves.instanceColor.array;
      for (let i = 0; i < sp.list.length; i++) {
        const dx = sp.pos[i * 2] - cam.x, dz = sp.pos[i * 2 + 1] - cam.z;
        const src = sp.mats.subarray(i * 16, i * 16 + 16), tint = sp.tints.subarray(i * 3, i * 3 + 3);
        if (dx * dx + dz * dz < r2 && nn < nearCap) {
          nb.set(src, nn * 16); nl.set(src, nn * 16); nc.set(tint, nn * 3); nn++;
        } else {
          fb.set(src, nf * 16); fl.set(src, nf * 16); fc.set(tint, nf * 3); nf++;
        }
      }
      for (const [lod, n] of [[sp.near, nn], [sp.far, nf]]) {
        lod.bark.count = n; lod.leaves.count = n;
        lod.bark.instanceMatrix.needsUpdate = true; lod.leaves.instanceMatrix.needsUpdate = true; lod.leaves.instanceColor.needsUpdate = true;
      }
    }
  }

  triangles() {
    let n = 0;
    for (const sp of Object.values(this.species)) for (const lod of [sp.near, sp.far]) {
      for (const im of [lod.bark, lod.leaves]) n += ((im.geometry.index?.count ?? im.geometry.attributes.position.count) / 3) * im.count;
    }
    return n;
  }
}
