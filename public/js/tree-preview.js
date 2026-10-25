import * as THREE from 'three';
import { buildGrown } from './grow.js';

const SPECIES = [
  ['redcedar', 'Eastern redcedar', 'Narrow evergreen crown · scale-like foliage'],
  ['douglasfir', 'Douglas-fir', 'Tall western fir · layered, drooping limbs'],
  ['ponderosa', 'Ponderosa pine', 'Open western pine · orange plated bark'],
  ['beech', 'American beech', 'Dense rounded crown · smooth gray bark'],
];
const canvas = document.querySelector('#view'), status = document.querySelector('#status');
const renderer = new THREE.WebGLRenderer({ canvas, antialias: true, alpha: false });
renderer.setPixelRatio(Math.min(devicePixelRatio, 1.75));
renderer.outputColorSpace = THREE.SRGBColorSpace;
renderer.toneMapping = THREE.ACESFilmicToneMapping;
renderer.toneMappingExposure = 1.4;
renderer.setScissorTest(true);

const scenes = [], views = [];
const loader = new THREE.TextureLoader();
const texture = async (path, color = false) => {
  const t = await loader.loadAsync(path);
  if (color) t.colorSpace = THREE.SRGBColorSpace;
  return t;
};
const leafMaps = await Promise.all(SPECIES.map(async ([kind]) => {
  const t = await texture(`/textures/trees/${kind}_leaves.png`, true);
  t.premultiplyAlpha = true;
  return t;
}));
const barkMaps = await Promise.all(SPECIES.map(async ([kind]) => ({
  map: await texture(`/textures/trees/${kind}_bark_color.jpg`, true),
  normal: await texture(`/textures/trees/${kind}_bark_normal.jpg`),
})));
for (const { map, normal } of barkMaps) {
  map.wrapS = map.wrapT = normal.wrapS = normal.wrapT = THREE.RepeatWrapping;
}

for (let i = 0; i < SPECIES.length; i++) {
  const [kind] = SPECIES[i], { bark, leaves } = buildGrown(kind, 7100 + i, 'near');
  const scene = new THREE.Scene();
  scene.background = new THREE.Color('#1a281e');
  scene.add(new THREE.HemisphereLight(0xe5f1df, 0x59634b, 3.2));
  const sun = new THREE.DirectionalLight(0xffe8c9, 3.8);
  sun.position.set(-35, 70, 55);
  scene.add(sun);
  const ground = new THREE.Mesh(new THREE.CircleGeometry(18, 48), new THREE.MeshStandardMaterial({ color: '#344334', roughness: 1 }));
  ground.rotation.x = -Math.PI / 2;
  ground.position.y = -0.2;
  scene.add(ground);

  const b = barkMaps[i], leaf = leafMaps[i];
  const barkMat = new THREE.MeshStandardMaterial({ map: b.map, normalMap: b.normal, roughness: 0.94 });
  const leafMat = new THREE.MeshStandardMaterial({ map: leaf, alphaTest: 0.48, side: THREE.DoubleSide, roughness: 0.92 });
  scene.add(new THREE.Mesh(bark, barkMat), new THREE.Mesh(leaves, leafMat));

  const camera = new THREE.PerspectiveCamera(34, 1, 0.1, 180);
  const view = { scene, camera, yaw: 0.42, pitch: 0.08, radius: 105, target: new THREE.Vector3(0, 29, 0), index: i };
  views.push(view);
  scenes.push(scene);
}

function orient(v) {
  const radius = v.radius, cp = Math.cos(v.pitch);
  v.camera.position.set(v.target.x + Math.sin(v.yaw) * radius * cp, v.target.y + Math.sin(v.pitch) * radius, v.target.z + Math.cos(v.yaw) * radius * cp);
  v.camera.lookAt(v.target);
}
function resize() {
  const rect = canvas.getBoundingClientRect();
  renderer.setSize(rect.width, rect.height, false);
  const w = rect.width / 2, h = rect.height / 2;
  for (const v of views) { v.camera.aspect = w / h; v.camera.updateProjectionMatrix(); orient(v); }
}
function frame() {
  const rect = canvas.getBoundingClientRect();
  const w = rect.width / 2, h = rect.height / 2;
  renderer.setScissor(0, h, w, h); renderer.setViewport(0, h, w, h); renderer.render(views[0].scene, views[0].camera);
  renderer.setScissor(w, h, w, h); renderer.setViewport(w, h, w, h); renderer.render(views[1].scene, views[1].camera);
  renderer.setScissor(0, 0, w, h); renderer.setViewport(0, 0, w, h); renderer.render(views[2].scene, views[2].camera);
  renderer.setScissor(w, 0, w, h); renderer.setViewport(w, 0, w, h); renderer.render(views[3].scene, views[3].camera);
  requestAnimationFrame(frame);
}

const labels = document.createElement('div');
labels.style.cssText = 'position:absolute;inset:0;pointer-events:none;display:grid;grid-template-columns:1fr 1fr;grid-template-rows:1fr 1fr;';
canvas.parentElement.style.position = 'relative';
canvas.parentElement.append(labels);
SPECIES.forEach(([, name, detail], i) => {
  const tag = document.createElement('div');
  tag.style.cssText = `align-self:${i < 2 ? 'start' : 'end'};justify-self:${i % 2 ? 'end' : 'start'};margin:14px;padding:10px 12px;border-radius:9px;background:#101913dd;color:#edf1e7;max-width:48%;`;
  tag.innerHTML = `<strong>${name}</strong><br><span style="color:#b7c4b4;font-size:.84rem">${detail}</span>`;
  labels.append(tag);
});

let active = null, last = null;
function viewAt(x, y) {
  const r = canvas.getBoundingClientRect();
  return (y - r.top >= r.height / 2 ? 2 : 0) + (x - r.left >= r.width / 2 ? 1 : 0);
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
  v.radius = THREE.MathUtils.clamp(v.radius * Math.exp(e.deltaY * 0.001), 28, 155);
  orient(v);
}, { passive: false });
new ResizeObserver(resize).observe(canvas);
resize();
status.textContent = 'Drag a panel to rotate its tree; use the mouse wheel or trackpad over a panel to zoom. Each preview uses the game’s procedural geometry and generated textures.';
requestAnimationFrame(frame);
