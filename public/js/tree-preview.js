// A page for looking at the species grown by grow.js on their own: /tree-preview.html, or
// /tree-preview.html?kinds=elm,olive for chosen ones. Each in its own panel; drag a panel to turn
// its tree, scroll over it to zoom.
import * as THREE from 'three';
import { buildGrown } from './grow.js';
import { TREE_SPECIES } from './treeplace.js';

const ABOUT = Object.fromEntries(TREE_SPECIES.map((s) => [s.id, s]));
const asked = new URLSearchParams(location.search).get('kinds');
const KINDS = (asked ? asked.split(',') : ['dogwood', 'crapemyrtle', 'elm', 'hemlock', 'juniper', 'olive', 'holly']).filter((k) => ABOUT[k]);
const SPECIES = KINDS.map((k) => [k, ABOUT[k].label, ABOUT[k].about]);
const COLS = Math.ceil(Math.sqrt(SPECIES.length)), ROWS = Math.ceil(SPECIES.length / COLS);

const canvas = document.querySelector('#view'), status = document.querySelector('#status');
canvas.style.aspectRatio = `${(COLS / ROWS) * 1.1}`;
canvas.setAttribute('aria-label', `Interactive 3D preview of ${SPECIES.map((s) => s[1]).join(', ')}`);
const renderer = new THREE.WebGLRenderer({ canvas, antialias: true, alpha: false, preserveDrawingBuffer: true });
renderer.setPixelRatio(Math.min(devicePixelRatio, 1.75));
renderer.outputColorSpace = THREE.SRGBColorSpace;
renderer.toneMapping = THREE.ACESFilmicToneMapping;
renderer.toneMappingExposure = 1.4;
renderer.setScissorTest(true);

const views = [];
const loader = new THREE.TextureLoader();
const texture = async (path, color = false) => {
  const t = await loader.loadAsync(path);
  if (color) t.colorSpace = THREE.SRGBColorSpace;
  return t;
};

for (let i = 0; i < SPECIES.length; i++) {
  const [kind] = SPECIES[i];
  const leaf = await texture(`/textures/trees/${kind}_leaves.png`, true);
  leaf.premultiplyAlpha = true;
  const b = { map: await texture(`/textures/trees/${kind}_bark_color.jpg`, true), normal: await texture(`/textures/trees/${kind}_bark_normal.jpg`) };
  b.map.wrapS = b.map.wrapT = b.normal.wrapS = b.normal.wrapT = THREE.RepeatWrapping;
  const { bark, leaves, tint } = buildGrown(kind, 7100 + i, 'near');
  const scene = new THREE.Scene();
  scene.background = new THREE.Color('#1a281e');
  scene.add(new THREE.HemisphereLight(0xe5f1df, 0x59634b, 3.2));
  const sun = new THREE.DirectionalLight(0xffe8c9, 3.8);
  sun.position.set(-35, 70, 55);
  scene.add(sun);
  const ground = new THREE.Mesh(new THREE.CircleGeometry(30, 48), new THREE.MeshStandardMaterial({ color: '#344334', roughness: 1 }));
  ground.rotation.x = -Math.PI / 2;
  ground.position.y = -0.2;
  scene.add(ground);
  const barkMat = new THREE.MeshStandardMaterial({ map: b.map, normalMap: b.normal, roughness: 0.94, color: new THREE.Color(tint, tint, tint) });
  const leafMat = new THREE.MeshStandardMaterial({ map: leaf, alphaTest: 0.48, side: THREE.DoubleSide, roughness: 0.92 });
  scene.add(new THREE.Mesh(bark, barkMat), new THREE.Mesh(leaves, leafMat));
  const camera = new THREE.PerspectiveCamera(34, 1, 0.1, 400);
  views.push({ scene, camera, yaw: 0.42, pitch: 0.08, radius: 115, target: new THREE.Vector3(0, 29, 0) });
}

function orient(v) {
  const radius = v.radius, cp = Math.cos(v.pitch);
  v.camera.position.set(v.target.x + Math.sin(v.yaw) * radius * cp, v.target.y + Math.sin(v.pitch) * radius, v.target.z + Math.cos(v.yaw) * radius * cp);
  v.camera.lookAt(v.target);
}
// (panel i's rectangle in canvas pixels, from the bottom left as WebGL counts)
const cell = (i, rect) => {
  const w = rect.width / COLS, h = rect.height / ROWS, c = i % COLS, r = Math.floor(i / COLS);
  return [c * w, rect.height - (r + 1) * h, w, h];
};
function resize() {
  const rect = canvas.getBoundingClientRect();
  renderer.setSize(rect.width, rect.height, false);
  for (const v of views) { v.camera.aspect = (rect.width / COLS) / (rect.height / ROWS); v.camera.updateProjectionMatrix(); orient(v); }
}
function frame() {
  const rect = canvas.getBoundingClientRect();
  views.forEach((v, i) => { const [x, y, w, h] = cell(i, rect); renderer.setScissor(x, y, w, h); renderer.setViewport(x, y, w, h); renderer.render(v.scene, v.camera); });
  requestAnimationFrame(frame);
}

const labels = document.createElement('div');
labels.style.cssText = `position:absolute;inset:0;pointer-events:none;display:grid;grid-template-columns:repeat(${COLS},1fr);grid-template-rows:repeat(${ROWS},1fr);`;
canvas.parentElement.style.position = 'relative';
canvas.parentElement.append(labels);
SPECIES.forEach(([, name, detail]) => {
  const tag = document.createElement('div');
  tag.style.cssText = 'align-self:start;justify-self:start;margin:10px;padding:8px 10px;border-radius:9px;background:#101913dd;color:#edf1e7;max-width:80%;font-size:.9rem;';
  tag.innerHTML = `<strong>${name}</strong><br><span style="color:#b7c4b4;font-size:.8rem">${detail}</span>`;
  labels.append(tag);
});

let active = null, last = null;
function viewAt(x, y) {
  const r = canvas.getBoundingClientRect();
  return Math.min(views.length - 1, Math.floor(((y - r.top) / r.height) * ROWS) * COLS + Math.floor(((x - r.left) / r.width) * COLS));
}
canvas.addEventListener('pointerdown', (e) => {
  active = viewAt(e.clientX, e.clientY);
  last = [e.clientX, e.clientY]; canvas.setPointerCapture(e.pointerId);
});
canvas.addEventListener('pointermove', (e) => {
  if (active == null || !last) return;
  const v = views[active];
  v.yaw -= (e.clientX - last[0]) * 0.008;
  v.pitch = THREE.MathUtils.clamp(v.pitch + (e.clientY - last[1]) * 0.006, -0.28, 0.48);
  last = [e.clientX, e.clientY]; orient(v);
});
canvas.addEventListener('pointerup', () => { active = last = null; });
canvas.addEventListener('wheel', (e) => {
  e.preventDefault();
  const v = views[viewAt(e.clientX, e.clientY)];
  v.radius = THREE.MathUtils.clamp(v.radius * Math.exp(e.deltaY * 0.001), 28, 200);
  orient(v);
}, { passive: false });
new ResizeObserver(resize).observe(canvas);
resize();
status.textContent = 'Drag a panel to rotate its tree; use the mouse wheel or trackpad over a panel to zoom. Each preview uses the game’s procedural geometry and generated textures.';
requestAnimationFrame(frame);
