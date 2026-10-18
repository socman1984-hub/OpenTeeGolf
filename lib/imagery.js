// Aerial/satellite imagery for a course, downloaded once and cached next to the course's real data.
//
//   US:        USGS NAIP orthoimagery (0.3–0.6 m, public domain)
//   elsewhere: Esri World Imagery (attribution required; for personal, non-commercial use)
//
// Images are requested in plain lat/lon (EPSG:4326). Course space maps to lat/lon by an affine
// transform ("geo"), so the browser can place every image with a single canvas transform.

import fs from 'node:fs';
import path from 'node:path';
import { UA, localFrame } from './realdata.js';

const NAIP = 'https://imagery.nationalmap.gov/arcgis/rest/services/USGSNAIPImagery/ImageServer/exportImage';
const ESRI = 'https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/export';
const PROVIDERS = {
  naip: { url: NAIP, attribution: 'Imagery: USGS / USDA National Agriculture Imagery Program (public domain)' },
  esri: { url: ESRI, attribution: 'Imagery: Esri, Maxar, Earthstar Geographics, and the GIS User Community' },
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
  if (!metaRes.ok || metaText.trimStart().startsWith('<')) throw new Error(`${provider === 'naip' ? 'USGS' : 'Esri'} imagery server busy (${metaRes.status})`);
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
    log(`Downloading aerial imagery ${out.length + 1}/${rows * cols} (${cw}×${ch})…`);
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
    const order = inUS(course) ? ['naip', 'esri'] : ['esri'];
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
    const manifest = { version: 1, provider, attribution: PROVIDERS[provider].attribution, geo, chunks, surround, created: new Date().toISOString() };
    fs.writeFileSync(manifestFile, JSON.stringify(manifest));
    log('Aerial imagery ready.');
    return manifest;
  })().finally(() => pending.delete(satDir));
  pending.set(satDir, job);
  return job;
}
