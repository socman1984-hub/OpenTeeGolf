// Trees: where they stand (from tree-height maps, or the aerial photo as a fallback) and how
// they're drawn (procedural trees from ez-tree with bark and leaf textures).
//
// Every tree is one of ~14 variants (different species, presets, seeds and branching), turned,
// stretched and tinted on its own so no two look alike. Each variant is drawn three ways:
//   near     - the full tree (leaves, branches, bark) close to the camera
//   mid      - a lighter build of the same tree further out (same trunk and limbs)
//   impostor - pictures of the full tree, taken from 24 directions when the course loads, shown
//              on a single card that always faces you, lit by the sun using baked normals
// Trees on the edge of woods (the ones you actually see) stay detailed much further out than
// trees inside a wood, and trees behind the camera never take the detailed slots.
// Trees between the camera and the ball fade to a see-through stipple.
import * as THREE from 'three/webgpu';
import {
  Fn, uniform, vec2, vec3, vec4, float, uv, positionWorld, positionLocal, cameraPosition, clamp, dot, length, mix,
  smoothstep, step, fract, floor, screenCoordinate, sin, cos, atan, asin, normalize, cross, max, texture,
  time, instanceIndex, instancedBufferAttribute, normalView, faceDirection, select,
} from 'three/tsl';
import { Tree } from 'ez-tree';

const T = (x, y, z) => new THREE.Vector3(-x, y, z);
const fract1 = (v) => v - Math.floor(v);
const hash = (x, z, k = 1) => fract1(Math.sin(x * 12.9898 * k + z * 78.233) * 43758.5453);

// ------------------------------------------------------------------ where trees stand

// Tree tops from a 1 m canopy height grid ({ w, h, minX, maxZ, res, heights: Uint8Array }).
// Each local high point at least 4 m tall is a tree; its crown reaches out until the canopy
// drops well below the top (or rises into a neighbour). allowed(x, z) rules out playing surfaces.
// Trees with open ground beside them are marked edge (the ones a golfer sees).
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
    const r = Math.min(8, Math.max(1.4, sum / 8));
    // Edge of a wood: open ground (no canopy) a little beyond the crown in some direction
    let edge = false;
    for (const [dx, dz] of dirs) {
      if (at(Math.round(i + dx * (r + 6)), Math.round(j - dz * (r + 6))) < 2 && at(Math.round(i + dx * (r + 12)), Math.round(j - dz * (r + 12))) < 2) { edge = true; break; }
    }
    const t = { x, z, h: top, r, kind: null, edge };
    out.push(t);
    const g = key(gx, gz);
    if (!grid.has(g)) grid.set(g, []);
    grid.get(g).push(t);
  }
  return out;
}

// Trees people have mapped in OpenStreetMap (single trees, and tree rows sampled every 7 m) added
// to the ones already found (canopy map or imagery). A mapped tree with a found tree within a few
// metres is that tree (OSM positions are often a few metres off), so nothing is added - though a
// needle-leaved tag still makes it a conifer. Otherwise a tree goes in at the mapped spot: as tall
// as the canopy map says there, else the mapped height, else a typical one.
// heightAt(x, z) -> canopy height in m (or 0/null); allowed(x, z) rules out greens, bunkers, etc.
export function addMappedTrees(trees, mapped, rows, { heightAt = () => 0, allowed = () => true } = {}) {
  const cell = 6, grid = new Map(), key = (a, b) => a * 73856093 ^ b * 19349663;
  const put = (t) => { const k = key(Math.floor(t.x / cell), Math.floor(t.z / cell)); if (!grid.has(k)) grid.set(k, []); grid.get(k).push(t); };
  const nearest = (x, z) => {
    let best = null, bd = Infinity;
    const gx = Math.floor(x / cell), gz = Math.floor(z / cell);
    for (let a = -1; a <= 1; a++) for (let b = -1; b <= 1; b++) {
      for (const t of grid.get(key(gx + a, gz + b)) || []) { const d = Math.hypot(t.x - x, t.z - z); if (d < bd) { bd = d; best = t; } }
    }
    return [best, bd];
  };
  for (const t of trees) put(t);
  const spots = [];
  for (const m of mapped || []) spots.push({ x: m[0], z: m[1], h: m[2] ?? null, c: m[3] ?? null, leaf: m[4] ?? null });
  for (const row of rows || []) {
    for (let i = 1; i < row.length; i++) {
      const [ax, az] = row[i - 1], [bx, bz] = row[i], len = Math.hypot(bx - ax, bz - az), n = Math.max(1, Math.round(len / 7));
      for (let k = i === 1 ? 0 : 1; k <= n; k++) spots.push({ x: ax + ((bx - ax) * k) / n, z: az + ((bz - az) * k) / n, row: true });
    }
  }
  let added = 0, matched = 0;
  for (const s of spots) {
    const [near, d] = nearest(s.x, s.z);
    if (near && d < Math.max(s.row ? 4 : 3, near.r * 0.7)) {
      matched++;
      if (s.leaf === 'n' && !near.mapped) near.kind = 'pine';
      continue;
    }
    if (!allowed(s.x, s.z)) continue;
    const n = hash(s.x, s.z, 11.3), canopy = heightAt(s.x, s.z) || 0;
    const h = canopy >= 3 ? canopy : s.h || (s.leaf === 'n' ? 12 : 10) + n * 6;
    const r = s.c ? Math.min(8, Math.max(1.2, s.c / 2)) : Math.min(6, Math.max(1.8, h * (s.leaf === 'n' ? 0.2 : 0.33)));
    const t = { x: s.x, z: s.z, h, r, kind: s.leaf === 'n' ? 'pine' : null, mapped: true };
    trees.push(t);
    put(t);
    added++;
  }
  return { added, matched };
}

// Species: narrow crowns or dark blue-green canopy in the photo read as conifers; the rest is a
// deterministic mix of broadleaf trees. photo(x, z) -> [r, g, b] or null.
export function assignSpecies(trees, photo) {
  for (const t of trees) {
    if (t.kind) continue;
    const c = photo?.(t.x, t.z);
    const lum = c ? (c[0] + c[1] + c[2]) / 3 : 100;
    const bluish = c ? c[2] > c[0] * 0.95 && lum < 80 : false;
    const n = hash(t.x, t.z);
    if (t.r / t.h < 0.16 || bluish || n < 0.07) t.kind = 'pine';
    else t.kind = n < 0.5 ? 'oak' : n < 0.82 ? 'ash' : 'aspen';
  }
  return trees;
}

// Trees without a canopy map: an edge tree has an empty side (no neighbour within 14 m in some
// eighth of the circle)
function markEdges(trees) {
  const cell = 14, grid = new Map(), key = (a, b) => a * 73856093 ^ b * 19349663;
  for (const t of trees) {
    const k = key(Math.floor(t.x / cell), Math.floor(t.z / cell));
    if (!grid.has(k)) grid.set(k, []);
    grid.get(k).push(t);
  }
  for (const t of trees) {
    if (t.edge !== undefined) continue;
    const seen = new Array(8).fill(false);
    const gx = Math.floor(t.x / cell), gz = Math.floor(t.z / cell);
    for (let a = -1; a <= 1; a++) for (let b = -1; b <= 1; b++) {
      for (const o of grid.get(key(gx + a, gz + b)) || []) {
        if (o === t) continue;
        const dx = o.x - t.x, dz = o.z - t.z, d = Math.hypot(dx, dz);
        if (d > cell) continue;
        seen[Math.floor(((Math.atan2(dz, dx) + Math.PI) / (2 * Math.PI)) * 8) % 8] = true;
      }
    }
    t.edge = seen.some((s) => !s);
  }
}

// ------------------------------------------------------------------ the variants

// kind, preset, seed, and how much to vary branching/leaves from the preset
const VARIANTS = [
  { kind: 'oak', preset: 'Oak Medium', seed: 101 }, { kind: 'oak', preset: 'Oak Medium', seed: 202 },
  { kind: 'oak', preset: 'Oak Medium', seed: 303 }, { kind: 'oak', preset: 'Oak Large', seed: 404, big: true },
  { kind: 'oak', preset: 'Oak Large', seed: 505, big: true },
  { kind: 'ash', preset: 'Ash Medium', seed: 606 }, { kind: 'ash', preset: 'Ash Medium', seed: 707 },
  { kind: 'ash', preset: 'Ash Large', seed: 808, big: true },
  { kind: 'aspen', preset: 'Aspen Medium', seed: 909, green: true }, { kind: 'aspen', preset: 'Aspen Large', seed: 1010, green: true, big: true },
  { kind: 'pine', preset: 'Pine Medium', seed: 1111 }, { kind: 'pine', preset: 'Pine Medium', seed: 1212 },
  { kind: 'pine', preset: 'Pine Large', seed: 1313, big: true }, { kind: 'pine', preset: 'Pine Large', seed: 1414, big: true },
];

function tweakOptions(o, seed, lod) {
  const r = (k) => hash(seed, k, 1.37);
  o.seed = seed;
  for (const l of Object.keys(o.branch.angle)) o.branch.angle[l] *= 0.85 + 0.3 * r(l + 1);
  for (const l of Object.keys(o.branch.children)) o.branch.children[l] = Math.max(2, o.branch.children[l] + Math.round(r(l + 9) * 2 - 1));
  o.branch.length[0] *= 0.88 + 0.24 * r(17);
  for (const l of Object.keys(o.branch.gnarliness)) o.branch.gnarliness[l] += (r(l + 23) - 0.5) * 0.1;
  o.leaves.angle += (r(31) - 0.5) * 20;
  // The presets use oversized leaves; at real tree sizes they'd be 70 cm across. Smaller, more of them.
  o.leaves.size *= 0.8;
  o.leaves.count = Math.round(o.leaves.count * 1.7 * (0.85 + 0.3 * r(37)));
  if (lod === 'mid') {
    // About a third of the triangles: fewer, larger leaf cards and fewer sides on each branch.
    // It must be the same tree as the near one (and the far pictures, baked from it), so nothing
    // that draws random numbers changes: not the branch levels or sections, which would grow every
    // limb a different way. (The leaf count only changes the very last twigs.)
    o.leaves.count = Math.max(3, Math.round(o.leaves.count * 0.3));
    o.leaves.size *= 1.75;
    for (const l of Object.keys(o.branch.segments)) o.branch.segments[l] = +l >= o.branch.levels ? 3 : Math.max(3, Math.round(o.branch.segments[l] * 0.5));
  }
}

function buildModel(v, lod) {
  const tree = new Tree();
  tree.loadPreset(v.preset);
  tweakOptions(tree.options, v.seed, lod);
  if (v.green) tree.options.bark.type = 'birch';
  // For the mid build, note where the finest twigs' bark is so it can be left out afterwards
  // (from there the leaves hide them)
  const twigs = [], L = tree.options.branch.levels;
  if (lod === 'mid' && L >= 3) {
    const grow = tree.generateBranch.bind(tree);
    tree.generateBranch = (b) => { const i0 = tree.branches.indices.length; grow(b); if (b.level >= L) twigs.push([i0, tree.branches.indices.length]); };
  }
  tree.generate();
  const bark = tree.branchesMesh.geometry, leaves = tree.leavesMesh.geometry;
  if (twigs.length) {
    const idx = bark.index.array, keep = [];
    let from = 0;
    for (const [a, b] of twigs) { for (let i = from; i < a; i++) keep.push(idx[i]); from = b; }
    for (let i = from; i < idx.length; i++) keep.push(idx[i]);
    bark.setIndex(keep);
  }
  bark.computeBoundingBox(); leaves.computeBoundingBox();
  const bb = bark.boundingBox, lb = leaves.boundingBox;
  const height = Math.max(bb.max.y, lb.max.y) - bb.min.y;
  const crown = Math.max(lb.max.x - lb.min.x, lb.max.z - lb.min.z) / 2;
  for (const g of [bark, leaves]) g.translate(0, -bb.min.y, 0);
  const p = bark.attributes.position;
  let trunk = 0;
  for (let i = 0; i < p.count; i++) if (p.getY(i) < height * 0.03) trunk = Math.max(trunk, Math.hypot(p.getX(i), p.getZ(i)));
  return {
    bark, leaves, height, crown, trunk: trunk || height * 0.02, crownBase: (lb.min.y - bb.min.y) / height,
    barkMat: tree.branchesMesh.material, leafMat: tree.leavesMesh.material,
  };
}

// ------------------------------------------------------------------ materials

// Camera-to-ball see-through: fragments near the line from the camera to the ball are dropped in
// a fine screen pattern (most of them, not all, so you still see the tree is there)
export const fade = { focus: uniform(new THREE.Vector3(0, -1e6, 0)), look: uniform(new THREE.Vector3(0, -1e6, 1)), on: uniform(0) };
const seeThroughMask = Fn(([P]) => {
  const C = cameraPosition, v = fade.focus.sub(C);
  const t = dot(P.sub(C), v).div(dot(v, v).max(1e-4));
  const tc = clamp(t, 0, 1);
  const d = length(P.sub(C.add(v.mul(tc))));
  const R = mix(float(3.2), float(1.3), tc);
  const toBall = smoothstep(R.mul(0.55), R, d).oneMinus().mul(step(t, 0.97)).mul(step(-0.02, t));
  // ...and along the view towards the target, but only near you (further trees are real obstacles)
  const dir = fade.look.sub(C).normalize(), along = dot(P.sub(C), dir);
  const d2 = length(P.sub(C.add(dir.mul(along)))), R2 = along.mul(0.22).add(1.8);
  const ahead = smoothstep(R2.mul(0.6), R2, d2).oneMinus().mul(step(0, along)).mul(step(along, length(v).add(7)));
  const amount = toBall.max(ahead).mul(fade.on).mul(0.85);
  const noise = fract(float(52.9829189).mul(fract(dot(screenCoordinate.xy, vec2(0.06711056, 0.00583715)))));
  return noise.greaterThanEqual(amount);
});

// Aspen leaf texture is autumn yellow; keep its detail but make it summer green
const greenLeaves = (c) => vec3(dot(c, vec3(0.3, 0.6, 0.1))).mul(vec3(0.55, 0.85, 0.32)).mul(1.15);

function barkMaterial(src) {
  const m = new THREE.MeshStandardNodeMaterial({ map: src.map || null, normalMap: src.normalMap || null, roughness: 0.95, metalness: 0 });
  if (src.color) m.color.copy(src.color);
  m.envMapIntensity = 0.5;
  m.maskNode = seeThroughMask(positionWorld);
  return m;
}
function leafMaterial(src, v) {
  const m = new THREE.MeshStandardNodeMaterial({
    map: src.map || null, alphaTest: Math.max(0.35, src.alphaTest || 0.5), side: THREE.DoubleSide, roughness: 0.82, metalness: 0,
  });
  m.color.setRGB(0.6, 0.68, 0.5); // shaded inside the crown, and a richer summer green
  m.envMapIntensity = 0.45;
  // (keep the picture's alpha: a vec3 colour alone would make every leaf card a solid square)
  if (v.green && src.map) { const tx = texture(src.map, uv()); m.colorNode = vec4(greenLeaves(tx.rgb).mul(vec3(0.6, 0.68, 0.5)), tx.a); }
  // Leaves sway a little, the tops most, each tree in its own rhythm
  const phase = float(instanceIndex).mul(1.618).fract().mul(6.283);
  const sway = positionLocal.y.mul(0.0035);
  m.positionNode = positionLocal.add(vec3(sin(time.mul(1.7).add(phase)), 0, cos(time.mul(1.3).add(phase.mul(1.7)))).mul(sway));
  m.maskNode = seeThroughMask(positionWorld);
  return m;
}

// ------------------------------------------------------------------ impostors

const AZ = 8, ELS = [0.09, 0.7, 1.31], FRAME = 160; // 8 directions x 3 heights (5°, 40°, 75°)
const FRAMES = AZ * ELS.length;

// Render every variant from all directions: colour into one atlas, normals into another
async function bakeImpostors(renderer, models, onProgress) {
  const V = models.length, W = FRAMES * FRAME, H = V * FRAME;
  const make = () => {
    const rt = new THREE.RenderTarget(W, H, { depthBuffer: true });
    rt.texture.generateMipmaps = true;
    rt.texture.minFilter = THREE.LinearMipmapLinearFilter;
    return rt;
  };
  const albedo = make(), normals = make();
  const scene = new THREE.Scene();
  const cam = new THREE.OrthographicCamera(-1, 1, 1, -1, 0.1, 1000);
  const prevTarget = renderer.getRenderTarget(), prevClear = renderer.getClearColor(new THREE.Color()), prevAlpha = renderer.getClearAlpha();
  const prevAuto = renderer.autoClear;
  renderer.setClearColor(0x000000, 0);
  for (const rt of [albedo, normals]) { renderer.setRenderTarget(rt); renderer.clear(); }
  renderer.autoClear = false;
  for (let vi = 0; vi < V; vi++) {
    const { near: m, v } = models[vi];
    const leafMap = m.leafMat.map;
    const albedoMats = [
      new THREE.MeshBasicNodeMaterial({ map: m.barkMat.map || null, color: m.barkMat.color }),
      new THREE.MeshBasicNodeMaterial({ map: leafMap, alphaTest: 0.5, side: THREE.DoubleSide }),
    ];
    albedoMats[1].color.setRGB(0.6, 0.68, 0.5);
    if (v.green && leafMap) { const tx = texture(leafMap, uv()); albedoMats[1].colorNode = vec4(greenLeaves(tx.rgb).mul(vec3(0.6, 0.68, 0.5)), tx.a); }
    const normalMat = (map, side) => {
      const n = new THREE.MeshBasicNodeMaterial({ side, alphaTest: map ? 0.5 : 0 });
      n.colorNode = normalView.mul(side === THREE.DoubleSide ? faceDirection : 1).mul(0.5).add(0.5);
      if (map) n.opacityNode = texture(map, uv()).a;
      return n;
    };
    const normalMats = [normalMat(null, THREE.FrontSide), normalMat(leafMap, THREE.DoubleSide)];
    const bark = new THREE.Mesh(m.bark, albedoMats[0]), leaves = new THREE.Mesh(m.leaves, albedoMats[1]);
    scene.clear(); scene.add(bark, leaves);
    const S = m.size, cy = m.height / 2;
    cam.left = -S / 2; cam.right = S / 2; cam.top = S / 2; cam.bottom = -S / 2;
    cam.updateProjectionMatrix();
    for (const [pass, rt, mats] of [['albedo', albedo, albedoMats], ['normal', normals, normalMats]]) {
      bark.material = mats[0]; leaves.material = mats[1];
      renderer.setRenderTarget(rt);
      for (let e = 0; e < ELS.length; e++) for (let a = 0; a < AZ; a++) {
        const th = (a / AZ) * Math.PI * 2, ph = ELS[e];
        cam.position.set(Math.sin(th) * Math.cos(ph) * 200, cy + Math.sin(ph) * 200, Math.cos(th) * Math.cos(ph) * 200);
        cam.lookAt(0, cy, 0);
        const fx = (e * AZ + a) * FRAME, fy = vi * FRAME;
        // (render targets carry their own viewport and scissor)
        rt.viewport.set(fx, fy, FRAME, FRAME);
        rt.scissor.set(fx, fy, FRAME, FRAME);
        rt.scissorTest = true;
        renderer.render(scene, cam);
      }
      void pass;
    }
    for (const mm of [...albedoMats, ...normalMats]) mm.dispose();
    await onProgress(vi + 1, V);
  }
  for (const rt of [albedo, normals]) { rt.scissorTest = false; rt.viewport.set(0, 0, W, H); }
  renderer.setRenderTarget(prevTarget);
  renderer.setClearColor(prevClear, prevAlpha);
  renderer.autoClear = prevAuto;
  return { albedo: albedo.texture, normals: normals.texture, V };
}

// Sun and sky for the impostors' own lighting (kept in step with the scene's lights)
export const impostorLight = {
  sunDir: uniform(new THREE.Vector3(0, 1, 0)), sun: uniform(new THREE.Color(1, 1, 1)),
  sky: uniform(new THREE.Color(0.5, 0.55, 0.6)), ground: uniform(new THREE.Color(0.25, 0.27, 0.2)),
};

function impostorMaterial(atlas, attrs) {
  const m = new THREE.SpriteNodeMaterial({ transparent: false, alphaTest: 0.5 });
  const center = instancedBufferAttribute(attrs.center), size = instancedBufferAttribute(attrs.size);
  const data = instancedBufferAttribute(attrs.data); // variant, yaw, tint r, tint g
  m.positionNode = center;
  m.scaleNode = size;
  m.colorNode = Fn(() => {
    const yaw = data.y, cs = cos(yaw.negate()), sn = sin(yaw.negate());
    const toCam = normalize(cameraPosition.sub(center));
    // Camera direction in the tree's own frame (undo its turn about the vertical)
    const local = vec3(toCam.x.mul(cs).add(toCam.z.mul(sn)), toCam.y, toCam.z.mul(cs).sub(toCam.x.mul(sn)));
    const az = atan(local.x, local.z).div(Math.PI * 2).add(1).fract().mul(AZ);
    const a0 = floor(az), a1 = a0.add(1).mod(AZ), w = az.sub(a0);
    const el = asin(clamp(local.y, -1, 1));
    const e = select(el.lessThan((ELS[0] + ELS[1]) / 2), float(0), select(el.lessThan((ELS[1] + ELS[2]) / 2), float(1), float(2)));
    // (pictures are stored top-down in the atlas)
    const frameUv = (a) => vec2(e.mul(AZ).add(a).add(uv().x).div(FRAMES), data.x.add(uv().y.oneMinus()).div(attrs.V));
    // The pictures sit on transparent black, so smaller (mipmapped) versions are darkened at the
    // edges by exactly their coverage: divide it back out
    const c = mix(texture(atlas.albedo, frameUv(a0)), texture(atlas.albedo, frameUv(a1)), w);
    const n = mix(texture(atlas.normals, frameUv(a0)), texture(atlas.normals, frameUv(a1)), w);
    const albedo = vec4(c.rgb.div(max(c.a, 0.02)), clamp(c.a.mul(1.6), 0, 1));
    const nv = n.rgb.div(max(n.a, 0.02)).mul(2).sub(1);
    // Baked normals are in the picture's camera frame; turn them into the world
    const f = local, r = normalize(cross(vec3(0, 1, 0), f).add(vec3(1e-4, 0, 0))), u = cross(f, r);
    const nl = normalize(r.mul(nv.x).add(u.mul(nv.y)).add(f.mul(nv.z)));
    const cy = cos(yaw), sy = sin(yaw);
    const nw = vec3(nl.x.mul(cy).add(nl.z.mul(sy)), nl.y, nl.z.mul(cy).sub(nl.x.mul(sy)));
    const L = impostorLight;
    // Foliage scatters light: soft wrap-around sun, plus the sky light the 3D trees get from reflections
    const diffuse = dot(nw, L.sunDir).mul(0.5).add(0.5).pow(1.5);
    const ambient = mix(vec3(L.ground), vec3(L.sky), nw.y.mul(0.5).add(0.5)).add(0.16);
    const tint = vec3(data.z, data.w, data.z.add(data.w).mul(0.45));
    const lit = albedo.rgb.mul(tint).mul(ambient.add(vec3(L.sun).mul(diffuse)));
    return vec4(lit, albedo.a);
  })();
  m.maskNode = seeThroughMask(positionWorld);
  m.fog = true;
  return m;
}

// ------------------------------------------------------------------ the forest

const LIMITS = { high: { near: 300, mid: 1400 }, balanced: { near: 200, mid: 850 }, performance: { near: 100, mid: 450 } };

export class Forest {
  constructor() {
    this.group = new THREE.Group();
    this.models = null; // per variant: { v, near, mid }
    this.atlas = null;
    this.quality = 'balanced';
    this.lastCam = null;
  }

  // Generate the variants and their impostor pictures (once per session)
  async prepare(renderer, onProgress = async () => {}) {
    if (this.models) return;
    const models = [];
    for (let i = 0; i < VARIANTS.length; i++) {
      const v = VARIANTS[i];
      const near = buildModel(v, 'near'), mid = buildModel(v, 'mid');
      near.size = Math.hypot(near.height, near.crown * 2) * 1.02;
      models.push({ v, near, mid });
      await onProgress('trees', 'Growing tree models', i + 1, VARIANTS.length);
    }
    this.atlas = await bakeImpostors(renderer, models, (d, t) => onProgress('impostors', 'Distant tree pictures', d, t));
    this.models = models;
  }

  // trees: [{ x, z, h, r, kind, edge? }]; heightAt(x, z) for the ground
  build(trees, heightAt, envApply = () => {}) {
    this.group.clear();
    markEdges(trees);
    const m = new THREE.Matrix4(), q = new THREE.Quaternion(), s = new THREE.Vector3(), up = new THREE.Vector3(0, 1, 0);
    const byVariant = this.models.map(() => []);
    for (const t of trees) {
      // Pick a variant of the right species (big presets for tall trees)
      const options = this.models.map((md, i) => i).filter((i) => this.models[i].v.kind === t.kind);
      const fit = options.filter((i) => !!this.models[i].v.big === t.h > 17);
      const pool = fit.length ? fit : options.length ? options : [0];
      t.variant = pool[Math.floor(hash(t.x, t.z, 3.1) * pool.length)];
      byVariant[t.variant].push(t);
    }
    this.trees = trees;
    const n = trees.length;
    // Impostor instance data for every tree (only the ones not drawn in 3D are sent each update)
    this.imp = {
      center: new Float32Array(n * 3), size: new Float32Array(n * 2), data: new Float32Array(n * 4),
    };
    this.variants = byVariant.map((list, vi) => {
      const { v, near, mid } = this.models[vi];
      const mats = new Float32Array(list.length * 16), tints = new Float32Array(list.length * 3);
      list.forEach((t, i) => {
        const y = heightAt(t.x, t.z) ?? 0;
        // Each tree its own height, spread, lean and turn
        const sy = t.h / near.height;
        const sxz = Math.min(sy * 1.5, Math.max(sy * 0.7, t.r / Math.max(0.1, near.crown))) * (0.92 + 0.16 * hash(t.x, t.z, 5.3));
        const sx = sxz * (0.9 + 0.2 * hash(t.x, t.z, 7.7)), sz = sxz * (0.9 + 0.2 * hash(t.x, t.z, 9.1));
        const rot = hash(t.x, t.z, 2.9) * Math.PI * 2;
        const lean = (hash(t.x, t.z, 4.4) - 0.5) * 0.06;
        q.setFromEuler(new THREE.Euler(lean, rot, lean * 0.6, 'YXZ'));
        m.compose(T(t.x, y - 0.15, t.z), q, s.set(sx, sy, sz));
        m.toArray(mats, i * 16);
        const n1 = hash(t.x, t.z, 6.1), n2 = hash(t.x, t.z, 8.3);
        // Each tree a slightly different green (the leaf texture carries the colour)
        tints[i * 3] = 0.82 + 0.3 * n1; tints[i * 3 + 1] = 0.9 + 0.18 * n2; tints[i * 3 + 2] = 0.78 + 0.2 * n2;
        t.y = y;
        t.rot = rot;
        t.k = i;
        t.tint = [tints[i * 3], tints[i * 3 + 1]];
        t.size = [near.size * Math.max(sx, sz), near.size * sy];
        t.center = [-t.x, y - 0.15 + (near.height * sy) / 2, t.z];
        // Collision shape for the ball, matching what's drawn
        t.trunkR = Math.max(0.12, near.trunk * sxz);
        const crownBase = y + near.crownBase * t.h;
        if (v.kind === 'pine') Object.assign(t, { trunkTop: crownBase + 0.6, coneBase: crownBase, coneH: t.h - near.crownBase * t.h, coneR: near.crown * sxz });
        else Object.assign(t, { trunkTop: crownBase + 0.3, cy: (crownBase + y + t.h) / 2, rh: near.crown * sxz, rv: (y + t.h - crownBase) / 2 });
      });
      const barkM = barkMaterial(near.barkMat), leafM = leafMaterial(near.leafMat, v);
      envApply(barkM); envApply(leafM);
      const make = (model, cap) => {
        const b = new THREE.InstancedMesh(model.bark, barkM, Math.max(1, cap));
        const l = new THREE.InstancedMesh(model.leaves, leafM, Math.max(1, cap));
        for (const im of [b, l]) { im.count = 0; im.castShadow = true; im.receiveShadow = true; im.frustumCulled = false; this.group.add(im); }
        l.instanceColor = new THREE.InstancedBufferAttribute(new Float32Array(Math.max(1, cap) * 3), 3);
        return { bark: b, leaves: l };
      };
      const lim = LIMITS[this.quality] || LIMITS.balanced;
      return { list, mats, tints, near: make(near, Math.min(list.length, lim.near)), mid: make(mid, Math.min(list.length, lim.mid)) };
    });
    trees.forEach((t, i) => {
      this.imp.center.set(t.center, i * 3);
      this.imp.size.set(t.size, i * 2);
      this.imp.data.set([t.variant, t.rot, t.tint[0], t.tint[1]], i * 4);
    });
    // One draw call for every distant tree
    const cap = Math.max(1, n);
    const attrs = {
      center: new THREE.InstancedBufferAttribute(new Float32Array(cap * 3), 3),
      size: new THREE.InstancedBufferAttribute(new Float32Array(cap * 2), 2),
      data: new THREE.InstancedBufferAttribute(new Float32Array(cap * 4), 4),
      V: this.atlas.V,
    };
    for (const a of [attrs.center, attrs.size, attrs.data]) a.setUsage(THREE.DynamicDrawUsage);
    this.impAttrs = attrs;
    this.impostors = new THREE.Sprite(impostorMaterial(this.atlas, attrs));
    this.impostors.count = 0;
    this.impostors.frustumCulled = false;
    this.group.add(this.impostors);
    this.lastCam = null;
    return trees;
  }

  // Decide which trees are drawn in full, which lighter, which as pictures. Edge trees count as
  // much closer than they are (they're what you see); trees behind the camera as further away.
  update(camera, force = false) {
    if (!this.trees) return;
    const cam = camera.position, dir = camera.getWorldDirection(new THREE.Vector3());
    const now = performance.now();
    if (!force && this.lastCam) {
      const moved = Math.hypot(cam.x - this.lastCam.x, cam.z - this.lastCam.z);
      const turned = dir.dot(this.lastCam.dir) < 0.985;
      if ((moved < 8 && !turned) || now - this.lastCam.t < 150) return;
    }
    this.lastCam = { x: cam.x, z: cam.z, dir: dir.clone(), t: now };
    const lim = LIMITS[this.quality] || LIMITS.balanced;
    const hx = dir.x, hz = dir.z, hl = Math.hypot(hx, hz) || 1;
    // Score: effective distance; small is important
    const scored = [];
    for (let i = 0; i < this.trees.length; i++) {
      const t = this.trees[i];
      const dx = t.center[0] - cam.x, dz = t.center[2] - cam.z, d = Math.hypot(dx, dz);
      const facing = d > 1 ? (dx * hx + dz * hz) / (d * hl) : 1;
      const e = d * (t.edge ? 0.45 : 1) * (facing < 0.2 ? 3 : 1);
      if (e < 130) scored.push([e, i]);
    }
    scored.sort((a, b) => a[0] - b[0]);
    const lod = new Uint8Array(this.trees.length); // 0 impostor, 1 mid, 2 near
    let nNear = 0, nMid = 0;
    for (const V of this.variants) { V.nearUsed = 0; V.midUsed = 0; }
    for (const [e, i] of scored) {
      const vi = this.trees[i].variant, V = this.variants[vi];
      if (e < 45 && nNear < lim.near && V.nearUsed < V.near.bark.instanceMatrix.count) { lod[i] = 2; nNear++; V.nearUsed++; }
      else if (nMid < lim.mid && V.midUsed < V.mid.bark.instanceMatrix.count) { lod[i] = 1; nMid++; V.midUsed++; }
    }
    // Write the instances
    for (const V of this.variants) { V.cn = 0; V.cm = 0; }
    const ia = this.impAttrs;
    let ni = 0;
    for (let i = 0; i < this.trees.length; i++) {
      const t = this.trees[i], V = this.variants[t.variant], k = t.k;
      const src = V.mats.subarray(k * 16, k * 16 + 16), tint = V.tints.subarray(k * 3, k * 3 + 3);
      if (lod[i] === 2) { const n = V.cn++; V.near.bark.instanceMatrix.array.set(src, n * 16); V.near.leaves.instanceMatrix.array.set(src, n * 16); V.near.leaves.instanceColor.array.set(tint, n * 3); }
      else if (lod[i] === 1) { const n = V.cm++; V.mid.bark.instanceMatrix.array.set(src, n * 16); V.mid.leaves.instanceMatrix.array.set(src, n * 16); V.mid.leaves.instanceColor.array.set(tint, n * 3); }
      else {
        ia.center.array.set(this.imp.center.subarray(i * 3, i * 3 + 3), ni * 3);
        ia.size.array.set(this.imp.size.subarray(i * 2, i * 2 + 2), ni * 2);
        ia.data.array.set(this.imp.data.subarray(i * 4, i * 4 + 4), ni * 4);
        ni++;
      }
    }
    for (const V of this.variants) {
      for (const [g, c] of [[V.near, V.cn], [V.mid, V.cm]]) {
        g.bark.count = c; g.leaves.count = c;
        g.bark.instanceMatrix.needsUpdate = true; g.leaves.instanceMatrix.needsUpdate = true; g.leaves.instanceColor.needsUpdate = true;
      }
    }
    this.impostors.count = ni;
    ia.center.needsUpdate = true; ia.size.needsUpdate = true; ia.data.needsUpdate = true;
    this.counts = { near: nNear, mid: nMid, impostor: ni };
  }

  triangles() {
    let n = 0;
    for (const V of this.variants || []) for (const g of [V.near, V.mid]) {
      for (const im of [g.bark, g.leaves]) n += ((im.geometry.index?.count ?? im.geometry.attributes.position.count) / 3) * im.count;
    }
    return n + (this.impostors?.count || 0) * 2;
  }
}
