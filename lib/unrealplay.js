// A round of golf played in Unreal Engine (unreal/play_course.py): the course and the ball's flight,
// bounce and roll worked out here, by the game's own course builder (public/js/course.js) and
// physics (public/js/physics.js), and Unreal shown each shot as a path to play back.
//
// Unreal works in the course's Unreal export (exports/<id>/, public/js/export-unreal.js):
// centimetres, X east, Y south, Z up, from the landscape's north-west corner. The export's ground
// differs a little from the game's (shores and bunkers smoothed), so every point is moved up or
// down by the difference there: the ball stays on Unreal's ground.
//
// Shots come from the launch monitor like the game's (onShot); the ball is aimed at the pin.
// Not yet: trees (the ball flies through them), players, aiming, wind.

import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import { buildCourse } from '../public/js/course.js';
import { simulateShot, LIE_EFFECT } from '../public/js/physics.js';

const M_TO_YD = 1.09361;
const TEE_ORDER = ['White', 'Blue', 'Gold', 'Black', 'Green', 'Red'];

export class UnrealRound {
  constructor({ id, dataFile, realFile, exportDir, startHole = 1 }) {
    const data = JSON.parse(fs.readFileSync(dataFile, 'utf8').replace(/^﻿/, ''));
    const real = realFile && fs.existsSync(realFile) ? JSON.parse(fs.readFileSync(realFile, 'utf8')) : null;
    this.id = id;
    this.course = buildCourse(data, real);
    this.exp = readExport(exportDir);
    this.seq = 0;
    this.scores = [];
    this.lastShot = null;
    this.message = '';
    this.seen = Date.now();
    this.startHole(Math.max(0, Math.min(this.course.holes.length - 1, startHole - 1)));
  }

  // ---- the hole
  hole() { return this.course.holes[this.holeIndex]; }
  pin() {
    const h = this.hole(), p = Object.values(h.pins)[0] || h.greenCenter;
    return { x: p.x, y: this.course.heightAt(p.x, p.z) ?? p.y, z: p.z };
  }
  tee() {
    const h = this.hole();
    for (const k of TEE_ORDER) if (h.tees[k]) return h.tees[k];
    return Object.values(h.tees)[0];
  }
  startHole(i) {
    this.holeIndex = i;
    const t = this.tee();
    this.ball = { x: t.x, y: this.course.heightAt(t.x, t.z) ?? t.y, z: t.z };
    this.prevBall = { ...this.ball };
    this.strokes = 0;
    this.lie = 'tee';
    this.phase = 'ready';
    this.aimAtPin();
    this.message = `Hole ${this.hole().number} · Par ${this.hole().par} · ${Math.round(dist(this.ball, this.pin()) * M_TO_YD)} yd`;
    this.seq++;
  }
  nextHole() {
    if (this.holeIndex + 1 >= this.course.holes.length) { this.phase = 'done'; this.message = `Round over: ${this.total()} (${toPar(this.total() - this.parSoFar())})`; this.seq++; return; }
    this.startHole(this.holeIndex + 1);
  }
  aimAtPin() { const p = this.pin(); this.heading = Math.atan2(p.x - this.ball.x, p.z - this.ball.z); }

  // ---- a shot from the launch monitor
  onShot(shot) {
    this.seen = Date.now();
    if (this.phase === 'holed') this.nextHole();
    if (this.phase !== 'ready') return null;
    const fx = LIE_EFFECT[this.lie] || LIE_EFFECT.rough;
    const play = { ...shot, speedMph: shot.speedMph * fx.speed, backSpin: shot.backSpin * fx.spin, sideSpin: shot.sideSpin * fx.spin };
    const c = this.course;
    const res = simulateShot(c, this.ball, play, { heading: this.heading, altitudeM: c.altitudeM, tempC: 20, humidity: 0.5, stimp: 10, pin: this.pin() });
    this.prevBall = { ...this.ball };
    this.strokes++;
    let surface = c.surfaceAt(res.rest.x, res.rest.z), msg = '';
    if (res.result === 'holed') {
      this.ball = { ...res.rest };
      this.phase = 'holed';
      msg = this.holeOut();
    } else {
      if (res.result === 'oob' || surface === 'oob' || surface == null) {
        this.strokes++; this.ball = { ...this.prevBall };
        msg = 'Out of bounds: stroke and distance';
      } else if (res.result === 'water' || surface === 'water') {
        this.strokes++; this.ball = this.waterDrop(res);
        msg = 'Penalty area: +1, dropped where it went in';
      } else this.ball = { ...res.rest };
      this.lie = c.surfaceAt(this.ball.x, this.ball.z) || 'rough';
      if (this.strokes >= this.hole().par + 5) { this.phase = 'holed'; msg = this.holeOut(true); }
      else {
        this.aimAtPin();
        const to = dist(this.ball, this.pin());
        msg = `${msg ? msg + ' · ' : ''}${Math.round(res.carry * M_TO_YD)} yd carry, ${Math.round(res.total * M_TO_YD)} total · ${this.lie === 'green' || this.lie === 'tee' ? 'on the' : 'in the'} ${this.lie} · ${this.lie === 'green' ? Math.round(to * 3.28084) + ' ft' : Math.round(to * M_TO_YD) + ' yd'} to go`;
      }
    }
    this.message = msg;
    this.seq++;
    this.lastShot = {
      seq: this.seq,
      shot: { speedMph: shot.speedMph, vla: shot.vla, hla: shot.hla, backSpin: shot.backSpin, sideSpin: shot.sideSpin },
      result: res.result, carryYd: +(res.carry * M_TO_YD).toFixed(1), totalYd: +(res.total * M_TO_YD).toFixed(1),
      // [seconds, X, Y, Z (cm), in the air] - thinned to about 30 a second
      points: thin(res.points).map((q) => [+q.t.toFixed(3), ...this.ue(q), q.air ? 1 : 0]),
      rest: this.ue(this.ball),
    };
    return this.lastShot;
  }
  holeOut(pickedUp = false) {
    const h = this.hole();
    this.scores[this.holeIndex] = this.strokes;
    const d = this.strokes - h.par;
    const name = { '-3': 'Albatross', '-2': 'Eagle', '-1': 'Birdie', 0: 'Par', 1: 'Bogey', 2: 'Double bogey' }[d] || (d < 0 ? `${-d} under` : `${d} over`);
    return `${pickedUp ? 'Picked up' : this.strokes === 1 ? 'Hole in one!' : name} · ${this.strokes} on hole ${h.number} · round ${toPar(this.total() - this.parSoFar())}`;
  }
  waterDrop(res) {
    const c = this.course, pts = res.points;
    for (let i = pts.length - 1; i >= 0; i--) {
      const s = c.surfaceAt(pts[i].x, pts[i].z);
      if (s && s !== 'water' && s !== 'oob') {
        const ox = this.prevBall.x - pts[i].x, oz = this.prevBall.z - pts[i].z, ol = Math.hypot(ox, oz) || 1;
        for (let back = 2; back < 40; back++) {
          const x = pts[i].x + (ox / ol) * back, z = pts[i].z + (oz / ol) * back, ss = c.surfaceAt(x, z);
          if (ss && ss !== 'water' && ss !== 'oob') return { x, y: c.heightAt(x, z), z };
        }
        return { x: pts[i].x, y: c.heightAt(pts[i].x, pts[i].z), z: pts[i].z };
      }
    }
    return { ...this.prevBall };
  }
  total() { return this.scores.reduce((a, b) => a + (b || 0), 0); }
  parSoFar() { return this.scores.reduce((a, b, i) => a + (b ? this.course.holes[i].par : 0), 0); }

  // ---- course metres (x east, z north, y up) to the export's Unreal centimetres, on its ground
  ue(p) {
    const { x0, z1, base } = this.exp;
    const game = this.course.heightAt(p.x, p.z), there = this.exp.heightAt(p.x, p.z);
    const dy = Number.isFinite(game) && Number.isFinite(there) ? there - game : 0;
    return [Math.round((p.x - x0) * 100), Math.round((z1 - p.z) * 100), Math.round((p.y + dy - base) * 100)];
  }

  // ---- what Unreal shows
  state() {
    const h = this.hole(), pin = this.pin();
    // (the way the ball is aimed, as an Unreal yaw: course heading h points along (sin h, cos h) in
    // x east / z north; Unreal Y is south)
    const yaw = (Math.atan2(-Math.cos(this.heading), Math.sin(this.heading)) * 180) / Math.PI;
    return {
      seq: this.seq, course: this.course.name, phase: this.phase, message: this.message, landscapeZ: this.exp.landscapeZ,
      hole: { number: h.number, par: h.par, index: h.index, count: this.course.holes.length },
      strokes: this.strokes, lie: this.lie, scores: this.scores, total: this.total(), toPar: this.total() - this.parSoFar(),
      ball: this.ue(this.ball), pin: this.ue(pin), aimYaw: +yaw.toFixed(2),
      toPinYd: Math.round(dist(this.ball, pin) * M_TO_YD),
      lastShot: this.lastShot,
    };
  }
}

const dist = (a, b) => Math.hypot(a.x - b.x, a.z - b.z);
const toPar = (d) => (d === 0 ? 'E' : d > 0 ? `+${d}` : `${d}`);
// (the physics samples at 60 a second; every other one is plenty to play back, keeping the bounces)
const thin = (pts) => pts.filter((q, i) => i % 2 === 0 || i === pts.length - 1 || !q.air);

// The export's numbers and its 16-bit heightmap, as metres at any course point (bilinear)
function readExport(dir) {
  const meta = JSON.parse(fs.readFileSync(path.join(dir, 'course.json'), 'utf8'));
  const x0 = meta.courseOrigin.x, z1 = meta.courseOrigin.z, base = meta.heightBaseMetres;
  const N = meta.samples, step = meta.metresPerSample, range = meta.heightRangeMetres;
  let H = null;
  try {
    const { w, h, data } = readPng16(fs.readFileSync(path.join(dir, 'heightmap.png')));
    if (w === N && h === N) H = data;
  } catch { /* (no heightmap: the game's heights as they are) */ }
  const heightAt = (x, z) => {
    if (!H) return NaN;
    const fi = (x - x0) / step, fj = (z1 - z) / step;
    if (!(fi >= 0 && fj >= 0 && fi <= N - 1 && fj <= N - 1)) return NaN;
    const i = Math.min(N - 2, Math.floor(fi)), j = Math.min(N - 2, Math.floor(fj)), u = fi - i, v = fj - j;
    const a = H[j * N + i], b = H[j * N + i + 1], c = H[(j + 1) * N + i], d = H[(j + 1) * N + i + 1];
    return base + (((a * (1 - u) + b * u) * (1 - v) + (c * (1 - u) + d * u) * v) / 65535) * range;
  };
  return { x0, z1, base, heightAt, landscapeZ: meta.landscape?.locationZ ?? 0 };
}

// A 16-bit greyscale PNG (as the exporter writes them): width, height, values
function readPng16(buf) {
  let o = 8, w = 0, h = 0, depth = 0, type = 0;
  const idat = [];
  while (o < buf.length) {
    const len = buf.readUInt32BE(o), kind = buf.toString('ascii', o + 4, o + 8), body = buf.subarray(o + 8, o + 8 + len);
    if (kind === 'IHDR') { w = body.readUInt32BE(0); h = body.readUInt32BE(4); depth = body[8]; type = body[9]; }
    else if (kind === 'IDAT') idat.push(body);
    else if (kind === 'IEND') break;
    o += 12 + len;
  }
  if (depth !== 16 || type !== 0) throw new Error('not a 16-bit greyscale PNG');
  const raw = zlib.inflateSync(Buffer.concat(idat)), bpp = 2, row = w * bpp;
  const out = new Uint16Array(w * h), prev = Buffer.alloc(row), cur = Buffer.alloc(row);
  for (let j = 0; j < h; j++) {
    const f = raw[j * (row + 1)], src = raw.subarray(j * (row + 1) + 1, (j + 1) * (row + 1));
    for (let i = 0; i < row; i++) {
      const a = i >= bpp ? cur[i - bpp] : 0, b = prev[i], c = i >= bpp ? prev[i - bpp] : 0;
      let pr = 0;
      if (f === 1) pr = a; else if (f === 2) pr = b; else if (f === 3) pr = (a + b) >> 1;
      else if (f === 4) { const p = a + b - c, pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c); pr = pa <= pb && pa <= pc ? a : pb <= pc ? b : c; }
      cur[i] = (src[i] + pr) & 255;
    }
    for (let i = 0; i < w; i++) out[j * w + i] = (cur[i * 2] << 8) | cur[i * 2 + 1];
    cur.copy(prev);
  }
  return { w, h, data: out };
}
