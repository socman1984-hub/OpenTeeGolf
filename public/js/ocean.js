// Ocean waves from a wave spectrum, worked out on the graphics card every frame (Tessendorf's
// method, "Simulating Ocean Water", 2001). The sea is the sum of a great many waves of every
// length and direction, their heights drawn from a measured sea spectrum (JONSWAP) for the wind
// and a long swell from the open sea. Each frame every wave is moved on by its own speed (longer
// waves travel faster) and one inverse FFT per detail level turns them all into a patch of sea
// that repeats seamlessly: the height, the sideways push that sharpens the crests ("choppy"
// waves), the slopes for lighting, and where the crests fold over, which is where foam starts.
//
// The GPU FFT follows the approach of WebTide by Barthélemy Paléologue (MIT licence,
// https://github.com/BarthPaleologue/WebTide): a spectrum pass, radix-2 butterfly passes along the
// rows then the columns (Stockham order, so no bit reversal), then a sign flip for the centred
// spectrum. Written again here for three.js TSL: each row or column is transformed whole inside
// one workgroup in shared memory (two passes over the data rather than one per step), the
// spectrum and the finishing touches are folded into those two passes, and the eight wave fields
// are packed two to a complex number.
//
// Detail levels ("cascades"): patches of different sizes, each covering its own band of wave
// lengths, so the sea has long swells and fine ripples without repeating visibly.

import * as THREE from 'three/webgpu';
import { Fn, instancedArray, workgroupArray, workgroupBarrier, workgroupId, localId, float, vec2, vec4, uniform, textureStore, uvec2, select, cos, sin, sqrt, exp, max, clamp, smoothstep, length } from 'three/tsl';

const G = 9.81;

// JONSWAP: the energy of a wind sea per frequency (rad/s), for wind U (m/s) blowing over fetch F (m).
// peak: optionally a fixed peak frequency (a swell), with alpha then set by the caller.
function jonswap(w, wp, alpha, gamma) {
  const sigma = w <= wp ? 0.07 : 0.09;
  const r = Math.exp(-((w - wp) ** 2) / (2 * sigma * sigma * wp * wp));
  return ((alpha * G * G) / w ** 5) * Math.exp(-1.25 * (wp / w) ** 4) * gamma ** r;
}
// How the energy spreads round the main direction: cos^2s of half the angle, scaled to add up to one
function spreading(s) {
  let sum = 0;
  const n = 720;
  for (let i = 0; i < n; i++) sum += Math.cos((-Math.PI + ((i + 0.5) * 2 * Math.PI) / n) / 2) ** (2 * s);
  const norm = 1 / ((sum * 2 * Math.PI) / n);
  return (theta) => norm * Math.abs(Math.cos(theta / 2)) ** (2 * s);
}
// A steady random number generator (the same sea every time), and Gaussian pairs from it
function rng(seed) {
  let s = seed >>> 0;
  return () => { s = (s + 0x6d2b79f5) >>> 0; let t = s; t = Math.imul(t ^ (t >>> 15), t | 1); t ^= t + Math.imul(t ^ (t >>> 7), t | 61); return ((t ^ (t >>> 14)) >>> 0) / 4294967296; };
}

export class OceanWaves {
  // size: points across each patch (a power of two); tiles: each patch's width (m), largest first
  constructor(renderer, { size = 256, tiles = [600, 90, 17] } = {}) {
    this.renderer = renderer;
    this.N = size;
    this.tiles = tiles;
    const N = size, C = tiles.length, NN = N * N;
    this.time = uniform(0);
    this.decay = uniform(1);      // (how much of last frame's foam is left)
    // How far the crests are pushed together, and how squeezed the surface must be for foam. (With
    // these the foam covers about as much of the sea as whitecaps really do: next to none in a
    // breeze, about 1% at 8 m/s, 3% at 13 m/s - Monahan's measurements - plus the fading trails.)
    this.choppy = uniform(1);
    this.foamAt = uniform(1);
    // The waves at time 0 (two complex numbers: h0(k) and the conjugate of h0(-k)) and each
    // one's wave vector and speed (kx, kz, omega)
    this.h0 = instancedArray(C * NN, 'vec4');
    this.k = instancedArray(C * NN, 'vec4');
    // The FFT halfway (the rows done, the columns still to do): each point holds four complex
    // numbers (eight wave fields)
    const mid = instancedArray(C * NN * 2, 'vec4');
    const foam = this.foam = instancedArray(C * NN, 'float');
    // The finished patches: (sideways x, height, sideways z, foam) and (slope x, slope z, fold, 0)
    const tex = () => {
      const t = new THREE.StorageTexture(N, N);
      t.type = THREE.HalfFloatType;
      t.wrapS = t.wrapT = THREE.RepeatWrapping;
      t.generateMipmaps = true;
      t.minFilter = THREE.LinearMipmapLinearFilter;
      t.magFilter = THREE.LinearFilter;
      return t;
    };
    this.disp = tiles.map(tex);
    this.slope = tiles.map(tex);

    const cmul = (a, b) => vec2(a.x.mul(b.x).sub(a.y.mul(b.y)), a.x.mul(b.y).add(a.y.mul(b.x)));
    const ci = (a) => vec2(a.y.negate(), a.x); // (times i)

    // One whole line of the inverse FFT (a row or a column) in one workgroup of N threads, all
    // log2(N) butterfly steps in shared memory (Stockham order: the result comes out in place, no
    // bit reversal). t: this thread's place along the line; v: its two values going in.
    const L = Math.log2(N), half = N / 2;
    const line = (t, v) => {
      const sh = workgroupArray('vec4', 2 * N);
      sh.element(t).assign(v[0]); sh.element(t.add(N)).assign(v[1]);
      workgroupBarrier();
      for (let s = 0; s < L; s++) {
        const b = N >> (s + 1);
        const j = t.mod(half), q = j.div(b), i = q.mul(2 * b).add(j.mod(b));
        const ang = float(q).mul((2 * Math.PI * b) / N), w = vec2(cos(ang), sin(ang)).mul(select(t.lessThan(half), float(1), float(-1)));
        // (worked out before anyone overwrites the shared line: toVar makes it happen here)
        const r = [0, N].map((f) => {
          const A = sh.element(i.add(f)), B = sh.element(i.add(b + f));
          return vec4(A.xy.add(cmul(w, B.xy)), A.zw.add(cmul(w, B.zw))).toVar();
        });
        workgroupBarrier();
        sh.element(t).assign(r[0]); sh.element(t.add(N)).assign(r[1]);
        workgroupBarrier();
      }
      return [sh.element(t).toVar(), sh.element(t.add(N)).toVar()];
    };

    // 1. The rows: every wave moved on to this moment, the eight fields made from it, and the
    // transform along each row (one workgroup per row of every patch)
    const rows = Fn(() => {
      const t = localId.x, g = workgroupId.x, c = g.div(N), y = g.mod(N);
      const id = c.mul(NN).add(y.mul(N)).add(t);
      const h0 = this.h0.element(id), kv = this.k.element(id);
      const ph = kv.z.mul(this.time), e = vec2(cos(ph), sin(ph));
      const h = cmul(h0.xy, e).add(cmul(h0.zw, vec2(e.x, e.y.negate())));
      const kx = kv.x, kz = kv.y, kl = max(length(kv.xy), 1e-6), hx = kx.div(kl), hz = kz.div(kl);
      const ih = ci(h);
      // (height, sideways pushes, slopes, and how the pushes change across the patch)
      const dx = ih.mul(hx), dz = ih.mul(hz), sx = ih.mul(kx), sz = ih.mul(kz);
      const dxx = h.mul(kx.mul(hx).negate()), dzz = h.mul(kz.mul(hz).negate()), dxz = h.mul(kx.mul(hz).negate());
      const [o0, o1] = line(t, [vec4(h.add(ci(dx)), dz.add(ci(sx))), vec4(sz.add(ci(dxx)), dzz.add(ci(dxz)))]);
      const base = c.mul(NN * 2).add(y.mul(N)).add(t);
      mid.element(base).assign(o0);
      mid.element(base.add(NN)).assign(o1);
    })().compute(C * NN, [N]);

    // 2. The columns, one patch at a time, and into the patch's textures: the centred spectrum's
    // sign flip, the crest-folding test for foam (where the sideways pushes squeeze the surface
    // together - the Jacobian - foam forms, and lingers a couple of seconds as it fades), and slopes
    // corrected for the squeezing
    const cols = tiles.map((_, c) => Fn(() => {
      const y = localId.x, x = workgroupId.x, p = y.mul(N).add(x), base = p.add(c * NN * 2);
      const [r0, r1] = line(y, [mid.element(base), mid.element(base.add(NN))]);
      const sg = select(x.add(y).mod(2).equal(0), float(1), float(-1));
      const v0 = r0.mul(sg), v1 = r1.mul(sg);
      const lam = this.choppy;
      const jxx = float(1).add(v1.y.mul(lam)), jzz = float(1).add(v1.z.mul(lam)), jxz = v1.w.mul(lam);
      const J = jxx.mul(jzz).sub(jxz.mul(jxz));
      const fm = foam.element(p.add(c * NN));
      fm.assign(max(fm.mul(this.decay), smoothstep(this.foamAt, this.foamAt.sub(0.35), J)));
      textureStore(this.disp[c], uvec2(x, y), vec4(v0.y.mul(lam), v0.x, v0.z.mul(lam), fm));
      textureStore(this.slope[c], uvec2(x, y), vec4(v0.w.div(clamp(jxx, 0.3, 2)), v1.x.div(clamp(jzz, 0.3, 2)), J, 0));
    })().compute(NN, [N]));

    this.steps = [rows, ...cols];
  }

  // The sea state: a wind sea (windMps blowing towards windDir, scene x/z, over fetch metres of
  // open water) and a swell from the open sea (swellDir) of about swellHeight metres with a period
  // of swellPeriod seconds
  setSea({ windMps = 5, windDir = [1, 0], fetch = 120000, swellDir = [1, 0], swellHeight = 1.2, swellPeriod = 12, seed = 7 }) {
    const N = this.N, C = this.tiles.length, NN = N * N;
    const h0 = this.h0.value.array, kk = this.k.value.array;
    const U = Math.max(0.3, windMps), F = fetch;
    const alpha = 0.076 * ((U * U) / (F * G)) ** 0.22, wp = 22 * ((G * G) / (U * F)) ** (1 / 3);
    const windSpread = spreading(6), swellSpread = spreading(24);
    // (the swell's strength set so its significant height comes out right: Hs = 4 sqrt(energy))
    const swp = (2 * Math.PI) / swellPeriod;
    let m0 = 0;
    for (let w = 0.05; w < 6; w += 0.002) m0 += jonswap(w, swp, 1, 5) * 0.002;
    const salpha = swellHeight > 0 ? (swellHeight / 4) ** 2 / m0 : 0;
    const wa = Math.atan2(windDir[1], windDir[0]), sa = Math.atan2(swellDir[1], swellDir[0]);
    const rand = rng(seed);
    const gauss = () => { const u = Math.max(1e-9, rand()), v = rand(), r = Math.sqrt(-2 * Math.log(u)); return [r * Math.cos(2 * Math.PI * v), r * Math.sin(2 * Math.PI * v)]; };
    // Each detail level takes the waves longer than the next level's patch / 6 (so none is
    // counted twice and each is drawn by the patch that shows it best)
    const energy = (kx, kz, c) => {
      const k = Math.hypot(kx, kz);
      if (k < 1e-6) return 0;
      const lo = c > 0 ? (2 * Math.PI * 6) / this.tiles[c] : 0, hi = c < C - 1 ? (2 * Math.PI * 6) / this.tiles[c + 1] : Infinity;
      if (k < lo || k >= hi) return 0;
      const w = Math.sqrt(G * k), dwdk = G / (2 * w), th = Math.atan2(kz, kx);
      const S = jonswap(w, wp, alpha, 3.3) * windSpread(th - wa) + jonswap(w, swp, salpha, 5) * swellSpread(th - sa);
      // (the tiniest ripples damped, so the finest patch doesn't fizz)
      return (S * dwdk / k) * Math.exp(-((k * 0.02) ** 2));
    };
    for (let c = 0; c < C; c++) {
      const dk = (2 * Math.PI) / this.tiles[c];
      const amp = new Float32Array(NN * 2);
      for (let y = 0; y < N; y++) for (let x = 0; x < N; x++) {
        const kx = (x - N / 2) * dk, kz = (y - N / 2) * dk, e = energy(kx, kz, c);
        const [a, b] = gauss(), s = Math.sqrt(e / 2) * dk / Math.SQRT2;
        // (the unpaired edge of the centred spectrum stays empty, so the sea comes out real)
        const edge = x === 0 || y === 0;
        amp[(y * N + x) * 2] = edge ? 0 : a * s; amp[(y * N + x) * 2 + 1] = edge ? 0 : b * s;
        const o = (c * NN + y * N + x) * 4;
        kk[o] = kx; kk[o + 1] = kz; kk[o + 2] = Math.sqrt(G * Math.hypot(kx, kz)); kk[o + 3] = 0;
      }
      for (let y = 0; y < N; y++) for (let x = 0; x < N; x++) {
        const o = (c * NN + y * N + x) * 4, m = (((N - y) % N) * N + ((N - x) % N)) * 2, i = (y * N + x) * 2;
        h0[o] = amp[i]; h0[o + 1] = amp[i + 1]; h0[o + 2] = amp[m]; h0[o + 3] = -amp[m + 1];
      }
    }
    this.h0.value.needsUpdate = true;
    this.k.value.needsUpdate = true;
    // Roughly how high the waves are (significant height, m), for the shading
    let var0 = 0;
    for (let i = 0; i < h0.length; i += 4) var0 += h0[i] ** 2 + h0[i + 1] ** 2 + h0[i + 2] ** 2 + h0[i + 3] ** 2;
    this.hs = 4 * Math.sqrt(var0);
  }

  // Move the sea on (seconds since the start, and this frame's step)
  update(t, dt) {
    this.time.value = t;
    this.decay.value = Math.exp(-dt / 1.8);
    this.renderer.compute(this.steps);
  }

  dispose() {
    for (const s of this.steps) s.dispose?.();
    for (const t of [...this.disp, ...this.slope]) t.dispose();
  }
}
