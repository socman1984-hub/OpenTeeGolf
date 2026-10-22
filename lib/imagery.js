// Aerial/satellite imagery for a course, downloaded once and cached next to the course's real data.
//
//   US:        the clearest of USGS NAIP orthoimagery (0.3–0.6 m, public domain), Esri World
//              Imagery and the USGS imagery basemap: a small preview of the course from each is
//              scored for haze and cloud (hazy photos are pale, washed out and soft), and the best
//              is downloaded. Some NAIP years are taken through haze.
//   elsewhere: Esri World Imagery (attribution required; for personal, non-commercial use)
//
// Images are requested in plain lat/lon (EPSG:4326). Course space maps to lat/lon by an affine
// transform ("geo"), so the browser can place every image with a single canvas transform.

import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import { UA, localFrame } from './realdata.js';

const NAIP = 'https://imagery.nationalmap.gov/arcgis/rest/services/USGSNAIPImagery/ImageServer/exportImage';
const ESRI = 'https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/export';
const USGS = 'https://basemap.nationalmap.gov/arcgis/rest/services/USGSImageryOnly/MapServer/export';
const PROVIDERS = {
  naip: { url: NAIP, name: 'USGS NAIP', attribution: 'Imagery: USGS / USDA National Agriculture Imagery Program (public domain)' },
  esri: { url: ESRI, name: 'Esri World Imagery', attribution: 'Imagery: Esri, Maxar, Earthstar Geographics, and the GIS User Community' },
  usgs: { url: USGS, name: 'USGS imagery', attribution: 'Imagery: USGS The National Map (public domain)' },
};
const CHUNK = 2048;       // max pixels per request side (smaller requests render faster on the servers)
const METERS_PER_PX = 0.5;
const SURROUND_M = 1500;  // low-res imagery around the course for the horizon

// Course (x, z) -> lat/lon affine: lat = a0 + a1*x + a2*z, lon = b0 + b1*x + b2*z
export function geoFor(courseData, real) {
  if (real?.geo) return real.geo;
  const f = courseData?.GolfSimBuilder?.frame;
  if (f) {
    const fr = localFrame(f.lat0, f.lon0);
    return { lat: [f.lat0, 0, 1 / fr.mLat], lon: [f.lon0, 1 / fr.mLon, 0] };
  }
  return null;
}
// Fit the affine from any course->[lat, lon] function
export function geoFromFunction(toLatLon) {
  const o = toLatLon(0, 0), ex = toLatLon(1, 0), ez = toLatLon(0, 1);
  return { lat: [o[0], ex[0] - o[0], ez[0] - o[0]], lon: [o[1], ex[1] - o[1], ez[1] - o[1]] };
}
const toLL = (geo, x, z) => [geo.lat[0] + geo.lat[1] * x + geo.lat[2] * z, geo.lon[0] + geo.lon[1] * x + geo.lon[2] * z];

const inUS = (b) => b.minLon > -170 && b.maxLon < -60 && b.minLat > 17 && b.maxLat < 72;

async function exportImage(provider, b, w, h) {
  const base = `${PROVIDERS[provider].url}?bbox=${b.minLon},${b.minLat},${b.maxLon},${b.maxLat}&bboxSR=4326&imageSR=4326&size=${w},${h}&format=jpg`;
  // f=json reports the extent the server actually used; the same request with f=image returns the pixels.
  const metaRes = await fetch(`${base}&f=json`, { headers: { 'User-Agent': UA }, signal: AbortSignal.timeout(60000) });
  const metaText = await metaRes.text();
  if (!metaRes.ok || metaText.trimStart().startsWith('<')) throw new Error(`${PROVIDERS[provider].name} server busy (${metaRes.status})`);
  const meta = JSON.parse(metaText);
  if (!meta.extent) throw new Error(`${provider} imagery unavailable`);
  const res = await fetch(`${base}&f=image`, { headers: { 'User-Agent': UA }, signal: AbortSignal.timeout(180000) });
  if (!res.ok) throw new Error(`${provider} imagery HTTP ${res.status}`);
  const buf = Buffer.from(await res.arrayBuffer());
  // Blank / no-coverage responses are tiny for their size
  if (buf.length < (w * h) / 60) throw new Error(`${provider} has no imagery here`);
  const e = meta.extent;
  return { buf, extent: { minLon: e.xmin, minLat: e.ymin, maxLon: e.xmax, maxLat: e.ymax } };
}

async function fetchArea(provider, b, metersPerPx, maxSide, dir, prefix, log) {
  const mLat = 111132.92;
  const px = Math.max(metersPerPx, (Math.max((b.maxLat - b.minLat), (b.maxLon - b.minLon)) * mLat) / maxSide) / mLat;
  const W = Math.ceil((b.maxLon - b.minLon) / px), H = Math.ceil((b.maxLat - b.minLat) / px);
  const cols = Math.ceil(W / CHUNK), rows = Math.ceil(H / CHUNK);
  const out = [];
  for (let r = 0; r < rows; r++) for (let c = 0; c < cols; c++) {
    const cw = Math.min(CHUNK, W - c * CHUNK), ch = Math.min(CHUNK, H - r * CHUNK);
    const cb = {
      minLon: b.minLon + c * CHUNK * px, maxLon: b.minLon + (c * CHUNK + cw) * px,
      maxLat: b.maxLat - r * CHUNK * px, minLat: b.maxLat - (r * CHUNK + ch) * px,
    };
    log(`Downloading ${prefix === 'surround_' ? 'surrounding ' : ''}aerial imagery ${out.length + 1}/${rows * cols}…`);
    let img;
    for (let attempt = 1; ; attempt++) {
      try { img = await exportImage(provider, cb, cw, ch); break; }
      catch (err) { if (attempt >= 2 || /no imagery/.test(err.message)) throw err; log(`${err.message} — retrying…`); }
    }
    const file = `${prefix}${r}_${c}.jpg`;
    fs.writeFileSync(path.join(dir, file), img.buf);
    out.push({ file, extent: img.extent, width: cw, height: ch });
  }
  return out;
}

// ---- picking the clearest source

// A minimal PNG reader (8-bit RGB or RGBA, not interlaced: what the imagery servers send for
// format=png24/png32) -> { w, h, rgb }
function decodePng(buf) {
  if (buf.readUInt32BE(0) !== 0x89504e47) throw new Error('not a PNG');
  let o = 8, w = 0, h = 0, type = 0, depth = 0, inter = 0;
  const idat = [];
  while (o < buf.length) {
    const len = buf.readUInt32BE(o), kind = buf.toString('ascii', o + 4, o + 8), d = buf.subarray(o + 8, o + 8 + len);
    if (kind === 'IHDR') { w = d.readUInt32BE(0); h = d.readUInt32BE(4); depth = d[8]; type = d[9]; inter = d[12]; }
    else if (kind === 'IDAT') idat.push(d);
    else if (kind === 'IEND') break;
    o += 12 + len;
  }
  if (depth !== 8 || inter || (type !== 2 && type !== 6)) throw new Error('unsupported PNG');
  const bpp = type === 6 ? 4 : 3, stride = w * bpp, raw = zlib.inflateSync(Buffer.concat(idat));
  const out = Buffer.alloc(h * stride), rgb = new Uint8Array(w * h * 3);
  for (let y = 0; y < h; y++) {
    const f = raw[y * (stride + 1)], line = raw.subarray(y * (stride + 1) + 1, (y + 1) * (stride + 1));
    for (let x = 0; x < stride; x++) {
      const a = x >= bpp ? out[y * stride + x - bpp] : 0, b = y ? out[(y - 1) * stride + x] : 0, c = x >= bpp && y ? out[(y - 1) * stride + x - bpp] : 0;
      let v = line[x];
      if (f === 1) v += a; else if (f === 2) v += b; else if (f === 3) v += (a + b) >> 1;
      else if (f === 4) { const p = a + b - c, pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c); v += pa <= pb && pa <= pc ? a : pb <= pc ? b : c; }
      out[y * stride + x] = v & 255;
    }
  }
  for (let i = 0; i < w * h; i++) { rgb[i * 3] = out[i * bpp]; rgb[i * 3 + 1] = out[i * bpp + 1]; rgb[i * 3 + 2] = out[i * bpp + 2]; }
  return { w, h, rgb };
}

// How clear an aerial photo is: rich colour and contrast score up; haze and cloud (a bright
// "dark channel": even the darkest of red, green and blue round each spot is pale) score down.
export function clarityScore({ w, h, rgb }) {
  const n = w * h, mn = new Float32Array(n), lum = new Float32Array(n);
  let sat = 0;
  for (let i = 0; i < n; i++) {
    const r = rgb[i * 3], g = rgb[i * 3 + 1], b = rgb[i * 3 + 2], hi = Math.max(r, g, b), lo = Math.min(r, g, b);
    mn[i] = lo; lum[i] = 0.299 * r + 0.587 * g + 0.114 * b; sat += (hi - lo) / (hi + 1);
  }
  sat /= n;
  // dark channel: the smallest channel's minimum over a 9 x 9 neighbourhood (rows, then columns)
  const R = 4, t = new Float32Array(n);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) { let m = 255; for (let k = Math.max(0, x - R); k <= Math.min(w - 1, x + R); k++) m = Math.min(m, mn[y * w + k]); t[y * w + x] = m; }
  let dark = 0;
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) { let m = 255; for (let k = Math.max(0, y - R); k <= Math.min(h - 1, y + R); k++) m = Math.min(m, t[k * w + x]); dark += m; }
  dark /= n;
  let mean = 0; for (let i = 0; i < n; i++) mean += lum[i]; mean /= n;
  let vr = 0, edge = 0;
  for (let i = 0; i < n; i++) vr += (lum[i] - mean) ** 2;
  for (let y = 1; y < h - 1; y++) for (let x = 1; x < w - 1; x++) { const i = y * w + x; edge += Math.abs(4 * lum[i] - lum[i - 1] - lum[i + 1] - lum[i - w] - lum[i + w]); }
  const std = Math.sqrt(vr / n);
  edge /= (w - 2) * (h - 2);
  if (std < 3) return -Infinity; // (blank: no coverage)
  return sat * 100 + std * 0.5 - dark * 0.5 + edge * 0.3;
}

// A photo of an area as plain RGB (w x h pixels, north up), from Esri World Imagery (worldwide):
// for working things out from the colours, e.g. where the sea is (see seawater.js)
export async function fetchPhotoRGB(b, w, h) {
  const url = `${ESRI}?bbox=${b.minLon},${b.minLat},${b.maxLon},${b.maxLat}&bboxSR=4326&imageSR=4326&size=${w},${h}&format=png24&f=image`;
  const res = await fetch(url, { headers: { 'User-Agent': UA }, signal: AbortSignal.timeout(90000) });
  if (!res.ok) throw new Error(`HTTP ${res.status}`);
  return decodePng(Buffer.from(await res.arrayBuffer()));
}

async function previewScore(provider, b) {
  const url = `${PROVIDERS[provider].url}?bbox=${b.minLon},${b.minLat},${b.maxLon},${b.maxLat}&bboxSR=4326&imageSR=4326&size=256,256&format=png24&f=image`;
  const res = await fetch(url, { headers: { 'User-Agent': UA }, signal: AbortSignal.timeout(60000) });
  if (!res.ok) throw new Error(`HTTP ${res.status}`);
  return clarityScore(decodePng(Buffer.from(await res.arrayBuffer())));
}

// The sources to try for this area, clearest first, with their scores
async function rankProviders(b, log) {
  if (!inUS(b)) return { order: ['esri'], scores: {} };
  log('Comparing aerial photos from different sources…');
  const list = ['naip', 'esri', 'usgs'];
  const scores = {};
  await Promise.all(list.map(async (p) => { try { scores[p] = Math.round(await previewScore(p, b) * 10) / 10; } catch { scores[p] = -Infinity; } }));
  // (NAIP wins near-ties: public domain and consistent from course to course)
  const adj = (p) => scores[p] + (p === 'naip' ? 4 : 0);
  const order = list.slice().sort((a, c) => adj(c) - adj(a));
  log(`Clearest photo: ${PROVIDERS[order[0]].name} (${list.map((p) => `${PROVIDERS[p].name} ${Number.isFinite(scores[p]) ? scores[p] : 'n/a'}`).join(', ')})`);
  return { order, scores };
}

const courseBox = (geo, grid) => {
  const maxX = grid.minX + (grid.nx - 1) * grid.cell, maxZ = grid.minZ + (grid.nz - 1) * grid.cell;
  const c = [[grid.minX, grid.minZ], [maxX, grid.minZ], [grid.minX, maxZ], [maxX, maxZ]].map(([x, z]) => toLL(geo, x, z));
  return { minLat: Math.min(...c.map((p) => p[0])), maxLat: Math.max(...c.map((p) => p[0])), minLon: Math.min(...c.map((p) => p[1])), maxLon: Math.max(...c.map((p) => p[1])) };
};

// For a course already built: if another source's photo is clearly clearer than the one it has,
// swap to it (the photo trees, worked out from the old photo, go too and are found again).
// Returns true when it changed.
export async function recheckSatellite(satDir, geo, grid, log = () => {}) {
  const manifestFile = path.join(satDir, 'manifest.json');
  if (!fs.existsSync(manifestFile)) return false;
  const old = JSON.parse(fs.readFileSync(manifestFile, 'utf8'));
  const { order, scores } = await rankProviders(courseBox(geo, grid), log);
  const best = order[0], cur = old.provider;
  if (best === cur || !(scores[best] > (scores[cur] ?? -Infinity) + 8)) return false;
  log(`Switching the aerial photo to ${PROVIDERS[best].name} (clearer)…`);
  for (const f of fs.readdirSync(satDir)) if (/^(course|surround)_\d+_\d+\.jpg$/.test(f) || /^photo_clean_\w+\.jpg$/.test(f) || f === 'manifest.json' || f === 'phototrees.json') fs.rmSync(path.join(satDir, f), { force: true });
  await ensureSatellite(satDir, geo, grid, log);
  return true;
}

const pending = new Map();
// Returns the imagery manifest, downloading the imagery the first time.
export function ensureSatellite(satDir, geo, grid, log = () => {}) {
  const manifestFile = path.join(satDir, 'manifest.json');
  if (fs.existsSync(manifestFile)) return Promise.resolve(JSON.parse(fs.readFileSync(manifestFile, 'utf8')));
  if (pending.has(satDir)) return pending.get(satDir);
  const job = (async () => {
    fs.mkdirSync(satDir, { recursive: true });
    const maxX = grid.minX + (grid.nx - 1) * grid.cell, maxZ = grid.minZ + (grid.nz - 1) * grid.cell;
    const bboxOf = (x0, z0, x1, z1) => {
      const c = [[x0, z0], [x1, z0], [x0, z1], [x1, z1]].map(([x, z]) => toLL(geo, x, z));
      return { minLat: Math.min(...c.map((p) => p[0])), maxLat: Math.max(...c.map((p) => p[0])), minLon: Math.min(...c.map((p) => p[1])), maxLon: Math.max(...c.map((p) => p[1])) };
    };
    const course = bboxOf(grid.minX, grid.minZ, maxX, maxZ);
    const { order, scores } = await rankProviders(course, log);
    let provider = null, chunks = null, lastErr;
    for (const p of order) {
      try { chunks = await fetchArea(p, course, METERS_PER_PX, 8000, satDir, 'course_', log); provider = p; break; }
      catch (err) { lastErr = err; log(`${err.message}; trying another source…`); }
    }
    if (!chunks) throw lastErr;
    let surround = null;
    try {
      const s = bboxOf(grid.minX - SURROUND_M, grid.minZ - SURROUND_M, maxX + SURROUND_M, maxZ + SURROUND_M);
      surround = (await fetchArea(provider, s, 3, 3000, satDir, 'surround_', log));
    } catch (err) { log(`Surrounding imagery skipped (${err.message})`); }
    const manifest = { version: 1, provider, attribution: PROVIDERS[provider].attribution, geo, chunks, surround, scores, created: new Date().toISOString() };
    fs.writeFileSync(manifestFile, JSON.stringify(manifest));
    log('Aerial imagery ready.');
    return manifest;
  })().finally(() => pending.delete(satDir));
  pending.set(satDir, job);
  return job;
}
