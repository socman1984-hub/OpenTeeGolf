// Three.js rendering of a course built by course.js, on WebGPU (falls back to WebGL 2 where
// WebGPU isn't available; add ?webgl to the address to force that).
// Course space is left-handed (Unity); three.js is right-handed, so x is mirrored: T(x,y,z) = (-x, y, z).

import * as THREE from 'three/webgpu';
import {
  Fn, texture, uv, vec2, vec3, float, mix, clamp, smoothstep, dot, distance, normalize, sin, cos, abs, atan,
  positionWorld, cameraPosition, instancedBufferAttribute, PI, normalMap, time, reflector, normalView, positionViewDirection, pow,
} from 'three/tsl';
import { SkyMesh } from 'three/addons/objects/SkyMesh.js';
import { Line2 } from 'three/addons/lines/webgpu/Line2.js';
import { LineGeometry } from 'three/addons/lines/LineGeometry.js';
import { MapControls } from 'three/addons/controls/MapControls.js';
import { mergeGeometries } from 'three/addons/utils/BufferGeometryUtils.js';
import { noise, pointInPoly } from './course.js';
import { Forest, treesFromCanopy, assignSpecies, fade, impostorLight } from './trees.js';

const T = (x, y, z) => new THREE.Vector3(-x, y, z);
const TEX_RES = 1.5; // meters per texel of the generated (no imagery) course texture

const SURFACE_RGB = {
  green: [92, 168, 64],
  fringe: [84, 150, 56],
  fairway: [102, 156, 58],
  tee: [104, 160, 60],
  rough: [62, 108, 40],
  firstcut: [84, 138, 50],
  deeprough: [50, 88, 34],
  water: [60, 70, 50],
  oob: [56, 96, 36],
  sand: [214, 196, 150],
};
const TEE_COLORS = { Black: 0x222222, Blue: 0x2255dd, White: 0xffffff, Green: 0x22aa33, Yellow: 0xeecc22, Red: 0xdd2222, Junior: 0xff8800, Par3: 0x999999 };
const SUN_DIR = new THREE.Vector3().setFromSphericalCoords(1, THREE.MathUtils.degToRad(48), THREE.MathUtils.degToRad(215));

export class CourseScene {
  constructor(container) {
    this.container = container;
    const forceWebGL = new URLSearchParams(location.search).has('webgl');
    this.renderer = new THREE.WebGPURenderer({ antialias: true, powerPreference: 'high-performance', forceWebGL });
    this.renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
    this.renderer.toneMapping = THREE.ACESFilmicToneMapping;
    this.renderer.toneMappingExposure = 0.75;
    this.renderer.shadowMap.enabled = true;
    this.renderer.shadowMap.type = THREE.PCFSoftShadowMap;
    container.appendChild(this.renderer.domElement);

    this.scene = new THREE.Scene();
    this.scene.fog = new THREE.FogExp2(0xc4d8e6, 0.00032);
    this.camera = new THREE.PerspectiveCamera(55, 1, 0.05, 9000);
    this.camPos = new THREE.Vector3(0, 50, -50);
    this.camLook = new THREE.Vector3();
    this.camGoalPos = this.camPos.clone();
    this.camGoalLook = new THREE.Vector3();

    // Sky (with drifting clouds); the environment map for sky reflections is made once the GPU is ready
    this.sky = makeSky();
    this.scene.add(this.sky);
    this.scene.environmentIntensity = 0.55;

    this.hemi = new THREE.HemisphereLight(0xdcecff, 0x5b6e3c, 0.7);
    this.scene.add(this.hemi);
    this.weather = { windMps: 0, windFromDeg: 0, rainMm: 0, cloud: 0.2 };
    this.sun = new THREE.DirectionalLight(0xfff1dc, 3.0);
    this.sun.castShadow = true;
    this.sun.shadow.mapSize.set(4096, 4096);
    const sc = this.sun.shadow.camera;
    sc.left = -220; sc.right = 220; sc.top = 220; sc.bottom = -220; sc.near = 10; sc.far = 1500;
    this.sun.shadow.bias = -0.0004;
    this.sun.shadow.normalBias = 0.6;
    this.scene.add(this.sun, this.sun.target);

    this.courseGroup = new THREE.Group();
    this.scene.add(this.courseGroup);
    this.dynamic = new THREE.Group();
    this.scene.add(this.dynamic);

    // Ball (drawn larger than life so it stays visible) and its soft contact shadow
    this.ball = new THREE.Mesh(new THREE.SphereGeometry(0.12, 24, 16), new THREE.MeshStandardMaterial({ color: 0xffffff, roughness: 0.3, emissive: 0x222222 }));
    this.dynamic.add(this.ball);
    this.ballShadow = new THREE.Mesh(new THREE.CircleGeometry(0.16, 24), new THREE.MeshBasicMaterial({ map: radialTexture(), transparent: true, depthWrite: false, opacity: 0.55 }));
    this.ballShadow.rotation.x = -Math.PI / 2;
    this.dynamic.add(this.ballShadow);

    // (Lines are drawn solid: a see-through Line2 material samples a shared copy of the screen, which
    // the water's reflection pass resizes every frame, and the GPU then rejects every frame - the
    // picture freezes. Fainter lines get a darker colour instead.)
    this.trailMat = new THREE.Line2NodeMaterial({ color: 0xffe14d, linewidth: 3, depthTest: true });
    this.trail = null;
    this.ghostTrails = [];

    this.aimLine = null;
    this.aimMarker = this.makeAimMarker();
    this.dynamic.add(this.aimMarker);
    this.flag = this.makeFlag();
    this.dynamic.add(this.flag);

    // Free-roaming camera (map style: drag to pan, right-drag to rotate, wheel to zoom)
    this.controls = new MapControls(this.camera, this.renderer.domElement);
    this.controls.enabled = false;
    this.controls.enableDamping = true;
    this.controls.dampingFactor = 0.12;
    this.controls.maxPolarAngle = THREE.MathUtils.degToRad(86);
    this.controls.minDistance = 2;
    this.controls.maxDistance = 2500;
    this.freeKeys = new Set();

    this.raycaster = new THREE.Raycaster();
    this.setQuality('balanced');
    this.stats = { frames: 0, last: performance.now(), worst: 0, fps: 0, onUpdate: null };
    this.mode = 'address';
    this.clock = new THREE.Clock();
    this.onFrame = null;
    window.addEventListener('resize', () => this.resize());
    this.resize();
    // WebGPU starts asynchronously: build() and the render loop wait for it
    // If the graphics card resets (driver crash, sleep), the picture can't recover by itself
    this.renderer.onDeviceLost = (info) => {
      console.error('GPU device lost', info);
      this.onDeviceLost?.(info);
    };
    this.ready = this.renderer.init().then(() => {
      this.backend = this.renderer.backend.isWebGPUBackend ? 'WebGPU' : 'WebGL 2';
      const skyScene = new THREE.Scene();
      const envSky = makeSky();
      envSky.cloudCoverage.value = 0;
      envSky.showSunDisc.value = 0; // the sun is the directional light; baked in, it floods everything with light
      skyScene.add(envSky);
      this.scene.environment = new THREE.PMREMGenerator(this.renderer).fromScene(skyScene).texture;
      this.renderer.setAnimationLoop(() => this.frame());
    });
  }

  // 'high' | 'balanced' | 'performance': resolution, shadow detail and vegetation density
  setQuality(q) {
    const Q = {
      high: { dpr: 2, shadow: 4096, trees: 1, grass: 32000, native: 1 },
      balanced: { dpr: 1.25, shadow: 2048, trees: 0.85, grass: 20000, native: 0.7 },
      performance: { dpr: 1, shadow: 1024, trees: 0.6, grass: 9000, native: 0.4 },
    }[q] || null;
    if (!Q) return;
    this.quality = { name: q, ...Q };
    this.renderer.setPixelRatio(Math.min(devicePixelRatio, Q.dpr));
    if (this.sun.shadow.mapSize.x !== Q.shadow) {
      this.sun.shadow.mapSize.set(Q.shadow, Q.shadow);
      this.sun.shadow.map?.dispose();
      this.sun.shadow.map = null;
    }
    if (this.grass) { this.dynamic.remove(this.grass); this.grass = null; this.grassCenter = null; }
    this.lastSize = null; // water reflection detail follows the setting too
    this.resize?.();
  }

  resize() {
    const w = this.container.clientWidth, h = this.container.clientHeight;
    const pr = this.renderer.getPixelRatio();
    const changed = w !== this.lastSize?.w || h !== this.lastSize?.h || pr !== this.lastSize?.pr;
    this.lastSize = { w, h, pr };
    this.renderer.setSize(w, h);
    this.camera.aspect = w / h;
    this.camera.updateProjectionMatrix();
    if (changed && this.waterMeshes?.length) this.makeWaterMaterial();
  }

  // ------------------------------------------------------------------ course

  // imagery: { manifest, url(file) } or null; buildings: [{ h, pts }]
  // progress(stepId, label, done?, total?, detail?) may return a promise (to let the page repaint)
  async build(course, { imagery = null, buildings = [], canopy = null } = {}, progress = async () => {}) {
    await this.ready;
    await progress('textures', 'Ground and water textures');
    await preloadTextures();
    this.course = course;
    this.courseGroup.clear();
    const { minX, maxX, minZ, maxZ } = course.bounds;
    const { nx, nz, cell, heights } = course.grid;
    let hmin = Infinity;
    for (const v of heights) hmin = Math.min(hmin, v);
    this.hmin = hmin;

    // Terrain mesh
    await progress('terrain', 'Building terrain');
    const pos = new Float32Array(nx * nz * 3), uv = new Float32Array(nx * nz * 2);
    for (let j = 0; j < nz; j++) for (let i = 0; i < nx; i++) {
      const k = j * nx + i, x = minX + i * cell, z = minZ + j * cell;
      pos[k * 3] = -x; pos[k * 3 + 1] = heights[k]; pos[k * 3 + 2] = z;
      uv[k * 2] = (x - minX) / (maxX - minX); uv[k * 2 + 1] = (z - minZ) / (maxZ - minZ);
    }
    const idx = new Uint32Array((nx - 1) * (nz - 1) * 6);
    let o = 0;
    for (let j = 0; j < nz - 1; j++) for (let i = 0; i < nx - 1; i++) {
      const a = j * nx + i, b = a + 1, c = a + nx, d = c + 1;
      idx[o++] = a; idx[o++] = b; idx[o++] = c; idx[o++] = b; idx[o++] = d; idx[o++] = c;
    }
    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.BufferAttribute(pos, 3));
    geo.setAttribute('uv', new THREE.BufferAttribute(uv, 2));
    geo.setIndex(new THREE.BufferAttribute(idx, 1));
    geo.computeVertexNormals();
    if (geo.attributes.normal.getY(0) < 0) { // ensure the surface faces up
      for (let i = 0; i < idx.length; i += 3) { const t = idx[i + 1]; idx[i + 1] = idx[i + 2]; idx[i + 2] = t; }
      geo.computeVertexNormals();
    }

    // Ground texture: aerial imagery blended with the mapped surfaces, or painted from the surfaces
    let sat = null;
    if (imagery) {
      try {
        sat = await this.loadImagery(course, imagery, (done, total) => progress('photos', 'Aerial photos into the game', done, total));
      } catch (err) {
        console.warn('Imagery failed, painting the course instead', err);
      }
    }
    await progress('paint', 'Painting the course');
    this.courseCanvas = sat ? this.paintFromImagery(course, sat) : this.paintCourse(course);
    this.imagery = sat;
    const tex = new THREE.CanvasTexture(this.courseCanvas);
    tex.colorSpace = THREE.SRGBColorSpace;
    tex.anisotropy = this.renderer.getMaxAnisotropy();
    const mask = this.buildSurfaceMask(course);
    const mat = terrainMaterial(tex, softTexture(this.courseCanvas), mask, this.buildMowDirections(course), sat ? 0.8 : 0.9);
    this.makeSurfaceLookup(course, mask);
    // Small copy of the course picture for the minimap (redrawing from the full one is slow)
    const ms = Math.min(1, 2048 / Math.max(this.courseCanvas.width, this.courseCanvas.height));
    this.minimapCanvas = document.createElement('canvas');
    this.minimapCanvas.width = Math.round(this.courseCanvas.width * ms); this.minimapCanvas.height = Math.round(this.courseCanvas.height * ms);
    this.minimapCanvas.getContext('2d').drawImage(this.courseCanvas, 0, 0, this.minimapCanvas.width, this.minimapCanvas.height);
    this.terrain = new THREE.Mesh(geo, mat);
    this.terrain.receiveShadow = true;
    this.courseGroup.add(this.terrain);

    // Ground beyond the modeled area: imagery out to the horizon when we have it
    if (sat?.surround) this.addSurround(course, sat);
    else {
      const outer = new THREE.Mesh(new THREE.PlaneGeometry(14000, 14000), new THREE.MeshStandardMaterial({ color: 0x46703a, roughness: 1 }));
      outer.rotation.x = -Math.PI / 2;
      outer.position.set(-(minX + maxX) / 2, hmin - 1.5, (minZ + maxZ) / 2);
      outer.receiveShadow = true;
      this.courseGroup.add(outer);
    }

    this.addWater(course);
    this.addStakesAndMarkers(course);
    if (canopy) this.addTreesFromCanopy(course, canopy);
    else if (sat) this.addTreesFromImagery(course, sat);
    else this.addTrees(course);
    await this.plantForest(course, this.pendingTrees || [], progress);
    if (sat) this.addNativeGrass(course, sat);
    if (buildings.length) this.addBuildings(course, buildings);
    this.grassCenter = null;
    this.applyEnvIntensity(this.courseGroup);
    this.applyEnvIntensity(this.dynamic);
    // Build every shader now rather than stalling on the first frames of play
    await progress('shaders', 'Preparing graphics');
    try { await this.renderer.compileAsync(this.scene, this.camera); } catch (err) { console.warn('Shader warm-up skipped', err); }
  }

  // Node materials use the scene environment at full strength and ignore envMapIntensity unless
  // they have their own envMap; give them the sky map so each keeps its own reflection strength.
  applyEnvIntensity(target) {
    const env = this.scene.environment;
    if (!env) return;
    const fix = (m) => {
      if (!m || m.envMap || m.envMapIntensity === undefined || m.envMapIntensity === 1) return;
      m.envMap = env;
      m.envMapIntensity *= this.scene.environmentIntensity;
      m.needsUpdate = true;
    };
    if (target.isMaterial) return fix(target);
    target.traverse((o) => (Array.isArray(o.material) ? o.material.forEach(fix) : fix(o.material)));
  }

  // ---- imagery

  async loadImagery(course, { manifest, url }, onCount = () => {}) {
    const load = (file) => new Promise((resolve, reject) => {
      const img = new Image();
      img.onload = () => resolve(img);
      img.onerror = () => reject(new Error(`Could not load ${file}`));
      img.src = url(file);
    });
    const total = manifest.chunks.length + (manifest.surround?.length || 0);
    let done = 0;
    const counted = (p) => p.then((v) => { onCount(++done, total); return v; });
    onCount(0, total);
    const chunks = await Promise.all(manifest.chunks.map(async (c) => ({ ...c, img: await counted(load(c.file)) })));
    let surround = null;
    if (manifest.surround?.length) {
      try { surround = await Promise.all(manifest.surround.map(async (c) => ({ ...c, img: await counted(load(c.file)) }))); } catch { surround = null; }
    }
    // lat/lon -> course (inverse of the affine geo transform)
    const [a0, a1, a2] = manifest.geo.lat, [b0, b1, b2] = manifest.geo.lon, det = a1 * b2 - a2 * b1;
    const toCourse = (lat, lon) => { const dl = lat - a0, dn = lon - b0; return [(b2 * dl - a2 * dn) / det, (a1 * dn - b1 * dl) / det]; };
    return { chunks, surround, toCourse, attribution: manifest.attribution };
  }

  // Draw an image (lat/lon extent) into a canvas whose pixels map course space: px = (x - x0)/res, py = (z1 - z)/res
  drawGeoImage(ctx, chunk, sat, x0, z1, res) {
    const e = chunk.extent;
    const map = (u, v) => {
      const lon = e.minLon + (u / chunk.width) * (e.maxLon - e.minLon);
      const lat = e.maxLat - (v / chunk.height) * (e.maxLat - e.minLat);
      const [x, z] = sat.toCourse(lat, lon);
      return [(x - x0) / res, (z1 - z) / res];
    };
    const p0 = map(0, 0), pu = map(1, 0), pv = map(0, 1);
    ctx.setTransform(pu[0] - p0[0], pu[1] - p0[1], pv[0] - p0[0], pv[1] - p0[1], p0[0], p0[1]);
    ctx.drawImage(chunk.img, 0, 0);
    ctx.setTransform(1, 0, 0, 1, 0, 0);
  }

  paintFromImagery(course, sat) {
    const { minX, maxX, minZ, maxZ } = course.bounds;
    const res = Math.max(0.5, Math.max(maxX - minX, maxZ - minZ) / 8192);
    const w = Math.ceil((maxX - minX) / res), h = Math.ceil((maxZ - minZ) / res);
    const canvas = document.createElement('canvas');
    canvas.width = w; canvas.height = h;
    const ctx = canvas.getContext('2d');
    ctx.fillStyle = '#4b6b35';
    ctx.fillRect(0, 0, w, h);
    // Aerial photos, colour balanced (survey imagery tends to be blue/grey) and graded richer
    for (const c of sat.chunks) this.drawGeoImage(ctx, c, sat, minX, maxZ, res);
    sat.balance = colorBalance(ctx, w, h);
    // Keep a small copy of the raw photo for tree detection and grass colours
    const s = 2;
    const sample = document.createElement('canvas');
    sample.width = Math.ceil((maxX - minX) / s); sample.height = Math.ceil((maxZ - minZ) / s);
    sample.getContext('2d').drawImage(canvas, 0, 0, sample.width, sample.height);
    sat.sample = { data: sample.getContext('2d').getImageData(0, 0, sample.width, sample.height).data, w: sample.width, h: sample.height, res: s, minX, maxZ };

    const path = (poly) => {
      ctx.beginPath();
      poly.forEach(([x, z], i) => { const px = (x - minX) / res, py = (maxZ - z) / res; if (i) ctx.lineTo(px, py); else ctx.moveTo(px, py); });
      ctx.closePath();
    };
    const fill = (surface, style, alpha, op = 'source-over') => {
      ctx.globalAlpha = alpha; ctx.globalCompositeOperation = op; ctx.fillStyle = style;
      for (const f of course.features) if (f.surface === surface && !f.inner) { path(f.poly); ctx.fill(); }
      ctx.globalAlpha = 1; ctx.globalCompositeOperation = 'source-over';
    };
    // Fairways the map is missing (and tree cover) detected from the photo itself
    this.detectSurfaces(course, sat, canvas, res);
    // All fairway area: mapped polygons plus detected mown grass
    const fwMask = document.createElement('canvas');
    fwMask.width = w; fwMask.height = h;
    const fm = fwMask.getContext('2d');
    if (sat.overlay) {
      const o = sat.overlay, oc = document.createElement('canvas');
      oc.width = o.w; oc.height = o.h;
      const oi = oc.getContext('2d').createImageData(o.w, o.h);
      for (let i = 0; i < o.codes.length; i++) if (o.codes[i] === 1) { oi.data[i * 4] = oi.data[i * 4 + 1] = oi.data[i * 4 + 2] = oi.data[i * 4 + 3] = 255; }
      oc.getContext('2d').putImageData(oi, 0, 0);
      fm.imageSmoothingEnabled = true;
      fm.drawImage(oc, 0, 0, o.w * o.res / res, o.h * o.res / res);
    }
    fm.fillStyle = '#fff';
    for (const f of course.features) if (f.surface === 'fairway' && !f.inner) {
      fm.beginPath();
      f.poly.forEach(([x, z], i) => { const px = (x - minX) / res, py = (maxZ - z) / res; if (i) fm.lineTo(px, py); else fm.moveTo(px, py); });
      fm.closePath(); fm.fill();
    }
    const maskedFill = (mask, style, alpha, op) => {
      const t = document.createElement('canvas');
      t.width = w; t.height = h;
      const tc = t.getContext('2d');
      tc.fillStyle = style; tc.fillRect(0, 0, w, h);
      tc.globalCompositeOperation = 'destination-in';
      tc.drawImage(mask, 0, 0);
      ctx.globalAlpha = alpha; ctx.globalCompositeOperation = op;
      ctx.drawImage(t, 0, 0);
      ctx.globalAlpha = 1; ctx.globalCompositeOperation = 'source-over';
    };
    // First cut: a slightly darker collar around the fairways
    ctx.strokeStyle = 'rgba(40, 80, 30, 0.22)'; ctx.lineWidth = 4 / res;
    for (const f of course.features) if (f.surface === 'fairway' && !f.inner) { path(f.poly); ctx.stroke(); }
    // Fairways and tees: richer green with mowing stripes; greens smoother and brighter
    maskedFill(fwMask, '#6cb043', 0.4, 'soft-light');
    fill('tee', '#73b84a', 0.45, 'soft-light');
    fill('green', '#5fae47', 0.45, 'soft-light');
    fill('green', '#64ad4a', 0.3);
    // Bunkers: clean sand
    fill('sand', '#e6d7ad', 0.7);
    // Water beds read dark under the water surface
    fill('water', '#22352c', 0.55);
    // Cart paths: pale pavement with slightly darker edges
    ctx.lineCap = 'round'; ctx.lineJoin = 'round';
    for (const pth of course.paths || []) {
      const line = () => { ctx.beginPath(); pth.pts.forEach(([x, z], i) => { const px = (x - minX) / res, py = (maxZ - z) / res; if (i) ctx.lineTo(px, py); else ctx.moveTo(px, py); }); };
      const paved = /asphalt|concrete|paved/.test(pth.surface) || pth.surface === 'unknown';
      line(); ctx.strokeStyle = paved ? 'rgba(120,118,112,0.75)' : 'rgba(150,130,100,0.7)'; ctx.lineWidth = (pth.width + 0.4) / res; ctx.stroke();
      line(); ctx.strokeStyle = paved ? 'rgba(178,175,166,0.92)' : 'rgba(190,170,135,0.9)'; ctx.lineWidth = pth.width / res; ctx.stroke();
    }
    return canvas;
  }

  // Read surfaces from the aerial photo (1 m grid):
  //   1 = mown fairway grass on holes whose fairway isn't mapped (bright and smooth, along the hole)
  //   2 = tree cover (darkest vegetation), which plays as deep rough
  // Brightness/smoothness thresholds are learned from the mapped fairways and rough of the course.
  detectSurfaces(course, sat, photo, photoRes) {
    const { minX, maxX, minZ, maxZ } = course.bounds;
    const res = 1, w = Math.ceil((maxX - minX) / res), h = Math.ceil((maxZ - minZ) / res);
    const c = document.createElement('canvas');
    c.width = w; c.height = h;
    c.getContext('2d').drawImage(photo, 0, 0, w, h);
    const d = c.getContext('2d').getImageData(0, 0, w, h).data;
    const N = w * h, L = new Float32Array(N);
    for (let i = 0; i < N; i++) L[i] = (d[i * 4] + d[i * 4 + 1] + d[i * 4 + 2]) / 3;
    // Local smoothness: standard deviation of brightness over 7×7 m (integral images)
    const I = new Float64Array((w + 1) * (h + 1)), I2 = new Float64Array((w + 1) * (h + 1));
    for (let j = 0; j < h; j++) {
      let row = 0, row2 = 0;
      for (let i = 0; i < w; i++) {
        const v = L[j * w + i]; row += v; row2 += v * v;
        I[(j + 1) * (w + 1) + i + 1] = I[j * (w + 1) + i + 1] + row;
        I2[(j + 1) * (w + 1) + i + 1] = I2[j * (w + 1) + i + 1] + row2;
      }
    }
    const R = 3;
    const std = (i, j) => {
      const i0 = Math.max(0, i - R), i1 = Math.min(w, i + R + 1), j0 = Math.max(0, j - R), j1 = Math.min(h, j + R + 1);
      const n = (i1 - i0) * (j1 - j0), W = w + 1;
      const s = I[j1 * W + i1] - I[j0 * W + i1] - I[j1 * W + i0] + I[j0 * W + i0];
      const s2 = I2[j1 * W + i1] - I2[j0 * W + i1] - I2[j1 * W + i0] + I2[j0 * W + i0];
      return Math.sqrt(Math.max(0, s2 / n - (s / n) ** 2));
    };
    const at = (x, z) => [Math.floor((x - minX) / res), Math.floor((maxZ - z) / res)];
    const codes = new Uint8Array(N);
    // Learn what fairway vs rough looks like on this course from the mapped areas
    const fwL = [], fwS = [], rL = [], rS = [];
    for (let j = 4; j < h - 4; j += 5) for (let i = 4; i < w - 4; i += 5) {
      const s = course.surfaceAt(minX + (i + 0.5) * res, maxZ - (j + 0.5) * res);
      if (s === 'fairway') { fwL.push(L[j * w + i]); fwS.push(std(i, j)); } else if (s === 'rough') { rL.push(L[j * w + i]); rS.push(std(i, j)); }
    }
    const med = (a) => { const s = [...a].sort((x, y) => x - y); return s[Math.floor(s.length / 2)]; };
    let detected = 0;
    sat.detectStats = { fw: fwL.length, rough: rL.length, fwL: med(fwL), rL: med(rL), fwS: med(fwS), rS: med(rS) };
    // A course whose map already has a fairway for every par 4 and 5 needs no guessing
    const mappedFairways = course.features.filter((f) => f.surface === 'fairway' && !f.inner).length;
    const longHoles = course.holes.filter((hh) => hh.par >= 4).length;
    if (mappedFairways < longHoles && fwL.length > 150 && rL.length > 150 && med(fwL) > med(rL) + 2) {
      // Mown grass is above all smooth; it is also at least as bright as typical rough
      const lThr = med(rL), sThr = med(fwS) < med(rS) ? (med(fwS) + med(rS)) / 2 : med(fwS) * 1.3;
      for (const hole of course.holes) {
        // Skip holes whose fairway is already mapped along most of their line; elsewhere only
        // unmapped rough is considered, so mapped fairways are never overridden
        let n = 0, hit = 0;
        for (let t = 0.3; t < 0.9; t += 0.05) {
          const a = hole.path[0], e = hole.path[hole.path.length - 1];
          n++; if (course.surfaceAt(a.x + (e.x - a.x) * t, a.z + (e.z - a.z) * t) === 'fairway') hit++;
        }
        if (hole.par >= 4 && hit / n > 0.7) continue;
        const reach = hole.par >= 4 ? 60 : 25;
        const xs = hole.path.map((p) => p.x), zs = hole.path.map((p) => p.z);
        const [i0, j1] = at(Math.min(...xs) - reach, Math.min(...zs) - reach), [i1, j0] = at(Math.max(...xs) + reach, Math.max(...zs) + reach);
        for (let j = Math.max(0, j0); j <= Math.min(h - 1, j1); j++) for (let i = Math.max(0, i0); i <= Math.min(w - 1, i1); i++) {
          const k = j * w + i;
          if (L[k] < lThr || d[k * 4 + 1] < d[k * 4] * 1.08 || std(i, j) > sThr) continue;
          const x = minX + (i + 0.5) * res, z = maxZ - (j + 0.5) * res;
          const q = closestOnPath(hole, x, z);
          if (q.d > reach || q.s < hole.length * (hole.par >= 4 ? 0.18 : 0.6) || q.s > hole.length - hole.greenR * 0.5) continue;
          if (course.surfaceAt(x, z) !== 'rough') continue;
          codes[k] = 1;
        }
        detected++;
      }
      // Clean up: keep solid areas only (5×5 majority), then drop small specks
      majority(codes, w, h, 1, 2, 13);
      removeSmall(codes, w, h, 1, 250);
    }
    // Tree cover: clearly darker than the course's rough and strongly textured (crowns and their
    // shadows), so dark-but-smooth links rough and gorse don't turn into forest
    const lums = []; for (let i = 0; i < N; i += 17) lums.push(L[i]);
    lums.sort((a, b) => a - b);
    const st = sat.detectStats;
    const dark = Math.min(lums[Math.floor(lums.length * 0.26)], st?.rough > 150 ? st.rL * 0.93 : 255);
    const rough = st?.rough > 150 ? st.rS : 8;
    for (let j = 0; j < h; j++) for (let i = 0; i < w; i++) {
      const k = j * w + i;
      if (codes[k] || L[k] >= dark) continue;
      const r = d[k * 4], g = d[k * 4 + 1], b = d[k * 4 + 2];
      if (g > r * 1.04 && g > b * 1.08 && std(i, j) > rough * 0.5) codes[k] = 2;
    }
    majority(codes, w, h, 2, 1, 5);
    // Cart paths seen in the photo (only when the map has hardly any): long, thin, pale and grey
    if ((course.paths || []).length < 5) {
      const pale = (st?.fw > 150 ? st.fwL : 110) + 18;
      for (let k = 0; k < N; k++) {
        if (codes[k]) continue;
        const r = d[k * 4], g = d[k * 4 + 1], b = d[k * 4 + 2], mx = Math.max(r, g, b), mn = Math.min(r, g, b);
        if (L[k] > pale && mx - mn < mx * 0.16) codes[k] = 4;
      }
      majority(codes, w, h, 4, 1, 4);
      for (const comp of components(codes, w, h, 4)) {
        let keep = comp.length >= 30 && comp.length <= 8000;
        if (keep) {
          // must be line-like: long axis much longer than it is wide, and mostly on rough
          const pts = comp.map((k) => [k % w, Math.floor(k / w)]);
          const [ax, ay] = principalAxis(pts);
          let mx = 0, my = 0; for (const [x, y] of pts) { mx += x; my += y; } mx /= pts.length; my /= pts.length;
          let a2 = 0, b2 = 0;
          for (const [x, y] of pts) { const u = (x - mx) * ax + (y - my) * ay, v = -(x - mx) * ay + (y - my) * ax; a2 += u * u; b2 += v * v; }
          keep = a2 / Math.max(1e-6, b2) > 40;
          if (keep) {
            const [ci, cj] = pts[Math.floor(pts.length / 2)];
            keep = course.surfaceAt(minX + (ci + 0.5) * res, maxZ - (cj + 0.5) * res) === 'rough';
          }
        }
        if (!keep) for (const k of comp) codes[k] = 0;
      }
    }
    // Heather / native grass: tan, unmown vegetation (not green turf, not grey paths or white sand).
    // Thin features such as cart paths are removed by the wide majority filter.
    for (let k = 0; k < N; k++) {
      if (codes[k]) continue;
      const r = d[k * 4], g = d[k * 4 + 1], b = d[k * 4 + 2];
      if (L[k] > 70 && L[k] < 205 && r >= g * 0.9 && g >= b * 1.05 && r - b > 22) codes[k] = 3;
    }
    majority(codes, w, h, 3, 3, 30);
    removeSmall(codes, w, h, 3, 300);
    sat.overlay = { minX, maxZ, res, w, h, codes };
    course.setOverlay(sat.overlay);
    sat.detectedHoles = detected;
  }

  // RGB weights per surface for the terrain shader: R = fairway/tee, G = green, B = sand; rough = the rest
  buildSurfaceMask(course, getSurface = null) {
    const { minX, maxX, minZ, maxZ } = course.bounds;
    // Half-metre mask on real courses (crisp edges), capped so very large courses stay manageable
    const res = course.real ? Math.max(0.5, Math.max(course.bounds.maxX - course.bounds.minX, course.bounds.maxZ - course.bounds.minZ) / 8192) : 1.5;
    const w = Math.ceil((maxX - minX) / res), h = Math.ceil((maxZ - minZ) / res);
    const c = document.createElement('canvas');
    c.width = w; c.height = h;
    const ctx = c.getContext('2d');
    ctx.fillStyle = '#000'; ctx.fillRect(0, 0, w, h);
    if (course.real) {
      const o = this.imagery?.overlay || null;
      if (o) {
        const img = ctx.getImageData(0, 0, w, h);
        for (let j = 0; j < h; j++) for (let i = 0; i < w; i++) {
          const oi = Math.floor((i * res) / o.res), oj = Math.floor((j * res) / o.res);
          const code = oi < o.w && oj < o.h ? o.codes[oj * o.w + oi] : 0;
          if (code === 1) img.data[(j * w + i) * 4] = 255;
          else if (code === 4) { img.data[(j * w + i) * 4 + 1] = 40; img.data[(j * w + i) * 4 + 2] = 90; }
        }
        ctx.putImageData(img, 0, 0);
      }
      const path = (poly) => {
        ctx.beginPath();
        poly.forEach(([x, z], i) => { const px = (x - minX) / res, py = (maxZ - z) / res; if (i) ctx.lineTo(px, py); else ctx.moveTo(px, py); });
        ctx.closePath();
      };
      const each = (surface, fn) => { for (const f of course.features) if (f.surface === surface && !f.inner) { path(f.poly); fn(); } };
      ctx.lineWidth = 4 / res; ctx.strokeStyle = 'rgb(120,0,0)';
      each('fairway', () => ctx.stroke());                       // first cut: half-way look
      ctx.fillStyle = 'rgb(255,0,0)'; each('fairway', () => ctx.fill()); each('tee', () => ctx.fill());
      ctx.lineWidth = 3 / res; ctx.strokeStyle = 'rgb(255,0,0)';               // collar: fairway-height ring
      each('green', () => ctx.stroke());                          // fringe
      ctx.fillStyle = 'rgb(0,255,0)'; each('green', () => ctx.fill());
      ctx.fillStyle = 'rgb(0,0,255)'; each('sand', () => ctx.fill());
      // Cart paths: blue 90 plus green 40. Needing both keeps the soft edges of bunkers (blue only)
      // and greens (green only) from reading as path.
      ctx.strokeStyle = 'rgb(0,40,90)'; ctx.lineCap = 'round'; ctx.lineJoin = 'round';
      for (const pth of course.paths || []) {
        ctx.lineWidth = pth.width / res;
        ctx.beginPath();
        pth.pts.forEach(([x, z], i) => { const px = (x - minX) / res, py = (maxZ - z) / res; if (i) ctx.lineTo(px, py); else ctx.moveTo(px, py); });
        ctx.stroke();
      }
    } else {
      const img = ctx.getImageData(0, 0, w, h);
      const W = { fairway: [255, 0, 0], tee: [255, 0, 0], firstcut: [120, 0, 0], fringe: [255, 0, 0], green: [0, 255, 0], sand: [0, 0, 255] };
      for (let j = 0; j < h; j++) for (let i = 0; i < w; i++) {
        const v = W[course.surfaceAt(minX + (i + 0.5) * res, maxZ - (j + 0.5) * res)];
        if (v) { const o = (j * w + i) * 4; img.data[o] = v[0]; img.data[o + 1] = v[1]; img.data[o + 2] = v[2]; }
      }
      ctx.putImageData(img, 0, 0);
    }
    // Soft edges between surfaces
    const soft = document.createElement('canvas');
    soft.width = w; soft.height = h;
    const sc = soft.getContext('2d');
    sc.filter = 'blur(0.6px)';
    sc.drawImage(c, 0, 0);
    return soft;
  }

  // Mowing direction for each 8 m cell: along the nearest hole's line (tee -> aim points -> green).
  // Stored as the doubled angle (cos 2a, sin 2a) so stripes facing either way blend smoothly.
  buildMowDirections(course) {
    const { minX, maxX, minZ, maxZ } = course.bounds;
    const res = 8, w = Math.ceil((maxX - minX) / res), h = Math.ceil((maxZ - minZ) / res);
    const c = document.createElement('canvas');
    c.width = w; c.height = h;
    const ctx = c.getContext('2d');
    const img = ctx.createImageData(w, h);
    const segs = [];
    for (const hole of course.holes) for (let i = 1; i < hole.path.length; i++) segs.push([hole.path[i - 1], hole.path[i]]);
    for (let j = 0; j < h; j++) for (let i = 0; i < w; i++) {
      const x = minX + (i + 0.5) * res, z = maxZ - (j + 0.5) * res;
      let best = Infinity, dir = [0, 1];
      for (const [a, b] of segs) {
        const dx = b.x - a.x, dz = b.z - a.z, l2 = dx * dx + dz * dz || 1;
        const t = Math.max(0, Math.min(1, ((x - a.x) * dx + (z - a.z) * dz) / l2));
        const d = Math.hypot(x - a.x - dx * t, z - a.z - dz * t);
        if (d < best) { best = d; dir = [dx, dz]; }
      }
      // three.js world x is mirrored
      const ang = Math.atan2(dir[1], -dir[0]);
      const o = (j * w + i) * 4;
      img.data[o] = (Math.cos(2 * ang) * 0.5 + 0.5) * 255; img.data[o + 1] = (Math.sin(2 * ang) * 0.5 + 0.5) * 255; img.data[o + 3] = 255;
    }
    ctx.putImageData(img, 0, 0);
    // Each fairway, green and tee is mown in one direction: along its own long axis.
    // (The per-cell hole direction above only fills in everything else.)
    const fres = 2, fw = Math.ceil((maxX - minX) / fres), fh = Math.ceil((maxZ - minZ) / fres);
    const out = document.createElement('canvas');
    out.width = fw; out.height = fh;
    const o2 = out.getContext('2d');
    o2.imageSmoothingEnabled = true;
    o2.drawImage(c, 0, 0, fw, fh);
    const color = (dx, dz) => {
      const ang = Math.atan2(dz, -dx); // three.js world x is mirrored
      return `rgb(${Math.round((Math.cos(2 * ang) * 0.5 + 0.5) * 255)},${Math.round((Math.sin(2 * ang) * 0.5 + 0.5) * 255)},0)`;
    };
    // Detected fairways (from the photo): one direction per patch
    const ov = this.imagery?.overlay;
    if (ov) {
      const comps = components(ov.codes, ov.w, ov.h, 1);
      const img2 = o2.getImageData(0, 0, fw, fh);
      for (const comp of comps) {
        const pts = comp.map((k) => [k % ov.w, Math.floor(k / ov.w)]);
        const [ax, ay] = principalAxis(pts);
        const ang = Math.atan2(-ay, -ax); // pixel rows run south; course x mirrored in three.js
        const cr = (Math.cos(2 * ang) * 0.5 + 0.5) * 255, cg = (Math.sin(2 * ang) * 0.5 + 0.5) * 255;
        for (const [i, j] of pts) {
          const fi = Math.floor((i * ov.res) / fres), fj = Math.floor((j * ov.res) / fres), o = (fj * fw + fi) * 4;
          if (fi < fw && fj < fh) { img2.data[o] = cr; img2.data[o + 1] = cg; }
        }
      }
      o2.putImageData(img2, 0, 0);
    }
    // Fairways/tees and greens get separate direction maps. Where a fairway meets a green each
    // keeps its own constant direction right up to (and past) its edge; blending the two angles
    // there would bend the stripes into swirls.
    const layer = (surfaces) => {
      const c2 = document.createElement('canvas');
      c2.width = fw; c2.height = fh;
      const x2 = c2.getContext('2d');
      x2.drawImage(out, 0, 0);
      const shapes = (course.features || []).filter((f) => !f.inner && surfaces.includes(f.surface));
      const trace = (f) => {
        x2.beginPath();
        f.poly.forEach(([x, z], i) => { const px = (x - minX) / fres, py = (maxZ - z) / fres; if (i) x2.lineTo(px, py); else x2.moveTo(px, py); });
        x2.closePath();
      };
      // Spread each shape's direction ~10 m beyond its outline first, then fill the shapes on top
      for (const f of shapes) { const [ax, az] = principalAxis(f.poly); x2.strokeStyle = color(ax, az); x2.lineWidth = 10; x2.lineJoin = 'round'; trace(f); x2.stroke(); }
      for (const f of shapes) { const [ax, az] = principalAxis(f.poly); x2.fillStyle = color(ax, az); trace(f); x2.fill(); }
      return c2;
    };
    return { fairway: layer(['fairway', 'tee']), green: layer(['green']) };
  }

  addSurround(course, sat) {
    // A wide, coarse mesh that continues the terrain outward and carries the low-res imagery
    const all = sat.surround;
    const corners = all.flatMap((c) => [sat.toCourse(c.extent.minLat, c.extent.minLon), sat.toCourse(c.extent.maxLat, c.extent.maxLon), sat.toCourse(c.extent.minLat, c.extent.maxLon), sat.toCourse(c.extent.maxLat, c.extent.minLon)]);
    const x0 = Math.min(...corners.map((p) => p[0])), x1 = Math.max(...corners.map((p) => p[0]));
    const z0 = Math.min(...corners.map((p) => p[1])), z1 = Math.max(...corners.map((p) => p[1]));
    const res = Math.max(2, Math.max(x1 - x0, z1 - z0) / 4096);
    const canvas = document.createElement('canvas');
    canvas.width = Math.ceil((x1 - x0) / res); canvas.height = Math.ceil((z1 - z0) / res);
    const ctx = canvas.getContext('2d');
    ctx.fillStyle = '#4b6b35'; ctx.fillRect(0, 0, canvas.width, canvas.height);
    for (const c of all) this.drawGeoImage(ctx, c, sat, x0, z1, res);
    colorBalance(ctx, canvas.width, canvas.height, sat.balance);
    const tex = new THREE.CanvasTexture(canvas);
    tex.colorSpace = THREE.SRGBColorSpace;
    tex.anisotropy = 8;
    const { minX, maxX, minZ, maxZ } = course.bounds;
    const N = 160;
    const g = new THREE.PlaneGeometry(1, 1, N, N);
    const p = g.attributes.position, uv = g.attributes.uv;
    for (let i = 0; i < p.count; i++) {
      const fu = uv.getX(i), fv = uv.getY(i);
      const x = x0 + fu * (x1 - x0), z = z0 + fv * (z1 - z0);
      const cx = Math.min(maxX - 1, Math.max(minX + 1, x)), cz = Math.min(maxZ - 1, Math.max(minZ + 1, z));
      const inside = cx === x && cz === z;
      const y = (this.course.heightAt(cx, cz) ?? this.hmin) - (inside ? 3 : 0.4);
      p.setXYZ(i, -x, y, z);
    }
    g.computeVertexNormals();
    const mesh = new THREE.Mesh(g, new THREE.MeshStandardMaterial({ map: tex, roughness: 1, side: THREE.DoubleSide, envMapIntensity: 0.3 }));
    mesh.receiveShadow = true;
    this.courseGroup.add(mesh);
  }

  paintCourse(course) {
    const { minX, maxX, minZ, maxZ } = course.bounds;
    const w = Math.ceil((maxX - minX) / TEX_RES), h = Math.ceil((maxZ - minZ) / TEX_RES);
    const canvas = document.createElement('canvas');
    canvas.width = w; canvas.height = h;
    const ctx = canvas.getContext('2d');
    const img = ctx.createImageData(w, h);
    const d = img.data;
    for (let j = 0; j < h; j++) {
      const z = maxZ - (j + 0.5) * TEX_RES;
      for (let i = 0; i < w; i++) {
        const x = minX + (i + 0.5) * TEX_RES;
        let s = course.surfaceAt(x, z) || 'oob';
        // With real terrain the ponds are drawn from the map; the rest of a penalty area is grass
        if (s === 'water' && course.real) s = 'rough';
        const base = SURFACE_RGB[s];
        let k = 1 + 0.06 * noise(x / 6, z / 6) + 0.05 * noise(x / 23, z / 23);
        if (s === 'fairway' || s === 'green' || s === 'tee') {
          const stripe = s === 'green' ? 4 : 9;
          const band = Math.floor((x * 0.6 + z * 0.8) / stripe) & 1;
          k *= band ? 1.06 : 0.95;
        } else if (s === 'rough' || s === 'oob') {
          k *= 1 + 0.08 * noise(x / 60 + 7, z / 60);
        }
        const o = (j * w + i) * 4;
        d[o] = Math.min(255, base[0] * k); d[o + 1] = Math.min(255, base[1] * k); d[o + 2] = Math.min(255, base[2] * k); d[o + 3] = 255;
      }
    }
    ctx.putImageData(img, 0, 0);
    // Soften the hard edges between surfaces
    const soft = document.createElement('canvas');
    soft.width = w; soft.height = h;
    const sctx = soft.getContext('2d');
    sctx.filter = 'blur(0.8px)';
    sctx.drawImage(canvas, 0, 0);
    return soft;
  }

  // ---- water, stakes, tee markers

  // One mirror for the water, moved to the level of the pond nearest the camera each frame
  // (off on the Performance setting, where ponds reflect only the sky). Rebuilt whenever the view
  // changes size: three.js resizes the mirror's picture then, and the water would keep pointing at
  // the discarded one, which makes every frame fail (the picture freezes).
  makeWaterMaterial() {
    const old = this.reflection, oldMat = this.waterMat;
    if (old) { this.scene.remove(old.target); this.reflection = null; }
    if (this.hasWater && this.quality?.name !== 'performance') {
      this.reflection = reflector({ resolutionScale: this.quality?.name === 'high' ? 0.5 : 0.33, bounces: false });
      this.reflection.target.rotateX(-Math.PI / 2);
      if (old) this.reflection.target.position.copy(old.target.position);
      this.scene.add(this.reflection.target);
    }
    this.waterMat = waterMaterial(this.scene.environment, this.reflection);
    for (const m of this.waterMeshes || []) m.material = this.waterMat;
    // Let go of the old ones after the next frame has stopped using them
    if (old || oldMat) setTimeout(() => { oldMat?.dispose(); old?.dispose?.(); }, 500);
  }

  addWater(course) {
    this.hasWater = course.waterBodies.length > 0;
    this.waterMeshes = [];
    this.makeWaterMaterial();
    this.waterCentres = course.waterBodies.map((wb) => {
      let x = 0, z = 0;
      for (const [px, pz] of wb.poly) { x += px; z += pz; }
      return { x: x / wb.poly.length, z: z / wb.poly.length, level: wb.level };
    });
    for (const wb of course.waterBodies) {
      const shape = new THREE.Shape(wb.poly.map(([x, z]) => new THREE.Vector2(-x, -z)));
      const g = new THREE.ShapeGeometry(shape);
      g.rotateX(-Math.PI / 2);
      // World-space UVs so the ripples have a consistent scale
      const p = g.attributes.position, uv = g.attributes.uv;
      for (let i = 0; i < p.count; i++) uv.setXY(i, p.getX(i) / 14, p.getZ(i) / 14);
      const m = new THREE.Mesh(g, this.waterMat);
      m.position.y = wb.level;
      m.receiveShadow = true;
      this.waterMeshes.push(m);
      this.courseGroup.add(m);
    }
  }

  addStakesAndMarkers(course) {
    if (course.oob) {
      const stakes = [];
      const P = course.oob;
      for (let i = 0; i < P.length; i++) {
        const [ax, az] = P[i], [bx, bz] = P[(i + 1) % P.length];
        const n = Math.max(1, Math.floor(Math.hypot(bx - ax, bz - az) / 18));
        for (let k = 0; k < n; k++) stakes.push([ax + ((bx - ax) * k) / n, az + ((bz - az) * k) / n]);
      }
      const sg = new THREE.CylinderGeometry(0.04, 0.04, 1, 6);
      sg.translate(0, 0.5, 0);
      const im = new THREE.InstancedMesh(sg, new THREE.MeshStandardMaterial({ color: 0xffffff }), stakes.length);
      const mtx = new THREE.Matrix4();
      stakes.forEach(([x, z], i) => { mtx.makeTranslation(-x, course.heightAt(x, z) ?? 0, z); im.setMatrixAt(i, mtx); });
      im.castShadow = true;
      this.courseGroup.add(im);
    }
    const markerGeo = new THREE.SphereGeometry(0.08, 14, 10);
    for (const h of course.holes) {
      const next = h.path[1] || h.greenCenter;
      for (const [type, t] of Object.entries(h.tees)) {
        const fx = next.x - t.x, fz = next.z - t.z, fl = Math.hypot(fx, fz) || 1;
        const rx = fz / fl, rz = -fx / fl;
        const mat2 = new THREE.MeshStandardMaterial({ color: TEE_COLORS[type] ?? 0xffffff, roughness: 0.4 });
        for (const s of [-3, 3]) {
          const x = t.x + rx * s, z = t.z + rz * s;
          const m = new THREE.Mesh(markerGeo, mat2);
          m.position.copy(T(x, (course.heightAt(x, z) ?? t.y) + 0.06, z));
          m.castShadow = true;
          this.courseGroup.add(m);
        }
      }
    }
  }

  // ---- buildings (OpenStreetMap footprints, extruded)

  addBuildings(course, buildings) {
    const geos = [];
    const wall = new THREE.Color(), roof = new THREE.Color();
    const walls = [0xe8e1d4, 0xd9cbb6, 0xc9c2b8, 0xefe9df, 0xb9a48a], roofs = [0x4a4a4f, 0x5b4c42, 0x3e4148, 0x6a6058, 0x585a5e];
    buildings.forEach((b, n) => {
      if (b.pts.length < 4) return;
      let ground = Infinity;
      for (const [x, z] of b.pts) { const y = course.heightAt(x, z); if (y !== null) ground = Math.min(ground, y); }
      if (!Number.isFinite(ground)) return;
      const shape = new THREE.Shape(b.pts.map(([x, z]) => new THREE.Vector2(-x, -z)));
      const g = new THREE.ExtrudeGeometry(shape, { depth: b.h, bevelEnabled: false });
      g.rotateX(-Math.PI / 2);
      g.translate(0, ground - 0.5, 0);
      const ng = g.index ? g.toNonIndexed() : g;
      wall.setHex(walls[n % walls.length]); roof.setHex(roofs[(n * 7) % roofs.length]);
      const nrm = ng.attributes.normal, col = new Float32Array(nrm.count * 3);
      for (let i = 0; i < nrm.count; i++) { const c = nrm.getY(i) > 0.5 ? roof : wall; col[i * 3] = c.r; col[i * 3 + 1] = c.g; col[i * 3 + 2] = c.b; }
      ng.setAttribute('color', new THREE.BufferAttribute(col, 3));
      ng.deleteAttribute('uv');
      geos.push(ng);
    });
    if (!geos.length) return;
    const merged = mergeGeometries(geos);
    const mesh = new THREE.Mesh(merged, new THREE.MeshStandardMaterial({ vertexColors: true, roughness: 0.85 }));
    mesh.castShadow = true; mesh.receiveShadow = true;
    this.courseGroup.add(mesh);
  }

  // ---- trees

  // Old-style spots ([x, z, r, r2, isPine], from the photo or the generator) as sized trees
  placeTrees(course, spots) {
    const trees = spots.map(([x, z, r, r2, pine]) => ({
      x, z, h: pine ? 11 + r * 10 : 9 + r * 11, r: pine ? 2.2 + r2 * 1.4 : 2.6 + r2 * 2.8, kind: pine ? 'pine' : null,
    }));
    const photo = this.photoAt();
    assignSpecies(trees.filter((t) => !t.kind), photo);
    this.pendingTrees = trees;
  }

  // Colour of the aerial photo under a point, or null
  photoAt() {
    const S = this.imagery?.sample;
    if (!S) return null;
    return (x, z) => {
      const i = Math.floor((x - S.minX) / S.res), j = Math.floor((S.maxZ - z) / S.res);
      if (i < 0 || j < 0 || i >= S.w || j >= S.h) return null;
      const o = (j * S.w + i) * 4;
      return [S.data[o], S.data[o + 1], S.data[o + 2]];
    };
  }

  // Trees from the canopy height map: one per crown, with its real height and spread
  addTreesFromCanopy(course, canopy) {
    const ok = new Set(['rough', 'deeprough', 'native', 'oob']);
    const trees = treesFromCanopy(canopy, (x, z) => {
      if (course.heightAt(x, z) === null) return false;
      const s = this.fastSurface(x, z);
      return s === null || ok.has(s);
    });
    assignSpecies(trees, this.photoAt());
    this.pendingTrees = trees;
  }

  async plantForest(course, trees, progress = async () => {}) {
    // Very wooded courses: keep the trees nearest the holes
    const max = Math.round(40000 * (this.quality?.trees ?? 1));
    if (trees.length > max) {
      const segs = [];
      for (const h of course.holes) for (let i = 1; i < h.path.length; i++) segs.push([h.path[i - 1], h.path[i]]);
      for (const t of trees) {
        let d = Infinity;
        for (const [a, b] of segs) {
          const dx = b.x - a.x, dz = b.z - a.z, l2 = dx * dx + dz * dz || 1;
          const u = Math.max(0, Math.min(1, ((t.x - a.x) * dx + (t.z - a.z) * dz) / l2));
          d = Math.min(d, Math.hypot(t.x - a.x - dx * u, t.z - a.z - dz * u));
        }
        t.d = d;
      }
      trees.sort((a, b) => a.d - b.d);
      trees.length = max;
    }
    this.forest ||= new Forest();
    this.forest.quality = this.quality?.name || 'balanced';
    // Tree models and their distant pictures are made once per session
    await this.forest.prepare(this.renderer, progress);
    await progress('trees-place', 'Planting trees', 0, 0, `${trees.length.toLocaleString()} trees`);
    this.forest.build(trees, (x, z) => course.heightAt(x, z), (m) => this.applyEnvIntensity(m));
    this.forest.update(this.camera, true);
    this.updateImpostorLight();
    this.courseGroup.add(this.forest.group);
    // Collision shapes for the ball
    const index = new TreeIndex(10);
    for (const t of trees) index.add(t);
    this.treeCount = trees.length;
    this.trees = index;
  }

  // Trees where the aerial photo shows tree canopy (dark, green, textured), outside the playing surfaces
  addTreesFromImagery(course, sat) {
    const S = sat.sample;
    const px = (x, z) => {
      const i = Math.floor((x - S.minX) / S.res), j = Math.floor((S.maxZ - z) / S.res);
      if (i < 0 || j < 0 || i >= S.w || j >= S.h) return null;
      const o = (j * S.w + i) * 4;
      return [S.data[o], S.data[o + 1], S.data[o + 2]];
    };
    // Tree canopy is the darkest vegetation in the photo; the cut-off adapts to each image
    const lums = [];
    for (let i = 0; i < S.data.length; i += 4 * 13) lums.push((S.data[i] + S.data[i + 1] + S.data[i + 2]) / 3);
    lums.sort((a, b) => a - b);
    const dark = lums[Math.floor(lums.length * 0.26)];
    const o = sat.overlay;
    const canopy = o
      ? (x, z) => { const i = Math.floor((x - o.minX) / o.res), j = Math.floor((o.maxZ - z) / o.res); return i >= 0 && j >= 0 && i < o.w && j < o.h && o.codes[j * o.w + i] === 2; }
      : (x, z) => {
        const c = px(x, z);
        if (!c) return false;
        const [r, g, b] = c, lum = (r + g + b) / 3;
        return lum < dark && g > r * 1.04 && g > b * 1.08;
      };
    const water = (x, z) => course.waterBodies.some((w) => pointInPoly(w.poly, x, z));
    const { minX, maxX, minZ, maxZ } = course.bounds;
    let seed = 7;
    const rnd = () => ((seed = (seed * 16807) % 2147483647) / 2147483647);
    const spots = [];
    const STEP = 5.5;
    for (let z = minZ + 3; z < maxZ - 3; z += STEP) {
      for (let x = minX + 3; x < maxX - 3; x += STEP) {
        const tx = x + (rnd() - 0.5) * STEP * 0.8, tz = z + (rnd() - 0.5) * STEP * 0.8;
        let n = 0;
        for (const [dx, dz] of [[0, 0], [2, 0], [-2, 0], [0, 2], [0, -2]]) if (canopy(tx + dx, tz + dz)) n++;
        if (n < 4) continue;
        const s = course.surfaceAt(tx, tz);
        if (s && s !== 'rough' && s !== 'deeprough' && s !== 'oob' && s !== 'water') continue;
        if ((s === 'water' || s === 'oob' || s === null) && water(tx, tz)) continue; // ponds outside the OOB line too
        // Not on the edge of fairways/greens (photo shadows bleed there)
        const s2 = course.surfaceAt(tx + 3, tz), s3 = course.surfaceAt(tx - 3, tz);
        if (['fairway', 'green', 'tee', 'sand', 'fringe'].includes(s2) || ['fairway', 'green', 'tee', 'sand', 'fringe'].includes(s3)) continue;
        spots.push([tx, tz, rnd(), rnd(), rnd() < 0.18]);
      }
    }
    // Keep the instance count reasonable
    const max = Math.round(45000 * (this.quality?.trees ?? 1));
    const keep = spots.length > max ? spots.filter(() => rnd() < max / spots.length) : spots;
    this.placeTrees(course, keep);
  }

  addTrees(course) {
    const { minX, maxX, minZ, maxZ } = course.bounds;
    const segs = [];
    for (const h of course.holes) {
      for (let i = 1; i < h.path.length; i++) segs.push([h.path[i - 1], h.path[i]]);
      for (const t of Object.values(h.tees)) segs.push([t, h.path[1] || h.greenCenter]);
    }
    const greens = course.holes.map((h) => [h.greenCenter.x, h.greenCenter.z, h.greenR + 22]);
    const spots = [];
    let seed = 1;
    const rnd = () => ((seed = (seed * 16807) % 2147483647) / 2147483647);
    const STEP = 8;
    for (let z = minZ + 4; z < maxZ - 4; z += STEP) {
      for (let x = minX + 4; x < maxX - 4; x += STEP) {
        const px = x + (rnd() - 0.5) * STEP, pz = z + (rnd() - 0.5) * STEP;
        const s = course.surfaceAt(px, pz);
        if (s !== 'rough' && s !== 'oob') continue;
        let dmin = Infinity;
        for (const [a, b] of segs) {
          const dx = b.x - a.x, dz = b.z - a.z, l2 = dx * dx + dz * dz;
          const t = Math.max(0, Math.min(1, ((px - a.x) * dx + (pz - a.z) * dz) / (l2 || 1)));
          dmin = Math.min(dmin, Math.hypot(px - a.x - dx * t, pz - a.z - dz * t));
        }
        if (dmin < (course.real ? 22 : 32)) continue;
        if (greens.some(([gx, gz, r]) => Math.hypot(px - gx, pz - gz) < r)) continue;
        // Keep clear of mapped fairways, greens, tees and bunkers
        if (course.real && [0, 1, 2, 3, 4, 5, 6, 7].some((k) => {
          const s2 = course.surfaceAt(px + Math.cos(k * 0.785) * 12, pz + Math.sin(k * 0.785) * 12);
          return s2 !== 'rough' && s2 !== 'oob' && s2 !== null;
        })) continue;
        // Near water keep it open
        if (course.surfaceAt(px + 6, pz) === 'water' || course.surfaceAt(px - 6, pz) === 'water' ||
            course.surfaceAt(px, pz + 6) === 'water' || course.surfaceAt(px, pz - 6) === 'water') continue;
        // Clustered stands, denser further from play
        const density = 0.25 + Math.min(0.6, (dmin - 32) / 60) + 0.35 * noise(px / 70, pz / 70);
        if (rnd() > density) continue;
        spots.push([px, pz, rnd(), rnd(), rnd() < 0.25]);
      }
    }
    this.placeTrees(course, spots);
  }

  // A quick surface lookup (1 m raster) for placing grass: the shader mask plus the photo overlay.
  // Exact enough for decoration, and thousands of times faster than the polygon tests.
  makeSurfaceLookup(course, mask) {
    const { minX, maxX, minZ, maxZ } = course.bounds;
    const w = Math.ceil(maxX - minX), h = Math.ceil(maxZ - minZ);
    const c = document.createElement('canvas'); c.width = w; c.height = h;
    const ctx = c.getContext('2d', { willReadFrequently: true });
    ctx.drawImage(mask, 0, 0, w, h);
    // Water (with a metre of bank) on its own layer, so nothing grows under the surface
    const wc = document.createElement('canvas'); wc.width = w; wc.height = h;
    const wx = wc.getContext('2d', { willReadFrequently: true });
    wx.fillStyle = wx.strokeStyle = '#fff'; wx.lineWidth = 2; wx.lineJoin = 'round';
    for (const wb of course.waterBodies || []) {
      wx.beginPath();
      wb.poly.forEach(([x, z], i) => (i ? wx.lineTo(x - minX, maxZ - z) : wx.moveTo(x - minX, maxZ - z)));
      wx.closePath(); wx.fill(); wx.stroke();
    }
    this.lookup = { data: ctx.getImageData(0, 0, w, h).data, water: wx.getImageData(0, 0, w, h).data, w, h, minX, maxZ };
  }

  // Rough tufts keep a little gap from mown surfaces, whose drawn edges are slightly soft
  nearMown(x, z, r = 1) {
    for (const [dx, dz] of [[r, 0], [-r, 0], [0, r], [0, -r]]) {
      const s = this.fastSurface(x + dx, z + dz);
      if (s === 'fairway' || s === 'firstcut' || s === 'fringe' || s === 'green') return true;
    }
    return false;
  }

  // Decoration (grass tufts, heather) stays off cart paths with a margin either side
  nearPath(x, z, r = 1.2) {
    for (const [dx, dz] of [[0, 0], [r, 0], [-r, 0], [0, r], [0, -r]]) if (this.fastSurface(x + dx, z + dz) === 'path') return true;
    return false;
  }

  fastSurface(x, z) {
    const L = this.lookup;
    if (!L) return this.course.surfaceAt(x, z);
    const i = Math.floor(x - L.minX), j = Math.floor(L.maxZ - z);
    if (i < 0 || j < 0 || i >= L.w || j >= L.h) return null;
    const o = (j * L.w + i) * 4, r = L.data[o], g = L.data[o + 1], b = L.data[o + 2];
    if (L.water[o] > 100) return 'water';
    if (b > 128) return 'sand';
    if (b > 50 && g > 20 && g < 70) return 'path';
    if (g > 200) return 'green';
    if (r > 200) return 'fairway';
    if (g > 60) return 'fringe';
    if (r > 60) return 'firstcut';
    const ov = this.imagery?.overlay;
    if (ov) {
      const oi = Math.floor((x - ov.minX) / ov.res), oj = Math.floor((ov.maxZ - z) / ov.res);
      const code = oi >= 0 && oj >= 0 && oi < ov.w && oj < ov.h ? ov.codes[oj * ov.w + oi] : 0;
      if (code === 2) return 'deeprough';
      if (code === 3) return 'native';
    }
    return 'rough';
  }

  // ---- weather: flag in the wind, rain, cloud cover

  // windFromDeg is the course-space direction the wind blows from (0 = from +z)
  setWeather({ windMps = 0, windFromDeg = 0, rainMm = 0, cloud = 0.2 }) {
    this.weather = { windMps, windFromDeg, rainMm, cloud };
    // Cloudier = hazier sky, weaker sun, softer light, more haze in the distance
    const sky = this.sky;
    sky.turbidity.value = 3.5 + 9 * cloud; sky.rayleigh.value = 1.1 + 1.6 * cloud; sky.mieCoefficient.value = 0.004 + 0.01 * cloud;
    sky.cloudCoverage.value = 0.15 + 0.7 * cloud; sky.cloudDensity.value = 0.3 + 0.5 * cloud;
    this.sun.intensity = 3.0 * (1 - 0.72 * cloud);
    this.hemi.intensity = 0.7 + 0.45 * cloud;
    this.renderer.toneMappingExposure = 0.75 - 0.08 * cloud;
    this.scene.fog.density = 0.00032 * (1 + 1.5 * cloud + Math.min(3, rainMm * 1.2));
    this.terrain && (this.terrain.material.roughness = rainMm > 0.05 ? 0.7 : 0.92); // wet sheen
    this.setRain(rainMm);
    this.updateImpostorLight();
  }

  // Distant tree pictures light themselves; keep them in step with the sun and sky (three.js lights
  // divide by pi for diffuse surfaces, plus a little for the sky reflection)
  updateImpostorLight() {
    impostorLight.sunDir.value.copy(SUN_DIR).normalize();
    impostorLight.sun.value.copy(this.sun.color).multiplyScalar(this.sun.intensity / Math.PI);
    impostorLight.sky.value.copy(this.hemi.color).multiplyScalar(this.hemi.intensity / Math.PI + 0.12);
    impostorLight.ground.value.copy(this.hemi.groundColor).multiplyScalar(this.hemi.intensity / Math.PI + 0.06);
  }

  animateFlag() {
    const cloth = this.flagCloth;
    if (!cloth) return;
    const { windMps, windFromDeg } = this.weather, t = performance.now() / 1000;
    const wf = (windFromDeg * Math.PI) / 180;
    // The flag stays upright on the pole and turns to point downwind
    const lift = Math.min(1, windMps / 6);
    cloth.rotation.set(0, Math.atan2(Math.cos(wf), Math.sin(wf)) + Math.sin(t * 1.3) * 0.06 * lift, 0);
    // Shape the cloth: folded against the pole in calm air, flying out with a travelling
    // ripple in wind (more and faster ripples the stronger it blows)
    const pos = cloth.geometry.attributes.position;
    if (!cloth.userData.base) cloth.userData.base = Float32Array.from(pos.array);
    const base = cloth.userData.base, L = 0.55;
    const freq = 4 + windMps * 1.1, amp = 0.02 + 0.035 * lift;
    for (let i = 0; i < pos.count; i++) {
      const x = base[i * 3], y = base[i * 3 + 1], f = x / L;
      const wave = Math.sin(x * 13 - t * freq) * amp * f;
      // hanging: the cloth bunches up close to the pole with a gentle sway
      const hx = x * 0.12, hz = Math.sin(t * 0.9 + x * 6) * 0.015 * f;
      pos.array[i * 3] = hx + (x - hx) * lift;
      pos.array[i * 3 + 1] = y - (1 - lift) * f * 0.05;
      pos.array[i * 3 + 2] = hz + (wave - hz) * lift;
    }
    pos.needsUpdate = true;
    cloth.geometry.computeVertexNormals();
  }

  setRain(mm) {
    const n = mm > 0.05 ? Math.min(9000, 2500 + mm * 2500) : 0;
    if (!n) { if (this.rain) { this.scene.remove(this.rain.mesh); this.rain = null; } return; }
    if (this.rain && this.rain.n === n) return;
    if (this.rain) this.scene.remove(this.rain.mesh);
    const pos = new Float32Array(n * 6), drops = [];
    for (let i = 0; i < n; i++) drops.push([(Math.random() - 0.5) * 80, Math.random() * 40, (Math.random() - 0.5) * 80]);
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.BufferAttribute(pos, 3));
    const mesh = new THREE.LineSegments(g, new THREE.LineBasicMaterial({ color: 0xb8c6d4, transparent: true, opacity: 0.38, depthWrite: false }));
    mesh.frustumCulled = false;
    this.scene.add(mesh);
    this.rain = { mesh, drops, n };
  }

  updateRain(dt) {
    const r = this.rain;
    if (!r) return;
    const { windMps, windFromDeg } = this.weather, wf = (windFromDeg * Math.PI) / 180;
    // Drops fall at ~9 m/s and drift downwind; they live in a box that travels with the camera
    const vx = Math.sin(wf) * windMps, vz = -Math.cos(wf) * windMps, vy = -9;
    const cam = this.camera.position, arr = r.mesh.geometry.attributes.position.array;
    r.drops.forEach((d, i) => {
      d[0] += vx * dt; d[1] += vy * dt; d[2] += vz * dt;
      if (d[1] < -5) { d[1] += 45; d[0] = (Math.random() - 0.5) * 80; d[2] = (Math.random() - 0.5) * 80; }
      if (d[0] > 40) d[0] -= 80; else if (d[0] < -40) d[0] += 80;
      if (d[2] > 40) d[2] -= 80; else if (d[2] < -40) d[2] += 80;
      const x = cam.x + d[0], y = cam.y + d[1] - 15, z = cam.z + d[2];
      const o = i * 6;
      arr[o] = x; arr[o + 1] = y; arr[o + 2] = z;
      arr[o + 3] = x - vx * 0.06; arr[o + 4] = y - vy * 0.06; arr[o + 5] = z - vz * 0.06;
    });
    r.mesh.geometry.attributes.position.needsUpdate = true;
  }

  // ---- putting grid: lines draped over the green coloured by slope, with dots flowing downhill

  showPuttGrid(ball, pin) {
    this.hidePuttGrid();
    const course = this.course;
    const dx = pin.x - ball.x, dz = pin.z - ball.z, dist = Math.hypot(dx, dz) || 1;
    const fx = dx / dist, fz = dz / dist, rx = fz, rz = -fx;            // along the putt, and across it
    const half = Math.min(16, Math.max(5, dist * 0.6 + 3));
    const cx = (ball.x + pin.x) / 2, cz = (ball.z + pin.z) / 2;
    const spacing = Math.min(1.2, Math.max(0.6, (half * 2) / 18));
    const onGreen = (x, z) => { const s = course.surfaceAt(x, z); return s === 'green' || s === 'fringe'; };
    const slopeAt = (x, z) => { const n = course.normalAt(x, z); return { pct: (Math.hypot(n[0], n[2]) / n[1]) * 100, dx: n[0], dz: n[2] }; };
    // Slope colour: white (flat, under ~1%) → yellow (~2%) → orange → red (3%+)
    const colorFor = (pct) => {
      const t = Math.min(1, pct / 3); // greens are mostly 0-3% slope
      return t < 0.4 ? [1, 1, 1 - t * 2] : t < 0.75 ? [1, 1 - (t - 0.4) * 1.4, 0.2] : [1, 0.5 - (t - 0.75) * 1.6, 0.15];
    };
    const pos = [], col = [], dots = [];
    const steps = Math.ceil(half / spacing);
    for (const [ax, az, bx, bz] of [[fx, fz, rx, rz], [rx, rz, fx, fz]]) {
      // lines running along (ax, az), stepped across (bx, bz)
      for (let k = -steps; k <= steps; k++) {
        const ox = cx + bx * k * spacing, oz = cz + bz * k * spacing;
        let prev = null;
        for (let s = -half; s <= half; s += spacing / 4) {
          const x = ox + ax * s, z = oz + az * s;
          const ok = onGreen(x, z);
          const y = course.heightAt(x, z);
          const cur = ok && y !== null ? [x, y, z] : null;
          if (prev && cur) {
            const c = colorFor(slopeAt(x, z).pct);
            pos.push(-prev[0], prev[1] + 0.012, prev[2], -cur[0], cur[1] + 0.012, cur[2]);
            col.push(...c, ...c);
          }
          prev = cur;
        }
      }
    }
    // A dot at each grid crossing on the green
    for (let i = -steps; i <= steps; i++) for (let j = -steps; j <= steps; j++) {
      const x = cx + fx * i * spacing + rx * j * spacing, z = cz + fz * i * spacing + rz * j * spacing;
      if (!onGreen(x, z)) continue;
      const sl = slopeAt(x, z), l = Math.hypot(sl.dx, sl.dz) || 1;
      dots.push({ x, z, dx: sl.dx / l, dz: sl.dz / l, pct: sl.pct, phase: Math.random() });
    }
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
    g.setAttribute('color', new THREE.Float32BufferAttribute(col, 3));
    const lines = new THREE.LineSegments(g, new THREE.LineBasicMaterial({ vertexColors: true, transparent: true, opacity: 0.55, depthWrite: false }));
    lines.renderOrder = 3;
    const dotPos = new THREE.InstancedBufferAttribute(new Float32Array(Math.max(1, dots.length) * 3), 3);
    dotPos.setUsage(THREE.DynamicDrawUsage);
    const dotMat = new THREE.PointsNodeMaterial({ color: 0xffffff, size: 7, sizeAttenuation: false, map: radialTexture(true), transparent: true, depthWrite: false, alphaTest: 0.05 });
    dotMat.positionNode = instancedBufferAttribute(dotPos);
    const points = new THREE.Sprite(dotMat);
    points.count = dots.length;
    points.frustumCulled = false;
    points.renderOrder = 4;
    this.puttGrid = { group: new THREE.Group(), points, dotPos, dots, spacing };
    this.puttGrid.group.add(lines, points);
    this.dynamic.add(this.puttGrid.group);
    this.updatePuttGrid(0);
  }

  hidePuttGrid() {
    if (!this.puttGrid) return;
    this.dynamic.remove(this.puttGrid.group);
    this.puttGrid.group.traverse((o) => { o.geometry?.dispose(); o.material?.dispose(); });
    this.puttGrid = null;
  }

  updatePuttGrid(dt) {
    const pg = this.puttGrid;
    if (!pg) return;
    const arr = pg.dotPos.array;
    pg.dots.forEach((d, i) => {
      // Faster on steeper ground; each dot slides downhill across one grid cell, then repeats
      d.phase = (d.phase + dt * (0.15 + d.pct * 0.3)) % 1;
      const travel = d.pct < 0.3 ? 0 : d.phase * pg.spacing;
      const x = d.x + d.dx * travel, z = d.z + d.dz * travel;
      const y = this.course.heightAt(x, z) ?? 0;
      arr[i * 3] = -x; arr[i * 3 + 1] = y + 0.02; arr[i * 3 + 2] = z;
    });
    pg.dotPos.needsUpdate = true;
  }

  // ---- heather / native grass: tall wispy clumps over the areas detected in the photo

  addNativeGrass(course, sat) {
    const o = sat.overlay, S = sat.sample;
    if (!o) return;
    const cells = [];
    for (let k = 0; k < o.codes.length; k += 1) if (o.codes[k] === 3) cells.push(k);
    if (!cells.length) return;
    // Roughly one clump per 2 m², capped so huge native areas stay fast
    const want = Math.min(140000, Math.round(cells.length / 1.6)) * (this.quality?.native ?? 1) | 0;
    const blades = [];
    for (let i = 0; i < 11; i++) {
      const a = (i / 11) * Math.PI * 2 + Math.random() * 0.6, lean = 0.15 + Math.random() * 0.35, hgt = 0.6 + Math.random() * 0.5;
      const bx = Math.cos(a) * 0.05, bz = Math.sin(a) * 0.05, wdt = 0.012 + Math.random() * 0.01;
      const g = new THREE.BufferGeometry();
      g.setAttribute('position', new THREE.Float32BufferAttribute([
        bx - wdt, 0, bz, bx + wdt, 0, bz, bx + Math.cos(a) * lean * 0.45, hgt, bz + Math.sin(a) * lean * 0.45,
      ], 3));
      // Darker at the base, sun-bleached at the tips
      g.setAttribute('color', new THREE.Float32BufferAttribute([0.45, 0.47, 0.36, 0.45, 0.47, 0.36, 1.18, 1.12, 0.92], 3));
      blades.push(g);
    }
    const geo = mergeGeometries(blades);
    geo.computeVertexNormals();
    const mat = new THREE.MeshStandardMaterial({ vertexColors: true, side: THREE.DoubleSide, roughness: 1, envMapIntensity: 0.5 });
    const mesh = new THREE.InstancedMesh(geo, mat, want);
    mesh.receiveShadow = true;
    const m = new THREE.Matrix4(), q = new THREE.Quaternion(), sc = new THREE.Vector3(), p = new THREE.Vector3(), c = new THREE.Color();
    const up = new THREE.Vector3(0, 1, 0);
    let n = 0;
    for (let tries = 0; tries < want * 3 && n < want; tries++) {
      const k = cells[Math.floor(Math.random() * cells.length)];
      const x = o.minX + ((k % o.w) + Math.random()) * o.res, z = o.maxZ - (Math.floor(k / o.w) + Math.random()) * o.res;
      if (course.surfaceAt(x, z) !== 'native' || this.fastSurface(x, z) === 'water' || this.nearPath(x, z, 1.5)) continue;
      const y = course.heightAt(x, z);
      if (y === null) continue;
      q.setFromAxisAngle(up, Math.random() * 6.28);
      const hgt = 0.35 + Math.random() * 0.4;
      m.compose(p.copy(T(x, y - 0.03, z)), q, sc.set(1.4 + Math.random() * 1.4, hgt, 1.4 + Math.random() * 1.4));
      mesh.setMatrixAt(n, m);
      // Colour from the photo underneath (tan, golden or brownish heather)
      let col = [0.62, 0.55, 0.36];
      if (S) {
        const i = Math.floor((x - S.minX) / S.res), j = Math.floor((S.maxZ - z) / S.res), oo = (j * S.w + i) * 4;
        if (oo >= 0 && oo < S.data.length) col = [S.data[oo] / 255, S.data[oo + 1] / 255, S.data[oo + 2] / 255];
      }
      mesh.setColorAt(n, c.setRGB(col[0] * 1.05, col[1] * 1.02, col[2] * 0.9, THREE.SRGBColorSpace));
      n++;
    }
    mesh.count = n;
    mesh.instanceMatrix.needsUpdate = true;
    if (mesh.instanceColor) mesh.instanceColor.needsUpdate = true;
    this.courseGroup.add(mesh);
    this.nativeClumps = n;
  }

  // ---- grass tufts around the ball (close-up detail)

  placeGrass(center, minMove = 8) {
    if (!this.course) return;
    if (this.grassCenter && Math.hypot(center.x - this.grassCenter.x, center.z - this.grassCenter.z) < minMove) return;
    this.grassCenter = { x: center.x, z: center.z };
    if (!this.grass) {
      const blades = [];
      for (let i = 0; i < 7; i++) {
        const a = (i / 7) * Math.PI * 2 + Math.random() * 0.5, lean = 0.25 + Math.random() * 0.35, hgt = 0.7 + Math.random() * 0.5;
        const g = new THREE.BufferGeometry();
        const bx = Math.cos(a) * 0.03, bz = Math.sin(a) * 0.03;
        g.setAttribute('position', new THREE.Float32BufferAttribute([
          bx - 0.012, 0, bz, bx + 0.012, 0, bz, bx + Math.cos(a) * lean * 0.3, hgt, bz + Math.sin(a) * lean * 0.3,
        ], 3));
        g.setAttribute('color', new THREE.Float32BufferAttribute([0.55, 0.6, 0.5, 0.55, 0.6, 0.5, 1.1, 1.15, 0.95], 3));
        blades.push(g);
      }
      const geo = mergeGeometries(blades);
      geo.computeVertexNormals();
      const mat = new THREE.MeshStandardMaterial({ vertexColors: true, side: THREE.DoubleSide, roughness: 1, envMapIntensity: 0.6 });
      this.applyEnvIntensity(mat);
      this.grass = new THREE.InstancedMesh(geo, mat, this.quality?.grass ?? 20000);
      this.grass.receiveShadow = true;
      this.grass.frustumCulled = false;
      this.dynamic.add(this.grass);
    }
    const m = new THREE.Matrix4(), q = new THREE.Quaternion(), sc = new THREE.Vector3(), p = new THREE.Vector3(), c = new THREE.Color();
    const up = new THREE.Vector3(0, 1, 0);
    let n = 0;
    const S = this.imagery?.sample;
    const maxGrass = this.grass.instanceMatrix.count;
    for (let k = 0; k < maxGrass * 2.5 && n < maxGrass; k++) {
      const r = Math.sqrt(Math.random()) * 42, a = Math.random() * Math.PI * 2;
      const x = center.x + Math.cos(a) * r, z = center.z + Math.sin(a) * r;
      const s = this.fastSurface(x, z);
      // Tufts only in the rough, never on mown grass (fairway, first cut, fringe, green, tee)
      if (s !== 'rough' && s !== 'deeprough' && s !== 'native' && s !== 'oob') continue;
      if (this.nearPath(x, z) || this.nearMown(x, z)) continue;
      const y = this.course.heightAt(x, z);
      if (y === null) continue;
      // Rough stands clearly taller than the cut grass around the fairways and greens
      const height = (s === 'deeprough' || s === 'native' ? 0.13 : 0.09) + Math.random() * 0.08;
      q.setFromAxisAngle(up, Math.random() * 6.28);
      m.compose(p.copy(T(x, y, z)), q, sc.set(0.6 + Math.random() * 0.4, height, 0.6 + Math.random() * 0.4));
      this.grass.setMatrixAt(n, m);
      // Match the ground colour underneath
      let col = [0.27, 0.42, 0.18];
      if (S) {
        const i = Math.floor((x - S.minX) / S.res), j = Math.floor((S.maxZ - z) / S.res), o = (j * S.w + i) * 4;
        if (o >= 0 && o < S.data.length) col = [S.data[o] / 255, S.data[o + 1] / 255, S.data[o + 2] / 255];
      }
      // Rough reads as deeper green; blend the photo colour with it
      const rough = s === 'rough' || s === 'deeprough' || s === 'oob';
      const tgt = rough ? [0.24, 0.42, 0.14] : [0.4, 0.62, 0.24];
      const v = 0.85 + Math.random() * 0.3;
      this.grass.setColorAt(n, c.setRGB((col[0] * 0.4 + tgt[0] * 0.6) * v, (col[1] * 0.4 + tgt[1] * 0.6) * v, (col[2] * 0.4 + tgt[2] * 0.6) * v, THREE.SRGBColorSpace));
      n++;
    }
    this.grass.count = n;
    this.grass.instanceMatrix.needsUpdate = true;
    if (this.grass.instanceColor) this.grass.instanceColor.needsUpdate = true;
  }

  // ------------------------------------------------------------------ flag, aim marker

  makeFlag() {
    const g = new THREE.Group();
    const pole = new THREE.Mesh(new THREE.CylinderGeometry(0.012, 0.012, 2.3, 8), new THREE.MeshStandardMaterial({ color: 0xf2f2f2 }));
    pole.position.y = 1.15;
    pole.castShadow = true;
    const flagGeo = new THREE.PlaneGeometry(0.55, 0.38, 12, 2);
    flagGeo.translate(0.275, 0, 0);
    const flag = new THREE.Mesh(flagGeo, new THREE.MeshStandardMaterial({ color: 0xe8282b, side: THREE.DoubleSide }));
    flag.position.y = 2.1;
    flag.castShadow = true;
    this.flagCloth = flag;
    const cup = new THREE.Mesh(new THREE.CircleGeometry(0.054, 24), new THREE.MeshBasicMaterial({ color: 0x111111 }));
    cup.rotation.x = -Math.PI / 2;
    cup.position.y = 0.01;
    // Hole locator for long range: a map-style pin whose tip sits right above the flagstick
    // (hidden up close, where the real flag is easy to see)
    const c = document.createElement('canvas'); c.width = 64; c.height = 96;
    const x = c.getContext('2d');
    x.shadowColor = 'rgba(0,0,0,0.45)'; x.shadowBlur = 6; x.shadowOffsetY = 2;
    x.fillStyle = '#ffffff'; x.strokeStyle = '#1b1b1b'; x.lineWidth = 3;
    x.beginPath(); x.arc(32, 30, 22, Math.PI * 0.82, Math.PI * 0.18); x.lineTo(32, 90); x.closePath(); x.fill();
    x.shadowColor = 'transparent'; x.stroke();
    x.fillStyle = '#e8282b'; x.beginPath(); x.arc(32, 30, 9, 0, Math.PI * 2); x.fill();
    const markerTex = new THREE.CanvasTexture(c);
    markerTex.colorSpace = THREE.SRGBColorSpace;
    const marker = new THREE.Sprite(new THREE.SpriteMaterial({ map: markerTex, sizeAttenuation: false, depthTest: false, transparent: true }));
    marker.center.set(0.5, 0);           // anchor the tip, so it points straight down at the hole
    marker.scale.set(0.028, 0.042, 1);
    marker.position.y = 2.6;
    marker.renderOrder = 6;
    this.pinMarker = marker;
    g.add(pole, flag, cup, marker);
    return g;
  }

  makeAimMarker() {
    const g = new THREE.Group();
    const ring = new THREE.Mesh(new THREE.RingGeometry(0.8, 1, 48), new THREE.MeshBasicMaterial({ color: 0xffe14d, transparent: true, opacity: 0.9, depthTest: false }));
    ring.rotation.x = -Math.PI / 2;
    ring.renderOrder = 5;
    const dot = new THREE.Mesh(new THREE.CircleGeometry(0.18, 24), new THREE.MeshBasicMaterial({ color: 0xffe14d, depthTest: false }));
    dot.rotation.x = -Math.PI / 2;
    dot.renderOrder = 5;
    this.aimLabelCanvas = document.createElement('canvas');
    this.aimLabelCanvas.width = 512; this.aimLabelCanvas.height = 96;
    this.aimLabelTex = new THREE.CanvasTexture(this.aimLabelCanvas);
    const label = new THREE.Sprite(new THREE.SpriteMaterial({ map: this.aimLabelTex, sizeAttenuation: false, depthTest: false, transparent: true }));
    label.scale.set(0.3, 0.056, 1);
    label.position.y = 2;
    label.renderOrder = 6;
    g.add(ring, dot, label);
    g.visible = false;
    this.aimRing = g;
    return g;
  }

  setAimTarget(p, text, ring = true) {
    if (!p) { this.aimMarker.visible = false; return; }
    const y = this.course.heightAt(p.x, p.z) ?? p.y;
    this.aimMarker.position.copy(T(p.x, y + 0.08, p.z));
    this.aimMarker.visible = true;
    this.aimMarker.children[0].visible = this.aimMarker.children[1].visible = ring;
    const d = this.camera.position.distanceTo(this.aimMarker.position);
    this.aimMarker.children[0].scale.setScalar(Math.max(1.2, d * 0.03));
    this.aimMarker.children[1].scale.setScalar(Math.max(1.2, d * 0.03));
    const ctx = this.aimLabelCanvas.getContext('2d');
    ctx.clearRect(0, 0, 512, 96);
    ctx.fillStyle = 'rgba(10,16,12,0.78)';
    ctx.font = 'bold 34px system-ui, sans-serif';
    const tw = Math.min(480, ctx.measureText(text).width + 44);
    ctx.beginPath(); ctx.roundRect(256 - tw / 2, 18, tw, 60, 14); ctx.fill();
    ctx.fillStyle = '#ffe14d'; ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
    ctx.fillText(text, 256, 49);
    this.aimLabelTex.needsUpdate = true;
  }

  // ------------------------------------------------------------------ dynamic objects

  setPin(pin) {
    this.pin = pin;
    this.flag.position.copy(T(pin.x, this.course.heightAt(pin.x, pin.z) ?? pin.y, pin.z));
  }

  setBall(p) {
    // p is the ball's contact point; its centre is set each frame from the drawn size
    this.ballBaseY = p.y;
    this.ball.position.copy(T(p.x, p.y + 0.12 * this.ball.scale.x, p.z));
    const g = this.course?.heightAt(p.x, p.z);
    this.ballShadow.visible = g != null;
    if (g != null) {
      this.ballShadow.position.copy(T(p.x, g + 0.015, p.z));
      const above = Math.max(0, p.y - g);
      this.ballShadow.material.opacity = 0.55 / (1 + above * 0.4);
    }
  }

  setAim(origin, heading, length, onGreen) {
    if (this.aimLine) { this.dynamic.remove(this.aimLine); this.aimLine.geometry.dispose(); }
    const pts = [];
    const fx = Math.sin(heading), fz = Math.cos(heading);
    const step = onGreen ? 0.25 : 4;
    for (let s = onGreen ? 0.3 : 2; s <= length; s += step) {
      const x = origin.x + fx * s, z = origin.z + fz * s;
      const y = this.course.heightAt(x, z);
      if (y === null) break;
      pts.push(T(x, y + (onGreen ? 0.03 : 0.25), z));
    }
    if (pts.length < 2) return;
    const g = new THREE.BufferGeometry().setFromPoints(pts);
    this.aimLine = new THREE.Line(g, new THREE.LineDashedMaterial({ color: 0xffffff, dashSize: onGreen ? 0.2 : 3, gapSize: onGreen ? 0.15 : 2, transparent: true, opacity: 0.8 }));
    this.aimLine.computeLineDistances();
    this.dynamic.add(this.aimLine);
  }

  hideAim() {
    if (this.aimLine) this.aimLine.visible = false;
    this.aimMarker.visible = false;
  }

  setTrail(points, n) {
    if (this.trail) { this.dynamic.remove(this.trail); this.trail.geometry.dispose(); this.trail = null; }
    if (!points || n < 2) return;
    const arr = [];
    for (let i = 0; i < n; i++) { const p = points[i]; arr.push(-p.x, p.y + 0.1, p.z); }
    const g = new LineGeometry();
    g.setPositions(arr);
    this.trail = new Line2(g, this.trailMat);
    this.trail.computeLineDistances();
    this.dynamic.add(this.trail);
  }

  // Keep the previous shot's line faintly visible
  // Auto-putt: a circle on the green around the hole (draped over the slope); null hides it
  setPuttCircle(pin, radius) {
    if (this.puttCircle) { this.dynamic.remove(this.puttCircle); this.puttCircle.geometry.dispose(); this.puttCircle = null; }
    if (!pin) return;
    const pts = [];
    for (let i = 0; i <= 96; i++) {
      const a = (i / 96) * Math.PI * 2, x = pin.x + Math.cos(a) * radius, z = pin.z + Math.sin(a) * radius;
      pts.push(-x, (this.course.heightAt(x, z) ?? pin.y) + 0.03, z);
    }
    const g = new LineGeometry();
    g.setPositions(pts);
    this.puttCircle = new Line2(g, new THREE.Line2NodeMaterial({ color: 0xe6e6e6, linewidth: 3 }));
    this.puttCircle.computeLineDistances();
    this.dynamic.add(this.puttCircle);
  }

  // Previous shots' traces can be hidden (H)
  setGhostsVisible(on) {
    this.ghostsVisible = on;
    for (const g of this.ghostTrails) g.visible = on;
  }

  keepTrailAsGhost() {
    if (!this.trail) return;
    const ghost = new Line2(this.trail.geometry, new THREE.Line2NodeMaterial({ color: 0x8a8f94, linewidth: 2 }));
    ghost.visible = this.ghostsVisible !== false;
    this.ghostTrails.push(ghost);
    this.dynamic.add(ghost);
    this.dynamic.remove(this.trail);
    this.trail = null;
  }

  clearTrails() {
    this.setTrail(null, 0);
    for (const g of this.ghostTrails) { this.dynamic.remove(g); g.geometry.dispose(); }
    this.ghostTrails = [];
  }

  // Course-space point under a screen position (on the terrain), or null
  pick(clientX, clientY) {
    if (!this.terrain) return null;
    const r = this.renderer.domElement.getBoundingClientRect();
    const ndc = new THREE.Vector2(((clientX - r.left) / r.width) * 2 - 1, -((clientY - r.top) / r.height) * 2 + 1);
    this.raycaster.setFromCamera(ndc, this.camera);
    const hit = this.raycaster.intersectObject(this.terrain, false)[0];
    return hit ? { x: -hit.point.x, y: hit.point.y, z: hit.point.z } : null;
  }

  // ------------------------------------------------------------------ camera

  // Course-space positions in, camera goals out.
  viewAddress(ball, heading, onGreen, pin) {
    this.setFree(false);
    this.mode = 'address';
    const fx = Math.sin(heading), fz = Math.cos(heading);
    const back = onGreen ? 2.6 : 6, up = onGreen ? 1.3 : 2.1;
    const y = (this.course.heightAt(ball.x - fx * back, ball.z - fz * back) ?? ball.y);
    this.camGoalPos.copy(T(ball.x - fx * back, Math.max(y, ball.y) + up, ball.z - fz * back));
    if (onGreen && pin) {
      this.camGoalLook.copy(T(pin.x, pin.y, pin.z));
    } else {
      const ahead = 50;
      const ly = this.course.heightAt(ball.x + fx * ahead, ball.z + fz * ahead) ?? ball.y;
      this.camGoalLook.copy(T(ball.x + fx * ahead, ly - 1, ball.z + fz * ahead));
    }
  }

  viewOverview(ball, pin) {
    this.setFree(false);
    this.mode = 'overview';
    const dx = pin.x - ball.x, dz = pin.z - ball.z, d = Math.hypot(dx, dz) || 1;
    const mx = (ball.x + pin.x) / 2, mz = (ball.z + pin.z) / 2;
    const h = Math.max(40, d * 0.75);
    this.camGoalPos.copy(T(mx - (dx / d) * d * 0.55, ball.y + h, mz - (dz / d) * d * 0.55));
    this.camGoalLook.copy(T(mx + (dx / d) * d * 0.1, pin.y, mz + (dz / d) * d * 0.1));
  }

  // Follow a shot. landing/rest are course-space points (known before the ball is shown flying).
  followBall(landing = null, rest = null, pin = null) {
    this.setFree(false);
    this.mode = 'follow';
    this._flight = landing ? {
      start: this.ball.position.clone(),
      landing: T(landing.x, landing.y, landing.z),
      rest: rest ? T(rest.x, rest.y, rest.z) : null,
      pin: pin ? T(pin.x, pin.y, pin.z) : null,
    } : null;
  }

  snapCamera() {
    this.camPos.copy(this.camGoalPos);
    this.camLook.copy(this.camGoalLook);
  }

  // Free view: roam anywhere with the mouse / WASD
  setFree(on) {
    if (on === (this.mode === 'free')) return;
    if (on) {
      this.prevMode = this.mode;
      this.mode = 'free';
      this.controls.target.copy(this.camLook);
      this.camera.position.copy(this.camPos);
      this.controls.enabled = true;
      this.controls.update();
    } else {
      this.controls.enabled = false;
      this.camPos.copy(this.camera.position);
      this.camLook.copy(this.controls.target);
      this.mode = this.prevMode || 'address';
    }
  }

  // Free view: jump to look at a course-space point
  flyTo(x, z) {
    if (this.mode !== 'free') return;
    const y = this.course.heightAt(x, z) ?? 0;
    const target = T(x, y, z);
    const offset = this.camera.position.clone().sub(this.controls.target);
    if (offset.length() > 400) offset.setLength(400);
    this.controls.target.copy(target);
    this.camera.position.copy(target).add(offset);
  }

  // Camera position/heading in course space (for the minimap)
  cameraInfo() {
    const p = this.camera.position, t = this.mode === 'free' ? this.controls.target : this.camLook;
    return { x: -p.x, z: p.z, heading: Math.atan2(-(t.x - p.x), t.z - p.z) };
  }

  frame() {
    const raw = this.clock.getDelta();
    const dt = Math.min(0.05, raw);
    // Game time follows the wall clock even if frames are slow, so shots play in real time.
    if (this.onFrame) this.onFrame(Math.min(raw, 0.5));
    if (this.mode === 'free') {
      // WASD / arrows move across the ground, Q/E down/up; Shift is faster
      if (this.freeKeys.size) {
        const fwd = new THREE.Vector3(); this.camera.getWorldDirection(fwd); fwd.y = 0; fwd.normalize();
        const right = new THREE.Vector3().crossVectors(fwd, new THREE.Vector3(0, 1, 0));
        const dist = this.camera.position.distanceTo(this.controls.target);
        const speed = (this.freeKeys.has('shift') ? 3 : 1) * Math.max(15, dist * 0.8) * dt;
        const mv = new THREE.Vector3();
        if (this.freeKeys.has('w')) mv.add(fwd);
        if (this.freeKeys.has('s')) mv.sub(fwd);
        if (this.freeKeys.has('d')) mv.add(right);
        if (this.freeKeys.has('a')) mv.sub(right);
        mv.multiplyScalar(speed);
        if (this.freeKeys.has('e')) mv.y += speed * 0.6;
        if (this.freeKeys.has('q')) mv.y -= speed * 0.6;
        this.camera.position.add(mv);
        this.controls.target.add(mv);
      }
      this.controls.update();
      // Close-up grass follows the free camera (re-seeded every 25 m of travel)
      if (this.camera.position.distanceTo(this.controls.target) < 80) this.placeGrass({ x: -this.controls.target.x, z: this.controls.target.z }, 25);
      // Stay above the ground
      if (this.course) {
        const g = this.course.heightAt(-this.camera.position.x, this.camera.position.z);
        const floor = (g ?? this.hmin ?? 0) + 1.5;
        if (this.camera.position.y < floor) this.camera.position.y = floor;
        const tg = this.course.heightAt(-this.controls.target.x, this.controls.target.z);
        if (tg != null && Math.abs(this.controls.target.y - tg) > 0.5) this.controls.target.y += (tg - this.controls.target.y) * 0.2;
      }
    } else {
      if (this.mode === 'follow') {
        // Chase from behind and above the ball, looking down towards where it will land, so the
        // ball and the ground ahead stay in view (rather than the sky behind a high ball)
        const b = this.ball.position;
        const prev = this._prevBall || b.clone();
        const f = this._flight;
        // Once the ball is down and rolling, line up behind it facing the flag (instead of turning
        // with every sideways kick of the roll)
        const landedDist = f ? f.landing.clone().sub(f.start).setY(0).length() : 0;
        const rolling = f && b.clone().sub(f.start).setY(0).length() > landedDist * 0.97;
        let dir;
        if (rolling && f.pin && f.pin.clone().sub(b).setY(0).length() > 3) dir = f.pin.clone().sub(b).setY(0);
        else if (f) dir = f.landing.clone().sub(f.start).setY(0);
        else { dir = b.clone().sub(prev).setY(0); }
        if (dir.lengthSq() > 1e-6) this._followDir = dir.normalize();
        dir = this._followDir || new THREE.Vector3(0, 0, 1);
        const ballGround = this.course ? (this.course.heightAt(-b.x, b.z) ?? b.y) : b.y;
        const height = Math.max(0, b.y - ballGround);
        const back = 26 + height * 0.4;
        this.camGoalPos.copy(b).addScaledVector(dir, -back);
        const groundY = this.course ? (this.course.heightAt(-this.camGoalPos.x, this.camGoalPos.z) ?? ballGround) : ballGround;
        this.camGoalPos.y = Math.max(groundY + 3, ballGround + 5 + height * 1.15);
        // Look at a point between the ball and its landing spot (just the ball once it's down)
        if (f && height > 1) {
          const toLand = f.landing.clone().sub(b).setY(0).length();
          const w = Math.min(0.55, toLand / 400);
          this.camGoalLook.copy(b).lerp(f.landing, w);
        } else if (rolling && this._followDir) {
          // look a little past the ball towards the flag
          this.camGoalLook.copy(b).addScaledVector(this._followDir, 15);
          this.camGoalLook.y = b.y;
        } else this.camGoalLook.copy(b);
        this._prevBall = b.clone();
      } else {
        this._prevBall = null;
      }
      const k = 1 - Math.exp(-dt * (this.mode === 'follow' ? 2.5 : 4));
      this.camPos.lerp(this.camGoalPos, k);
      this.camLook.lerp(this.camGoalLook, this.mode === 'follow' ? 1 - Math.exp(-dt * 8) : k);
      this.camera.position.copy(this.camPos);
      this.camera.lookAt(this.camLook);
    }
    // Shadows follow what we're looking at
    const focus = this.mode === 'free' ? this.controls.target : this.camLook;
    const toFocus = focus.clone().sub(this.camera.position).setY(0);
    const center = this.camera.position.clone().add(toFocus.clampLength(0, 160));
    this.sun.target.position.copy(center);
    this.sun.position.copy(center).addScaledVector(SUN_DIR, 700);
    // Draw the ball larger the further away it is, but true size up close
    const camDist = this.camera.position.distanceTo(this.ball.position);
    const ballScale = Math.min(5, Math.max(0.0213, camDist * 0.004)) / 0.12;
    this.ball.scale.setScalar(ballScale);
    if (this.ballBaseY != null) this.ball.position.y = this.ballBaseY + 0.12 * ballScale;
    this.ballShadow.scale.setScalar(ballScale);
    // Keep the water mirror at the level of the nearest pond
    if (this.reflection && this.waterCentres?.length) {
      const cx = -this.camera.position.x, cz = this.camera.position.z;
      let best = this.waterCentres[0], bd = Infinity;
      for (const w of this.waterCentres) { const d = (w.x - cx) ** 2 + (w.z - cz) ** 2; if (d < bd) { bd = d; best = w; } }
      this.reflection.target.position.set(-best.x, best.level, best.z);
    }
    // The hole locator only matters from a distance
    this.pinMarker.visible = this.camera.position.distanceTo(this.flag.position) > 70;
    if (this.aimMarker.visible) {
      const d = camDist && this.camera.position.distanceTo(this.aimMarker.position);
      this.aimMarker.children[0].scale.setScalar(Math.max(1.2, d * 0.03));
      this.aimMarker.children[1].scale.setScalar(Math.max(1.2, d * 0.03));
    }
    // Flag flutter and water ripples
    // Trees: detailed near the camera, light further away; see-through between camera and ball
    this.forest?.update(this.camera);
    fade.focus.value.copy(this.ball.position);
    fade.look.value.copy(this.camLook);
    fade.on.value = this.mode === 'address' || this.mode === 'follow' ? 1 : 0;
    this.animateFlag();
    this.updateRain(dt);
    this.updatePuttGrid(dt);
    this.renderer.render(this.scene, this.camera);
    // Frame stats for the FPS overlay: average FPS and the slowest frame in each half second
    const st = this.stats, now = performance.now();
    st.frames++; st.worst = Math.max(st.worst, raw * 1000);
    if (now - st.last >= 500) {
      st.fps = (st.frames * 1000) / (now - st.last);
      const info = this.renderer.info.render;
      st.onUpdate?.({ fps: st.fps, worstMs: st.worst, calls: info.drawCalls, triangles: info.triangles, backend: this.backend });
      st.frames = 0; st.worst = 0; st.last = now;
    }
  }
}

// ------------------------------------------------------------------ helpers

// Cart path in the surface mask: blue ≈ 0.35 together with green ≈ 0.16 (see buildSurfaceMask)
const pathWeight = (sm, ws) => smoothstep(0.2, 0.28, sm.b).mul(smoothstep(0.42, 0.5, sm.b).oneMinus())
  .mul(smoothstep(0.07, 0.12, sm.g)).mul(smoothstep(0.24, 0.3, sm.g).oneMinus()).mul(ws.oneMinus());

// Close-up ground textures (CC0, ambientCG: Grass005, Grass004, Ground080, Asphalt031) and the
// three.js water normal map (MIT). Average colours (linear) let the shader use them as detail.
const GROUND_FILES = { lawn: 'lawn', rough: 'rough', sand: 'sand', asphalt: 'asphalt' };
const GROUND_MEAN = { lawn: [0.131, 0.23, 0.025], rough: [0.118, 0.152, 0.03], sand: [0.462, 0.342, 0.165], asphalt: [0.208, 0.208, 0.208] };
const loadedTextures = new Map(), texturePromises = [];
function loadTexture(file, srgb) {
  if (!loadedTextures.has(file)) {
    let done;
    texturePromises.push(new Promise((r) => { done = r; }));
    const t = new THREE.TextureLoader().load(`/textures/${file}`, done, undefined, done);
    t.wrapS = t.wrapT = THREE.RepeatWrapping;
    t.colorSpace = srgb ? THREE.SRGBColorSpace : THREE.NoColorSpace;
    t.anisotropy = 8;
    loadedTextures.set(file, t);
  }
  return loadedTextures.get(file);
}
const groundTexture = (name) => loadTexture(`${GROUND_FILES[name]}_color.jpg`, true);
const groundNormal = (name) => loadTexture(`${GROUND_FILES[name]}_normal.jpg`, false);
// Start every download and wait for them, so the ground never shows half-loaded (black) textures
function preloadTextures() {
  for (const n of Object.keys(GROUND_FILES)) { groundTexture(n); groundNormal(n); }
  loadTexture('water_normal.jpg', false);
  return Promise.all(texturePromises);
}

// Pond water: opaque, ripples drifting in three directions, and a mirror image of the course
// (trees, sky, hills) that grows stronger at grazing angles (Fresnel), wobbled by the ripples.
// reflection: a reflector() node, or null for sky reflections only (performance setting).
function waterMaterial(env, reflection) {
  const m = new THREE.MeshPhysicalNodeMaterial({ roughness: 0.06, metalness: 0, ior: 1.33 });
  if (env) { m.envMap = env; m.envMapIntensity = reflection ? 0.12 : 0.85; }
  const ripples = loadTexture('water_normal.jpg', false);
  const w = uv();
  const a = texture(ripples, w.mul(1.0).add(time.mul(vec2(0.012, 0.007)))).xyz.mul(2).sub(1);
  const b = texture(ripples, w.mul(0.37).add(time.mul(vec2(-0.009, 0.011)))).xyz.mul(2).sub(1);
  const c = texture(ripples, w.mul(3.1).add(time.mul(vec2(0.02, -0.015)))).xyz.mul(2).sub(1);
  const n = normalize(vec3(a.xy.add(b.xy).add(c.xy.mul(0.5)), a.z.mul(b.z)));
  m.normalNode = normalMap(n.mul(0.5).add(0.5), vec2(0.22));
  // Deep water colour: greener close up, darker further away
  const dist = distance(cameraPosition, positionWorld);
  const deep = mix(vec3(0.035, 0.08, 0.06), vec3(0.018, 0.045, 0.038), smoothstep(10, 120, dist));
  if (!reflection) { m.colorNode = deep; return m; }
  reflection.uvNode = reflection.uvNode.add(n.xy.mul(0.018));
  const facing = clamp(dot(normalView, positionViewDirection), 0, 1);
  const fresnel = float(0.04).add(float(0.96).mul(pow(facing.oneMinus(), 4)));
  m.colorNode = deep.mul(fresnel.oneMinus());
  m.emissiveNode = reflection.rgb.mul(fresnel).mul(0.92);
  return m;
}

// Blurred copy of the course picture (about 2 m per pixel, blurred over ~8 m)
function softTexture(canvas) {
  const k = Math.min(1, 1024 / Math.max(canvas.width, canvas.height));
  const c = document.createElement('canvas');
  c.width = Math.max(1, Math.round(canvas.width * k)); c.height = Math.max(1, Math.round(canvas.height * k));
  const x = c.getContext('2d');
  x.filter = 'blur(4px)';
  x.drawImage(canvas, 0, 0, c.width, c.height);
  const t = new THREE.CanvasTexture(c);
  t.colorSpace = THREE.SRGBColorSpace;
  return t;
}

// Sky dome with Preetham scattering and procedural clouds
function makeSky() {
  const sky = new SkyMesh();
  sky.scale.setScalar(8000);
  sky.turbidity.value = 3.5; sky.rayleigh.value = 1.1; sky.mieCoefficient.value = 0.004; sky.mieDirectionalG.value = 0.82;
  sky.cloudCoverage.value = 0.25; sky.cloudDensity.value = 0.35;
  sky.sunPosition.value.copy(SUN_DIR);
  return sky;
}

// Terrain material (TSL): the photo (or painted map) graded per surface, with grass/sand texture
// near the camera and mowing stripes on fairways and greens.
// Surface mask: R = fairway/tee/collar, G = green, B = sand (high) or cart path (low).
// soft: a heavily blurred copy of the map. Fairways and greens take only its broad light and shade,
// so small things in the photo (trees, shadows, carts, bare patches) never show on them.
function terrainMaterial(map, soft, mask, mowDirs, strength) {
  const maskTex = new THREE.CanvasTexture(mask);
  maskTex.colorSpace = THREE.NoColorSpace;
  const dirTex = new THREE.CanvasTexture(mowDirs.fairway), dirTexG = new THREE.CanvasTexture(mowDirs.green);
  dirTex.colorSpace = dirTexG.colorSpace = THREE.NoColorSpace;
  const material = new THREE.MeshStandardNodeMaterial({ roughness: 0.92, metalness: 0 });
  material.envMapIntensity = 0.4;
  material.colorNode = Fn(() => {
    const vUv = uv();
    const base = texture(map, vUv).rgb;
    const sm = texture(maskTex, vUv).rgb;
    // Crisp edges: tighten the (slightly soft) mask so each surface has a clear outline
    const wf = smoothstep(0.32, 0.68, sm.r), wg = smoothstep(0.3, 0.7, sm.g), ws = smoothstep(0.6, 0.85, sm.b);
    const wp = pathWeight(sm, ws);
    const wr = clamp(float(1).sub(wf).sub(wg).sub(ws).sub(wp), 0, 1);
    // Give each surface its own colour while keeping the photo's light and shade:
    // fairway bright and even, rough deeper and darker, green the smoothest and brightest
    const lum = dot(base, vec3(0.2126, 0.7152, 0.0722));
    const shade = clamp(lum.div(0.3), 0.65, 1.35);
    const shadeSoft = clamp(dot(texture(soft, vUv).rgb, vec3(0.2126, 0.7152, 0.0722)).div(0.3), 0.85, 1.15);
    const graded = vec3(0.15, 0.4, 0.05).mul(mix(1, shadeSoft, 0.35)).mul(wf)
      .add(vec3(0.055, 0.155, 0.02).mul(shade).mul(wr))
      .add(vec3(0.125, 0.43, 0.06).mul(mix(1, shadeSoft, 0.12)).mul(wg))
      .add(vec3(0.5, 0.39, 0.23).mul(mix(1, shade, 0.4)).mul(ws))
      .add(vec3(lum.mul(1.05)).mul(wp));                                    // pavement: the photo, greyed
    // Fairways and greens are fully graded (no photo detail); elsewhere strong enough that a photo
    // slightly out of line with the mapped shapes doesn't show through
    const col = mix(base, graded, wf.add(wr.mul(0.5)).add(wg).add(ws.mul(0.8)).add(wp.mul(0.7))).toVar();

    const q = positionWorld.xz;
    const camD = distance(cameraPosition, positionWorld);
    const near = smoothstep(25, 380, camD).oneMinus();
    // Photo-real close-up detail (CC0 textures): each texture divided by its own average colour,
    // so it adds the look of real blades, grains and grit without changing the graded colours.
    // Two scales each so the tiling doesn't show.
    const LUMA = vec3(0.2126, 0.7152, 0.0722);
    const ratio = (name, scale, offset = 0) => {
      const c = texture(groundTexture(name), q.mul(scale).add(offset)).rgb;
      const m = vec3(...GROUND_MEAN[name]);
      return clamp(mix(vec3(dot(c, LUMA).div(dot(m, LUMA))), c.div(m), 0.3), 0.25, 2.5);
    };
    const dF = mix(ratio('lawn', 0.8), ratio('lawn', 0.21, 0.37), 0.35);
    const dG = ratio('lawn', 1.3);
    const dR = mix(ratio('rough', 0.6), ratio('rough', 0.15, 0.53), 0.4);
    const dS = mix(ratio('sand', 0.35), ratio('sand', 0.09, 0.2), 0.3);
    const dP = ratio('asphalt', 0.6);
    const blend = dF.mul(wf).add(dR.mul(wr)).add(dG.mul(wg)).add(dS.mul(ws)).add(dP.mul(wp));
    // Rough reads clumpier, greens the most even
    const amount = near.mul(strength).mul(float(1).add(wr.mul(0.2)).sub(wg.mul(0.4)));
    col.mulAssign(mix(vec3(1), blend, amount));
    // Broad patchiness in the rough, visible from far away too
    const patchy = dot(texture(groundTexture('rough'), q.mul(0.011)).rgb, LUMA).div(dot(vec3(...GROUND_MEAN.rough), LUMA));
    col.mulAssign(mix(1, patchy, wr.mul(0.14)));

    // Mowing stripes: bands along the hole, lighter or darker depending on which way the
    // grass was laid relative to where you look from (like a real freshly cut fairway)
    const dcF = texture(dirTex, vUv).rg.mul(2).sub(1), dcG = texture(dirTexG, vUv).rg.mul(2).sub(1);
    const angF = atan(dcF.y, dcF.x).mul(0.5), angG = atan(dcG.y, dcG.x).mul(0.5);
    const alongF = vec2(cos(angF), sin(angF)), acrossF = vec2(alongF.y.negate(), alongF.x);
    const alongG = vec2(cos(angG), sin(angG)), acrossG = vec2(alongG.y.negate(), alongG.x);
    const view = normalize(positionWorld.xz.sub(cameraPosition.xz).add(1e-4));
    const facingF = abs(dot(view, alongF)).mul(0.55).add(0.45), facingG = abs(dot(view, alongG)).mul(0.55).add(0.45);
    const band = (dir, width, sharp) => clamp(sin(dot(q, dir).div(width).mul(PI)).mul(sharp), -1, 1);
    const bandF = band(acrossF, 7.5, 5), bandG = band(acrossG, 3, 4), bandG2 = band(alongG, 3, 4);
    const greenFade = smoothstep(60, 180, camD).oneMinus();
    const mow = wf.mul(bandF).mul(0.085).mul(facingF)
      .add(wg.mul(bandG.mul(0.04).add(bandG2.mul(0.022))).mul(facingG).mul(greenFade));
    return col.mul(mow.add(1));
  })();
  material.normalNode = Fn(() => {
    const vUv = uv();
    const sm = texture(maskTex, vUv).rgb;
    const wf = smoothstep(0.32, 0.68, sm.r), wg = smoothstep(0.3, 0.7, sm.g), ws = smoothstep(0.6, 0.85, sm.b);
    const wp = pathWeight(sm, ws);
    const wr = clamp(float(1).sub(wf).sub(wg).sub(ws).sub(wp), 0, 1);
    const q = positionWorld.xz;
    const nt = (name, scale) => texture(groundNormal(name), q.mul(scale)).xyz;
    // (Cart paths go without: WebGPU allows 16 textures per shader and asphalt is smooth enough)
    const nb = nt('lawn', 0.8).mul(wf.add(wg)).add(nt('rough', 0.6).mul(wr)).add(nt('sand', 0.35).mul(ws)).add(vec3(0.5, 0.5, 1).mul(wp));
    const near = smoothstep(15, 160, distance(cameraPosition, positionWorld)).oneMinus();
    return normalMap(mix(vec3(0.5, 0.5, 1), nb, near.mul(float(0.9).sub(wg.mul(0.4)))), vec2(0.7));
  })();
  return material;
}

function closestOnPath(hole, x, z) {
  let best = { d: Infinity, s: 0 };
  for (let i = 1; i < hole.path.length; i++) {
    const a = hole.path[i - 1], b = hole.path[i];
    const dx = b.x - a.x, dz = b.z - a.z, l2 = dx * dx + dz * dz;
    const t = l2 ? Math.max(0, Math.min(1, ((x - a.x) * dx + (z - a.z) * dz) / l2)) : 0;
    const dd = Math.hypot(x - a.x - dx * t, z - a.z - dz * t);
    if (dd < best.d) best = { d: dd, s: hole.cum[i - 1] + t * Math.sqrt(l2) };
  }
  return best;
}
// Keep a code only where most of its neighbourhood agrees (removes speckle, fills pinholes)
function majority(codes, w, h, code, r, need) {
  const src = codes.slice();
  for (let j = 0; j < h; j++) for (let i = 0; i < w; i++) {
    let n = 0;
    for (let dj = -r; dj <= r; dj++) for (let di = -r; di <= r; di++) {
      const ii = i + di, jj = j + dj;
      if (ii >= 0 && jj >= 0 && ii < w && jj < h && src[jj * w + ii] === code) n++;
    }
    const k = j * w + i;
    if (n >= need) { if (!src[k]) codes[k] = code; } else if (src[k] === code) codes[k] = 0;
  }
}
// Trees bucketed on a grid so the ball only checks the few trees around it
class TreeIndex {
  constructor(cell) { this.cell = cell; this.map = new Map(); this.count = 0; this.all = []; }
  key(i, j) { return i * 100003 + j; }
  add(t) {
    // A tree is listed in every cell its crown reaches
    const reach = Math.max(t.rh || 0, t.coneR || 0, t.trunkR);
    const i0 = Math.floor((t.x - reach) / this.cell), i1 = Math.floor((t.x + reach) / this.cell);
    const j0 = Math.floor((t.z - reach) / this.cell), j1 = Math.floor((t.z + reach) / this.cell);
    for (let i = i0; i <= i1; i++) for (let j = j0; j <= j1; j++) {
      const k = this.key(i, j);
      if (!this.map.has(k)) this.map.set(k, []);
      this.map.get(k).push(t);
    }
    this.count++;
    this.all.push(t);
  }
  near(x, z) { return this.map.get(this.key(Math.floor(x / this.cell), Math.floor(z / this.cell))) || EMPTY; }
}
const EMPTY = [];

// Long axis (unit vector) of a set of 2D points
function principalAxis(pts) {
  let mx = 0, my = 0;
  for (const [x, y] of pts) { mx += x; my += y; }
  mx /= pts.length; my /= pts.length;
  let sxx = 0, syy = 0, sxy = 0;
  for (const [x, y] of pts) { const dx = x - mx, dy = y - my; sxx += dx * dx; syy += dy * dy; sxy += dx * dy; }
  const ang = 0.5 * Math.atan2(2 * sxy, sxx - syy);
  return [Math.cos(ang), Math.sin(ang)];
}
// Connected patches (4-neighbour) of a code, as lists of cell indices
function components(codes, w, h, code) {
  const seen = new Uint8Array(w * h), out = [], stack = [];
  for (let s = 0; s < codes.length; s++) {
    if (codes[s] !== code || seen[s]) continue;
    const comp = [];
    stack.push(s); seen[s] = 1;
    while (stack.length) {
      const k = stack.pop(); comp.push(k);
      const i = k % w, j = (k - i) / w;
      for (const [di, dj] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) {
        const ii = i + di, jj = j + dj, kk = jj * w + ii;
        if (ii >= 0 && jj >= 0 && ii < w && jj < h && !seen[kk] && codes[kk] === code) { seen[kk] = 1; stack.push(kk); }
      }
    }
    out.push(comp);
  }
  return out;
}

// Remove connected patches of a code smaller than minArea cells
function removeSmall(codes, w, h, code, minArea) {
  const seen = new Uint8Array(w * h), stack = [];
  for (let s = 0; s < codes.length; s++) {
    if (codes[s] !== code || seen[s]) continue;
    const comp = [];
    stack.push(s); seen[s] = 1;
    while (stack.length) {
      const k = stack.pop(); comp.push(k);
      const i = k % w, j = (k - i) / w;
      for (const [di, dj] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) {
        const ii = i + di, jj = j + dj, kk = jj * w + ii;
        if (ii >= 0 && jj >= 0 && ii < w && jj < h && !seen[kk] && codes[kk] === code) { seen[kk] = 1; stack.push(kk); }
      }
    }
    if (comp.length < minArea) for (const k of comp) codes[k] = 0;
  }
}

// White-balance an aerial photo towards natural vegetation colours and boost saturation a little.
// Returns the channel gains so other imagery of the same course can be matched.
function colorBalance(ctx, w, h, gains = null) {
  const img = ctx.getImageData(0, 0, w, h), d = img.data;
  if (!gains) {
    let r = 0, g = 0, b = 0, n = 0;
    for (let i = 0; i < d.length; i += 4 * 37) { r += d[i]; g += d[i + 1]; b += d[i + 2]; n++; }
    r /= n; g /= n; b /= n;
    // Typical summer course from the air: R:G:B about 0.82 : 1 : 0.62, mid-tones around 118
    const lift = 118 / Math.max(40, (r + g + b) / 3) * 0.92;
    gains = [Math.min(1.6, (0.82 * g) / r) * lift, lift, Math.min(1.4, (0.62 * g) / b) * lift];
  }
  const [kr, kg, kb] = gains, sat = 1.22, contrast = 1.06;
  for (let i = 0; i < d.length; i += 4) {
    let r = d[i] * kr, g = d[i + 1] * kg, b = d[i + 2] * kb;
    const l = 0.3 * r + 0.59 * g + 0.11 * b;
    r = l + (r - l) * sat; g = l + (g - l) * sat; b = l + (b - l) * sat;
    d[i] = (r - 118) * contrast + 118; d[i + 1] = (g - 118) * contrast + 118; d[i + 2] = (b - 118) * contrast + 118;
  }
  ctx.putImageData(img, 0, 0);
  return gains;
}

// Soft round spot: black (shadows) or white (glowing dots)
function radialTexture(white = false) {
  const c = document.createElement('canvas'); c.width = c.height = 64;
  const x = c.getContext('2d');
  const g = x.createRadialGradient(32, 32, 0, 32, 32, 32);
  const rgb = white ? '255,255,255' : '0,0,0';
  g.addColorStop(0, `rgba(${rgb},${white ? 1 : 0.9})`); if (white) g.addColorStop(0.45, `rgba(${rgb},0.9)`); g.addColorStop(1, `rgba(${rgb},0)`);
  x.fillStyle = g; x.fillRect(0, 0, 64, 64);
  return new THREE.CanvasTexture(c);
}
