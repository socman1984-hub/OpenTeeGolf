// The clouds drawn by the sky (three.js SkyMesh), worked out the same way here: on the CPU, to know
// how much cloud is in front of the sun (the light dims as a cloud crosses it), and as a shader
// node, for the clouds' shadows moving over the course. SkyMesh's noise hashes without sin(), so
// both give the same clouds as the sky shows. Keep in step with SkyMesh (three.js r186).
import { Fn, vec2, vec3, float, floor, fract, dot, mix, clamp, exp, max, smoothstep, normalize } from 'three/tsl';

// ---- CPU

const fr = (x) => x - Math.floor(x);
function gradient(ix, iy) {
  let px = fr(ix * 0.1031), py = fr(iy * 0.1030), pz = fr(ix * 0.0973);
  const d = px * (py + 33.33) + py * (pz + 33.33) + pz * (px + 33.33);
  px += d; py += d; pz += d;
  return [fr((px + py) * pz) * 2 - 1, fr((px + pz) * py) * 2 - 1];
}
function noise(x, y) {
  const ix = Math.floor(x), iy = Math.floor(y), fx = x - ix, fy = y - iy;
  const q = (f) => f * f * f * (f * (f * 6 - 15) + 10);
  const ux = q(fx), uy = q(fy);
  const g = (i, j, ox, oy) => { const [a, b] = gradient(ix + i, iy + j); return a * (fx - ox) + b * (fy - oy); };
  const a = g(0, 0, 0, 0), b = g(1, 0, 1, 0), c = g(0, 1, 0, 1), d = g(1, 1, 1, 1);
  return ((a + (b - a) * ux) * (1 - uy) + (c + (d - c) * ux) * uy) * 1.6;
}
function fbm(x, y, drift) {
  let r = 0, a = 1;
  for (let k = 0; k < 4; k++) { r += a * noise(x, y); a *= 0.5; x = x * 2 + drift; y = y * 2 + drift; }
  return r;
}

// How opaque the clouds are (0..1) looking along dir ([x, y, z], unit, y up) at time t (s, the
// shaders' time), for a sky with these cloud settings
export function cloudAlpha(sky, [dx, dy, dz], t) {
  if (dy <= 0 || sky.cloudCoverage.value <= 0) return 0;
  const elevation = 1 + (0.1 - 1) * sky.cloudElevation.value, sc = sky.cloudScale.value, sp = sky.cloudSpeed.value;
  const u = (dx / (dy * elevation)) * sc + t * sp, v = (dz / (dy * elevation)) * sc + t * sp;
  const n = Math.min(1, Math.max(0, fbm(u * 1000, v * 1000, t * sp * 300) * 0.7 + 0.5));
  const region = noise(u * 300, v * 300) * 0.37 + 0.5;
  const cov = Math.min(1, Math.max(0, sky.cloudCoverage.value + (region - 0.5) * 0.6));
  const depth = Math.max(0, n - (1 - cov));
  const fade = Math.min(1, Math.max(0, dy / (0.03 + 0.06 * sky.cloudElevation.value)));
  return (1 - Math.exp(-depth * sky.cloudDensity.value * 12)) * fade * fade * (3 - 2 * fade);
}

// ---- shader

const gradientN = Fn(([i]) => {
  const p = fract(vec3(i.x, i.y, i.x).mul(vec3(0.1031, 0.1030, 0.0973))).toVar();
  p.addAssign(dot(p, p.yzx.add(33.33)));
  return fract(p.xx.add(p.yz).mul(p.zy)).mul(2.0).sub(1.0);
});
const noiseN = Fn(([p]) => {
  const i = floor(p), f = fract(p);
  const u = f.mul(f).mul(f).mul(f.mul(f.mul(6.0).sub(15.0)).add(10.0));
  const a = dot(gradientN(i), f);
  const b = dot(gradientN(i.add(vec2(1, 0))), f.sub(vec2(1, 0)));
  const c = dot(gradientN(i.add(vec2(0, 1))), f.sub(vec2(0, 1)));
  const d = dot(gradientN(i.add(vec2(1, 1))), f.sub(vec2(1, 1)));
  return mix(mix(a, b, u.x), mix(c, d, u.x), u.y).mul(1.6);
});
const fbmN = (p0, drift) => {
  let r = float(0), a = 1, p = p0;
  for (let k = 0; k < 4; k++) { r = r.add(noiseN(p).mul(a)); a *= 0.5; p = p.mul(2.0).add(drift); }
  return r;
};

// cloudAlpha as a node: dir (vec3, unit), t (the shaders' time)
export function cloudAlphaNode(sky, dir, t) {
  const elevation = mix(float(1.0), float(0.1), sky.cloudElevation);
  const uvN = dir.xz.div(dir.y.max(1e-3).mul(elevation)).mul(sky.cloudScale).add(t.mul(sky.cloudSpeed));
  const n = clamp(fbmN(uvN.mul(1000.0), t.mul(sky.cloudSpeed).mul(300.0)).mul(0.7).add(0.5), 0.0, 1.0);
  const region = noiseN(uvN.mul(300.0)).mul(0.37).add(0.5);
  const cov = clamp(sky.cloudCoverage.add(region.sub(0.5).mul(0.6)), 0.0, 1.0);
  const depth = max(0.0, n.sub(float(1.0).sub(cov)));
  const fade = smoothstep(0.0, float(0.03).add(sky.cloudElevation.mul(0.06)), dir.y);
  return float(1.0).sub(exp(depth.mul(sky.cloudDensity).mul(-12.0))).mul(fade);
}

// Clouds are treated as a layer this high (m) for their shadows: the cloud over a spot on the
// ground is the one the sky shows in the sun's direction from a point this far below it, seen
// from the middle of the course. (Lower than real clouds: the sky draws them as if far off, and
// this keeps their shadows about as big as the clouds look - a few hundred metres.)
export const CLOUD_HEIGHT = 400;
// The direction from origin (the middle of the course, at ground level) to where the sun's ray
// through p meets the cloud layer: the sky direction whose cloud shades p (sun: unit, y up)
export const shadowDir = (p, sun, origin) => normalize(p.add(sun.mul(float(CLOUD_HEIGHT).sub(p.y).div(sun.y.max(0.05)))).sub(origin));
export function shadowDirCPU([px, py, pz], [sx, sy, sz], [ox, oy, oz]) {
  const k = (CLOUD_HEIGHT - py) / Math.max(0.05, sy);
  const x = px + sx * k - ox, y = py + sy * k - oy, z = pz + sz * k - oz, l = Math.hypot(x, y, z) || 1;
  return [x / l, y / l, z / l];
}
