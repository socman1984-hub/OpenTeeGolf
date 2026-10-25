// Bits floating down the streams: flecks of foam, thickest where the water runs fast or falls,
// and fallen leaves, turning slowly as they go. Each is carried along its stream at the current's
// speed (quicker mid-channel than by the banks) and kept within reach of the camera: one that
// drifts out of reach or off the end of its stream starts again somewhere nearby. (After Creek
// Water Studio by bjlin-meshy, MIT.)
//
// chains: the courses's streams (streams.js: pts, hw, nrm, level, down, speed, fall per sample,
// course coordinates; samples 2 m apart). Course x is the scene's mirrored.
import * as THREE from 'three/webgpu';

const REACH = 70;             // m round the camera
const LEAF_TINTS = [[0.62, 0.62, 0.22], [0.78, 0.6, 0.18], [0.72, 0.38, 0.14], [0.5, 0.32, 0.17], [0.42, 0.48, 0.2]];

function leafTexture() {
  const c = document.createElement('canvas');
  c.width = c.height = 64;
  const g = c.getContext('2d');
  g.fillStyle = '#fff';
  g.beginPath();
  g.moveTo(32, 4);
  g.bezierCurveTo(52, 16, 52, 44, 32, 56);
  g.bezierCurveTo(12, 44, 12, 16, 32, 4);
  g.fill();
  g.strokeStyle = 'rgba(0,0,0,0.25)'; g.lineWidth = 2;
  g.beginPath(); g.moveTo(32, 6); g.lineTo(32, 62); g.stroke();
  const t = new THREE.CanvasTexture(c);
  t.colorSpace = THREE.SRGBColorSpace;
  return t;
}
function foamTexture() {
  const c = document.createElement('canvas');
  c.width = c.height = 64;
  const g = c.getContext('2d');
  const grad = g.createRadialGradient(32, 32, 2, 32, 32, 30);
  grad.addColorStop(0, 'rgba(255,255,255,0.85)'); grad.addColorStop(0.5, 'rgba(255,255,255,0.4)'); grad.addColorStop(1, 'rgba(255,255,255,0)');
  g.fillStyle = grad; g.fillRect(0, 0, 64, 64);
  // (broken up into bubbles rather than one soft disc)
  g.globalCompositeOperation = 'destination-out';
  for (let i = 0; i < 26; i++) { g.beginPath(); g.arc(8 + Math.random() * 48, 8 + Math.random() * 48, 1.5 + Math.random() * 4, 0, 6.3); g.fillStyle = `rgba(0,0,0,${0.3 + Math.random() * 0.5})`; g.fill(); }
  return new THREE.CanvasTexture(c);
}

export class Flotsam {
  // rocks: boulders standing out of the water ({ x, z, r }, course coordinates), which what floats
  // down slides round rather than through
  constructor(chains, { foam = 400, leaves = 60 } = {}, rocks = []) {
    this.rocks = new Map();
    for (const rk of rocks) {
      const key = `${Math.floor(rk.x / 4)},${Math.floor(rk.z / 4)}`;
      if (!this.rocks.has(key)) this.rocks.set(key, []);
      this.rocks.get(key).push(rk);
    }
    this.chains = chains.filter((c) => c.level && c.speed && c.pts.length > 3);
    // (which way along each stream is downstream, in samples)
    for (const c of this.chains) c.dirI = c.down[c.down.length - 1] > c.down[0] ? 1 : -1;
    this.group = new THREE.Group();
    const plane = new THREE.PlaneGeometry(1, 1).rotateX(-Math.PI / 2);
    const foamMat = new THREE.MeshStandardNodeMaterial({ map: foamTexture(), transparent: true, depthWrite: false, roughness: 0.6, color: 0xcfd6d0, opacity: 0.7 });
    const leafMat = new THREE.MeshStandardNodeMaterial({ map: leafTexture(), transparent: true, alphaTest: 0.4, roughness: 0.75, side: THREE.DoubleSide });
    this.foam = new THREE.InstancedMesh(plane, foamMat, foam);
    this.leaves = new THREE.InstancedMesh(plane, leafMat, leaves);
    this.leaves.instanceColor = new THREE.InstancedBufferAttribute(new Float32Array(leaves * 3), 3);
    for (const m of [this.foam, this.leaves]) {
      m.frustumCulled = false; m.count = 0;
      m.renderOrder = 5;          // (after the stream water, which they float on)
      m.layers.set(1);            // (not in the pond mirror)
      this.group.add(m);
    }
    this.leaves.receiveShadow = true;
    this.items = [];
    for (let i = 0; i < foam + leaves; i++) this.items.push({ leaf: i >= foam, alive: false });
    this._m = new THREE.Matrix4(); this._q = new THREE.Quaternion(); this._s = new THREE.Vector3(); this._p = new THREE.Vector3();
    this._c = new THREE.Color(); this._up = new THREE.Vector3(0, 1, 0);
  }

  // (the stream's values at fractional sample f)
  at(c, f, arr) { const i = Math.max(0, Math.min(c.pts.length - 2, Math.floor(f))), u = Math.min(1, Math.max(0, f - i)); return arr[i] + (arr[i + 1] - arr[i]) * u; }

  // Somewhere on a stream within reach of (cx, cz) (course coordinates); foam favours fast water
  spawn(it, cx, cz) {
    for (let tries = 0; tries < 30; tries++) {
      const c = this.chains[Math.floor(Math.random() * this.chains.length)];
      const i = Math.floor(Math.random() * (c.pts.length - 1));
      const [x, z] = c.pts[i];
      if (Math.hypot(x - cx, z - cz) > REACH) continue;
      if (!it.leaf && Math.random() > 0.04 + Math.min(1, c.speed[i] / 2.5) * 0.6 + Math.min(1, c.fall[i] * 8)) continue;
      Object.assign(it, { alive: true, c, f: i + Math.random(), t: (Math.random() * 2 - 1) * (it.leaf ? 0.6 : 0.85), yaw: Math.random() * 6.28,
        spin: (Math.random() - 0.5) * (it.leaf ? 0.8 : 0.1), size: it.leaf ? 0.08 + Math.random() * 0.07 : 0.05 + Math.random() * Math.random() * 0.22,
        tint: LEAF_TINTS[Math.floor(Math.random() * LEAF_TINTS.length)], bob: Math.random() * 6.28, life: 0 });
      return true;
    }
    it.alive = false;
    return false;
  }

  update(camera, dt) {
    if (!this.chains.length) return;
    dt = Math.min(dt, 0.1);
    const cx = -camera.position.x, cz = camera.position.z, now = performance.now() / 1000;
    let nf = 0, nl = 0;
    for (const it of this.items) {
      if (!it.alive || it.f < 0 || it.f > it.c.pts.length - 1.01) { if (!this.spawn(it, cx, cz)) continue; }
      const c = it.c;
      // (carried along: m/s down the stream, slower towards the banks; samples are 2 m apart)
      const v = this.at(c, it.f, c.speed) * (1 - 0.55 * it.t * it.t);
      it.f += (c.dirI * v * dt) / 2;
      it.t = Math.max(-0.9, Math.min(0.9, it.t + Math.sin(now * 0.7 + it.bob) * 0.02 * dt));
      it.yaw += it.spin * dt; it.life += dt;
      const i = Math.max(0, Math.min(c.pts.length - 2, Math.floor(it.f))), u = it.f - i;
      const [ax, az] = c.pts[i], [bx, bz] = c.pts[i + 1], [nx, nz] = c.nrm[i];
      const hw = Math.max(1.9, this.at(c, it.f, c.hw));
      let x = ax + (bx - ax) * u + nx * hw * it.t, z = az + (bz - az) * u + nz * hw * it.t;
      // (round any rock in the way: pushed out across the stream to its edge, to whichever side
      // it's already on, and held back a little as it squeezes past)
      const hit = this.rockAt(x, z);
      if (hit) {
        const side = Math.sign((x - hit.x) * nx + (z - hit.z) * nz) || (it.t >= 0 ? 1 : -1);
        it.t = Math.max(-0.95, Math.min(0.95, it.t + side * (hit.r + 0.06 - hit.d) / hw));
        it.f -= (c.dirI * v * dt) / 2 * 0.6;
        x = ax + (bx - ax) * u + nx * hw * it.t; z = az + (bz - az) * u + nz * hw * it.t;
      }
      if (Math.hypot(x - cx, z - cz) > REACH + 10) { it.alive = false; continue; }
      const y = this.at(c, it.f, c.level) + 0.03 + Math.sin(now * 2 + it.bob) * 0.008;
      // (foam drawn out along the current, faster water longer streaks; fades in as it appears)
      const fade = Math.min(1, it.life / 1.5);
      // (a turn about the vertical that lays the flake's long side along the current, in the scene)
      const along = Math.atan2(-(bz - az), -(bx - ax));
      this._q.setFromAxisAngle(this._up, it.leaf ? it.yaw : along + it.yaw * 0.1);
      const stretch = it.leaf ? 1 : 1 + Math.min(2, v * 0.6);
      this._s.set(it.size * stretch * fade, 1, it.size * (it.leaf ? 1.6 : 1) * fade);
      this._m.compose(this._p.set(-x, y, z), this._q, this._s);
      if (it.leaf) {
        this.leaves.setMatrixAt(nl, this._m);
        this.leaves.setColorAt(nl, this._c.setRGB(...it.tint));
        nl++;
      } else this.foam.setMatrixAt(nf++, this._m);
    }
    this.foam.count = nf; this.leaves.count = nl;
    this.foam.instanceMatrix.needsUpdate = true;
    this.leaves.instanceMatrix.needsUpdate = true;
    if (this.leaves.instanceColor) this.leaves.instanceColor.needsUpdate = true;
  }

  // The rock (with how far from its middle) a point is inside, if any
  rockAt(x, z) {
    const bi = Math.floor(x / 4), bj = Math.floor(z / 4);
    for (let dj = -1; dj <= 1; dj++) for (let di = -1; di <= 1; di++) {
      for (const rk of this.rocks.get(`${bi + di},${bj + dj}`) || []) {
        const d = Math.hypot(x - rk.x, z - rk.z);
        if (d < rk.r + 0.05) return { ...rk, d };
      }
    }
    return null;
  }

  dispose() {
    for (const m of [this.foam, this.leaves]) { m.geometry.dispose(); m.material.map?.dispose(); m.material.dispose(); }
  }
}
