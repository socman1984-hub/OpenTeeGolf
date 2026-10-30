// Round toy trees for the Cartoon graphics style: a short brown trunk under a soft, lumpy ball of
// leaves (broadleaf trees), stacked cones (pines, spruces, firs, cedars), a tall rounded column
// (cypresses, poplars), a fan of fronds on a slim trunk (palms) or a plain green cactus; flowering
// trees (dogwood, crape myrtle) in their blossom colours. Each shape
// is one instanced mesh of a few dozen triangles, so tens of thousands of trees cost little: no
// leaf textures, no distant pictures. Lumps are lit with normals pointing out from the middle of
// the crown, so a handful of flat faces still shades as one smooth, rounded mass.
//
// Trees near the camera get smoother shapes than the rest (update moves them across as it moves).
// Same use as trees.js's Forest: build(trees, heightAt), update(camera), group.
import * as THREE from 'three/webgpu';

const CONE = new Set(['pine', 'spruce', 'loblolly', 'redcedar', 'douglasfir', 'baldcypress', 'hemlock', 'holly']);
const COLUMN = new Set(['cypress', 'poplar']);
const CACTUS = new Set(['saguaro']);
const PALM = new Set(['palm']);
// (leaf colours: bright, warm greens for broadleaf trees, deeper blue-greens for conifers)
const LEAF = [0x2e6e22, 0x3a7d28, 0x285f1e, 0x467f2c, 0x23571d, 0x4f8a30];
const NEEDLE = [0x1f5a2e, 0x1a4d28, 0x266636];
// Species with colours of their own: flowering dogwoods and crape myrtles in bloom, silvery olives,
// blue-grey junipers, dark glossy hollies
const SPECIES_COLOURS = {
  dogwood: [0xf2eee4, 0xf0dde6, 0xe6e4d2], crapemyrtle: [0xe0609e, 0xd2559a, 0xb46ec4], olive: [0x8d9e72, 0x99a882, 0x7f9268],
  juniper: [0x5d7e6c, 0x6a8a78, 0x55705f], holly: [0x174a26, 0x1c5229],
};
const hash = (x, z, k) => { const s = Math.sin(x * 12.9898 + z * 78.233 + k * 37.719) * 43758.5453; return s - Math.floor(s); };

// Normals pointing out from c (so the lumps shade as one rounded crown)
function roundNormals(geo, c) {
  const p = geo.attributes.position, n = new Float32Array(p.count * 3), v = new THREE.Vector3();
  for (let i = 0; i < p.count; i++) {
    v.set(p.getX(i) - c.x, (p.getY(i) - c.y) * 0.8, p.getZ(i) - c.z).normalize();
    n.set([v.x, v.y, v.z], i * 3);
  }
  geo.setAttribute('normal', new THREE.BufferAttribute(n, 3));
  return geo;
}
// Several geometries as one (positions and normals only)
function merge(parts) {
  const geos = parts.map((g) => g.index ? g.toNonIndexed() : g);
  let n = 0;
  for (const g of geos) n += g.attributes.position.count;
  const pos = new Float32Array(n * 3), nor = new Float32Array(n * 3);
  let o = 0;
  for (const g of geos) { pos.set(g.attributes.position.array, o * 3); nor.set(g.attributes.normal.array, o * 3); o += g.attributes.position.count; }
  const out = new THREE.BufferGeometry();
  out.setAttribute('position', new THREE.BufferAttribute(pos, 3));
  out.setAttribute('normal', new THREE.BufferAttribute(nor, 3));
  return out;
}

// Crown shapes, 1 unit across and centred at (0, 0, 0) unless said otherwise
const SHAPES = {
  // a ball of five lumps, wider than tall, with smooth rounded light
  round(near) {
    const lumps = [[0, 0.05, 0, 0.42], [0.22, -0.08, 0.1, 0.3], [-0.2, -0.06, -0.12, 0.32], [0.04, 0.2, -0.16, 0.28], [-0.12, 0.12, 0.2, 0.27]];
    return roundNormals(merge(lumps.map(([x, y, z, r]) => new THREE.IcosahedronGeometry(r, near ? 2 : 0).translate(x, y, z))), { x: 0, y: -0.05, z: 0 });
  },
  // three stacked cones from y = 0 to 1, base 1 across
  cone(near) {
    const tiers = [[0, 0.55, 0.5], [0.3, 0.45, 0.4], [0.58, 0.42, 0.29]];
    return merge(tiers.map(([y, h, r]) => new THREE.ConeGeometry(r, h, near ? 16 : 8, 1, true).translate(0, y + h / 2, 0)));
  },
  // a tall rounded column, y = 0 to 1
  column(near) {
    const g = new THREE.SphereGeometry(0.5, near ? 16 : 8, near ? 12 : 6).scale(0.32, 1, 0.32).translate(0, 0.5, 0);
    return roundNormals(g, { x: 0, y: 0.5, z: 0 });
  },
  // palm fronds: seven drooping blades round the top, at y = 0
  palm() {
    const blades = [];
    for (let i = 0; i < 7; i++) {
      const b = new THREE.PlaneGeometry(0.5, 0.12, 4, 1).translate(0.25, 0, 0);
      const p = b.attributes.position;
      for (let k = 0; k < p.count; k++) { const x = p.getX(k); p.setY(k, p.getY(k) * 0 - x * x * 0.5); p.setZ(k, (k % 2 ? 1 : -1) * 0.06 * (1 - x * 1.6)); }
      b.rotateY((i / 7) * Math.PI * 2);
      b.computeVertexNormals();
      blades.push(b);
    }
    return merge(blades);
  },
  // a saguaro: a column with two arms
  cactus() {
    const body = new THREE.CapsuleGeometry(0.12, 0.76, 4, 8).translate(0, 0.5, 0);
    const armL = new THREE.CapsuleGeometry(0.07, 0.25, 4, 6).translate(-0.22, 0.6, 0);
    const armR = new THREE.CapsuleGeometry(0.07, 0.2, 4, 6).translate(0.22, 0.48, 0);
    const elbowL = new THREE.CapsuleGeometry(0.07, 0.12, 4, 6).rotateZ(Math.PI / 2).translate(-0.14, 0.45, 0);
    const elbowR = new THREE.CapsuleGeometry(0.07, 0.1, 4, 6).rotateZ(Math.PI / 2).translate(0.14, 0.36, 0);
    return merge([body, armL, armR, elbowL, elbowR]);
  },
};

const NEAR = 170, NEAR_MAX = 1500; // (m; how many trees at most get the smoother shapes)

export class ToonForest {
  constructor() {
    this.group = new THREE.Group();
    this.quality = 'performance';
    this.trees = null;
    this.sets = [];
    this.lastCam = null;
  }

  async prepare() {}

  build(trees, heightAt, envApply = () => {}) {
    this.group.clear();
    this.trees = trees;
    this.sets = [];
    this.lastCam = null;
    const by = { round: [], cone: [], column: [], palm: [], cactus: [] };
    for (const t of trees) {
      const shape = CONE.has(t.kind) ? 'cone' : COLUMN.has(t.kind) ? 'column' : PALM.has(t.kind) ? 'palm' : CACTUS.has(t.kind) ? 'cactus' : 'round';
      by[shape].push(t);
    }
    const leafMat = new THREE.MeshStandardMaterial({ roughness: 0.85, metalness: 0, side: THREE.DoubleSide });
    const trunkMat = new THREE.MeshStandardMaterial({ color: 0x7a5233, roughness: 0.9 });
    envApply(leafMat); envApply(trunkMat);
    const trunkGeo = new THREE.CylinderGeometry(0.5, 0.7, 1, 6, 1, true).translate(0, 0.5, 0);
    const m4 = new THREE.Matrix4(), q = new THREE.Quaternion(), s = new THREE.Vector3(), p = new THREE.Vector3(), c = new THREE.Color(), up = new THREE.Vector3(0, 1, 0);
    const trunks = [];
    for (const [shape, list] of Object.entries(by)) {
      if (!list.length) continue;
      // each tree's placement and colour, kept to be shared out between the near and far meshes
      const mats = new Float32Array(list.length * 16), cols = new Float32Array(list.length * 3), xz = new Float32Array(list.length * 2);
      list.forEach((t, i) => {
        const y = heightAt(t.x, t.z) ?? 0, h = Math.max(3, t.h || 10), r = Math.max(1.2, t.r || h * 0.3);
        const turn = hash(t.x, t.z, 1) * Math.PI * 2, k = 0.9 + hash(t.x, t.z, 2) * 0.2;
        q.setFromAxisAngle(up, turn);
        // (where the crown sits, and how tall a trunk shows under it)
        let trunkH = 0, trunkW = 0;
        if (shape === 'round') {
          const w = r * 2.1 * k, ch = Math.min(h * 0.75, w * 0.85);
          p.set(-t.x, y + h - ch * 0.5, t.z); s.set(w, ch * 1.15, w);
          trunkH = h - ch * 0.75; trunkW = Math.max(0.25, h * 0.028);
        } else if (shape === 'cone') {
          const w = Math.max(r * 2, h * 0.4) * k;
          p.set(-t.x, y + h * 0.12, t.z); s.set(w, h * 0.9, w);
          trunkH = h * 0.18; trunkW = Math.max(0.2, h * 0.022);
        } else if (shape === 'column') {
          p.set(-t.x, y + h * 0.08, t.z); s.set(h * 0.9 * k, h * 0.94, h * 0.9 * k);
          trunkH = h * 0.12; trunkW = Math.max(0.15, h * 0.02);
        } else if (shape === 'palm') {
          const w = Math.max(5, r * 2.4);
          p.set(-t.x, y + h, t.z); s.set(w, w, w);
          trunkH = h; trunkW = Math.max(0.18, h * 0.02);
        } else {
          p.set(-t.x, y, t.z); s.set(h, h, h);
        }
        m4.compose(p, q, s).toArray(mats, i * 16);
        const pal = SPECIES_COLOURS[t.kind] || (shape === 'cone' ? NEEDLE : shape === 'cactus' ? [0x5a9a4a] : LEAF);
        c.setHex(pal[Math.floor(hash(t.x, t.z, 3) * pal.length)]).toArray(cols, i * 3);
        xz[i * 2] = -t.x; xz[i * 2 + 1] = t.z;
        if (trunkH > 0.3) trunks.push([t.x, y, t.z, trunkH, trunkW]);
      });
      const mesh = (geo, n) => {
        const m = new THREE.InstancedMesh(geo, leafMat, n);
        m.instanceColor = new THREE.InstancedBufferAttribute(new Float32Array(n * 3), 3);
        m.castShadow = true; m.receiveShadow = true;
        m.frustumCulled = false; m.count = 0;
        this.group.add(m);
        return m;
      };
      this.sets.push({ n: list.length, mats, cols, xz, near: mesh(SHAPES[shape](true), Math.min(list.length, NEAR_MAX)), far: mesh(SHAPES[shape](false), list.length) });
    }
    if (trunks.length) {
      const mesh = new THREE.InstancedMesh(trunkGeo, trunkMat, trunks.length);
      trunks.forEach(([x, y, z, h, w], i) => mesh.setMatrixAt(i, m4.compose(p.set(-x, y - 0.3, z), q.identity(), s.set(w, h + 0.3, w))));
      mesh.castShadow = true;
      mesh.computeBoundingSphere();
      this.group.add(mesh);
    }
  }

  buildShrubs() {}

  // Trees within NEAR of the camera to the smooth shapes, the rest to the simple ones (only again
  // once the camera has moved a little)
  update(camera, force = false) {
    if (!this.sets.length) return;
    const cam = camera.position;
    if (!force && this.lastCam && Math.hypot(cam.x - this.lastCam.x, cam.z - this.lastCam.z) < 15) return;
    this.lastCam = { x: cam.x, z: cam.z };
    for (const S of this.sets) {
      let nn = 0, nf = 0;
      const nm = S.near.instanceMatrix.array, nc = S.near.instanceColor.array, fm = S.far.instanceMatrix.array, fc = S.far.instanceColor.array, cap = S.near.instanceMatrix.count;
      for (let i = 0; i < S.n; i++) {
        const near = nn < cap && Math.hypot(S.xz[i * 2] - cam.x, S.xz[i * 2 + 1] - cam.z) < NEAR;
        if (near) { nm.set(S.mats.subarray(i * 16, i * 16 + 16), nn * 16); nc.set(S.cols.subarray(i * 3, i * 3 + 3), nn * 3); nn++; }
        else { fm.set(S.mats.subarray(i * 16, i * 16 + 16), nf * 16); fc.set(S.cols.subarray(i * 3, i * 3 + 3), nf * 3); nf++; }
      }
      S.near.count = nn; S.far.count = nf;
      for (const m of [S.near, S.far]) { m.instanceMatrix.needsUpdate = true; m.instanceColor.needsUpdate = true; }
    }
  }

  triangles() {
    let n = 0;
    for (const m of this.group.children) n += ((m.geometry.index?.count ?? m.geometry.attributes.position.count) / 3) * m.count;
    return n;
  }
}
