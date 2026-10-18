// Checks the direct Mevo+ client (lib/mevo.js) against the documented wire format and a simulated
// device that answers the handshake, arms, fires a shot, and re-arms.
// Usage: node test/mevo-test.js
import {
  BUS, MODES, encodeFrame, parseFrame, FrameReader, readFloat40, writeFloat40, toBallData, decodeFlight, MevoClient,
} from '../lib/mevo.js';
import { fakeMevo, flightPayload } from './fake-mevo.js';

let failed = 0;
const check = (ok, msg) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${msg}`); if (!ok) failed++; };
const hex = (b) => Buffer.from(b).toString('hex').toUpperCase().match(/../g).join(' ');

// ---- 1. Wire format (worked examples from ironsight's WIRE.md)
check(hex(encodeFrame(0x40, 0xaa, [1, 1])) === 'F0 40 10 AA 01 01 00 FC F1', 'status poll frame matches the documented bytes');
const stuffed = encodeFrame(0x30, 0xbf, [0xf0, 0xf1, 0xfd, 0xfa]);
check(!stuffed.subarray(1, -1).some((b) => b === 0xf0 || b === 0xf1), 'byte stuffing removes frame markers from the interior');
const back = parseFrame(stuffed);
check(back && back.type === 0xbf && hex(back.payload) === 'F0 F1 FD FA', 'stuffed frame decodes back to the original payload');
const bad = Buffer.from(encodeFrame(0x40, 0xaa, [1, 1])); bad[4] ^= 1;
check(parseFrame(bad) === null, 'a corrupted frame fails its checksum');
for (const [v, h] of [[1, '00 01 40 00 00'], [12.5, '00 04 64 00 00'], [-2.3, '00 02 B6 66 67'], [100, '00 07 64 00 00'], [0.0254, 'FF FB 68 09 E2']]) {
  const enc = writeFloat40(v);
  const docBytes = Buffer.from(h.replace(/ /g, ''), 'hex');
  // (the documented 0.0254 differs from a straight encode in the last mantissa bits; both decode to it)
  check(Math.abs(readFloat40(docBytes, 0) - v) < 1e-5 && Math.abs(readFloat40(Buffer.from(enc), 0) - v) < 1e-6 && hex(enc).slice(0, 8) === h.slice(0, 8),
    `FLOAT40 ${v}: documented bytes ${h} decode correctly, encoding round-trips`);
}
const reader = new FrameReader();
const two = Buffer.concat([encodeFrame(0x10, 0xe3, [0x41]), encodeFrame(0x10, 0xe3, [0x42])]);
const got = [...reader.push(two.subarray(0, 5)), ...reader.push(two.subarray(5))];
check(got.length === 2 && got[1].payload[0] === 0x42, 'frames split across TCP packets are reassembled');

// ---- 2. A simulated Mevo+ (test/fake-mevo.js)
const { server, seen } = fakeMevo();
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const port = server.address().port;
const client = new MevoClient({ host: '127.0.0.1', port, log: (m) => console.log(`     ${m}`) });
const states = [];
client.on('status', (s) => states.push(s.state));
const until = (pred, ms, what) => new Promise((resolve, reject) => {
  const t0 = Date.now();
  const tick = () => { if (pred()) resolve(); else if (Date.now() - t0 > ms) reject(new Error(`timed out waiting for ${what}`)); else setTimeout(tick, 20); };
  tick();
});
try {
  const t0 = Date.now();
  client.start();
  await until(() => client.state === 'ready', 8000, 'ready');
  check(true, `handshake, configure and arm completed in ${Date.now() - t0} ms (one refused arm retried)`);
  check(client.info?.ssid === 'FS M2-123456' && client.info?.battery === 87 && client.info?.model === 'Mevo+', `device identified (${client.info?.model} ${client.info?.ssid}, battery ${client.info?.battery}%)`);
  check(seen.modes.at(-1) === MODES.full, 'full-swing detection mode set');

  const shot = new Promise((r) => client.once('shot', r));
  seen.fireShot();
  const b = await Promise.race([shot, new Promise((_, rej) => setTimeout(() => rej(new Error('no shot')), 8000))]);
  check(Math.abs(b.Speed - 150.3) < 0.2 && b.VLA === 12 && b.HLA === 1.3, `ball: ${b.Speed.toFixed(1)} mph, VLA ${b.VLA}, HLA ${b.HLA} (right)`);
  check(b.BackSpin === 2700 && b.SideSpin === 450 && Math.abs(b.SpinAxis - 9.46) < 0.05, `spin: ${b.BackSpin} back, ${b.SideSpin} side, axis ${b.SpinAxis.toFixed(1)}`);
  check(b.ClubData && Math.abs(b.ClubData.Speed - 100.7) < 0.2 && b.ClubData.Path === 2.1, `club: ${b.ClubData?.Speed.toFixed(1)} mph, path ${b.ClubData?.Path}`);
  await until(() => client.state === 'ready' && client.armed, 5000, 're-arm');
  check(true, 're-armed after the shot');

  client.setMode('putting');
  await until(() => seen.modes.at(-1) === MODES.putting && client.armed && client.state === 'ready', 5000, 'putting mode');
  check(true, 'switched to putting mode and re-armed');

  // Device readings for the setup page
  await until(() => client.info?.tilt != null, 3000, 'tilt');
  check(client.info.tilt === 0.6 && client.info.roll === -1.8 && client.info.tempC === 31.5, `level and temperature read (tilt ${client.info.tilt}°, roll ${client.info.roll}°, ${client.info.tempC} °C)`);

  // Player setup: settings and radar calibration sent, then re-armed
  await client.applySetup({ distanceM: 2.44, surfaceIn: 1, teeHeightIn: 2, ballType: 1 });
  const cal = seen.cal || [];
  check(seen.params?.join(',') === '6,15,38' && ((cal[1] << 8) | cal[2]) === 2440 && cal[4] === 25, `setup sent: settings ${seen.params?.join('/')}, distance ${(cal[1] << 8) | cal[2]} mm, surface ${cal[4]} mm`);
  check(client.armed && client.state === 'ready', 're-armed with the new setup');

  // Live camera: the Pi says "not ready" first, then streams
  await client.camera(true);
  check(seen.streaming && client.info.streaming, `camera streaming after ${seen.camTries} start requests`);
  await client.camera(false);
  check(!seen.streaming, 'camera back to normal mode');

  // Keepalive runs every second without upsetting anything
  await new Promise((r) => setTimeout(r, 2300));
  check(client.state === 'ready', 'still ready after keepalives');

  // Disarm when done, stay connected, arm again
  await client.setArmed(false);
  check(client.state === 'idle' && !seen.armed, 'disarmed on request (device no longer armed)');
  await new Promise((r) => setTimeout(r, 1500));
  check(client.state === 'idle', 'stays connected while disarmed');
  await client.setArmed(true);
  check(client.state === 'ready' && seen.armed, 'armed again on request');

  // Disconnect: disarms first, then hangs up and stays off
  await client.disconnect();
  await new Promise((r) => setTimeout(r, 100));
  check(!seen.armed && client.state === 'off' && !client.running, 'disconnect disarms the device and hangs up');

  // First-shot case: spin missing from 0xD4, taken from 0xEF instead
  const f = decodeFlight(Buffer.from(flightPayload()));
  f.backspin = 0; f.sidespin = 0;
  const fb = toBallData(f, { total: 2737, axis: 9.5 }, null);
  check(Math.abs(fb.BackSpin - 2699) < 3 && Math.abs(fb.SideSpin - 452) < 3, `spin falls back to the spin result (${fb.BackSpin}/${fb.SideSpin})`);
} catch (err) {
  check(false, err.message);
} finally {
  client.stop();
  server.close();
}
// ---- 3. A unit that leaves the information questions unanswered (camera board busy, different
// firmware) still connects and arms, without waiting on every skipped question
{
  const quiet = (f) => (f.dest === BUS.PI && f.type !== 0xaa) || f.type === 0x48 || f.type === 0x9b || f.type === 0xd2;
  const { server: s2 } = fakeMevo({ quiet });
  await new Promise((r) => s2.listen(0, '127.0.0.1', r));
  const logs = [];
  const c2 = new MevoClient({ host: '127.0.0.1', port: s2.address().port, log: (m) => logs.push(m) });
  const t0 = Date.now();
  try {
    c2.start();
    await until(() => c2.state === 'ready', 25000, 'ready with unanswered questions');
    check(true, `connected and armed in ${((Date.now() - t0) / 1000).toFixed(1)} s with unanswered questions skipped`);
    check(logs.some((l) => /no answer to: .*camera info/.test(l)), 'the skipped questions are logged');
  } catch (err) {
    check(false, `${err.message} (${logs.join(' | ')})`);
  } finally {
    c2.stop();
    s2.close();
  }
}

// ---- 4. If the device hangs up mid-handshake, the client notices at once and says where it was
{
  let n = 0;
  const { server: s3 } = fakeMevo({ quiet: (f) => f.dest === BUS.AVR && f.type === 0xaa && ++n > 0 });
  const conns = [];
  s3.on('connection', (sock) => { conns.push(sock); setTimeout(() => sock.destroy(), 300); });
  await new Promise((r) => s3.listen(0, '127.0.0.1', r));
  const c3 = new MevoClient({ host: '127.0.0.1', port: s3.address().port });
  const err = new Promise((r) => c3.on('status', (s) => { if (s.state === 'searching') r(s.error); }));
  const t0 = Date.now();
  c3.start();
  const e = await Promise.race([err, new Promise((r) => setTimeout(() => r(null), 5000))]);
  check(e && /closed the connection/.test(e) && /reading the radar/.test(e) && Date.now() - t0 < 2000, `a hang-up is reported straight away: "${e}"`);
  c3.stop();
  s3.close();
}
console.log(`states: ${[...new Set(states)].join(' → ')}`);
if (failed) { console.log(`${failed} check(s) failed`); process.exit(1); }
console.log('all Mevo+ checks passed');
process.exit(0);
