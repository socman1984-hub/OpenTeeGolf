// A simulated Flight Relay Protocol device (a launch monitor or bridge, like the Garmin R10 and
// Square Golf ones) for testing lib/frp.js without hardware: a tiny WebSocket server that answers
// the "start" handshake, sends its telemetry, records the commands it gets, and fires shots on
// request in FRP's event sequence.
//
// Standalone: node test/fake-frp.js [port=5880] [seconds between shots=15]
// then pick "Other: Flight Relay Protocol device" in the game, address ws://127.0.0.1:5880/frp
import http from 'node:http';
import crypto from 'node:crypto';
import { pathToFileURL } from 'node:url';

// One WebSocket text frame, server to client (never masked)
function frame(text) {
  const data = Buffer.from(text, 'utf8');
  const n = data.length;
  const head = n < 126 ? Buffer.from([0x81, n]) : n < 65536 ? Buffer.from([0x81, 126, n >> 8, n & 255]) : null;
  return Buffer.concat([head, data]);
}

// Text frames from a client (always masked); calls onText for each, onClose on a close frame
function reader(onText, onClose) {
  let buf = Buffer.alloc(0);
  return (chunk) => {
    buf = Buffer.concat([buf, chunk]);
    for (;;) {
      if (buf.length < 2) return;
      const op = buf[0] & 15, masked = buf[1] & 128;
      let n = buf[1] & 127, o = 2;
      if (n === 126) { if (buf.length < 4) return; n = buf.readUInt16BE(2); o = 4; }
      else if (n === 127) { if (buf.length < 10) return; n = Number(buf.readBigUInt64BE(2)); o = 10; }
      const mask = masked ? buf.subarray(o, o + 4) : null;
      if (masked) o += 4;
      if (buf.length < o + n) return;
      const data = Buffer.from(buf.subarray(o, o + n));
      if (mask) for (let i = 0; i < n; i++) data[i] ^= mask[i & 3];
      buf = buf.subarray(o + n);
      if (op === 1) onText(data.toString('utf8'));
      else if (op === 8) return onClose();
    }
  };
}

export function fakeFrp({ port = 0, device = 'Fake-FRP-0001', model = 'Fake Monitor', manufacturer = 'OpenTeeGolf', battery = 87 } = {}) {
  const sockets = new Set();
  const got = []; // every message received from controllers
  let shotNumber = 0;
  const send = (obj) => { for (const s of sockets) if (s.started) s.write(frame(JSON.stringify(obj))); };
  const server = http.createServer((req, res) => { res.writeHead(426); res.end(); });
  server.on('upgrade', (req, sock) => {
    const key = req.headers['sec-websocket-key'];
    const accept = crypto.createHash('sha1').update(key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');
    sock.write(`HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ${accept}\r\n\r\n`);
    sock.path = req.url;
    sockets.add(sock);
    const close = () => { sockets.delete(sock); sock.destroy(); };
    sock.on('data', reader((text) => {
      let msg;
      try { msg = JSON.parse(text); } catch { return; }
      got.push(msg);
      if (msg.kind === 'start') {
        sock.started = true;
        sock.write(frame(JSON.stringify({ kind: 'init', version: '0.1.0' })));
        sock.write(frame(JSON.stringify({ device, event: { kind: 'device_telemetry', manufacturer, model, telemetry: { ready: 'true', battery_pct: String(battery) } } })));
      }
    }, close));
    sock.on('close', () => sockets.delete(sock));
    sock.on('error', close);
  });
  return {
    server,
    got,
    listen: () => new Promise((r) => server.listen(port, '127.0.0.1', () => r(server.address().port))),
    // FRP's sequence for one shot; ball/club as FRP sends them (unit-tagged strings). The ball and
    // club events can come in either order (order: 'club-first')
    shot({ ball, club, order } = {}) {
      const key = { shot_id: crypto.randomUUID(), shot_number: ++shotNumber };
      if (ball === undefined) ball = { launch_speed: '67.2mps', launch_azimuth: -1.3, launch_elevation: 14.2, carry_distance: '180.5m', backspin_rpm: 3200, sidespin_rpm: -450, total_spin_rpm: 3231, spin_axis: -8.0 };
      const ev = [];
      ev.push({ kind: 'shot_trigger', key });
      const b = ball && { kind: 'ball_flight', key, ball };
      const c = club && { kind: 'club_path', key, club };
      for (const e of order === 'club-first' ? [c, b] : [b, c]) if (e) ev.push(e);
      ev.push({ kind: 'shot_finished', key });
      for (const e of ev) send({ device, event: e });
      return key;
    },
    telemetry(t) { send({ device, event: { kind: 'device_telemetry', manufacturer, model, telemetry: t } }); },
    send,
    dropAll() { for (const s of sockets) s.destroy(); sockets.clear(); },
    clients: () => [...sockets].filter((s) => s.started).length,
    close: () => new Promise((r) => { for (const s of sockets) s.destroy(); server.close(() => r()); }),
  };
}

if (import.meta.url === pathToFileURL(process.argv[1]).href) {
  const port = +(process.argv[2] || 5880), every = +(process.argv[3] || 15);
  const dev = fakeFrp({ port });
  dev.listen().then((p) => {
    console.log(`Fake FRP device on ws://127.0.0.1:${p}/frp, a shot every ${every} s`);
    setInterval(() => {
      const speed = 40 + Math.random() * 30;
      dev.shot({ ball: { launch_speed: `${speed.toFixed(1)}mps`, launch_azimuth: +(Math.random() * 6 - 3).toFixed(1), launch_elevation: +(10 + Math.random() * 8).toFixed(1), backspin_rpm: Math.round(2500 + Math.random() * 2500), sidespin_rpm: Math.round(Math.random() * 800 - 400) } });
      console.log(`shot: ${speed.toFixed(1)} m/s`);
    }, every * 1000);
    setInterval(() => {
      const last = dev.got.at(-1);
      if (last?.kind === 'set_detection_mode') console.log('mode:', JSON.stringify(last));
    }, 1000);
  });
}
