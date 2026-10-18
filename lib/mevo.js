// Direct connection to a FlightScope Mevo+ / Mevo Gen2 (no FS Golf needed).
//
// The protocol was reverse-engineered for interoperability by the ironsight project
// (https://github.com/divotmaker/ironsight, Copyright 2026 Eric Thill, MIT OR Apache-2.0);
// this is a JavaScript port of its wire format, handshake and shot sequence. As with ironsight,
// the device's licensing exchange is not touched: it is optional and the radar arms without it.
//
// The Mevo+ is a Wi-Fi access point (SSID "FS M2-xxxxxx"); the radar protocol is binary frames on
// TCP 192.168.2.1:5100. Three boards answer on it: DSP (radar core), AVR (radar I/O, shots) and
// PI (camera). Every exchange is strictly one request, one reply.
import net from 'node:net';
import { EventEmitter } from 'node:events';

export const BUS = { APP: 0x10, PI: 0x12, AVR: 0x30, DSP: 0x40 };
// Detection modes (commsIndex): what ironsight's own bridge uses for each shot type
export const MODES = { full: 1, chipping: 5, putting: 3, outdoor: 9 };
const T = {
  STATUS: 0xaa, DSP_QUERY: 0x48, DSP_QUERY_RESP: 0xc8, DEV_INFO: 0x67, DEV_INFO_RESP: 0xe7, PROD_INFO: 0xfd,
  CONFIG_QUERY: 0x21, CONFIG_RESP: 0xa0, AVR_CONFIG_QUERY: 0x23, AVR_CONFIG_RESP: 0xa2, PARAM_READ: 0xbe, PARAM_VALUE: 0xbf,
  CAL_DATA_REQ: 0xd2, CAL_DATA_RESP: 0xd3, CAL_PARAM_REQ: 0xd0, CAL_PARAM_RESP: 0xd1, TIME_SYNC: 0x9b,
  CAM_CONFIG: 0x82, CAM_CONFIG_REQ: 0x83, NET_CONFIG: 0xde, CONFIG_ACK: 0x95, CONFIG_NACK: 0x94, MODE_SET: 0xa5,
  MODE_ACK: 0xb1, AVR_CONFIG_CMD: 0xb0, TEXT: 0xe3, SHOT_TEXT: 0xe5, FLIGHT: 0xd4, CLUB: 0xed, SPIN: 0xef,
  SHOT_DATA_ACK: 0x69, SHOT_RESULT_REQ: 0x6d, CAM_STATE: 0x81, RADAR_CAL: 0xa4,
};

// Setup the player can change (what FS Golf asks for when you set up the Mevo+)
export const DEFAULT_SETUP = {
  distanceM: 2.74,     // Mevo+ to ball (FlightScope: about 7-9 ft indoors, 8 ft+ outdoors)
  surfaceIn: 1,        // hitting mat height above the floor the Mevo+ stands on (inches)
  teeHeightIn: 1.5,    // driver tee height (inches)
  ballType: 0,         // 0 = standard balls, 1 = RCT (radar-capture) balls
  trackPct: 1.0,       // minimum ball-tracking share before a shot counts (0.6-1.0)
  mode: 'auto',        // 'auto' (putting/chipping/full by club), or force 'full' / 'outdoor'
};

// Camera config "standard preset" (1024x768) used when the live view is on
function camConfigStandard() {
  const i16 = (v) => [(v >> 8) & 255, v & 255];
  return [0x33, 1, ...i16(1024), ...i16(768), ...i16(0), ...i16(0), 20, 20, 20, ...i16(2000), ...i16(1500), 0, 0,
    ...writeFloat40(0), ...i16(-1), ...i16(-1), ...i16(-1), ...i16(-1), ...i16(-1), 0, ...i16(-1), ...i16(-1),
    ...i16(-1), ...i16(-1), ...writeFloat40(-1), ...i16(-1), ...i16(-1)];
}
const TIMEOUT = 3000;

// ------------------------------------------------------------------ wire format

const ESCAPES = { 0xf0: 0x01, 0xf1: 0x02, 0xfd: 0x03, 0xfa: 0x04 };
const UNESCAPE = { 0x01: 0xf0, 0x02: 0xf1, 0x03: 0xfd, 0x04: 0xfa };
const stuff = (bytes) => {
  const out = [];
  for (const b of bytes) if (b in ESCAPES) out.push(0xfd, ESCAPES[b]); else out.push(b);
  return out;
};

// F0 STUFFED(dest src type payload) STUFFED(cs_hi cs_lo) F1; checksum = 16-bit sum of the stuffed bytes
export function encodeFrame(dest, type, payload = [], src = BUS.APP) {
  const data = stuff([dest, src, type, ...payload]);
  let cs = 0;
  for (const b of data) cs = (cs + b) & 0xffff;
  return Buffer.from([0xf0, ...data, ...stuff([cs >> 8, cs & 0xff]), 0xf1]);
}

// One wire frame (with F0/F1) -> { dest, src, type, payload } or null if it doesn't check out
export function parseFrame(wire) {
  if (wire.length < 7 || wire[0] !== 0xf0 || wire[wire.length - 1] !== 0xf1) return null;
  const inner = wire.subarray(1, wire.length - 1), vals = [], offs = [];
  for (let i = 0; i < inner.length; i++) {
    if (inner[i] === 0xfd) {
      const v = UNESCAPE[inner[i + 1]];
      if (v === undefined) return null;
      vals.push(v); offs.push(i); i++;
    } else { vals.push(inner[i]); offs.push(i); }
  }
  const n = vals.length;
  if (n < 5) return null;
  let cs = 0;
  for (let i = 0; i < offs[n - 2]; i++) cs = (cs + inner[i]) & 0xffff;
  if (cs !== ((vals[n - 2] << 8) | vals[n - 1])) return null;
  return { dest: vals[0], src: vals[1], type: vals[2], payload: Buffer.from(vals.slice(3, n - 2)) };
}

// Splits a TCP byte stream into frames
export class FrameReader {
  constructor() { this.buf = Buffer.alloc(0); }
  push(chunk) {
    this.buf = Buffer.concat([this.buf, chunk]);
    const frames = [];
    for (;;) {
      const s = this.buf.indexOf(0xf0);
      if (s < 0) { this.buf = Buffer.alloc(0); break; }
      const e = this.buf.indexOf(0xf1, s + 1);
      if (e < 0) { this.buf = this.buf.subarray(s); break; }
      const f = parseFrame(this.buf.subarray(s, e + 1));
      if (f) frames.push(f);
      this.buf = this.buf.subarray(e + 1);
    }
    return frames;
  }
}

// Big-endian field codecs
export const int16 = (p, o) => { const v = (p[o] << 8) | p[o + 1]; return v >= 0x8000 ? v - 0x10000 : v; };
export const int24 = (p, o) => { const v = (p[o] << 16) | (p[o + 1] << 8) | p[o + 2]; return v >= 0x800000 ? v - 0x1000000 : v; };
export const readFloat40 = (p, o) => { const m = int24(p, o + 2); return m === 0 ? 0 : m * 2 ** (int16(p, o) - 23); };
export function writeFloat40(v) {
  if (!v) return [0, 0, 0, 0, 0];
  let e = Math.floor(Math.log2(Math.abs(v))) + 1; // frexp: |v| = f * 2^e, f in [0.5, 1)
  let f = Math.abs(v) / 2 ** e;
  if (f >= 1) { f /= 2; e++; } else if (f < 0.5) { f *= 2; e--; }
  let m = Math.floor(f * 2 ** 23);
  if (v < 0) m = -m;
  const mu = m & 0xffffff, eu = e & 0xffff;
  return [eu >> 8, eu & 0xff, (mu >> 16) & 0xff, (mu >> 8) & 0xff, mu & 0xff];
}
const text = (p) => Buffer.from(p).toString('latin1').replace(/\0+$/g, '').replace(/[^\x20-\x7e\n]/g, '').trim();

// ------------------------------------------------------------------ shot messages

// 0xD4 FLIGHT_RESULT (wire signs: azimuth and sidespin negative = right)
export function decodeFlight(p) {
  if (p.length < 49) return null;
  const k = (o) => int24(p, o) / 1000;
  return {
    shot: int24(p, 1), launchSpeed: k(16), azimuth: k(19), elevation: k(22), carry: k(25), flightTime: k(28), maxHeight: k(31),
    backspin: int24(p, 43), sidespin: int24(p, 46),
    clubSpeed: p.length >= 88 ? k(85) : null,
  };
}
// 0xED CLUB_RESULT (wire signs: path and face negated relative to the usual display)
export function decodeClub(p) {
  if (p.length < 26) return null;
  const c = (o) => int24(p, o) / 100;
  return { speed: c(5), speedPost: c(8), path: -c(11), attack: c(14), face: -c(17), loft: c(20), smash: int24(p, 23) / 1000 };
}
// 0xEF SPIN_RESULT: total spin and axis (axis sign negated relative to the usual display)
export function decodeSpin(p) {
  if (p.length < 134) return null;
  return { total: int16(p, 108), axis: -int16(p, 132) / 10 };
}

// Combine into OpenConnect-style ball data (mph, degrees, rpm; HLA and side spin positive = right)
export function toBallData(flight, spin, club) {
  if (!flight) return null;
  let back = flight.backspin, side = -flight.sidespin;
  // The first shot of a session can carry uninitialised spin in 0xD4; use 0xEF then
  const bad = !Number.isFinite(back) || Math.abs(back) > 16000 || Math.abs(side) > 9000 || (back === 0 && side === 0);
  if (bad && spin && spin.total > 0) {
    const a = (spin.axis * Math.PI) / 180;
    back = Math.round(spin.total * Math.cos(a));
    side = Math.round(spin.total * Math.sin(a));
  }
  const total = Math.hypot(back, side);
  return {
    Speed: flight.launchSpeed * 2.23694,
    VLA: flight.elevation,
    HLA: -flight.azimuth,
    BackSpin: back, SideSpin: side, TotalSpin: total,
    SpinAxis: total ? (Math.atan2(side, back) * 180) / Math.PI : 0,
    CarryDistance: flight.carry * 1.09361,
    ClubData: club ? { Speed: club.speed * 2.23694, AngleOfAttack: club.attack, FaceToTarget: club.face, Path: club.path, SmashFactor: club.smash } : undefined,
  };
}

// ------------------------------------------------------------------ the client

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const STEP_TEXT = { DSP: 'introducing itself', radar: 'reading the radar', camera: 'reading the camera board', settings: 'sending settings', arming: 'arming' };
const isAck = (m, src = BUS.AVR) => m.src === src && m.type === T.CONFIG_ACK;

export class MevoClient extends EventEmitter {
  // options: { host, port, mode, log, trace }; trace(direction, frame) sees every frame ('>' sent, '<' received)
  constructor({ host = '192.168.2.1', port = 5100, mode = 'full', log = () => {}, trace = null } = {}) {
    super();
    Object.assign(this, { host, port, mode, log, trace });
    this.running = false;
    this.waiters = [];
    this.lock = Promise.resolve();
    this.state = 'off';
  }

  setState(state, extra = {}) {
    this.state = state;
    this.emit('status', { state, mode: this.mode, ...this.info, ...extra });
  }

  start() {
    if (this.running) return;
    this.running = true;
    (async () => {
      while (this.running) {
        try { await this.session(); }
        catch (err) {
          if (this.running) {
            const at = this.sock && this.step ? ` (while ${STEP_TEXT[this.step] || this.step})` : '';
            this.log(`Mevo+: ${err.message}${at}`);
            this.setState('searching', { error: `${err.message}${at}` });
          }
        }
        this.step = null;
        this.cleanup();
        if (this.running) await sleep(4000);
      }
      this.setState('off');
    })();
  }

  stop() {
    this.running = false;
    this.cleanup();
  }

  // Done for now: disarm (so the radar stops watching for shots), then hang up and stay off
  async disconnect() {
    if (this.sock && (this.armed || this.state === 'arming')) {
      const t = sleep(3000);
      await Promise.race([this.exclusive(() => this.disarm()).catch(() => {}), t]);
    }
    this.stop();
    this.setState('off');
  }

  // Stay connected but stop detecting shots / start again (with the current mode and setup)
  // (while it's busy - connecting, mid-shot, changing mode - the choice is applied when that's done)
  setArmed(on) {
    this.wantDisarmed = !on;
    if (!this.sock || !['ready', 'idle'].includes(this.state)) return Promise.resolve();
    if (on === (this.state === 'ready')) return Promise.resolve();
    return this.exclusive(async () => {
      if (on) {
        this.setState('arming');
        await this.configure(MODES[this.mode] ?? MODES.full);
        await this.arm();
        this.setState('ready');
      } else {
        await this.disarm();
        this.setState('idle');
      }
    });
  }

  cleanup() {
    clearInterval(this.keepTimer); clearTimeout(this.triggerTimer); clearTimeout(this.flightTimer);
    this.keepTimer = null;
    for (const w of this.waiters) w.reject(new Error('connection closed'));
    this.waiters = [];
    this.sock?.destroy();
    this.sock = null;
    this.armed = false;
    this.shot = null;
  }

  // ---- plumbing

  send(dest, type, payload = []) {
    if (!this.sock) throw new Error('not connected');
    this.trace?.('>', { dest, src: BUS.APP, type, payload: Buffer.from(payload) });
    this.sock.write(encodeFrame(dest, type, payload));
  }

  // Resolve with the next message matching pred (messages it doesn't want pass by)
  waitFor(pred, ms = TIMEOUT, what = 'reply') {
    return new Promise((resolve, reject) => {
      const w = { pred, resolve, reject };
      w.timer = setTimeout(() => { this.waiters = this.waiters.filter((x) => x !== w); reject(new Error(`no ${what} from the Mevo+`)); }, ms);
      this.waiters.push(w);
    });
  }

  ask(dest, type, payload, pred, what, ms) {
    const p = this.waitFor(pred, ms, what);
    this.send(dest, type, payload);
    return p;
  }

  // Operations run one at a time (the device answers one request at a time)
  exclusive(fn) {
    const p = this.lock.then(fn);
    this.lock = p.catch(() => {});
    return p;
  }

  onFrame(m) {
    // Status cache for the HUD
    if (m.type === T.STATUS && m.src === BUS.DSP && m.payload.length >= 64 && m.payload[0] === 0x80) {
      const p = m.payload;
      this.info = {
        ...this.info, battery: p[61], charging: !!p[63],
        batteryV: ((p[53] << 8) | p[54]) / 1000, dspTempC: int16(p, 40) / 100,
      };
    }
    if (m.type === T.STATUS && m.src === BUS.AVR && m.payload.length >= 25) {
      this.info = {
        ...this.info, tilt: +readFloat40(m.payload, 15).toFixed(2), roll: +(-readFloat40(m.payload, 20)).toFixed(2),
        tempC: +readFloat40(m.payload, 10).toFixed(1), avrState: ['idle', 'armed', 'arming', 'tracking'][m.payload[1]] || String(m.payload[1]),
      };
    }
    // The device's own status lines ("ARMED DetectionMode=1", "BALL TRIGGER", …), for the setup page
    if (m.type === T.TEXT || m.type === T.SHOT_TEXT) {
      const t = text(m.payload);
      if (t) this.info = { ...this.info, lastText: t, lastTextAt: Date.now() };
    }
    if (m.type === T.CAM_STATE && m.src === BUS.PI && m.payload.length >= 2) this.info = { ...this.info, camera: m.payload[1] === 1 ? 'on' : 'off' };
    // Shot data is collected whenever it arrives (its order isn't fixed). A flight result counts
    // as a shot even if the trigger message was missed; if "PROCESSED" never follows, the shot
    // is reported anyway shortly after.
    if (m.type === T.FLIGHT || m.type === T.CLUB || m.type === T.SPIN) {
      if (m.type === T.FLIGHT && !this.shot && decodeFlight(m.payload)) { this.log('Mevo+: flight result without a trigger message'); this.startShot(); }
      if (this.shot && !this.shot.done) {
        if (m.type === T.FLIGHT) this.shot.flight = decodeFlight(m.payload) || this.shot.flight;
        else if (m.type === T.CLUB) this.shot.club = decodeClub(m.payload) || this.shot.club;
        else this.shot.spin = decodeSpin(m.payload) || this.shot.spin;
        if (m.type === T.FLIGHT && this.shot.flight) {
          clearTimeout(this.flightTimer);
          this.flightTimer = setTimeout(() => { if (this.shot && !this.shot.finishing) { this.log('Mevo+: no "PROCESSED" after the flight result; reporting the shot'); this.onShotText('PROCESSED'); } }, 2500);
        }
      }
    }
    // Shot progress arrives as shot text (0xE5); some firmware may use plain text (0xE3)
    if (m.type === T.SHOT_TEXT || (m.type === T.TEXT && /BALL TRIGGER|PROCESSED/.test(text(m.payload)))) this.onShotText(text(m.payload));
    for (const w of this.waiters) {
      if (w.pred(m)) {
        clearTimeout(w.timer);
        this.waiters = this.waiters.filter((x) => x !== w);
        w.resolve(m);
        break;
      }
    }
  }

  // ---- connection lifecycle

  async session() {
    this.setState('connecting');
    this.sock = await new Promise((resolve, reject) => {
      const s = net.connect({ host: this.host, port: this.port });
      const t = setTimeout(() => { s.destroy(); reject(new Error(`can't reach the Mevo+ at ${this.host} (is this PC on the Mevo+ Wi-Fi?)`)); }, 5000);
      s.once('connect', () => { clearTimeout(t); s.setNoDelay(true); resolve(s); });
      s.once('error', (err) => { clearTimeout(t); reject(new Error(`can't reach the Mevo+ at ${this.host} (${err.code || err.message})`)); });
    });
    const reader = new FrameReader();
    const closed = new Promise((resolve) => {
      // (shot results are taken whoever they're addressed to)
      const SHOT_TYPES = [T.FLIGHT, T.CLUB, T.SPIN, T.SHOT_TEXT];
      this.sock.on('data', (chunk) => { for (const f of reader.push(chunk)) { this.trace?.('<', f); if (f.dest === BUS.APP || SHOT_TYPES.includes(f.type)) this.onFrame(f); } });
      this.sock.on('close', () => resolve('closed'));
      this.sock.on('error', () => resolve('error'));
    });
    this.closedSignal = closed;
    // When the device hangs up, everything still waiting for a reply fails at once
    this.dropped = false;
    closed.then((why) => {
      this.dropped = true;
      for (const w of this.waiters) { clearTimeout(w.timer); w.reject(new Error(`the Mevo+ closed the connection (${why})`)); }
      this.waiters = [];
    });
    this.setState('handshake');
    await this.exclusive(() => this.handshake());
    this.setState('arming');
    if (this.wantDisarmed) this.setState('idle');
    else {
      this.step = 'settings';
      await this.exclusive(async () => { await this.configure(MODES[this.mode] ?? MODES.full); this.step = 'arming'; await this.arm(); });
      this.setState('ready');
    }
    this.keepFails = 0;
    this.keepTimer = setInterval(() => this.keepalive(), 1000);
    await closed;
    throw new Error('connection to the Mevo+ dropped');
  }

  // Phases 1-3: introduce ourselves to the DSP, AVR and PI, in the order FS Golf does. Only the
  // status replies are required; the rest is information (model, firmware, network name), so a
  // step that goes unanswered is noted and skipped instead of starting over. (Units differ in
  // firmware, and the camera board can take a while to wake up.)
  async handshake() {
    const { DSP, AVR, PI } = BUS;
    const from = (src, type) => (m) => m.src === src && m.type === type;
    const skipped = [];
    // An optional question; a board that leaves two in a row unanswered isn't asked any more
    const quiet = {};
    const opt = async (bus, type, payload, replyType, what, ms) => {
      if (quiet[bus] >= 2) { skipped.push(what); return null; }
      try {
        const r = await this.ask(bus, type, payload, from(bus, replyType), what, ms);
        quiet[bus] = 0;
        return r;
      } catch (err) {
        if (this.dropped) throw err;
        quiet[bus] = (quiet[bus] || 0) + 1;
        skipped.push(what);
        return null;
      }
    };
    // DSP
    this.step = 'DSP';
    await this.ask(DSP, T.STATUS, [1, 1], from(DSP, T.STATUS), 'DSP status');
    const hw = await opt(DSP, T.DSP_QUERY, [], T.DSP_QUERY_RESP, 'DSP query');
    const model = hw?.payload[1] === 0xc0 ? 'Mevo Gen2' : 'Mevo+';
    const dsp = await opt(DSP, T.DEV_INFO, [], T.DEV_INFO_RESP, 'DSP info');
    for (const q of [0x00, 0x08, 0x09]) await opt(DSP, T.PROD_INFO, [1, q], T.PROD_INFO, 'product info');
    await opt(DSP, T.CONFIG_QUERY, [], T.CONFIG_RESP, 'DSP config');
    // AVR (the radar)
    this.step = 'radar';
    await this.ask(AVR, T.STATUS, [1, 1], from(AVR, T.STATUS), 'AVR status');
    await opt(AVR, T.STATUS, [1, 1], T.STATUS, 'AVR status');
    for (let i = 0; i < 2; i++) this.lastAvrInfo = (await opt(AVR, T.DEV_INFO, [], T.DEV_INFO_RESP, 'AVR info')) || this.lastAvrInfo;
    for (const id of [0x0c, 0x0d]) await opt(AVR, T.PARAM_READ, [3, 0, 0, id], T.PARAM_VALUE, 'AVR parameter');
    await opt(AVR, T.CONFIG_QUERY, [], T.CONFIG_RESP, 'AVR config');
    await opt(AVR, T.CAL_DATA_REQ, [9, 0, 0, 3, 0, 0, 0, 0, 0, 0xa5], T.CAL_DATA_RESP, 'factory calibration');
    await opt(AVR, T.CAL_PARAM_REQ, [2, 0, 8], T.CAL_PARAM_RESP, 'calibration parameters');
    await opt(AVR, T.AVR_CONFIG_QUERY, [], T.AVR_CONFIG_RESP, 'AVR settings');
    await opt(AVR, T.PARAM_READ, [3, 0, 0, 0x64], T.PARAM_VALUE, 'AVR parameter');
    const now = Math.floor(Date.now() / 1000);
    await opt(AVR, T.TIME_SYNC, [8, 0, (now >>> 24) & 255, (now >> 16) & 255, (now >> 8) & 255, now & 255, 0, 0, 1], T.TIME_SYNC, 'time sync');
    // PI (camera board)
    this.step = 'camera';
    await opt(PI, T.STATUS, [1, 3], T.STATUS, 'camera status', 5000);
    const pi = await opt(PI, T.DEV_INFO, [], T.DEV_INFO_RESP, 'camera info');
    await opt(PI, T.PARAM_READ, [3, 0, 0, 0x0a], T.PARAM_VALUE, 'camera parameter');
    for (let i = 0; i < 2; i++) await opt(PI, T.CAM_CONFIG_REQ, [2, 1, 5], T.CAM_CONFIG, 'camera config');
    await opt(PI, T.NET_CONFIG, [1, 0], T.NET_CONFIG, 'network name');
    const net2 = await opt(PI, T.NET_CONFIG, [1, 8], T.NET_CONFIG, 'network settings');
    for (const id of [0x01, 0x07, 0x08, 0x09, 0x06, 0x0b, 0x03, 0x04, 0x05]) {
      await opt(PI, T.PARAM_READ, [3, 0, 0, id], T.PARAM_VALUE, 'camera parameter');
    }
    // "<SSID>\0<password>" after a length byte; only the network name is kept
    const ssid = net2 ? Buffer.from(net2.payload.subarray(1)).toString('latin1').split('\0')[0].replace(/[^\x20-\x7e]/g, '').trim() : '';
    const avrInfo = this.lastAvrInfo ? text(this.lastAvrInfo.payload).slice(0, 80) : '';
    this.info = { ...this.info, model, ssid, firmware: { dsp: dsp ? text(dsp.payload).slice(0, 80) : '', avr: avrInfo, camera: pi ? text(pi.payload).slice(0, 80) : '' } };
    this.log(`Mevo+: connected to ${model} ${ssid}${skipped.length ? ` (no answer to: ${[...new Set(skipped)].join(', ')})` : ''}`);
  }

  // Phase 4: detection mode (the AVR wants a B0 [01 00] commit after each command)
  // With a saved setup (this.setup), also sends ball type, tracking share, tee height and the radar
  // calibration (distance to ball, surface height), each followed by a commit, as FS Golf does.
  async configure(mode) {
    const { AVR } = BUS;
    const commit = (what) => this.ask(AVR, T.AVR_CONFIG_CMD, [1, 0], (m) => isAck(m), what);
    await commit('config gate');
    const st = this.setup;
    if (st) {
      const i24 = (v) => [(v >> 16) & 255, (v >> 8) & 255, v & 255];
      const params = [
        [0x06, [6, 0, 0, 0x06, ...i24(st.ballType ? 1 : 0)]],
        [0x0f, [8, 0, 0, 0x0f, ...writeFloat40(Math.min(1, Math.max(0.6, st.trackPct)))]],
        [0x26, [8, 0, 0, 0x26, ...writeFloat40(st.teeHeightIn * 0.0254)]],
      ];
      for (const [, payload] of params) {
        await this.ask(AVR, T.PARAM_VALUE, payload, (m) => isAck(m), 'setting ack');
        await commit('setting commit');
      }
    }
    await this.ask(AVR, T.MODE_SET, [2, 0, mode], (m) => m.src === AVR && m.type === T.MODE_SET, 'mode echo');
    await commit('mode commit');
    if (st) {
      const mm = Math.round(Math.min(6, Math.max(1, st.distanceM)) * 1000);
      const h = Math.min(255, Math.max(0, Math.floor(st.surfaceIn * 25.4)));
      await this.ask(AVR, T.RADAR_CAL, [6, mm >> 8, mm & 255, 0, h, 0, 0], (m) => m.src === AVR && m.type === T.RADAR_CAL, 'calibration echo');
      await commit('calibration commit');
    }
  }

  // New setup from the player: disarm, configure with it, re-arm
  applySetup(setup) {
    this.setup = { ...DEFAULT_SETUP, ...setup };
    if (!this.armed && !this.shot) return Promise.resolve();
    return this.exclusive(async () => {
      this.setState('arming');
      await this.disarm();
      await this.configure(MODES[this.mode] ?? MODES.full);
      await this.arm();
      this.setState('ready');
    });
  }

  // Live camera view: standard 1024x768 picture with streaming on (the Pi needs a few tries
  // while it warms up), or back to the camera's normal shot-only mode
  camera(on) {
    const { PI } = BUS;
    return this.exclusive(async () => {
      if (!on) {
        await this.ask(PI, T.CAM_STATE, [1, 1], (m) => m.src === PI && (m.type === T.CONFIG_ACK || m.type === T.CAM_STATE), 'camera reply').catch(() => {});
        this.info = { ...this.info, camera: 'on', streaming: false };
        return;
      }
      await this.ask(PI, T.CAM_CONFIG, camConfigStandard(), (m) => m.src === PI && m.type === T.CONFIG_ACK, 'camera config ack').catch(() => {});
      await this.ask(PI, T.CAM_CONFIG_REQ, [2, 1, 5], (m) => m.src === PI && m.type === T.CAM_CONFIG, 'camera config').catch(() => {});
      for (let i = 0; i < 8; i++) {
        const r = await this.ask(PI, T.CAM_STATE, [1, 3], (m) => m.src === PI && m.type === T.CAM_STATE, 'camera state', 3000).catch(() => null);
        if (r && r.payload[1] === 1) { this.info = { ...this.info, camera: 'on', streaming: true }; this.emit('status', { state: this.state, mode: this.mode, ...this.info }); return; }
        await sleep(4000);
      }
      throw new Error('the camera did not start');
    });
  }

  // Phase 6: DSP status, AVR status, ARM; the AVR may refuse (NACK) for a while after configuring
  async arm() {
    const { DSP, AVR, PI } = BUS;
    for (let tries = 0; tries < 40; tries++) {
      await this.ask(DSP, T.STATUS, [1, 1], (m) => m.src === DSP && m.type === T.STATUS, 'DSP status');
      await this.ask(AVR, T.STATUS, [1, 1], (m) => m.src === AVR && m.type === T.STATUS, 'AVR status');
      const r = await this.ask(AVR, T.AVR_CONFIG_CMD, [1, 1], (m) => m.src === AVR && (m.type === T.CONFIG_ACK || m.type === T.CONFIG_NACK), 'arm reply');
      if (r.type === T.CONFIG_NACK) { await sleep(750); continue; }
      const armed = this.waitFor((m) => m.type === T.TEXT && /ARMED/.test(text(m.payload)) && !/CANCELLED/.test(text(m.payload)), 5000, 'ARMED');
      const piStatus = this.ask(PI, T.STATUS, [1, 3], (m) => m.src === PI && m.type === T.STATUS, 'camera status', 5000).catch(() => null);
      await Promise.all([armed, piStatus]);
      this.armed = true;
      return;
    }
    throw new Error('the Mevo+ would not arm');
  }

  async disarm() {
    const { AVR } = BUS;
    const reset = this.waitFor((m) => m.type === T.MODE_ACK || (m.type === T.TEXT && /ARMED CANCELLED/.test(text(m.payload))), 3000, 'disarm');
    await this.ask(AVR, T.AVR_CONFIG_CMD, [1, 0], (m) => isAck(m), 'disarm ack');
    await reset.catch(() => {});
    await this.waitFor((m) => (m.type === T.TEXT && /System State/.test(text(m.payload))) || m.type === T.MODE_ACK, 1500, 'idle').catch(() => {});
    this.armed = false;
  }

  // Full swing / chipping / putting: disarm, set the mode, re-arm
  setMode(mode) {
    if (!(mode in MODES) || mode === this.mode) return;
    this.mode = mode;
    if (!this.armed) return;
    this.exclusive(async () => {
      if (this.shot) { this.pendingModeChange = true; return; } // applied when the shot re-arms
      this.setState('arming');
      await this.disarm();
      await this.configure(MODES[mode]);
      if (this.wantDisarmed) { this.setState('idle'); return; }
      await this.arm();
      this.setState('ready');
    }).catch((err) => { this.log(`Mevo+: mode change failed (${err.message})`); this.sock?.destroy(); });
  }

  // Status polls every second keep the session alive (not during a shot)
  keepalive() {
    if (!['ready', 'idle'].includes(this.state) || this.shot || this.keepBusy) return;
    this.keepBusy = true;
    this.exclusive(async () => {
      const { DSP, AVR, PI } = BUS;
      const all = ['DSP', 'AVR'].map((b) => this.waitFor((m) => m.src === BUS[b] && m.type === T.STATUS, 3000, `${b} status`));
      all.push(this.waitFor((m) => m.src === PI && m.type === T.STATUS, 3000, 'PI status').catch(() => null));
      this.send(DSP, T.STATUS, [1, 1]); this.send(AVR, T.STATUS, [1, 1]); this.send(PI, T.STATUS, [1, 3]);
      await Promise.all(all);
    }).then(() => { this.keepFails = 0; this.emit('status', { state: this.state, mode: this.mode, ...this.info }); })
      .catch(() => { if (++this.keepFails >= 3) { this.log('Mevo+: not answering, reconnecting'); this.sock?.destroy(); } })
      .finally(() => { this.keepBusy = false; });
  }

  // ---- shots

  startShot() {
    this.shot = { flight: null, club: null, spin: null };
    this.armed = false;
    this.setState('shot');
    this.emit('trigger');
    clearTimeout(this.triggerTimer);
    // Trigger but never processed: the session is stuck; start over
    this.triggerTimer = setTimeout(() => { if (this.shot) { this.log('Mevo+: shot never finished, reconnecting'); this.sock?.destroy(); } }, 30000);
  }

  onShotText(t) {
    if (/BALL TRIGGER/.test(t)) {
      if (this.shot && !this.shot.finishing) return; // (the same trigger, reported twice)
      this.log('Mevo+: ball trigger');
      this.startShot();
    } else if (/PROCESSED/.test(t)) {
      if (this.shot?.finishing) return;
      clearTimeout(this.triggerTimer); clearTimeout(this.flightTimer);
      this.shot ||= { flight: null, club: null, spin: null };
      this.shot.finishing = true;
      this.exclusive(() => this.finishShot()).catch((err) => { this.log(`Mevo+: ${err.message}, reconnecting`); this.sock?.destroy(); });
    }
  }

  // Report the shot as soon as the device says PROCESSED (its results are all in by then), then
  // acknowledge, drain to IDLE and re-arm (the device goes dormant if it isn't re-armed). Waiting
  // for IDLE before reporting cost another 1.5-2 s on every shot.
  async finishShot() {
    const { AVR } = BUS;
    const s = this.shot;
    // (the flight result normally comes just before PROCESSED; allow it a moment if not)
    for (let i = 0; i < 10 && !s.flight; i++) await sleep(100);
    s.done = true;
    const ball = toBallData(s.flight, s.spin, s.club);
    if (ball) {
      this.info = { ...this.info, shots: (this.info?.shots || 0) + 1, lastShot: { ...ball, at: Date.now() } };
      this.log(`Mevo+: shot ${ball.Speed.toFixed(1)} mph, launch ${ball.VLA.toFixed(1)}°, direction ${ball.HLA.toFixed(1)}°, spin ${Math.round(ball.TotalSpin)}`);
      // With holdAfterShot (the game arms it when it's the player's turn) it stays disarmed now
      if (this.holdAfterShot) this.wantDisarmed = true;
      this.emit('shot', ball, s);
      this.emit('status', { state: this.state, mode: this.mode, ...this.info }); // (so "shot read" shows at once)
    } else {
      this.log(`Mevo+: shot without a usable flight result (miss-hit?)${s.flight ? ` speed ${s.flight.launchSpeed}` : ''}`);
      this.info = { ...this.info, lastMiss: Date.now() };
      this.emit('status', { state: this.state, mode: this.mode, ...this.info });
    }
    const idle = this.waitFor((m) => m.type === T.SHOT_TEXT && /IDLE/.test(text(m.payload)), 4000, 'IDLE');
    this.send(AVR, T.SHOT_DATA_ACK); this.send(AVR, T.SHOT_DATA_ACK);
    // (some firmware never sends IDLE)
    let gotIdle = true;
    await idle.catch(() => { gotIdle = false; });
    if (gotIdle) await this.ask(AVR, T.SHOT_RESULT_REQ, [], (m) => m.src === AVR && m.type === T.CLUB, 'club result', 1500).catch(() => {});
    this.shot = null;
    // Re-arm, in the current mode if it changed meanwhile (unless it's to stay disarmed)
    if (this.pendingModeChange) { this.pendingModeChange = false; await this.configure(MODES[this.mode]); }
    if (this.wantDisarmed) { this.armed = false; this.setState('idle'); return; }
    const ack = this.waitFor((m) => isAck(m), TIMEOUT, 'arm ack');
    this.send(AVR, T.AVR_CONFIG_CMD, [1, 1]);
    await ack;
    await this.waitFor((m) => m.type === T.TEXT && /ARMED/.test(text(m.payload)) && !/CANCELLED/.test(text(m.payload)), 4000, 'ARMED');
    this.armed = true;
    this.setState('ready');
  }
}
