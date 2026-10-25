// Trees: where they stand (from tree-height maps, or the aerial photo as a fallback) and how
// they're drawn (procedural trees from ez-tree with bark and leaf textures: its own, plus ours for
// the species it has none for).
//
// Every tree is one of the variants (different species, presets, seeds and branching), turned,
// stretched and tinted on its own so no two look alike. Each variant is drawn three ways:
//   near     - the full tree (leaves, branches, bark) close to the camera
//   mid      - a lighter build of the same tree further out (same trunk and limbs)
//   impostor - pictures of the full tree, taken from 24 directions when the course loads, shown
//              on a single card that always faces you (blended between the nearest pictures),
//              lit by the sun using baked normals, like the 3D leaves
// Trees on the edge of woods (the ones you actually see) stay detailed much further out than
// trees inside a wood, and trees behind the camera never take the detailed slots.
// Trees between the camera and the ball fade to a see-through stipple.
import * as THREE from 'three/webgpu';
import {
  Fn, uniform, vec2, vec3, vec4, float, uv, positionWorld, positionLocal, cameraPosition, clamp, dot, length, mix,
  smoothstep, step, fract, floor, screenCoordinate, sin, cos, atan, asin, normalize, cross, max, texture,
  time, instanceIndex, instancedBufferAttribute, normalView, attribute, normalViewGeometry, pow, positionGeometry, log2, fwidth, cameraViewMatrix,
} from 'three/tsl';
import { grassWind, SHARED_INSTANCES } from './grass.js';
import { Tree } from 'ez-tree';
import { mergeGeometries } from 'three/addons/utils/BufferGeometryUtils.js';
import { hash, fract1 } from './treeplace.js';
import { buildSugarMaple } from './sugarmaple.js';
import { buildPalm } from './palm.js';
import { buildGrown } from './grow.js';
// Where trees stand (shared with the course editor, which has no 3D)
export { treesFromCanopy, addMappedTrees, treesInArea, addTreeAreas, assignSpecies } from './treeplace.js';

const T = (x, y, z) => new THREE.Vector3(-x, y, z);
// Shrubs inside woods are drawn out to about this far from the camera (see buildShrubs)
const SHRUB_NEAR = 70;

// ------------------------------------------------------------------ where trees stand

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

// The species beyond ez-tree's own presets, shaped from the nearest one (applied after the per-seed
// variation, the same for the near and mid builds so both are the same tree). tint: the variant's
// leaf colour relative to the others.
const SHAPES = {
  // Maple: a dense, rounded crown on a shorter trunk; more, slightly larger leaves
  maple(o) {
    o.branch.length[0] *= 0.8;
    for (const l of [1, 2]) o.branch.angle[l] *= 0.85;
    o.branch.children[1] += 1;
    o.leaves.count = Math.round(o.leaves.count * 1.35); o.leaves.size *= 1.1;
  },
  // Birch: slender, white-barked, light airy crown with slightly hanging twigs
  birch(o) {
    o.bark.type = 'birch';
    for (const l of Object.keys(o.branch.radius)) o.branch.radius[l] *= 0.6;
    o.branch.force = { direction: { x: 0, y: -1, z: 0 }, strength: 0.012 };
    o.leaves.size *= 0.75; o.leaves.count = Math.round(o.leaves.count * 1.2); // (birch leaves: see OWN)
  },
  // Weeping willow: long limbs whose twigs pour down towards the ground
  willow(o) {
    o.bark.type = 'willow';
    o.branch.force = { direction: { x: 0, y: -1, z: 0 }, strength: 0.09 };
    o.branch.length[2] *= 1.6; if (o.branch.length[3] != null) o.branch.length[3] *= 2.2;
    for (const l of Object.keys(o.branch.gnarliness)) o.branch.gnarliness[l] *= 0.5;
    o.leaves.count = Math.round(o.leaves.count * 2.4); // (willow leaves: see OWN) o.leaves.size *= 0.85; o.leaves.angle = 5; o.leaves.start = 0;
  },
  // Lombardy poplar: a tall, narrow column, branches swept up along the trunk
  poplar(o) {
    o.bark.type = 'oak'; // (dark, furrowed - not the aspen preset's white)
    o.branch.force = { direction: { x: 0, y: 1, z: 0 }, strength: 0.08 };
    for (const l of Object.keys(o.branch.angle)) o.branch.angle[l] = Math.min(o.branch.angle[l], 28);
    for (const l of [1, 2]) if (o.branch.length[l]) o.branch.length[l] *= 0.55;
    o.branch.start[1] = 0.15;
    o.leaves.count = Math.round(o.leaves.count * 1.3);
  },
  // Spruce: a full, dark cone of needles from near the ground, shorter side branches
  spruce(o) {
    o.branch.start[1] = 0.12;
    o.branch.children[0] = Math.round(o.branch.children[0] * 1.35);
    if (o.branch.length[1]) o.branch.length[1] *= 0.75;
    o.leaves.count = Math.round(o.leaves.count * 1.8); o.leaves.size *= 1.1;
  },
};

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
  { kind: 'maple', grow: 'sugar-maple', seed: 1515, tint: [0.95, 1.02, 0.85] }, { kind: 'maple', grow: 'sugar-maple', seed: 1021, tint: [0.95, 1.02, 0.85] },
  { kind: 'maple', grow: 'sugar-maple', seed: 1616, big: true, tint: [0.95, 1.02, 0.85] },
  { kind: 'birch', preset: 'Ash Medium', seed: 1717, tint: [1.08, 1.06, 0.95] }, { kind: 'birch', preset: 'Ash Medium', seed: 1818, tint: [1.08, 1.06, 0.95] },
  { kind: 'willow', preset: 'Ash Medium', seed: 1919, tint: [1.05, 1.08, 0.8] }, { kind: 'willow', preset: 'Ash Large', seed: 2020, big: true, tint: [1.05, 1.08, 0.8] },
  { kind: 'poplar', preset: 'Aspen Medium', seed: 2121, green: true }, { kind: 'poplar', preset: 'Aspen Large', seed: 2222, green: true, big: true },
  { kind: 'spruce', preset: 'Pine Medium', seed: 2323, tint: [0.72, 0.85, 0.92] }, { kind: 'spruce', preset: 'Pine Large', seed: 2424, big: true, tint: [0.72, 0.85, 0.92] },
  // Palms (see palm.js): cabbage, date and coconut palms for medium heights, Mexican fan palms for tall ones
  { kind: 'palm', grow: 'palm', palm: 'sabal', seed: 2525 }, { kind: 'palm', grow: 'palm', palm: 'date', seed: 2626 },
  { kind: 'palm', grow: 'palm', palm: 'coconut', seed: 2727 }, { kind: 'palm', grow: 'palm', palm: 'washingtonia', seed: 2828, big: true },
  { kind: 'palm', grow: 'palm', palm: 'washingtonia', seed: 2929, big: true },
  // Grown by grow.js, with true-colour leaf pictures (raw)
  { kind: 'liveoak', grow: 'grow', seed: 3101, raw: true }, { kind: 'liveoak', grow: 'grow', seed: 3102, raw: true }, { kind: 'liveoak', grow: 'grow', seed: 3103, raw: true, big: true },
  { kind: 'cypress', grow: 'grow', seed: 3201, raw: true }, { kind: 'cypress', grow: 'grow', seed: 3202, raw: true },
  { kind: 'baldcypress', grow: 'grow', seed: 3301, raw: true }, { kind: 'baldcypress', grow: 'grow', seed: 3302, raw: true, big: true },
  { kind: 'mesquite', grow: 'grow', seed: 3401, raw: true }, { kind: 'mesquite', grow: 'grow', seed: 3402, raw: true },
  { kind: 'paloverde', grow: 'grow', seed: 3501, raw: true }, { kind: 'paloverde', grow: 'grow', seed: 3502, raw: true },
  { kind: 'saguaro', grow: 'grow', seed: 3601, raw: true }, { kind: 'saguaro', grow: 'grow', seed: 3602, raw: true },
  { kind: 'sycamore', grow: 'grow', seed: 3701, raw: true }, { kind: 'sycamore', grow: 'grow', seed: 3702, raw: true, big: true },
  { kind: 'magnolia', grow: 'grow', seed: 3801, raw: true }, { kind: 'magnolia', grow: 'grow', seed: 3802, raw: true },
  { kind: 'redmaple', grow: 'grow', seed: 3901, raw: true }, { kind: 'redmaple', grow: 'grow', seed: 3902, raw: true }, { kind: 'redmaple', grow: 'grow', seed: 3903, raw: true, big: true },
  { kind: 'tuliptree', grow: 'grow', seed: 4001, raw: true }, { kind: 'tuliptree', grow: 'grow', seed: 4002, raw: true, big: true },
  { kind: 'loblolly', grow: 'grow', seed: 4101, raw: true }, { kind: 'loblolly', grow: 'grow', seed: 4102, raw: true }, { kind: 'loblolly', grow: 'grow', seed: 4103, raw: true, big: true },
  { kind: 'redcedar', grow: 'grow', seed: 4201, raw: true }, { kind: 'redcedar', grow: 'grow', seed: 4202, raw: true, big: true },
  { kind: 'douglasfir', grow: 'grow', seed: 4301, raw: true }, { kind: 'douglasfir', grow: 'grow', seed: 4302, raw: true }, { kind: 'douglasfir', grow: 'grow', seed: 4303, raw: true, big: true },
  { kind: 'ponderosa', grow: 'grow', seed: 4401, raw: true }, { kind: 'ponderosa', grow: 'grow', seed: 4402, raw: true }, { kind: 'ponderosa', grow: 'grow', seed: 4403, raw: true, big: true },
  { kind: 'beech', grow: 'grow', seed: 4501, raw: true }, { kind: 'beech', grow: 'grow', seed: 4502, raw: true, big: true },
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

// Our own pictures for species ez-tree has none of (see NOTICE.md): leafy twigs for maple,
// willow and birch (made by scripts/leaf-twigs.py from scanned leaves), bark for ash, maple,
// poplar, pine and spruce, palm fronds and trunks (drawn by scripts/palm-textures.py), and the
// leaves and bark of the species grown by grow.js (drawn by scripts/tree-textures.py). The
// other species keep ez-tree's own.
const GROWN = ['liveoak', 'cypress', 'baldcypress', 'mesquite', 'paloverde', 'saguaro', 'sycamore', 'magnolia', 'redmaple', 'tuliptree', 'loblolly', 'redcedar', 'douglasfir', 'ponderosa', 'beech'];
const OWN = { leaves: ['maple', 'willow', 'birch', 'palm', ...GROWN], bark: ['ash', 'maple', 'poplar', 'pine', 'spruce', 'palm', ...GROWN] };
let own = null;
async function loadOwnTextures() {
  if (own) return;
  const loader = new THREE.TextureLoader();
  const load = async (file, srgb) => {
    const t = await loader.loadAsync(`/textures/trees/${file}`);
    if (srgb) t.colorSpace = THREE.SRGBColorSpace;
    return t;
  };
  const out = { leaves: {}, bark: {} };
  await Promise.all([
    ...OWN.leaves.map(async (k) => { const t = await load(`${k}_leaves.png`, true); t.premultiplyAlpha = true; out.leaves[k] = t; }),
    ...OWN.bark.map(async (k) => { out.bark[k] = { map: await load(`${k}_bark_color.jpg`, true), normal: await load(`${k}_bark_normal.jpg`, false) }; }),
  ]);
  own = out;
}

function buildModel(v, lod) {
  if (v.grow === 'sugar-maple') {
    // Our own sugar maple (see sugarmaple.js): bark uv already tiled, so repeat 1
    const { bark, leaves } = buildSugarMaple(v.seed, lod);
    return finishModel(v, bark, leaves, { color: new THREE.Color(1, 1, 1) }, { alphaTest: 0.5, color: new THREE.Color(1, 1, 1) }, { x: 1, y: 1 });
  }
  if (v.grow === 'grow') {
    // Species grown by grow.js; it knows its own trunk width (see there)
    const { bark, leaves, trunk, tint } = buildGrown(v.kind, v.seed, lod);
    const m = finishModel(v, bark, leaves, { color: new THREE.Color(tint, tint, tint) }, { alphaTest: 0.5, color: new THREE.Color(1, 1, 1) }, { x: 1, y: 1 });
    m.trunk = trunk;
    return m;
  }
  if (v.grow === 'palm') {
    // Our own palms (see palm.js): bark and frond uv point into the palm pictures as they are
    const { bark, leaves } = buildPalm(v.palm, v.seed, lod);
    return finishModel(v, bark, leaves, { color: new THREE.Color(1, 1, 1) }, { alphaTest: 0.5, color: new THREE.Color(1, 1, 1) }, { x: 1, y: 1 });
  }
  const tree = new Tree();
  tree.loadPreset(v.preset);
  tweakOptions(tree.options, v.seed, lod);
  if (v.green) tree.options.bark.type = 'birch';
  SHAPES[v.kind]?.(tree.options);
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
  return finishModel(v, bark, leaves, tree.branchesMesh.material, tree.leavesMesh.material, tree.options.bark.textureScale);
}

// Everything after the geometry, the same for every model: crown lighting, size, trunk width,
// and our own bark and leaf pictures. barkScale: how the bark picture is tiled ({ x, y }).
function finishModel(v, bark, leaves, barkMat, leafMat, barkScale) {
  shapeCrown(leaves);
  bark.computeBoundingBox(); leaves.computeBoundingBox();
  const bb = bark.boundingBox, lb = leaves.boundingBox;
  const height = Math.max(bb.max.y, lb.max.y) - bb.min.y;
  const crown = Math.max(lb.max.x - lb.min.x, lb.max.z - lb.min.z) / 2;
  for (const g of [bark, leaves]) g.translate(0, -bb.min.y, 0);
  const p = bark.attributes.position;
  let trunk = 0;
  for (let i = 0; i < p.count; i++) if (p.getY(i) < height * 0.03) trunk = Math.max(trunk, Math.hypot(p.getX(i), p.getZ(i)));
  const ownBark = own?.bark[v.kind], ownLeaves = own?.leaves[v.kind];
  if (ownBark) {
    // (tiled as ez-tree tiles its own bark for this tree)
    const sc = barkScale;
    const tile = (t) => { const c = t.clone(); c.wrapS = c.wrapT = THREE.RepeatWrapping; c.repeat.set(sc.x, 1 / sc.y); c.needsUpdate = true; return c; };
    barkMat = { map: tile(ownBark.map), normalMap: tile(ownBark.normal), color: barkMat.color };
  }
  if (ownLeaves) leafMat = { map: ownLeaves, alphaTest: leafMat.alphaTest, color: leafMat.color };
  return {
    bark, leaves, height, crown, trunk: trunk || height * 0.02, crownBase: (lb.min.y - bb.min.y) / height, barkMat, leafMat,
  };
}

// Leaves lit as one crown rather than thousands of loose cards: each leaf's normal is bent most of
// the way towards pointing out of the crown, so a tree shades softly from its sunny side round to
// its shady side as a real canopy does (instead of every card catching the sun on its own, which
// looks speckled and flat). Leaves deep inside the crown or low in it are shaded by the rest:
// leafAo, 1 on the outside at the top down to 0.4 in the heart of the tree.
const smooth = (a, b, x) => { const t = Math.min(1, Math.max(0, (x - a) / (b - a))); return t * t * (3 - 2 * t); };
function shapeCrown(g) {
  if (!g.attributes.normal) g.computeVertexNormals();
  g.computeBoundingBox();
  const b = g.boundingBox, c = b.getCenter(new THREE.Vector3()), r = b.getSize(new THREE.Vector3()).multiplyScalar(0.5);
  const p = g.attributes.position, n = g.attributes.normal, ao = new Float32Array(p.count);
  const out = new THREE.Vector3(), nn = new THREE.Vector3();
  for (let i = 0; i < p.count; i++) {
    out.set((p.getX(i) - c.x) / Math.max(0.1, r.x), (p.getY(i) - c.y) / Math.max(0.1, r.y), (p.getZ(i) - c.z) / Math.max(0.1, r.z));
    const depth = Math.min(1, out.length());
    out.normalize();
    nn.set(n.getX(i), n.getY(i), n.getZ(i));
    if (nn.dot(out) < 0) nn.negate();
    nn.lerp(out, 0.7).normalize();
    n.setXYZ(i, nn.x, nn.y, nn.z);
    const up = (p.getY(i) - b.min.y) / Math.max(0.1, b.max.y - b.min.y);
    ao[i] = (0.5 + 0.5 * smooth(0.25, 1, depth)) * (0.8 + 0.2 * up);
  }
  n.needsUpdate = true;
  g.setAttribute('leafAo', new THREE.BufferAttribute(ao, 1));
}

// A shrub about 1 unit across and 1 tall: leaf cards over a low dome, facing outwards, lit as one
// crown (see shapeCrown)
function shrubGeometry(cards = 28) {
  const parts = [], o = new THREE.Object3D();
  for (let i = 0; i < cards; i++) {
    const az = (i / cards) * Math.PI * 2 + Math.random() * 0.6, el = Math.random() ** 0.8 * 1.2;
    const r = 0.25 + Math.random() * 0.2;
    const cx = Math.cos(el) * Math.cos(az) * r, cy = 0.2 + Math.sin(el) * 0.5, cz = Math.cos(el) * Math.sin(az) * r;
    const size = 0.6 + Math.random() * 0.35;
    const g = new THREE.PlaneGeometry(size, size);
    o.position.set(cx, cy, cz);
    o.lookAt(cx * 3, cy * 1.6 + 0.2, cz * 3);
    o.rotateZ(Math.random() * Math.PI * 2);
    o.updateMatrix();
    g.applyMatrix4(o.matrix);
    parts.push(g);
  }
  const g = mergeGeometries(parts);
  shapeCrown(g);
  return g;
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
// A leaf's colour: its picture (summer green for aspens), a richer green (not for raw pictures,
// drawn in their true colours: moss, flowers and fruit must stay grey, white and red), shaded
// inside the crown
const leafColour = (map, v) => {
  const tx = texture(map, uv()), leafAo = attribute('leafAo', 'float');
  return vec4((v.green ? greenLeaves(tx.rgb) : tx.rgb).mul(v.raw ? vec3(1, 1, 1) : vec3(0.6, 0.68, 0.5)).mul(leafAo), tx.a);
};
// Sunlight through the leaves: looking towards the sun, a crown glows a warm yellow-green at its
// thin edges and gaps, and a little light always comes through. viewDir: from the camera.
const LEAF_GLOW = vec3(1.05, 1.2, 0.55);
const leafGlow = (albedo, viewDir) => albedo.mul(vec3(impostorLight.sun)).mul(LEAF_GLOW)
  .mul(pow(clamp(dot(viewDir, impostorLight.sunDir), 0, 1), 4).mul(0.75).add(0.05));

// The whole tree sways as one, its trunk, branches and leaves together (each tree in its own
// rhythm; bark and leaves of a tree share an instance number): not at all at the foot, more and
// more up the tree, by height up the model itself (not in the scene, where the height above the
// lowest point of the course would add to it), and further in wind
const treeSway = () => {
  const phase = float(instanceIndex).mul(1.618).fract().mul(6.283);
  const up = positionGeometry.y.max(0);
  // (models are about 60 units tall: the top moves about 15 cm in still air)
  const amount = up.mul(up).mul(0.00004).mul(grassWind.mul(0.12).add(1).min(3));
  return positionLocal.add(vec3(sin(time.mul(1.1).add(phase)), 0, cos(time.mul(0.9).add(phase.mul(1.7)))).mul(amount));
};

function barkMaterial(src) {
  const m = new THREE.MeshStandardNodeMaterial({ map: src.map || null, normalMap: src.normalMap || null, roughness: 0.95, metalness: 0 });
  if (src.color) m.color.copy(src.color);
  m.envMapIntensity = 0.5;
  m.positionNode = treeSway();
  m.maskNode = seeThroughMask(positionWorld);
  return m;
}
function leafMaterial(src, v) {
  // (leaves barely mirror the sky: with the crown-shaped normals, a normal sheen turned the crowns
  // pale blue-white wherever they faced away from you)
  const m = new THREE.MeshPhysicalNodeMaterial({
    map: src.map || null, alphaTest: Math.max(0.35, src.alphaTest || 0.5), side: THREE.DoubleSide, roughness: 0.82, metalness: 0,
    specularIntensity: 0.12,
  });
  m.color.setRGB(0.6, 0.68, 0.5); // a richer summer green
  m.envMapIntensity = 0.45;
  // (keep the picture's alpha: a vec3 colour alone would make every leaf card a solid square)
  if (src.map) {
    const c = leafColour(src.map, v);
    m.colorNode = c;
    m.emissiveNode = leafGlow(c.rgb, normalize(positionWorld.sub(cameraPosition)));
  }
  // The crown-shaped normals (see shapeCrown), on both sides of each card: the back of a leaf
  // faces the same way out of the crown as its front
  m.normalNode = normalViewGeometry;
  // Further off, the leaf picture is read from its smaller, blurred copies, where the see-through
  // gaps average the alpha down below the cut-off, so whole leaves dropped out and trees looked
  // bare between ~50 and 130 m (before the distant tree pictures take over). Boosting the alpha
  // with distance keeps each leaf about as full as it is up close.
  m.opacityNode = mix(float(1), float(2.6), smoothstep(12, 110, length(positionWorld.sub(cameraPosition))));
  // (the leaves move with their branches: see treeSway)
  m.positionNode = treeSway();
  m.maskNode = seeThroughMask(positionWorld);
  return m;
}

// ------------------------------------------------------------------ impostors

const AZ = 8, ELS = [0.09, 0.7, 1.31], FRAME = 160; // 8 directions x 3 heights (5°, 40°, 75°)
const FRAMES = AZ * ELS.length;
// Variants side by side in the atlas, so its height stays under the 8192-pixel limit on textures
// (46 variants: 23 rows of 2)
const COLS = 2;
// How much the leaves' coverage is raised when they're baked into the pictures
const LEAF_BAKE_BOOST = 3;

// Render every variant from all directions: colour into one atlas, normals into another
async function bakeImpostors(renderer, models, onProgress) {
  const V = models.length, ROWS = Math.ceil(V / COLS), W = COLS * FRAMES * FRAME, H = ROWS * FRAME;
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
      new THREE.MeshBasicNodeMaterial({ map: leafMap, alphaTest: 0.35, side: THREE.DoubleSide }),
    ];
    albedoMats[1].color.setRGB(0.6, 0.68, 0.5);
    if (leafMap) albedoMats[1].colorNode = leafColour(leafMap, v);
    // (each tree is only 160 pixels across here, so the leaf pictures are read from their smallest,
    // most see-through copies: without a boost most leaves fall under the cut-off and the distant
    // pictures were bare branches, so trees changed from green to brown as they passed into the
    // distance - the same boost the 3D leaves get far off, see leafMaterial)
    albedoMats[1].opacityNode = float(LEAF_BAKE_BOOST);
    // (leaves: their crown-shaped normals, the same on both sides of a card, as the 3D trees use)
    const normalMat = (map, side) => {
      const n = new THREE.MeshBasicNodeMaterial({ side, alphaTest: map ? 0.35 : 0 });
      n.colorNode = (map ? normalViewGeometry : normalView).mul(0.5).add(0.5);
      if (map) n.opacityNode = texture(map, uv()).a.mul(LEAF_BAKE_BOOST);
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
        const fx = ((vi % COLS) * FRAMES + e * AZ + a) * FRAME, fy = Math.floor(vi / COLS) * FRAME;
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
  return { albedo: albedo.texture, normals: normals.texture, V, rows: ROWS };
}

// Sun and sky for the impostors' own lighting (kept in step with the scene's lights)
export const impostorLight = {
  sunDir: uniform(new THREE.Vector3(0, 1, 0)), sun: uniform(new THREE.Color(1, 1, 1)),
  sky: uniform(new THREE.Color(0.5, 0.55, 0.6)), ground: uniform(new THREE.Color(0.25, 0.27, 0.2)),
};

// The cards for distant trees: a sprite (always facing the camera) whose surface is lit like the
// 3D leaves - same material, sun, sky and reflections, same glow - so a tree looks the same on
// either side of where it changes from one to the other. (They used to light themselves with a
// simpler model, which came out far darker than the 3D trees: as you moved, trees passing in and
// out of the distance changed colour and seemed to blink.)
class LitSpriteMaterial extends THREE.MeshPhysicalNodeMaterial {
  constructor(params) {
    super(params);
    this.isSpriteNodeMaterial = true;
    this._useSizeAttenuation = true;
    this.rotationNode = null;
    this.scaleNode = null;
    this.rotation = 0;
  }
  get sizeAttenuation() { return this._useSizeAttenuation; }
  setupPositionView(builder) { return THREE.SpriteNodeMaterial.prototype.setupPositionView.call(this, builder); }
}

function impostorMaterial(atlas, attrs) {
  // (alpha to coverage: the edges of the pictures are smoothed by the multisampling instead of
  // cut hard, so they don't sparkle as you move)
  const m = new LitSpriteMaterial({ transparent: false, alphaTest: 0.5, alphaToCoverage: true, roughness: 0.82, metalness: 0, specularIntensity: 0.12 });
  m.envMapIntensity = 0.45;
  const center = instancedBufferAttribute(attrs.center), size = instancedBufferAttribute(attrs.size);
  const data = instancedBufferAttribute(attrs.data); // variant, yaw, tint r, tint g
  m.positionNode = center;
  m.scaleNode = size;
  const yaw = data.y;
  const toCam = normalize(cameraPosition.sub(center));
  // Camera direction in the tree's own frame (undo its turn about the vertical)
  const cs = cos(yaw.negate()), sn = sin(yaw.negate());
  const local = vec3(toCam.x.mul(cs).add(toCam.z.mul(sn)), toCam.y, toCam.z.mul(cs).sub(toCam.x.mul(sn)));
  const az = atan(local.x, local.z).div(Math.PI * 2).add(1).fract().mul(AZ);
  // (wrapping round from the last direction to the first without a GPU modulo, inexact as above)
  const a0 = floor(az).min(AZ - 1), a1 = a0.greaterThan(AZ - 1.5).select(float(0), a0.add(1)), w = az.sub(a0);
  // Between the two nearest directions round the tree and the two nearest heights, blended, so
  // the picture changes smoothly as you move instead of jumping (the heights are evenly spaced)
  const fe = clamp(asin(clamp(local.y, -1, 1)).sub(ELS[0]).div(ELS[1] - ELS[0]), 0, ELS.length - 1.001);
  const e0 = floor(fe), e1 = e0.add(1).min(ELS.length - 1), we = fe.sub(e0);
  // (pictures are stored top-down in the atlas.) The row and column of the variant's pictures,
  // worked out with half a step of margin: the GPU's division is not exact (4 / 2 can come out as
  // 1.9999999), and rounding that down picked an empty part of the atlas, so some variants' distant
  // trees vanished - and came back when you moved close enough for the 3D tree
  const row = floor(data.x.add(0.5).div(COLS)), col = floor(data.x.sub(row.mul(COLS)).add(0.5));
  const frameUv = (a, e) => vec2(col.mul(FRAMES).add(e.mul(AZ)).add(a).add(uv().x).div(FRAMES * COLS), row.add(uv().y.oneMinus()).div(attrs.rows));
  const pick = (tex) => mix(
    mix(texture(tex, frameUv(a0, e0)), texture(tex, frameUv(a1, e0)), w),
    mix(texture(tex, frameUv(a0, e1)), texture(tex, frameUv(a1, e1)), w), we);
  // The pictures sit on transparent black, so smaller (mipmapped) versions are darkened at the
  // edges by exactly their coverage: divide it back out
  const c = pick(atlas.albedo), n = pick(atlas.normals);
  // ...and the further off (the smaller the version of the picture drawn), the more the thin
  // parts - fronds, twigs, airy crowns - blur below the cut-off and the tree would blink in and
  // out as you move: the coverage is raised with every halving of the picture's size, so a tree
  // keeps about the solidity it has up close
  const lod = max(log2(fwidth(uv().x).mul(FRAME).max(1e-4)), 0);
  const albedo = vec4(c.rgb.div(max(c.a, 0.02)), clamp(c.a.mul(1.6).mul(lod.mul(0.45).add(1)), 0, 1));
  const nv = n.rgb.div(max(n.a, 0.02)).mul(2).sub(1);
  // Baked normals are in the picture's camera frame; turn them into the world, then the view
  const f = local, r = normalize(cross(vec3(0, 1, 0), f).add(vec3(1e-4, 0, 0))), u = cross(f, r);
  const nl = normalize(r.mul(nv.x).add(u.mul(nv.y)).add(f.mul(nv.z)));
  const cy = cos(yaw), sy = sin(yaw);
  const nw = vec3(nl.x.mul(cy).add(nl.z.mul(sy)), nl.y, nl.z.mul(cy).sub(nl.x.mul(sy)));
  m.normalNode = normalize(cameraViewMatrix.mul(vec4(nw, 0)).xyz);
  const tint = vec3(data.z, data.w, data.z.add(data.w).mul(0.45));
  m.colorNode = vec4(albedo.rgb.mul(tint), albedo.a);
  m.emissiveNode = leafGlow(albedo.rgb.mul(tint), toCam.negate());
  m.maskNode = seeThroughMask(positionWorld);
  m.fog = true;
  return m;
}

// ------------------------------------------------------------------ the forest

// near/mid: how many trees at most; nearR/midR: out to what effective distance (m) they're drawn
// that way (beyond midR, the pictures)
const LIMITS = { high: { near: 180, mid: 800, nearR: 42, midR: 100 }, balanced: { near: 140, mid: 560, nearR: 40, midR: 90 }, performance: { near: 60, mid: 260, nearR: 35, midR: 75 } };

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
    await loadOwnTextures();
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
        // Each tree a slightly different green (the leaf texture carries the colour), and each
        // species its own shade (darker blue-green spruces, yellower willows)
        const vt = v.tint || [1, 1, 1];
        tints[i * 3] = (0.82 + 0.3 * n1) * vt[0]; tints[i * 3 + 1] = (0.9 + 0.18 * n2) * vt[1]; tints[i * 3 + 2] = (0.78 + 0.2 * n2) * vt[2];
        t.y = y;
        t.rot = rot;
        t.k = i;
        t.tint = [tints[i * 3], tints[i * 3 + 1]];
        t.size = [near.size * Math.max(sx, sz), near.size * sy];
        t.center = [-t.x, y - 0.15 + (near.height * sy) / 2, t.z];
        // Collision shape for the ball, matching what's drawn
        t.trunkR = Math.max(0.12, near.trunk * sxz);
        const crownBase = y + near.crownBase * t.h;
      if (v.kind === 'pine' || v.kind === 'loblolly' || v.kind === 'redcedar' || v.kind === 'douglasfir' || v.kind === 'ponderosa' || v.kind === 'spruce' || v.kind === 'baldcypress') Object.assign(t, { trunkTop: crownBase + 0.6, coneBase: crownBase, coneH: t.h - near.crownBase * t.h, coneR: near.crown * sxz });
        else Object.assign(t, { trunkTop: crownBase + 0.3, cy: (crownBase + y + t.h) / 2, rh: near.crown * sxz, rv: (y + t.h - crownBase) / 2 });
      });
      const barkM = barkMaterial(near.barkMat), leafM = leafMaterial(near.leafMat, v);
      envApply(barkM); envApply(leafM);
      const make = (model, cap) => {
        // (room for SHARED_INSTANCES at least, see there; how many are used is limited by cap)
        const room = Math.max(SHARED_INSTANCES, cap);
        const b = new THREE.InstancedMesh(model.bark, barkM, room);
        const l = new THREE.InstancedMesh(model.leaves, leafM, room);
        for (const im of [b, l]) { im.count = 0; im.castShadow = true; im.receiveShadow = true; im.frustumCulled = false; this.group.add(im); }
        l.instanceColor = new THREE.InstancedBufferAttribute(new Float32Array(room * 3), 3);
        b.userData.cap = cap;
        return { bark: b, leaves: l };
      };
      const lim = LIMITS[this.quality] || LIMITS.balanced;
      return { list, mats, tints, near: make(near, Math.max(1, Math.min(list.length, lim.near))), mid: make(mid, Math.max(1, Math.min(list.length, lim.mid))) };
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
      V: this.atlas.V, rows: this.atlas.rows,
    };
    for (const a of [attrs.center, attrs.size, attrs.data]) a.setUsage(THREE.DynamicDrawUsage);
    this.impAttrs = attrs;
    this.impostors = new THREE.Sprite(impostorMaterial(this.atlas, attrs));
    envApply(this.impostors.material);
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
    this.updateShrubs(camera);
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
      if (e < lim.midR) scored.push([e, i]);
    }
    scored.sort((a, b) => a[0] - b[0]);
    const lod = new Uint8Array(this.trees.length); // 0 impostor, 1 mid, 2 near
    let nNear = 0, nMid = 0;
    for (const V of this.variants) { V.nearUsed = 0; V.midUsed = 0; }
    for (const [e, i] of scored) {
      const vi = this.trees[i].variant, V = this.variants[vi];
      if (e < lim.nearR && nNear < lim.near && V.nearUsed < V.near.bark.userData.cap) { lod[i] = 2; nNear++; V.nearUsed++; }
      else if (nMid < lim.mid && V.midUsed < V.mid.bark.userData.cap) { lod[i] = 1; nMid++; V.midUsed++; }
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

  // Undergrowth in big, dense woods (not clumps of trees): leafy shrubs of every size filling the ground under the trees, so
  // a wood reads as thick rather than trunks standing on bare ground. Two kinds of spot:
  //   edge     - on the open side of each tree along the outside of a dense wood, under the edge
  //              of its crown and a little beyond (what you see of a wood from the course); always
  //              drawn
  //   interior - a jittered 2.5 m grid over the ground inside dense woods; drawn only within
  //              SHRUB_NEAR m of the camera (further in, the trees and the edge shrubs hide them)
  // Interior shrubs are kept in 160 m chunks, so those far from the camera are skipped (few,
  // large chunks: every separate mesh costs time to set up when the course loads). Sizes run from knee-high
  // sprigs to a few big thickets; leaves come from the woods' own trees (oak, ash, maple), and
  // each shrub has its own shade: fresh, dark, yellowish or blue-green, with the odd reddish one.
  // allowed(x, z): where a shrub may grow (rough, not by paths or mown grass).
  buildShrubs(heightAt, allowed, envApply = () => {}) {
    const trees = this.trees || [];
    const cell = 14, grid = new Map(), key = (a, b) => a * 73856093 ^ b * 19349663;
    for (const t of trees) { const k = key(Math.floor(t.x / cell), Math.floor(t.z / cell)); if (!grid.has(k)) grid.set(k, []); grid.get(k).push(t); }
    const around = (x, z, r) => {
      const out = [], gx = Math.floor(x / cell), gz = Math.floor(z / cell);
      for (let a = -1; a <= 1; a++) for (let b = -1; b <= 1; b++) for (const o of grid.get(key(gx + a, gz + b)) || []) if (Math.hypot(o.x - x, o.z - z) < r) out.push(o);
      return out;
    };
    // Only real woods get undergrowth, not clumps or copses: a tree is deep in a wood when at least
    // 70 trees stand within 35 m of it (a clump never reaches that, however tight), and a tree is
    // in the woods when such a one is within 20 m (so the edge of a wood counts too)
    const big = 25, bgrid = new Map();
    for (const t of trees) { const k = key(Math.floor(t.x / big), Math.floor(t.z / big)); if (!bgrid.has(k)) bgrid.set(k, []); bgrid.get(k).push(t); }
    const within = (x, z, r, fn) => {
      const gx = Math.floor(x / big), gz = Math.floor(z / big), n = Math.ceil(r / big);
      for (let a = -n; a <= n; a++) for (let b = -n; b <= n; b++) for (const o of bgrid.get(key(gx + a, gz + b)) || []) if (Math.hypot(o.x - x, o.z - z) < r && fn(o)) return true;
      return false;
    };
    const core = new Set();
    for (const t of trees) { let n = 0; within(t.x, t.z, 35, () => ++n >= 70); if (n >= 70) core.add(t); }
    const woods = new Map();
    const inWoods = (t) => { if (!woods.has(t)) woods.set(t, core.has(t) || within(t.x, t.z, 20, (o) => core.has(o))); return woods.get(t); };
    const spots = [], taken = new Map();
    // (shrubs may overlap a little, as real ones grow into each other, but not stand inside one another)
    const free = (x, z, r) => {
      const gx = Math.floor(x / 4), gz = Math.floor(z / 4);
      for (let a = -2; a <= 2; a++) for (let b = -2; b <= 2; b++) for (const [px, pz, pr] of taken.get(key(gx + a, gz + b)) || []) if (Math.hypot(px - x, pz - z) < (r + pr) * 0.5) return false;
      return true;
    };
    // Sizes vary a lot: mostly small and medium, some big, the odd thicket
    const size = (x, z, i) => {
      const sz = hash(x + i * 3.1, z, 15.1), v1 = hash(x, z + i * 2.3, 15.9), v2 = hash(x + i, z + i, 16.3);
      if (sz > 0.95) return { h: 3 + v1 * 1.5, w: 5 + v2 * 3, big: true };
      if (sz > 0.8) return { h: 2.2 + v1 * 1.2, w: 3.5 + v2 * 2, big: true };
      if (sz > 0.5) return { h: 1.3 + v1 * 1, w: 2 + v2 * 1.6 };
      if (sz > 0.2) return { h: 0.8 + v1 * 0.7, w: 1.2 + v2 * 1.1 };
      return { h: 0.45 + v1 * 0.4, w: 0.7 + v2 * 0.6 };
    };
    const add = (x, z, s, edge) => {
      if (!allowed(x, z) || !free(x, z, s.w / 2)) return;
      const y = heightAt(x, z);
      if (y === null || y === undefined) return;
      const k = key(Math.floor(x / 4), Math.floor(z / 4));
      if (!taken.has(k)) taken.set(k, []);
      taken.get(k).push([x, z, s.w / 2]);
      spots.push({ x, z, y, h: s.h, w: s.w, edge, kind: hash(x, z, 17.1), rot: hash(x, z, 18.7) * 6.283, tint: hash(x, z, 19.3), shade: hash(x, z, 20.9) });
    };
    // The edges first (they matter most)
    for (const t of trees) {
      if (!t.edge || t.user || !inWoods(t)) continue;
      // neighbours within 14 m, and which way is open
      const near = around(t.x, t.z, cell).filter((o) => o !== t);
      const seen = new Array(8).fill(false);
      for (const o of near) seen[Math.floor(((Math.atan2(o.z - t.z, o.x - t.x) + Math.PI) / (2 * Math.PI)) * 8) % 8] = true;
      // Only the outside of dense woods: plenty of trees round it, open on one side (not a gap
      // within the wood, nor a lone tree or a thin row), and open ground beyond
      const open = seen.filter((v) => !v).length;
      if (near.length < 8 || open < 2 || open > 5) continue;
      let ox = 0, oz = 0;
      seen.forEach((s, k) => { if (!s) { const a = ((k + 0.5) / 8) * 2 * Math.PI - Math.PI; ox += Math.cos(a); oz += Math.sin(a); } });
      const ol = Math.hypot(ox, oz);
      if (ol < 0.01) continue;
      ox /= ol; oz /= ol;
      if (around(t.x + ox * 12, t.z + oz * 12, 7).length) continue;
      const count = 5 + Math.floor(hash(t.x, t.z, 12.3) * 4);
      for (let i = 0; i < count; i++) {
        const turn = (hash(t.x + i, t.z, 13.7) - 0.5) * 3.2, c = Math.cos(turn), s = Math.sin(turn);
        const dx = ox * c - oz * s, dz = ox * s + oz * c, sz = size(t.x, t.z, i);
        // (big ones set a little further back, under the trees, so they don't spill out far)
        const d = (t.rh || t.coneR || 3) * (sz.big ? 0.15 + hash(t.x, t.z + i, 14.1) * 0.5 : 0.3 + hash(t.x, t.z + i, 14.1) * 0.9) + 0.4;
        add(t.x + dx * d, t.z + dz * d, sz, true);
      }
    }
    // Then the ground inside dense woods: wherever there are at least 4 trees within 9 m and
    // one within 6 m, nearly every spot on a 2.5 m grid gets a shrub
    if (trees.length) {
      let minX = Infinity, maxX = -Infinity, minZ = Infinity, maxZ = -Infinity;
      for (const t of trees) { minX = Math.min(minX, t.x); maxX = Math.max(maxX, t.x); minZ = Math.min(minZ, t.z); maxZ = Math.max(maxZ, t.z); }
      const STEP = 2.5;
      for (let gz = Math.floor(minZ / STEP); gz <= Math.ceil(maxZ / STEP); gz++) for (let gx = Math.floor(minX / STEP); gx <= Math.ceil(maxX / STEP); gx++) {
        const x = (gx + hash(gx, gz, 21.7)) * STEP, z = (gz + hash(gx, gz, 22.3)) * STEP;
        if (hash(gx, gz, 23.9) > 0.9) continue;
        const near = around(x, z, 9);
        if (near.length < 4 || !near.some((o) => Math.hypot(o.x - x, o.z - z) < 6) || !near.every(inWoods)) continue;
        add(x, z, size(x, z, 7), false);
      }
    }
    // Leaves of the woods' own broadleaf trees
    const pick = (kind) => this.models.find((md) => md.v.kind === kind)?.near;
    const sources = [pick('oak'), pick('ash'), pick('maple')].filter(Boolean);
    const geos = { edge: shrubGeometry(28), interior: shrubGeometry(18) };
    const mats = sources.map((src) => { const mat = leafMaterial(src.leafMat, { green: false }); envApply(mat); return mat; });
    const m = new THREE.Matrix4(), q = new THREE.Quaternion(), sc = new THREE.Vector3(), up = new THREE.Vector3(0, 1, 0), col = new THREE.Color();
    // Shades (multiplying the leaf texture): fresh, dark, yellowish, blue-green, and rarely reddish
    const SHADES = [[0.82, 0.95, 0.66], [0.58, 0.7, 0.5], [0.98, 1.0, 0.58], [0.6, 0.8, 0.72], [0.95, 0.68, 0.48]];
    const shadeOf = (p) => SHADES[p.shade < 0.32 ? 0 : p.shade < 0.6 ? 1 : p.shade < 0.8 ? 2 : p.shade < 0.96 ? 3 : 4];
    const chunks = new Map(), CH = 160;
    for (const p of spots) {
      // (edge shrubs, always drawn, in one mesh per kind of leaf)
      const ck = p.edge ? `0,0,1,${Math.floor(p.kind * sources.length)}` : `${Math.floor(p.x / CH)},${Math.floor(p.z / CH)},0,${Math.floor(p.kind * sources.length)}`;
      if (!chunks.has(ck)) chunks.set(ck, []);
      chunks.get(ck).push(p);
    }
    this.shrubCount = 0; this.shrubChunks = [];
    if (!sources.length) return;
    for (const [ck, list] of chunks) {
      const [cx, cz, edge, si] = ck.split(',').map(Number);
      const room = Math.max(SHARED_INSTANCES, list.length); // (see SHARED_INSTANCES)
      const mesh = new THREE.InstancedMesh(edge ? geos.edge : geos.interior, mats[si], room);
      mesh.count = list.length;
      mesh.instanceColor = new THREE.InstancedBufferAttribute(new Float32Array(room * 3), 3);
      list.forEach((p, i) => {
        q.setFromAxisAngle(up, p.rot);
        m.compose(T(p.x, p.y - 0.1, p.z), q, sc.set(p.w, p.h, p.w * (0.8 + p.tint * 0.4)));
        mesh.setMatrixAt(i, m);
        const [r, g, b] = shadeOf(p), v = 0.85 + p.tint * 0.3;
        mesh.setColorAt(i, col.setRGB(r * v, g * v, b * v));
      });
      mesh.receiveShadow = true;
      mesh.computeBoundingSphere();
      mesh.layers.set(1); // (not drawn into the pond reflections)
      this.group.add(mesh);
      if (!edge) this.shrubChunks.push({ mesh, x: (cx + 0.5) * CH, z: (cz + 0.5) * CH });
      this.shrubCount += list.length;
    }
    this.shrubCam = null;
  }

  // Shrubs inside the woods: only the chunks near the camera
  updateShrubs(camera) {
    const ch = this.shrubChunks;
    if (!ch?.length) return;
    const x = -camera.position.x, z = camera.position.z;
    if (this.shrubCam && Math.hypot(x - this.shrubCam.x, z - this.shrubCam.z) < 6) return;
    this.shrubCam = { x, z };
    for (const c of ch) c.mesh.visible = Math.hypot(c.x - x, c.z - z) < SHRUB_NEAR + 113; // (+ half a chunk's diagonal)
  }

  triangles() {
    let n = 0;
    for (const V of this.variants || []) for (const g of [V.near, V.mid]) {
      for (const im of [g.bark, g.leaves]) n += ((im.geometry.index?.count ?? im.geometry.attributes.position.count) / 3) * im.count;
    }
    return n + (this.impostors?.count || 0) * 2;
  }
}
