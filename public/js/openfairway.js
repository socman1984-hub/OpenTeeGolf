// OpenFairway golf ball physics: drag, lift and spin decay in flight, wind that grows with height,
// impacts with spin-dependent restitution and landing bite, and roll with sliding friction,
// rolling resistance, grass drag and slope until the ball can rest.
//
// Ported to JavaScript from the OpenFairwayPhysics plugin of uopengolf
// (https://github.com/digitalhand/uopengolf, Plugins/OpenFairwayPhysics, revision
// 51d5fffdd9db9eaafc927823a2d76007e64d5191; MIT, Copyright (c) 2026 uopengolf contributors), itself
// a port of ogolf-physics' addons/openfairway. See NOTICE.md. Equations and calibrated constants
// are the plugin's gameplay defaults; the comments here say where OpenTeeGolf differs.
//
// Vectors are [x, y, z] arrays in a right-handed frame with y up, in SI units (m, m/s, rad/s).
// The functions are frame-free otherwise, so physics.js works in course space directly.

export const MASS = 0.04592623;
export const RADIUS = 0.021335;
const AREA = Math.PI * RADIUS * RADIUS;
const INERTIA = 0.4 * MASS * RADIUS * RADIUS;
export const G = 9.81;
export const RAD_PER_RPM = 0.10472;
// The rate OpenFairway is calibrated at
export const DT = 1 / 120;

// ------------------------------------------------------------------ vectors

const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const len = (a) => Math.hypot(a[0], a[1], a[2]);
const add = (a, b, k = 1) => [a[0] + b[0] * k, a[1] + b[1] * k, a[2] + b[2] * k];
const scale = (a, k) => [a[0] * k, a[1] * k, a[2] * k];
const set = (out, a) => { out[0] = a[0]; out[1] = a[1]; out[2] = a[2]; };
const tangent = (a, n) => add(a, n, -dot(a, n));
const lerp = (a, b, t) => a + (b - a) * t;
const clamp = (x, lo, hi) => Math.min(hi, Math.max(lo, x));
const smooth01 = (t) => { const c = clamp(t, 0, 1); return c * c * (3 - 2 * c); };
// (a zero-width range is a step)
const smoothRange = (x, a, b) => (Math.abs(b - a) < 1e-6 ? (x >= b ? 1 : 0) : smooth01((x - a) / (b - a)));
const rampUp = (x, a, b) => (b <= a ? (x >= a ? 1 : 0) : smooth01((x - a) / (b - a)));

const validNormal = (n) => {
  const l = len(n);
  return Number.isFinite(l) && l > 1e-3 ? scale(n, 1 / l) : [0, 1, 0];
};

// ------------------------------------------------------------------ profiles

export const FLIGHT = {
  // Smits/Smith spin-down: dω/dt = -coefficient · speed/radius · ω
  spinDecayCoefficient: 2.0e-5,
  // Drag polynomial in the Reynolds number, eased into its cap from 175k
  cdPoly: [1.1948, -0.0000209661, 1.42472e-10, -3.14383e-16],
  highReCdCap: 0.2,
  highReCdBlendStart: 175000,
  lowReCdFloor: 0.38,
  lowReBlendStart: 30000,
  cdAt50k: 0.4632,
  clMaxBase: 0.268,
  clMaxHighSpin: 0.32,
  clMaxSrTransitionStart: 0.35,
  clMaxSrTransitionEnd: 0.5,
  spinDragMultiplierCoeff: 4.0,
  spinDragMultiplierMax: 1.2,
  spinDragMultiplierHighSpinMax: 1.03,
  spinDragMultiplierUltraHighSpinMax: 1.21,
  highSpinDragSrStart: 0.3,
  highSpinDragSrEnd: 0.48,
  highSpinDragReliefReFullMax: 90000,
  highSpinDragReliefReZero: 105000,
  ultraHighSpinDragSrStart: 0.57,
  ultraHighSpinDragSrEnd: 0.77,
  // Re 50k-70k bridges the 50k and 70k lift fits instead of dipping to zero through 60k/65k
  bridgeLowReLift: true,
  highReStart: 75000,
  highReMidSpinGain: 16.0,
  highReSpinGain: 16.0,
  highReGainReductionStart: 0.1,
  highReGainReductionEnd: 0.18,
  highReGainRecoveryStart: 0.26,
  highReGainRecoveryEnd: 0.4,
  highSpinClAttenuationStart: 0.45,
  highSpinClAttenuationEnd: 0.55,
  highSpinClAttenuationMax: 0.09,
  ultraHighSpinClAttenuationStart: 0.58,
  ultraHighSpinClAttenuationEnd: 0.85,
  ultraHighSpinClAttenuationMax: 0.1,
  lowReHighSpinClAttenuationMax: 0.1,
  lowReUltraHighSpinClAttenuationMax: 0.06,
  // (the legacy low-launch lift boost is neutral, as in the plugin's gameplay profile)
  lowLaunchLiftRecoveryMax: 1.0,
  lowLaunchVlaFullDeg: 6.5,
  lowLaunchVlaZeroDeg: 9.5,
  lowLaunchReStart: 85000,
  lowLaunchReEnd: 110000,
  lowLaunchSpinRatioFull: 0.18,
  lowLaunchSpinRatioMax: 0.22,
  spinDragProgressiveCapSrStart: 0.33,
  spinDragProgressiveCapSrEnd: 0.5,
  spinDragProgressiveCapBoostMax: 0.25,
  midSpinClBoostSrStart: 0.17,
  midSpinClBoostSrEnd: 0.31,
  midSpinClBoostMax: 0.45,
  highLaunchDragBoostMax: 1.24,
  highLaunchDragVlaStartDeg: 24.5,
  highLaunchDragVlaFullDeg: 31.5,
  highLaunchDragSrStart: 0.5,
  highLaunchDragSrEnd: 0.7,
};

export const BOUNCE = {
  corBaseA: 0.45,
  corBaseB: -0.01,
  corBaseC: 0.0002,
  corHighSpeedCap: 0.25,
  corHighSpeedThreshold: 20.0,
  corKillThreshold: 2.0,
  flightTangentialRetentionBase: 0.55,
  flightSpinFactorMin: 0.4,
  flightSpinFactorDivisor: 8000,
  flightRetentionRampStartMps: 0.75,
  flightRetentionRampEndMps: 2.0,
  rolloutLowSpinRetention: 0.85,
  rolloutHighSpinRetention: 0.7,
  rolloutSpinRatioThreshold: 0.2,
  spinCorLowSpinThreshold: 1500,
  spinCorLowSpinMaxReduction: 0.3,
  spinCorHighSpinRangeRpm: 1500,
  spinCorHighSpinAdditionalReduction: 0.4,
  corVelocityLowThreshold: 12.0,
  corVelocityMidThreshold: 25.0,
  corVelocityLowScale: 0.5,
  rolloutBounceCorKillThreshold: 4.0,
  rolloutBounceCorScale: 0.5,
};

export const ROLLOUT = {
  chipSpeedThreshold: 20.0,
  pitchSpeedThreshold: 35.0,
  chipVelocityScaleMin: 0.6,
  chipVelocityScaleMax: 0.87,
  lowSpinThreshold: 1250,
  midSpinThreshold: 1750,
  lowSpinMultiplierMax: 1.3,
  midSpinMultiplierMax: 2.25,
  highSpinMultiplierMax: 2.5,
  highSpinRampRange: 1000,
  tangentVelocityThreshold: 0.05,
  // Turf resists the down-slope pull up to rolling friction × this of the normal load
  slopeResistanceRatio: 1.6,
};

// The ball as a whole (the plugin's FBallPhysicsProfile)
export const PROFILE = {
  dragScale: 1.01,
  liftScale: 1.0,
  // Forward speed a sand landing loses per m/s it drives into the sand (the crater)
  sandPlowRatio: 2.5,
  // Speed-dependent grass drag of every green; its rolling friction is solved with it from the stimp
  greenGrassViscosity: 0.0009,
};

// ------------------------------------------------------------------ surfaces

const surface = (kineticFriction, rollingFriction, grassViscosity, criticalAngle, spinbackResponseScale, extra = {}) => ({
  kineticFriction, rollingFriction, grassViscosity, criticalAngle, spinbackResponseScale,
  spinbackThetaBoostMax: 0, spinbackSpinStartRpm: 0, spinbackSpinEndRpm: 0, spinbackSpeedStartMps: 0, spinbackSpeedEndMps: 0,
  rollingSpinScale: 1, restitutionScale: 1, impactRetentionScale: 1, impactPlowRatio: 0, bounce: null,
  ...extra,
});

// OpenFairway's seven presets. A green's rolling friction comes from its stimp (surfaceSettings).
export const PRESETS = {
  fairway: surface(0.5, 0.05, 0.0017, 0.29, 0.78),
  fairwaySoft: surface(0.56, 0.07, 0.0024, 0.32, 0.92),
  rough: surface(0.62, 0.095, 0.0032, 0.35, 0.7),
  firm: surface(0.3, 0.03, 0.001, 0.25, 0.6),
  fringe: surface(0.56, 0.0725, 0.00245, 0.29, 0.78),
  green: surface(0.58, null, PROFILE.greenGrassViscosity, 0.36, 1.12, {
    spinbackThetaBoostMax: 0.12, spinbackSpinStartRpm: 3500, spinbackSpinEndRpm: 5500,
    spinbackSpeedStartMps: 8, spinbackSpeedEndMps: 20, rollingSpinScale: 0,
  }),
  sand: surface(0.65, 0.3, 0.012, 0.45, 1.0, {
    restitutionScale: 0.6, impactRetentionScale: 0.25, impactPlowRatio: PROFILE.sandPlowRatio,
    bounce: { firstImpactMinimumSpeed: 0.15, laterImpactMinimumSpeed: 0.15, laterImpactScale: 0.85, minimumBounceHeight: 0.001 },
  }),
};
export { surface as makeSurface };

// Green speed: the rolling friction that, with the green's grass drag, stops a ball leaving a
// stimpmeter at 1.83 m/s after `stimp` feet. (OpenFairway allows 8-15 ft; the game goes down to 6.)
const STIMPMETER_MPS = 1.83;
const speedDragPerSecond = (viscosity) => (10 * Math.PI * RADIUS * viscosity) / MASS;
// Metres rolled from `speed` decelerating at a0 + k·v
const rollMeters = (speed, a0, k) => (k <= 0 ? (speed * speed) / (2 * a0) : speed / k - (a0 / (k * k)) * Math.log(1 + (k * speed) / a0));
export function greenRollingFriction(stimpFt, viscosity = PROFILE.greenGrassViscosity) {
  const k = speedDragPerSecond(viscosity);
  const d = clamp(Number.isFinite(stimpFt) ? stimpFt : 10, 6, 15) * 0.3048;
  let lo = 0, hi = (STIMPMETER_MPS * STIMPMETER_MPS) / (2 * d);
  for (let i = 0; i < 32; i++) {
    const mid = 0.5 * (lo + hi);
    if (rollMeters(STIMPMETER_MPS, mid, k) > d) lo = mid; else hi = mid;
  }
  return (7 * 0.5 * (lo + hi)) / (5 * G);
}
// Speed (m/s) a putt needs to roll `distanceM` on a level green of this stimp
export function puttSpeed(distanceM, stimpFt, viscosity = PROFILE.greenGrassViscosity) {
  const a0 = (5 * G * greenRollingFriction(stimpFt, viscosity)) / 7, k = speedDragPerSecond(viscosity);
  let lo = 0, hi = 30;
  for (let i = 0; i < 40; i++) {
    const mid = 0.5 * (lo + hi);
    if (rollMeters(mid, a0, k) > distanceM) hi = mid; else lo = mid;
  }
  return 0.5 * (lo + hi);
}

// ------------------------------------------------------------------ air

// Air density (kg/m³) from altitude, temperature, humidity (0-1) and, if known, the measured
// station pressure (Pa); the dry air contributes only its partial pressure (Tetens).
export function airDensity(altitudeM, tempC, humidity = 0, stationPressurePa = 0) {
  const T = Math.max(tempC + 273.15, 1);
  const rh = Number.isNaN(humidity) ? 0 : clamp(humidity, 0, 1);
  const p = stationPressurePa > 0 ? stationPressurePa : 101325 * Math.exp((-9.80665 * 0.0289644 * altitudeM) / (8.314462618 * T));
  if (rh <= 0) return p / (287.058 * T);
  const pv = rh * 610.78 * Math.pow(10, (7.5 * tempC) / (tempC + 237.3));
  return (Math.max(p - pv, 0) * 0.0289644 + pv * 0.018016) / (8.314462618 * T);
}

// Dynamic viscosity of air (Sutherland)
export function airViscosity(tempC) {
  const T = Math.max(tempC + 273.15, 1);
  return (1.716e-5 * Math.pow(T / 273.15, 1.5) * (273.15 + 110.4)) / (T + 110.4);
}

// Share of the 10 m wind at a height above the launch ground: the neutral log profile over open
// grass (roughness 0.03 m), none at the grass, growing until 100 m.
export function windFactor(heightM, referenceM = 10, roughnessM = 0.03) {
  if (!Number.isFinite(heightM) || heightM <= roughnessM) return 0;
  const lz = Math.log(roughnessM);
  return (Math.log(Math.min(heightM, 100)) - lz) / (Math.log(referenceM) - lz);
}

// ------------------------------------------------------------------ aerodynamics

const P = FLIGHT;
const RE_SAMPLES = [50000, 60000, 65000, 70000, 75000];
const clRe50k = (s) => 0.0472121 + 2.84795 * s - 23.4342 * s * s + 45.4849 * s * s * s;
const clRe60k = (s) => 0.320524 - 4.7032 * s + 14.0613 * s * s;
const clRe65k = (s) => 0.266667 - 4.0 * s + 13.3333 * s * s;
const clRe70k = (s) => 0.0496189 + 0.00211396 * s + 2.34201 * s * s;

function clMax(s) {
  if (s <= P.clMaxSrTransitionStart) return P.clMaxBase;
  if (s >= P.clMaxSrTransitionEnd) return P.clMaxHighSpin;
  return lerp(P.clMaxBase, P.clMaxHighSpin, smoothRange(s, P.clMaxSrTransitionStart, P.clMaxSrTransitionEnd));
}

function highReSpinGain(s) {
  if (s <= P.highReGainReductionStart) return P.highReSpinGain;
  if (s < P.highReGainReductionEnd) return lerp(P.highReSpinGain, P.highReMidSpinGain, smoothRange(s, P.highReGainReductionStart, P.highReGainReductionEnd));
  if (s <= P.highReGainRecoveryStart) return P.highReMidSpinGain;
  if (s < P.highReGainRecoveryEnd) return lerp(P.highReMidSpinGain, P.highReSpinGain, smoothRange(s, P.highReGainRecoveryStart, P.highReGainRecoveryEnd));
  return P.highReSpinGain;
}

const clHighRe = (s) => { const g = highReSpinGain(s); return (clMax(s) * s * g) / (1 + s * g); };
const clAtSample = (i, s) => [clRe50k, clRe60k, clRe65k, clRe70k, clHighRe][i](s);

const highSpinAttenuation = (s, cl, max, ultraMax) =>
  cl * (1 - max * smoothRange(s, P.highSpinClAttenuationStart, P.highSpinClAttenuationEnd)) *
  (1 - ultraMax * smoothRange(s, P.ultraHighSpinClAttenuationStart, P.ultraHighSpinClAttenuationEnd));

export function dragCoefficient(re) {
  const cap = 200000;
  if (re > cap) return P.highReCdCap;
  if (re >= 50000) {
    const [a, b, c, d] = P.cdPoly;
    const poly = a + b * re + c * re * re + d * re * re * re;
    if (P.highReCdBlendStart >= cap || re <= P.highReCdBlendStart) return poly;
    return lerp(poly, P.highReCdCap, smoothRange(re, P.highReCdBlendStart, cap));
  }
  if (re <= P.lowReBlendStart) return P.lowReCdFloor;
  return lerp(P.lowReCdFloor, P.cdAt50k, smoothRange(re, P.lowReBlendStart, 50000));
}

export function liftCoefficient(re, spinRatio) {
  const s = Math.max(0, spinRatio);
  if (s <= 0) return 0;
  const max = clMax(s);
  if (re < 50000) {
    if (re <= 30000) return 0;
    const t = smooth01((re - 30000) / 20000);
    return highSpinAttenuation(s, clamp(clRe50k(s), 0, max) * t, P.lowReHighSpinClAttenuationMax, P.lowReUltraHighSpinClAttenuationMax);
  }
  if (re >= P.highReStart) return highSpinAttenuation(s, clamp(clHighRe(s), 0, max), P.highSpinClAttenuationMax, P.ultraHighSpinClAttenuationMax);
  const samples = P.bridgeLowReLift ? [0, 3, 4] : [0, 1, 2, 3, 4];
  let hiSlot = samples.length - 1;
  for (let k = 0; k < samples.length; k++) if (re <= RE_SAMPLES[samples[k]]) { hiSlot = k; break; }
  const hi = samples[hiSlot], lo = samples[Math.max(hiSlot - 1, 0)];
  const clLo = Math.max(0, clAtSample(lo, s)), clHi = Math.max(0, clAtSample(hi, s));
  const reLo = RE_SAMPLES[lo], reHi = RE_SAMPLES[hi];
  const w = reHi !== reLo ? (re - reLo) / (reHi - reLo) : 0;
  return highSpinAttenuation(s, clamp(lerp(clLo, clHi, w), 0, max), P.lowReHighSpinClAttenuationMax, P.lowReUltraHighSpinClAttenuationMax);
}

function spinDragMultiplier(s, re) {
  if (s <= 0) return 1;
  const highSpin = smoothRange(s, P.highSpinDragSrStart, P.highSpinDragSrEnd);
  const relief = 1 - smoothRange(re, P.highSpinDragReliefReFullMax, P.highSpinDragReliefReZero);
  let cap = lerp(P.spinDragMultiplierMax, P.spinDragMultiplierHighSpinMax, highSpin * relief);
  cap += P.spinDragProgressiveCapBoostMax * smoothRange(s, P.spinDragProgressiveCapSrStart, P.spinDragProgressiveCapSrEnd);
  cap = lerp(cap, P.spinDragMultiplierUltraHighSpinMax, smoothRange(s, P.ultraHighSpinDragSrStart, P.ultraHighSpinDragSrEnd));
  return Math.min(1 + P.spinDragMultiplierCoeff * s * s, cap);
}

function lowLaunchLiftScale(vlaDeg, s, re) {
  const launch = smoothRange(P.lowLaunchVlaZeroDeg - vlaDeg, 0, P.lowLaunchVlaZeroDeg - P.lowLaunchVlaFullDeg);
  if (launch <= 0) return 1;
  const reF = smoothRange(re, P.lowLaunchReStart, P.lowLaunchReEnd);
  if (reF <= 0) return 1;
  const spinF = 1 - smoothRange(s, P.lowLaunchSpinRatioFull, P.lowLaunchSpinRatioMax);
  if (spinF <= 0) return 1;
  return lerp(1, P.lowLaunchLiftRecoveryMax, launch * reF * spinF);
}

function highLaunchDragScale(vlaDeg, s) {
  const launch = smoothRange(vlaDeg, P.highLaunchDragVlaStartDeg, P.highLaunchDragVlaFullDeg);
  if (launch <= 0) return 1;
  const spinF = smoothRange(s, P.highLaunchDragSrStart, P.highLaunchDragSrEnd);
  if (spinF <= 0) return 1;
  return lerp(1, P.highLaunchDragBoostMax, launch * spinF);
}

function midSpinClBoost(s) {
  if (P.midSpinClBoostMax <= 0) return 1;
  const t = smoothRange(s, P.midSpinClBoostSrStart, P.midSpinClBoostSrEnd);
  return 1 + P.midSpinClBoostMax * t * (1 - t) * 4;
}

// Below this speed through the air (m/s) the ball feels gravity only
const MIN_AERO_SPEED = 0.5;

// Drag and lift coefficients and the spin decay rate for one instant of flight. `air` is the
// ball's velocity through the air; env: { rho, mu, dragScale, liftScale, launchDeg }.
export function aeroSample(air, spin, env) {
  const speed = len(air);
  const out = { speed, spinRatio: 0, reynolds: 0, cd: 0, cl: 0, spinDecay: (Math.max(0, P.spinDecayCoefficient) * speed) / RADIUS };
  if (speed < MIN_AERO_SPEED) return out;
  out.spinRatio = (len(spin) * RADIUS) / speed;
  out.reynolds = (env.rho * speed * RADIUS * 2) / env.mu;
  out.cd = dragCoefficient(out.reynolds) * spinDragMultiplier(out.spinRatio, out.reynolds) * env.dragScale * highLaunchDragScale(env.launchDeg, out.spinRatio);
  out.cl = liftCoefficient(out.reynolds, out.spinRatio) * env.liftScale * lowLaunchLiftScale(env.launchDeg, out.spinRatio, out.reynolds) * midSpinClBoost(out.spinRatio);
  return out;
}

// One flight step (semi-implicit Euler, as calibrated): v and spin change in place. `wind` is the
// air's velocity where the ball is.
export function stepFlight(v, spin, env, wind, dt = DT) {
  const air = [v[0] - wind[0], v[1] - wind[1], v[2] - wind[2]];
  const s = aeroSample(air, spin, env);
  let a = [0, -G, 0];
  if (s.speed >= MIN_AERO_SPEED) {
    const k = (0.5 * env.rho * AREA * s.speed) / MASS;
    a = add(a, air, -k * s.cd);
    const w = len(spin);
    if (w > 0.1) a = add(a, cross(spin, air), (k * s.cl) / w);
  }
  set(v, add(v, a, dt));
  set(spin, scale(spin, Math.exp(-s.spinDecay * dt)));
  return s;
}

// ------------------------------------------------------------------ launch calibration

// Launch regimes: "<family>-<speed>-<launch>-<spin>"
const family = (mph, vla) => (mph < 60 ? 'C' : mph > 110 && vla < 18 ? 'D' : vla > 30 ? 'W' : 'I');
const speedBin = (mph) => (mph < 60 ? 'S0' : mph < 72 ? 'S1a' : mph < 85 ? 'S1b' : mph < 105 ? 'S2' : mph < 120 ? 'S3' : 'S4');
const launchBin = (vla) => (vla < 10 ? 'V0' : vla < 18 ? 'V1' : vla < 25 ? 'V2' : vla < 33 ? 'V3' : 'V4');
const spinBin = (rpm) => (rpm < 2500 ? 'P0' : rpm < 4000 ? 'P1' : rpm < 5500 ? 'P2' : rpm < 7500 ? 'P3' : 'P4');
export const regimeKey = (mph, vla, rpm) => `${family(mph, vla)}-${speedBin(mph)}-${launchBin(vla)}-${spinBin(rpm)}`;

// Drag and lift multipliers fitted to FlightScope reference shots, per regime [drag, lift]. Only
// these two of the plugin's override fields are set by any row, so only they are carried.
export const REGIMES = {
  'C-S0': [0.7, 1.2], 'C-S0-V1-P0': [0.55, 1.15], 'C-S0-V4-P3': [0.65, 1.25],
  'I-S1a-V0-P1': [0.94, 1.04], 'I-S1a-V2-P1': [0.8, 1.14], 'I-S1a-V2-P2': [0.82, 1.13], 'I-S1a-V2-P3': [0.82, 1.12],
  'I-S1a-V3-P2': [0.79, 1.14], 'I-S1a-V3-P3': [0.94, 1.04], 'I-S1a-V1-P2': [0.92, 1.05],
  'I-S1b-V0-P0': [0.97, 1.02], 'I-S1b-V2-P2': [0.94, 1.03], 'I-S1b-V2-P3': [0.97, 1.01], 'I-S1b-V3-P2': [0.88, 1.06],
  'I-S1b-V3-P3': [0.97, 1.02], 'I-S1b-V1-P2': [0.98, 1.01],
  'W-S1a-V3-P3': [0.83, 1.1],
  'I-S3-V2-P3': [1.11, 0.94], 'I-S3-V1-P2': [1.04, 0.98], 'I-S2-V2-P3': [1.05, 1], 'I-S2-V2-P4': [1.06, 0.95],
  'I-S2-V1-P2': [1.03, 0.99], 'I-S2-V0-P2': [1.06, 0.96], 'I-S2-V1-P0': [0.96, 1.03], 'I-S2-V1-P1': [0.97, 1.02],
  'D-S3-V1': [1.04, 0.99], 'D-S4-V0-P1': [1.03, 0.98], 'D-S4-V0-P2': [1.09, 0.94], 'D-S4-V1-P0': [0.98, 1.02],
  'D-S4-V1-P1': [1.04, 1], 'D-S4-V1-P2': [1.04, 1],
  'W-S2-V3-P4': [1.06, 0.97],
};

function lookupRegime(mph, vla, rpm) {
  const f = family(mph, vla), s = speedBin(mph), l = launchBin(vla);
  for (const k of [`${f}-${s}-${l}-${spinBin(rpm)}`, `${f}-${s}-${l}`, `${f}-${s}`, f]) if (REGIMES[k]) return REGIMES[k];
  return [1, 1];
}

// The table is read at the anchors around a launch and blended, so the calibration has no steps
const SPEED_ANCHORS = [54, 66, 78.5, 95, 107.5, 115, 125];
const LAUNCH_ANCHORS = [5, 14, 21.5, 27.5, 31.5, 36.5];
const SPIN_ANCHORS = [1250, 3250, 4750, 6500, 8500];
function bracket(anchors, x) {
  if (x <= anchors[0]) return [[anchors[0], 1]];
  for (let i = 1; i < anchors.length; i++) {
    if (x < anchors[i]) { const t = (x - anchors[i - 1]) / (anchors[i] - anchors[i - 1]); return [[anchors[i - 1], 1 - t], [anchors[i], t]]; }
  }
  return [[anchors[anchors.length - 1], 1]];
}

// First-landing bite per landing bucket: spin bite scale, the most it may spin back (m/s), and the
// impact speeds it fades in over
const LANDING = {
  run: [0, 0, 0, 0],
  release: [0.5, 0, 8, 20],
  check: [0.85, 0, 8, 20],
  spinback: [1, 1.2, 8, 20],
};
export function landingBucket(vla, rpm) {
  const p = spinBin(rpm);
  if (p === 'P0' || p === 'P1') return 'run';
  if (p === 'P2') return 'release';
  if (p === 'P3') return 'check';
  const v = launchBin(vla);
  return v === 'V0' || v === 'V1' ? 'check' : 'spinback';
}

// Everything about a shot fixed at launch: the blended drag/lift scales and the landing response
export function resolveLaunch(speedMph, vlaDeg, totalSpinRpm) {
  let drag = 0, lift = 0;
  for (const [s, ws] of bracket(SPEED_ANCHORS, speedMph))
    for (const [l, wl] of bracket(LAUNCH_ANCHORS, vlaDeg))
      for (const [p, wp] of bracket(SPIN_ANCHORS, totalSpinRpm)) {
        const w = ws * wl * wp;
        if (w <= 0) continue;
        const [d, li] = lookupRegime(s, l, p);
        drag += d * w; lift += li * w;
      }
  const landing = { spinBiteScale: 0, maxSpinbackMps: 0, speedRampStartMps: 0, speedRampEndMps: 0 };
  for (const [l, wl] of bracket(LAUNCH_ANCHORS, vlaDeg))
    for (const [p, wp] of bracket(SPIN_ANCHORS, totalSpinRpm)) {
      const r = LANDING[landingBucket(l, p)], w = wl * wp;
      landing.spinBiteScale += r[0] * w; landing.maxSpinbackMps += r[1] * w;
      landing.speedRampStartMps += r[2] * w; landing.speedRampEndMps += r[3] * w;
    }
  return {
    regime: regimeKey(speedMph, vlaDeg, totalSpinRpm),
    bucket: landingBucket(vlaDeg, totalSpinRpm),
    dragScale: PROFILE.dragScale * drag,
    liftScale: PROFILE.liftScale * lift,
    launchDeg: vlaDeg,
    landing,
  };
}

// ------------------------------------------------------------------ impact

const B = BOUNCE;
const landingWeight = (vn) => rampUp(vn, B.flightRetentionRampStartMps, B.flightRetentionRampEndMps);

function flightRetention(vn, rpm) {
  const landing = B.flightTangentialRetentionBase * clamp(1 - rpm / B.flightSpinFactorDivisor, B.flightSpinFactorMin, 1);
  const w = landingWeight(vn);
  return w >= 1 ? landing : lerp(1, landing, w);
}

function restitution(vn, rpm, minimumSpeed) {
  const base = vn < minimumSpeed ? 0 : vn > B.corHighSpeedThreshold ? B.corHighSpeedCap : B.corBaseA + B.corBaseB * vn + B.corBaseC * vn * vn;
  const speedF = vn < B.corVelocityLowThreshold ? (B.corVelocityLowScale * vn) / B.corVelocityLowThreshold
    : vn < B.corVelocityMidThreshold ? lerp(B.corVelocityLowScale, 1, (vn - B.corVelocityLowThreshold) / (B.corVelocityMidThreshold - B.corVelocityLowThreshold))
    : 1;
  const spinF = rpm < B.spinCorLowSpinThreshold ? (B.spinCorLowSpinMaxReduction * rpm) / B.spinCorLowSpinThreshold
    : B.spinCorLowSpinMaxReduction + B.spinCorHighSpinAdditionalReduction * Math.min((rpm - B.spinCorLowSpinThreshold) / B.spinCorHighSpinRangeRpm, 1);
  return clamp(base * (1 - spinF * speedF), 0, 1);
}

const kineticEnergy = (v, w) => 0.5 * MASS * dot(v, v) + 0.5 * INERTIA * dot(w, w);

// Penner-style check on top of the contact impulse: incoming backspin takes a further share of
// 2Rω/7 off the forward speed on steep, energetic landings, down to zero (or a little spin-back)
function landingBite(v0, w0, v, w, n, surf, resp) {
  const inT = tangent(v0, n), inTL = len(inT);
  if (inTL < 0.01) return [v, w];
  const dir = scale(inT, 1 / inTL);
  const backspin = dot(w0, cross(dir, n));
  const angle = Math.atan2(-dot(v0, n), inTL);
  const bite = resp.spinBiteScale * surf.spinbackResponseScale * (2 / 7) * RADIUS * backspin *
    clamp((angle - surf.criticalAngle) / 0.1, 0, 1) * rampUp(len(v0), resp.speedRampStartMps, resp.speedRampEndMps);
  if (bite <= 0) return [v, w];
  const fwd = dot(tangent(v, n), dir);
  const floor = resp.maxSpinbackMps > 0 && surf.spinbackResponseScale > 1 ? -resp.maxSpinbackMps : 0;
  const bitten = Math.max(fwd - bite, Math.min(floor, fwd));
  if (bitten === fwd) return [v, w];
  const change = scale(dir, bitten - fwd);
  const v2 = add(v, change), w2 = add(w, cross(n, change), 1 / RADIUS);
  return kineticEnergy(v2, w2) <= kineticEnergy(v0, w0) ? [v2, w2] : [v, w];
}

// The ball strikes a surface with normal n: v and spin change in place. The first impact from
// flight takes the calibrated landing loss (and bite); later ones are roll-out bounces.
export function resolveImpact(v, spin, nIn, surf, firstImpact = true, response = null) {
  const n = validNormal(nIn);
  const vn = dot(v, n);
  if (vn >= 0) return;
  const incoming = -vn;
  const rpm = len(spin) / RAD_PER_RPM;
  const sb = surf.bounce;
  const minimum = firstImpact ? (sb ? sb.firstImpactMinimumSpeed : B.corKillThreshold)
    : (sb ? sb.laterImpactMinimumSpeed : Math.max(B.corKillThreshold, B.rolloutBounceCorKillThreshold));
  let e = restitution(incoming, rpm, minimum);
  if (!firstImpact) e *= sb ? sb.laterImpactScale : B.rolloutBounceCorScale;
  e *= surf.restitutionScale;
  const jn = MASS * (1 + e) * incoming;
  const arm = scale(n, -RADIUS);
  const contact = add(v, cross(spin, arm));
  const slip = tangent(contact, n);
  let jt = scale(slip, (-2 * MASS) / 7);
  let spinback = surf.spinbackResponseScale;
  if (firstImpact && spinback > 1) {
    const spinT = smoothRange(rpm, surf.spinbackSpinStartRpm, surf.spinbackSpinEndRpm);
    const speedT = smoothRange(len(v), surf.spinbackSpeedStartMps, surf.spinbackSpeedEndMps);
    const effective = surf.criticalAngle + surf.spinbackThetaBoostMax * spinT * speedT;
    const angle = Math.atan2(incoming, len(tangent(v, n)));
    spinback = 1 + (spinback - 1) * clamp((angle - effective) / 0.1, 0, 1);
  }
  const maxJt = surf.kineticFriction * Math.max(0, spinback) * jn;
  const jtl = len(jt);
  if (jtl > maxJt && jtl > 0) jt = scale(jt, maxJt / jtl);

  let nv = add(add(v, n, jn / MASS), jt, 1 / MASS);
  let nw = add(spin, cross(arm, jt), 1 / INERTIA);
  const speed = len(v);
  let keep = firstImpact ? flightRetention(incoming, rpm)
    : speed > 0.1 && B.rolloutSpinRatioThreshold > 0
      ? lerp(B.rolloutLowSpinRetention, B.rolloutHighSpinRetention, Math.min((len(spin) * RADIUS) / speed / B.rolloutSpinRatioThreshold, 1))
      : B.rolloutHighSpinRetention;
  // (a grazing contact spends little energy pushing into the surface)
  const normalShare = clamp((incoming * incoming) / Math.max(dot(v, v), 1e-12), 0, 1);
  keep = clamp(keep * lerp(1, surf.impactRetentionScale, normalShare), 0, 1);
  const keepT = (a, k) => add(scale(n, dot(a, n)), tangent(a, n), k);
  nv = keepT(nv, keep);
  nw = keepT(nw, keep);
  if (surf.impactPlowRatio > 0) {
    // Sand: the crater takes forward speed in proportion to how hard the ball drives in
    const tl = len(tangent(nv, n));
    const k = tl > 0 ? Math.max(0, tl - surf.impactPlowRatio * incoming) / tl : 1;
    nv = keepT(nv, k);
    nw = keepT(nw, k);
  }
  if (firstImpact && response && response.spinBiteScale > 0) [nv, nw] = landingBite(v, spin, nv, nw, n, surf, response);
  set(v, nv);
  set(spin, nw);
}

// True when the ball leaves the surface fast enough to count as a bounce rather than a roll
export function hasRebound(v, nIn, surf) {
  const n = validNormal(nIn);
  const s = dot(v, n);
  const h = surf.bounce ? surf.bounce.minimumBounceHeight : 0.25 / (2 * G);
  return s > 0 && s * s >= 2 * G * Math.max(n[1], 0) * h;
}

// The spin the ball leaves the club with: as measured, or, for a putt with none measured, rolling
export function launchSpin(v, measured, nIn) {
  if (dot(measured, measured) > 0) return measured;
  const n = validNormal(nIn);
  const vn = dot(v, n), t = tangent(v, n);
  const rolling = (1 - landingWeight(Math.abs(vn))) * (1 - rampUp(len(t), 12, 18));
  return rolling <= 0 ? [0, 0, 0] : scale(cross(n, t), rolling / RADIUS);
}

// ------------------------------------------------------------------ ground

const R = ROLLOUT;

export function spinFrictionMultiplier(rpm, speed) {
  const speedScale = speed < R.chipSpeedThreshold ? lerp(R.chipVelocityScaleMin, R.chipVelocityScaleMax, speed / R.chipSpeedThreshold)
    : speed < R.pitchSpeedThreshold ? lerp(R.chipVelocityScaleMax, 1, (speed - R.chipSpeedThreshold) / (R.pitchSpeedThreshold - R.chipSpeedThreshold))
    : 1;
  const spinScale = rpm < R.lowSpinThreshold ? lerp(1, R.lowSpinMultiplierMax, rpm / R.lowSpinThreshold)
    : rpm < R.midSpinThreshold ? lerp(R.lowSpinMultiplierMax, R.midSpinMultiplierMax, (rpm - R.lowSpinThreshold) / (R.midSpinThreshold - R.lowSpinThreshold))
    : lerp(R.midSpinMultiplierMax, R.highSpinMultiplierMax, Math.min((rpm - R.midSpinThreshold) / R.highSpinRampRange, 1));
  return 1 + (spinScale - 1) * speedScale;
}

const dampenSpin = (w, viscosity, dt) => scale(w, Math.exp((-8 * Math.PI * viscosity * RADIUS ** 3 / INERTIA) * dt));

function resistContactSpin(w, n, rollingFriction, load, dt) {
  const ns = dot(w, n);
  const remaining = Math.max(0, Math.abs(ns) - (rollingFriction * load * RADIUS / INERTIA) * dt);
  return add(w, n, Math.sign(ns) * remaining - ns);
}

// One step on the ground: sliding friction until the ball rolls, rolling resistance, grass drag
// and the slope. v and spin change in place; impactRpm is the spin at the first landing.
export function stepGround(v, spin, nIn, surf, impactRpm, dt = DT) {
  const n = validNormal(nIn);
  let vel = tangent(v, n);
  const slip = tangent(add(vel, cross(spin, scale(n, -RADIUS))), n);
  const load = MASS * G * Math.max(0, n[1]);
  const slope = add([0, -MASS * G, 0], n, load);
  const slopeL = len(slope);
  const staticCap = surf.rollingFriction * R.slopeResistanceRatio * load;
  if (dot(vel, vel) < 0.05 * 0.05 && dot(spin, spin) < 8 * 8 && slopeL <= staticCap) {
    set(v, [0, 0, 0]); set(spin, [0, 0, 0]);
    return;
  }
  if (dot(vel, vel) < 1e-4 && dot(slip, slip) < 1e-4 && slopeL <= staticCap) {
    set(v, [0, 0, 0]);
    set(spin, resistContactSpin(dampenSpin(spin, surf.grassViscosity, dt), n, surf.rollingFriction, load, dt));
    return;
  }
  const mult = spinFrictionMultiplier(Math.max(impactRpm, len(spin) / RAD_PER_RPM), len(vel));
  let w = spin;
  if (len(slip) >= R.tangentVelocityThreshold && dot(slip, slip) > 1e-7) {
    let j = scale(slip, (-2 * MASS) / 7);
    const maxJ = surf.kineticFriction * mult * load * dt, jl = len(j);
    if (jl > maxJ) j = scale(j, maxJ / jl);
    vel = add(vel, j, 1 / MASS);
    w = add(w, cross(scale(n, -RADIUS), j), 1 / INERTIA);
  }
  let force = add(slope, vel, -6 * Math.PI * RADIUS * surf.grassViscosity);
  if (dot(vel, vel) > 1e-7) {
    // (a green rolls at its own resistance whatever the landing spin)
    const rollMult = surf.rollingSpinScale === 1 ? mult : 1 + (mult - 1) * surf.rollingSpinScale;
    force = add(force, vel, (-surf.rollingFriction * rollMult * load) / len(vel));
  }
  let nv = add(vel, force, dt / MASS);
  if (dot(vel, nv) < 0 && slopeL <= staticCap) nv = [0, 0, 0];
  w = resistContactSpin(dampenSpin(w, surf.grassViscosity, dt), n, surf.rollingFriction, load, dt);
  set(v, nv);
  set(spin, w);
}

// True when a ball on the ground is slow enough and the slope cannot move it
export function canRest(v, spin, nIn, surf) {
  if (dot(v, v) >= 0.05 * 0.05 || dot(spin, spin) >= 0.5 * 0.5) return false;
  const n = validNormal(nIn);
  const g = [0, -MASS * G, 0];
  return len(tangent(g, n)) <= surf.rollingFriction * R.slopeResistanceRatio * MASS * G * Math.max(0, n[1]);
}
