// Checks the flight model against TrackMan PGA Tour averages (sea level, flat fairway).
// Run: npm test   (--fit grid-searches the aero coefficients, --fit-ground the turf model)
import { simulateShot, AERO, SURFACES, GROUND } from '../public/js/physics.js';

const flat = (surface = 'fairway') => ({
  heightAt: () => 0,
  normalAt: () => [0, 1, 0],
  surfaceAt: () => surface,
});

const Y = 1.09361;
// speed mph, launch deg, spin rpm -> carry yd, apex yd, typical fairway roll-out yd
const TOUR = [
  { name: 'Driver', speed: 167, vla: 10.9, spin: 2686, carry: 275, apex: 32, roll: 20 },
  { name: '3-wood', speed: 158, vla: 9.2, spin: 3655, carry: 243, apex: 30, roll: 15 },
  { name: '5-iron', speed: 132, vla: 12.1, spin: 5361, carry: 194, apex: 31, roll: 8 },
  { name: '7-iron', speed: 120, vla: 16.3, spin: 7097, carry: 172, apex: 32, roll: 5 },
  { name: 'PW', speed: 102, vla: 24.2, spin: 9304, carry: 136, apex: 29, roll: 3 },
];

const hit = (c, surface) =>
  simulateShot(flat(surface), { x: 0, y: 0, z: 0 },
    { speedMph: c.speed, vla: c.vla, hla: 0, backSpin: c.spin, sideSpin: 0 },
    { heading: 0, altitudeM: 0, stimp: 10 });

function error() {
  let e = 0;
  for (const c of TOUR) {
    const r = hit(c);
    e += ((r.carry * Y - c.carry) / c.carry) ** 2 + 0.3 * ((r.apex * Y - c.apex) / c.apex) ** 2;
  }
  return e;
}

if (process.argv.includes('--fit-ground')) {
  const chipRoll = () => {
    const r = simulateShot(flat('green'), { x: 0, y: 0.01, z: 0 },
      { speedMph: 38, vla: 24, hla: 0, backSpin: 5000, sideSpin: 0 }, { heading: 0, stimp: 10 });
    return (r.total - r.carry) * Y;
  };
  let best = { e: Infinity };
  for (const fric of [0.4, 0.55, 0.7, 0.85, 1.0])
    for (const eS of [0.7, 0.85, 1.0, 1.2])
      for (const bite of [1, 1.3, 1.6, 2, 2.5])
        for (const drag of [0.3, 0.6, 1.0, 1.5]) {
          Object.assign(SURFACES.fairway, { fric, e: eS, drag });
          Object.assign(SURFACES.green, { fric: fric + 0.05, e: eS * 0.9 });
          GROUND.spinBite = bite;
          let e = 0;
          for (const c of TOUR) { const r = hit(c); e += ((r.total - r.carry) * Y - c.roll) ** 2; }
          e += (chipRoll() - 6) ** 2;
          if (e < best.e) best = { e, fric, eS, bite, drag };
        }
  console.log('best ground', best);
  Object.assign(SURFACES.fairway, { fric: best.fric, e: best.eS, drag: best.drag });
  Object.assign(SURFACES.green, { fric: best.fric + 0.05, e: best.eS * 0.9 });
  GROUND.spinBite = best.bite;
}

if (process.argv.includes('--fit')) {
  let best = { e: Infinity };
  for (const cd0 of [0.2, 0.22, 0.24, 0.26])
    for (const cd1 of [0.1, 0.2, 0.3, 0.4])
      for (const cdv of [0, 0.04, 0.08, 0.12])
        for (const cl0 of [0, 0.04, 0.08])
          for (const cl1 of [0.6, 0.8, 1.0, 1.3])
            for (const clp of [0.6, 0.8, 1.0]) {
              Object.assign(AERO, { cd0, cd1, cdv, cl0, cl1, clp });
              const e = error();
              if (e < best.e) best = { e, cd0, cd1, cdv, cl0, cl1, clp };
            }
  console.log('best', best);
  Object.assign(AERO, best);
}

let failed = 0;
for (const c of TOUR) {
  const r = hit(c);
  const carry = r.carry * Y, total = r.total * Y, apex = r.apex * Y;
  const ok = Math.abs(carry - c.carry) / c.carry < 0.06;
  if (!ok) failed++;
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${c.name.padEnd(7)} carry ${carry.toFixed(0)} (tour ${c.carry})  total ${total.toFixed(0)}  apex ${apex.toFixed(0)} (tour ${c.apex})  roll ${(total - carry).toFixed(1)} (typ ${c.roll})`);
}

// Slice: positive spin axis should curve right.
const s = simulateShot(flat(), { x: 0, y: 0, z: 0 },
  { speedMph: 150, vla: 12, hla: 0, backSpin: 2800, sideSpin: 800 }, { heading: 0 });
console.log(`slice offline ${(s.offline * Y).toFixed(1)} yd (should be positive/right)`);
if (s.offline <= 0) failed++;

// Putt: 10 ft on stimp 10 needs ~ 1.83 m/s at the stimpmeter release... check a 3.6 mph putt distance.
const p = simulateShot({ ...flat('green') }, { x: 0, y: 0, z: 0 },
  { speedMph: 4.5, vla: 1, hla: 0, backSpin: 0, sideSpin: 0 }, { heading: 0, stimp: 10 });
console.log(`putt 4.5 mph rolls ${(p.total * 3.28084).toFixed(1)} ft`);

// Chip onto a green should check up, not run out
const ch = simulateShot(flat('green'), { x: 0, y: 0.01, z: 0 },
  { speedMph: 38, vla: 24, hla: 0, backSpin: 5000, sideSpin: 0 }, { heading: 0, stimp: 10 });
console.log(`chip carry ${(ch.carry * Y).toFixed(1)} yd, roll ${((ch.total - ch.carry) * Y).toFixed(1)} yd`);
if (ch.total - ch.carry > 12) failed++;

// Holing a putt
const h = simulateShot({ ...flat('green') }, { x: 0, y: 0, z: 0 },
  { speedMph: 3.5, vla: 0, hla: 0, backSpin: 0, sideSpin: 0 }, { heading: 0, stimp: 10, pin: { x: 0, y: 0, z: 2 } });
console.log(`putt at pin 2m: ${h.result}`);
if (h.result !== 'holed') failed++;

process.exit(failed ? 1 : 0);
