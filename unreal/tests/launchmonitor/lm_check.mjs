// Checks the game's launch monitor connections (OTGLaunchMonitorSubsystem) against the web
// version's own fakes and code: runs the game headless (UnrealEditor-Cmd -game -nullrhi) three
// times - Mevo+ directly (test/fake-mevo.js) plus a shot through OpenConnect (as FS Golf sends
// it), an FRP device (test/fake-frp.js), and a Garmin R10 through its bridge program
// (test/fake-bridge.js) - and compares every shot the game reports with what lib/mevo.js,
// lib/frp.js and server.js's toShot make of the same messages.
//
//   node unreal/tests/launchmonitor/lm_check.mjs        (the editor target built; no editor open)

import { spawn } from 'node:child_process';
import net from 'node:net';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { fakeMevo, flightPayload } from '../../../test/fake-mevo.js';
import { fakeFrp } from '../../../test/fake-frp.js';
import { decodeFlight, toBallData } from '../../../lib/mevo.js';
import { frpToBallData } from '../../../lib/frp.js';
import { normalizeSpin } from '../../../public/js/physics.js';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO = path.resolve(HERE, '../../..');
const PROJECT = path.join(REPO, 'unreal', 'OpenTeeGolf', 'OpenTeeGolf.uproject');
const EDITOR = process.env.UE_EDITOR_CMD || 'C:\\Program Files\\Epic Games\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe';

// server.js toShot (the reference: what the web version makes of a shot)
function toShot(b) {
  const lim = (v, lo, hi) => (Number.isFinite(+v) ? Math.min(hi, Math.max(lo, +v)) : 0);
  const spin = normalizeSpin(b);
  const backSpin = lim(spin.backSpin, -4000, 15000), sideSpin = lim(spin.sideSpin, -8000, 8000);
  const total = Number.isFinite(+b.TotalSpin) && Math.abs(b.TotalSpin) <= 16000 ? +b.TotalSpin : Math.hypot(backSpin, sideSpin);
  return {
    speedMph: lim(b.Speed, 0, 240), vla: lim(b.VLA, -20, 85), hla: lim(b.HLA, -60, 60), backSpin, sideSpin, totalSpin: total,
    spinAxis: Number.isFinite(+b.SpinAxis) ? lim(b.SpinAxis, -90, 90) : (Math.atan2(sideSpin, backSpin) * 180) / Math.PI,
  };
}

const failures = [];
const check = (ok, what) => { console.log(`  ${ok ? 'ok  ' : 'FAIL'} ${what}`); if (!ok) failures.push(what); };
function sameShot(got, want, what) {
  const keys = ['speedMph', 'vla', 'hla', 'backSpin', 'sideSpin', 'totalSpin', 'spinAxis'];
  const bad = keys.filter((k) => !(Math.abs(got[k] - want[k]) < 1e-3));
  check(!bad.length, `${what}: ${keys.map((k) => `${k} ${got[k]?.toFixed(2)}`).join(', ')}${bad.length ? ` (expected ${bad.map((k) => `${k} ${want[k].toFixed(4)}`).join(', ')})` : ''}`);
}

// One headless run of the game; resolves with its shots (OTGSHOT lines) and log
function runGame(args, { seconds, onLine = () => {} }) {
  return new Promise((resolve) => {
    // (one command line, quoted as Unreal reads it: it parses the raw command line itself)
    const all = ['-game', '-nullrhi', '-nosound', '-unattended', '-nosplash', '-stdout', '-FullStdOutLogOutput', `-quitafter=${seconds}`, ...args];
    const p = spawn(`"${EDITOR}" "${PROJECT}" ${all.join(' ')}`, { windowsHide: true, shell: true });
    const shots = [], lines = [];
    let buf = '';
    const take = (d) => {
      buf += d;
      let i;
      while ((i = buf.indexOf('\n')) >= 0) {
        const line = buf.slice(0, i).trimEnd();
        buf = buf.slice(i + 1);
        lines.push(line);
        const m = /OTGSHOT (\{.*\})/.exec(line);
        if (m) shots.push(JSON.parse(m[1]));
        if (/LogOTGLM/.test(line)) console.log('   ', line.replace(/^.*LogOTGLM: (Display: )?/, ''));
        onLine(line);
      }
    };
    p.stdout.setEncoding('utf8').on('data', take);
    p.stderr.setEncoding('utf8').on('data', take);
    const killer = setTimeout(() => { console.log('  (the game didn\'t quit; stopping it)'); p.kill(); }, (seconds + 120) * 1000);
    p.on('exit', (code) => { clearTimeout(killer); resolve({ shots, lines, code }); });
  });
}

const freePort = () => new Promise((r) => { const s = net.createServer().listen(0, '127.0.0.1', () => { const { port } = s.address(); s.close(() => r(port)); }); });

// ---------------------------------------------------------------- Mevo+ directly, and OpenConnect
async function mevoRun() {
  console.log('Mevo+ directly (fake Mevo+), and a shot through OpenConnect');
  const mevoPort = await freePort(), ocPort = await freePort();
  const { server, seen } = fakeMevo();
  await new Promise((r) => server.listen(mevoPort, '127.0.0.1', r));
  let fired = 0, lastArms = 0, ocReplies = [];
  // a shot each time the game arms it (twice): the first in full swing, the second after the game
  // has said the putter's in hand and it's the player's turn again
  const t = setInterval(() => {
    if (seen.armed && fired < 2 && seen.arms !== lastArms) { lastArms = seen.arms; fired++; setTimeout(() => seen.fireShot(), 300); }
  }, 100);
  // FS Golf's way: connect to the OpenConnect port, heartbeat, then a shot
  let ocSent = false;
  const sendOc = () => {
    if (ocSent) return;
    const sock = net.connect(ocPort, '127.0.0.1', () => {
      ocSent = true;
      const base = { DeviceID: 'Mevo+ (simulated)', Units: 'Yards', ShotNumber: 4242, APIversion: '1' };
      sock.write(JSON.stringify({ ...base, ShotDataOptions: { ContainsBallData: false, IsHeartBeat: true, LaunchMonitorIsReady: true } }));
      setTimeout(() => sock.write(JSON.stringify({ ...base, BallData: { Speed: 120.5, SpinAxis: -4.5, TotalSpin: 6100, HLA: -2.25, VLA: 18.5 }, ShotDataOptions: { ContainsBallData: true, IsHeartBeat: false, LaunchMonitorIsReady: true } })), 300);
      setTimeout(() => sock.end(), 1500);
    });
    sock.setEncoding('utf8');
    sock.on('data', (d) => ocReplies.push(d));
    sock.on('error', () => {});
  };
  const { shots } = await runGame(['-lm=direct', `-mevo=127.0.0.1:${mevoPort}`, '-lmturn', `-ocport=${ocPort}`, '-execat="12:OTG.LM.Player PT 3|12.5:OTG.LM.Turn 1|14:OTG.LM.Status"'], {
    seconds: 24,
    onLine: (l) => { if (/OpenConnect listener on port/.test(l)) setTimeout(sendOc, 500); },
  });
  clearInterval(t);
  server.close();
  const want = toShot(toBallData(decodeFlight(flightPayload()), null, null));
  const mevo = shots.filter((s) => s.source === 'mevo');
  check(mevo.length === 2, `two Mevo+ shots (got ${mevo.length})`);
  mevo.forEach((s, i) => sameShot(s, want, `Mevo+ shot ${i + 1}`));
  check(mevo[0] && Math.abs(mevo[0].clubSpeedMph - 45 * 2.23694) < 1e-3, `club speed ${mevo[0]?.clubSpeedMph?.toFixed(2)} mph`);
  check(JSON.stringify(seen.modes.slice(0, 2)) === '[1,3]', `detection modes: full swing, then putting (got ${JSON.stringify(seen.modes)})`);
  check(seen.arms >= 3, `armed for each shot (arm requests: ${seen.arms}, the first refused as the real one does)`);
  check(!seen.armed, 'left disarmed after the last shot (held until the next turn)');
  const oc = shots.filter((s) => s.source === 'launch-monitor');
  check(oc.length === 1, `one OpenConnect shot (got ${oc.length})`);
  if (oc[0]) sameShot(oc[0], toShot({ Speed: 120.5, SpinAxis: -4.5, TotalSpin: 6100, HLA: -2.25, VLA: 18.5 }), 'OpenConnect shot');
  const replies = ocReplies.join('');
  check(/"Code":201/.test(replies) && /"Club":"DR"/.test(replies), 'OpenConnect: told the connector the player (club DR)');
  check((replies.match(/"Code":200/g) || []).length >= 2, 'OpenConnect: heartbeat and shot answered');
}

// ---------------------------------------------------------------- FRP
async function frpRun() {
  console.log('Flight Relay Protocol device (fake FRP)');
  const dev = fakeFrp({ port: await freePort() });
  const port = await dev.listen();
  let fired = false;
  const t = setInterval(() => { if (!fired && dev.clients()) { fired = true; setTimeout(() => dev.shot({ club: { club_speed: '44.7mps', attack_angle: -1.5, face_angle: 0.8, path: 2.1 } }), 500); } }, 100);
  const { shots } = await runGame(['-lm=frp', `-frp=ws://127.0.0.1:${port}/frp`, '-execat="7:OTG.LM.Player PT 3|8:OTG.LM.Status"'], { seconds: 12 });
  clearInterval(t);
  const ball = { launch_speed: '67.2mps', launch_azimuth: -1.3, launch_elevation: 14.2, carry_distance: '180.5m', backspin_rpm: 3200, sidespin_rpm: -450, total_spin_rpm: 3231, spin_axis: -8.0 };
  const want = toShot(frpToBallData(ball, null));
  const frp = shots.filter((s) => s.source === 'frp');
  check(frp.length === 1, `one FRP shot (got ${frp.length})`);
  if (frp[0]) {
    sameShot(frp[0], want, 'FRP shot');
    check(Math.abs(frp[0].carryYd - 180.5 / 0.9144) < 1e-3, `carry ${frp[0].carryYd.toFixed(1)} yd`);
    check(Math.abs(frp[0].clubSpeedMph - 44.7 / 0.44704) < 1e-3, `club speed ${frp[0].clubSpeedMph.toFixed(2)} mph`);
    check(frp[0].device === 'OpenTeeGolf Fake Monitor', `device "${frp[0].device}"`);
  }
  const start = dev.got.find((m) => m.kind === 'start');
  check(start && start.version?.[0] === '0.1.0' && start.name === 'OpenTeeGolf', 'said hello (start, FRP 0.1.0)');
  const modes = dev.got.filter((m) => m.kind === 'set_detection_mode').map((m) => `${m.mode}/${m.handed}`);
  check(modes[0] === 'full/rh' && modes.includes('putting/rh'), `detection modes sent: ${modes.join(', ')}`);
  await dev.close();
}

// ---------------------------------------------------------------- Garmin R10 through its bridge
async function bridgeRun() {
  console.log('Garmin R10 through its bridge program (fake bridge)');
  const port = await freePort();
  const cmd = `${process.execPath}|${path.join(REPO, 'test', 'fake-bridge.js')}`;
  const { shots, lines } = await runGame(['-lm=r10', `-bridgeport=${port}`, `-bridgecmd="${cmd}"`, '-execat="8:OTG.LM.Status"'], { seconds: 12 });
  const want = toShot(frpToBallData({ launch_speed: '45mps', launch_elevation: 20, backspin_rpm: 7000, sidespin_rpm: 300 }, null));
  const r10 = shots.filter((s) => s.source === 'r10');
  check(r10.length === 1, `one R10 shot (got ${r10.length})`);
  if (r10[0]) {
    sameShot(r10[0], want, 'R10 shot');
    check(r10[0].device === 'Garmin Approach R10', `device "${r10[0].device}"`);
  }
  check(lines.some((l) => /Launch monitor r10: ready/.test(l)), 'reported ready once the bridge had the device');
  // the bridge program stops with the game
  const stillThere = await new Promise((r) => { const s = net.connect(port, '127.0.0.1', () => { s.destroy(); r(true); }); s.on('error', () => r(false)); });
  check(!stillThere, 'bridge program stopped with the game');
}

// ---------------------------------------------------------------- Mevo+ dropping out, and quitting while armed
async function mevoDropRun() {
  console.log('Mevo+ dropping the connection, and the game quitting while it is armed');
  const mevoPort = await freePort();
  const { server, seen } = fakeMevo();
  const socks = new Set();
  server.on('connection', (s) => { socks.add(s); s.on('close', () => socks.delete(s)); });
  await new Promise((r) => server.listen(mevoPort, '127.0.0.1', r));
  let connections = 0, dropped = false, armedAfterDrop = false;
  server.on('connection', () => connections++);
  const t = setInterval(() => {
    if (seen.armed && !dropped) { dropped = true; seen.armed = false; for (const s of socks) s.destroy(); }
    else if (dropped && seen.armed) armedAfterDrop = true;
  }, 100);
  const { lines } = await runGame(['-lm=direct', `-mevo=127.0.0.1:${mevoPort}`, '-lmturn', '-ocport=0'], { seconds: 16 });
  clearInterval(t);
  server.close();
  check(connections >= 2, `connected again after the drop (${connections} connections)`);
  check(armedAfterDrop, 'armed again after reconnecting');
  check(lines.some((l) => /connection to the Mevo\+ dropped|closed the connection/.test(l)), 'noticed the drop');
  check(!seen.armed, 'disarmed when the game quit');
}

await mevoRun();
await mevoDropRun();
await frpRun();
await bridgeRun();
console.log(failures.length ? `\n${failures.length} check(s) failed` : '\nAll launch monitor checks passed');
process.exit(failures.length ? 1 : 0);
