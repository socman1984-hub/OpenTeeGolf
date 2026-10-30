// Small 3D previews for the course editor, drawn with the game's own models and materials:
//   tree  - one tree of a species, at its typical height, with a golfer beside it for scale
//   rough - the rough at the course's height, close up round a ball sitting down in it
//   tall  - a stand of tall (native) grass at the course's height, with a golfer at its edge
//   plant - a shrub, flower bed or grass as the game draws it, with a golfer beside it
// Drag to turn the view. Each preview draws only while it's on screen.
import * as THREE from 'three/webgpu';
import { previewTree } from './trees.js';
import { tuftGeometry, nativeClumpGeometry, NATIVE_KINDS, grassMaterial } from './grass.js';
import { TREE_SPECIES } from './treeplace.js';
import { GRASS_RANGE, grassCm } from './mowing.js';
import { Plants, PLANTS } from './plants.js';

const rand = (s) => () => { s = (s * 16807) % 2147483647; return s / 2147483647; };

// A golfer (1.8 m) for scale: legs, body, arms and head in plain colours
function golfer() {
  const g = new THREE.Group(), shirt = new THREE.MeshStandardMaterial({ color: 0x2f5f9e, roughness: 0.8 });
  const trousers = new THREE.MeshStandardMaterial({ color: 0x3a3a40, roughness: 0.9 }), skin = new THREE.MeshStandardMaterial({ color: 0xd9a77f, roughness: 0.8 });
  const part = (geo, mat, x, y, z) => { const m = new THREE.Mesh(geo, mat); m.position.set(x, y, z); m.castShadow = true; g.add(m); };
  for (const x of [-0.1, 0.1]) part(new THREE.CapsuleGeometry(0.07, 0.72, 4, 8), trousers, x, 0.44, 0);
  part(new THREE.CapsuleGeometry(0.17, 0.42, 4, 10), shirt, 0, 1.18, 0);
  for (const x of [-0.24, 0.24]) part(new THREE.CapsuleGeometry(0.05, 0.5, 4, 8), shirt, x, 1.12, 0);
  part(new THREE.SphereGeometry(0.11, 16, 12), skin, 0, 1.66, 0);
  return g;
}

class View {
  constructor(canvas) {
    this.canvas = canvas;
    this.renderer = new THREE.WebGPURenderer({ canvas, antialias: true });
    this.renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
    this.renderer.shadowMap.enabled = true;
    this.renderer.toneMapping = THREE.ACESFilmicToneMapping;
    this.scene = new THREE.Scene();
    this.scene.background = new THREE.Color(0xb9d3e8);
    this.scene.add(new THREE.HemisphereLight(0xdfeeff, 0x4a5a30, 2.2));
    this.sun = new THREE.DirectionalLight(0xfff1dc, 2.6);
    this.sun.castShadow = true;
    this.sun.shadow.mapSize.set(1024, 1024);
    this.scene.add(this.sun, this.sun.target);
    this.camera = new THREE.PerspectiveCamera(35, 1, 0.01, 500);
    this.camera.layers.enable(1); // (the game draws plants on layer 1: kept out of pond reflections)
    this.content = new THREE.Group();
    this.scene.add(this.content);
    this.orbit = { yaw: 0.6, pitch: 0.12, dist: 10, target: new THREE.Vector3() };
    this.ready = this.renderer.init();
    // drag to turn
    let last = null;
    canvas.addEventListener('pointerdown', (e) => { last = [e.clientX, e.clientY]; canvas.setPointerCapture(e.pointerId); });
    canvas.addEventListener('pointermove', (e) => {
      if (!last) return;
      this.orbit.yaw -= (e.clientX - last[0]) * 0.01;
      this.orbit.pitch = Math.min(1.2, Math.max(0.02, this.orbit.pitch + (e.clientY - last[1]) * 0.006));
      last = [e.clientX, e.clientY];
      this.render();
    });
    canvas.addEventListener('pointerup', () => { last = null; });
    // only drawn (and swaying) while on screen
    this.visible = false;
    new IntersectionObserver(([e]) => { this.visible = e.isIntersecting && e.intersectionRatio > 0; this.loop(); }).observe(canvas);
  }

  frame(target, dist, pitch = 0.12) {
    this.orbit.target.copy(target); this.orbit.dist = dist; this.orbit.pitch = pitch;
    // (the sun from the front left, its shadows covering what's shown)
    const s = this.sun.shadow.camera, r = Math.max(2, dist * 0.7);
    s.left = -r; s.right = r; s.top = r; s.bottom = -r; s.near = 0.1; s.far = dist * 6 + 20; s.updateProjectionMatrix();
    this.sun.position.copy(target).add(new THREE.Vector3(-0.6, 1.1, 0.8).normalize().multiplyScalar(dist * 2 + 10));
    this.sun.target.position.copy(target);
  }

  async render() {
    await this.ready;
    const c = this.canvas, w = c.clientWidth, h = c.clientHeight;
    if (!w || !h) return;
    if (c.width !== Math.round(w * this.renderer.getPixelRatio())) this.renderer.setSize(w, h, false);
    this.camera.aspect = w / h; this.camera.updateProjectionMatrix();
    const o = this.orbit, cp = Math.cos(o.pitch);
    this.camera.position.set(o.target.x + Math.sin(o.yaw) * o.dist * cp, o.target.y + Math.sin(o.pitch) * o.dist, o.target.z + Math.cos(o.yaw) * o.dist * cp);
    this.camera.lookAt(o.target);
    this.renderer.render(this.scene, this.camera);
  }

  loop() {
    if (this.looping || !this.visible) return;
    this.looping = true;
    const step = async () => {
      if (!this.visible || document.hidden) { this.looping = false; return; }
      await this.render();
      setTimeout(() => requestAnimationFrame(step), 33); // (about 30 a second: enough for the grass to sway)
    };
    step();
  }

  clear() {
    for (const o of [...this.content.children]) {
      this.content.remove(o);
      o.traverse((m) => { if (m.isMesh && !m.userData.keep) m.geometry?.dispose(); });
    }
  }
}

const ground = (size, colour, y = 0) => {
  const m = new THREE.Mesh(new THREE.CircleGeometry(size, 48).rotateX(-Math.PI / 2), new THREE.MeshStandardMaterial({ color: colour, roughness: 1 }));
  m.position.y = y; m.receiveShadow = true;
  return m;
};

// ------------------------------------------------------------------ trees

export class TreePreview extends View {
  constructor(canvas) { super(canvas); this.cache = new Map(); this.kind = null; }

  // Show a species; returns its label and typical height for the caption
  async show(kind) {
    const sp = TREE_SPECIES.find((s) => s.id === kind);
    if (!sp) return null;
    this.kind = kind;
    if (!this.cache.has(kind)) this.cache.set(kind, previewTree(kind));
    const model = await this.cache.get(kind);
    if (this.kind !== kind || !model) return sp;
    this.clear();
    const H = sp.h, s = H / model.height;
    model.group.scale.setScalar(s);
    model.group.traverse((m) => { m.userData.keep = true; });
    const person = golfer();
    person.position.set(Math.max(2.5, H * 0.32), 0, H * 0.12);
    person.rotation.y = -0.6;
    this.content.add(ground(Math.max(40, H * 5), 0x5f8a3c), model.group, person);
    this.frame(new THREE.Vector3(0, H * 0.48, 0), H * 1.6 + 4, 0.08);
    await this.render();
    this.loop();
    return sp;
  }
}

// ------------------------------------------------------------------ grass

export class GrassPreview extends View {
  constructor(canvas) {
    super(canvas);
    this.view = 'rough';
    this.scale = { rough: 0.8, native: 1 };
    this.material = grassMaterial({ sway: 0.004 });
    this.tufts = [0, 1, 2].map(() => tuftGeometry({ blades: 11, spread: 0.5 }));
    this.clumps = NATIVE_KINDS.map((k) => nativeClumpGeometry(k));
    for (const g of [...this.tufts, ...this.clumps]) g.userData = { keep: true };
  }

  // The rough's and the tall grass's scales (mowing.js GRASS_RANGE), and which to show
  async show(view, scale) {
    this.view = view; Object.assign(this.scale, scale);
    this.clear();
    if (view === 'rough') this.buildRough(); else this.buildTall();
    await this.render();
    this.loop();
    return { cm: grassCm(view === 'rough' ? 'rough' : 'native', this.scale[view === 'rough' ? 'rough' : 'native']) };
  }

  // Instanced grass: list of [geometry index, x, z, height, width, colour]
  addGrass(geos, list) {
    const m4 = new THREE.Matrix4(), q = new THREE.Quaternion(), up = new THREE.Vector3(0, 1, 0), c = new THREE.Color();
    geos.forEach((geo, gi) => {
      const mine = list.filter((t) => t[0] === gi);
      if (!mine.length) return;
      const mesh = new THREE.InstancedMesh(geo, this.material, mine.length);
      mesh.userData.keep = true;
      mesh.instanceColor = new THREE.InstancedBufferAttribute(new Float32Array(mine.length * 3), 3);
      mine.forEach(([, x, z, h, w, col, turn], i) => {
        mesh.setMatrixAt(i, m4.compose(new THREE.Vector3(x, -0.004, z), q.setFromAxisAngle(up, turn), new THREE.Vector3(w, h, w)));
        mesh.setColorAt(i, c.setRGB(...col, THREE.SRGBColorSpace));
      });
      mesh.receiveShadow = true;
      this.content.add(mesh);
    });
  }

  // Close up: a ball sitting down in the rough, the fairway's edge just behind it
  buildRough() {
    const r = rand(7), s = this.scale.rough, list = [];
    for (let i = 0; i < 2600; i++) {
      const x = (r() - 0.5) * 2.4, z = (r() - 0.5) * 2.4;
      if (z < -0.7) continue; // (the fairway, mown, behind)
      // (as the game: tufts 5.5-9.5 x a little more near the fairway, times the course's height)
      const h = (0.055 + r() * 0.04) * (1.1 + 0.5 * r()) * s / 1.1;
      const v = 0.85 + r() * 0.3;
      list.push([i % 3, x, z, h, h * (1.2 + r() * 0.8), [0.32 * v, 0.42 * v, 0.22 * v], r() * 6.28]);
    }
    this.addGrass(this.tufts, list);
    const fairway = new THREE.Mesh(new THREE.PlaneGeometry(2.6, 1.0).rotateX(-Math.PI / 2), new THREE.MeshStandardMaterial({ color: 0x5c9a3a, roughness: 1 }));
    fairway.position.set(0, 0.001, -1.2); fairway.receiveShadow = true;
    const ball = new THREE.Mesh(new THREE.SphereGeometry(0.02135, 24, 16), new THREE.MeshStandardMaterial({ color: 0xf6f6f2, roughness: 0.35 }));
    ball.position.set(0, 0.02135 * 0.7, 0.05); ball.castShadow = true;
    this.content.add(ground(3, 0x3d5a26), fairway, ball);
    this.frame(new THREE.Vector3(0, 0.03, 0), 0.75, 0.32);
  }

  // A stand of tall grass, ordinary rough in front, a golfer at its edge
  buildTall() {
    const r = rand(11), s = this.scale.native, list = [];
    for (let i = 0; i < 1400; i++) {
      const x = (r() - 0.5) * 12, z = -r() * 7;
      const kind = r() < 0.4 ? 0 : r() < 0.5 ? 1 : r() < 0.6 ? 2 : 3;
      const H = [[0.55, 1.0], [0.75, 1.2], [0.45, 0.75], [0.4, 0.65]][kind];
      const h = (H[0] + r() * (H[1] - H[0])) * s, straw = r(), v = 0.88 + r() * 0.24;
      const col = [0.68 * (0.74 + straw * 0.5) * v, 0.7 * (0.97 + straw * 0.06) * v, 0.42 * (0.6 + straw * 0.18) * v];
      list.push([kind, x, z, h, 1.1 + r() * 0.7, col, r() * 6.28]);
    }
    this.addGrass(this.clumps, list);
    const person = golfer();
    person.position.set(1.2, 0, 0.6);
    person.rotation.y = 0.3;
    this.content.add(ground(14, 0x4a6d2e), person);
    this.frame(new THREE.Vector3(0, 0.7, -1), 6.5, 0.12);
  }
}

// ------------------------------------------------------------------ shrubs, flowers and grasses

export class PlantPreview extends View {
  constructor(canvas) { super(canvas); this.plants = new Plants(); this.kind = null; }

  async show(kind) {
    const P = PLANTS[kind];
    if (!P) return null;
    this.kind = kind;
    // a few of them as they'd grow: a short row of hedge, a bed of flowers, a small group of the rest
    const spots = [], r = rand(kind.length * 977 + 3);
    const at = (x, z, k = 1) => spots.push({ kind, x, z, y: 0, w: P.w * k, d: P.w * k, h: P.h * k, rot: kind === 'hedge' ? 0 : r() * 6.28, tint: r() });
    if (kind === 'hedge') for (let i = 0; i < 6; i++) at(i * 0.95 - 2.4, 0, 1);
    else if (P.form === 'flowers') { for (let i = 0; i < 4; i++) for (let j = 0; j < 2; j++) at(i * 1.0 - 1.5, j * 1.0 - 0.5, 0.95); }
    else { at(0, 0, 1); at(-P.w * 1.1, -P.w * 0.6, 0.8); at(P.w * 0.9, -P.w * 1.1, 0.7); }
    // (cleared first: the plants' meshes are made afresh by build)
    this.clear();
    await this.plants.build(spots, {});
    if (this.kind !== kind) return P;
    this.content.add(ground(30, 0x5f8a3c), this.plants.group);
    const person = golfer();
    person.position.set(Math.max(1.6, P.w * 1.5 + (kind === 'hedge' ? 2.4 : 0)), 0, P.w * 0.6);
    person.rotation.y = -0.6;
    this.content.add(person);
    const H = Math.max(1.8, P.h);
    // (looking between the plants and the golfer, so both are in the picture)
    this.frame(new THREE.Vector3(person.position.x * 0.4, H * 0.4, 0), H * 1.6 + P.w * 1.2 + 2.5, 0.22);
    await this.render();
    this.loop();
    return P;
  }
}

export { GRASS_RANGE };
