// Grass for the rough: tufts of curved, tapering blades drawn as instanced meshes, and a material
// that lights both faces of a blade alike and sways the tips in the wind.
// A tuft is built about 1 unit tall; each instance scales it to its real height.

import * as THREE from 'three/webgpu';
import { float, vec3, sin, cos, time, instanceIndex, positionLocal, uniform, attribute } from 'three/tsl';
import { mergeGeometries } from 'three/addons/utils/BufferGeometryUtils.js';

// Wind speed (m/s) the grass sways in; set from the weather
export const grassWind = uniform(0);

const rnd = ([a, b]) => a + Math.random() * (b - a);

// One tuft: `blades` blades rising from a small clump, each bending over in its own direction.
// Sizes are relative to the tuft's height: spread = clump radius, width = blade width at the base.
export function tuftGeometry({ blades = 9, spread = 0.3, lean = [0.15, 0.55], height = [0.55, 1.05], width = [0.05, 0.09], dry = 0.15 } = {}) {
  const parts = [];
  for (let i = 0; i < blades; i++) {
    const a = (i / blades) * Math.PI * 2 + Math.random() * 0.8;
    const rr = Math.sqrt(Math.random()) * spread;
    const bx = Math.cos(a) * rr, bz = Math.sin(a) * rr;
    // Mostly bending away from the middle of the clump, some across it
    const d = a + (Math.random() - 0.5) * 1.4, dx = Math.cos(d), dz = Math.sin(d);
    const L = rnd(lean), H = rnd(height), W = rnd(width);
    // Point along the blade (t = 0 root, 1 tip): the bend grows towards the tip, which droops a little
    const at = (t, side) => {
      const w = W * (1 - t) ** 0.8 * side * 0.5;
      return [bx + dx * L * t * t - dz * w, H * t * (1 - 0.2 * L * t), bz + dz * L * t * t + dx * w];
    };
    const b0 = at(0, -1), b1 = at(0, 1), m0 = at(0.55, -1), m1 = at(0.55, 1), tip = at(1, 0);
    const pos = [...b0, ...b1, ...m1, ...b0, ...m1, ...m0, ...m0, ...m1, ...tip];
    // Shaded at the root (inside the clump), lighter towards the tip; some blades dried at the tips
    const v = 0.9 + Math.random() * 0.2, isDry = Math.random() < dry;
    const root = [0.42 * v, 0.45 * v, 0.38 * v], mid = [0.82 * v, 0.86 * v, 0.76 * v];
    const top = isDry ? [1.2, 1.08, 0.72] : [1.05 * v, 1.1 * v, 0.95 * v];
    const col = [...root, ...root, ...mid, ...root, ...mid, ...mid, ...mid, ...mid, ...top];
    // Normals mostly up (as for a lawn), tilted towards the way the blade bends
    const n = new THREE.Vector3(dx * 0.35, 1, dz * 0.35).normalize();
    const nor = []; for (let k = 0; k < 9; k++) nor.push(n.x, n.y, n.z);
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
    g.setAttribute('normal', new THREE.Float32BufferAttribute(nor, 3));
    g.setAttribute('color', new THREE.Float32BufferAttribute(col, 3));
    parts.push(g);
  }
  return addBend(mergeGeometries(parts));
}

// A clump of reeds about 1 unit tall: long, narrow, nearly upright blades, coloured as they are
// (the instance colour only varies the shade). With cattails, a few bare stalks carry the brown
// seed heads above the leaves.
export function reedGeometry({ blades = 12, cattails = 0 } = {}) {
  const parts = [];
  const add = (pos, col) => {
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
    g.setAttribute('color', new THREE.Float32BufferAttribute(col, 3));
    parts.push(g);
  };
  for (let i = 0; i < blades; i++) {
    const a = Math.random() * Math.PI * 2, rr = Math.sqrt(Math.random()) * 0.12;
    const bx = Math.cos(a) * rr, bz = Math.sin(a) * rr;
    const d = a + (Math.random() - 0.5), dx = Math.cos(d), dz = Math.sin(d);
    const H = 0.6 + Math.random() * 0.4, L = 0.04 + Math.random() * 0.14, W = 0.018 + Math.random() * 0.01;
    const at = (t, side) => {
      const w = W * (1 - t) ** 0.7 * side * 0.5;
      return [bx + dx * L * t * t - dz * w, H * t, bz + dz * L * t * t + dx * w];
    };
    const b0 = at(0, -1), b1 = at(0, 1), m0 = at(0.6, -1), m1 = at(0.6, 1), tip = at(1, 0);
    // Pale where it comes out of the water, green up the blade, some tips browned
    const g = 0.85 + Math.random() * 0.25, low = [0.42 * g, 0.44 * g, 0.28 * g], mid = [0.3 * g, 0.42 * g, 0.16 * g];
    const top = Math.random() < 0.25 ? [0.55, 0.48, 0.28] : [0.36 * g, 0.48 * g, 0.18 * g];
    add([...b0, ...b1, ...m1, ...b0, ...m1, ...m0, ...m0, ...m1, ...tip], [...low, ...low, ...mid, ...low, ...mid, ...mid, ...mid, ...mid, ...top]);
  }
  for (let i = 0; i < cattails; i++) {
    // Stalk (a thin upright strip, crossed so it shows from every side) and a brown head on top
    const bx = (Math.random() - 0.5) * 0.12, bz = (Math.random() - 0.5) * 0.12, H = 1 + Math.random() * 0.25;
    const lean = [(Math.random() - 0.5) * 0.06, (Math.random() - 0.5) * 0.06];
    const stalk = [0.32, 0.38, 0.2];
    for (const [ux, uz] of [[1, 0], [0, 1]]) {
      const w = 0.006;
      const p = (y, s) => [bx + lean[0] * y + ux * w * s, y, bz + lean[1] * y + uz * w * s];
      add([...p(0, -1), ...p(0, 1), ...p(H, 1), ...p(0, -1), ...p(H, 1), ...p(H, -1)], [...stalk, ...stalk, ...stalk, ...stalk, ...stalk, ...stalk]);
    }
    const head = new THREE.CylinderGeometry(0.018, 0.018, 0.14, 6, 1).toNonIndexed();
    head.translate(bx + lean[0] * (H - 0.12), H - 0.12, bz + lean[1] * (H - 0.12));
    head.deleteAttribute('uv'); head.deleteAttribute('normal');
    const brown = [0.3, 0.17, 0.08], hc = [];
    for (let k = 0; k < head.attributes.position.count; k++) hc.push(...brown);
    head.setAttribute('color', new THREE.Float32BufferAttribute(hc, 3));
    parts.push(head);
  }
  const geo = addBend(mergeGeometries(parts));
  geo.computeVertexNormals();
  return geo;
}

// How much each vertex moves in the wind: 0 at the roots, 1 at the tallest tips (height²)
export function addBend(geo) {
  const pos = geo.attributes.position, bend = new Float32Array(pos.count);
  let top = 1e-6;
  for (let i = 0; i < pos.count; i++) top = Math.max(top, pos.getY(i));
  for (let i = 0; i < pos.count; i++) bend[i] = (Math.max(0, pos.getY(i)) / top) ** 2;
  geo.setAttribute('bend', new THREE.Float32BufferAttribute(bend, 1));
  return geo;
}

// An instanced mesh of tufts with its per-tuft colours in place from the start (a mesh first drawn
// without them, e.g. while empty, keeps a shader that ignores them and shows bare grey-white blades)
export function grassMesh(geo, mat, count) {
  const mesh = new THREE.InstancedMesh(geo, mat, count);
  mesh.instanceColor = new THREE.InstancedBufferAttribute(new Float32Array(count * 3), 3);
  mesh.receiveShadow = true;
  return mesh;
}

// sway: how far (m) the tips move in calm air; geometry needs a `bend` attribute (addBend)
export function grassMaterial({ sway = 0.006 } = {}) {
  const m = new THREE.MeshStandardNodeMaterial({ vertexColors: true, side: THREE.DoubleSide, roughness: 1, metalness: 0 });
  m.envMapIntensity = 0.35; // (reflecting more sky turns the blades blue-green against the ground)
  // Sway: gusts roll across the grass as waves (by position; three.js has already placed each tuft
  // in the scene when positionNode runs, so positionLocal is in metres), plus a little of each
  // tuft's own rhythm. Further and faster in wind.
  const p = positionLocal;
  const own = float(instanceIndex).mul(1.618).fract().mul(1.5);
  const t = time.mul(grassWind.mul(0.12).add(1.2)).add(p.x.mul(0.35)).add(p.z.mul(0.27)).add(own);
  const amp = grassWind.mul(0.35).add(1).min(4).mul(sway);
  m.positionNode = p.add(vec3(sin(t), 0, cos(t.mul(0.83).add(1.3))).mul(attribute('bend', 'float').mul(amp)));
  return m;
}
