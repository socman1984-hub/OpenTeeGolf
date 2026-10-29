// Shots from any launch monitor that speaks the Flight Relay Protocol (FRP): an open standard
// (github.com/flightrelay/spec, CC0) for streaming shots as JSON over a WebSocket. OpenTeeGolf
// is the "controller": it connects to the "device" (a launch monitor, or a bridge such as the
// Garmin R10 and Square Golf ones in lib/bridges.js), says hello, and collects each shot's
// events until the shot is finished.
//
//   const frp = new FrpController({ url: 'ws://127.0.0.1:5880/frp' });
//   frp.on('status', (s) => ...);   // { state, text, error, device, model, ready, battery }
//   frp.on('shot', (ball) => ...);  // OpenConnect-style BallData (mph, degrees, rpm), ClubData inside
//   frp.start(); frp.setMode({ mode: 'putting', handed: 'rh' }); frp.stop();

import { EventEmitter } from 'node:events';

const VERSIONS = ['0.1.0'];
const RETRY_MS = [1000, 2000, 4000, 8000];
const STALE_MS = 10000; // a shot with no shot_finished this long after its last event is dropped

// A unit-tagged string ("67.2mps", "180.5m", "0.47in") in metres per second or metres; a plain
// number is taken as already in those units. null when missing or unreadable.
const SPEED = { mps: 1, mph: 0.44704, kph: 1 / 3.6, fps: 0.3048 };
const DIST = { m: 1, yd: 0.9144, ft: 0.3048, in: 0.0254, mm: 0.001, cm: 0.01 };
export function unitValue(v, table) {
  if (typeof v === 'number') return Number.isFinite(v) ? v : null;
  if (typeof v !== 'string') return null;
  const m = /^\s*(-?\d+(?:\.\d+)?(?:e[-+]?\d+)?)\s*([a-z]+)\s*$/i.exec(v);
  if (!m) return null;
  const f = table[m[2].toLowerCase()];
  return f ? +m[1] * f : null;
}
const num = (v) => (v == null || v === '' || !Number.isFinite(+v) ? null : +v);

// An FRP ball (and club) into the OpenConnect-style ball data the rest of the game reads. FRP and
// OpenConnect agree on signs: launch direction, side spin and spin axis positive = right.
export function frpToBallData(ball, club) {
  if (!ball) return null;
  const speed = unitValue(ball.launch_speed, SPEED);
  if (speed == null) return null;
  let back = num(ball.backspin_rpm), side = num(ball.sidespin_rpm);
  const total = num(ball.total_spin_rpm), axis = num(ball.spin_axis);
  if ((back == null || side == null) && total != null) {
    const a = ((axis || 0) * Math.PI) / 180;
    if (back == null) back = total * Math.cos(a);
    if (side == null) side = total * Math.sin(a);
  }
  back ??= 0; side ??= 0;
  const tot = total ?? Math.hypot(back, side);
  const carry = unitValue(ball.carry_distance, DIST);
  const out = {
    Speed: speed / 0.44704,
    VLA: num(ball.launch_elevation) ?? 0,
    HLA: num(ball.launch_azimuth) ?? 0,
    BackSpin: back, SideSpin: side, TotalSpin: tot,
    SpinAxis: axis ?? (tot ? (Math.atan2(side, back) * 180) / Math.PI : 0),
  };
  if (carry != null) out.CarryDistance = carry / 0.9144;
  if (club) {
    const cs = unitValue(club.club_speed, SPEED);
    out.ClubData = {
      Speed: cs != null ? cs / 0.44704 : undefined,
      AngleOfAttack: num(club.attack_angle) ?? undefined,
      FaceToTarget: num(club.face_angle) ?? undefined,
      Path: num(club.path) ?? undefined,
      Loft: num(club.dynamic_loft) ?? undefined,
      SmashFactor: num(club.smash_factor) ?? undefined,
    };
  }
  return out;
}

export class FrpController extends EventEmitter {
  constructor({ url, name = 'OpenTeeGolf', log = console.log } = {}) {
    super();
    this.url = url;
    this.name = name;
    this.log = log;
    this.ws = null;
    this.running = false;
    this.tries = 0;
    this.timer = null;
    this.shots = new Map(); // shot_id -> { ball, club, impact, number, at }
    this.mode = {}; // the last detection mode and handedness, sent again on every connection
    this.status = { state: 'off', text: 'Not connected', error: null, device: null, model: null, ready: false, battery: null };
  }

  setStatus(patch) {
    Object.assign(this.status, patch);
    this.emit('status', { ...this.status });
  }

  start() {
    if (this.running) return;
    this.running = true;
    this.connect();
  }

  stop() {
    this.running = false;
    clearTimeout(this.timer);
    const ws = this.ws;
    this.ws = null;
    try { ws?.close(); } catch { /* already closed */ }
    this.shots.clear();
    this.setStatus({ state: 'off', text: 'Not connected', ready: false });
  }

  connect() {
    if (!this.running) return;
    if (typeof WebSocket !== 'function') {
      this.setStatus({ state: 'error', text: 'Needs Node.js 22 or later', error: 'no WebSocket support' });
      return;
    }
    this.setStatus({ state: 'connecting', text: `Connecting to ${this.url}…` });
    let ws;
    try { ws = new WebSocket(this.url); } catch (err) {
      this.setStatus({ state: 'error', text: 'Bad address', error: err.message });
      return this.retry();
    }
    this.ws = ws;
    let opened = false;
    ws.onopen = () => {
      opened = true;
      ws.send(JSON.stringify({ kind: 'start', version: VERSIONS, name: this.name }));
    };
    ws.onmessage = (e) => {
      if (ws !== this.ws) return;
      let msg;
      try { msg = JSON.parse(typeof e.data === 'string' ? e.data : Buffer.from(e.data).toString('utf8')); } catch { return; }
      this.handle(msg);
    };
    ws.onerror = () => {}; // followed by onclose
    ws.onclose = () => {
      if (ws !== this.ws) return;
      this.ws = null;
      if (!this.running) return;
      if (opened) this.log(`FRP: ${this.url} closed the connection`);
      this.setStatus({ state: 'connecting', text: opened ? 'Connection lost, reconnecting…' : 'Waiting for the launch monitor…', ready: false });
      this.retry();
    };
  }

  retry() {
    clearTimeout(this.timer);
    const ms = RETRY_MS[Math.min(this.tries++, RETRY_MS.length - 1)];
    this.timer = setTimeout(() => this.connect(), ms);
  }

  send(obj) {
    if (this.ws?.readyState === 1) this.ws.send(JSON.stringify(obj));
  }

  // Detection mode ('full' | 'putting' | 'chipping') and handedness ('rh' | 'lh'); devices that
  // can't switch ignore it
  setMode({ mode, handed } = {}) {
    const next = { ...this.mode };
    if (mode === 'full' || mode === 'putting' || mode === 'chipping') next.mode = mode;
    if (handed === 'rh' || handed === 'lh') next.handed = handed;
    if (next.mode === this.mode.mode && next.handed === this.mode.handed) return;
    this.mode = next;
    if (this.status.state === 'connected') this.send({ kind: 'set_detection_mode', ...this.mode });
  }

  handle(msg) {
    // protocol messages (no envelope)
    if (msg.kind === 'init') {
      this.tries = 0;
      this.log(`FRP: connected to ${this.url} (FRP ${msg.version})`);
      this.setStatus({ state: 'connected', text: 'Connected', error: null });
      if (this.mode.mode || this.mode.handed) this.send({ kind: 'set_detection_mode', ...this.mode });
      return;
    }
    if (msg.kind === 'alert') {
      this.log(`FRP ${msg.severity}: ${msg.message}`);
      if (msg.severity !== 'warn') this.setStatus({ error: msg.message });
      return;
    }
    const ev = msg.event;
    if (!ev || typeof ev !== 'object') return;
    if (msg.device && msg.device !== this.status.device) this.setStatus({ device: String(msg.device) });
    if (ev.kind === 'device_telemetry') {
      const t = ev.telemetry || {};
      const patch = {};
      if (ev.model) patch.model = [ev.manufacturer, ev.model].filter(Boolean).join(' ');
      if ('ready' in t) patch.ready = t.ready === 'true' || t.ready === true;
      if ('battery_pct' in t && num(t.battery_pct) != null) patch.battery = Math.round(num(t.battery_pct));
      this.setStatus(patch);
      return;
    }
    if (ev.kind === 'alert') {
      this.log(`FRP ${msg.device || ''} ${ev.severity}: ${ev.message}`);
      this.setStatus({ error: ev.severity === 'warn' ? this.status.error : ev.message });
      this.emit('alert', ev);
      return;
    }
    const id = ev.key?.shot_id;
    if (!id) return;
    const now = Date.now();
    for (const [k, s] of this.shots) if (now - s.at > STALE_MS) this.shots.delete(k);
    let s = this.shots.get(id);
    if (!s) { s = { number: ev.key.shot_number ?? null, at: now }; this.shots.set(id, s); }
    s.at = now;
    if (ev.kind === 'shot_trigger') this.emit('trigger', s.number);
    else if (ev.kind === 'ball_flight') s.ball = ev.ball;
    else if (ev.kind === 'club_path') s.club = ev.club;
    else if (ev.kind === 'face_impact') s.impact = ev.impact;
    else if (ev.kind === 'shot_finished') {
      this.shots.delete(id);
      const ball = frpToBallData(s.ball, s.club);
      if (ball) this.emit('shot', ball, { shotId: id, shotNumber: s.number, device: msg.device || this.status.device });
      else this.log(`FRP: shot ${s.number ?? id} had no ball data`);
    }
  }
}
