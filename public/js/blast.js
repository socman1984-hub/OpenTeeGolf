// Boom range: a game for kids on the driving range. Things to blow up stand down the range (TNT
// barrels, crates, watermelons, pumpkins, bunches of balloons, a castle of blocks and one giant
// bomb), and every ball that hits one, or lands near it, sets it off: a flash, a fireball, bits
// flying everywhere, and a bang. Barrels and bombs set off whatever stands near them. Three levels:
// on Easy everything is more than twice the size and a ball landing anywhere near counts; Hard is
// life size and has to be close. The best score is kept for each.
//
// blastRange(base) takes the driving range from practice.js (its land, boards and flags) and puts
// the targets on it. main.js passes the ball along its flight (ballAt), says where it came to
// rest (landed), and reads the score.
import * as THREE from 'three/webgpu';
import { boom as boomSound } from './creek-sound.js';

// size: how much bigger everything is; reach: how close (m) the flying ball has to pass, beyond the
// thing itself; land: how close (m) to the thing a ball has to come down or stop; balls a game
export const BLAST_LEVELS = {
  easy: { name: 'Easy', about: 'giant targets, near misses count', size: 2.3, reach: 2, land: 9, balls: 15 },
  medium: { name: 'Medium', about: 'big targets', size: 1.6, reach: 1.2, land: 5.5, balls: 12 },
  hard: { name: 'Hard', about: 'life size, hit them close', size: 1, reach: 0.6, land: 3, balls: 10 },
};
const YD = 0.9144;
const T = (x, y, z) => new THREE.Vector3(-x, y, z);
const rand = (a, b) => a + Math.random() * (b - a);
const pick = (xs) => xs[Math.floor(Math.random() * xs.length)];

// What each kind of thing is: size (radius, height in m), points, how far its blast sets off
// others (chain, m), how big the explosion is, and what flies out of it
const KINDS = {
  tnt: { name: 'TNT barrel', r: 0.75, h: 1.5, points: 100, chain: 16, size: 1.5, fire: true, bits: [0xd8342c, 0x2b2b2b, 0xf2c94c] },
  crate: { name: 'crate', r: 1.0, h: 1.9, points: 50, chain: 0, size: 1, fire: true, bits: [0xb57d43, 0x8a5a2b, 0xd9a868] },
  melon: { name: 'watermelon', r: 1.1, h: 1.6, points: 75, chain: 0, size: 0.9, splat: [0xf0485a, 0x2f8a3a, 0xfbe3e6], bits: [0x2f8a3a, 0xf0485a, 0x1d5c27] },
  pumpkin: { name: 'pumpkin', r: 1.2, h: 1.7, points: 75, chain: 0, size: 0.9, splat: [0xff9a2e, 0xffd36b], bits: [0xf07f1a, 0xd8681a, 0x4d7a2a] },
  balloons: { name: 'balloons', r: 1.8, h: 2.5, points: 150, chain: 0, size: 0.6, confetti: true, bits: [0xe53935, 0x1e88e5, 0xfdd835, 0x43a047, 0x8e24aa, 0xff7eb6] },
  castle: { name: 'castle', r: 4.2, h: 7, points: 300, chain: 0, size: 2.2, fire: true, bits: [0xb9b2a6, 0x8f897f, 0xd8d2c6, 0xc23b32] },
  bomb: { name: 'GIANT BOMB', r: 2.2, h: 4.4, points: 500, chain: 30, size: 3.2, fire: true, bits: [0x2a2a30, 0x4a4a52, 0xf2c94c, 0xff6a00] },
};

// Where they stand: across (m, + right) and down the range (yards); balloons float up
const LAYOUT = [
  ['crate', -7, 45], ['melon', 6, 50], ['tnt', -3, 62], ['pumpkin', 12, 70], ['tnt', 2, 75], ['crate', 4, 80],
  ['balloons', -14, 85], ['melon', -9, 98], ['tnt', 10, 105], ['tnt', 13, 109], ['crate', 16, 103], ['pumpkin', -2, 118],
  ['balloons', 9, 128], ['castle', -10, 145], ['tnt', 0, 158], ['crate', -3, 162], ['melon', 14, 170], ['pumpkin', -18, 178],
  ['balloons', -6, 190], ['tnt', 8, 200], ['crate', 11, 205], ['tnt', 5, 210], ['bomb', -2, 232], ['balloons', 16, 245],
];

// ------------------------------------------------------------------ the look of things

function canvasTexture(w, h, draw) {
  const c = document.createElement('canvas');
  c.width = w; c.height = h;
  draw(c.getContext('2d'), w, h);
  const t = new THREE.CanvasTexture(c);
  t.colorSpace = THREE.SRGBColorSpace;
  return t;
}
let looks = null;
function makeLooks() {
  if (looks) return looks;
  const tnt = canvasTexture(256, 128, (g, w, h) => {
    g.fillStyle = '#d8342c'; g.fillRect(0, 0, w, h);
    g.fillStyle = '#2b2b2b'; g.fillRect(0, 8, w, 10); g.fillRect(0, h - 18, w, 10);
    g.fillStyle = '#f2c94c'; g.fillRect(0, 44, w, 40);
    g.fillStyle = '#1b1b1b'; g.font = 'bold 34px sans-serif'; g.textAlign = 'center'; g.textBaseline = 'middle';
    for (const x of [w * 0.25, w * 0.75]) g.fillText('TNT', x, 65);
  });
  const crate = canvasTexture(128, 128, (g, w, h) => {
    g.fillStyle = '#c08a4e'; g.fillRect(0, 0, w, h);
    g.strokeStyle = '#7a4f24'; g.lineWidth = 3;
    for (let y = 0; y < h; y += 21) { g.beginPath(); g.moveTo(0, y); g.lineTo(w, y); g.stroke(); }
    g.lineWidth = 14; g.strokeStyle = '#8a5a2b'; g.strokeRect(7, 7, w - 14, h - 14);
    g.beginPath(); g.moveTo(10, 10); g.lineTo(w - 10, h - 10); g.stroke();
  });
  const melon = canvasTexture(256, 128, (g, w, h) => {
    g.fillStyle = '#3f9c45'; g.fillRect(0, 0, w, h);
    g.fillStyle = '#1f5c27';
    for (let i = 0; i < 12; i++) {
      g.beginPath();
      for (let y = 0; y <= h; y += 4) g.lineTo(i * (w / 12) + 6 * Math.sin(y / 9 + i), y);
      for (let y = h; y >= 0; y -= 4) g.lineTo(i * (w / 12) + 9 + 6 * Math.sin(y / 9 + i), y);
      g.fill();
    }
  });
  const std = (o) => new THREE.MeshStandardMaterial({ roughness: 0.7, ...o });
  looks = {
    tnt: std({ map: tnt, roughness: 0.5 }),
    tntTop: std({ color: 0x2b2b2b }),
    crate: std({ map: crate, roughness: 0.85 }),
    melon: std({ map: melon, roughness: 0.45 }),
    pumpkin: std({ color: 0xf07f1a, roughness: 0.55 }),
    stem: std({ color: 0x4d6a2a }),
    stone: std({ color: 0xbdb6aa, roughness: 0.9 }),
    roof: std({ color: 0xc23b32 }),
    bomb: std({ color: 0x24242a, roughness: 0.25, metalness: 0.3 }),
    fuse: std({ color: 0xcaa46a }),
    string: new THREE.MeshBasicMaterial({ color: 0xeeeeee }),
    balloon: [0xe53935, 0x1e88e5, 0xfdd835, 0x43a047, 0x8e24aa].map((c) => std({ color: c, roughness: 0.25 })),
    spark: new THREE.MeshBasicMaterial({ color: 0xffd36b }),
    bits: new Map(),
    box: new THREE.BoxGeometry(1, 1, 1),
    ball: new THREE.IcosahedronGeometry(1, 2),
    ring: new THREE.RingGeometry(0.85, 1, 48).rotateX(-Math.PI / 2),
    paper: new THREE.PlaneGeometry(1, 0.6),
  };
  return looks;
}
const bitMaterial = (c) => {
  const L = makeLooks();
  if (!L.bits.has(c)) L.bits.set(c, new THREE.MeshStandardMaterial({ color: c, roughness: 0.7, side: THREE.DoubleSide }));
  return L.bits.get(c);
};

// One thing to blow up, standing on the ground at the origin (balloons above it)
function model(kind) {
  const L = makeLooks(), g = new THREE.Group(), K = KINDS[kind];
  const mesh = (geo, mat, y = 0) => { const m = new THREE.Mesh(geo, mat); m.position.y = y; m.castShadow = true; g.add(m); return m; };
  if (kind === 'tnt') {
    mesh(new THREE.CylinderGeometry(K.r, K.r, K.h, 24), L.tnt, K.h / 2);
    mesh(new THREE.CylinderGeometry(K.r * 0.96, K.r * 0.96, 0.04, 24), L.tntTop, K.h + 0.02);
  } else if (kind === 'crate') {
    const s = 1.5;
    mesh(new THREE.BoxGeometry(s, s, s), L.crate, s / 2).rotation.y = rand(0, 1.5);
    mesh(new THREE.BoxGeometry(0.9, 0.9, 0.9), L.crate, s + 0.45).rotation.y = rand(0, 1.5);
  } else if (kind === 'melon') {
    const m = mesh(new THREE.SphereGeometry(1, 32, 20), L.melon, 0.75);
    m.scale.set(1.0, 0.78, 1.25);
  } else if (kind === 'pumpkin') {
    const geo = new THREE.SphereGeometry(1, 40, 24), p = geo.attributes.position;
    for (let i = 0; i < p.count; i++) {
      const x = p.getX(i), z = p.getZ(i), a = Math.atan2(z, x), k = 1 + 0.08 * Math.cos(a * 10);
      p.setXYZ(i, x * k * 1.15, p.getY(i) * 0.82, z * k * 1.15);
    }
    geo.computeVertexNormals();
    mesh(geo, L.pumpkin, 0.82);
    mesh(new THREE.CylinderGeometry(0.08, 0.13, 0.4, 8), L.stem, 1.75).rotation.z = 0.25;
  } else if (kind === 'balloons') {
    mesh(new THREE.CylinderGeometry(0.15, 0.2, 0.25, 10), L.stone, 0.12);
    g.userData.bob = new THREE.Group();
    g.add(g.userData.bob);
    for (let i = 0; i < 5; i++) {
      const a = (i / 5) * Math.PI * 2, top = new THREE.Vector3(Math.cos(a) * 0.7, 6.5 + (i % 2) * 0.6, Math.sin(a) * 0.7);
      const b = new THREE.Mesh(L.ball, L.balloon[i]);
      b.scale.set(0.55, 0.68, 0.55);
      b.position.copy(top);
      b.castShadow = true;
      g.userData.bob.add(b);
      // (its string, from the weight up to the balloon's knot)
      const from = new THREE.Vector3(0, 0.25, 0), to = top.clone().setY(top.y - 0.65), dir = to.clone().sub(from);
      const s = new THREE.Mesh(new THREE.CylinderGeometry(0.01, 0.01, dir.length(), 3), L.string);
      s.position.copy(from).addScaledVector(dir, 0.5);
      s.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), dir.normalize());
      g.userData.bob.add(s);
    }
  } else if (kind === 'castle') {
    // a keep of stacked blocks with four corner towers and a red roof
    const blocks = [];
    for (let y = 0; y < 5; y++) for (let i = -2; i <= 2; i++) for (const s of [-1, 1]) {
      blocks.push([i * 1.2 + (y % 2) * 0.3, y * 0.9 + 0.45, s * 2.4], [s * 2.4, y * 0.9 + 0.45, i * 1.2 + (y % 2) * 0.3]);
    }
    for (const [cx, cz] of [[-2.6, -2.6], [2.6, -2.6], [-2.6, 2.6], [2.6, 2.6]]) {
      mesh(new THREE.CylinderGeometry(0.9, 1.0, 6.2, 12), L.stone, 3.1).position.set(cx, 3.1, cz);
      mesh(new THREE.ConeGeometry(1.15, 1.6, 12), L.roof, 7).position.set(cx, 7, cz);
    }
    const inst = new THREE.InstancedMesh(new THREE.BoxGeometry(1.15, 0.85, 1.15), L.stone, blocks.length);
    const m4 = new THREE.Matrix4();
    blocks.forEach(([x, y, z], i) => inst.setMatrixAt(i, m4.makeTranslation(x, y, z)));
    inst.castShadow = true;
    g.add(inst);
  } else if (kind === 'bomb') {
    mesh(new THREE.SphereGeometry(K.r, 40, 28), L.bomb, K.r);
    mesh(new THREE.CylinderGeometry(0.45, 0.45, 0.5, 16), L.bomb, K.r * 2 + 0.1);
    const fuse = mesh(new THREE.CylinderGeometry(0.07, 0.07, 1.2, 8), L.fuse, K.r * 2 + 0.75);
    fuse.rotation.z = 0.35;
    const spark = new THREE.Mesh(L.ball, L.spark);
    spark.scale.setScalar(0.18);
    spark.position.set(-0.2, K.r * 2 + 1.35, 0);
    g.add(spark);
    g.userData.spark = spark;
  }
  return g;
}

// ------------------------------------------------------------------ explosions

// Everything flying about after a bang: each piece { mesh, v, spin, life, kind } moved per frame
class Effects {
  constructor(group, heightAt) {
    this.group = group;
    this.heightAt = heightAt;
    this.parts = [];
  }

  add(mesh, o) { this.group.add(mesh); this.parts.push({ mesh, age: 0, ...o }); }

  // A bang at course (x, y, z): K the kind's look
  explode(x, y, z, K) {
    const L = makeLooks(), at = T(x, y, z), s = K.size;
    // the flash
    if (K.fire) {
      const flash = new THREE.Mesh(L.ball, new THREE.MeshBasicMaterial({ color: 0xfff1b8, transparent: true, depthWrite: false, blending: THREE.AdditiveBlending }));
      flash.position.copy(at);
      this.add(flash, { type: 'flash', life: 0.35, grow: 3.2 * s });
      // the fireball: puffs of flame turning to smoke as they rise
      for (let i = 0; i < 10 + 6 * s; i++) {
        const m = new THREE.Mesh(L.ball, new THREE.MeshBasicMaterial({ color: 0xffd36b, transparent: true, depthWrite: false }));
        m.position.copy(at).add(new THREE.Vector3(rand(-1, 1), rand(-0.3, 1), rand(-1, 1)).multiplyScalar(s * 0.8));
        this.add(m, { type: 'fire', life: rand(1.2, 2.2), v: new THREE.Vector3(rand(-1.5, 1.5), rand(2, 6), rand(-1.5, 1.5)).multiplyScalar(s * 0.7), grow: rand(1.2, 2.2) * s });
      }
    }
    // splats: a burst of juice
    if (K.splat) {
      for (let i = 0; i < 26; i++) {
        const m = new THREE.Mesh(L.ball, bitMaterial(pick(K.splat)));
        m.scale.setScalar(rand(0.08, 0.22) * s);
        m.position.copy(at);
        const a = rand(0, Math.PI * 2), up = rand(3, 9);
        this.add(m, { type: 'bit', life: rand(2, 3.5), v: new THREE.Vector3(Math.cos(a) * rand(3, 9), up, Math.sin(a) * rand(3, 9)), spin: new THREE.Vector3() });
      }
    }
    // confetti from balloons: slow, fluttering
    if (K.confetti) {
      for (let i = 0; i < 70; i++) {
        const m = new THREE.Mesh(L.paper, bitMaterial(pick(K.bits)));
        m.scale.setScalar(rand(0.15, 0.3));
        m.position.copy(at).add(new THREE.Vector3(rand(-1, 1), rand(-0.5, 1), rand(-1, 1)));
        this.add(m, { type: 'confetti', life: rand(3, 5), v: new THREE.Vector3(rand(-4, 4), rand(1, 5), rand(-4, 4)), spin: new THREE.Vector3(rand(-6, 6), rand(-6, 6), rand(-6, 6)) });
      }
    }
    // bits of the thing flying everywhere, bouncing where they come down
    const n = K.confetti ? 6 : Math.round(16 + 10 * s);
    for (let i = 0; i < n; i++) {
      const m = new THREE.Mesh(L.box, bitMaterial(pick(K.bits)));
      m.scale.set(rand(0.15, 0.5), rand(0.1, 0.3), rand(0.15, 0.5)).multiplyScalar(Math.sqrt(s));
      m.position.copy(at);
      m.castShadow = true;
      const a = rand(0, Math.PI * 2), out = rand(4, 13) * Math.sqrt(s);
      this.add(m, { type: 'bit', life: rand(3, 4.5), v: new THREE.Vector3(Math.cos(a) * out, rand(5, 15) * Math.sqrt(s), Math.sin(a) * out), spin: new THREE.Vector3(rand(-9, 9), rand(-9, 9), rand(-9, 9)) });
    }
    // a ring racing out across the ground
    const g = this.heightAt(x, z) ?? y;
    const ring = new THREE.Mesh(L.ring, new THREE.MeshBasicMaterial({ color: K.fire ? 0xffe2a0 : 0xffffff, transparent: true, depthWrite: false, side: THREE.DoubleSide }));
    ring.position.copy(T(x, g + 0.08, z));
    this.add(ring, { type: 'ring', life: 0.7, grow: 9 * s });
  }

  update(dt) {
    const keep = [];
    for (const p of this.parts) {
      p.age += dt;
      const k = p.age / p.life, m = p.mesh;
      if (k >= 1) {
        this.group.remove(m);
        if (p.type === 'flash' || p.type === 'fire' || p.type === 'ring') m.material.dispose();
        continue;
      }
      keep.push(p);
      if (p.type === 'flash') {
        m.scale.setScalar(p.grow * Math.min(1, k * 4));
        m.material.opacity = 1 - k;
      } else if (p.type === 'ring') {
        m.scale.setScalar(0.5 + p.grow * Math.sqrt(k));
        m.material.opacity = 0.8 * (1 - k);
      } else if (p.type === 'fire') {
        p.v.multiplyScalar(1 - 1.5 * dt);
        p.v.y += 1.2 * dt;
        m.position.addScaledVector(p.v, dt);
        m.scale.setScalar(p.grow * (0.35 + 0.65 * Math.sqrt(k)));
        // yellow, orange, red, then dark smoke
        const c = m.material.color;
        if (k < 0.15) c.setHex(0xfff0a0); else if (k < 0.3) c.setHex(0xffa22e); else if (k < 0.45) c.setHex(0xe0451c); else c.setRGB(0.25 - k * 0.1, 0.24 - k * 0.1, 0.25 - k * 0.1);
        m.material.opacity = k < 0.45 ? 0.95 : 0.85 * (1 - (k - 0.45) / 0.55);
      } else {
        // bits, splats and confetti: thrown, falling, bouncing on the ground
        const flutter = p.type === 'confetti';
        p.v.y -= (flutter ? 2.2 : 14) * dt;
        if (flutter) p.v.multiplyScalar(1 - 1.8 * dt);
        m.position.addScaledVector(p.v, dt);
        if (p.spin) { m.rotation.x += p.spin.x * dt; m.rotation.y += p.spin.y * dt; m.rotation.z += p.spin.z * dt; }
        const ground = (this.heightAt(-m.position.x, m.position.z) ?? 0) + 0.05;
        if (m.position.y < ground) {
          m.position.y = ground;
          if (flutter) { p.v.set(0, 0, 0); p.spin?.set(0, 0, 0); } else { p.v.y = Math.abs(p.v.y) * 0.35; p.v.x *= 0.6; p.v.z *= 0.6; p.spin?.multiplyScalar(0.6); }
        }
        if (k > 0.8) m.scale.multiplyScalar(1 - dt * 3);
      }
    }
    this.parts = keep;
  }

  clear() { for (const p of this.parts) this.group.remove(p.mesh); this.parts = []; }
}

// ------------------------------------------------------------------ the game

export function blastRange(base, units = 'yd') {
  const U = units === 'm' ? 1 : YD;
  const spot = Object.create(base);
  let course = null, scene = null, effects = null, group = null, last = 0, shake = 0, level = 'easy';
  const queue = []; // explosions waiting to go off (chain reactions): { t, target }
  const bestKey = () => `golfsim.boom.best.${level}`;
  const readBest = () => { try { return +localStorage.getItem(bestKey()) || 0; } catch { return 0; } };
  spot.game = { score: 0, ball: 0, best: 0, shots: [], shot: null };
  spot.targets = [];
  spot.level = () => BLAST_LEVELS[level];
  spot.balls = () => BLAST_LEVELS[level].balls;
  spot.onBoom = null; // (target, points, chain) for the banner

  const place = () => {
    for (const t of spot.targets) group.remove(t.obj);
    const L = BLAST_LEVELS[level], k = L.size;
    spot.targets = LAYOUT.map(([kind, x, d], i) => {
      const z = d * U, K0 = KINDS[kind], y = course.heightAt(x, z) ?? 2;
      // (bigger on the easier levels: the thing, its bang and what it sets off around it)
      const K = { ...K0, r: K0.r * k, h: K0.h * k, size: K0.size * Math.sqrt(k), chain: K0.chain * Math.sqrt(k) };
      const obj = model(kind);
      obj.scale.setScalar(k);
      obj.position.copy(T(x, y, z));
      obj.rotation.y = rand(0, Math.PI * 2);
      group.add(obj);
      // (the middle of what you aim at: balloons up in the air)
      const cy = kind === 'balloons' ? y + 6.8 * k : y + K.h / 2;
      return { i, kind, K, x, z, y, cy, obj, alive: true, colour: `#${(K.bits[0]).toString(16).padStart(6, '0')}`, label: K.name };
    });
  };

  spot.decorate = function (sc, c) {
    base.decorate.call(this, sc, c);
    this.flags?.forEach((f) => (f.visible = false));
    scene = sc; course = c;
    group = new THREE.Group();
    sc.courseGroup.add(group);
    effects = new Effects(group, (x, z) => course.heightAt(x, z));
    place();
    sc.applyEnvIntensity(group);
  };

  const blow = (t, chain) => {
    if (!t.alive) return;
    t.alive = false;
    group.remove(t.obj);
    effects.explode(t.x, t.cy, t.z, t.K);
    boomSound(t.K.fire ? t.K.size : 0.4, !t.K.fire);
    shake = Math.max(shake, Math.min(1, 0.25 * t.K.size));
    const pts = t.K.points * (chain + 1);
    spot.game.score += pts;
    if (spot.game.shot) { spot.game.shot.points += pts; spot.game.shot.hits.push(t.K.name); }
    spot.onBoom?.(t, pts, chain);
    // what a barrel or the bomb sets off nearby, a moment later
    if (t.K.chain) {
      for (const o of spot.targets) {
        const d = Math.hypot(o.x - t.x, o.z - t.z);
        if (o.alive && o !== t && d < t.K.chain && !queue.some((q) => q.target === o)) queue.push({ t: last + 0.12 + d * 0.025, target: o, chain: chain + 1 });
      }
    }
  };

  // Called as the ball moves from a to b (course coordinates, either may be null)
  spot.ballAt = (a, b) => {
    if (!a || !b) return;
    const dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z, l2 = dx * dx + dy * dy + dz * dz || 1e-9;
    for (const t of spot.targets) {
      if (!t.alive) continue;
      // nearest point of the step to the target's middle, against a generous reach
      const s = Math.max(0, Math.min(1, ((t.x - a.x) * dx + (t.cy - a.y) * dy + (t.z - a.z) * dz) / l2));
      const px = a.x + dx * s - t.x, py = a.y + dy * s - t.cy, pz = a.z + dz * s - t.z;
      const L = BLAST_LEVELS[level], reach = t.K.r + L.reach;
      if (px * px + pz * pz < reach * reach && Math.abs(py) < t.K.h / 2 + L.reach + 0.3) blow(t, 0);
    }
  };

  // Where the ball came down and stopped: anything close by goes off too (a near miss still counts)
  spot.landed = (p) => {
    if (!p) return;
    const land = BLAST_LEVELS[level].land;
    for (const t of spot.targets) if (t.alive && Math.hypot(p.x - t.x, p.z - t.z) < t.K.r + land) blow(t, 0);
  };

  spot.startShot = () => { spot.game.shot = { points: 0, hits: [] }; };
  spot.endShot = () => {
    const g = spot.game, s = g.shot || { points: 0, hits: [] };
    g.ball++;
    g.shots.push(s);
    g.shot = null;
    const over = g.ball >= spot.balls() || !spot.targets.some((t) => t.alive);
    if (over && g.score > g.best) { g.best = g.score; try { localStorage.setItem(bestKey(), String(g.best)); } catch { /* storage unavailable */ } }
    return { ...s, over };
  };
  // A new game, at a level ('easy' | 'medium' | 'hard'; the same one again if not given)
  spot.newGame = (lv) => {
    if (BLAST_LEVELS[lv]) level = lv;
    spot.game = { score: 0, ball: 0, best: readBest(), shots: [], shot: null };
    queue.length = 0;
    effects.clear();
    place();
  };
  spot.left = () => spot.targets.filter((t) => t.alive).length;

  spot.update = function (clock) {
    base.update?.call(this, clock);
    const dt = Math.min(0.1, Math.max(0, clock - last));
    last = clock;
    if (!effects) return;
    for (let i = queue.length - 1; i >= 0; i--) if (queue[i].t <= clock) { const q = queue.splice(i, 1)[0]; blow(q.target, q.chain); }
    effects.update(dt);
    // balloons bob, the bomb's fuse fizzes
    for (const t of spot.targets) {
      if (!t.alive) continue;
      const u = t.obj.userData;
      if (u.bob) { u.bob.position.y = 0.25 * Math.sin(clock * 1.3 + t.i); u.bob.rotation.y = 0.2 * Math.sin(clock * 0.5 + t.i); }
      if (u.spark) u.spark.scale.setScalar(0.14 + 0.08 * Math.abs(Math.sin(clock * 23 + Math.sin(clock * 7))));
    }
    // the camera shakes with a big bang
    if (shake > 0.01 && scene) {
      scene.camPos.add(new THREE.Vector3(rand(-1, 1), rand(-1, 1), rand(-1, 1)).multiplyScalar(shake * 0.35));
      shake *= Math.pow(0.02, dt);
    }
  };
  return spot;
}
