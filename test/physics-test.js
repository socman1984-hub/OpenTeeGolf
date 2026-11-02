// Checks the ball physics (OpenFairway, public/js/openfairway.js) on a flat course: that the port
// matches the OpenFairwayPhysics plugin's own results, and how it compares with launch monitors.
// Run: npm test
import { simulateShot, puttSpeed } from '../public/js/physics.js';

const flat = (surface = 'fairway') => ({
  heightAt: () => 0,
  normalAt: () => [0, 1, 0],
  surfaceAt: () => surface,
});

const Y = 1.09361, FT = 3.28084;
let failed = 0;
const check = (ok, msg) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${msg}`); if (!ok) failed++; };
const hit = (shot, opts = {}, surface = 'fairway') =>
  simulateShot(flat(surface), { x: 0, y: 0, z: 0 }, { hla: 0, sideSpin: 0, ...shot }, { heading: 0, altitudeM: 0, stimp: 10, ...opts });
// The plugin's air: 75 F at sea level, dry, calm
const OF_AIR = { tempC: (75 - 32) / 1.8, humidity: 0 };

// 1. The port: the plugin's headless simulator gives the same shots (OpenFairwayPhysics README and
// Docs/FlightScopeAnalysis.md, gameplay profile)
{
  const axis = (-9.9 * Math.PI) / 180;
  const r = hit({ speedMph: 150, vla: 12.5, hla: 1.5, backSpin: 2335 * Math.cos(axis), sideSpin: 2335 * Math.sin(axis) }, OF_AIR);
  const h = (1.5 * Math.PI) / 180, along = (q) => (q.x * Math.sin(h) + q.z * Math.cos(h)) * Y;
  check(Math.abs(along(r.carryPoint) - 248.41) < 0.5 && Math.abs(along(r.rest) - 262.51) < 0.5 && Math.abs(r.apex * FT - 83.9) < 0.5,
    `plugin drive: carry ${along(r.carryPoint).toFixed(2)} (248.41) total ${along(r.rest).toFixed(2)} (262.51) apex ${(r.apex * FT).toFixed(1)} ft (83.9)`);
  const w = hit({ speedMph: 114.46, vla: 6.95, backSpin: 1931.9, sideSpin: -47.8 }, OF_AIR);
  check(Math.abs(w.carry * Y - 121.55) < 0.5 && Math.abs(w.total * Y - 159.24) < 0.5 && Math.abs(w.flightTime - 2.741) < 0.02,
    `plugin low wood: carry ${(w.carry * Y).toFixed(2)} (121.55) total ${(w.total * Y).toFixed(2)} (159.24) hang ${w.flightTime.toFixed(3)} s (2.741)`);
}

// 2. FlightScope (the plugin's references: Trajectory Optimizer, 75 F)
for (const c of [
  { name: 'low wood', speedMph: 114.5, vla: 6.95, backSpin: 1933, carry: 119.8, apexFt: 14.6 },
  { name: 'iron', speedMph: 75.7, vla: 20.78, backSpin: 4463, carry: 87.6, apexFt: 35 },
]) {
  const r = hit(c, OF_AIR);
  check(Math.abs(r.carry * Y - c.carry) < 3, `FlightScope ${c.name}: carry ${(r.carry * Y).toFixed(1)} (FS ${c.carry})  apex ${(r.apex * FT).toFixed(1)} ft (FS ${c.apexFt})`);
}

// 3. TrackMan PGA Tour averages (sea level, flat fairway). OpenFairway carries these fast shots
// long, most of all the fast, low-launch 3-wood and 5-iron (about +8%): its calibration has few
// shots above 125 mph. `was` is the game's previous model.
const TOUR = [
  { name: 'Driver', speed: 167, vla: 10.9, spin: 2686, carry: 275, apex: 32, roll: 20, was: [270, 288] },
  { name: '3-wood', speed: 158, vla: 9.2, spin: 3655, carry: 243, apex: 30, roll: 15, was: [255, 271] },
  { name: '5-iron', speed: 132, vla: 12.1, spin: 5361, carry: 194, apex: 31, roll: 8, was: [204, 214] },
  { name: '7-iron', speed: 120, vla: 16.3, spin: 7097, carry: 172, apex: 32, roll: 5, was: [174, 180] },
  { name: 'PW', speed: 102, vla: 24.2, spin: 9304, carry: 136, apex: 29, roll: 3, was: [133, 134] },
];
for (const c of TOUR) {
  const r = hit({ speedMph: c.speed, vla: c.vla, backSpin: c.spin });
  const carry = r.carry * Y, total = r.total * Y, apex = r.apex * Y;
  check(Math.abs(carry - c.carry) / c.carry < 0.1 && Math.abs(apex - c.apex) / c.apex < 0.2,
    `${c.name.padEnd(7)} carry ${carry.toFixed(0)} (tour ${c.carry}, was ${c.was[0]})  total ${total.toFixed(0)} (was ${c.was[1]})  apex ${apex.toFixed(0)} (tour ${c.apex})  roll ${(total - carry).toFixed(1)} (typ ${c.roll})`);
}

// 4. Typical amateur shots against typical launch-monitor numbers
for (const c of [
  { name: 'driver 150 mph', speedMph: 150, vla: 12, backSpin: 2700, carry: [235, 255], total: [255, 280] },
  { name: '7-iron 118 mph', speedMph: 118, vla: 17, backSpin: 6800, carry: [165, 182], total: [165, 185] },
  { name: 'PW 95 mph', speedMph: 95, vla: 25, backSpin: 9000, carry: [115, 130], total: [113, 132] },
]) {
  const r = hit(c), carry = r.carry * Y, total = r.total * Y;
  check(carry >= c.carry[0] && carry <= c.carry[1] && total >= c.total[0] && total <= c.total[1],
    `${c.name}: carry ${carry.toFixed(1)}  total ${total.toFixed(1)}  apex ${(r.apex * FT).toFixed(0)} ft`);
}

// Slice: positive side spin curves right
const s = hit({ speedMph: 150, vla: 12, backSpin: 2800, sideSpin: 800 });
check(s.offline > 10, `slice offline ${(s.offline * Y).toFixed(1)} yd (right)`);

// Wind: about 12 yd lost into a 10 mph headwind (the USGTF rule the plugin matches), gained downwind
{
  const drive = { speedMph: 150, vla: 12.5, backSpin: 2335 };
  const calm = hit(drive).carry * Y;
  const into = hit(drive, { wind: { mps: 4.4704, fromDeg: 0 } }).carry * Y - calm;
  const down = hit(drive, { wind: { mps: 4.4704, fromDeg: 180 } }).carry * Y - calm;
  check(into < -6 && into > -20 && down > 4, `10 mph wind: ${into.toFixed(1)} yd into it, +${down.toFixed(1)} yd downwind`);
}

// Green speed: a stimpmeter ball rolls the stimp, and puttSpeed gives the pace for a distance
const st = hit({ speedMph: 1.83 / 0.44704, vla: 0, backSpin: 0 }, {}, 'green');
check(Math.abs(st.total * FT - 10) < 0.3, `stimpmeter ball rolls ${(st.total * FT).toFixed(2)} ft on stimp 10`);
const pv = puttSpeed(6, 10), pr = hit({ speedMph: pv / 0.44704, vla: 1, backSpin: 0 }, {}, 'green');
check(Math.abs(pr.total - 6) < 0.15, `putt struck at ${pv.toFixed(2)} m/s for 6 m rolls ${pr.total.toFixed(2)} m`);

// Chip onto a green checks up rather than running out
const ch = hit({ speedMph: 38, vla: 24, backSpin: 5000 }, {}, 'green');
check(ch.total - ch.carry < 12 / Y, `chip carry ${(ch.carry * Y).toFixed(1)} yd, roll ${((ch.total - ch.carry) * Y).toFixed(1)} yd`);

// Sand: a full shot plugs near its pitch mark
const sd = hit({ speedMph: 118, vla: 17, backSpin: 6800 }, {}, 'sand');
check(sd.total - sd.carry < 2, `7-iron into sand rolls ${((sd.total - sd.carry) * Y).toFixed(1)} yd`);

// Holing a putt
const h = simulateShot(flat('green'), { x: 0, y: 0, z: 0 },
  { speedMph: 3.5, vla: 0, hla: 0, backSpin: 0, sideSpin: 0 }, { heading: 0, stimp: 10, pin: { x: 0, y: 0, z: 2 } });
check(h.result === 'holed', `putt at pin 2m: ${h.result}`);

// Break: a straight putt across a green rising 2% towards +x curls downhill (towards -x). The
// normal is worked out as course.js does it, so the two can't disagree on which way is down.
const tilted = (() => {
  const heightAt = (x) => 0.02 * x;
  const normalAt = (x, z) => { const a = heightAt(x - 1) - heightAt(x + 1), b = 2, l = Math.hypot(a, b); return [a / l, b / l, 0]; };
  return { heightAt, normalAt, surfaceAt: () => 'green' };
})();
const br = simulateShot(tilted, { x: 0, y: 0, z: 0 },
  { speedMph: 6, vla: 1, hla: 0, backSpin: 0, sideSpin: 0 }, { heading: 0, stimp: 10 });
check(br.rest.x < -0.3, `putt across a 2% slope breaks ${(br.rest.x * 100).toFixed(0)} cm (downhill)`);

process.exit(failed ? 1 : 0);
