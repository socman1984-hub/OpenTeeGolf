// Three.js rendering of a course built by course.js, on WebGPU (falls back to WebGL 2 where
// WebGPU isn't available; add ?webgl to the address to force that).
// Course space is left-handed (Unity); three.js is right-handed, so x is mirrored: T(x,y,z) = (-x, y, z).

import * as THREE from 'three/webgpu';
import {
  Fn, texture, uv, vec2, vec3, float, mix, clamp, smoothstep, dot, distance, normalize, sin, cos, abs, atan,
  positionWorld, cameraPosition, instancedBufferAttribute, PI, normalMap, time, reflector, normalView, positionViewDirection, pow,
  uniform, select, fwidth, normalWorld, attribute, fract, floor, step,
} from 'three/tsl';
import { SkyMesh } from 'three/addons/objects/SkyMesh.js';
import { Line2 } from 'three/addons/lines/webgpu/Line2.js';
import { LineGeometry } from 'three/addons/lines/LineGeometry.js';
import { MapControls } from 'three/addons/controls/MapControls.js';
import { mergeGeometries, mergeVertices } from 'three/addons/utils/BufferGeometryUtils.js';
import { noise, pointInPoly } from './course.js';
import { Forest, treesFromCanopy, assignSpecies, addMappedTrees, addTreeAreas, fade, impostorLight } from './trees.js';
import { treesFromPhoto, removeTrees } from './treeplace.js';
import { tuftGeometry, reedGeometry, grassMaterial, grassMesh, grassWind, addBend, NATIVE_KINDS, nativeClumpGeometry } from './grass.js';
import { MOW_WIDTH, mowIndex } from './mowing.js';
import { layoutParking, bayCorners, carColour } from './parking.js';
import { roadLines, roadSamples, stripTriangles, onRoad } from './roads.js';
import { MIN_HW } from './streams.js';

// The course's mowing patterns (pattern numbers from mowing.js), read by the terrain shader
const mowPattern = { fairway: uniform(1), green: uniform(3) };

const T = (x, y, z) => new THREE.Vector3(-x, y, z);
const TEX_RES = 1.5; // meters per texel of the generated (no imagery) course texture

const SURFACE_RGB = {
  green: [92, 168, 64],
  fringe: [84, 150, 56],
  fairway: [102, 156, 58],
  tee: [104, 160, 60],
  rough: [68, 92, 40],
  firstcut: [84, 138, 50],
  deeprough: [56, 74, 32],
  water: [60, 70, 50],
  oob: [62, 82, 36],
  sand: [214, 196, 150],
};
// Surfaces that grow rough grass (tufts, the patch around a ball at rest)
const ROUGH = new Set(['rough', 'deeprough', 'native', 'oob']);
// A steady 0..1 value for a spot on the course
const hash01 = (x, z) => { const s = Math.sin(x * 12.9898 + z * 78.233) * 43758.5453; return s - Math.floor(s); };
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
    // (layer 1: tall grass - drawn by this camera, but not again by the water's mirror, whose
    // camera sees only layer 0)
    this.camera.layers.enable(1);
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
    this.ball = new THREE.Mesh(new THREE.SphereGeometry(0.12, 64, 40), golfBallMaterial());
    this.spinTilt = 0;
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
      // grass: rough tufts around the ball / free camera, blades per tuft; lie: tufts in the patch the ball sits in
      high: { dpr: 2, shadow: 4096, trees: 1, grass: 32000, blades: 11, lie: 9000, native: 1 },
      balanced: { dpr: 1.25, shadow: 2048, trees: 0.85, grass: 20000, blades: 8, lie: 6000, native: 0.7 },
      performance: { dpr: 1, shadow: 1024, trees: 0.6, grass: 9000, blades: 5, lie: 2500, native: 0.4 },
    }[q] || null;
    if (!Q) return;
    this.quality = { name: q, ...Q };
    this.renderer.setPixelRatio(Math.min(devicePixelRatio, Q.dpr));
    if (this.sun.shadow.mapSize.x !== Q.shadow) {
      this.sun.shadow.mapSize.set(Q.shadow, Q.shadow);
      this.sun.shadow.map?.dispose();
      this.sun.shadow.map = null;
    }
    if (this.grass) { this.dynamic.remove(this.grass, this.grassBack); this.grass = this.grassBack = this.grassJob = null; this.grassCenter = null; }
    if (this.lieGrass) { this.dynamic.remove(this.lieGrass); this.lieGrass = null; this.lieKey = null; }
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
    this.setMowing(course.mowing);
    const shore = this.buildShore(course);
    const mat = terrainMaterial(tex, softTexture(this.courseCanvas), mask, this.buildMowDirections(course), sat ? 0.8 : 0.9, shore.canvas, shore.hasSand);
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
    this.addBridges(course);
    this.addBankGrass(course, shore);
    this.addReedsAndRocks(course, shore);
    this.addStakesAndMarkers(course);
    // Trees: the canopy height map's, then OpenStreetMap's and the player's, then the ones the
    // aerial photo shows where none stand yet; less any taken out in the course editor
    this.pendingTrees = []; this.photoTrees = 0; this.photoTreeList = null;
    if (canopy) this.addTreesFromCanopy(course, canopy);
    else if (!sat) this.addTrees(course);
    this.addOsmTrees(course, canopy);
    if (this.photoRaster) this.addPhotoTrees(course, canopy);
    this.photoRaster = null;
    this.pendingTrees = removeTrees(this.pendingTrees, course.removedTrees);
    // No trunks on roads or cart paths (mapped, or seen in the photo): crowns found from above
    // often hang over one. Such a tree moves to the nearest clear spot within 3 m, or goes. (The
    // player's own trees stay where they were put.)
    const lanes = [...(course.roads?.roads || []), ...(course.paths || [])];
    const paved = onRoad(lanes, 0.8);
    const blocked = (x, z) => paved(x, z) || this.nearPath(x, z, 0.8);
    this.pendingTrees = this.pendingTrees.filter((t) => {
      if (t.user || !blocked(t.x, t.z)) return true;
      for (const d of [1, 1.5, 2, 2.5, 3]) for (let k = 0; k < 12; k++) {
        const a = (k / 12) * Math.PI * 2, x = t.x + Math.cos(a) * d, z = t.z + Math.sin(a) * d;
        if (!blocked(x, z) && course.heightAt(x, z) !== null) { t.x = x; t.z = z; return true; }
      }
      return false;
    });
    await this.plantForest(course, this.pendingTrees, progress);
    this.addNativeGrass(course, sat); // (also clears the last course's, without a photo)
    this.addRoughCarpet(course);
    if (buildings.length) this.addBuildings(course, buildings);
    this.addCars(course);
    this.addRoadMarkings(course);
    this.grassCenter = null; this.grassJob = null;
    if (this.grass) this.grass.count = 0; // (the next lot is laid out at once, for the new course)
    if (this.lieGrass) this.lieGrass.count = 0;
    this.lieKey = null;
    this.applyEnvIntensity(this.courseGroup);
    this.applyEnvIntensity(this.dynamic);
    // Build every shader now rather than stalling on the first frames of play
    await progress('shaders', 'Preparing graphics');
    // (everything, not just what's in view now: the tall grass tiles, say, would otherwise each
    // stall a frame the first time they come into view)
    const culled = [];
    this.scene.traverse((o) => { if (o.isMesh && o.frustumCulled) { culled.push(o); o.frustumCulled = false; } });
    try { await this.renderer.compileAsync(this.scene, this.camera); } catch (err) { console.warn('Shader warm-up skipped', err); }
    for (const o of culled) o.frustumCulled = true;
  }

  // Mowing patterns on fairways and greens: { fairway, green } pattern ids (mowing.js)
  setMowing(m) {
    if (m?.fairway) mowPattern.fairway.value = mowIndex(m.fairway);
    if (m?.green) mowPattern.green.value = mowIndex(m.green);
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
    // A 1 m copy for finding tree crowns (see addPhotoTrees)
    const tr = document.createElement('canvas');
    tr.width = Math.ceil(maxX - minX); tr.height = Math.ceil(maxZ - minZ);
    tr.getContext('2d').drawImage(canvas, 0, 0, tr.width, tr.height);
    this.photoRaster = { data: tr.getContext('2d').getImageData(0, 0, tr.width, tr.height).data, w: tr.width, h: tr.height, res: 1, minX, maxZ };
    // Car parks: which bays have a car in the photo (read before anything is painted over it)
    this.parking = this.findCars(course, ctx, res);

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
    this.paintRoads(ctx, course, res, minX, maxZ);
    this.paintParking(ctx, res, minX, maxZ);
    return canvas;
  }

  // ---- public roads (OpenStreetMap)

  // The road surface: dark asphalt over the photo (all at once, so junctions join cleanly)
  paintRoads(ctx, course, res, minX, maxZ) {
    const roads = course.roads?.roads || [];
    if (!roads.length) return;
    ctx.lineCap = 'round'; ctx.lineJoin = 'round';
    for (const [pad, colour] of [[0.5, 'rgba(92,90,84,0.55)'], [0, 'rgba(57,57,56,0.97)']]) {
      ctx.strokeStyle = colour;
      for (const r of roads) {
        ctx.beginPath();
        r.pts.forEach(([x, z], i) => { const px = (x - minX) / res, py = (maxZ - z) / res; if (i) ctx.lineTo(px, py); else ctx.moveTo(px, py); });
        ctx.lineWidth = (r.width + pad) / res; ctx.stroke();
      }
    }
  }

  // The lines painted on main roads (see roadLines), as thin strips just above the ground
  // ---- bridges: where a cart path runs into a pond or stream, a timber footbridge carries it
  // across. Each spans the water plus about 2 m of bank either side, rising from the path's
  // height at each end to at least half a metre above the water mid-span (a gentle arch), with a
  // plank deck, a fascia board down each side, and posts and a rail along both edges.
  addBridges(course) {
    const water = (course.waterBodies || []).filter((wb) => wb.poly?.length > 2);
    this.bridgeCount = 0;
    if (!water.length || !course.paths?.length) return;
    const bbox = water.map((wb) => {
      const xs = wb.poly.map((p) => p[0]), zs = wb.poly.map((p) => p[1]);
      return [Math.min(...xs), Math.min(...zs), Math.max(...xs), Math.max(...zs)];
    });
    const waterAt = (x, z) => {
      for (let i = 0; i < water.length; i++) {
        const b = bbox[i];
        if (x >= b[0] && x <= b[2] && z >= b[1] && z <= b[3] && pointInPoly(water[i].poly, x, z)) return water[i];
      }
      return null;
    };
    const decks = [];
    for (const path of course.paths) {
      // the path every half metre
      const S = [];
      for (let i = 1; i < path.pts.length; i++) {
        const [ax, az] = path.pts[i - 1], [bx, bz] = path.pts[i], n = Math.max(1, Math.ceil(Math.hypot(bx - ax, bz - az) / 0.5));
        for (let k = i === 1 ? 0 : 1; k <= n; k++) S.push([ax + ((bx - ax) * k) / n, az + ((bz - az) * k) / n]);
      }
      for (let k = 0; k < S.length; k++) {
        if (!waterAt(...S[k])) continue;
        let e = k;
        while (e + 1 < S.length && waterAt(...S[e + 1])) e++;
        if (e - k >= 1) {
          const a = Math.max(0, k - 4), b = Math.min(S.length - 1, e + 4);
          const ends = [course.heightAt(...S[a]), course.heightAt(...S[b])];
          if (ends[0] !== null && ends[1] !== null) {
            const pts = S.slice(a, b + 1).map(([x, z], i, arr) => {
              const wb = waterAt(x, z), lvl = wb ? (wb.levelAt ? wb.levelAt(x, z) : wb.level) : -Infinity;
              const lin = ends[0] + (ends[1] - ends[0]) * (i / (arr.length - 1)) + 0.12;
              return [x, z, Math.max(lin, lvl + 0.55)];
            });
            // (smoothed into a gentle arch, the ends kept where they meet the path)
            for (let pass = 0; pass < 3; pass++) for (let i = 1; i < pts.length - 1; i++) pts[i][2] = Math.max(pts[i][2], (pts[i - 1][2] + pts[i + 1][2]) / 2);
            decks.push({ pts, w: Math.max(2.2, path.width + 0.6) });
          }
        }
        k = e;
      }
    }
    if (!decks.length) return;
    const pos = [], col = [];
    const quad = (a, b, c, d, rgb) => { pos.push(...a, ...b, ...c, ...a, ...c, ...d); for (let i = 0; i < 6; i++) col.push(...rgb); };
    const W3 = (x, y, z) => [-x, y, z];
    const posts = [];
    for (const { pts, w } of decks) {
      const hw = w / 2;
      let along = 0;
      for (let i = 0; i < pts.length; i++) {
        const p = pts[i], a = pts[Math.max(0, i - 1)], b = pts[Math.min(pts.length - 1, i + 1)];
        const dx = b[0] - a[0], dz = b[1] - a[1], l = Math.hypot(dx, dz) || 1, nx = -dz / l, nz = dx / l;
        p.n = [nx, nz];
        if (i && (along += Math.hypot(p[0] - pts[i - 1][0], p[1] - pts[i - 1][1])) >= 2 || i === 0 || i === pts.length - 1) {
          if (i) along = 0;
          for (const side of [-1, 1]) posts.push([p[0] + nx * hw * side, p[1] + nz * hw * side, p[2]]);
        }
      }
      for (let i = 1; i < pts.length; i++) {
        const p = pts[i - 1], q = pts[i];
        const L = (o, s2, dy = 0) => W3(o[0] + o.n[0] * hw * s2, o[2] + dy, o[1] + o.n[1] * hw * s2);
        // planks: alternate shades of weathered timber
        // (vertex colours are linear: a weathered brown timber is quite dark in these numbers)
        const v = 0.85 + ((i * 7) % 3) * 0.1, wood = [0.16 * v, 0.085 * v, 0.042 * v];
        quad(L(p, -1), L(q, -1), L(q, 1), L(p, 1), wood);
        for (const s2 of [-1, 1]) {
          // fascia down the side, and the rail along the top of the posts
          quad(L(p, s2), L(q, s2), L(q, s2, -0.3), L(p, s2, -0.3), [0.09, 0.05, 0.026]);
          quad(L(p, s2, 0.86), L(q, s2, 0.86), L(q, s2, 0.96), L(p, s2, 0.96), [0.12, 0.066, 0.034]);
        }
      }
    }
    const deck = new THREE.BufferGeometry();
    deck.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
    deck.setAttribute('color', new THREE.Float32BufferAttribute(col, 3));
    deck.computeVertexNormals();
    const parts = [deck];
    for (const [x, z, y] of posts) {
      const g = new THREE.BoxGeometry(0.12, 1.25, 0.12).toNonIndexed();
      g.deleteAttribute('uv');
      g.translate(-x, y + 0.35, z);
      const c = []; for (let i = 0; i < g.attributes.position.count; i++) c.push(0.1, 0.056, 0.03);
      g.setAttribute('color', new THREE.Float32BufferAttribute(c, 3));
      parts.push(g);
    }
    const mat = new THREE.MeshStandardMaterial({ vertexColors: true, roughness: 0.9, metalness: 0, side: THREE.DoubleSide });
    const mesh = new THREE.Mesh(mergeGeometries(parts), mat);
    mesh.castShadow = mesh.receiveShadow = true;
    this.courseGroup.add(mesh);
    this.bridgeCount = decks.length;
  }

  addRoadMarkings(course) {
    const roads = course.roads?.roads || [];
    const flat = { white: [], yellow: [] };
    for (const r of roads) {
      const lines = roadLines(r, course.roads.yellowCentre);
      if (!lines.length) continue;
      const sm = roadSamples(r.pts);
      for (const l of lines) stripTriangles(sm, l.offset, l.width, l.dash, flat[l.yellow ? 'yellow' : 'white']);
    }
    for (const [colour, xz] of Object.entries(flat)) {
      if (!xz.length) continue;
      const pos = new Float32Array((xz.length / 2) * 3);
      let n = 0;
      for (let i = 0; i < xz.length; i += 6) {
        // (whole triangles only, and none past the edge of the terrain)
        const y0 = course.heightAt(xz[i], xz[i + 1]), y1 = course.heightAt(xz[i + 2], xz[i + 3]), y2 = course.heightAt(xz[i + 4], xz[i + 5]);
        if (y0 === null || y1 === null || y2 === null) continue;
        pos.set([-xz[i], y0 + 0.04, xz[i + 1], -xz[i + 2], y1 + 0.04, xz[i + 3], -xz[i + 4], y2 + 0.04, xz[i + 5]], n);
        n += 9;
      }
      if (!n) continue;
      const geo = new THREE.BufferGeometry();
      geo.setAttribute('position', new THREE.BufferAttribute(pos.slice(0, n), 3));
      const up = new Float32Array(n);
      for (let i = 1; i < n; i += 3) up[i] = 1;
      geo.setAttribute('normal', new THREE.BufferAttribute(up, 3));
      const mat = new THREE.MeshStandardMaterial({
        color: colour === 'yellow' ? 0xd8a51e : 0xe4e4dc, roughness: 0.7, side: THREE.DoubleSide,
        polygonOffset: true, polygonOffsetFactor: -2, polygonOffsetUnits: -4,
      });
      const mesh = new THREE.Mesh(geo, mat);
      mesh.receiveShadow = true;
      this.courseGroup.add(mesh);
    }
  }

  // ---- car parks (OpenStreetMap) and the cars parked in them

  // Lay out the bays and decide which have a car. With the aerial photo (ctx, res m per pixel):
  // a bay holds a car where the photo there differs clearly from the car park's own surface,
  // and the car takes that colour. Without one, about half the bays are taken, in common colours.
  // A car park that is mostly green in the photo is grass (overflow parking), so it isn't painted.
  findCars(course, ctx = null, res = 1) {
    const out = { lots: [], cars: [] };
    if (!course.parking?.lots?.length) return out;
    const { minX, maxX, minZ, maxZ } = course.bounds;
    const bays = layoutParking(course.parking).filter((b) => b.x > minX && b.x < maxX && b.z > minZ && b.z < maxZ);
    out.lots = course.parking.lots.map((lot) => ({ ...lot, bays: [], paved: /asphalt|paved|concrete/.test(lot.surface || 'asphalt') }));
    for (const b of bays) out.lots[b.lot].bays.push(b);
    const MAX_CARS = 2500;
    for (const lot of out.lots) {
      if (!lot.bays.length) continue;
      let pixel = null;
      if (ctx) {
        // The photo under the car park, read once
        const xs = lot.pts.map((p) => p[0]), zs = lot.pts.map((p) => p[1]);
        const px0 = Math.max(0, Math.floor((Math.min(...xs) - minX) / res)), py0 = Math.max(0, Math.floor((maxZ - Math.max(...zs)) / res));
        const pw = Math.min(ctx.canvas.width - px0, Math.ceil((Math.max(...xs) - Math.min(...xs)) / res) + 2);
        const ph = Math.min(ctx.canvas.height - py0, Math.ceil((Math.max(...zs) - Math.min(...zs)) / res) + 2);
        if (pw < 2 || ph < 2) continue;
        const d = ctx.getImageData(px0, py0, pw, ph).data;
        pixel = (x, z) => {
          const i = Math.floor((x - minX) / res) - px0, j = Math.floor((maxZ - z) / res) - py0;
          if (i < 0 || j < 0 || i >= pw || j >= ph) return null;
          const o = (j * pw + i) * 4;
          return [d[o], d[o + 1], d[o + 2]];
        };
        // The car park's own surface: the median colour over its bays, and how green it is
        const samples = [];
        for (const b of lot.bays) for (const [x, z] of bayCorners({ ...b, w: b.w * 0.95, d: b.d * 1.05 })) { const c = pixel(x, z); if (c) samples.push(c); }
        if (samples.length < 4) { pixel = null; } else {
          const med = [0, 1, 2].map((k) => samples.map((c) => c[k]).sort((a, b) => a - b)[samples.length >> 1]);
          const green = samples.filter(([r, g, b]) => g > r * 1.08 && g > b * 1.05).length / samples.length;
          if (green > 0.45) lot.paved = false;
          lot.ground = med;
        }
      }
      const lotCars = [];
      for (const b of lot.bays) {
        if (out.cars.length + lotCars.length >= MAX_CARS) break;
        const n = hash01(b.x * 1.7, b.z * 2.3);
        let col = null;
        if (pixel && lot.ground) {
          // A car stands out both from the car park as a whole and from the aisle right in front
          // of its bay. (Comparing with the aisle too keeps tree shadows, worn tracks and patchy
          // ground - which cover the aisle as well - from passing as cars.)
          const vx = -b.uz, vz = b.ux, at = (s, t) => pixel(b.x + vx * s + b.ux * t, b.z + vz * s + b.uz * t);
          const patch = (t0, t1) => {
            const px = [];
            for (let s = -0.75; s <= 0.75; s += 0.375) for (let t = t0; t <= t1; t += 0.5) { const c = at(s, t); if (c) px.push(c); }
            return px;
          };
          const med = (px) => [0, 1, 2].map((k) => px.map((c) => c[k]).sort((a, b2) => a - b2)[px.length >> 1]);
          const dist = (c, m) => Math.hypot(c[0] - m[0], c[1] - m[1], c[2] - m[2]);
          const car = patch(-2, 2);
          // (the aisle is at -u; bays mapped one by one could face either way: the end more
          // like the car park's surface)
          let aisle = patch(-b.d / 2 - 2.5, -b.d / 2 - 0.8);
          if (b.aisle === false) {
            const other = patch(b.d / 2 + 0.8, b.d / 2 + 2.5);
            if (other.length && (!aisle.length || dist(med(other), lot.ground) < dist(med(aisle), lot.ground))) aisle = other;
          }
          if (car.length < 10) continue;
          const am = aisle.length >= 4 ? med(aisle) : lot.ground;
          // Green isn't a car: tree crowns over the bay, their shadows, grass and mown stripes. (A
          // bay that is mostly green is under a tree.) Green measured against the car park's own
          // surface, as the photo may have a green cast that tints every car a little.
          const exg = (c) => 2 * c[1] - c[0] - c[2], groundExg = exg(lot.ground);
          const green = (c) => c[1] > c[0] && exg(c) > Math.max(25, groundExg + 22);
          if (car.filter(green).length > car.length * 0.35) continue;
          const hits = car.filter((c) => dist(c, lot.ground) > 30 && dist(c, am) > 30 && !green(c));
          const dGround = car.reduce((sum, c) => sum + dist(c, lot.ground), 0) / car.length;
          const dAisle = car.reduce((sum, c) => sum + dist(c, am), 0) / car.length;
          const need = lot.paved ? 1 : 1.35; // (stricter on unpaved ground, usually empty)
          if (dGround < 26 * need || dAisle < 26 * need || hits.length < car.length * 0.35 * need) continue; // an empty bay
          // Most cars are white, black, grey or silver, and the photo has a colour cast (often
          // green): a car is a neutral shade of its brightness unless it's clearly coloured (red,
          // blue...), when that colour is strengthened a little
          col = [0, 1, 2].map((k) => hits.reduce((sum, c) => sum + c[k], 0) / hits.length / 255);
          const grey = (col[0] + col[1] + col[2]) / 3, chroma = Math.max(...col) - Math.min(...col);
          const greenish = col[1] >= col[0] * 0.95 && col[1] >= col[2]; // (green and olive: the cast)
          const shade = Math.min(0.95, Math.max(0.06, (grey - 0.2) * 1.5 + 0.2));
          col = chroma < 0.12 || greenish ? [shade, shade, shade * 1.02] : col.map((v) => Math.min(1, Math.max(0, grey + (v - grey) * 1.6)));
        } else {
          if (n > 0.55) continue;
          col = carColour(hash01(b.z * 3.1, b.x * 0.7));
        }
        // Some cars are reversed in
        const rev = hash01(b.x, b.z * 5.3) < 0.25 ? -1 : 1;
        lotCars.push({ x: b.x, z: b.z, ux: b.ux * rev, uz: b.uz * rev, col, suv: hash01(b.x * 0.3, b.z * 0.9) < 0.35 });
      }
      // A grass or dirt (overflow) car park in use is full of cars; a few scattered "cars" there
      // are bare patches, tracks and tree shadows, so it stays empty
      if (pixel && lot.ground && !lot.paved && lotCars.length < lot.bays.length * 0.45) continue;
      out.cars.push(...lotCars);
    }
    return out;
  }

  // Paved car parks on the ground picture: fresh asphalt with white bay lines (painted over the
  // photo, whose cars become the 3D ones)
  paintParking(ctx, res, minX, maxZ) {
    const lots = this.parking?.lots?.filter((l) => l.paved && l.bays.length) || [];
    if (!lots.length) return;
    const P = (x, z) => [(x - minX) / res, (maxZ - z) / res];
    for (const lot of lots) {
      ctx.beginPath();
      lot.pts.forEach(([x, z], i) => (i ? ctx.lineTo(...P(x, z)) : ctx.moveTo(...P(x, z))));
      ctx.closePath();
      ctx.fillStyle = 'rgba(74,74,72,0.96)'; ctx.fill();
      ctx.strokeStyle = 'rgba(105,104,100,0.9)'; ctx.lineWidth = 0.4 / res; ctx.stroke(); // (kerb)
    }
    if (res > 0.6) return; // (lines too fine for the picture)
    // (12 cm lines; finer than a pixel of the picture they're drawn fainter instead)
    const lw = 0.12 / res;
    ctx.strokeStyle = `rgba(236,236,230,${Math.min(0.85, 0.85 * Math.max(0.55, lw))})`; ctx.lineWidth = Math.max(0.6, lw); ctx.lineCap = 'butt';
    ctx.beginPath();
    for (const lot of lots) for (const b of lot.bays) {
      // Each bay's two side lines (shared lines are simply drawn twice)
      const [c0, c1, c2, c3] = bayCorners(b);
      ctx.moveTo(...P(...c0)); ctx.lineTo(...P(...c3));
      ctx.moveTo(...P(...c1)); ctx.lineTo(...P(...c2));
    }
    ctx.stroke();
  }

  // The cars: two simple body styles (cars and SUVs), drawn as instances, with collision boxes
  // so a ball bounces off them
  addCars(course) {
    const cars = this.parking?.cars || [];
    this.carBoxes = [];
    if (!cars.length) return;
    const m = new THREE.Matrix4(), q = new THREE.Quaternion(), p = new THREE.Vector3(), one = new THREE.Vector3(1, 1, 1), c = new THREE.Color();
    const up = new THREE.Vector3(0, 1, 0);
    const mat = new THREE.MeshStandardMaterial({ vertexColors: true, roughness: 0.32, metalness: 0.35 });
    for (const suv of [false, true]) {
      const list = cars.filter((car) => car.suv === suv);
      if (!list.length) continue;
      const mesh = new THREE.InstancedMesh(carGeometry(suv), mat, list.length);
      mesh.castShadow = mesh.receiveShadow = true;
      list.forEach((car, i) => {
        const y = course.heightAt(car.x, car.z) ?? 0;
        // Length along local x; three.js x is mirrored
        q.setFromAxisAngle(up, Math.atan2(-car.uz, -car.ux));
        m.compose(p.copy(T(car.x, y, car.z)), q, one);
        mesh.setMatrixAt(i, m);
        mesh.setColorAt(i, c.setRGB(...car.col, THREE.SRGBColorSpace));
        // Collision: the car's footprint, as a low box
        const L = suv ? 2.4 : 2.3, W = suv ? 0.97 : 0.92, vx = -car.uz, vz = car.ux;
        this.carBoxes.push({ h: suv ? 1.75 : 1.45, pts: [[-1, -1], [1, -1], [1, 1], [-1, 1]].map(([s, t]) => [car.x + vx * W * s + car.ux * L * t, car.z + vz * W * s + car.uz * L * t]) });
      });
      mesh.instanceMatrix.needsUpdate = true; mesh.instanceColor.needsUpdate = true;
      this.courseGroup.add(mesh);
    }
    this.applyEnvIntensity(mat);
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
      ctx.lineWidth = 2 / res; ctx.strokeStyle = 'rgb(120,0,0)';
      each('fairway', () => ctx.stroke());                       // first cut: a narrow half-way band
      ctx.fillStyle = 'rgb(255,0,0)'; each('fairway', () => ctx.fill()); each('tee', () => ctx.fill());
      ctx.lineWidth = 3 / res; ctx.strokeStyle = 'rgb(255,0,0)';               // collar: fairway-height ring
      each('green', () => ctx.stroke());                          // fringe
      ctx.fillStyle = 'rgb(0,255,0)'; each('green', () => ctx.fill());
      ctx.fillStyle = 'rgb(0,0,255)'; each('sand', () => ctx.fill());
      // Paved car parks: pavement too (same code as the cart paths below)
      ctx.fillStyle = 'rgb(0,40,90)';
      for (const lot of this.parking?.lots || []) if (lot.paved && lot.bays.length) { path(lot.pts); ctx.fill(); }
      // Cart paths: blue 90 plus green 40. Needing both keeps the soft edges of bunkers (blue only)
      // and greens (green only) from reading as path.
      // (roads too: pavement)
      ctx.strokeStyle = 'rgb(0,40,90)'; ctx.lineCap = 'round'; ctx.lineJoin = 'round';
      for (const pth of [...(course.paths || []), ...(course.roads?.roads || [])]) {
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
    sc.filter = 'blur(1.2px)'; // (enough that path edges are smooth ramps, not steps)
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
    const layer = (surfaces, blue) => {
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
      // Blue (unused by the directions; 'lighten' keeps the larger value per channel, so red and
      // green are untouched). Fairway map: how muddy each pond's bank is, out to 6 m from the water.
      // Green map: where the tall unmown grass grows, for the tawny ground beneath it.
      x2.globalCompositeOperation = 'lighten'; x2.lineJoin = 'round'; x2.lineWidth = 6;
      if (blue === 'mud') for (const wb of course.waterBodies || []) {
        const b = Math.round(255 * ({ natural: 1, reedy: 0.8, rocky: 0.45, manicured: 0.05 }[wb.style] ?? 1));
        x2.fillStyle = x2.strokeStyle = `rgb(0,0,${b})`;
        trace({ poly: wb.poly }); x2.fill(); x2.stroke();
      }
      if (blue === 'native' && ov) {
        const nc = document.createElement('canvas');
        nc.width = ov.w; nc.height = ov.h;
        const nx = nc.getContext('2d'), ni = nx.createImageData(ov.w, ov.h);
        for (let k = 0; k < ov.codes.length; k++) if (ov.codes[k] === 3) { ni.data[k * 4 + 2] = 255; ni.data[k * 4 + 3] = 255; }
        nx.putImageData(ni, 0, 0);
        x2.filter = 'blur(1px)';
        x2.drawImage(nc, (ov.minX - minX) / fres, (maxZ - ov.maxZ) / fres, (ov.w * ov.res) / fres, (ov.h * ov.res) / fres);
        x2.filter = 'none';
      }
      x2.globalCompositeOperation = 'source-over';
      return c2;
    };
    return { fairway: layer(['fairway', 'tee'], 'mud'), green: layer(['green'], 'native') };
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
    // Car parks (no photo: cars in about half the bays)
    this.parking = this.findCars(course);
    sctx.filter = 'none';
    this.paintRoads(sctx, course, TEX_RES, minX, maxZ);
    this.paintParking(sctx, TEX_RES, minX, maxZ);
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
    for (const m of this.waterMeshes || []) if (!m.userData.flowing) m.material = this.waterMat;
    // Let go of the old ones after the next frame has stopped using them
    if (old || oldMat) setTimeout(() => { oldMat?.dispose(); old?.dispose?.(); }, 500);
  }

  addWater(course) {
    this.hasWater = course.waterBodies.length > 0;
    this.waterMeshes = [];
    this.makeWaterMaterial();
    this.flowMat?.dispose(); this.streamMat?.dispose();
    this.flowMat = course.waterBodies.some((wb) => wb.levelAt && !wb.ribbon) ? flowingWaterMaterial(this.scene.environment) : null;
    this.streamMat = course.waterBodies.some((wb) => wb.ribbon) ? streamMaterial(this.scene.environment) : null;
    // (the mirror follows the ponds; streams run and break up any reflection)
    this.waterCentres = course.waterBodies.filter((wb) => !wb.levelAt).map((wb) => {
      let x = 0, z = 0;
      for (const [px, pz] of wb.poly) { x += px; z += pz; }
      return { x: x / wb.poly.length, z: z / wb.poly.length, level: wb.level };
    });
    for (const wb of course.waterBodies) {
      if (wb.ribbon && wb.levelAt) {
        const m = new THREE.Mesh(streamGeometry(wb, meshGroundAt(course)), this.streamMat);
        m.userData.flowing = true;
        m.position.y = wb.level;
        m.receiveShadow = true;
        this.waterMeshes.push(m);
        this.courseGroup.add(m);
        continue;
      }
      // (rivers mapped as outlines: outline points at most 4 m apart, so the surface can follow the
      // slope and the flow can change along it)
      const poly = wb.levelAt ? densify(wb.poly, 4) : wb.poly;
      const shape = new THREE.Shape(poly.map(([x, z]) => new THREE.Vector2(-x, -z)));
      const g = new THREE.ShapeGeometry(shape);
      g.rotateX(-Math.PI / 2);
      // World-space UVs so the ripples have a consistent scale
      const p = g.attributes.position, uv = g.attributes.uv;
      for (let i = 0; i < p.count; i++) uv.setXY(i, p.getX(i) / 14, p.getZ(i) / 14);
      if (wb.levelAt) {
        // Rivers slope: each point of the surface at the level there, flowing downhill along the
        // channel, faster where it's steeper
        const flowAt = streamFlow(wb);
        const flow = new Float32Array(p.count * 3);
        for (let i = 0; i < p.count; i++) {
          const x = -p.getX(i), z = p.getZ(i);
          p.setY(i, wb.levelAt(x, z) - wb.level);
          const [tx, tz, v] = flowAt(x, z);
          flow[i * 3] = -tx; flow[i * 3 + 1] = tz; flow[i * 3 + 2] = v; // (in UV terms: u runs along -x)
        }
        g.setAttribute('flow', new THREE.BufferAttribute(flow, 3));
      }
      const m = new THREE.Mesh(g, wb.levelAt ? this.flowMat : this.waterMat);
      m.userData.flowing = !!wb.levelAt;
      m.position.y = wb.level;
      m.receiveShadow = true;
      this.waterMeshes.push(m);
      this.courseGroup.add(m);
    }
  }

  // ---- water edges: a muddy bank at the waterline and taller grass along the shore. Ponds differ:
  //   natural    - muddy bank, tall grass, reed beds in places, the odd rock
  //   manicured  - mown right down to a thin wet edge; no mud strip, tall grass or reeds
  //   reedy      - reed beds round most of the shore
  //   rocky      - lined with rocks, little mud

  // Red: how close each spot is to water, over the course area: 1 in the water, 0.5 at the
  // shoreline, falling to ~0 two metres out (a blurred copy of the water shapes). The terrain
  // shader draws the muddy bank from it; `inside` (unblurred) says what is water when placing
  // the bank grass. Green: the greens plus a 2 m ring around them, where the collar is mown
  // plain (no fairway stripes running into it).
  buildShore(course) {
    // Each pond gets its own kind of bank, the same every time (from where it is)
    for (const wb of course.waterBodies || []) {
      if (wb.style) continue;
      let cx = 0, cz = 0;
      for (const [x, z] of wb.poly) { cx += x; cz += z; }
      const n = hash01(cx / wb.poly.length + 0.37, cz / wb.poly.length - 0.71);
      wb.style = n < 0.35 ? 'natural' : n < 0.65 ? 'manicured' : n < 0.82 ? 'reedy' : 'rocky';
    }
    const { minX, maxX, minZ, maxZ } = course.bounds;
    const res = Math.max(0.4, Math.max(maxX - minX, maxZ - minZ) / 4096);
    const w = Math.ceil((maxX - minX) / res), h = Math.ceil((maxZ - minZ) / res);
    const sharp = document.createElement('canvas'); sharp.width = w; sharp.height = h;
    const sx = sharp.getContext('2d', { willReadFrequently: true });
    sx.fillStyle = '#000'; sx.fillRect(0, 0, w, h);
    const trace = (poly) => {
      sx.beginPath();
      poly.forEach(([x, z], i) => (i ? sx.lineTo((x - minX) / res, (maxZ - z) / res) : sx.moveTo((x - minX) / res, (maxZ - z) / res)));
      sx.closePath();
    };
    sx.globalCompositeOperation = 'lighter';
    sx.fillStyle = '#f00';
    for (const wb of course.waterBodies || []) { trace(wb.poly); sx.fill(); }
    // Blue: bunkers. Blurred like the rest, their edge is wherever the blue crosses one half: a
    // smooth curve at any distance (the surface mask's half-metre squares showed as little bumps)
    const bunkers = (course.features || []).filter((f) => f.surface === 'sand' && !f.inner);
    sx.fillStyle = '#00f';
    for (const f of bunkers) { trace(f.poly); sx.fill(); }
    sx.fillStyle = sx.strokeStyle = '#0f0'; sx.lineWidth = 4 / res; sx.lineJoin = 'round';
    const greens = (course.features || []).filter((f) => f.surface === 'green' && !f.inner);
    for (const f of greens) { trace(f.poly); sx.fill(); sx.stroke(); }
    // (generated greens have no outline: mark their green and fringe around each green centre)
    if (!greens.length) for (const hole of course.holes) for (let dz = -40; dz <= 40; dz++) for (let dx = -40; dx <= 40; dx++) {
      const x = hole.greenCenter.x + dx, z = hole.greenCenter.z + dz, s = course.surfaceAt(x, z);
      if (s === 'green' || s === 'fringe') sx.fillRect((x - 0.5 - minX) / res, (maxZ - z - 0.5) / res, 1 / res, 1 / res);
    }
    sx.globalCompositeOperation = 'source-over';
    const canvas = document.createElement('canvas'); canvas.width = w; canvas.height = h;
    const cx = canvas.getContext('2d');
    cx.filter = `blur(${(1.4 / res).toFixed(2)}px)`;
    cx.drawImage(sharp, 0, 0);
    return { canvas, inside: sx.getImageData(0, 0, w, h).data, w, h, res, minX, maxZ, hasSand: bunkers.length > 0 };
  }

  // Taller, unmown grass along the shore (as on most golf course ponds): clumps from the waterline
  // out to ~2.5 m, patchy along the bank, and never on fairways, greens, tees, sand or paths
  // The shorelines, for placing things along them: shoreSpot() picks a random point on one, with
  // the direction pointing away from the water, its water body and that water's level there
  shoreTools(course, shore) {
    const isWater = (x, z) => {
      const i = Math.floor((x - shore.minX) / shore.res), j = Math.floor((shore.maxZ - z) / shore.res);
      return i >= 0 && j >= 0 && i < shore.w && j < shore.h && shore.inside[(j * shore.w + i) * 4] > 127;
    };
    const edges = [];
    let perimeter = 0;
    for (const wb of course.waterBodies || []) for (let k = 0; k < wb.poly.length; k++) {
      const a = wb.poly[k], b = wb.poly[(k + 1) % wb.poly.length], len = Math.hypot(b[0] - a[0], b[1] - a[1]);
      if (len > 0.01) { edges.push({ a, b, len, at: perimeter, wb }); perimeter += len; }
    }
    const shoreSpot = () => {
      const s = Math.random() * perimeter;
      let lo = 0, hi = edges.length - 1;
      while (lo < hi) { const mid = (lo + hi + 1) >> 1; if (edges[mid].at <= s) lo = mid; else hi = mid - 1; }
      const { a, b, len, at, wb } = edges[lo], t = (s - at) / len;
      const x = a[0] + (b[0] - a[0]) * t, z = a[1] + (b[1] - a[1]) * t;
      let nx = -(b[1] - a[1]) / len, nz = (b[0] - a[0]) / len;
      if (isWater(x + nx * 0.8, z + nz * 0.8)) { nx = -nx; nz = -nz; }
      return { x, z, nx, nz, tx: (b[0] - a[0]) / len, tz: (b[1] - a[1]) / len, wb, level: wb.levelAt ? wb.levelAt(x, z) : wb.level };
    };
    return { isWater, perimeter, shoreSpot };
  }

  addBankGrass(course, shore) {
    const { isWater, perimeter, shoreSpot } = this.shoreTools(course, shore);
    if (!perimeter) return;
    const want = Math.min(Math.round(perimeter / 0.15), 60000) * (this.quality?.native ?? 1) | 0;
    if (want < 1) return;
    const geo = tuftGeometry({ blades: 9, spread: 0.3, lean: [0.1, 0.4], height: [0.6, 1.05], width: [0.035, 0.06], dry: 0.35 });
    const mat = grassMaterial({ sway: 0.03 });
    this.applyEnvIntensity(mat);
    const mesh = grassMesh(geo, mat, want);
    const m = new THREE.Matrix4(), q = new THREE.Quaternion(), sc = new THREE.Vector3(), p = new THREE.Vector3(), c = new THREE.Color();
    const up = new THREE.Vector3(0, 1, 0);
    const MOWN = new Set(['fairway', 'firstcut', 'fringe', 'green', 'tee', 'sand', 'path']);
    let n = 0;
    for (let tries = 0; tries < want * 3 && n < want; tries++) {
      // A random spot along the shoreline: thick in some stretches, sparse or bare in others
      const { x: x0, z: z0, nx, nz, tx, tz, wb } = shoreSpot();
      // (none on mown-to-the-edge ponds, less where rocks line the bank)
      if (wb.style === 'manicured' || (wb.style === 'rocky' && Math.random() < 0.6)) continue;
      if (noise(x0 * 0.06 + 31, z0 * 0.06) < -0.35) continue;
      // Step out from the water
      const d = 0.15 + Math.random() ** 1.6 * 2.4, along = (Math.random() - 0.5) * 0.3;
      const x = x0 + nx * d + tx * along, z = z0 + nz * d + tz * along;
      if (isWater(x, z) || MOWN.has(this.fastSurface(x, z, true)) || this.nearSand(x, z)) continue;
      const y = course.heightAt(x, z);
      if (y === null) continue;
      // Tallest a little back from the water, shorter right at the edge and towards the grass
      const hgt = (0.28 + Math.random() * 0.3) * (d < 0.5 ? 0.7 : d > 1.8 ? 0.75 : 1);
      q.setFromAxisAngle(up, Math.random() * 6.28);
      m.compose(p.copy(T(x, y - 0.02, z)), q, sc.set(hgt * (1 + Math.random() * 0.6), hgt, hgt * (1 + Math.random() * 0.6)));
      mesh.setMatrixAt(n, m);
      // Summer green with some straw-coloured clumps
      const dry = Math.random() ** 2, v = 0.85 + Math.random() * 0.3;
      mesh.setColorAt(n, c.setRGB((0.3 + 0.3 * dry) * v, (0.4 + 0.15 * dry) * v, (0.15 + 0.12 * dry) * v, THREE.SRGBColorSpace));
      n++;
    }
    mesh.count = n;
    mesh.instanceMatrix.needsUpdate = true;
    mesh.instanceColor.needsUpdate = true;
    this.courseGroup.add(mesh);
    this.bankClumps = n;
  }

  // Reeds and cattails in the shallows, and rocks along the waterline, each in their own stretches
  // of bank. Reeds stay off shores beside fairways, greens and tees (they'd sit in the line of play).
  addReedsAndRocks(course, shore) {
    const { isWater, perimeter, shoreSpot } = this.shoreTools(course, shore);
    if (!perimeter) return;
    const q = new THREE.Quaternion(), e = new THREE.Euler(), sc = new THREE.Vector3(), p = new THREE.Vector3(), m = new THREE.Matrix4(), c = new THREE.Color();
    const up = new THREE.Vector3(0, 1, 0);
    const amount = this.quality?.native ?? 1;
    const MOWN = new Set(['fairway', 'green', 'fringe', 'tee', 'path']);
    const nearPlay = (x, z, nx, nz) => [1.5, 3.5, 6].some((d) => MOWN.has(this.fastSurface(x + nx * d, z + nz * d, true)));
    // Where the ground meets the plant or rock: on the bank, or on the bed in the shallows (but
    // never more than half a metre down, wherever the terrain under the water lies)
    const baseY = (x, z, level) => Math.max(course.heightAt(x, z) ?? level, level - 0.5);

    // ---- reeds: two kinds of clump, plain and with cattails
    const want = Math.min(Math.round(perimeter * 1.4), 12000) * amount | 0;
    const kinds = [reedGeometry({ blades: 12 }), reedGeometry({ blades: 10, cattails: 3 })].map((geo) => {
      const mat = grassMaterial({ sway: 0.035 });
      this.applyEnvIntensity(mat);
      return { mesh: grassMesh(geo, mat, Math.ceil(want / 2)), n: 0 };
    });
    for (let tries = 0; tries < want * 4 && kinds.some((k) => k.n < k.mesh.instanceMatrix.count); tries++) {
      const s = shoreSpot();
      // Reed beds, not a continuous ring: in patches on natural ponds, round most of a reedy one,
      // rarely among rocks, never on a mown one
      const reedLimit = { natural: 0.2, reedy: -0.45, rocky: 0.55 }[s.wb.style];
      if (reedLimit === undefined || noise(s.x * 0.035 + 7, s.z * 0.035 - 3) < reedLimit) continue;
      if (nearPlay(s.x, s.z, s.nx, s.nz)) continue;
      // Mostly just inside the water, some right at the edge (in a stream, only along its banks:
      // the current keeps the channel clear)
      const d = -0.1 - Math.random() ** 1.3 * (s.wb.levelAt ? 0.45 : 1.6), along = (Math.random() - 0.5) * 0.6;
      const x = s.x + s.nx * d + s.tx * along, z = s.z + s.nz * d + s.tz * along;
      const k = kinds[Math.random() < 0.35 ? 1 : 0];
      if (k.n >= k.mesh.instanceMatrix.count) continue;
      const y = baseY(x, z, s.level);
      // Standing 0.9-1.6 m clear of the water (plus whatever is below the surface)
      const hgt = 0.9 + Math.random() * 0.7 + Math.max(0, s.level - y);
      q.setFromAxisAngle(up, Math.random() * 6.28);
      m.compose(p.copy(T(x, y - 0.05, z)), q, sc.set(0.9 + Math.random() * 0.5, hgt, 0.9 + Math.random() * 0.5));
      k.mesh.setMatrixAt(k.n, m);
      const v = 0.8 + Math.random() * 0.35;
      k.mesh.setColorAt(k.n++, c.setRGB(v, v, v * (0.9 + Math.random() * 0.15)));
    }
    for (const k of kinds) {
      k.mesh.count = k.n;
      k.mesh.instanceMatrix.needsUpdate = true; k.mesh.instanceColor.needsUpdate = true;
      if (k.n) this.courseGroup.add(k.mesh);
    }
    this.reedClumps = kinds[0].n + kinds[1].n;

    // ---- rocks: lumpy, part-buried stones along the waterline in rocky stretches, and the odd
    // stone elsewhere on the bank
    const rocks = Math.min(Math.round(perimeter * 0.5), 6000) * amount | 0;
    const mesh = new THREE.InstancedMesh(rockGeometry(), rockMaterial(), rocks);
    mesh.instanceColor = new THREE.InstancedBufferAttribute(new Float32Array(rocks * 3), 3);
    mesh.castShadow = mesh.receiveShadow = true;
    this.applyEnvIntensity(mesh.material);
    let n = 0;
    for (let tries = 0; tries < rocks * 4 && n < rocks; tries++) {
      const s = shoreSpot();
      // Rock-lined ponds all the way round; on natural ones in rocky stretches; elsewhere the odd one
      const rocky = s.wb.style === 'rocky' || (s.wb.style === 'natural' && noise(s.x * 0.03 - 11, s.z * 0.03 + 5) > 0.3);
      if (!rocky && Math.random() > (s.wb.style === 'manicured' ? 0.01 : 0.04)) continue;
      const d = (Math.random() - 0.55) * 1.4, along = (Math.random() - 0.5) * 0.8;
      const x = s.x + s.nx * d + s.tx * along, z = s.z + s.nz * d + s.tz * along;
      if (!isWater(x, z) && ['sand', 'path', 'green'].includes(this.fastSurface(x, z, true))) continue;
      const size = 0.25 + Math.random() ** 1.5 * (rocky ? 0.65 : 0.35);
      // On the bank: sunk about a third into the ground. In the water: breaking the surface.
      // (in a stream they stand higher: the current keeps them washed clean and wet)
      const ground = course.heightAt(x, z) ?? s.level, wet = isWater(x, z), stream = wet && !!s.wb.levelAt;
      const y = wet ? Math.max(ground - size * 0.35, s.level - size * (stream ? 0.08 : 0.3)) : ground - size * 0.35;
      e.set((Math.random() - 0.5) * 0.6, Math.random() * 6.28, (Math.random() - 0.5) * 0.6);
      q.setFromEuler(e);
      m.compose(p.copy(T(x, y, z)), q, sc.set(size * (0.9 + Math.random() * 0.6), size * (0.55 + Math.random() * 0.35), size * (0.9 + Math.random() * 0.6)));
      mesh.setMatrixAt(n, m);
      // Grey to brownish grey; darker where wet at the waterline
      const v = (0.8 + Math.random() * 0.35) * (stream ? 0.55 : wet ? 0.85 : 1), warm = Math.random() * 0.1;
      mesh.setColorAt(n++, c.setRGB((0.7 + warm) * v, (0.67 + warm * 0.5) * v, 0.62 * v, THREE.SRGBColorSpace));
    }
    mesh.count = n;
    mesh.instanceMatrix.needsUpdate = true; mesh.instanceColor.needsUpdate = true;
    if (n) this.courseGroup.add(mesh);
    this.shoreRocks = n;
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

  // Trees the aerial photo shows that nothing has placed yet (see treesFromPhoto): in the rough and
  // beyond, clear of mown grass, sand, water and paths
  addPhotoTrees(course, canopy) {
    const ROUGHISH = new Set(['rough', 'deeprough', 'native', 'oob']);
    const allowed = (x, z) => {
      if (course.heightAt(x, z) === null) return false;
      const s = this.fastSurface(x, z);
      return (s === null || ROUGHISH.has(s)) && !this.nearMown(x, z, 2) && !this.nearPath(x, z) && !this.nearSand(x, z, 1.5);
    };
    const heightAt = canopy ? (x, z) => {
      const i = Math.floor((x - canopy.minX) / canopy.res), j = Math.floor((canopy.maxZ - z) / canopy.res);
      return i >= 0 && j >= 0 && i < canopy.w && j < canopy.h ? canopy.heights[j * canopy.w + i] : 0;
    } : null;
    const found = treesFromPhoto(this.photoRaster, { allowed, existing: this.pendingTrees, heightAt });
    assignSpecies(found, this.photoAt());
    this.pendingTrees.push(...found);
    this.photoTrees = found.length;
    this.photoTreeList = found.map((t) => [t.x, t.z, t.h, t.r]);
  }

  // Trees mapped in OpenStreetMap (single trees and tree rows), merged with the ones found above
  // (and the ones added in the course editor: single trees and tree areas)
  addOsmTrees(course, canopy) {
    this.osmTrees = null;
    if (!course.trees?.length && !course.treeRows?.length && !course.userTrees?.length && !course.treeAreas?.length) return;
    const trees = (this.pendingTrees ||= []);
    // Canopy height near a point: the tallest reading within 2 m
    const heightAt = canopy ? (x, z) => {
      let top = 0;
      for (let dj = -2; dj <= 2; dj++) for (let di = -2; di <= 2; di++) {
        const i = Math.floor((x - canopy.minX) / canopy.res) + di, j = Math.floor((canopy.maxZ - z) / canopy.res) + dj;
        if (i >= 0 && j >= 0 && i < canopy.w && j < canopy.h) top = Math.max(top, canopy.heights[j * canopy.w + i]);
      }
      return top;
    } : null;
    // Not on greens, tees, bunkers, water or cart paths (or off the terrain)
    const NO = new Set(['green', 'fringe', 'tee', 'sand', 'water', 'path']);
    const allowed = (x, z) => course.heightAt(x, z) !== null && !NO.has(this.fastSurface(x, z)) && !NO.has(course.surfaceAt(x, z));
    const { added, matched } = addMappedTrees(trees, course.trees, course.treeRows, { heightAt: heightAt || undefined, allowed });
    // (tree areas also keep off fairways; single trees go wherever the player put them)
    const areaOk = (x, z) => allowed(x, z) && !['fairway', 'firstcut'].includes(this.fastSurface(x, z)) && course.surfaceAt(x, z) !== 'fairway';
    const user = addTreeAreas(trees, course.userTrees, course.treeAreas, { heightAt: heightAt || undefined, allowed: areaOk });
    if (added || user) assignSpecies(trees, this.photoAt());
    this.osmTrees = { added, matched, user };
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
    const o = this.osmTrees, extra = [o?.added && `${o.added.toLocaleString()} from OpenStreetMap`, this.photoTrees && `${this.photoTrees.toLocaleString()} seen in the aerial photo`, o?.user && `${o.user.toLocaleString()} added in the editor`].filter(Boolean);
    const osm = extra.length ? ` (${extra.join(', ')})` : '';
    await progress('trees-place', 'Planting trees', 0, 0, `${trees.length.toLocaleString()} trees${osm}`);
    this.forest.build(trees, (x, z) => course.heightAt(x, z), (m) => this.applyEnvIntensity(m));
    // (undergrowth along the edges of dense woods: in the rough, clear of paths and mown grass)
    this.forest.buildShrubs((x, z) => course.heightAt(x, z), (x, z) => ROUGH.has(this.fastSurface(x, z)) && !this.nearPath(x, z, 1.5) && !this.nearMown(x, z, 2) && !this.nearSand(x, z, 1.5), (m) => this.applyEnvIntensity(m));
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

  // Grass stays out of bunkers: the 1 m surface lookup is coarser than the drawn sand edge, so
  // keep a margin (otherwise stray tufts poke up just inside the sand)
  nearSand(x, z, r = 1.2) {
    for (const [dx, dz] of [[0, 0], [r, 0], [-r, 0], [0, r], [0, -r], [r * 0.7, r * 0.7], [-r * 0.7, r * 0.7], [r * 0.7, -r * 0.7], [-r * 0.7, -r * 0.7]]) {
      if (this.fastSurface(x + dx, z + dz, true) === 'sand') return true;
    }
    return false;
  }

  // Decoration (grass tufts, heather) stays off cart paths with a margin either side
  nearPath(x, z, r = 1.2) {
    for (const [dx, dz] of [[0, 0], [r, 0], [-r, 0], [0, r], [0, -r]]) if (this.fastSurface(x + dx, z + dz) === 'path') return true;
    return false;
  }

  // dry: what's there apart from water and its 1 m margin (for things that grow on the bank)
  fastSurface(x, z, dry = false) {
    const L = this.lookup;
    if (!L) return this.course.surfaceAt(x, z);
    const i = Math.floor(x - L.minX), j = Math.floor(L.maxZ - z);
    if (i < 0 || j < 0 || i >= L.w || j >= L.h) return null;
    const o = (j * L.w + i) * 4, r = L.data[o], g = L.data[o + 1], b = L.data[o + 2];
    if (!dry && L.water[o] > 100) return 'water';
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
    grassWind.value = windMps;
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
    // Two dots at each grid crossing on the green, one on each of its lines (so they never leave
    // the grid): one shows the slope along the putt, the other the slope across it. Each runs
    // downhill along its line to the next crossing, then starts again, faster the steeper that
    // way is; where it's flat that way (under 0.3%), or the line ends at the edge of the green,
    // it stays still on the crossing.
    for (let i = -steps; i <= steps; i++) for (let j = -steps; j <= steps; j++) {
      const x = cx + fx * i * spacing + rx * j * spacing, z = cz + fz * i * spacing + rz * j * spacing;
      if (!onGreen(x, z)) continue;
      const sl = slopeAt(x, z), l = Math.hypot(sl.dx, sl.dz) || 1;
      for (const [ux, uz] of [[fx, fz], [rx, rz]]) {
        const part = (sl.dx * ux + sl.dz * uz) / l, pct = sl.pct * Math.abs(part);
        const dx = ux * Math.sign(part), dz = uz * Math.sign(part);
        const moves = pct >= 0.3 && onGreen(x + dx * spacing, z + dz * spacing);
        dots.push({ x, z, dx, dz, pct: moves ? pct : 0, phase: Math.random() });
      }
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
    // (not a Sprite's geometry: three.js shares one between every Sprite - the far trees, the pin
    // marker, the aim label - and disposing it breaks them all: every frame after fails, frozen)
    this.puttGrid.group.traverse((o) => { if (!o.isSprite) o.geometry?.dispose(); o.material?.dispose(); });
    this.puttGrid = null;
  }

  updatePuttGrid(dt) {
    const pg = this.puttGrid;
    if (!pg) return;
    const arr = pg.dotPos.array;
    pg.dots.forEach((d, i) => {
      // Faster on steeper ground (a square every ~3 s at 0.5% slope, about two a second at 4%);
      // each dot slides one grid square along its line, then repeats
      if (d.pct) d.phase = (d.phase + dt * (0.08 + d.pct * 0.5)) % 1;
      const travel = d.pct ? d.phase * pg.spacing : 0;
      const x = d.x + d.dx * travel, z = d.z + d.dz * travel;
      const y = this.course.heightAt(x, z) ?? 0;
      arr[i * 3] = -x; arr[i * 3 + 1] = y + 0.02; arr[i * 3 + 2] = z;
    });
    pg.dotPos.needsUpdate = true;
  }

  // ---- tall unmown grass ("native" areas detected in the photo)
  // Four kinds of clump (see nativeClumpGeometry), mixed in drifts: which kinds grow where, and
  // how green or straw-coloured they are, change gently across an area as in a real meadow. A
  // lighter layer covers every area; near the ball (or the free camera) a dense layer fills in,
  // thinning out smoothly so its edge never shows.

  // A clump at (x, z): its kind, height and colour (col: the photo's colour there)
  nativeClump(x, z, col, c) {
    const drift = noise(x * 0.045 + 3, z * 0.045 - 7) * 0.5 + 0.5, flat = noise(x * 0.12 - 5, z * 0.12 + 2) * 0.5 + 0.5;
    const w = [1, 0.25 + drift * 0.7, 0.2 + (1 - drift) * 0.6, 0.05 + flat * flat * 0.6];
    let r = Math.random() * (w[0] + w[1] + w[2] + w[3]), kind = 0;
    while (kind < 3 && (r -= w[kind]) > 0) kind++;
    // (taller and shorter stands across an area)
    const H = [[0.55, 1.0], [0.75, 1.2], [0.45, 0.75], [0.4, 0.65]][kind];
    const h = (H[0] + Math.random() * (H[1] - H[0])) * (0.85 + 0.3 * (noise(x * 0.06 - 9, z * 0.06 + 4) * 0.5 + 0.5));
    // Greener in some drifts, straw-coloured in others, with smaller patches within them, the odd
    // clump darker (shaded, or still green at the base), each a little different
    const straw = Math.min(1, Math.max(0, (noise(x * 0.03 + 11, z * 0.03 - 4) * 0.5 + 0.5) * 0.75 + (noise(x * 0.14 + 2, z * 0.14 - 8) * 0.5 + 0.5) * 0.4 - 0.08));
    const v = (0.88 + Math.random() * 0.24) * (Math.random() < 0.12 ? 0.75 : 1);
    const tint = [0.74 + straw * 0.5, 0.97 + straw * 0.06, 0.6 + straw * 0.18];
    c.setRGB(col[0] * tint[0] * v, col[1] * tint[1] * v, col[2] * tint[2] * v * 0.92, THREE.SRGBColorSpace);
    return { kind, h };
  }

  nativeColour(x, z) {
    const S = this.nativeSample;
    if (S) {
      const i = Math.floor((x - S.minX) / S.res), j = Math.floor((S.maxZ - z) / S.res), o = (j * S.w + i) * 4;
      if (i >= 0 && j >= 0 && i < S.w && o < S.data.length) return [S.data[o] / 255, S.data[o + 1] / 255, S.data[o + 2] / 255];
    }
    return [0.62, 0.55, 0.36];
  }

  // (cells: only ones the course itself counts as native - checked once per cell, as that check
  // is slow - and then the quick per-metre look-up for paths, water and mapped features)
  nativeSpot(x, z) {
    return this.fastSurface(x, z) === 'native' && !this.nearPath(x, z, 1.5) && !this.nearSand(x, z, 1.5);
  }

  addNativeGrass(course, sat) {
    this.nativeTiles = null; this.nativeNear = null; // (their meshes go with the rest of the course)
    const o = sat?.overlay;
    if (!o) return;
    const cells = [];
    for (let k = 0; k < o.codes.length; k += 1) {
      if (o.codes[k] !== 3) continue;
      const x = o.minX + ((k % o.w) + 0.5) * o.res, z = o.maxZ - (Math.floor(k / o.w) + 0.5) * o.res;
      if (course.surfaceAt(x, z) === 'native') cells.push(k);
    }
    if (!cells.length) return;
    this.nativeSample = sat.sample;
    // About one clump per 1.5 m², capped so huge native areas stay fast
    const want = Math.min(110000, Math.round((cells.length * o.res * o.res) / 1.5)) * (this.quality?.native ?? 1) | 0;
    const m = new THREE.Matrix4(), q = new THREE.Quaternion(), sc = new THREE.Vector3(), p = new THREE.Vector3(), c = new THREE.Color();
    const up = new THREE.Vector3(0, 1, 0);
    // (in 64 m tiles, so the parts behind and beside the camera are skipped)
    const FAR = 64, farTiles = new Map();
    for (let tries = 0, n = 0; tries < want * 3 && n < want; tries++) {
      const k = cells[Math.floor(Math.random() * cells.length)];
      const x = o.minX + ((k % o.w) + Math.random()) * o.res, z = o.maxZ - (Math.floor(k / o.w) + Math.random()) * o.res;
      if (!this.nativeSpot(x, z)) continue;
      const y = course.heightAt(x, z);
      if (y === null) continue;
      const { kind, h } = this.nativeClump(x, z, this.nativeColour(x, z), c);
      const wide = 1.1 + Math.random() * 0.7;
      q.setFromAxisAngle(up, Math.random() * 6.28);
      m.compose(p.copy(T(x, y - 0.03, z)), q, sc.set(wide, h, wide * (0.85 + Math.random() * 0.3)));
      const fk = `${Math.floor(x / FAR)},${Math.floor(z / FAR)}`;
      if (!farTiles.has(fk)) farTiles.set(fk, NATIVE_KINDS.map(() => []));
      farTiles.get(fk)[kind].push(m.clone(), c.clone());
      n++;
    }
    this.nativeMat = grassMaterial({ sway: 0.035 }); // (tall clumps sway in the wind)
    this.nativeMat.envMapIntensity = 0.5;
    this.nativeClumps = 0;
    const liteGeos = NATIVE_KINDS.map((kind) => nativeClumpGeometry(kind, true));
    for (const lists of farTiles.values()) NATIVE_KINDS.forEach((kind, i) => {
      const list = lists[i], count = list.length / 2;
      if (!count) return;
      const mesh = grassMesh(liteGeos[i], this.nativeMat, count);
      for (let j = 0; j < count; j++) { mesh.setMatrixAt(j, list[j * 2]); mesh.setColorAt(j, list[j * 2 + 1]); }
      mesh.instanceMatrix.needsUpdate = true; mesh.instanceColor.needsUpdate = true;
      mesh.computeBoundingSphere();
      mesh.layers.set(1);
      this.courseGroup.add(mesh);
      this.nativeClumps += count;
    });
    // The dense layer near the camera: about three full clumps a square metre over every area,
    // worked out once here and kept in 24 m tiles. The graphics card fades them by distance from
    // the camera every frame (nativeNearMaterial): every clump out to 16 m, then fewer and fewer,
    // each growing up out of the ground as it comes in, none past 36 m. So the dense patch moves
    // smoothly with you, wherever you go. Only the tiles in reach are drawn (updateNativeTiles).
    const area = cells.length * o.res * o.res;
    const N = Math.min(500000, Math.round(area * 3.5 * (this.quality?.native ?? 1)));
    const TILE = 24, tiles = new Map();
    for (let tries = 0, n = 0; tries < N * 2 && n < N; tries++) {
      const k = cells[Math.floor(Math.random() * cells.length)];
      const x = o.minX + ((k % o.w) + Math.random()) * o.res, z = o.maxZ - (Math.floor(k / o.w) + Math.random()) * o.res;
      if (!this.nativeSpot(x, z)) continue;
      const y = course.heightAt(x, z);
      if (y === null) continue;
      const { kind, h } = this.nativeClump(x, z, this.nativeColour(x, z), c);
      const tk = `${Math.floor(x / TILE)},${Math.floor(z / TILE)}`;
      let tile = tiles.get(tk);
      if (!tile) tiles.set(tk, (tile = { x: (Math.floor(x / TILE) + 0.5) * TILE, z: (Math.floor(z / TILE) + 0.5) * TILE, kinds: [[], [], [], []] }));
      tile.kinds[kind].push(x, y, z, h, c.r, c.g, c.b);
      n++;
    }
    // Each tile keeps its clumps' data, per kind; four meshes (one per kind) draw the tiles in
    // reach, their data copied in whenever the tiles in reach change (updateNativeTiles). New
    // objects coming into view stall the renderer while it sets them up, so none ever do.
    this.nativeTiles = [];
    const most = [0, 0, 0, 0];
    for (const tile of tiles.values()) {
      tile.data = tile.kinds.map((list, kind) => {
        const n = list.length / 7;
        const mats = new Float32Array(n * 16), cols = new Float32Array(n * 3), orig = new Float32Array(n * 4);
        for (let j = 0; j < n; j++) {
          const [x, y, z, h, r, gg, b] = list.slice(j * 7, j * 7 + 7), wide = 0.9 + Math.random() * 0.6;
          q.setFromAxisAngle(up, Math.random() * 6.28);
          m.compose(p.copy(T(x, y - 0.03, z)), q, sc.set(wide, h, wide * (0.85 + Math.random() * 0.3)));
          m.toArray(mats, j * 16);
          cols.set([r, gg, b], j * 3);
          orig.set([-x, y - 0.03, z, Math.random()], j * 4);
        }
        most[kind] = Math.max(most[kind], n);
        return { n, mats, cols, orig };
      });
      delete tile.kinds;
      this.nativeTiles.push(tile);
    }
    this.nativeNearMat = nativeNearMaterial();
    this.nativeNearMat.envMapIntensity = 0.5;
    // (room for every tile that can be in reach at once: a 24 m grid within 49 m of a point)
    const inReach = Math.ceil((Math.PI * (NATIVE_FADE.R + 17 + TILE) ** 2) / TILE ** 2);
    this.nativeNear = NATIVE_KINDS.map((kind, i) => {
      const cap = Math.max(1, Math.min(most[i] * inReach, 120000));
      const g = new THREE.BufferGeometry(), base = nativeClumpGeometry(kind);
      for (const [name, attr] of Object.entries(base.attributes)) g.setAttribute(name, attr);
      g.setAttribute('origin', new THREE.InstancedBufferAttribute(new Float32Array(cap * 4), 4));
      const mesh = grassMesh(g, this.nativeNearMat, cap);
      mesh.count = 0; mesh.frustumCulled = false;
      for (const a of [mesh.instanceMatrix, mesh.instanceColor, g.attributes.origin]) a.setUsage(THREE.DynamicDrawUsage);
      mesh.layers.set(1);
      this.courseGroup.add(mesh);
      return mesh;
    });
    this.nativeInReach = '';
  }

  // The dense tall grass in reach of the camera (the rest would all be faded out anyway): when the
  // tiles in reach change, their clumps are copied into the four meshes
  updateNativeTiles() {
    const tiles = this.nativeTiles, meshes = this.nativeNear;
    if (!tiles?.length || !meshes) return;
    const cx = -this.camera.position.x, cz = this.camera.position.z, reach = NATIVE_FADE.R + 17; // (+ half a tile's diagonal)
    const near = [];
    for (let i = 0; i < tiles.length; i++) if (Math.hypot(tiles[i].x - cx, tiles[i].z - cz) < reach) near.push(i);
    const key = near.join(',');
    if (key === this.nativeInReach) return;
    this.nativeInReach = key;
    meshes.forEach((mesh, kind) => {
      const cap = mesh.instanceMatrix.count, origin = mesh.geometry.attributes.origin;
      let n = 0;
      for (const i of near) {
        const d = tiles[i].data[kind];
        if (!d.n || n + d.n > cap) continue;
        mesh.instanceMatrix.array.set(d.mats, n * 16); mesh.instanceColor.array.set(d.cols, n * 3); origin.array.set(d.orig, n * 4);
        n += d.n;
      }
      mesh.count = n;
      // (only the part in use goes to the graphics card)
      for (const [a, size] of [[mesh.instanceMatrix, 16], [mesh.instanceColor, 3], [origin, 4]]) {
        a.clearUpdateRanges(); a.addUpdateRange(0, Math.max(1, n) * size); a.needsUpdate = true;
      }
    });
  }

  // ---- close-up rough: a carpet of short grass right round the camera
  // One fixed set of tufts on a jittered grid 34 m square, laid out afresh round the camera every
  // frame by the graphics card: each tuft takes the copy of its square nearest the camera, so the
  // carpet follows you anywhere with nothing to work out as you move. Where each one stands, and
  // whether and how tall it grows there, come from a map of the course at 1 m (roughMap): the
  // ground height, how much rough grass (none on fairways, greens, tees, paths, sand or water;
  // thickest right beside mown grass, thinning out over 20 m; taller in deep rough) and the light
  // and shade in the photo. It fades out by 16 m, where the tufts round the ball and the ground's
  // own texture carry on.
  // How thick the rough is at d metres from mown grass: 1 right at the edge (out to 3 m), easing
  // down to 0 by 22 m
  roughEdge(d) { return 1 - Math.min(1, Math.max(0, (d - 3) / 19)); }

  addRoughCarpet(course) {
    const { minX, maxX, minZ, maxZ } = course.bounds;
    const w = Math.ceil(maxX - minX), h = Math.ceil(maxZ - minZ), data = new Uint8Array(w * h * 4);
    const AMOUNT = { rough: 150, oob: 130, deeprough: 235 }, S = this.imagery?.sample;
    const MOWN = new Set(['fairway', 'firstcut', 'fringe', 'green', 'tee']);
    // How far each metre is from mown grass (two-pass chamfer distance, in metres): the rough is
    // thickest right where the fairway ends and thins out over about 20 m
    const surfs = new Array(w * h), far = new Float32Array(w * h);
    for (let j = 0; j < h; j++) for (let i = 0; i < w; i++) {
      const k = j * w + i, surf = this.fastSurface(minX + i + 0.5, maxZ - j - 0.5);
      surfs[k] = surf; far[k] = MOWN.has(surf) ? 0 : 1e6;
    }
    for (let j = 0; j < h; j++) for (let i = 0; i < w; i++) {
      const k = j * w + i;
      if (i > 0) far[k] = Math.min(far[k], far[k - 1] + 1);
      if (j > 0) far[k] = Math.min(far[k], far[k - w] + 1);
      if (i > 0 && j > 0) far[k] = Math.min(far[k], far[k - w - 1] + 1.414);
      if (i < w - 1 && j > 0) far[k] = Math.min(far[k], far[k - w + 1] + 1.414);
    }
    for (let j = h - 1; j >= 0; j--) for (let i = w - 1; i >= 0; i--) {
      const k = j * w + i;
      if (i < w - 1) far[k] = Math.min(far[k], far[k + 1] + 1);
      if (j < h - 1) far[k] = Math.min(far[k], far[k + w] + 1);
      if (i < w - 1 && j < h - 1) far[k] = Math.min(far[k], far[k + w + 1] + 1.414);
      if (i > 0 && j < h - 1) far[k] = Math.min(far[k], far[k + w - 1] + 1.414);
    }
    for (let j = 0; j < h; j++) for (let i = 0; i < w; i++) {
      const x = minX + i + 0.5, z = maxZ - j - 0.5, o = (j * w + i) * 4, k = j * w + i;
      const y = course.heightAt(x, z) ?? this.hmin, v = Math.max(0, Math.min(65535, Math.round((y - this.hmin) * 100)));
      data[o] = v >> 8; data[o + 1] = v & 255;
      const surf = surfs[k], near = this.roughEdge(far[k]);
      // (none within 1.5 m of sand: read between metres, the map would otherwise put tufts in it)
      data[o + 2] = AMOUNT[surf] && !this.nearPath(x, z, 0.6) && !this.nearSand(x, z, 1.5) ? Math.min(255, Math.round(AMOUNT[surf] * (0.6 + 0.65 * near * near))) : 0;
      let shade = 1;
      if (S) {
        const si = Math.floor((x - S.minX) / S.res), sj = Math.floor((S.maxZ - z) / S.res), so = (sj * S.w + si) * 4;
        if (so >= 0 && so < S.data.length) shade = Math.max(0.75, Math.min(1.2, (0.2126 * S.data[so] + 0.7152 * S.data[so + 1] + 0.0722 * S.data[so + 2]) / 255 / 0.3));
      }
      data[o + 3] = Math.round(((shade - 0.75) / 0.45) * 255);
    }
    this.mownDist = { far, w, h, minX, maxZ };
    const map = new THREE.DataTexture(data, w, h, THREE.RGBAFormat, THREE.UnsignedByteType);
    map.magFilter = map.minFilter = THREE.NearestFilter; map.generateMipmaps = false; map.colorSpace = THREE.NoColorSpace;
    map.needsUpdate = true;
    const G = { high: 236, balanced: 200, performance: 136 }[this.quality?.name] || 200, SIDE = 34, cell = new Float32Array(G * G * 4);
    for (let k = 0; k < G * G; k++) {
      const gi = k % G, gj = Math.floor(k / G);
      cell.set([((gi + Math.random()) / G) * SIDE, ((gj + Math.random()) / G) * SIDE, Math.random(), Math.random()], k * 4);
    }
    const base = tuftGeometry({ blades: (this.quality?.blades ?? 8) + 2, spread: 0.5 });
    const g = new THREE.BufferGeometry();
    for (const [name, attr] of Object.entries(base.attributes)) g.setAttribute(name, attr);
    g.setAttribute('cell', new THREE.InstancedBufferAttribute(cell, 4));
    const mat = roughCarpetMaterial(map, { minX, maxZ, w, h, hmin: this.hmin, SIDE });
    this.applyEnvIntensity(mat);
    const mesh = new THREE.InstancedMesh(g, mat, G * G);
    mesh.frustumCulled = false; mesh.receiveShadow = true; mesh.layers.set(1);
    this.courseGroup.add(mesh);
  }

  // ---- grass tufts around the ball (close-up detail)

  // The new tufts are laid out on a second mesh a few milliseconds each frame (stepGrass), while
  // the ones already there stay on screen; when it's done the two swap. (All at once it takes
  // 30-40 ms, a visible hitch every few metres you move.) The very first lot, before there's
  // anything to show, is laid out at once.
  placeGrass(center, minMove = 8) {
    if (!this.course) return;
    if (this.grassCenter && Math.hypot(center.x - this.grassCenter.x, center.z - this.grassCenter.z) < minMove) return;
    this.grassCenter = { x: center.x, z: center.z };
    if (!this.grass) {
      const mat = grassMaterial();
      this.applyEnvIntensity(mat);
      const geo = tuftGeometry({ blades: this.quality?.blades ?? 8 }), count = this.quality?.grass ?? 20000;
      [this.grass, this.grassBack] = [0, 1].map(() => {
        const mesh = grassMesh(geo, mat, count);
        mesh.frustumCulled = false; mesh.count = 0;
        this.dynamic.add(mesh);
        return mesh;
      });
    }
    const first = this.grass.count === 0;
    this.grassJob = { center: { x: center.x, z: center.z }, k: 0, n: 0 };
    if (first) { this.stepGrass(Infinity); }
  }

  stepGrass(budgetMs = 3) {
    const job = this.grassJob, mesh = this.grassBack;
    if (!job || !mesh) return;
    const t0 = performance.now(), center = job.center;
    const m = new THREE.Matrix4(), q = new THREE.Quaternion(), sc = new THREE.Vector3(), p = new THREE.Vector3(), c = new THREE.Color();
    const up = new THREE.Vector3(0, 1, 0);
    const maxGrass = mesh.instanceMatrix.count, tries = maxGrass * 2.5;
    while (job.k < tries && job.n < maxGrass) {
      if ((job.k & 63) === 0 && performance.now() - t0 > budgetMs) return;
      job.k++;
      // Densest near the centre, thinning out smoothly to 42 m (no density step, which would
      // show as a ring), and none right on the ball: the patch around it has its own, shorter grass
      const r = Math.random() ** 0.8 * 42, a = Math.random() * Math.PI * 2;
      if (r < 0.3) continue;
      const x = center.x + Math.cos(a) * r, z = center.z + Math.sin(a) * r;
      // Thickest right beside the fairway, as the close-up carpet: away from it fewer tufts, and
      // shorter (checked first, as it's the quickest)
      const M = this.mownDist, mi = M ? Math.floor(x - M.minX) : -1, mj = M ? Math.floor(M.maxZ - z) : -1;
      const near = M && mi >= 0 && mj >= 0 && mi < M.w && mj < M.h ? this.roughEdge(M.far[mj * M.w + mi]) : 0.5;
      if (Math.random() > 0.45 + 0.55 * near) continue;
      const s = this.fastSurface(x, z);
      // Tufts only in the rough, never on mown grass (fairway, first cut, fringe, green, tee)
      if (!ROUGH.has(s)) continue;
      if (this.nearPath(x, z) || this.nearMown(x, z) || this.nearSand(x, z)) continue;
      const y = this.course.heightAt(x, z);
      if (y === null) continue;
      // Rough stands clearly taller than the cut grass around the fairways and greens
      // (shrinking into the ground over the last 12 m, so the edge of the area fades out)
      const fadeOut = Math.min(1, (42 - r) / 12);
      const h = ((s === 'deeprough' || s === 'native' ? 0.08 : 0.055) + Math.random() * (s === 'rough' || s === 'oob' ? 0.04 : 0.06)) * fadeOut * (0.85 + 0.9 * near);
      q.setFromAxisAngle(up, Math.random() * 6.28);
      m.compose(p.copy(T(x, y - 0.004, z)), q, sc.set(h * (1.2 + Math.random() * 0.8), h, h * (1.2 + Math.random() * 0.8)));
      mesh.setMatrixAt(job.n, m);
      mesh.setColorAt(job.n, this.grassColor(x, z, s, c));
      job.n++;
    }
    // Done: show the new lot in place of the old
    mesh.count = job.n;
    mesh.instanceMatrix.needsUpdate = true;
    if (mesh.instanceColor) mesh.instanceColor.needsUpdate = true;
    const old = this.grass;
    old.count = 0;
    this.grass = mesh; this.grassBack = old;
    this.grassJob = null;
  }

  // Grass colour: the dark green of the rough on the ground, lighter or darker with the light and shade
  // in the photo underneath. (Only the photo's brightness: its colour runs blue-grey, which made
  // the tufts read as a teal patch against the ground.)
  grassColor(x, z, s, c) {
    const S = this.imagery?.sample;
    let shade = 1;
    if (S) {
      const i = Math.floor((x - S.minX) / S.res), j = Math.floor((S.maxZ - z) / S.res), o = (j * S.w + i) * 4;
      if (o >= 0 && o < S.data.length) shade = Math.max(0.75, Math.min(1.2, (0.2126 * S.data[o] + 0.7152 * S.data[o + 1] + 0.0722 * S.data[o + 2]) / 255 / 0.3));
    }
    const tgt = s === 'native' ? [0.4, 0.62, 0.24] : [0.25, 0.33, 0.18]; // (the rough's dark, slightly blue green)
    const v = (0.85 + Math.random() * 0.3) * shade;
    return c.setRGB(tgt[0] * v, tgt[1] * v, tgt[2] * v, THREE.SRGBColorSpace);
  }

  // ---- the ball at rest: in the rough it sits down in the grass, with blades all around it

  settleBall(ball, lie) {
    let s = lie;
    if (s === 'rough') { const f = this.fastSurface(ball.x, ball.z); if (f === 'deeprough' || f === 'native') s = f; }
    // How far it sits down (m) depends on the grass, and from spot to spot: some lies sit up, some are buried
    const luck = hash01(ball.x, ball.z);
    const depth = { rough: 0.007, oob: 0.007, deeprough: 0.011, native: 0.013 }[s] || 0;
    this.ballSink = depth * (0.45 + luck * 1.1);
    this.placeLieGrass(ball, s, luck);
  }

  // A dense patch of short tufts out to a few metres from the ball, thickest right next to it, and a
  // ring of blades pressed up against it
  placeLieGrass(ball, s, luck) {
    const key = `${ball.x.toFixed(2)}|${ball.z.toFixed(2)}|${s}`;
    if (this.lieKey === key) return;
    this.lieKey = key;
    if (!this.lieGrass) {
      const mat = grassMaterial({ sway: 0.004 });
      this.applyEnvIntensity(mat);
      const geo = tuftGeometry({ blades: 6, spread: 0.35, lean: [0.2, 0.6], width: [0.06, 0.1] });
      this.lieGrass = grassMesh(geo, mat, this.quality?.lie ?? 6000);
      this.lieGrass.frustumCulled = false;
      this.dynamic.add(this.lieGrass);
    }
    const mesh = this.lieGrass;
    if (!ROUGH.has(s) || !this.course) { mesh.count = 0; return; }
    const m = new THREE.Matrix4(), q = new THREE.Quaternion(), sc = new THREE.Vector3(), p = new THREE.Vector3(), c = new THREE.Color();
    const up = new THREE.Vector3(0, 1, 0);
    const tall = (s === 'deeprough' || s === 'native' ? 0.065 : 0.045) * (0.9 + luck * 0.3);
    const max = mesh.instanceMatrix.count, nest = Math.min(30, max);
    let n = 0;
    for (let k = 0; k < max * 2 && n < max; k++) {
      const ring = n < nest;
      // The ring: short blades just outside the ball (radius 2.1 cm), so it still shows; the patch:
      // denser the closer to the ball
      const r = ring ? 0.03 + Math.random() * 0.05 : 0.08 + Math.random() ** 1.6 * 3;
      const a = Math.random() * Math.PI * 2;
      const x = ball.x + Math.cos(a) * r, z = ball.z + Math.sin(a) * r;
      if (!ring) {
        if (!ROUGH.has(this.fastSurface(x, z)) || this.nearPath(x, z, 0.6) || this.nearMown(x, z, 0.3) || this.nearSand(x, z, 0.9)) continue;
      }
      const y = this.course.heightAt(x, z);
      if (y === null) continue;
      // Fade the patch out towards its edge into the wider tufts around it
      const h = tall * (ring ? 0.45 + Math.random() * 0.4 : 0.65 + Math.random() * 0.6) * (r > 2 ? 1.6 - r * 0.3 : 1);
      q.setFromAxisAngle(up, Math.random() * 6.28);
      m.compose(p.copy(T(x, y - 0.003, z)), q, sc.set(h * (0.8 + Math.random() * 0.6), h, h * (0.8 + Math.random() * 0.6)));
      mesh.setMatrixAt(n, m);
      mesh.setColorAt(n, this.grassColor(x, z, s, c));
      n++;
    }
    mesh.count = n;
    mesh.instanceMatrix.needsUpdate = true;
    if (mesh.instanceColor) mesh.instanceColor.needsUpdate = true;
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
    const marker = new THREE.Sprite(new THREE.SpriteMaterial({ map: markerTex, sizeAttenuation: false, depthTest: false, transparent: true, fog: false }));
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
    // The tag is drawn in the top of a taller, otherwise clear picture centred 2.6 m up,
    // so it sits above the hole locator pin (also 2.6 m up, and the same size on screen at any
    // distance) instead of the pin covering the numbers when aiming at the flag. No fog on it:
    // far off, the haze washed it out to a pale blue box.
    this.aimLabelCanvas = document.createElement('canvas');
    this.aimLabelCanvas.width = 512; this.aimLabelCanvas.height = 320;
    this.aimLabelTex = new THREE.CanvasTexture(this.aimLabelCanvas);
    this.aimLabelTex.colorSpace = THREE.SRGBColorSpace; // (a dark tag, not a washed-out grey one)
    const label = new THREE.Sprite(new THREE.SpriteMaterial({ map: this.aimLabelTex, sizeAttenuation: false, depthTest: false, transparent: true, fog: false }));
    label.scale.set(0.3, 0.1867, 1);
    label.position.y = 2.6;
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
    ctx.clearRect(0, 0, 512, 320);
    ctx.fillStyle = 'rgba(10,16,12,0.85)';
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
    // (and how far it sits down in the grass, see settleBall)
    this.ballBaseY = p.y;
    this.ballSink = 0;
    this.ballSpin = { w: p.w || 0, air: !!p.air }; // (from the shot simulation, see spinBall)
    this.ball.position.copy(T(p.x, p.y + 0.12 * this.ball.scale.x, p.z));
    const g = this.course?.heightAt(p.x, p.z);
    this.ballShadow.visible = g != null;
    if (g != null) {
      this.ballShadow.position.copy(T(p.x, g + 0.015, p.z));
      const above = Math.max(0, p.y - g);
      this.ballShadow.material.opacity = 0.55 / (1 + above * 0.4);
    }
  }

  // Turn the ball as it really spins. In the air: backspin about the axis across its flight,
  // tilted by the side spin, at the simulated rate slowed down 10x (real backspin is 30-160 turns a
  // second, which a 60 Hz screen can only show as a flicker, often appearing to turn backwards);
  // faster spin still turns visibly faster. On the ground: rolling without slipping, from how far
  // it moved and its drawn size.
  spinBall(dt) {
    const b = this.ball.position, prev = this._ballPrev;
    this._ballPrev = b.clone();
    if (!prev || !this.ballSpin) return;
    const d = b.clone().sub(prev).setY(0), dist = d.length();
    const up = new THREE.Vector3(0, 1, 0);
    if (dist > 1e-5) this._ballDir = d.clone().divideScalar(dist);
    let axis = null, angle = 0;
    if (this.ballSpin.air && this.ballSpin.w > 0 && this._ballDir) {
      // (up x forward is the axis of rolling forward; backspin turns the other way)
      axis = up.clone().cross(this._ballDir).negate().applyAxisAngle(this._ballDir, -this.spinTilt);
      // (at most 70° a frame, so a slow frame can't make it look like it turns backwards)
      angle = Math.min(this.ballSpin.w * 0.1 * dt, 1.2);
    } else if (dist > 1e-5 && dist < 5) {
      axis = up.clone().cross(this._ballDir);
      angle = dist / (0.12 * this.ball.scale.x);
    }
    if (!axis || !angle) return;
    this.ball.quaternion.premultiply(new THREE.Quaternion().setFromAxisAngle(axis.normalize(), angle));
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

  frame() {
    const raw = this.clock.getDelta();
    const dt = Math.min(0.05, raw);
    // Game time follows the wall clock even if frames are slow, so shots play in real time.
    if (this.onFrame) this.onFrame(Math.min(raw, 0.5));
    if (this.mode === 'free') {
      // WASD / arrows move across the ground, Q/E down/up (Space up); Shift is faster
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
        if (this.freeKeys.has('e') || this.freeKeys.has('space')) mv.y += speed * 0.6;
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
    if (this.ballBaseY != null) this.ball.position.y = this.ballBaseY + 0.12 * ballScale - (this.ballSink || 0);
    this.ballShadow.scale.setScalar(ballScale);
    this.spinBall(dt);
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
    this.updateNativeTiles();
    this.stepGrass();
    carpetBall.value.copy(this.ball.position);
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

// Cart path in the surface mask: blue ≈ 0.35 together with green ≈ 0.16 (see buildSurfaceMask).
// How much path a spot is: the smaller of the two as a share of the full path code (bunkers are
// blue without the green, greens green without the blue, so neither counts). The mask is blurred,
// so this rises smoothly across a path's edge; the edge is drawn where it crosses one half,
// softened over about a screen pixel - a smooth line rather than the mask's half-metre steps.
const pathWeight = (sm, ws) => {
  const p = sm.b.div(0.353).min(sm.g.div(0.157)), aa = fwidth(p).max(0.02);
  return smoothstep(float(0.5).sub(aa), float(0.5).add(aa), p).mul(ws.oneMinus());
};

// Close-up ground textures (CC0, ambientCG: Grass005, Grass004, Ground080, Asphalt031, Ground094B,
// Rock058) and the three.js water normal map (MIT). Average colours (linear) let the shader use them as detail.
const GROUND_FILES = { lawn: 'lawn', rough: 'rough', sand: 'sand', asphalt: 'asphalt', mud: 'mud', rock: 'rock' };
const GROUND_MEAN = { lawn: [0.131, 0.23, 0.025], rough: [0.118, 0.152, 0.03], sand: [0.462, 0.342, 0.165], asphalt: [0.208, 0.208, 0.208], mud: [0.262, 0.201, 0.138], rock: [0.079, 0.091, 0.105] };
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
  for (const n of Object.keys(GROUND_FILES)) { groundTexture(n); if (!['mud', 'rock'].includes(n)) groundNormal(n); }
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

// Running water (streams and rivers): the ripples are carried along by the current. Each vertex has
// a flow attribute: the direction downstream in UV terms and the speed in m/s. The ripple
// texture slides that way in two layers half a cycle apart, each faded out as it jumps back (the
// usual flow-map trick), and fast water gets light streaks of foam.
function flowingWaterMaterial(env) {
  const m = new THREE.MeshPhysicalNodeMaterial({ roughness: 0.1, metalness: 0, ior: 1.33 });
  if (env) { m.envMap = env; m.envMapIntensity = 0.85; }
  const ripples = loadTexture('water_normal.jpg', false);
  const f = attribute('flow', 'vec3');
  const w = uv(), speed = f.z, fast = smoothstep(0.5, 2.2, speed);
  const vel = f.xy.mul(speed).div(14);           // UV units per second (UVs are metres / 14)
  const CYCLE = 1.5;
  const ph0 = fract(time.div(CYCLE)), ph1 = fract(time.div(CYCLE).add(0.5));
  const w0 = abs(ph0.mul(2).sub(1)).oneMinus();  // (each layer unseen as it jumps back)
  const layer = (ph, shift) => {
    const q = w.sub(vel.mul(ph.mul(CYCLE))).add(shift);
    const a = texture(ripples, q.mul(2.4)).xyz.mul(2).sub(1);
    const b = texture(ripples, q.mul(0.9).add(0.31)).xyz.mul(2).sub(1);
    return vec3(a.xy.add(b.xy.mul(0.6)), a.z.mul(b.z));
  };
  const l0 = layer(ph0, vec2(0, 0)), l1 = layer(ph1, vec2(0.43, 0.17));
  const n = normalize(mix(l1, l0, w0));
  m.normalNode = normalMap(n.mul(0.5).add(0.5), vec2(float(0.22).add(fast.mul(0.3))));
  const dist = distance(cameraPosition, positionWorld);
  // (a shallow stream shows a little more colour than a deep pond)
  const deep = mix(vec3(0.045, 0.085, 0.07), vec3(0.022, 0.05, 0.042), smoothstep(10, 120, dist));
  // Foam: where the ripple crests stack up in fast water
  const crest = mix(l1.x, l0.x, w0).add(mix(l1.y, l0.y, w0)).mul(0.5);
  const foam = smoothstep(0.25, 0.7, crest).mul(fast).mul(0.55);
  const facing = clamp(dot(normalView, positionViewDirection), 0, 1);
  const fresnel = float(0.04).add(float(0.96).mul(pow(facing.oneMinus(), 4)));
  m.colorNode = mix(deep.mul(fresnel.oneMinus()), vec3(0.62, 0.66, 0.64), foam);
  m.roughnessNode = mix(float(0.1), float(0.45), foam);
  return m;
}

// The dense tall grass near the camera (see addNativeGrass): each clump has an 'origin' (its base
// in the scene, and w a random number). Every clump is shown out to FULL metres from the camera,
// then fewer and fewer out to R; a clump whose number the falling density passes grows up out
// of the ground over a few metres rather than appearing at once, and shrinks back the same way.
const NATIVE_FADE = { FULL: 16, R: 36 };
function nativeNearMaterial() {
  const o = attribute('origin', 'vec4');
  return grassMaterial({
    sway: 0.035,
    place: (p) => {
      const d = distance(cameraPosition.xz, o.xz);
      const density = clamp(float(NATIVE_FADE.R).sub(d).div(NATIVE_FADE.R - NATIVE_FADE.FULL), 0, 1).pow(1.2);
      const grow = clamp(density.sub(o.w).div(0.12), 0, 1);
      return o.xyz.add(p.sub(o.xyz).mul(grow));
    },
  });
}

// The close-up rough carpet (see addRoughCarpet). map: per metre of the course, ground height
// (red and green: centimetres above the lowest point), how much rough grass (blue) and the photo's
// light and shade (alpha). It's read texel by texel (exactly, unfiltered) and blended by hand.
const carpetBall = uniform(new THREE.Vector3(0, -1e6, 0));
function roughCarpetMaterial(map, { minX, maxZ, w, h, hmin, SIDE }) {
  const cell = attribute('cell', 'vec4'), R = 16;
  const look = (cx, cz) => {
    const fx = cx.sub(minX).sub(0.5), fz = float(maxZ).sub(cz).sub(0.5);
    const i0 = floor(fx), j0 = floor(fz), u = fx.sub(i0), v = fz.sub(j0);
    const at = (di, dj) => texture(map, vec2(i0.add(di + 0.5).div(w), j0.add(dj + 0.5).div(h))).level(0);
    const a = at(0, 0), b = at(1, 0), c = at(0, 1), d = at(1, 1);
    const ht = (t) => t.r.mul(255 * 256).add(t.g.mul(255)).div(100).add(hmin);
    const lerp4 = (fa, fb, fc, fd) => mix(mix(fa, fb, u), mix(fc, fd, u), v);
    return { y: lerp4(ht(a), ht(b), ht(c), ht(d)), amount: lerp4(a.b, b.b, c.b, d.b), shade: lerp4(a.a, b.a, c.a, d.a).mul(0.45).add(0.75) };
  };
  // (where this tuft stands now: the copy of its square nearest the camera; course x is mirrored)
  const camX = cameraPosition.x.negate(), camZ = cameraPosition.z;
  const sx = cell.x.add(floor(camX.sub(cell.x).div(SIDE).add(0.5)).mul(SIDE));
  const sz = cell.y.add(floor(camZ.sub(cell.y).div(SIDE).add(0.5)).mul(SIDE));
  const spot = look(sx, sz);
  const at = vec3(sx.negate(), spot.y.sub(0.01), sz);
  const fade = smoothstep(R, R - 4, distance(at, cameraPosition));
  // From about 24 cm and dense right beside the fairway down to 9 cm and patchy 20 m out (deep
  // rough the tallest; all a little more than real rough, so the blades read from standing
  // height), none right round the ball, whose own patch shows how it lies
  const clear = smoothstep(0.2, 0.5, distance(vec2(sx.negate(), sz), carpetBall.xz));
  // (where it's thinner, fewer tufts as well as shorter ones)
  const present = step(fract(cell.w.mul(7.31)), smoothstep(0.25, 0.75, spot.amount).mul(0.45).add(0.55));
  const hgt = spot.amount.mul(0.26).mul(cell.z.mul(0.5).add(0.75)).mul(fade).mul(clear).mul(present);
  const turn = cell.w.mul(6.283), cs = cos(turn), sn = sin(turn);
  const m = grassMaterial({
    sway: 0.006,
    place: (p) => at.add(vec3(p.x.mul(cs).sub(p.z.mul(sn)).mul(1.6), p.y, p.x.mul(sn).add(p.z.mul(cs)).mul(1.6)).mul(hgt)),
  });
  // The dark green of the rough (as the tufts round the ball), lighter or darker with the photo
  m.colorNode = vec3(0.05, 0.088, 0.028).mul(spot.shade).mul(cell.z.mul(0.3).add(0.85));
  return m;
}

// The ground's height exactly as the terrain mesh draws it: each 2 m square of the height grid as
// two flat triangles (split from its +x corner to its +z corner, as built above)
function meshGroundAt(course) {
  const { nx, nz, cell, heights } = course.grid, { minX, minZ } = course.bounds;
  return (x, z) => {
    const fx = (x - minX) / cell, fz = (z - minZ) / cell, i = Math.floor(fx), j = Math.floor(fz);
    if (i < 0 || j < 0 || i >= nx - 1 || j >= nz - 1) return null;
    const u = fx - i, v = fz - j, k = j * nx + i;
    const a = heights[k], b = heights[k + 1], c = heights[k + nx], d = heights[k + nx + 1];
    return u + v <= 1 ? a * (1 - u - v) + b * u + c * v : d * (u + v - 1) + b * (1 - v) + c * (1 - u);
  };
}

// A stream laid down its channel (see streams.js): rows across it every 2 m, seven points each,
// from bank to bank. UVs are metres downstream and across, so the ripples can be drawn out along
// the current and carried down it; 'stream' holds the speed (m/s), where across the channel the
// point is (-1 one bank, 1 the other) and the half width.
const ACROSS = [-1, -0.7, -0.35, 0, 0.35, 0.7, 1];
function streamGeometry(wb, groundAt) {
  const { chain, i0, i1 } = wb.ribbon, A = ACROSS.length, rows = i1 - i0 + 1;
  const pos = new Float32Array(rows * A * 3), uvs = new Float32Array(rows * A * 2), st = new Float32Array(rows * A * 3), nrm = new Float32Array(rows * A * 3);
  for (let r = 0; r < rows; r++) {
    const i = i0 + r, [cx, cz] = chain.pts[i], [nx, nz] = chain.nrm[i], hw = Math.max(MIN_HW, chain.hw[i]);
    // (across measured looking downstream, so the two banks keep their sides whichever way it runs)
    const side = chain.down[Math.min(i + 1, chain.pts.length - 1)] >= chain.down[Math.max(0, i - 1)] ? 1 : -1;
    for (let a = 0; a < A; a++) {
      const k = r * A + a, t = ACROSS[a], x = cx + nx * hw * t, z = cz + nz * hw * t;
      // (never under the ground drawn beneath it - or anywhere within half a metre, so the ground
      // can't show through between these points either - whatever the water level says; only
      // the edges may go under, where they tuck beneath the banks)
      const level = wb.levelAt(x, z);
      let ground = -Infinity;
      if (Math.abs(t) < 1) for (const [dx, dz] of [[0, 0], [0.5, 0], [-0.5, 0], [0, 0.5], [0, -0.5]]) ground = Math.max(ground, groundAt(x + dx, z + dz) ?? -Infinity);
      const y = Math.max(level, ground + 0.04);
      pos[k * 3] = -x; pos[k * 3 + 1] = y - wb.level; pos[k * 3 + 2] = z;
      uvs[k * 2] = chain.down[i]; uvs[k * 2 + 1] = t * hw * side;
      st[k * 3] = chain.speed[i]; st[k * 3 + 1] = t; st[k * 3 + 2] = hw;
      nrm[k * 3 + 1] = 1;
    }
  }
  const idx = [];
  for (let r = 0; r < rows - 1; r++) for (let a = 0; a < A - 1; a++) {
    const p = r * A + a, q = p + A;
    idx.push(p, q, p + 1, p + 1, q, q + 1);
  }
  // (facing up: the mirrored x can turn the winding over)
  const ax = pos[A * 3] - pos[0], az = pos[A * 3 + 2] - pos[2], bx = pos[3] - pos[0], bz = pos[5] - pos[2];
  if (az * bx - ax * bz < 0) for (let k = 0; k < idx.length; k += 3) [idx[k + 1], idx[k + 2]] = [idx[k + 2], idx[k + 1]];
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.BufferAttribute(pos, 3));
  g.setAttribute('normal', new THREE.BufferAttribute(nrm, 3));
  g.setAttribute('uv', new THREE.BufferAttribute(uvs, 2));
  g.setAttribute('stream', new THREE.BufferAttribute(st, 3));
  g.setIndex(idx);
  return g;
}

// Stream water: ripples drawn out along the current and carried down it (two layers half a cycle
// apart, each faded out as it jumps back), quicker mid-channel than by the banks. The middle is
// deep and dark; towards the banks it shallows and the bed shows through. Fast water breaks into
// streaks of white, most of all along the edges.
function streamMaterial(env) {
  const m = new THREE.MeshPhysicalNodeMaterial({ roughness: 0.08, metalness: 0, ior: 1.33 });
  if (env) { m.envMap = env; m.envMapIntensity = 0.9; }
  const ripples = loadTexture('water_normal.jpg', false);
  const st = attribute('stream', 'vec3'), P = uv();
  const across = abs(st.y), fast = smoothstep(0.45, 2.0, st.x);
  const v = st.x.mul(float(1).sub(across.mul(across).mul(0.6)));
  const CYCLE = 1.2;
  const ph0 = fract(time.div(CYCLE)), ph1 = fract(time.div(CYCLE).add(0.5));
  const w0 = abs(ph0.mul(2).sub(1)).oneMinus();
  const layer = (ph, shift) => {
    const q = P.sub(vec2(v.mul(ph).mul(CYCLE), 0)).add(shift);
    const a = texture(ripples, q.mul(vec2(0.07, 0.2))).xyz.mul(2).sub(1);
    const b = texture(ripples, q.mul(vec2(0.22, 0.55)).add(0.37)).xyz.mul(2).sub(1);
    return vec3(a.xy.add(b.xy.mul(0.7)), a.z.mul(b.z));
  };
  const l0 = layer(ph0, vec2(0, 0)), l1 = layer(ph1, vec2(3.7, 1.3));
  const n = normalize(mix(l1, l0, w0));
  m.normalNode = normalMap(n.mul(0.5).add(0.5), vec2(float(0.28).add(fast.mul(0.35))));
  const dist = distance(cameraPosition, positionWorld);
  const deep = mix(vec3(0.03, 0.075, 0.062), vec3(0.02, 0.05, 0.044), smoothstep(10, 120, dist));
  const shallow = vec3(0.11, 0.12, 0.075);
  const base = mix(deep, shallow, smoothstep(0.45, 1, across).mul(0.8));
  const crest = mix(l1.x, l0.x, w0).add(mix(l1.y, l0.y, w0)).mul(0.5);
  const foam = smoothstep(0.25, 0.7, crest).mul(fast).mul(0.5)
    .add(smoothstep(0.82, 1, across).mul(smoothstep(0.05, 0.45, crest)).mul(smoothstep(0.3, 1.4, st.x)).mul(0.4)).min(0.8);
  const facing = clamp(dot(normalView, positionViewDirection), 0, 1);
  const fresnel = float(0.04).add(float(0.96).mul(pow(facing.oneMinus(), 4)));
  m.colorNode = mix(base.mul(fresnel.oneMinus()), vec3(0.64, 0.68, 0.66), foam);
  m.roughnessNode = mix(float(0.08), float(0.5), foam);
  return m;
}

// An outline with extra points so no side is longer than max metres
function densify(poly, max) {
  const out = [];
  for (let i = 0; i < poly.length; i++) {
    const [ax, az] = poly[i], [bx, bz] = poly[(i + 1) % poly.length], n = Math.max(1, Math.ceil(Math.hypot(bx - ax, bz - az) / max));
    for (let k = 0; k < n; k++) out.push([ax + ((bx - ax) * k) / n, az + ((bz - az) * k) / n]);
  }
  return out;
}

// Which way, and how fast, a stream or river runs at a point: (x, z) => [tx, tz, metres/second].
// The channel's direction there is the average direction of its banks within 12 m (as an axis,
// so the two banks, drawn opposite ways, agree); downstream is whichever way along it the water
// level drops, measured over 50 m so bumps in the elevation data don't flip it. Speed grows with
// the slope: barely moving on the flat, about 0.6 m/s at 1 in 100, 2.5 m/s on steep stretches.
function streamFlow(wb) {
  const edges = [];
  for (let i = 0; i < wb.poly.length; i++) {
    const [ax, az] = wb.poly[i], [bx, bz] = wb.poly[(i + 1) % wb.poly.length], len = Math.hypot(bx - ax, bz - az);
    if (len > 0.01) edges.push({ x: (ax + bx) / 2, z: (az + bz) / 2, len, a: Math.atan2(bz - az, bx - ax) });
  }
  return (x, z) => {
    let c = 0, s = 0;
    for (const e of edges) {
      const d = Math.hypot(e.x - x, e.z - z);
      if (d > 12) continue;
      const k = e.len * (1 - d / 13);
      c += Math.cos(2 * e.a) * k; s += Math.sin(2 * e.a) * k;
    }
    if (!c && !s) return [1, 0, 0.1];
    const ang = Math.atan2(s, c) / 2;
    let tx = Math.cos(ang), tz = Math.sin(ang);
    let slope = (wb.levelAt(x - tx * 25, z - tz * 25) - wb.levelAt(x + tx * 25, z + tz * 25)) / 50;
    if (slope < 0) { tx = -tx; tz = -tz; slope = -slope; }
    return [tx, tz, Math.min(2.5, 0.12 + slope * 48)];
  };
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
// shore: how close to water each spot is (red) and the greens with their collars (green), see
// buildShore: for the muddy bank at the waterline and plain-mown collars.
function terrainMaterial(map, soft, mask, mowDirs, strength, shore, hasSand = false) {
  const maskTex = new THREE.CanvasTexture(mask);
  maskTex.colorSpace = THREE.NoColorSpace;
  const shoreTex = new THREE.CanvasTexture(shore);
  shoreTex.colorSpace = THREE.NoColorSpace;
  // Bunkers: where the smooth blurred bunker field crosses one half, softened over about a screen
  // pixel (crisp and smooth at any distance). Courses without mapped bunkers use the surface mask.
  const sandWeight = (sm, vUv) => {
    if (!hasSand) return smoothstep(0.66, 0.76, sm.b);
    const sb = texture(shoreTex, vUv).b, aa = fwidth(sb).max(0.004);
    return smoothstep(float(0.5).sub(aa), float(0.5).add(aa), sb);
  };
  const dirTex = new THREE.CanvasTexture(mowDirs.fairway), dirTexG = new THREE.CanvasTexture(mowDirs.green);
  dirTex.colorSpace = dirTexG.colorSpace = THREE.NoColorSpace;
  const material = new THREE.MeshStandardNodeMaterial({ roughness: 0.92, metalness: 0 });
  material.envMapIntensity = 0.4;
  material.colorNode = Fn(() => {
    const vUv = uv();
    const base = texture(map, vUv).rgb;
    const sm = texture(maskTex, vUv).rgb;
    // Crisp edges: tighten the (slightly soft) mask so each surface has a clear outline (bunkers
    // the crispest: a groomed bunker has a clean-cut edge)
    const wf = smoothstep(0.32, 0.68, sm.r), wg = smoothstep(0.3, 0.7, sm.g), ws = sandWeight(sm, vUv);
    const wp = pathWeight(sm, ws);
    const wr = clamp(float(1).sub(wf).sub(wg).sub(ws).sub(wp), 0, 1);
    // Give each surface its own colour while keeping the photo's light and shade:
    // fairway bright and even, rough deeper and darker, green the smoothest and brightest
    const lum = dot(base, vec3(0.2126, 0.7152, 0.0722));
    const shade = clamp(lum.div(0.3), 0.65, 1.35);
    const shadeSoft = clamp(dot(texture(soft, vUv).rgb, vec3(0.2126, 0.7152, 0.0722)).div(0.3), 0.85, 1.15);
    // (deep, warm greens: real turf reads darker and less blue than a bright lime; sand evenly
    // pale, as freshly raked)
    // (mown grass a little lighter and warmer than the rough, the rough a deep, slightly blue
    // green: close in colour, as on a real course, with a crisp edge between them)
    const graded = vec3(0.06, 0.112, 0.022).mul(mix(1, shadeSoft, 0.35)).mul(wf)
      .add(mix(vec3(0.04, 0.072, 0.024), vec3(0.085, 0.078, 0.034), texture(dirTexG, vUv).b).mul(shade.min(1.1)).mul(wr)) // (tawny under tall grass)
      .add(vec3(0.07, 0.128, 0.025).mul(mix(1, shadeSoft, 0.12)).mul(wg))
      .add(vec3(0.52, 0.41, 0.25).mul(mix(1, shadeSoft, 0.2)).mul(ws))
      .add(vec3(lum.mul(1.05)).mul(wp));                                    // pavement: the photo, greyed
    // Fairways, greens and sand are fully graded (no photo detail); the rough mostly, and fully
    // where the photo is pale (sand or bare ground in the picture that the map puts in the rough
    // would otherwise show as a grey halo, e.g. round a bunker drawn a little off)
    const roughW = wr.mul(mix(0.85, 1, smoothstep(0.25, 0.45, lum)));
    const col = mix(base, graded, wf.add(roughW).add(wg).add(ws).add(wp.mul(0.7))).toVar();

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
    // Rough reads clumpier, greens the most even, and raked sand smoother than its wind-rippled photo
    const amount = near.mul(strength).mul(float(1).add(wr.mul(0.2)).sub(wg.mul(0.4)).sub(ws.mul(0.55)));
    col.mulAssign(mix(vec3(1), blend, amount));
    // Broad patchiness in the rough, visible from far away too
    const patchy = dot(texture(groundTexture('rough'), q.mul(0.011)).rgb, LUMA).div(dot(vec3(...GROUND_MEAN.rough), LUMA));
    col.mulAssign(mix(1, patchy, wr.mul(0.14)));

    // Mowing patterns (chosen per course, see mowing.js): each pass of the mower lays the grass one
    // way, so it reads lighter or darker depending on where you look from. Each fairway and green
    // is mown along its own direction (dirTex). Passes are a mower's width, with crisp, slightly
    // wobbly edges; the edges soften with distance so far-off stripes don't shimmer. Like real
    // turf, the pattern is faint right at your feet, clearest at mid distance, and fades far off.
    const dcF = texture(dirTex, vUv).rg.mul(2).sub(1), dcG = texture(dirTexG, vUv).rg.mul(2).sub(1);
    const angF = atan(dcF.y, dcF.x).mul(0.5), angG = atan(dcG.y, dcG.x).mul(0.5);
    const alongF = vec2(cos(angF), sin(angF)), alongG = vec2(cos(angG), sin(angG));
    const view = normalize(positionWorld.xz.sub(cameraPosition.xz).add(1e-4));
    const edge = camD.mul(0.006).max(0.15);
    const pattern = (along, width, id) => {
      const across = vec2(along.y.negate(), along.x);
      const d1 = normalize(along.add(across)), d2 = normalize(along.sub(across));
      const a = dot(q, along);
      const wobble = sin(a.mul(0.23)).mul(0.06).add(sin(a.mul(0.061).add(1.7)).mul(0.12));
      const pass = (dir) => clamp(sin(dot(q, dir).add(wobble).div(width).mul(PI)).mul(float((2 * width) / Math.PI).div(edge)), -1, 1);
      const options = [float(0), pass(across), pass(d1), pass(across).mul(pass(along)), pass(d1).mul(pass(d2))];
      return options.reduce((acc, v, i) => (i === 0 ? v : select(id.greaterThan(i - 0.5), v, acc)));
    };
    const facingF = abs(dot(view, alongF)).mul(0.55).add(0.45), facingG = abs(dot(view, alongG)).mul(0.55).add(0.45);
    const fairwayFade = smoothstep(2, 14, camD).mul(smoothstep(250, 600, camD).oneMinus());
    const greenFade = smoothstep(1, 6, camD).mul(smoothstep(60, 180, camD).oneMinus());
    // (the collar round each green is mown plain, so the fairway stripes stop at it)
    const collar = smoothstep(0.42, 0.52, texture(shoreTex, vUv).g);
    const mow = wf.mul(pattern(alongF, MOW_WIDTH.fairway, mowPattern.fairway)).mul(0.085).mul(facingF).mul(fairwayFade).mul(collar.oneMinus())
      .add(wg.mul(pattern(alongG, MOW_WIDTH.green, mowPattern.green)).mul(0.05).mul(facingG).mul(greenFade));

    // Muddy bank at the waterline: bare earth from the water's edge out to about a metre (wider
    // and narrower along the bank), dark and wet at the water, drier further up, and continuing
    // a little way under the surface so the shallows look muddy too. Not on sand or paths, and
    // only a thin edge beside greens.
    const sh = texture(shoreTex, vUv).r;
    const wid = clamp(dot(texture(groundTexture('rough'), q.mul(0.045).add(0.71)).rgb, LUMA).div(dot(vec3(...GROUND_MEAN.rough), LUMA)), 0.5, 1.5);
    const e0 = clamp(wid.sub(0.5).mul(0.14), 0.02, 0.14);
    // (how muddy this pond's banks are: its style, see buildMowDirections - mown-to-the-edge ponds
    // have almost none)
    const mudAmt = texture(dirTex, vUv).b;
    const mud = smoothstep(e0, e0.add(0.12), sh).mul(smoothstep(0.82, 0.97, sh).oneMinus())
      .mul(ws.oneMinus()).mul(wp.oneMinus()).mul(wg.mul(0.7).oneMinus()).mul(mudAmt);
    // Where there's little mud, the grass right at the water is darker and wet instead
    const wetLine = smoothstep(0.36, 0.5, sh).mul(smoothstep(0.5, 0.75, sh).oneMinus()).mul(mudAmt.oneMinus()).mul(0.4);
    const wet = smoothstep(0.25, 0.6, sh);
    // (with the grain of real damp earth: CC0 ambientCG Ground094B)
    const mudCol = mix(vec3(0.28, 0.155, 0.07), vec3(0.13, 0.075, 0.038), wet).mul(mix(ratio('mud', 0.45), ratio('mud', 0.13, 0.3), 0.35));
    // Raked bunkers: fine grooves about 5 cm apart running along the hole, gently wavy as a rake
    // is pulled by hand. They show up close and fade out by ~25 m (finer than the screen can show
    // further off). Round the sand, the grass at the edge drops into a shadowed lip.
    const acrossR = vec2(alongF.y.negate(), alongF.x);
    const rakeWave = sin(dot(q, alongF).mul(0.9)).mul(0.05).add(sin(dot(q, alongF).mul(0.23).add(2.1)).mul(0.12));
    const groove = smoothstep(-0.3, 0.9, sin(dot(q, acrossR).add(rakeWave).div(0.05).mul(PI))).mul(2).sub(1);
    const rake = ws.mul(groove).mul(0.07).mul(smoothstep(4, 25, camD).oneMinus());
    // Lip: the grass edge darkens as it turns down into the bunker, and the sand face just inside
    // sits in its shade (less raked there too)
    const sb = hasSand ? texture(shoreTex, vUv).b : sm.b;
    const lip = (hasSand ? smoothstep(0.28, 0.5, sb) : smoothstep(0.12, 0.62, sb)).mul(ws.oneMinus()).mul(wp.oneMinus()).mul(0.3);
    const face = ws.mul((hasSand ? smoothstep(0.5, 0.64, sb) : smoothstep(0.8, 0.97, sb)).oneMinus()).mul(0.16);
    const ground = col.mul(mow.add(1)).mul(rake.mul(face.mul(4).oneMinus().max(0)).add(1)).mul(lip.oneMinus()).mul(face.oneMinus());
    return mix(ground.mul(wetLine.oneMinus()), mudCol, mud);
  })();
  material.normalNode = Fn(() => {
    const vUv = uv();
    const sm = texture(maskTex, vUv).rgb;
    const wf = smoothstep(0.32, 0.68, sm.r), wg = smoothstep(0.3, 0.7, sm.g), ws = sandWeight(sm, vUv);
    const wp = pathWeight(sm, ws);
    const wr = clamp(float(1).sub(wf).sub(wg).sub(ws).sub(wp), 0, 1);
    const q = positionWorld.xz;
    const nt = (name, scale) => texture(groundNormal(name), q.mul(scale)).xyz;
    // (Cart paths and bunkers go without: WebGPU allows 16 textures per shader - with the shadow
    // and sky maps this shader is at that limit, and one more makes the whole ground fail to draw.
    // Asphalt is smooth enough, and raked sand gets its grooves from the colour.)
    const nb = nt('lawn', 0.8).mul(wf.add(wg)).add(nt('rough', 0.6).mul(wr)).add(vec3(0.5, 0.5, 1).mul(ws.add(wp)));
    const near = smoothstep(15, 160, distance(cameraPosition, positionWorld)).oneMinus();
    // (the muddy bank by the water is smoother than turf, and raked sand than wind-rippled sand)
    const sh = texture(shoreTex, vUv).r;
    const bank = smoothstep(0.08, 0.25, sh).mul(smoothstep(0.82, 0.97, sh).oneMinus());
    return normalMap(mix(vec3(0.5, 0.5, 1), nb, near.mul(float(0.9).sub(wg.mul(0.4)).sub(ws.mul(0.5))).mul(bank.mul(0.6).oneMinus())), vec2(0.7));
  })();
  return material;
}

// A golf ball: glossy white with a clear coat, ~330 dimples (as a normal map) and printed marks -
// an alignment line round the middle and a logo - so you can see it turn. Both maps are drawn
// here, laid out for three.js's sphere UVs (u round the equator, v pole to pole).
function golfBallMaterial() {
  // Dimple centres spread evenly over the sphere (a Fibonacci spiral)
  const N = 332, R = 0.088; // dimple angular radius (rad): neighbours almost touch, as on a real ball
  const centres = [];
  for (let i = 0; i < N; i++) {
    const y = 1 - (2 * (i + 0.5)) / N, r = Math.sqrt(1 - y * y), a = i * Math.PI * (3 - Math.sqrt(5));
    centres.push([Math.cos(a) * r, y, Math.sin(a) * r]);
  }
  // Normal map: start flat, then press in each dimple over the pixels it covers
  const W = 1024, H = 512;
  const nc = document.createElement('canvas'); nc.width = W; nc.height = H;
  const nx = nc.getContext('2d'), img = nx.createImageData(W, H), px = img.data;
  for (let o = 0; o < px.length; o += 4) { px[o] = 128; px[o + 1] = 128; px[o + 2] = 255; px[o + 3] = 255; }
  // Direction for a pixel: three.js SphereGeometry has x = -cos(phi) sin(theta), y = cos(theta),
  // z = sin(phi) sin(theta), with phi = 2 pi u and theta = pi v (v = 0 at the top row of the canvas)
  for (const [cx, cy, cz] of centres) {
    const theta0 = Math.acos(cy), phi0 = Math.atan2(cz, -cx);
    const jr = Math.ceil(((R * 1.05) / Math.PI) * H), j0 = Math.round((theta0 / Math.PI) * H);
    for (let j = Math.max(0, j0 - jr); j <= Math.min(H - 1, j0 + jr); j++) {
      const theta = ((j + 0.5) / H) * Math.PI, st = Math.sin(theta), ct = Math.cos(theta);
      const ir = Math.min(W / 2, Math.ceil(((R * 1.05) / Math.max(0.05, st) / (2 * Math.PI)) * W)), i0 = Math.round(((phi0 / (2 * Math.PI)) * W + W) % W);
      for (let ii = i0 - ir; ii <= i0 + ir; ii++) {
        const i = ((ii % W) + W) % W, phi = ((i + 0.5) / W) * 2 * Math.PI, sp = Math.sin(phi), cp = Math.cos(phi);
        const dx = -cp * st, dy = ct, dz = sp * st;
        const dot = dx * cx + dy * cy + dz * cz, a = Math.acos(Math.min(1, dot));
        if (a >= R) continue;
        // Bowl-shaped pit: the surface leans towards the centre, most part-way out, flat at the bottom
        const t = a / R, slope = 1.6 * t * (1 - t * t * 0.35);
        let gx = cx - dot * dx, gy = cy - dot * dy, gz = cz - dot * dz;
        const gl = Math.hypot(gx, gy, gz) || 1; gx /= gl; gy /= gl; gz /= gl;
        // Tangent-space axes for this UV layout: along u and along uv.y
        const tu = gx * sp + gz * cp, tv = gx * cp * ct + gy * st - gz * sp * ct;
        const n = [tu * slope, tv * slope, 1], l = Math.hypot(...n);
        const o = (j * W + i) * 4;
        px[o] = (n[0] / l) * 127 + 128; px[o + 1] = (n[1] / l) * 127 + 128; px[o + 2] = (n[2] / l) * 127 + 128;
      }
    }
  }
  nx.putImageData(img, 0, 0);
  // Colour: white, an alignment line round the equator and a logo on each side of it
  const cc = document.createElement('canvas'); cc.width = W; cc.height = H;
  const c = cc.getContext('2d');
  c.fillStyle = '#ffffff'; c.fillRect(0, 0, W, H);
  c.fillStyle = '#1a1a1a'; c.fillRect(0, H / 2 - 3, W, 6);
  c.font = 'bold 30px system-ui, sans-serif'; c.textAlign = 'center';
  c.fillStyle = '#c8202a'; c.fillText('OpenTee', W * 0.25, H / 2 - 12);
  c.fillStyle = '#1a1a1a'; c.fillText('1', W * 0.75, H / 2 - 12);
  const map = new THREE.CanvasTexture(cc); map.colorSpace = THREE.SRGBColorSpace; map.anisotropy = 4;
  const normalMap = new THREE.CanvasTexture(nc); normalMap.colorSpace = THREE.NoColorSpace;
  return new THREE.MeshPhysicalMaterial({
    map, normalMap, normalScale: new THREE.Vector2(1, 1),
    roughness: 0.35, clearcoat: 0.8, clearcoatRoughness: 0.12, emissive: 0x1c1c1c,
  });
}

// A parked car about 4.5 m long (length along x, nose towards +x), as boxes: the body, a cabin of
// glass narrowing towards the roof, and four wheels. Vertex colours are multipliers: the body
// takes the car's colour, the glass and tyres stay dark.
function carGeometry(suv = false) {
  const parts = [];
  const box = (lx, ly, lz, cx, cy, cz, shade, taper = 0) => {
    const g = new THREE.BoxGeometry(lx, ly, lz).toNonIndexed();
    const pos = g.attributes.position;
    // (the top of the cabin narrower than its base, like a windscreen and rear window)
    if (taper) for (let i = 0; i < pos.count; i++) if (pos.getY(i) > 0) { pos.setX(i, pos.getX(i) * (1 - taper)); pos.setZ(i, pos.getZ(i) * (1 - taper * 0.35)); }
    g.translate(cx, cy, cz);
    g.deleteAttribute('uv');
    const col = []; for (let i = 0; i < pos.count; i++) col.push(shade, shade, shade);
    g.setAttribute('color', new THREE.Float32BufferAttribute(col, 3));
    parts.push(g);
  };
  const L = suv ? 4.75 : 4.55, W = suv ? 1.92 : 1.8, bodyH = suv ? 0.85 : 0.68, clear = suv ? 0.32 : 0.24;
  box(L, bodyH, W, 0, clear + bodyH / 2, 0, 1);                                                   // body
  box(L * 0.06, bodyH * 0.5, W * 0.9, L * 0.48, clear + bodyH * 0.55, 0, 0.55);                  // grille
  const cabL = suv ? 2.9 : 2.35, cabH = suv ? 0.72 : 0.62;
  box(cabL, cabH, W * 0.88, suv ? -0.25 : -0.3, clear + bodyH + cabH / 2, 0, 0.13, suv ? 0.12 : 0.24); // glass
  box(cabL * (suv ? 0.82 : 0.6), 0.05, W * 0.78, suv ? -0.25 : -0.3, clear + bodyH + cabH, 0, 1);   // roof
  for (const sx of [-1, 1]) for (const sz of [-1, 1]) {
    const w = new THREE.CylinderGeometry(0.34, 0.34, 0.24, 12).toNonIndexed();
    w.rotateX(Math.PI / 2);
    w.translate(sx * L * 0.33, 0.34, sz * (W / 2 - 0.08));
    w.deleteAttribute('uv');
    const col = []; for (let i = 0; i < w.attributes.position.count; i++) col.push(0.05, 0.05, 0.05);
    w.setAttribute('color', new THREE.Float32BufferAttribute(col, 3));
    parts.push(w);
  }
  const g = mergeGeometries(parts);
  g.computeVertexNormals();
  return g;
}

// A lumpy stone about 1 m across (scaled and turned per rock): a sphere pushed in and out by noise
function rockGeometry() {
  let g = new THREE.IcosahedronGeometry(0.5, 2);
  g.deleteAttribute('uv'); g.deleteAttribute('normal');
  g = mergeVertices(g);
  const p = g.attributes.position, v = new THREE.Vector3();
  for (let i = 0; i < p.count; i++) {
    v.fromBufferAttribute(p, i);
    const n = noise(v.x * 2.1 + v.y * 1.3 + 5, v.z * 2.1 - v.y * 0.7) * 0.22 + noise(v.x * 6 + 1, v.z * 6 + v.y * 4) * 0.06;
    v.multiplyScalar(1 + n);
    if (v.y < -0.1) v.y *= 0.6; // flatter underneath, where it sits in the ground
    p.setXYZ(i, v.x, v.y, v.z);
  }
  g.computeVertexNormals();
  return g;
}

// Stone: the per-rock colour with a fine grain (the asphalt photo's speckle, which reads as rock)
function rockMaterial() {
  const m = new THREE.MeshStandardNodeMaterial({ roughness: 0.9, metalness: 0 });
  m.envMapIntensity = 0.5;
  // Photo-scanned rock (CC0 ambientCG Rock058), projected from above, the front and the side and
  // blended by which way the surface faces (the stones have no texture coordinates of their own),
  // divided by its average colour so it adds cracks and veins to each stone's own colour
  const tex = groundTexture('rock'), p = positionWorld.mul(1.6), w = abs(normalWorld);
  const c = texture(tex, p.xz).rgb.mul(w.y).add(texture(tex, p.zy).rgb.mul(w.x)).add(texture(tex, p.xy).rgb.mul(w.z)).div(w.x.add(w.y).add(w.z));
  const r = c.div(vec3(...GROUND_MEAN.rock));
  m.colorNode = clamp(mix(vec3(dot(r, vec3(0.2126, 0.7152, 0.0722))), r, 0.5), 0.3, 2.2);
  return m;
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
