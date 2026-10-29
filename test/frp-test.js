// Checks the Flight Relay Protocol controller (lib/frp.js) against the spec's units and signs and a
// simulated device: handshake, telemetry, shots (in either event order, missing parts, unknown
// shots), detection mode commands and reconnecting.
// Usage: node test/frp-test.js
import { FrpController, frpToBallData, unitValue } from '../lib/frp.js';
import { BRIDGES, BridgeProcess } from '../lib/bridges.js';
import { fakeFrp } from './fake-frp.js';
import { fileURLToPath } from 'node:url';

let failed = 0;
const check = (ok, msg) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${msg}`); if (!ok) failed++; };
const near = (a, b, tol = 0.05) => Number.isFinite(a) && Math.abs(a - b) <= tol;
const wait = (ms) => new Promise((r) => setTimeout(r, ms));
async function until(fn, ms = 4000) {
  const t0 = Date.now();
  while (Date.now() - t0 < ms) { if (fn()) return true; await wait(20); }
  return false;
}

// ---- 1. Units and conversion
const SPEED = { mps: 1, mph: 0.44704, kph: 1 / 3.6, fps: 0.3048 };
check(near(unitValue('67.2mps', SPEED), 67.2, 1e-9) && near(unitValue('150.3mph', SPEED), 67.19, 0.01) && near(unitValue('100kph', SPEED), 27.78, 0.01), 'speeds in m/s, mph and km/h');
check(unitValue('12', SPEED) === null && unitValue('12parsecs', SPEED) === null && unitValue(null, SPEED) === null, 'untagged or unknown units are rejected');
let b = frpToBallData({ launch_speed: '67.2mps', launch_azimuth: -1.3, launch_elevation: 14.2, carry_distance: '180.5m', backspin_rpm: 3200, sidespin_rpm: -450, total_spin_rpm: 3231, spin_axis: -8 });
check(near(b.Speed, 150.32) && b.HLA === -1.3 && b.VLA === 14.2, 'ball speed to mph; launch direction and angle as sent (positive = right)');
check(b.BackSpin === 3200 && b.SideSpin === -450 && b.TotalSpin === 3231 && b.SpinAxis === -8 && near(b.CarryDistance, 197.4), 'spin components and carry (yards)');
b = frpToBallData({ launch_speed: '150mph', launch_elevation: 12, total_spin_rpm: 3000, spin_axis: 10 });
check(near(b.Speed, 150) && near(b.BackSpin, 2954.4, 0.5) && near(b.SideSpin, 520.9, 0.5) && b.HLA === 0, 'total spin and axis only: split into back and side spin');
b = frpToBallData({ launch_speed: '5mps', launch_elevation: 0, backspin_rpm: 40 });
check(b.SideSpin === 0 && b.SpinAxis === 0 && b.TotalSpin === 40, 'back spin alone (a putt)');
check(frpToBallData({ launch_elevation: 12 }) === null && frpToBallData(null) === null, 'no ball speed: no shot');
b = frpToBallData({ launch_speed: '60mps' }, { club_speed: '40mps', path: -2.1, attack_angle: -3.5, face_angle: 1.2, dynamic_loft: 18.4, smash_factor: 1.5 });
check(near(b.ClubData.Speed, 89.48) && b.ClubData.Path === -2.1 && b.ClubData.FaceToTarget === 1.2 && b.ClubData.AngleOfAttack === -3.5, 'club data');

// ---- 2. Against a simulated device
const dev = fakeFrp();
const port = await dev.listen();
const frp = new FrpController({ url: `ws://127.0.0.1:${port}/frp`, log: () => {} });
const shots = [], statuses = [];
frp.on('shot', (ball, meta) => shots.push({ ball, meta }));
frp.on('status', (s) => statuses.push(s));
frp.setMode({ mode: 'putting', handed: 'lh' });
frp.start();
check(await until(() => frp.status.state === 'connected'), 'connects and completes the handshake');
const start = dev.got.find((m) => m.kind === 'start');
check(start && Array.isArray(start.version) && start.version.includes('0.1.0') && start.name === 'OpenTeeGolf', 'sends "start" with the versions it speaks');
check(await until(() => frp.status.ready === true && frp.status.battery === 87 && frp.status.model === 'OpenTeeGolf Fake Monitor'), 'reads the telemetry (ready, battery, model)');
check(await until(() => dev.got.some((m) => m.kind === 'set_detection_mode' && m.mode === 'putting' && m.handed === 'lh')), 'sends the detection mode set before connecting');
frp.setMode({ mode: 'chipping' });
check(await until(() => dev.got.some((m) => m.kind === 'set_detection_mode' && m.mode === 'chipping' && m.handed === 'lh')), 'sends a new detection mode (handedness kept)');
const nModes = dev.got.filter((m) => m.kind === 'set_detection_mode').length;
frp.setMode({ mode: 'chipping', handed: 'lh' });
await wait(100);
check(dev.got.filter((m) => m.kind === 'set_detection_mode').length === nModes, "doesn't repeat an unchanged mode");

dev.shot();
check(await until(() => shots.length === 1), 'a shot arrives after shot_finished');
check(near(shots[0].ball.Speed, 150.32) && shots[0].ball.HLA === -1.3 && shots[0].meta.shotNumber === 1 && shots[0].meta.device === 'Fake-FRP-0001', 'with its numbers, shot number and device');
dev.shot({ ball: { launch_speed: '120mph', launch_elevation: 18, total_spin_rpm: 6000, spin_axis: -4 }, club: { club_speed: '85mph', path: 1 }, order: 'club-first' });
check(await until(() => shots.length === 2) && near(shots[1].ball.Speed, 120) && near(shots[1].ball.ClubData.Speed, 85), 'club data before ball data: still one shot with both');
dev.shot({ ball: null, club: { club_speed: '80mph' } });
await wait(150);
check(shots.length === 2, 'a shot with no ball data (missed ball) is not played');
// a shot whose trigger was missed (connected mid-shot)
const key = { shot_id: 'mid-shot-1', shot_number: 99 };
dev.send({ device: 'Fake-FRP-0001', event: { kind: 'ball_flight', key, ball: { launch_speed: '30mps', launch_elevation: 30 } } });
dev.send({ device: 'Fake-FRP-0001', event: { kind: 'shot_finished', key } });
check(await until(() => shots.length === 3) && shots[2].meta.shotNumber === 99, 'a shot whose trigger was missed still plays');
dev.send({ device: 'Fake-FRP-0001', event: { kind: 'something_new', key, x: 1 } });
dev.send({ kind: 'alert', severity: 'warn', message: 'just saying' });
await wait(100);
check(frp.status.state === 'connected' && shots.length === 3, 'unknown events and warnings are ignored');
dev.telemetry({ ready: 'false' });
check(await until(() => frp.status.ready === false), 'ready=false from the device');

// reconnecting after the device drops the connection
dev.dropAll();
check(await until(() => frp.status.state === 'connecting'), 'notices the connection drop');
check(await until(() => frp.status.state === 'connected' && dev.clients() === 1, 6000), 'reconnects by itself');
check(await until(() => dev.got.filter((m) => m.kind === 'start').length === 2 && dev.got.filter((m) => m.kind === 'set_detection_mode').length >= 3), 'repeats the handshake and its detection mode');
dev.shot();
check(await until(() => shots.length === 4), 'shots arrive after reconnecting');

frp.stop();
await wait(100);
check(frp.status.state === 'off' && dev.clients() === 0, 'stops cleanly');
await dev.close();

// ---- 3. A bridge program: started, its progress read, its shots through FRP, restarted when it stops
const fakeBridge = fileURLToPath(new URL('./fake-bridge.js', import.meta.url));
{
  const br = new BridgeProcess('r10', { port: 5899, log: () => {}, command: [process.execPath, fakeBridge, '--fail'] });
  const seen = [];
  br.on('status', (s) => seen.push(s.state));
  br.start();
  check(await until(() => seen.includes('searching') && br.status.error && /no R10/.test(br.status.error)), 'bridge: "not found" is reported');
  br.restart([]);
  br.command = [process.execPath, fakeBridge];
  check(await until(() => br.status.state === 'ready', 6000), 'bridge: tried again, found it, ready');
  const f = new FrpController({ url: br.url, log: () => {} });
  const got = [];
  f.on('shot', (ball) => got.push(ball));
  f.start();
  check(await until(() => got.length === 1, 6000) && near(got[0].Speed, 100.66) && got[0].BackSpin === 7000, "bridge: its shot arrives through FRP");
  check(f.status.model === 'Garmin Approach R10', 'bridge: device named from its telemetry');
  f.stop();
  br.stop();
  await wait(200);
  check(br.proc === null && br.status.state === 'off', 'bridge: stopped with the game');
}

// ---- 4. Bridge downloads are pinned
for (const [k, br] of Object.entries(BRIDGES)) {
  check(/^v\d+\.\d+\.\d+$/.test(br.version) && Object.values(br.files).every((f) => /^[0-9a-f]{64}$/.test(f.sha256)), `${br.name} bridge: exact release and SHA-256 for every download`);
}

console.log(failed ? `${failed} FRP checks FAILED` : 'all FRP checks passed');
process.exit(failed ? 1 : 0);
