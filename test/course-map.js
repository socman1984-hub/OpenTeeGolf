// Builds the course model and writes a top-down surface map (PPM) for inspection.
// Usage: node test/course-map.js <path to .course.json> <out.png> [real.json]
import fs from 'node:fs';
import zlib from 'node:zlib';
import { buildCourse } from '../public/js/course.js';
const courseData = JSON.parse(fs.readFileSync(process.argv[2], 'utf8').replace(/^\uFEFF/, ''));
let t = Date.now();
const real = process.argv[4] ? JSON.parse(fs.readFileSync(process.argv[4], 'utf8')) : null;
const c = buildCourse(courseData, real);
console.log('synthetic greens on holes:', c.holes.filter((h) => h.synthGreen).map((h) => h.number).join(', ') || 'none');
console.log('build ms', Date.now() - t, 'grid', c.grid.nx, c.grid.nz, 'holes', c.holes.length, 'alt', c.altitudeM.toFixed(0));
const col = { green: [80, 190, 70], fringe: [70, 160, 55], fairway: [110, 175, 60], tee: [140, 200, 90], rough: [50, 100, 35], water: [40, 90, 200], sand: [225, 210, 160], oob: [90, 70, 50], null: [0, 0, 0] };
const S = 2, { minX, maxX, minZ, maxZ } = c.bounds;
const w = Math.floor((maxX - minX) / S), h = Math.floor((maxZ - minZ) / S);
const buf = Buffer.alloc(w * h * 3);
t = Date.now();
const hs = c.grid.heights; let hmin = Infinity, hmax = -Infinity; for (const v of hs) { hmin = Math.min(hmin, v); hmax = Math.max(hmax, v); }
for (let j = 0; j < h; j++) for (let i = 0; i < w; i++) {
  const x = minX + i * S, z = maxZ - j * S;
  const s = c.surfaceAt(x, z); const k = col[s] || col.null;
  const sh = 0.75 + 0.5 * ((c.heightAt(x, z) ?? hmin) - hmin) / (hmax - hmin);
  const o = (j * w + i) * 3; buf[o] = Math.min(255, k[0] * sh); buf[o + 1] = Math.min(255, k[1] * sh); buf[o + 2] = Math.min(255, k[2] * sh);
}
for (const hole of c.holes) for (const p of [...Object.values(hole.pins), ...Object.values(hole.tees)]) {
  const i = Math.round((p.x - minX) / S), j = Math.round((maxZ - p.z) / S);
  for (let a = -1; a <= 1; a++) for (let b = -1; b <= 1; b++) { const o = ((j + a) * w + i + b) * 3; buf[o] = 255; buf[o + 1] = 255; buf[o + 2] = 255; }
}
console.log('surface ms', Date.now() - t, 'height range', hmin.toFixed(1), hmax.toFixed(1));
const crcT = Array.from({ length: 256 }, (_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; return c >>> 0; });
const crc = (b) => { let c = ~0; for (const x of b) c = crcT[(c ^ x) & 255] ^ (c >>> 8); return (~c) >>> 0; };
const chunk = (type, data) => { const l = Buffer.alloc(4); l.writeUInt32BE(data.length); const td = Buffer.concat([Buffer.from(type), data]); const cb = Buffer.alloc(4); cb.writeUInt32BE(crc(td)); return Buffer.concat([l, td, cb]); };
const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4); ihdr[8] = 8; ihdr[9] = 2;
const raw = Buffer.alloc((w * 3 + 1) * h); for (let j = 0; j < h; j++) buf.copy(raw, j * (w * 3 + 1) + 1, j * w * 3, (j + 1) * w * 3);
fs.writeFileSync(process.argv[3], Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ihdr), chunk('IDAT', zlib.deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]));
