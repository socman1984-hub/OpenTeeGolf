// The land around a course out to the horizon: real elevation and satellite photos for an
// 80 x 80 km square centred on the course, so distant hills and mountains stand where they are.
// Nothing grows on it; it's only the shape of the land with the photos draped over it.
//
//   farland.json  manifest: the square's lat/lon extent, the height grid's size, the photo layers
//   farland.bin   heights: Int16 metres above sea level, rows north to south, west to east
//   farN_*.jpg    the photo layers (Esri World Imagery): sharper ones covering less ground
//                 nearer the course, where hills take up more of the screen
//   s2_Z_X_Y.jpg  Sentinel-2 cloudless tiles (EOX, 10 m): one even, cloud-free mosaic of the
//                 whole world. Esri's imagery is a patchwork of photos taken at different times
//                 (side by side: a green one, a blue-grey one, a brown one), so the game takes
//                 the colours from Sentinel-2 and only the fine detail from Esri.
//
// Heights come from the Terrarium elevation tiles (AWS Open Data) at a coarse zoom.
import fs from 'node:fs';
import path from 'node:path';
import { localFrame, fetchTerrarium, UA } from './realdata.js';
import { PROVIDERS, fetchArea, inUS } from './imagery.js';

const S2 = 'https://tiles.maps.eox.at/wmts/1.0.0/s2cloudless-2020_3857/default/g';
const S2_ATTRIBUTION = 'Sentinel-2 cloudless 2020 by EOX IT Services GmbH (contains modified Copernicus Sentinel data 2020), CC BY-NC-SA 4.0';

export const FAR_HALF = 40000;   // metres from the course centre to the square's edge
const GRID = 1024;               // height samples along each side (about 78 m apart)
const PHOTO_SIDE = 4096;         // photo pixels along each layer's longer side
// Photo layers, nearest first: half-width of the square each covers (about 3, 8 and 20 m a pixel)
const LAYERS = [6000, 16000, FAR_HALF];

const toLL = (geo, x, z) => [geo.lat[0] + geo.lat[1] * x + geo.lat[2] * z, geo.lon[0] + geo.lon[1] * x + geo.lon[2] * z];

const pending = new Map();
// The manifest, downloading everything the first time (cached with the course's imagery). A
// manifest from before the sharper layers keeps its heights and photo and gains the new layers.
export function ensureFarLand(satDir, geo, grid, log = () => {}) {
  const manifestFile = path.join(satDir, 'farland.json');
  const old = fs.existsSync(manifestFile) && fs.existsSync(path.join(satDir, 'farland.bin'))
    ? JSON.parse(fs.readFileSync(manifestFile, 'utf8')) : null;
  if (old?.version >= 3) return Promise.resolve(old);
  if (pending.has(satDir)) return pending.get(satDir);
  const job = (async () => {
    fs.mkdirSync(satDir, { recursive: true });
    const cx = grid.minX + ((grid.nx - 1) * grid.cell) / 2, cz = grid.minZ + ((grid.nz - 1) * grid.cell) / 2;
    const [lat0, lon0] = toLL(geo, cx, cz);
    const fr = localFrame(lat0, lon0);
    const square = (half) => ({
      minLat: lat0 - half / fr.mLat, maxLat: lat0 + half / fr.mLat,
      minLon: lon0 - half / fr.mLon, maxLon: lon0 + half / fr.mLon,
    });
    const b = old?.extent || square(FAR_HALF);

    // Heights: the coarsest zoom that still gives a sample every ~78 m
    let Z = old?.zoom;
    if (!old) {
      const want = (2 * FAR_HALF) / GRID;
      Z = Math.max(8, Math.min(12, Math.ceil(Math.log2((156543 * Math.cos((lat0 * Math.PI) / 180)) / want))));
      log('Downloading the distant land’s elevation…');
      const elev = await fetchTerrarium(b, (msg, step) => {
        if (step?.total) log(`Distant elevation ${step.at}/${step.total} tiles`);
      }, Z);
      const H = new Int16Array(GRID * GRID);
      for (let j = 0; j < GRID; j++) {
        const lat = b.maxLat - ((j + 0.5) / GRID) * (b.maxLat - b.minLat);
        for (let i = 0; i < GRID; i++) {
          const lon = b.minLon + ((i + 0.5) / GRID) * (b.maxLon - b.minLon);
          const v = elev.sample(lat, lon);
          H[j * GRID + i] = Number.isFinite(v) ? Math.max(-32000, Math.min(32000, Math.round(v))) : 0;
        }
      }
      fs.writeFileSync(path.join(satDir, 'farland.bin'), Buffer.from(H.buffer));
    }

    // The photo layers: Esri everywhere (the USGS basemap as a fallback in the US)
    let provider = old?.provider || null;
    const layers = [];
    for (let n = 0; n < LAYERS.length; n++) {
      const half = LAYERS[n];
      // (already downloaded: kept; the old single photo is the widest layer)
      const had = old?.layers?.find((l) => l.half === half) || (old && half === FAR_HALF && old.imagery?.length ? { half, imagery: old.imagery } : null);
      if (had) { layers.push(had); continue; }
      let imagery = null, lastErr = null;
      for (const p of inUS(b) ? ['esri', 'usgs'] : ['esri']) {
        try {
          imagery = await fetchArea(p, square(half), (2 * half) / PHOTO_SIDE, PHOTO_SIDE, satDir, `far${n}_`,
            (m) => log(m.replace(/aerial imagery/, `distant photo (${Math.round(half / 1000)} km)`)));
          provider ||= p;
          break;
        } catch (err) { lastErr = err; log(`${err.message}; trying another source…`); }
      }
      if (imagery) layers.push({ half, imagery });
      else log(`Distant photo within ${Math.round(half / 1000)} km skipped (${lastErr?.message || 'unavailable'})`);
    }

    // Sentinel-2 colour: zoom 14 (about 6-10 m) within 16 km, zoom 12 out to the edge
    let sentinel = [];
    try {
      sentinel = [...await s2Tiles(satDir, square(LAYERS[1]), 14, log), ...await s2Tiles(satDir, b, 12, log)];
    } catch (err) { log(`Even-coloured satellite tiles skipped (${err.message})`); }

    const manifest = {
      version: 3, half: FAR_HALF, extent: b, w: GRID, h: GRID, zoom: Z,
      heights: 'farland.bin', layers, provider, sentinel,
      sentinelAttribution: sentinel.length ? S2_ATTRIBUTION : null,
      attribution: provider ? PROVIDERS[provider].attribution : null,
      elevation: 'Terrarium elevation tiles (AWS Open Data)',
      created: old?.created || new Date().toISOString(),
    };
    fs.writeFileSync(manifestFile, JSON.stringify(manifest));
    log('Distant land ready.');
    return manifest;
  })().finally(() => pending.delete(satDir));
  pending.set(satDir, job);
  return job;
}

// Web-Mercator tiles covering a lat/lon box at zoom z, downloaded (6 at a time) into the course's
// imagery folder: [{ file, extent, width, height }] (the same shape as the Esri chunks)
async function s2Tiles(dir, b, z, log) {
  const n = 2 ** z;
  const tx = (lon) => Math.floor(((lon + 180) / 360) * n);
  const ty = (lat) => { const r = (lat * Math.PI) / 180; return Math.floor(((1 - Math.log(Math.tan(r) + 1 / Math.cos(r)) / Math.PI) / 2) * n); };
  const lonOf = (x) => (x / n) * 360 - 180;
  const latOf = (y) => (Math.atan(Math.sinh(Math.PI * (1 - (2 * y) / n))) * 180) / Math.PI;
  const want = [];
  for (let y = ty(b.maxLat); y <= ty(b.minLat); y++) for (let x = tx(b.minLon); x <= tx(b.maxLon); x++) want.push({ x, y });
  if (want.length > 1500) throw new Error('too many tiles');
  const out = [];
  let done = 0, next = 0;
  const worker = async () => {
    while (next < want.length) {
      const { x, y } = want[next++];
      const file = `s2_${z}_${x}_${y}.jpg`, full = path.join(dir, file);
      if (!fs.existsSync(full)) {
        for (let attempt = 1; ; attempt++) {
          try {
            const res = await fetch(`${S2}/${z}/${y}/${x}.jpg`, { headers: { 'User-Agent': UA }, signal: AbortSignal.timeout(30000) });
            if (!res.ok) throw new Error(`HTTP ${res.status}`);
            fs.writeFileSync(full, Buffer.from(await res.arrayBuffer()));
            break;
          } catch (err) { if (attempt >= 3) throw err; await new Promise((r) => setTimeout(r, 1000 * attempt)); }
        }
      }
      out.push({ file, width: 256, height: 256, extent: { minLon: lonOf(x), maxLon: lonOf(x + 1), maxLat: latOf(y), minLat: latOf(y + 1) } });
      if (++done % 25 === 0 || done === want.length) log(`Even-coloured satellite tiles (zoom ${z}) ${done}/${want.length}`);
    }
  };
  await Promise.all(Array.from({ length: 6 }, worker));
  return out;
}
