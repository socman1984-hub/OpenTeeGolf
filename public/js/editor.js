// Course editor: a top-down map of a built course where holes (tees, aim points, green center,
// pins) and shapes (greens, fairways, bunkers, tee boxes, water, penalty areas, OOB) can be edited.
import { makeCourseData, TEE_ORDER, PIN_DAYS } from './coursefile.js';
import { smoothPoly } from './course.js';
import { treesFromCanopy, treesFromPhoto, removeTrees, addMappedTrees, addTreeAreas, TREE_SIZES, TREE_SPECIES, TREE_MIXES, AREA_HEIGHTS } from './treeplace.js';
import { MOW_PATTERNS, MOW_WIDTH, courseMowing, mowShade, GRASS_HEIGHTS, courseGrass } from './mowing.js';
import { BUILDING_TYPES, classifyBuildings, footprintInfo } from './buildingtypes.js';

const $ = (id) => document.getElementById(id);
const esc = (s) => String(s ?? '').replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[c]);
const courseId = new URLSearchParams(location.search).get('course');
const M_TO_YD = 1.09361;

const TEE_COLORS = { Black: '#1d1d1d', Blue: '#2f62e8', White: '#ffffff', Green: '#22aa33', Yellow: '#eecc22', Red: '#dd2222', Junior: '#ff8800', Par3: '#9a9a9a' };
const SHAPES = {
  fairway: { label: 'Fairway', fill: 'rgba(118,180,70,0.92)', stroke: '#a6dc76' },
  green: { label: 'Green', fill: 'rgba(98,214,92,0.95)', stroke: '#c4ffbd' },
  tee: { label: 'Tee box', fill: 'rgba(136,196,98,0.95)', stroke: '#dcffd0' },
  bunker: { label: 'Bunker', fill: 'rgba(232,215,165,0.97)', stroke: '#fff3cf' },
  water: { label: 'Water', fill: 'rgba(55,120,175,0.92)', stroke: '#8fd0ff' },
  penalty: { label: 'Penalty area', fill: 'rgba(220,50,40,0.14)', stroke: '#ff5a48' },
  oob: { label: 'Out of bounds', fill: null, stroke: '#ffffff' },
  trees: { label: 'Tree area', fill: 'rgba(40,120,40,0.25)', stroke: '#8fe37f' },
};
const DRAW_ORDER = ['oob', 'trees', 'fairway', 'tee', 'green', 'bunker', 'water', 'penalty'];
const FEATURE_KINDS = ['green', 'fairway', 'bunker', 'tee', 'water'];

let courseData = null, real = null, grid = null, hill = null, photo = null, showPhoto = true;
let holes = [], shapes = [], passthrough = {};
// Trees: the ones the game finds (canopy heights, OpenStreetMap) and the player's own
let userTrees = [];          // [{ x, z, size }]
let removedTrees = [];       // found trees taken out: [[x, z], ...]
let baseTrees = [];          // found trees, for showing
let shownTrees = [], shownKey = '';
let showTrees = true;
// Buildings around the course (OpenStreetMap), what each is (the player's choices: { "x,z": type }),
// and whether they're shown
let buildings = [], buildingTypes = {}, showBuildings = true, buildingKinds = null, kindsKey = '';
const treeOpts = { size: 'medium', density: 'medium', kind: 'auto', areaKind: 'auto', hMin: AREA_HEIGHTS.min, hMax: AREA_HEIGHTS.max };
// Map colours for each species (planted trees are drawn in them; 'auto' ones in the usual green)
const SPECIES_COLOR = { oak: '#5f9e3c', maple: '#c86b2d', ash: '#9fcf5a', birch: '#f2f0dc', aspen: '#e8d74a', poplar: '#3fb5a3', willow: '#a7e07a', pine: '#2f6b4a', spruce: '#3d6f9e' };
const speciesOptions = (sel) => [...TREE_MIXES, ...TREE_SPECIES].map((o) => `<option value="${o.id}" ${o.id === (sel || 'auto') ? 'selected' : ''}>${esc(o.label)}</option>`).join('');
const course = { name: '', location: '', stimp: 10, mowing: courseMowing(null), grass: courseGrass(null) };
const sel = { hole: 0, shape: -1, tree: -1, found: null, removed: -1, many: [], building: -1 }; // many: found trees picked with Shift
const view = { cx: 0, cz: 0, scale: 1 };
let mode = null;   // { type: 'place', kind, key } | { type: 'draw', kind, pts }
let drag = null;
let hover = null;
const undoStack = [];
let dirty = false;
let editable = true;

const canvas = $('map');
const ctx = canvas.getContext('2d');

// ------------------------------------------------------------------ geometry

const pointInPoly = (poly, x, z) => {
  let inside = false;
  for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) {
    const [xi, zi] = poly[i], [xj, zj] = poly[j];
    if (zi > z !== zj > z && x < ((xj - xi) * (z - zi)) / (zj - zi) + xi) inside = !inside;
  }
  return inside;
};
const polyArea = (poly) => {
  let a = 0;
  for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) a += poly[j][0] * poly[i][1] - poly[i][0] * poly[j][1];
  return Math.abs(a / 2);
};
const centroid = (poly) => [poly.reduce((s, p) => s + p[0], 0) / poly.length, poly.reduce((s, p) => s + p[1], 0) / poly.length];
const segDistPx = (p, a, b) => {
  const dx = b[0] - a[0], dy = b[1] - a[1], l2 = dx * dx + dy * dy;
  const t = l2 ? Math.max(0, Math.min(1, ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / l2)) : 0;
  return Math.hypot(p[0] - a[0] - dx * t, p[1] - a[1] - dy * t);
};

function heightAt(x, z) {
  if (!grid) return 0;
  const fx = Math.max(0, Math.min(grid.nx - 1.001, (x - grid.minX) / grid.cell));
  const fz = Math.max(0, Math.min(grid.nz - 1.001, (z - grid.minZ) / grid.cell));
  const i = Math.floor(fx), j = Math.floor(fz), u = fx - i, v = fz - j, k = j * grid.nx + i, h = grid.h;
  return h[k] * (1 - u) * (1 - v) + h[k + 1] * u * (1 - v) + h[k + grid.nx] * (1 - u) * v + h[k + grid.nx + 1] * u * v;
}

// ------------------------------------------------------------------ view transform

const W = () => canvas.clientWidth, H = () => canvas.clientHeight;
const toScreen = (x, z) => [(x - view.cx) * view.scale + W() / 2, H() / 2 - (z - view.cz) * view.scale];
const toWorld = (sx, sy) => [(sx - W() / 2) / view.scale + view.cx, (H() / 2 - sy) / view.scale + view.cz];

function fit(points, pad = 40) {
  if (!points.length) return;
  const xs = points.map((p) => p[0]), zs = points.map((p) => p[1]);
  const minX = Math.min(...xs), maxX = Math.max(...xs), minZ = Math.min(...zs), maxZ = Math.max(...zs);
  view.cx = (minX + maxX) / 2; view.cz = (minZ + maxZ) / 2;
  view.scale = Math.min((W() - pad * 2) / Math.max(20, maxX - minX), (H() - pad * 2) / Math.max(20, maxZ - minZ));
  draw();
}
const holePoints = (h) => [...Object.values(h.tees), ...h.aims, h.green, ...Object.values(h.pins)].filter(Boolean).map((p) => [p.x, p.z]);
const fitHole = () => holes[sel.hole] && fit(holePoints(holes[sel.hole]), 70);
function fitAll() {
  const oob = shapes.find((s) => s.kind === 'oob');
  fit(oob ? oob.pts : holes.flatMap(holePoints));
}

// ------------------------------------------------------------------ load / save

async function load() {
  if (!courseId) { $('status').textContent = 'No course selected'; return; }
  const [g, r, list] = await Promise.all([
    fetch(`/api/courses/${courseId}/data`).then((x) => (x.ok ? x.json() : Promise.reject(new Error('Course not found')))),
    fetch(`/api/courses/${courseId}/real`).then((x) => (x.ok ? x.json() : null)),
    fetch('/api/courses').then((x) => x.json()),
  ]);
  courseData = g; real = r;
  editable = !!list.courses.find((c) => c.id === courseId)?.editable;
  course.name = g.CourseName || ''; course.location = g.Location || ''; course.stimp = g.DefaultStimp || 10;
  course.mowing = courseMowing(g);
  course.grass = courseGrass(g);
  $('c-rough-h').value = course.grass.rough; $('c-native-h').value = course.grass.native;
  holes = g.Holes.map((h) => {
    const tees = {}, aims = [];
    let green = null;
    for (const t of h.Tees) {
      if (!t.Position) continue;
      const p = { x: t.Position.x, z: t.Position.z };
      if (t.TeeType.startsWith('AimPoint')) aims[+t.TeeType.slice(8) - 1] = p;
      else if (t.TeeType === 'GreenCenterPoint') green = p;
      else tees[t.TeeType] = p;
    }
    const pins = {};
    for (const p of h.Pins) if (p.Position) pins[p.Day] = { x: p.Position.x, z: p.Position.z };
    const pl = Object.values(pins);
    if (!green && pl.length) green = { x: pl.reduce((s, p) => s + p.x, 0) / pl.length, z: pl.reduce((s, p) => s + p.z, 0) / pl.length };
    return { number: h.HoleNumber, par: h.Par, index: h.Index, tees, aims: aims.filter(Boolean), green: green || { x: 0, z: 0 }, pins };
  });
  shapes = [];
  if (r?.features) {
    for (const [kind, list2] of Object.entries(r.features)) {
      if (FEATURE_KINDS.includes(kind)) list2.forEach((f) => shapes.push({ kind, pts: f.pts.map((p) => [...p]), inner: !!f.hole, ...(f.flow ? { flow: true } : {}) }));
      else passthrough[kind] = list2;
    }
  }
  for (const a of r?.treeAreas || []) shapes.push({ kind: 'trees', pts: a.pts.map((p) => [...p]), density: a.density || 'medium', species: a.kind || 'auto', hMin: a.hMin, hMax: a.hMax });
  userTrees = (r?.userTrees || []).map((t) => ({ x: t.x, z: t.z, size: t.size || 'medium', kind: t.kind || 'auto' }));
  removedTrees = (r?.removedTrees || []).map(([x, z]) => [x, z]);
  for (const hz of g.Hazards || []) shapes.push({ kind: 'penalty', pts: hz.coords.map((c) => [c.x, c.z]), innerOOB: !!hz.innerOOB });
  if (g.pOOB?.coords?.length > 2) shapes.push({ kind: 'oob', pts: g.pOOB.coords.map((c) => [c.x, c.z]) });
  if (r?.grid) {
    const bytes = Uint8Array.from(atob(r.grid.heights), (c) => c.charCodeAt(0));
    grid = { ...r.grid, h: new Float32Array(bytes.buffer) };
    hill = makeHillshade(grid);
  }
  buildingTypes = { ...(r?.buildingTypes || {}) };
  fetch(`/api/courses/${courseId}/buildings`).then((x) => (x.ok ? x.json() : [])).catch(() => [])
    .then((list) => { buildings = Array.isArray(list) ? list.filter((b) => b.pts?.length >= 4) : []; renderBuildingInfo(); draw(); });
  document.title = `${course.name} · Course Editor`;
  $('c-name').value = course.name; $('c-location').value = course.location; $('c-stimp').value = course.stimp;
  showMowing();
  $('play').href = `/?course=${encodeURIComponent(courseId)}`;
  if (!editable) setStatus('Course not found — it may have been deleted.');
  else if (g.GolfSimBuilder?.needsReview) setStatus('Some parts were guessed — check tees, pins and par on each hole.');
  renderDrawButtons();
  renderHoles();
  resize();
  fitHole();
  photoReady = grid ? loadPhoto() : Promise.resolve(null);
  loadTrees();
}

// The trees the game will plant: from the canopy height map (if the course has one) and
// OpenStreetMap, kept off greens, fairways, tees, bunkers and water as in the game
async function loadTrees() {
  const NO_CANOPY = new Set(['green', 'fairway', 'tee', 'bunker', 'water']), NO_MAPPED = new Set(['green', 'tee', 'bunker', 'water']);
  const inShape = (kinds) => (x, z) => shapes.some((s) => kinds.has(s.kind) && !s.inner && pointInPoly(s.pts, x, z));
  const onCanopyBad = inShape(NO_CANOPY), onMappedBad = inShape(NO_MAPPED);
  let canopy = null;
  try {
    setTreeInfo('Loading tree heights…');
    const cm = await fetch(`/api/courses/${courseId}/canopy`).then((x) => (x.ok ? x.json() : null));
    if (cm?.available) {
      const buf = await fetch(`/api/courses/${courseId}/canopy.bin`).then((x) => (x.ok ? x.arrayBuffer() : null));
      if (buf) canopy = { ...cm, heights: new Uint8Array(buf) };
    }
  } catch { /* shown without */ }
  const found = canopy ? treesFromCanopy(canopy, (x, z) => !onCanopyBad(x, z)) : [];
  const heightAt = canopy ? (x, z) => {
    const i = Math.floor((x - canopy.minX) / canopy.res), j = Math.floor((canopy.maxZ - z) / canopy.res);
    return i >= 0 && j >= 0 && i < canopy.w && j < canopy.h ? canopy.heights[j * canopy.w + i] : 0;
  } : undefined;
  addMappedTrees(found, real?.trees || [], real?.treeRows || [], { heightAt, allowed: (x, z) => !onMappedBad(x, z) });
  // Trees seen in the aerial photo: the ones the game planted last time it loaded the course, or
  // (not played since the map was built) found here much as the game does, 2 m clear of mown
  // grass, sand and water
  const saved = await fetch(`/api/courses/${courseId}/phototrees`).then((x) => (x.ok ? x.json() : null)).catch(() => null);
  const raster = saved?.trees ? null : await photoReady;
  if (saved?.trees) found.push(...saved.trees.map(([x, z, h, r]) => ({ x, z, h, r, kind: null, photo: true })));
  else if (raster) {
    setTreeInfo('Looking for trees in the aerial photo…');
    await new Promise((r) => setTimeout(r, 30));
    const clear = clearMask(raster);
    const allowed = (x, z) => {
      const i = Math.floor(x - raster.minX), j = Math.floor(raster.maxZ - z);
      return i >= 0 && j >= 0 && i < clear.w && j < clear.h && !clear.data[(j * clear.w + i) * 4 + 3];
    };
    found.push(...treesFromPhoto(raster, { allowed, existing: found, heightAt }));
  }
  baseTrees = found;
  treeEnv.heightAt = heightAt;
  const onFairway = inShape(new Set(['fairway']));
  treeEnv.allowed = (x, z) => !onMappedBad(x, z) && !onFairway(x, z); // (for tree areas)
  treeEnv.note = canopy ? '' : ' (this course has no tree-height map)';
  shownKey = '';
  draw();
  // (straight after building a course: go through the trees in the line of play)
  if (new URLSearchParams(location.search).get('review') === 'trees' && !review.started) startReview();
}

// ------------------------------------------------------------------ tree review
// Trees found automatically (tree-height map, OpenStreetMap, aerial photo) that stand in the line
// of play: trunk within 4 m (plus half the crown, up to 3 m more) of the line from a hole's back
// tee through its aim points to the green - in the way of a normal shot. Some are real and part
// of the hole; some are mistakes (or would make the hole unplayable). Trees close together on the
// same hole are one group; the player goes through the groups one by one: keep or remove.
const review = { started: false, list: [], i: 0, kept: 0, removed: 0 };
function treesInPlay() {
  const segDist = (x, z, a, b) => {
    const dx = b.x - a.x, dz = b.z - a.z, l2 = dx * dx + dz * dz;
    const t = l2 ? Math.max(0, Math.min(1, ((x - a.x) * dx + (z - a.z) * dz) / l2)) : 0;
    return Math.hypot(x - a.x - dx * t, z - a.z - dz * t);
  };
  const lines = holes.map((h) => [TEE_ORDER.map((k) => h.tees[k]).find(Boolean), ...h.aims, h.green].filter(Boolean));
  const out = [];
  for (const t of plannedTrees()) {
    if (t.user) continue;
    let best = null;
    lines.forEach((pts, hi) => {
      for (let k = 1; k < pts.length; k++) {
        const d = segDist(t.x, t.z, pts[k - 1], pts[k]);
        if (d < 4 + Math.min(3, (t.r || 3) * 0.5) && (!best || d < best.d)) best = { d, hole: hi };
      }
    });
    if (best) out.push({ t, ...best });
  }
  // Group trees within 8 m of each other on the same hole
  const groups = [];
  for (const f of out) {
    const g = groups.find((gr) => gr.hole === f.hole && gr.trees.some((o) => Math.hypot(o.x - f.t.x, o.z - f.t.z) < 8));
    if (g) { g.trees.push(f.t); g.d = Math.min(g.d, f.d); } else groups.push({ hole: f.hole, d: f.d, trees: [f.t] });
  }
  // hole by hole
  return groups.sort((a, b) => holes[a.hole].number - holes[b.hole].number || a.d - b.d);
}
function startReview() {
  review.started = true;
  review.list = treesInPlay(); review.i = 0; review.kept = 0; review.removed = 0;
  showReview();
}
function showReview() {
  const box = $('review'), item = review.list[review.i];
  box.classList.remove('hidden');
  if (!item) {
    const none = !review.list.length;
    box.innerHTML = none
      ? `<h3>No trees in the line of play</h3><div class="muted small">None of the trees found automatically stand in the way of a normal shot.</div>
        <div class="btn-row"><button id="rv-close">Close</button></div>`
      : `<h3>Review done</h3><div class="muted small">${review.removed} removed, ${review.kept} kept. Removed trees show as a red cross: click one to put it back.</div>
        <div class="btn-row"><button id="rv-save" class="primary">Save the course</button><button id="rv-close">Close</button></div>`;
    $('rv-close').onclick = () => { box.classList.add('hidden'); clearTreeSel(); renderShapePanel(); draw(); };
    if ($('rv-save')) $('rv-save').onclick = async () => { await save(); box.classList.add('hidden'); };
    clearTreeSel(); renderShapePanel(); draw();
    return;
  }
  const { trees, hole, d } = item, h = holes[hole], n = trees.length;
  // Show the trees, their hole selected (its line drawn), the nearest one picked
  sel.hole = hole; sel.shape = -1; clearTreeSel(); sel.found = trees[0];
  renderHoles(); renderShapePanel();
  const xs = trees.map((t) => t.x), zs = trees.map((t) => t.z);
  view.cx = (Math.min(...xs) + Math.max(...xs)) / 2; view.cz = (Math.min(...zs) + Math.max(...zs)) / 2;
  view.scale = Math.min(W(), H()) / Math.max(90, (Math.max(...xs) - Math.min(...xs)) * 2.5, (Math.max(...zs) - Math.min(...zs)) * 2.5);
  draw();
  const photo = trees.filter((t) => t.photo).length;
  const what = n === 1 ? `A tree ${photo ? 'guessed from the aerial photo' : 'found from the tree-height map or OpenStreetMap'}, about ${Math.round(trees[0].h)} m tall,`
    : `${n} trees together (${photo === n ? 'all' : photo ? `${photo}` : 'none'} guessed from the aerial photo),`;
  box.innerHTML = `<h3>${review.i + 1} of ${review.list.length} · hole ${h.number}</h3>
    <div class="muted small">${what} ${d < 2 ? 'right on' : `${Math.round(d)} m from`} the line of play. ${n === 1 ? 'Does it' : 'Do they'} belong there?</div>
    <div class="btn-row">
      <button id="rv-keep" class="keep">Keep</button><button id="rv-remove" class="remove">Remove</button>
      <button id="rv-keep-all">Keep the rest</button><button id="rv-remove-all">Remove the rest</button>
    </div>`;
  const remove = (item2) => { for (const t of item2.trees) removedTrees.push([Math.round(t.x * 100) / 100, Math.round(t.z * 100) / 100]); };
  const count = (from) => review.list.slice(from).reduce((s2, g) => s2 + g.trees.length, 0);
  $('rv-keep').onclick = () => { review.kept += n; review.i++; showReview(); };
  $('rv-remove').onclick = () => { change(); remove(item); review.removed += n; review.i++; showReview(); };
  $('rv-keep-all').onclick = () => { review.kept += count(review.i); review.i = review.list.length; showReview(); };
  $('rv-remove-all').onclick = () => {
    change();
    for (; review.i < review.list.length; review.i++) { remove(review.list[review.i]); review.removed += review.list[review.i].trees.length; }
    showReview();
  };
}
// The flagged trees still to look at, ringed on the map (the group being looked at in yellow)
function drawReview() {
  if (!review.started || review.i >= review.list.length) return;
  ctx.strokeStyle = '#ff8a3d'; ctx.lineWidth = 2; ctx.setLineDash([4, 3]);
  for (let k = review.i; k < review.list.length; k++) for (const t of review.list[k].trees) {
    const [x, y] = toScreen(t.x, t.z);
    ctx.strokeStyle = k === review.i ? '#ffd84a' : '#ff8a3d';
    ctx.beginPath(); ctx.arc(x, y, Math.max(7, t.r * view.scale + 3), 0, 7); ctx.stroke();
  }
  ctx.setLineDash([]);
}
const treeEnv = { heightAt: undefined, allowed: () => true, note: '' };
let photoReady = Promise.resolve(null);
// Where photo trees may not go: mown grass, sand and water shapes and 2 m around them, drawn at 1 m
function clearMask(raster) {
  const c = document.createElement('canvas');
  c.width = raster.w; c.height = raster.h;
  const x = c.getContext('2d');
  x.setTransform(1, 0, 0, -1, -raster.minX, raster.maxZ);
  x.fillStyle = x.strokeStyle = '#000'; x.lineWidth = 4; x.lineJoin = 'round';
  for (const s of shapes) {
    if (!['green', 'fairway', 'tee', 'bunker', 'water'].includes(s.kind) || s.inner) continue;
    x.beginPath(); s.pts.forEach(([px, pz], i) => (i ? x.lineTo(px, pz) : x.moveTo(px, pz))); x.closePath();
    x.fill(); x.stroke();
  }
  return { data: x.getImageData(0, 0, c.width, c.height).data, w: c.width, h: c.height };
}
// A tree area as saved and planted: its outline, density, species (or mix) and height range
// (the shape's own kind is 'trees', so its species is kept as species)
const areaData = (s) => ({
  pts: s.pts, density: s.density || 'medium',
  ...(s.species && s.species !== 'auto' ? { kind: s.species } : {}),
  ...(Number.isFinite(s.hMin) && Number.isFinite(s.hMax) ? { hMin: s.hMin, hMax: s.hMax } : {}),
});

// Found trees plus the player's, recomputed only when the player's trees or areas change
function plannedTrees() {
  const areas = shapes.filter((s) => s.kind === 'trees').map(areaData);
  const key = JSON.stringify([userTrees, areas, baseTrees.length, removedTrees]);
  if (key !== shownKey) {
    const mine = [];
    addTreeAreas(mine, userTrees, areas, { heightAt: treeEnv.heightAt, allowed: treeEnv.allowed });
    // (photo trees give way to the player's trees, as in the game)
    const near = (t) => mine.some((m) => Math.abs(m.x - t.x) < 4 && Math.abs(m.z - t.z) < 4 && Math.hypot(m.x - t.x, m.z - t.z) < 4);
    const found = removeTrees(baseTrees, removedTrees).filter((t) => !t.photo || !mine.length || !near(t));
    shownTrees = found.concat(mine);
    shownKey = key;
    const photo = found.filter((t) => t.photo).length;
    setTreeInfo(`${shownTrees.length.toLocaleString()} trees · ${photo.toLocaleString()} guessed from the aerial photo · ${mine.length.toLocaleString()} added by you`
      + `${removedTrees.length ? ` · ${removedTrees.length.toLocaleString()} removed` : ''}${treeEnv.note}`);
  }
  return shownTrees;
}
function setTreeInfo(t) { const el = $('tree-info'); if (el) el.textContent = t; }

// Aerial photo background (same imagery as the game), drawn in course space
async function loadPhoto() {
  const res0 = await fetch(`/api/courses/${courseId}/satellite`).catch(() => null);
  if (!res0?.ok) return null;
  const manifest = await res0.json();
  if (!manifest.chunks) return null;
  const imgs = await Promise.all(manifest.chunks.map((c) => new Promise((resolve) => {
    const img = new Image();
    img.onload = () => resolve({ ...c, img });
    img.onerror = () => resolve(null);
    img.src = `/api/courses/${courseId}/satellite/${c.file}`;
  })));
  const [a0, a1, a2] = manifest.geo.lat, [b0, b1, b2] = manifest.geo.lon, det = a1 * b2 - a2 * b1;
  const toCourse = (lat, lon) => { const dl = lat - a0, dn = lon - b0; return [(b2 * dl - a2 * dn) / det, (a1 * dn - b1 * dl) / det]; };
  const maxZ = grid.minZ + (grid.nz - 1) * grid.cell, wM = (grid.nx - 1) * grid.cell, hM = (grid.nz - 1) * grid.cell;
  const res = Math.max(0.5, Math.max(wM, hM) / 8192);
  const c = document.createElement('canvas');
  c.width = Math.ceil(wM / res); c.height = Math.ceil(hM / res);
  const x = c.getContext('2d');
  const raw = document.createElement('canvas');
  raw.width = c.width; raw.height = c.height;
  const rx = raw.getContext('2d');
  for (const ch of imgs) {
    if (!ch) continue;
    const e = ch.extent;
    const map = (u, v) => {
      const [cx, cz] = toCourse(e.maxLat - (v / ch.height) * (e.maxLat - e.minLat), e.minLon + (u / ch.width) * (e.maxLon - e.minLon));
      return [(cx - grid.minX) / res, (maxZ - cz) / res];
    };
    const p0 = map(0, 0), pu = map(1, 0), pv = map(0, 1);
    rx.setTransform(pu[0] - p0[0], pu[1] - p0[1], pv[0] - p0[0], pv[1] - p0[1], p0[0], p0[1]);
    rx.drawImage(ch.img, 0, 0);
  }
  x.filter = 'saturate(1.25)';
  x.drawImage(raw, 0, 0);
  photo = c;
  $('toggle-photo').classList.remove('hidden');
  draw();
  // A plain 1 m copy for finding trees, as the game does
  const t = document.createElement('canvas');
  t.width = Math.ceil(wM); t.height = Math.ceil(hM);
  t.getContext('2d').drawImage(raw, 0, 0, t.width, t.height);
  return { data: t.getContext('2d').getImageData(0, 0, t.width, t.height).data, w: t.width, h: t.height, res: 1, minX: grid.minX, maxZ };
}

function makeHillshade(g) {
  const c = document.createElement('canvas');
  c.width = g.nx; c.height = g.nz;
  const x = c.getContext('2d');
  const img = x.createImageData(g.nx, g.nz);
  let lo = Infinity, hi = -Infinity;
  for (const v of g.h) { lo = Math.min(lo, v); hi = Math.max(hi, v); }
  for (let j = 1; j < g.nz - 1; j++) for (let i = 1; i < g.nx - 1; i++) {
    const k = j * g.nx + i;
    const gx = (g.h[k + 1] - g.h[k - 1]) / (2 * g.cell), gz = (g.h[k + g.nx] - g.h[k - g.nx]) / (2 * g.cell);
    const shade = Math.max(0.35, Math.min(1.25, 0.85 + (-gx + gz) * 1.6));
    const t = (g.h[k] - lo) / (hi - lo || 1);
    const o = ((g.nz - 1 - j) * g.nx + i) * 4;
    img.data[o] = (50 + 25 * t) * shade; img.data[o + 1] = (88 + 30 * t) * shade; img.data[o + 2] = (40 + 10 * t) * shade; img.data[o + 3] = 255;
  }
  x.putImageData(img, 0, 0);
  return c;
}

async function save() {
  if (!editable) return setStatus('This course can’t be saved.');
  const r2 = (v) => Math.round(v * 100) / 100;
  const P = (p) => ({ x: r2(p.x), y: Math.round(heightAt(p.x, p.z) * 1000) / 1000, z: r2(p.z) });
  const sorted = [...holes].sort((a, b) => a.number - b.number);
  const out = makeCourseData({
    name: course.name, location: course.location, stimp: course.stimp, mowing: { ...course.mowing }, grass: { rough: course.grass.rough, native: course.grass.native },
    lat: courseData.CourseLat, lon: courseData.CourseLong, altitudeM: (courseData.altitudeV2 || 0) * 0.3048,
    designer: courseData.Designer || 'OpenTeeGolf course builder',
    holes: sorted.map((h) => ({
      number: h.number, par: h.par, index: h.index,
      tees: Object.fromEntries(Object.entries(h.tees).map(([k, p]) => [k, P(p)])),
      aims: h.aims.map(P), greenCenter: P(h.green),
      pins: Object.fromEntries(Object.entries(h.pins).map(([k, p]) => [k, P(p)])),
    })),
    hazards: shapes.filter((s) => s.kind === 'penalty').map((s) => ({ coords: s.pts.map(([x, z]) => P({ x, z })), innerOOB: !!s.innerOOB })),
    oob: (shapes.find((s) => s.kind === 'oob')?.pts || []).map(([x, z]) => P({ x, z })),
    builder: { ...(courseData.GolfSimBuilder || {}), needsReview: false, edited: new Date().toISOString() },
  });
  const features = { ...passthrough };
  for (const k of FEATURE_KINDS) features[k] = shapes.filter((s) => s.kind === k).map((s) => ({ hole: !!s.inner, pts: s.pts.map(([x, z]) => [r2(x), r2(z)]), ...(k === 'water' && s.flow ? { flow: true } : {}) }));
  const treeAreas = shapes.filter((s) => s.kind === 'trees').map((s) => ({ ...areaData(s), pts: s.pts.map(([x, z]) => [r2(x), r2(z)]) }));
  setStatus('Saving…');
  const res = await fetch(`/api/courses/${courseId}`, {
    method: 'PUT', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ course: out, features, userTrees, treeAreas, removedTrees, buildingTypes }),
  });
  const body = await res.json().catch(() => ({}));
  if (!res.ok) return setStatus(`Save failed: ${body.error || res.status}`);
  courseData = out;
  setDirty(false);
  setStatus(`Saved ${new Date().toLocaleTimeString()}`);
}

function setStatus(t) { $('status').textContent = t; }
function setDirty(d) { dirty = d; $('dirty').textContent = d ? '● unsaved changes' : ''; }

// ------------------------------------------------------------------ undo

const snapshot = () => JSON.stringify({ holes, shapes, course, userTrees, removedTrees, buildingTypes });
function change() {
  undoStack.push(snapshot());
  if (undoStack.length > 100) undoStack.shift();
  setDirty(true);
}
function undo() {
  const s = undoStack.pop();
  if (!s) return;
  const o = JSON.parse(s);
  holes = o.holes; shapes = o.shapes; Object.assign(course, o.course); userTrees = o.userTrees || []; removedTrees = o.removedTrees || []; buildingTypes = o.buildingTypes || {};
  sel.hole = Math.min(sel.hole, holes.length - 1); sel.shape = -1; sel.tree = -1; sel.found = null; sel.removed = -1;
  setDirty(true);
  renderHoles(); renderShapePanel(); showMowing(); draw();
}

// ------------------------------------------------------------------ drawing

function resize() {
  const dpr = devicePixelRatio || 1;
  canvas.width = W() * dpr; canvas.height = H() * dpr;
  draw();
}
window.addEventListener('resize', resize);

// Shapes are drawn with the same rounded curves the game uses; their points stay the handles
function tracePoly(pts, smooth = true) {
  if (smooth && pts.length > 2) pts = smoothPoly(pts);
  ctx.beginPath();
  pts.forEach(([x, z], i) => { const [sx, sy] = toScreen(x, z); i ? ctx.lineTo(sx, sy) : ctx.moveTo(sx, sy); });
  ctx.closePath();
}

function draw() {
  const dpr = devicePixelRatio || 1;
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.fillStyle = '#2b4226';
  ctx.fillRect(0, 0, W(), H());
  const bg = photo && showPhoto ? photo : hill;
  if (bg) {
    const [sx, sy] = toScreen(grid.minX, grid.minZ + (grid.nz - 1) * grid.cell);
    ctx.imageSmoothingEnabled = true;
    ctx.drawImage(bg, sx, sy, (grid.nx - 1) * grid.cell * view.scale, (grid.nz - 1) * grid.cell * view.scale);
  }
  // Over the aerial photo, shapes are see-through so real features can be traced
  ctx.globalAlpha = photo && showPhoto ? 0.45 : 1;
  // Shapes, back to front
  for (const kind of DRAW_ORDER) {
    shapes.forEach((s, i) => {
      if (s.kind !== kind) return;
      const st = SHAPES[kind];
      tracePoly(s.pts, kind !== 'oob'); // the OOB line is used as drawn
      if (st.fill) { ctx.fillStyle = s.inner ? 'rgba(60,100,45,0.95)' : st.fill; ctx.fill(); }
      ctx.lineWidth = i === sel.shape ? 2.5 : kind === 'oob' || kind === 'penalty' ? 2 : 1;
      ctx.setLineDash(kind === 'oob' ? [8, 6] : []);
      ctx.strokeStyle = i === sel.shape ? '#ffd84a' : st.stroke;
      ctx.stroke();
      ctx.setLineDash([]);
    });
  }
  ctx.globalAlpha = 1;
  if (showBuildings) drawBuildings();
  if (showTrees) drawTrees();
  // Holes: others faint, the selected one with handles
  holes.forEach((h, i) => { if (i !== sel.hole) drawHole(h, false); });
  if (holes[sel.hole]) drawHole(holes[sel.hole], true);
  if (showTrees) drawReview();
  // Selected shape vertices
  const s = shapes[sel.shape];
  if (s) {
    s.pts.forEach((p, i) => {
      const [a, b] = toScreen(p[0], p[1]);
      const q = toScreen(...s.pts[(i + 1) % s.pts.length]);
      ctx.fillStyle = 'rgba(255,255,255,0.35)';
      ctx.beginPath(); ctx.arc((a + q[0]) / 2, (b + q[1]) / 2, 3.5, 0, 7); ctx.fill();
      ctx.fillStyle = '#fff'; ctx.strokeStyle = '#000'; ctx.lineWidth = 1;
      ctx.fillRect(a - 4, b - 4, 8, 8); ctx.strokeRect(a - 4, b - 4, 8, 8);
    });
  }
  // Shape being drawn
  if (mode?.type === 'draw' && mode.pts.length) {
    ctx.strokeStyle = SHAPES[mode.kind].stroke; ctx.lineWidth = 2; ctx.setLineDash([5, 4]);
    ctx.beginPath();
    mode.pts.forEach(([x, z], i) => { const [sx, sy] = toScreen(x, z); i ? ctx.lineTo(sx, sy) : ctx.moveTo(sx, sy); });
    if (hover) ctx.lineTo(hover[0], hover[1]);
    ctx.stroke(); ctx.setLineDash([]);
    mode.pts.forEach(([x, z]) => { const [sx, sy] = toScreen(x, z); ctx.fillStyle = '#fff'; ctx.fillRect(sx - 3, sy - 3, 6, 6); });
  }
  // Scale bar
  const m = [10, 25, 50, 100, 200, 500].find((v) => v * view.scale > 70) || 1000;
  ctx.fillStyle = '#fff'; ctx.fillRect(16, H() - 22, m * view.scale, 3);
  ctx.font = '12px system-ui'; ctx.fillText(`${m} m`, 16, H() - 28);
}

// Trees as crowns seen from above: found ones dark, the player's brighter, the selected one ringed
function drawTrees() {
  const list = plannedTrees();
  const w = W(), h = H();
  // (all crowns of a kind in one path: far quicker than a path per tree)
  // (planted trees of a chosen species in that species' colour)
  // (trees guessed from the aerial photo in amber with a dashed edge, so they're easy to check)
  const found = new Path2D(), guessed = new Path2D(), mine = new Path2D(), foundDots = [], guessedDots = [], mineDots = [], bySpecies = {};
  for (const t of list) {
    const [sx, sy] = toScreen(t.x, t.z), r = t.r * view.scale;
    if (sx < -r || sy < -r || sx > w + r || sy > h + r) continue;
    if (r < 2) { (t.user ? mineDots : t.photo ? guessedDots : foundDots).push(sx, sy); continue; }
    const path = t.photo ? guessed : !t.user ? found : t.kind && SPECIES_COLOR[t.kind] ? (bySpecies[t.kind] ||= new Path2D()) : mine;
    path.moveTo(sx + r, sy); path.arc(sx, sy, r, 0, 6.2832);
  }
  ctx.lineWidth = 1;
  ctx.fillStyle = 'rgba(22,72,24,0.55)'; ctx.fill(found);
  ctx.strokeStyle = 'rgba(150,220,130,0.55)'; ctx.stroke(found);
  ctx.fillStyle = 'rgba(150,105,20,0.5)'; ctx.fill(guessed);
  ctx.setLineDash([3, 3]); ctx.strokeStyle = 'rgba(255,200,90,0.9)'; ctx.stroke(guessed); ctx.setLineDash([]);
  ctx.fillStyle = 'rgba(80,175,70,0.7)'; ctx.fill(mine);
  ctx.strokeStyle = '#d9ffbf'; ctx.stroke(mine);
  ctx.globalAlpha = 0.75;
  for (const [k, path] of Object.entries(bySpecies)) { ctx.fillStyle = SPECIES_COLOR[k]; ctx.fill(path); }
  ctx.globalAlpha = 1;
  ctx.strokeStyle = '#ffffff';
  for (const path of Object.values(bySpecies)) ctx.stroke(path);
  ctx.fillStyle = 'rgba(30,90,30,0.85)';
  for (let i = 0; i < foundDots.length; i += 2) ctx.fillRect(foundDots[i] - 1.5, foundDots[i + 1] - 1.5, 3, 3);
  ctx.fillStyle = 'rgba(230,160,40,0.9)';
  for (let i = 0; i < guessedDots.length; i += 2) ctx.fillRect(guessedDots[i] - 1.5, guessedDots[i + 1] - 1.5, 3, 3);
  ctx.fillStyle = '#b9f59a';
  for (let i = 0; i < mineDots.length; i += 2) ctx.fillRect(mineDots[i] - 1.5, mineDots[i + 1] - 1.5, 3, 3);
  // Found trees taken out: a faint cross (click it to put the tree back)
  if (view.scale > 0.6) {
    ctx.lineWidth = 1.5; ctx.strokeStyle = 'rgba(255,120,100,0.75)';
    ctx.beginPath();
    for (const [x, z] of removedTrees) {
      const [sx, sy] = toScreen(x, z);
      if (sx < -6 || sy < -6 || sx > w + 6 || sy > h + 6) continue;
      ctx.moveTo(sx - 4, sy - 4); ctx.lineTo(sx + 4, sy + 4); ctx.moveTo(sx + 4, sy - 4); ctx.lineTo(sx - 4, sy + 4);
    }
    ctx.stroke();
  }
  // Trees picked with Shift: a ring round each, and the box being dragged
  ctx.lineWidth = 2; ctx.strokeStyle = '#ffd84a';
  for (const t of sel.many) {
    const [sx, sy] = toScreen(t.x, t.z);
    ctx.beginPath(); ctx.arc(sx, sy, Math.max(6, t.r * view.scale + 3), 0, 7); ctx.stroke();
  }
  if (drag?.type === 'box') {
    ctx.setLineDash([5, 4]); ctx.strokeStyle = '#ffd84a'; ctx.fillStyle = 'rgba(255,216,74,0.08)';
    ctx.fillRect(drag.sx, drag.sy, drag.ex - drag.sx, drag.ey - drag.sy);
    ctx.strokeRect(drag.sx, drag.sy, drag.ex - drag.sx, drag.ey - drag.sy);
    ctx.setLineDash([]);
  }
  const s = userTrees[sel.tree], f = sel.found, rm = removedTrees[sel.removed];
  const ring = s ? [s.x, s.z, (TREE_SIZES[s.size] || 13) * 0.33] : f ? [f.x, f.z, f.r] : rm ? [rm[0], rm[1], 2] : null;
  if (ring) {
    const [sx, sy] = toScreen(ring[0], ring[1]), r = Math.max(8, ring[2] * view.scale + 4);
    ctx.lineWidth = 2.5; ctx.strokeStyle = '#ffd84a';
    ctx.beginPath(); ctx.arc(sx, sy, r, 0, 7); ctx.stroke();
  }
}

function holeHandles(h) {
  const out = [];
  for (const [k, p] of Object.entries(h.tees)) out.push({ kind: 'tee', key: k, p });
  h.aims.forEach((p, i) => out.push({ kind: 'aim', key: i, p }));
  out.push({ kind: 'green', p: h.green });
  for (const [k, p] of Object.entries(h.pins)) out.push({ kind: 'pin', key: k, p });
  return out;
}

function drawHole(h, active) {
  const back = TEE_ORDER.map((t) => h.tees[t]).find(Boolean);
  if (!back) return;
  const line = [back, ...h.aims, h.green].map((p) => toScreen(p.x, p.z));
  ctx.strokeStyle = active ? 'rgba(255,255,255,0.95)' : 'rgba(255,255,255,0.35)';
  ctx.lineWidth = active ? 2 : 1;
  ctx.setLineDash(active ? [] : [4, 4]);
  ctx.beginPath(); line.forEach(([x, y], i) => (i ? ctx.lineTo(x, y) : ctx.moveTo(x, y))); ctx.stroke();
  ctx.setLineDash([]);
  // Hole number by the back tee
  const [nx, ny] = toScreen(back.x, back.z);
  ctx.fillStyle = active ? '#ffd84a' : 'rgba(0,0,0,0.55)';
  ctx.beginPath(); ctx.arc(nx - 14, ny + 14, 10, 0, 7); ctx.fill();
  ctx.fillStyle = active ? '#111' : '#fff'; ctx.font = 'bold 11px system-ui'; ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
  ctx.fillText(h.number, nx - 14, ny + 14);
  ctx.textAlign = 'start'; ctx.textBaseline = 'alphabetic';
  if (!active) return;
  for (const hd of holeHandles(h)) {
    const [x, y] = toScreen(hd.p.x, hd.p.z);
    ctx.lineWidth = 1.5; ctx.strokeStyle = '#000';
    if (hd.kind === 'tee') {
      ctx.fillStyle = TEE_COLORS[hd.key] || '#fff';
      ctx.beginPath(); ctx.arc(x, y, 7, 0, 7); ctx.fill(); ctx.stroke();
    } else if (hd.kind === 'aim') {
      ctx.fillStyle = '#ffd84a';
      ctx.beginPath(); ctx.moveTo(x, y - 8); ctx.lineTo(x + 8, y); ctx.lineTo(x, y + 8); ctx.lineTo(x - 8, y); ctx.closePath(); ctx.fill(); ctx.stroke();
      ctx.fillStyle = '#111'; ctx.font = 'bold 9px system-ui'; ctx.textAlign = 'center'; ctx.fillText(hd.key + 1, x, y + 3); ctx.textAlign = 'start';
    } else if (hd.kind === 'green') {
      ctx.strokeStyle = '#fff'; ctx.lineWidth = 2;
      ctx.beginPath(); ctx.arc(x, y, 8, 0, 7); ctx.moveTo(x - 11, y); ctx.lineTo(x + 11, y); ctx.moveTo(x, y - 11); ctx.lineTo(x, y + 11); ctx.stroke();
    } else if (hd.kind === 'pin') {
      ctx.fillStyle = '#e8282b';
      ctx.beginPath(); ctx.arc(x, y, 5, 0, 7); ctx.fill(); ctx.stroke();
      ctx.fillStyle = '#fff'; ctx.font = '10px system-ui'; ctx.fillText(hd.key.slice(0, 2), x + 7, y - 5);
    }
  }
}

// ------------------------------------------------------------------ buildings

// What each building is (worked out again only when something it depends on changes)
function kinds() {
  const tee = (h) => h && TEE_ORDER.map((t) => h.tees[t]).find(Boolean);
  const start = [tee(holes[0]), holes[8]?.green, tee(holes[9]), holes[17]?.green].filter(Boolean);
  const key = `${buildings.length}|${JSON.stringify(start)}|${JSON.stringify(buildingTypes)}`;
  if (key !== kindsKey) { buildingKinds = classifyBuildings(buildings, start, buildingTypes); kindsKey = key; }
  return buildingKinds;
}
// Choose a building's type (back to the guess: the choice is dropped)
function setBuildingType(key, type, auto) {
  if (type === auto) delete buildingTypes[key]; else buildingTypes[key] = type;
  renderShapePanel(); renderBuildingInfo(); draw();
}
function renderBuildingInfo() {
  const legend = $('b-legend');
  if (legend && !legend.childElementCount) legend.innerHTML = BUILDING_TYPES.map((t) => `<span><i style="background:${t.colour}"></i>${t.label.split(' (')[0]}</span>`).join('');
  const el = $('b-info');
  if (!el) return;
  if (!buildings.length) { el.textContent = 'No buildings mapped around this course.'; return; }
  const n = {};
  for (const k of kinds()) n[k.type] = (n[k.type] || 0) + 1;
  const changed = Object.keys(buildingTypes).length;
  el.textContent = `${buildings.length} buildings: ${BUILDING_TYPES.filter((t) => n[t.id]).map((t) => `${n[t.id]} ${t.label.split(' (')[0].toLowerCase()}`).join(', ')}${changed ? ` · ${changed} set by you` : ''}`;
}
function drawBuildings() {
  if (!buildings.length) return;
  const ks = kinds(), colour = Object.fromEntries(BUILDING_TYPES.map((t) => [t.id, t.colour]));
  ctx.lineWidth = 1.5;
  buildings.forEach((b, i) => {
    ctx.beginPath();
    b.pts.forEach(([x, z], j) => { const [sx, sy] = toScreen(x, z); if (j) ctx.lineTo(sx, sy); else ctx.moveTo(sx, sy); });
    ctx.closePath();
    ctx.globalAlpha = 0.55; ctx.fillStyle = colour[ks[i].type] || '#ccc'; ctx.fill();
    ctx.globalAlpha = 1; ctx.strokeStyle = i === sel.building ? '#ffd84a' : 'rgba(0,0,0,0.6)'; ctx.lineWidth = i === sel.building ? 3 : 1.2; ctx.stroke();
  });
  // (the type written on each, once there's room)
  if (view.scale > 1.2) {
    ctx.font = '11px system-ui'; ctx.textAlign = 'center'; ctx.fillStyle = '#fff'; ctx.strokeStyle = 'rgba(0,0,0,0.7)'; ctx.lineWidth = 3;
    buildings.forEach((b, i) => {
      const { cx, cz } = footprintInfo(b.pts), [sx, sy] = toScreen(cx, cz), t = BUILDING_TYPES.find((x) => x.id === ks[i].type)?.label.split(' (')[0] || '';
      ctx.strokeText(t, sx, sy + 4); ctx.fillText(t, sx, sy + 4);
    });
    ctx.textAlign = 'start';
  }
}
// The building under the cursor (the smallest, where they overlap), or -1
function hitBuilding(sx, sy) {
  const [x, z] = toWorld(sx, sy);
  let best = -1, ba = Infinity;
  buildings.forEach((b, i) => { if (pointInPoly(b.pts, x, z)) { const { area } = footprintInfo(b.pts); if (area < ba) { ba = area; best = i; } } });
  return best;
}
const escapeHtml = (t) => String(t).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' })[c]);

// ------------------------------------------------------------------ hit testing

function hitHandle(sx, sy) {
  const h = holes[sel.hole];
  if (!h) return null;
  // Pins first: they sit on top of the green center
  const list = holeHandles(h).sort((a, b) => (a.kind === 'pin' ? -1 : 0) - (b.kind === 'pin' ? -1 : 0));
  for (const hd of list) {
    const [x, y] = toScreen(hd.p.x, hd.p.z);
    if (Math.hypot(x - sx, y - sy) < 10) return hd;
  }
  return null;
}
function hitVertex(sx, sy) {
  const s = shapes[sel.shape];
  if (!s) return null;
  for (let i = 0; i < s.pts.length; i++) {
    const [x, y] = toScreen(...s.pts[i]);
    if (Math.abs(x - sx) < 7 && Math.abs(y - sy) < 7) return { i };
  }
  for (let i = 0; i < s.pts.length; i++) {
    const a = toScreen(...s.pts[i]), b = toScreen(...s.pts[(i + 1) % s.pts.length]);
    if (Math.hypot((a[0] + b[0]) / 2 - sx, (a[1] + b[1]) / 2 - sy) < 7) return { insertAfter: i };
  }
  return null;
}
// The nearest of the player's trees under the cursor (within its crown, or 10 px when small)
function hitUserTree(sx, sy) {
  let best = -1, bd = Infinity;
  userTrees.forEach((t, i) => {
    const [x, y] = toScreen(t.x, t.z), d = Math.hypot(x - sx, y - sy);
    if (d <= Math.max(10, (TREE_SIZES[t.size] || 13) * 0.33 * view.scale) && d < bd) { best = i; bd = d; }
  });
  return best;
}

// A found tree (canopy map, OpenStreetMap or aerial photo) under the cursor
function hitFoundTree(sx, sy) {
  let best = null, bd = Infinity;
  for (const t of plannedTrees()) {
    if (t.user) continue;
    const [x, y] = toScreen(t.x, t.z), d = Math.hypot(x - sx, y - sy);
    if (d <= Math.max(6, t.r * view.scale) && d < bd) { best = t; bd = d; }
  }
  return best;
}
// A removed tree's cross under the cursor
function hitRemoved(sx, sy) {
  if (view.scale <= 0.6) return -1;
  return removedTrees.findIndex(([x, z]) => { const [px, py] = toScreen(x, z); return Math.hypot(px - sx, py - sy) < 7; });
}
const clearTreeSel = () => { sel.tree = -1; sel.found = null; sel.removed = -1; sel.many = []; sel.building = -1; };

function hitShape(sx, sy) {
  const [x, z] = toWorld(sx, sy);
  let best = -1, bestArea = Infinity;
  shapes.forEach((s, i) => {
    let hit;
    if (s.kind === 'oob') {
      // The boundary only selects near its edge
      hit = s.pts.some((p, k) => segDistPx([sx, sy], toScreen(...p), toScreen(...s.pts[(k + 1) % s.pts.length])) < 6);
      if (hit) { best = i; bestArea = -1; }
      return;
    }
    hit = pointInPoly(s.pts, x, z);
    if (hit) { const a = polyArea(s.pts); if (a < bestArea) { best = i; bestArea = a; } }
  });
  return best;
}

// ------------------------------------------------------------------ mouse

canvas.addEventListener('mousedown', (e) => {
  canvas.focus(); // so Enter / Delete / Esc reach the editor rather than a sidebar button
  const sx = e.offsetX, sy = e.offsetY;
  const [x, z] = toWorld(sx, sy);
  if (e.button !== 0) { drag = { type: 'pan', sx, sy, cx: view.cx, cz: view.cz }; return; }
  if (mode?.type === 'place') { placePoint(x, z); return; }
  if (mode?.type === 'tree') {
    change();
    userTrees.push({ x, z, size: treeOpts.size, kind: treeOpts.kind });
    clearTreeSel(); sel.tree = userTrees.length - 1; sel.shape = -1;
    renderShapePanel(); draw(); return;
  }
  if (mode?.type === 'draw') {
    if (e.detail > 1) return finishDraw(); // double-click
    mode.pts.push([x, z]); draw(); return;
  }
  // Shift: pick many found trees - click one to add or drop it, or drag a box round several
  if (e.shiftKey && showTrees) {
    drag = { type: 'box', sx, sy, ex: sx, ey: sy, found: hitFoundTree(sx, sy) };
    return;
  }
  const hd = hitHandle(sx, sy);
  if (hd) { change(); drag = { type: 'handle', hd }; return; }
  if (showTrees) {
    const ti = hitUserTree(sx, sy);
    if (ti >= 0) {
      change();
      clearTreeSel(); sel.tree = ti; sel.shape = -1;
      drag = { type: 'tree', i: ti };
      renderShapePanel(); draw(); return;
    }
    // (a found tree or a removed one: selected on a click, the map still pans on a drag)
    const ri = hitRemoved(sx, sy), ft = ri < 0 ? hitFoundTree(sx, sy) : null;
    if (ri >= 0 || ft) { drag = { type: 'pan', sx, sy, cx: view.cx, cz: view.cz, click: true, found: ft, removed: ri }; return; }
  }
  const v = hitVertex(sx, sy);
  if (v) {
    const s = shapes[sel.shape];
    change();
    if (v.insertAfter != null) { s.pts.splice(v.insertAfter + 1, 0, [x, z]); drag = { type: 'vertex', i: v.insertAfter + 1 }; }
    else if (e.altKey) { if (s.pts.length > 3) s.pts.splice(v.i, 1); draw(); }
    else drag = { type: 'vertex', i: v.i };
    return;
  }
  // Grabbing the selected shape anywhere inside moves the whole shape (e.g. to line it up with
  // the photo); a click without moving still selects, as below
  if (sel.shape >= 0 && insideShape(shapes[sel.shape], sx, sy)) {
    drag = { type: 'move', sx, sy, from: [x, z], orig: shapes[sel.shape].pts.map((p) => [...p]), moved: false };
    return;
  }
  drag = { type: 'pan', sx, sy, cx: view.cx, cz: view.cz, click: true };
});
// Inside a shape (the out-of-bounds line: near it)
function insideShape(s, sx, sy) {
  if (s.kind === 'oob') return s.pts.some((p, k) => segDistPx([sx, sy], toScreen(...p), toScreen(...s.pts[(k + 1) % s.pts.length])) < 6);
  return pointInPoly(s.pts, ...toWorld(sx, sy));
}
// Shift the selected shape by (dx, dz) metres from the corners it had (orig)
function moveShape(orig, dx, dz) {
  shapes[sel.shape].pts = orig.map(([x, z]) => [x + dx, z + dz]);
}
window.addEventListener('mousemove', (e) => {
  const r = canvas.getBoundingClientRect();
  const sx = e.clientX - r.left, sy = e.clientY - r.top;
  hover = [sx, sy];
  if (!drag) {
    if (mode?.type === 'draw') draw();
    // (a move cursor over the selected shape: it can be dragged as a whole)
    else if (!mode && e.target === canvas) canvas.style.cursor = sel.shape >= 0 && insideShape(shapes[sel.shape], sx, sy) ? 'move' : 'grab';
    return;
  }
  const [x, z] = toWorld(sx, sy);
  if (drag.type === 'pan') {
    if (Math.hypot(sx - drag.sx, sy - drag.sy) > 3) drag.click = false;
    view.cx = drag.cx - (sx - drag.sx) / view.scale;
    view.cz = drag.cz + (sy - drag.sy) / view.scale;
    canvas.style.cursor = 'grabbing';
  } else if (drag.type === 'handle') {
    drag.hd.p.x = x; drag.hd.p.z = z;
    renderHolePanelValues();
  } else if (drag.type === 'move') {
    if (!drag.moved && Math.hypot(sx - drag.sx, sy - drag.sy) <= 3) return;
    if (!drag.moved) { change(); drag.moved = true; } // (one undo step for the whole move)
    moveShape(drag.orig, x - drag.from[0], z - drag.from[1]);
    canvas.style.cursor = 'grabbing';
  } else if (drag.type === 'vertex') {
    shapes[sel.shape].pts[drag.i] = [x, z];
  } else if (drag.type === 'tree') {
    userTrees[drag.i].x = x; userTrees[drag.i].z = z;
  } else if (drag.type === 'box') {
    drag.ex = sx; drag.ey = sy;
  }
  draw();
});
window.addEventListener('mouseup', (e) => {
  if (drag?.type === 'box') {
    const many = sel.many.slice();
    sel.tree = -1; sel.removed = -1; sel.shape = -1;
    if (sel.found && !many.includes(sel.found)) many.push(sel.found);
    sel.found = null;
    if (Math.hypot(drag.ex - drag.sx, drag.ey - drag.sy) < 4) {
      // a Shift-click: add the tree under it, or drop it if it's already picked
      if (drag.found) { const k = many.indexOf(drag.found); if (k >= 0) many.splice(k, 1); else many.push(drag.found); }
    } else {
      const x0 = Math.min(drag.sx, drag.ex), x1 = Math.max(drag.sx, drag.ex), y0 = Math.min(drag.sy, drag.ey), y1 = Math.max(drag.sy, drag.ey);
      for (const t of plannedTrees()) {
        if (t.user) continue;
        const [px, py] = toScreen(t.x, t.z);
        if (px >= x0 && px <= x1 && py >= y0 && py <= y1 && !many.includes(t)) many.push(t);
      }
    }
    if (many.length === 1) sel.found = many[0]; else sel.many = many;
    drag = null;
    renderShapePanel(); draw();
    return;
  }
  if ((drag?.type === 'pan' && drag.click) || (drag?.type === 'move' && !drag.moved)) {
    // A click (not a drag) selects whatever shape is under the cursor
    clearTreeSel();
    if (drag.found) { sel.found = drag.found; sel.shape = -1; }
    else if (drag.removed >= 0) { sel.removed = drag.removed; sel.shape = -1; }
    else {
      const bi = showBuildings ? hitBuilding(drag.sx, drag.sy) : -1;
      sel.building = bi;
      sel.shape = bi >= 0 ? -1 : hitShape(drag.sx, drag.sy);
    }
    renderShapePanel();
    draw();
  }
  drag = null;
  canvas.style.cursor = mode ? 'crosshair' : 'grab';
});
canvas.addEventListener('wheel', (e) => {
  e.preventDefault();
  const [x, z] = toWorld(e.offsetX, e.offsetY);
  view.scale = Math.max(0.05, Math.min(40, view.scale * Math.exp(-e.deltaY * 0.0015)));
  // keep the point under the cursor fixed
  const [x2, z2] = toWorld(e.offsetX, e.offsetY);
  view.cx += x - x2; view.cz += z - z2;
  draw();
}, { passive: false });
canvas.addEventListener('contextmenu', (e) => e.preventDefault());

window.addEventListener('keydown', (e) => {
  if (e.target.matches('input, select, textarea')) return;
  if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'z') { e.preventDefault(); undo(); return; }
  if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 's') { e.preventDefault(); save(); return; }
  if (e.key === 'Escape') { setMode(null); return; }
  if (e.key === 'Enter' && mode?.type === 'draw') { finishDraw(); return; }
  if (e.key === 'Backspace' && mode?.type === 'draw') { mode.pts.pop(); draw(); return; }
  if ((e.key === 'Delete' || e.key === 'Backspace') && sel.tree >= 0) { deleteTree(); return; }
  if ((e.key === 'Delete' || e.key === 'Backspace') && sel.found) { removeFound(); return; }
  if ((e.key === 'Delete' || e.key === 'Backspace') && sel.many.length) { removeMany(); return; }
  if ((e.key === 'Delete' || e.key === 'Backspace') && sel.shape >= 0) deleteShape();
  // Arrow keys nudge the selected shape: 25 cm, or 1 m with Shift (up = north on the map)
  const nudge = { ArrowLeft: [-1, 0], ArrowRight: [1, 0], ArrowUp: [0, 1], ArrowDown: [0, -1] }[e.key];
  if (nudge && sel.shape >= 0 && !mode) {
    e.preventDefault();
    const step = e.shiftKey ? 1 : 0.25;
    change();
    moveShape(shapes[sel.shape].pts.map((p) => [...p]), nudge[0] * step, nudge[1] * step);
    draw();
  }
});
window.addEventListener('beforeunload', (e) => { if (dirty) { e.preventDefault(); e.returnValue = ''; } });

// ------------------------------------------------------------------ modes

function setMode(m) {
  mode = m;
  const hint = $('mode-hint');
  if (!m) hint.classList.add('hidden');
  else {
    hint.classList.remove('hidden');
    hint.textContent = m.type === 'place'
      ? `Click the map to place: ${m.label}  (Esc to cancel)`
      : m.type === 'tree' ? `Click the map to add ${treeOpts.size} trees, one per click · Esc when done`
      : `Drawing ${SHAPES[m.kind].label.toLowerCase()}: click to add points · double-click or Enter to finish · Esc to cancel`;
  }
  canvas.style.cursor = m ? 'crosshair' : 'grab';
  document.querySelectorAll('.draw-buttons button').forEach((b) => b.classList.toggle('active', m?.type === 'draw' && b.dataset.kind === m.kind));
  $('t-add').classList.toggle('active', m?.type === 'tree');
  $('t-area').classList.toggle('active', m?.type === 'draw' && m.kind === 'trees');
  renderHolePanel();
  draw();
}

function placePoint(x, z) {
  const h = holes[sel.hole];
  change();
  const p = { x, z };
  if (mode.kind === 'tee') h.tees[mode.key] = p;
  else if (mode.kind === 'aim') h.aims[mode.key] = p;
  else if (mode.kind === 'green') h.green = p;
  else if (mode.kind === 'pin') h.pins[mode.key] = p;
  h.aims = h.aims.filter(Boolean);
  setMode(null);
}

function finishDraw() {
  const pts = mode.pts.filter((p, i, a) => i === 0 || Math.hypot(p[0] - a[i - 1][0], p[1] - a[i - 1][1]) > 0.3);
  const kind = mode.kind;
  if (pts.length < 3) { setStatus('A shape needs at least 3 points'); return; }
  change();
  if (kind === 'oob') shapes = shapes.filter((s) => s.kind !== 'oob'); // only one boundary
  shapes.push(kind === 'trees' ? { kind, pts, density: treeOpts.density, species: treeOpts.areaKind, hMin: treeOpts.hMin, hMax: treeOpts.hMax } : { kind, pts });
  sel.shape = shapes.length - 1;
  clearTreeSel();
  setMode(null);
  renderShapePanel();
}

function deleteTree() {
  if (sel.tree < 0) return;
  change();
  userTrees.splice(sel.tree, 1);
  sel.tree = -1;
  renderShapePanel();
  draw();
}

// Take out the selected found tree (the game leaves out found trees within 3 m of the spot);
// replace: put one of the player's own trees there instead, to move or change
function removeFound(replace = false) {
  const f = sel.found;
  if (!f) return;
  change();
  removedTrees.push([Math.round(f.x * 100) / 100, Math.round(f.z * 100) / 100]);
  clearTreeSel();
  if (replace) {
    const size = Object.keys(TREE_SIZES).reduce((a, b) => (Math.abs(TREE_SIZES[b] - f.h) < Math.abs(TREE_SIZES[a] - f.h) ? b : a));
    userTrees.push({ x: f.x, z: f.z, size, kind: 'auto' });
    sel.tree = userTrees.length - 1;
  }
  renderShapePanel(); draw();
}
// Take out every tree picked with Shift (one undo step)
function removeMany() {
  if (!sel.many.length) return;
  change();
  for (const f of sel.many) removedTrees.push([Math.round(f.x * 100) / 100, Math.round(f.z * 100) / 100]);
  const n = sel.many.length;
  clearTreeSel();
  setStatus(`Removed ${n} trees (Ctrl+Z to undo)`);
  renderShapePanel(); draw();
}
function restoreRemoved() {
  if (sel.removed < 0) return;
  change();
  removedTrees.splice(sel.removed, 1);
  clearTreeSel();
  renderShapePanel(); draw();
}

function deleteShape() {
  if (sel.shape < 0) return;
  change();
  shapes.splice(sel.shape, 1);
  sel.shape = -1;
  renderShapePanel();
  draw();
}

// ------------------------------------------------------------------ side panel

function renderDrawButtons() {
  const box = $('draw-buttons');
  box.innerHTML = '';
  for (const kind of ['green', 'fairway', 'bunker', 'tee', 'water', 'penalty', 'oob']) {
    const b = document.createElement('button');
    b.dataset.kind = kind;
    b.innerHTML = `<span class="sw" style="background:${SHAPES[kind].fill || 'transparent'};border:1px solid ${SHAPES[kind].stroke}"></span>${SHAPES[kind].label}`;
    b.onclick = () => setMode(mode?.type === 'draw' && mode.kind === kind ? null : { type: 'draw', kind, pts: [] });
    box.appendChild(b);
  }
}

function renderShapePanel() {
  const box = $('shape-panel');
  const s = shapes[sel.shape];
  const t = userTrees[sel.tree];
  if (t) {
    box.className = 'small';
    box.innerHTML = `
      <label>Selected tree <select id="t-sel-size">${Object.keys(TREE_SIZES).map((k) => `<option value="${k}" ${k === t.size ? 'selected' : ''}>${k[0].toUpperCase() + k.slice(1)} (about ${TREE_SIZES[k]} m)</option>`).join('')}</select></label>
      <label>Species <select id="t-sel-kind">${speciesOptions(t.kind)}</select></label>
      <div class="muted">Drag it to move it.</div>
      <div class="btn-row"><button id="t-del">Delete tree</button></div>`;
    $('t-sel-size').onchange = () => { change(); t.size = $('t-sel-size').value; draw(); };
    $('t-sel-kind').onchange = () => { change(); t.kind = $('t-sel-kind').value; draw(); };
    $('t-del').onclick = deleteTree;
    return;
  }
  if (sel.building >= 0 && buildings[sel.building]) {
    const k = kinds()[sel.building], b = buildings[sel.building], { area } = footprintInfo(b.pts);
    const label = (id) => BUILDING_TYPES.find((t) => t.id === id)?.label || id;
    box.className = 'small';
    box.innerHTML = `
      <label>Selected building <select id="b-type">${BUILDING_TYPES.map((t) => `<option value="${t.id}" ${t.id === k.type ? 'selected' : ''}>${t.label}</option>`).join('')}</select></label>
      <div class="muted">${b.name ? `${escapeHtml(b.name)} · ` : ''}about ${Math.round(area)} m²${b.kind && b.kind !== 'yes' ? ` · mapped as “${escapeHtml(b.kind)}”` : ''}.
        ${k.type === k.auto ? 'The game’s guess.' : `The game guessed ${label(k.auto).toLowerCase()}.`}</div>
      ${k.type !== k.auto ? '<div class="btn-row"><button id="b-auto">Use the game’s guess</button></div>' : ''}`;
    $('b-type').onchange = () => { change(); setBuildingType(k.key, $('b-type').value, k.auto); };
    if ($('b-auto')) $('b-auto').onclick = () => { change(); setBuildingType(k.key, k.auto, k.auto); };
    return;
  }
  if (sel.many.length) {
    const photo = sel.many.filter((t) => t.photo).length;
    box.className = 'small';
    box.innerHTML = `
      <div><b>${sel.many.length} trees selected</b></div>
      <div class="muted">${photo ? `${photo} guessed from the aerial photo, ` : ''}${sel.many.length - photo} from the tree-height map or OpenStreetMap. Shift-click a tree to add or drop it.</div>
      <div class="btn-row"><button id="m-del">Remove ${sel.many.length} trees</button><button id="m-clear">Clear selection</button></div>`;
    $('m-del').onclick = removeMany;
    $('m-clear').onclick = () => { clearTreeSel(); renderShapePanel(); draw(); };
    return;
  }
  if (sel.found) {
    const f = sel.found;
    box.className = 'small';
    box.innerHTML = `
      <div><b>${f.photo ? 'Tree guessed from the aerial photo' : 'Tree found automatically'}</b></div>
      <div class="muted">${f.photo ? 'Spotted as a tree crown in the photo. If it isn’t really a tree, remove it.' : 'From the tree-height map or OpenStreetMap.'} About ${Math.round(f.h)} m tall.</div>
      <div class="btn-row"><button id="f-del">Remove tree</button><button id="f-own">Replace with my own tree</button></div>`;
    $('f-del').onclick = () => removeFound(false);
    $('f-own').onclick = () => removeFound(true);
    return;
  }
  if (removedTrees[sel.removed]) {
    box.className = 'small';
    box.innerHTML = `<div><b>Removed tree</b></div><div class="muted">A tree found automatically was taken out here.</div>
      <div class="btn-row"><button id="f-restore">Put it back</button></div>`;
    $('f-restore').onclick = restoreRemoved;
    return;
  }
  if (!s) { box.innerHTML = 'Click a shape on the map to select it.'; box.className = 'muted small'; return; }
  box.className = 'small';
  const area = polyArea(s.pts);
  box.innerHTML = `
    <label>Selected shape <select id="s-kind">${Object.entries(SHAPES).map(([k, v]) => `<option value="${k}" ${k === s.kind ? 'selected' : ''}>${v.label}</option>`).join('')}</select></label>
    ${s.kind === 'trees' ? `<label>Tree density <select id="s-density">${['sparse', 'medium', 'dense'].map((d) => `<option value="${d}" ${d === (s.density || 'medium') ? 'selected' : ''}>${d[0].toUpperCase() + d.slice(1)}</option>`).join('')}</select></label>
      <label>Species <select id="s-species">${speciesOptions(s.species)}</select></label>
      <div class="height-row">
        <label>Shortest (m) <input id="s-hmin" type="number" min="2" max="40" step="1" value="${Number.isFinite(s.hMin) ? s.hMin : ''}" placeholder="map"></label>
        <label>Tallest (m) <input id="s-hmax" type="number" min="2" max="40" step="1" value="${Number.isFinite(s.hMax) ? s.hMax : ''}" placeholder="map"></label>
      </div>
      <div class="muted">Each tree gets a height between these. Leave both empty to use the measured heights where the course has them.</div>` : ''}
    ${s.kind === 'water' && !s.inner ? `<label class="check"><input id="s-flow" type="checkbox" ${s.flow ? 'checked' : ''}> Stream or river</label>
      <div class="muted">${s.flow ? 'Runs downhill: the water slopes with its banks and flows faster where it\u2019s steeper.' : 'A pond or lake: the water lies flat.'}</div>` : ''}
    <div class="muted">${s.pts.length} points · ${Math.round(area).toLocaleString()} m²${s.inner ? ' · cut-out' : ''}</div>
    <div class="btn-row"><button id="s-del">Delete shape</button></div>`;
  $('s-kind').onchange = () => {
    change();
    if ($('s-kind').value === 'oob') shapes = shapes.filter((x, i) => x.kind !== 'oob' || i === sel.shape);
    shapes[sel.shape = shapes.indexOf(s)].kind = $('s-kind').value;
    if (s.kind === 'trees') s.density ||= treeOpts.density;
    renderShapePanel(); draw();
  };
  $('s-del').onclick = deleteShape;
  if ($('s-flow')) $('s-flow').onchange = () => { change(); if ($('s-flow').checked) s.flow = true; else delete s.flow; renderShapePanel(); draw(); };
  if ($('s-density')) $('s-density').onchange = () => { change(); s.density = $('s-density').value; draw(); };
  if ($('s-species')) $('s-species').onchange = () => { change(); s.species = $('s-species').value; draw(); };
  if ($('s-hmin')) {
    const heights = () => {
      change();
      const lo = parseFloat($('s-hmin').value), hi = parseFloat($('s-hmax').value);
      if (Number.isFinite(lo) && Number.isFinite(hi)) { s.hMin = Math.min(40, Math.max(2, lo)); s.hMax = Math.min(40, Math.max(2, hi)); }
      else { delete s.hMin; delete s.hMax; }
      draw();
    };
    $('s-hmin').onchange = heights; $('s-hmax').onchange = heights;
  }
}

function renderHoles() {
  const box = $('hole-buttons');
  box.innerHTML = '';
  holes.forEach((h, i) => {
    const b = document.createElement('button');
    b.textContent = h.number;
    b.className = i === sel.hole ? 'sel' : '';
    b.onclick = () => { sel.hole = i; setMode(null); renderHoles(); fitHole(); };
    box.appendChild(b);
  });
  renderHolePanel();
}

const pathLen = (pts) => pts.slice(1).reduce((s, p, i) => s + Math.hypot(p.x - pts[i].x, p.z - pts[i].z), 0);
const teeLen = (h, t) => (h.tees[t] ? pathLen([h.tees[t], ...h.aims, h.green]) : null);

function renderHolePanel() {
  const box = $('hole-panel');
  const h = holes[sel.hole];
  if (!h) { box.innerHTML = '<div class="muted small">No holes yet — add one.</div>'; return; }
  const placing = (kind, key) => mode?.type === 'place' && mode.kind === kind && String(mode.key) === String(key);
  const row = (sw, name, val, kind, key, removable) => `
    <div class="pt-row"><span class="sw" style="${sw}"></span><span class="name">${name}</span>
      <span class="val" data-val="${kind}:${key}">${val}</span>
      <button data-place="${kind}:${key}" class="${placing(kind, key) ? 'active' : ''}">Place</button>
      ${removable ? `<button data-remove="${kind}:${key}" title="Remove">✕</button>` : ''}</div>`;
  const yd = (m) => (m == null ? '—' : `${Math.round(m * M_TO_YD)} yd`);
  box.innerHTML = `
    <div class="hole-props">
      <label>Hole # <input id="h-num" type="number" min="1" max="36" value="${h.number}"></label>
      <label>Par <input id="h-par" type="number" min="3" max="6" value="${h.par}"></label>
      <label>Handicap <input id="h-idx" type="number" min="1" max="18" value="${h.index ?? ''}"></label>
    </div>
    <div class="pt-group"><h4>Tees</h4>
      ${TEE_ORDER.map((t) => row(`background:${TEE_COLORS[t]}`, t, h.tees[t] ? yd(teeLen(h, t)) : 'not set', 'tee', t, !!h.tees[t])).join('')}
    </div>
    <div class="pt-group"><h4>Aim points (doglegs)</h4>
      ${[0, 1].map((i) => row('background:#ffd84a;border-radius:2px', `Aim ${i + 1}`, h.aims[i] ? 'set' : 'not set', 'aim', i, !!h.aims[i])).join('')}
    </div>
    <div class="pt-group"><h4>Green &amp; pins</h4>
      ${row('border:2px solid #fff;background:transparent', 'Green center', 'set', 'green', 0, false)}
      ${PIN_DAYS.map((d) => row('background:#e8282b', `${d} pin`, h.pins[d] ? 'set' : 'not set', 'pin', d, false)).join('')}
      <div class="btn-row"><button id="h-autopins">Spread pins across green</button></div>
    </div>`;
  const num = (id, key, min) => {
    $(id).onchange = () => {
      const v = Math.max(min, Math.round(+$(id).value || min));
      if (key === 'number' && holes.some((o) => o !== h && o.number === v)) { setStatus(`Hole ${v} already exists`); $(id).value = h.number; return; }
      change(); h[key] = v; renderHoles(); draw();
    };
  };
  num('h-num', 'number', 1); num('h-par', 'par', 3); num('h-idx', 'index', 1);
  box.querySelectorAll('[data-place]').forEach((b) => {
    const [kind, key] = b.dataset.place.split(':');
    b.onclick = () => {
      const k = kind === 'aim' ? +key : key;
      if (placing(kind, k)) return setMode(null);
      // Aim 2 needs aim 1 first
      const aimKey = kind === 'aim' ? Math.min(k, h.aims.length) : k;
      setMode({ type: 'place', kind, key: aimKey, label: kind === 'tee' ? `${key} tee` : kind === 'aim' ? `aim point ${aimKey + 1}` : kind === 'pin' ? `${key} pin` : 'green center' });
    };
  });
  box.querySelectorAll('[data-remove]').forEach((b) => {
    const [kind, key] = b.dataset.remove.split(':');
    b.onclick = () => {
      if (kind === 'tee' && Object.keys(h.tees).length <= 1) return setStatus('A hole needs at least one tee');
      change();
      if (kind === 'tee') delete h.tees[key];
      if (kind === 'aim') h.aims.splice(+key, 1);
      renderHolePanel(); draw();
    };
  });
  $('h-autopins').onclick = () => { change(); autoPins(h); renderHolePanel(); draw(); };
}

// Update just the distance labels while dragging
function renderHolePanelValues() {
  const h = holes[sel.hole];
  if (!h) return;
  for (const t of TEE_ORDER) {
    const el = document.querySelector(`[data-val="tee:${t}"]`);
    if (el && h.tees[t]) el.textContent = `${Math.round(teeLen(h, t) * M_TO_YD)} yd`;
  }
}

// Four pins spread over the green the hole's center sits in
function autoPins(h) {
  const green = shapes.filter((s) => s.kind === 'green' && !s.inner).find((s) => pointInPoly(s.pts, h.green.x, h.green.z));
  const back = h.aims[h.aims.length - 1] || TEE_ORDER.map((t) => h.tees[t]).find(Boolean) || { x: h.green.x, z: h.green.z - 100 };
  const fx = h.green.x - back.x, fz = h.green.z - back.z, fl = Math.hypot(fx, fz) || 1;
  const fwd = [fx / fl, fz / fl], right = [fwd[1], -fwd[0]];
  const c = green ? centroid(green.pts) : [h.green.x, h.green.z];
  const r = green ? Math.sqrt(polyArea(green.pts) / Math.PI) : 10;
  const offsets = { Thursday: [0, 0], Friday: [-0.45, -0.35], Saturday: [0.45, 0.35], Sunday: [0.4, -0.4] };
  for (const [day, [u, w]] of Object.entries(offsets)) {
    let s = 1, p;
    do {
      p = [c[0] + (fwd[0] * u + right[0] * w) * r * s, c[1] + (fwd[1] * u + right[1] * w) * r * s];
      s *= 0.7;
    } while (green && !pointInPoly(green.pts, p[0], p[1]) && s > 0.05);
    h.pins[day] = { x: p[0], z: p[1] };
  }
}

// ------------------------------------------------------------------ buttons

$('c-name').oninput = () => { course.name = $('c-name').value; setDirty(true); };
$('c-location').oninput = () => { course.location = $('c-location').value; setDirty(true); };
// Rough and tall grass heights
for (const [kind, id] of [['rough', 'c-rough-h'], ['native', 'c-native-h']]) {
  $(id).innerHTML = GRASS_HEIGHTS[kind].map((o) => `<option value="${o.id}">${o.label}</option>`).join('');
  $(id).onchange = () => { change(); course.grass[kind] = $(id).value; setDirty(true); };
}
$('c-stimp').onchange = () => { course.stimp = +$('c-stimp').value || 10; setDirty(true); };

// Mowing patterns: a picker and a small top-down preview for fairways and for greens
for (const kind of ['fairway', 'green']) {
  const s = $(`c-mow-${kind}`);
  s.innerHTML = MOW_PATTERNS.map((p) => `<option value="${p.id}">${esc(p.label)}</option>`).join('');
  s.onchange = () => { change(); course.mowing[kind] = s.value; drawMowPreview(kind); setDirty(true); };
}
function showMowing() {
  for (const kind of ['fairway', 'green']) { $(`c-mow-${kind}`).value = course.mowing[kind]; drawMowPreview(kind); }
}
function drawMowPreview(kind) {
  const cv = $(`mow-prev-${kind}`), c = cv.getContext('2d');
  const W = cv.width, H = cv.height, img = c.createImageData(W, H);
  const width = MOW_WIDTH[kind], span = width * 5; // five passes across
  const [r, g, b] = kind === 'green' ? [84, 132, 44] : [66, 116, 36];
  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
    const k = 1 + 0.2 * mowShade(course.mowing[kind], ((x + 0.5) / W) * span, ((H - y - 0.5) / W) * span, width);
    const o = (y * W + x) * 4;
    img.data[o] = r * k; img.data[o + 1] = g * k; img.data[o + 2] = b * k; img.data[o + 3] = 255;
  }
  c.putImageData(img, 0, 0);
}
$('h-add').onclick = () => {
  change();
  const n = holes.reduce((m, h) => Math.max(m, h.number), 0) + 1;
  const [cx, cz] = [view.cx, view.cz];
  const h = { number: n, par: 4, index: n, tees: { White: { x: cx, z: cz - 150 } }, aims: [], green: { x: cx, z: cz + 150 }, pins: {} };
  autoPins(h);
  holes.push(h);
  sel.hole = holes.length - 1;
  renderHoles(); fitHole();
  setStatus(`Added hole ${n} — drag its tee, green and pins into place`);
};
$('h-del').onclick = () => {
  const h = holes[sel.hole];
  if (!h || !confirm(`Delete hole ${h.number}?`)) return;
  change();
  holes.splice(sel.hole, 1);
  sel.hole = Math.max(0, sel.hole - 1);
  renderHoles(); draw();
};
// Trees panel
$('t-add').onclick = () => setMode(mode?.type === 'tree' ? null : { type: 'tree' });
$('t-area').onclick = () => setMode(mode?.type === 'draw' && mode.kind === 'trees' ? null : { type: 'draw', kind: 'trees', pts: [] });
$('t-size').onchange = () => { treeOpts.size = $('t-size').value; if (mode?.type === 'tree') setMode(mode); };
$('t-density').onchange = () => { treeOpts.density = $('t-density').value; };
// Species pickers, area heights, and the list of species (with their map colours)
$('t-kind').innerHTML = speciesOptions(treeOpts.kind);
$('t-area-kind').innerHTML = speciesOptions(treeOpts.areaKind);
$('t-kind').onchange = () => { treeOpts.kind = $('t-kind').value; };
$('t-area-kind').onchange = () => { treeOpts.areaKind = $('t-area-kind').value; };
$('t-hmin').value = treeOpts.hMin; $('t-hmax').value = treeOpts.hMax;
$('t-hmin').onchange = () => { treeOpts.hMin = Math.min(40, Math.max(2, +$('t-hmin').value || AREA_HEIGHTS.min)); };
$('t-hmax').onchange = () => { treeOpts.hMax = Math.min(40, Math.max(2, +$('t-hmax').value || AREA_HEIGHTS.max)); };
$('species-list').innerHTML = TREE_SPECIES.map((sp) => `<li><i style="background:${SPECIES_COLOR[sp.id]}"></i><b>${esc(sp.label)}</b><span>${esc(sp.about)}</span></li>`).join('')
  + `<li><i style="background:rgba(80,175,70,0.9)"></i><b>Automatic</b><span>chosen from the aerial photo</span></li>`;
$('t-review').onclick = () => { if (!showTrees) { showTrees = true; $('t-show').checked = true; } startReview(); };
$('b-show').onchange = () => { showBuildings = $('b-show').checked; if (!showBuildings && sel.building >= 0) { sel.building = -1; renderShapePanel(); } draw(); };
$('t-show').onchange = () => { showTrees = $('t-show').checked; if (!showTrees && (sel.tree >= 0 || sel.found || sel.removed >= 0)) { clearTreeSel(); renderShapePanel(); } draw(); };
$('save').onclick = save;
$('play').onclick = async (e) => {
  if (!dirty) return;
  e.preventDefault();
  if (confirm('Save your changes before playing?')) await save();
  location.href = $('play').href;
};
$('fit-hole').onclick = fitHole;
$('fit-all').onclick = fitAll;

load().catch((err) => setStatus(`Couldn't load course: ${err.message}`));

$('toggle-photo').onclick = () => {
  showPhoto = !showPhoto;
  $('toggle-photo').textContent = showPhoto ? 'Photo' : 'Terrain';
  draw();
};
