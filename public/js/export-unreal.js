// Export the loaded course for Unreal Engine 5 (a prototype: see unreal/README.md). Everything comes
// from the course as the game built it (terrain with its water carved in, the painted aerial photo,
// the trees it planted), written by the server into exports/<course id>/:
//
//   heightmap.png      16-bit greyscale, a size Unreal's Landscape takes as it is (505 .. 8129
//                      samples a side), 1 m (or 2 m for big courses) between samples; row 0 north
//   photo.jpg          the course picture over exactly the heightmap's area (for the landscape's
//                      material: its UVs run 0..1 over the whole landscape)
//   layer_*.png        fairway, green, tee, bunker, water and rough as distance fields (0.5 on each
//                      outline, more inside, less outside, 0..1 over 3 m either side), over the same
//                      square at twice the heightmap's samples
//   trees.json         [{ x, y, z, h, r, kind, variant, yaw, sx, sy, sz }] in Unreal centimetres
//                      (variant: which tree_N.glb; yaw degrees; sx, sy, sz its scale as the game
//                      draws it: sx, sy across, sz up)
//   tree_N.glb         the game's tree models (bark and leaves with their pictures), metres
//   tree_N_leaves.png  its leaf picture in the colours the game draws it (the game tints leaves as it
//                      draws them: summer green, the species' own shade)
//   tree_N_bark.jpg, tree_N_bark_n.jpg   its bark picture and relief (tiling already in the uvs)
//   tree_models.json   [{ variant, kind, file, height, crown, leaves, bark, barkNormal }] (metres)
//   grass_tuft.glb     a tuft of grass blades for Unreal's landscape grass, metres
//   water.glb          every pond, lake and stream surface as one mesh, in the same frame as the
//                      landscape (glTF x = east, y = up, z = south, metres from the north-west
//                      corner, up from heightBaseMetres); water.json: its bounds, to line it up
//   water_bodies.json  the water as Unreal's Water system wants it: lakes (an outline at a level) and
//                      rivers (a centre line with a width and a falling level), Unreal centimetres:
//                      [{ type: 'lake', points: [{ x, y, z }] } | { type: 'river', points: [{ x, y, z, w }] }]
//   props_*.glb        the course's built things, one model per kind - paths (paved, gravel),
//                      bridges, buildings, cars - in the water's frame (x east, y up, z south,
//                      metres from the north-west corner), coloured per vertex; props.json: each
//                      file's kind and bounds in Unreal centimetres, to line it up
//   holes.json         tees, pins and aim points per hole, in Unreal centimetres
//   course.json        sizes and scales for the landscape import, and how coordinates map
//
// Coordinates: Unreal X = east, Y = south (Unreal is left-handed: X forward, Y right, Z up), in
// centimetres, with the landscape's north-west corner at 0, 0. Course x is east, z north.

import * as THREE from 'three/webgpu';
import { GLTFExporter } from 'three/addons/exporters/GLTFExporter.js';
import { hash } from './treeplace.js';
import { tuftGeometry } from './grass.js';

const SIZES = [505, 1009, 2017, 4033, 8129];

export async function exportForUnreal({ course, scene, id, status = () => {} }) {
  const { minX, maxX, minZ, maxZ } = course.bounds;
  const W = maxX - minX, H = maxZ - minZ;
  // Samples a side: the smallest size that covers the course at 1 m, else 2 m between samples
  let step = 1, N = SIZES.find((s) => s - 1 >= Math.max(W, H) / step);
  if (!N || N > 4033) { step = 2; N = SIZES.find((s) => s - 1 >= Math.max(W, H) / step) || 8129; }
  const span = (N - 1) * step;                       // metres across the landscape
  const x0 = (minX + maxX) / 2 - span / 2, z1 = (minZ + maxZ) / 2 + span / 2; // its north-west corner
  const at = (i, j) => [x0 + i * step, z1 - j * step];
  const put = (file, body, type) => fetch(`/api/export/${id}/${file}`, { method: 'PUT', headers: { 'Content-Type': type }, body })
    .then((r) => { if (!r.ok) throw new Error(`${file}: HTTP ${r.status}`); });

  // ---- heights (beyond the course: the land around it as the game draws it, easing into the
  // real distant land, or the course's edge carried outward without it)
  status('Exporting heights…');
  await tick();
  const clampX = (x) => Math.min(maxX - 0.01, Math.max(minX, x)), clampZ = (z) => Math.min(maxZ - 0.01, Math.max(minZ, z));
  const hts = new Float32Array(N * N);
  let lo = Infinity, hi = -Infinity;
  for (let j = 0; j < N; j++) {
    for (let i = 0; i < N; i++) {
      const [x, z] = at(i, j);
      const inside = x >= minX && x <= maxX && z >= minZ && z <= maxZ;
      let y = inside || !scene.groundBeyond ? course.heightAt(clampX(x), clampZ(z)) : scene.groundBeyond(x, z);
      if (y === null || !Number.isFinite(y)) y = NaN;
      hts[j * N + i] = y;
      if (y < lo) lo = y;
      if (y > hi) hi = y;
    }
    if ((j & 255) === 0) await tick();
  }
  for (let k = 0; k < hts.length; k++) if (!Number.isFinite(hts[k])) hts[k] = lo;
  // Ponds and streams deeper than the game carves them (it only needs a flat surface over them;
  // Unreal's water takes its colour from how deep it is): the bed falls away from the bank, a
  // quarter of a metre deeper for every metre out, down to 2.5 m more in the middle
  {
    const D = new Float32Array(N * N);
    for (let j = 0; j < N; j++) for (let i = 0; i < N; i++) {
      const [x, z] = at(i, j), inside = x >= minX && x <= maxX && z >= minZ && z <= maxZ;
      D[j * N + i] = inside && course.surfaceAt(x, z) === 'water' ? 1e6 : 0;
    }
    for (let j = 1; j < N; j++) for (let i = 1; i < N - 1; i++) {
      const k = j * N + i;
      if (D[k]) D[k] = Math.min(D[k], D[k - 1] + step, D[k - N] + step, D[k - N - 1] + step * 1.414, D[k - N + 1] + step * 1.414);
    }
    for (let j = N - 2; j >= 0; j--) for (let i = N - 2; i >= 1; i--) {
      const k = j * N + i;
      if (D[k]) D[k] = Math.min(D[k], D[k + 1] + step, D[k + N] + step, D[k + N + 1] + step * 1.414, D[k + N - 1] + step * 1.414);
    }
    // (kept above the export's lowest point less 4 m: inside the same height range, so the landscape's
    // import settings stay the same and a re-import of the heightmap is enough)
    for (let k = 0; k < hts.length; k++) if (D[k] && D[k] < 1e5) hts[k] = Math.max(lo - 4, hts[k] - Math.min(2.5, 0.25 * D[k]));
  }
  // Unreal: a 16-bit value v sits at (v - 32768) / 128 * (Z scale / 100) metres. Z scale chosen so
  // the course's whole height range (plus a margin) fits in the 16 bits.
  const range = Math.max(10, hi - lo + 10);
  const zScale = (range / 512) * 100;
  const base = lo - 5;                                 // metres at value 0
  const v16 = new Uint16Array(N * N);
  for (let k = 0; k < hts.length; k++) v16[k] = Math.round(((hts[k] - base) / range) * 65535);
  await put('heightmap.png', await png16(v16, N, N), 'image/png');

  // ---- the course picture over the landscape's area
  status('Exporting the aerial photo…');
  await tick();
  const src = scene.courseCanvas;
  if (src) {
    const P = Math.min(8192, Math.round(span * 2)); // (about 0.5 m a pixel, up to 8192)
    const c = document.createElement('canvas');
    c.width = c.height = P;
    const g = c.getContext('2d');
    g.fillStyle = '#4b6b35'; g.fillRect(0, 0, P, P);
    // (the land around first, from the wider, coarser photo the game puts round the course; then
    // the course canvas, which covers [minX, maxX] x [minZ, maxZ], row 0 at maxZ)
    const k = P / span;
    if (scene.surroundCanvas && scene.surroundRect) {
      const [sx0, sz0, sx1, sz1] = scene.surroundRect;
      g.drawImage(scene.surroundCanvas, (sx0 - x0) * k, (z1 - sz1) * k, (sx1 - sx0) * k, (sz1 - sz0) * k);
    }
    g.drawImage(src, (minX - x0) * k, (z1 - maxZ) * k, W * k, H * k);
    await put('photo.jpg', await new Promise((r) => c.toBlob(r, 'image/jpeg', 0.9)), 'image/jpeg');
  }

  // ---- surfaces, as distance fields: each pixel how far it is from the edge of the course's
  // fairways (with a fringe round every green), greens, tees, bunkers and water - 0.5 on the
  // outline, more inside, less outside, reaching 0 and 1 SDF_M metres out and in. Blending between
  // pixels then follows the true outline, so edges come out as smooth curves at any distance
  // rather than in pixel steps. The rough is everything else (1 - the most of the others).
  status('Exporting surface outlines…');
  await tick();
  const SDF_M = 3, FRINGE_M = 1.5;
  const M = Math.min(8192, (N - 1) * 2 + 1), ms = span / (M - 1);
  const feats = course.features || [];
  const field = (surfaces, grow = 0) => {
    // inside: the outlines filled (holes cut out) on a canvas, one pixel a sample
    const c = document.createElement('canvas');
    c.width = c.height = M;
    const g = c.getContext('2d', { willReadFrequently: true });
    const px = (x) => (x - x0) / ms + 0.5, py = (z) => (z1 - z) / ms + 0.5;
    const polys = feats.filter((f) => surfaces.includes(f.surface) && f.poly?.length > 2);
    for (const inner of [false, true]) {
      g.fillStyle = inner ? '#000' : '#fff';
      for (const f of polys) if (!!f.inner === inner) {
        g.beginPath();
        f.poly.forEach(([x, z], k) => (k ? g.lineTo(px(x), py(z)) : g.moveTo(px(x), py(z))));
        g.closePath(); g.fill();
      }
    }
    const inside = g.getImageData(0, 0, M, M).data;
    // distance to the nearest outline, worked out only near the outlines (SDF_M + grow around each edge)
    const R = SDF_M + grow, d = new Float32Array(M * M).fill(R);
    for (const f of polys) {
      const P = f.poly;
      for (let k = 0; k < P.length; k++) {
        const [ax, az] = P[k], [bx, bz] = P[(k + 1) % P.length];
        const i0 = Math.max(0, Math.floor((Math.min(ax, bx) - R - x0) / ms)), i1 = Math.min(M - 1, Math.ceil((Math.max(ax, bx) + R - x0) / ms));
        const j0 = Math.max(0, Math.floor((z1 - Math.max(az, bz) - R) / ms)), j1 = Math.min(M - 1, Math.ceil((z1 - Math.min(az, bz) + R) / ms));
        const dx = bx - ax, dz = bz - az, l2 = dx * dx + dz * dz || 1e-9;
        for (let j = j0; j <= j1; j++) {
          const z = z1 - j * ms;
          for (let i = i0; i <= i1; i++) {
            const x = x0 + i * ms;
            const t = Math.max(0, Math.min(1, ((x - ax) * dx + (z - az) * dz) / l2));
            const e = Math.hypot(x - ax - dx * t, z - az - dz * t), q = j * M + i;
            if (e < d[q]) d[q] = e;
          }
        }
      }
    }
    // signed (inside +), grown outward by grow metres, as 0..1 with the outline at 0.5
    const out = new Float32Array(M * M);
    for (let q = 0; q < out.length; q++) {
      const sd = (inside[q * 4] > 127 ? d[q] : -d[q]) + grow;
      out[q] = Math.max(0, Math.min(1, 0.5 + sd / (2 * SDF_M)));
    }
    return out;
  };
  const F = {
    green: field(['green']), tee: field(['tee']), bunker: field(['sand']), water: field(['water']),
  };
  // fairways, and a fringe of fairway-cut grass round every green (where it isn't green itself)
  const fair = field(['fairway']), fringe = field(['green'], FRINGE_M);
  F.fairway = fair.map((v, q) => Math.max(v, Math.min(fringe[q], 1 - F.green[q])));
  await tick();
  // the rough: everything the others aren't
  F.rough = new Float32Array(M * M);
  for (let q = 0; q < M * M; q++) F.rough[q] = 1 - Math.max(F.fairway[q], F.green[q], F.tee[q], F.bunker[q], F.water[q]);
  for (const [k, v] of Object.entries(F)) {
    const b = new Uint8Array(M * M);
    for (let q = 0; q < b.length; q++) b[q] = Math.round(v[q] * 255);
    await put(`layer_${k}.png`, await png8(b, M, M), 'image/png');
  }

  // ---- trees, holes, and the numbers the import needs
  const ue = (x, y, z) => ({ x: Math.round((x - x0) * 100), y: Math.round((z1 - z) * 100), z: Math.round((y - base) * 100) });
  const yAt = (x, z) => course.heightAt(x, z) ?? lo;
  // (each tree as the game draws it: its model, turn and size; see Forest.build in trees.js)
  const forest = scene.forest, models = forest?.models || [];
  const planted = forest?.trees || scene.trees?.all || [];
  const trees = planted.map((t) => {
    const out = { ...ue(t.x, yAt(t.x, t.z), t.z), h: Math.round(t.h * 100), r: Math.round(t.r * 100), kind: t.kind || null };
    const md = models[t.variant]?.near;
    if (md) {
      const sy = t.h / md.height;
      const sxz = Math.min(sy * 1.5, Math.max(sy * 0.7, t.r / Math.max(0.1, md.crown))) * (0.92 + 0.16 * hash(t.x, t.z, 5.3));
      Object.assign(out, {
        variant: t.variant, yaw: +(((t.rot || 0) * 180) / Math.PI).toFixed(1),
        sx: +(sxz * (0.9 + 0.2 * hash(t.x, t.z, 7.7))).toFixed(3), sy: +(sxz * (0.9 + 0.2 * hash(t.x, t.z, 9.1))).toFixed(3), sz: +sy.toFixed(3),
      });
    }
    return out;
  });
  await put('trees.json', JSON.stringify(trees), 'application/json');

  // ---- models: the trees the course uses, a grass tuft, and the water
  status('Exporting tree models…');
  await tick();
  const exporter = new GLTFExporter();
  const glb = (object) => exporter.parseAsync(object, { binary: true, maxTextureSize: 2048 }).then((b) => new Blob([b], { type: 'model/gltf-binary' }));
  const used = [...new Set(trees.map((t) => t.variant).filter((v) => v != null))];
  const list = [];
  for (const vi of used) {
    const { v, near } = models[vi];
    const mesh = treeMesh(near);
    if (!mesh) continue;
    await put(`tree_${vi}.glb`, await glb(mesh), 'model/gltf-binary');
    const entry = { variant: vi, kind: v.kind, file: `tree_${vi}.glb`, height: +near.height.toFixed(3), crown: +near.crown.toFixed(3) };
    const leafPng = await picture(near.leafMat?.map, 'image/png', (d) => leafColours(d, v));
    if (leafPng) { await put(`tree_${vi}_leaves.png`, leafPng, 'image/png'); entry.leaves = `tree_${vi}_leaves.png`; }
    const barkJpg = await picture(near.barkMat?.map, 'image/jpeg');
    if (barkJpg) { await put(`tree_${vi}_bark.jpg`, barkJpg, 'image/jpeg'); entry.bark = `tree_${vi}_bark.jpg`; }
    const barkN = await picture(near.barkMat?.normalMap, 'image/jpeg');
    if (barkN) { await put(`tree_${vi}_bark_n.jpg`, barkN, 'image/jpeg'); entry.barkNormal = `tree_${vi}_bark_n.jpg`; }
    list.push(entry);
    status(`Exporting tree models… ${list.length}/${used.length}`);
  }
  await put('tree_models.json', JSON.stringify(list, null, 1), 'application/json');
  {
    const g = tuftGeometry({ blades: 12, spread: 0.04, lean: [0.01, 0.05], height: [0.08, 0.16], width: [0.006, 0.01] });
    const keep = new THREE.BufferGeometry();
    for (const k of ['position', 'normal', 'uv']) if (g.attributes[k]) keep.setAttribute(k, g.attributes[k]);
    if (g.index) keep.setIndex(g.index);
    keep.computeVertexNormals();
    const m = new THREE.Mesh(keep, new THREE.MeshStandardMaterial({ color: 0x4f7a2e, roughness: 0.9, side: THREE.DoubleSide }));
    m.name = 'GrassTuft';
    await put('grass_tuft.glb', await glb(m), 'model/gltf-binary');
  }
  status('Exporting water…');
  await tick();
  const water = waterMesh(scene, x0, z1, base);
  if (water) {
    await put('water.glb', await glb(water.mesh), 'model/gltf-binary');
    await put('water.json', JSON.stringify(water.info, null, 1), 'application/json');
  }
  await put('water_bodies.json', JSON.stringify(waterBodies(course, ue)), 'application/json');
  status('Exporting buildings, paths, bridges and cars…');
  await tick();
  const props = [];
  for (const p of propMeshes(scene, x0, z1, base)) {
    await put(p.file, await glb(p.mesh), 'model/gltf-binary');
    props.push(p.info);
  }
  await put('props.json', JSON.stringify(props, null, 1), 'application/json');
  const P3 = (p) => (p ? ue(p.x, yAt(p.x, p.z), p.z) : null);
  const holes = course.holes.map((h) => ({
    number: h.number, par: h.par, index: h.index,
    tees: Object.fromEntries(Object.entries(h.tees).map(([k, p]) => [k, P3(p)])),
    pins: Object.fromEntries(Object.entries(h.pins).map(([k, p]) => [k, P3(p)])),
    aims: h.aims.map(P3), green: P3(h.greenCenter),
  }));
  await put('holes.json', JSON.stringify(holes, null, 1), 'application/json');
  const meta = {
    name: course.name, id, lat: course.lat ?? null, lon: course.lon ?? null,
    samples: N, metresPerSample: step, sizeMetres: span,
    // (Landscape import: Location 0,0,Z, Scale X = Y = metresPerSample * 100, Scale Z = zScale)
    landscape: { scaleXY: step * 100, scaleZ: +zScale.toFixed(4), locationZ: +((range / 2) * 100).toFixed(1) },
    heightBaseMetres: base, heightRangeMetres: range,
    courseOrigin: { x: x0, z: z1 },
    coords: 'Unreal X = east, Y = south, Z up, centimetres; 0,0 = the landscape\'s north-west corner; Z = 0 at heightBaseMetres',
    trees: trees.length, holes: holes.length,
    exported: new Date().toISOString(),
  };
  await put('course.json', JSON.stringify(meta, null, 1), 'application/json');
  status(`Exported ${N}×${N} landscape, ${trees.length.toLocaleString()} trees, ${holes.length} holes`);
  return meta;
}

const tick = () => new Promise((r) => setTimeout(r, 0));

// A texture's picture as a file (up to 2048 px), optionally with its pixels changed (d: RGBA)
async function picture(tex, type, change = null) {
  const img = tex?.image;
  if (!img || !(img.width > 0)) return null;
  const k = Math.min(1, 2048 / Math.max(img.width, img.height));
  const c = document.createElement('canvas');
  c.width = Math.round(img.width * k); c.height = Math.round(img.height * k);
  const g = c.getContext('2d', { willReadFrequently: true });
  g.drawImage(img, 0, 0, c.width, c.height);
  if (change) { const d = g.getImageData(0, 0, c.width, c.height); change(d.data); g.putImageData(d, 0, 0); }
  return new Promise((r) => c.toBlob(r, type, 0.9));
}
// The game's leaf colouring (trees.js leafColour), in linear light: aspen-type pictures turned
// summer green, then a richer green (not for raw pictures: moss, flowers stay their colours) and
// the species' own shade
function leafColours(d, v) {
  const lin = new Float32Array(256);
  for (let i = 0; i < 256; i++) { const c = i / 255; lin[i] = c <= 0.04045 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4; }
  const srgb = (x) => { x = Math.min(1, Math.max(0, x)); return Math.round((x <= 0.0031308 ? x * 12.92 : 1.055 * x ** (1 / 2.4) - 0.055) * 255); };
  const tint = v.tint || [1, 1, 1], rich = v.raw ? [1, 1, 1] : [0.6, 0.68, 0.5];
  const k = [rich[0] * tint[0], rich[1] * tint[1], rich[2] * tint[2]];
  for (let o = 0; o < d.length; o += 4) {
    let r = lin[d[o]], g = lin[d[o + 1]], b = lin[d[o + 2]];
    if (v.green) { const l = (0.3 * r + 0.6 * g + 0.1 * b) * 1.15; r = l * 0.55; g = l * 0.85; b = l * 0.32; }
    d[o] = srgb(r * k[0]); d[o + 1] = srgb(g * k[1]); d[o + 2] = srgb(b * k[2]);
  }
}

// A tree model as one mesh for glTF: bark (its picture tiled as the game tiles it) and leaves (cut
// out by their picture's alpha, both sides), with plain standard materials the exporter knows
function treeMesh(near) {
  const strip = (g) => {
    const out = new THREE.BufferGeometry();
    for (const k of ['position', 'normal', 'uv']) if (g.attributes[k]) out.setAttribute(k, g.attributes[k].clone());
    if (g.index) out.setIndex(g.index.clone());
    if (!out.attributes.normal) out.computeVertexNormals();
    return out;
  };
  const bark = strip(near.bark), leaves = strip(near.leaves);
  if (!bark.attributes.uv || !leaves.attributes.uv) return null;
  // (the bark's tiling put into its uvs: a texture's repeat doesn't survive every importer)
  const bm = near.barkMat?.map;
  if (bm) {
    const uv = bark.attributes.uv;
    for (let i = 0; i < uv.count; i++) uv.setXY(i, uv.getX(i) * bm.repeat.x + bm.offset.x, uv.getY(i) * bm.repeat.y + bm.offset.y);
  }
  const plain = (t) => { if (!t) return null; const c = t.clone(); c.repeat.set(1, 1); c.offset.set(0, 0); c.needsUpdate = true; return c; };
  const barkMat = new THREE.MeshStandardMaterial({ name: 'Bark', map: plain(bm), normalMap: plain(near.barkMat?.normalMap), roughness: 0.9 });
  const leafMat = new THREE.MeshStandardMaterial({ name: 'Leaves', map: near.leafMat?.map || null, alphaTest: 0.5, side: THREE.DoubleSide, roughness: 0.75 });
  const b = new THREE.Mesh(bark, barkMat), l = new THREE.Mesh(leaves, leafMat);
  b.name = 'Bark'; l.name = 'Leaves';
  const group = new THREE.Group();
  group.name = 'Tree';
  group.add(b, l);
  return group;
}

// Ponds and lakes as outlines at their level (points about 5 m apart), streams and rivers as centre
// lines with their width and level (every 10 m); sloping water mapped as an outline (creeks on desert
// courses) as a lake at its average level. ue(x, y, z): course metres to Unreal centimetres.
function waterBodies(course, ue) {
  const out = [], chains = new Set();
  for (const wb of course.waterBodies || []) {
    if (wb.sea || !wb.poly?.length) continue;
    if (wb.ribbon?.chain) { chains.add(wb.ribbon.chain); continue; }
    const pts = [];
    let last = null;
    for (const [x, z] of wb.poly) {
      if (last && Math.hypot(x - last[0], z - last[1]) < 5) continue;
      pts.push(ue(x, wb.level, z)); last = [x, z];
    }
    if (pts.length >= 3) out.push({ type: 'lake', points: pts });
  }
  for (const c of chains) {
    const pts = [];
    for (let i = 0; i < c.pts.length; i += 5) {
      const [x, z] = c.pts[i], lvl = c.level ? c.level[i] : null;
      if (lvl == null) continue;
      pts.push({ ...ue(x, lvl, z), w: Math.round(c.hw[i] * 200) });
    }
    const n = c.pts.length - 1;
    if (n % 5 && c.level) { const [x, z] = c.pts[n]; pts.push({ ...ue(x, c.level[n], z), w: Math.round(c.hw[n] * 200) }); }
    if (pts.length >= 2) out.push({ type: 'river', points: pts });
  }
  return out;
}

// The built things the scene noted (scene.exportProps: { kind, mesh }), one mesh per kind, in the
// landscape's frame like the water: scene (x, y, z) -> (-x - x0, y - base, z1 - z) metres, a half
// turn about the vertical (the scene's x is the course's mirrored), so the triangles keep facing
// the way they did. Instanced meshes (cars) are written out one copy per instance, each in its
// instance's colour. Colours per vertex (white where there were none).
function propMeshes(scene, x0, z1, base) {
  const byKind = new Map();
  const m4 = new THREE.Matrix4(), col = new THREE.Color();
  const add = (kind, g, tint) => {
    const p = g.attributes.position, n = g.attributes.normal;
    const pos = new Float32Array(p.count * 3), nor = new Float32Array(p.count * 3), c = new Float32Array(p.count * 3);
    const src = g.attributes.color;
    for (let i = 0; i < p.count; i++) {
      pos[i * 3] = -p.getX(i) - x0; pos[i * 3 + 1] = p.getY(i) - base; pos[i * 3 + 2] = z1 - p.getZ(i);
      if (n) { nor[i * 3] = -n.getX(i); nor[i * 3 + 1] = n.getY(i); nor[i * 3 + 2] = -n.getZ(i); }
      const r = src ? src.getX(i) : 1, gg = src ? src.getY(i) : 1, b = src ? src.getZ(i) : 1;
      c[i * 3] = r * tint.r; c[i * 3 + 1] = gg * tint.g; c[i * 3 + 2] = b * tint.b;
    }
    const q = new THREE.BufferGeometry();
    q.setAttribute('position', new THREE.BufferAttribute(pos, 3));
    q.setAttribute('normal', new THREE.BufferAttribute(nor, 3));
    q.setAttribute('color', new THREE.BufferAttribute(c, 3));
    if (g.index) q.setIndex(new THREE.BufferAttribute(Uint32Array.from(g.index.array), 1));
    else q.setIndex(new THREE.BufferAttribute(Uint32Array.from({ length: p.count }, (_, i) => i), 1));
    if (!n) q.computeVertexNormals();
    if (!byKind.has(kind)) byKind.set(kind, []);
    byKind.get(kind).push(q);
  };
  for (const { kind, mesh } of scene.exportProps || []) {
    if (!mesh?.geometry?.attributes?.position) continue;
    mesh.updateWorldMatrix(true, false);
    if (mesh.isInstancedMesh) {
      for (let i = 0; i < mesh.count; i++) {
        mesh.getMatrixAt(i, m4);
        m4.premultiply(mesh.matrixWorld);
        if (mesh.instanceColor) mesh.getColorAt(i, col); else col.setRGB(1, 1, 1);
        add(kind, mesh.geometry.clone().applyMatrix4(m4), col.clone());
      }
    } else {
      add(kind, mesh.geometry.clone().applyMatrix4(mesh.matrixWorld), new THREE.Color(1, 1, 1));
    }
  }
  const out = [];
  for (const [kind, parts] of byKind) {
    let nv = 0, ni = 0;
    for (const q of parts) { nv += q.attributes.position.count; ni += q.index.count; }
    const pos = new Float32Array(nv * 3), nor = new Float32Array(nv * 3), c = new Float32Array(nv * 3), idx = new Uint32Array(ni);
    let ov = 0, oi = 0;
    for (const q of parts) {
      pos.set(q.attributes.position.array, ov * 3); nor.set(q.attributes.normal.array, ov * 3); c.set(q.attributes.color.array, ov * 3);
      for (let i = 0; i < q.index.count; i++) idx[oi++] = q.index.array[i] + ov;
      ov += q.attributes.position.count;
    }
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.BufferAttribute(pos, 3));
    g.setAttribute('normal', new THREE.BufferAttribute(nor, 3));
    g.setAttribute('color', new THREE.BufferAttribute(c, 3));
    g.setIndex(new THREE.BufferAttribute(idx, 1));
    g.computeBoundingBox();
    const b = g.boundingBox;
    const mesh = new THREE.Mesh(g, new THREE.MeshStandardMaterial({ name: kind, vertexColors: true, roughness: 0.85, side: THREE.DoubleSide }));
    mesh.name = kind;
    const file = `props_${kind}.glb`;
    out.push({ file, mesh, info: { kind, file, triangles: idx.length / 3, min: { x: b.min.x * 100, y: b.min.z * 100, z: b.min.y * 100 }, max: { x: b.max.x * 100, y: b.max.z * 100, z: b.max.y * 100 } } });
  }
  return out;
}

// Every water surface (not the sea) as one mesh in the landscape's frame: x east and z south of
// the north-west corner, y up from the export's base height, metres
function waterMesh(scene, x0, z1, base) {
  const parts = [];
  for (const m of scene.waterMeshes || []) {
    if (!m.geometry?.attributes?.position) continue;
    m.updateWorldMatrix(true, false);
    const g = m.geometry.clone().applyMatrix4(m.matrixWorld);
    const p = g.attributes.position, out = new Float32Array(p.count * 3);
    for (let i = 0; i < p.count; i++) {
      // (scene x is the course's mirrored)
      out[i * 3] = -p.getX(i) - x0; out[i * 3 + 1] = p.getY(i) - base; out[i * 3 + 2] = z1 - p.getZ(i);
    }
    parts.push({ pos: out, index: g.index ? g.index.array : null, n: p.count });
  }
  if (!parts.length) return null;
  let nv = 0, ni = 0;
  for (const q of parts) { nv += q.n; ni += q.index ? q.index.length : q.n; }
  const pos = new Float32Array(nv * 3), idx = new Uint32Array(ni);
  let ov = 0, oi = 0;
  for (const q of parts) {
    pos.set(q.pos, ov * 3);
    if (q.index) for (let i = 0; i < q.index.length; i++) idx[oi++] = q.index[i] + ov;
    else for (let i = 0; i < q.n; i++) idx[oi++] = ov + i;
    ov += q.n;
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.BufferAttribute(pos, 3));
  g.setIndex(new THREE.BufferAttribute(idx, 1));
  // (facing up: the mirroring turns the triangles over, so the winding is put right if it did)
  g.computeVertexNormals();
  let up = 0;
  for (let i = 1; i < g.attributes.normal.array.length; i += 3) up += g.attributes.normal.array[i];
  if (up < 0) {
    for (let i = 0; i < idx.length; i += 3) { const t = idx[i + 1]; idx[i + 1] = idx[i + 2]; idx[i + 2] = t; }
    g.index.needsUpdate = true;
    g.computeVertexNormals();
  }
  g.computeBoundingBox();
  const b = g.boundingBox;
  const mesh = new THREE.Mesh(g, new THREE.MeshStandardMaterial({ name: 'Water', color: 0x0d2a2e, roughness: 0.05 }));
  mesh.name = 'Water';
  // (its bounds in Unreal terms - X east, Y south, Z up, centimetres - to line the import up with)
  const info = { min: { x: b.min.x * 100, y: b.min.z * 100, z: b.min.y * 100 }, max: { x: b.max.x * 100, y: b.max.z * 100, z: b.max.y * 100 }, triangles: idx.length / 3 };
  return { mesh, info };
}

// ---- PNG writing (greyscale, 8 or 16 bits), deflated by the browser
async function png16(v, w, h) {
  const row = 1 + w * 2, raw = new Uint8Array(row * h);
  for (let j = 0; j < h; j++) {
    raw[j * row] = 0;
    for (let i = 0; i < w; i++) { const x = v[j * w + i], o = j * row + 1 + i * 2; raw[o] = x >> 8; raw[o + 1] = x & 255; }
  }
  return pngFile(w, h, 16, raw);
}
async function png8(v, w, h) {
  const row = 1 + w, raw = new Uint8Array(row * h);
  for (let j = 0; j < h; j++) { raw[j * row] = 0; raw.set(v.subarray(j * w, (j + 1) * w), j * row + 1); }
  return pngFile(w, h, 8, raw);
}
async function pngFile(w, h, depth, raw) {
  const idat = new Uint8Array(await new Response(new Blob([raw]).stream().pipeThrough(new CompressionStream('deflate'))).arrayBuffer());
  const ihdr = new Uint8Array(13), dv = new DataView(ihdr.buffer);
  dv.setUint32(0, w); dv.setUint32(4, h); ihdr[8] = depth; ihdr[9] = 0; // greyscale
  return new Blob([new Uint8Array([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ihdr), chunk('IDAT', idat), chunk('IEND', new Uint8Array(0))], { type: 'image/png' });
}
let CRC;
function crc32(bytes) {
  if (!CRC) { CRC = new Uint32Array(256); for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; CRC[n] = c >>> 0; } }
  let c = 0xffffffff;
  for (let i = 0; i < bytes.length; i++) c = CRC[(c ^ bytes[i]) & 255] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}
function chunk(type, data) {
  const out = new Uint8Array(12 + data.length), dv = new DataView(out.buffer);
  dv.setUint32(0, data.length);
  for (let i = 0; i < 4; i++) out[4 + i] = type.charCodeAt(i);
  out.set(data, 8);
  dv.setUint32(8 + data.length, crc32(out.subarray(4, 8 + data.length)));
  return out;
}
