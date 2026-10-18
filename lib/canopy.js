// Tree canopy heights for a course: Meta & World Resources Institute "High Resolution Canopy
// Height Maps" (about 1 m resolution, worldwide, CC BY 4.0), read straight from the public
// cloud-optimised GeoTIFFs on AWS. The result is a 1 m grid of canopy height in metres over the
// course, cached next to its imagery as canopy.bin + canopy.json.
import fs from 'node:fs';
import path from 'node:path';
import { fromUrl } from 'geotiff';

const BASE = 'https://dataforgood-fb-data.s3.amazonaws.com/forests/v1/alsgedi_global_v6_float/chm';
const ZOOM = 9;
export const CANOPY_CREDIT = 'Tree heights: Meta & World Resources Institute, High Resolution Canopy Height Maps (CC BY 4.0)';

const R = 6378137;
const merc = (lat, lon) => [(lon * Math.PI / 180) * R, Math.log(Math.tan(Math.PI / 4 + (lat * Math.PI) / 360)) * R];
function quadkey(lat, lon) {
  const n = 2 ** ZOOM, x = Math.floor(((lon + 180) / 360) * n), r = (lat * Math.PI) / 180;
  const y = Math.floor(((1 - Math.log(Math.tan(r) + 1 / Math.cos(r)) / Math.PI) / 2) * n);
  let q = '';
  for (let i = ZOOM; i > 0; i--) { const m = 1 << (i - 1); q += ((x & m) ? 1 : 0) + ((y & m) ? 2 : 0); }
  return q;
}

const pending = new Map();
// geo: course -> lat/lon affine; grid: { minX, minZ, nx, nz, cell }
export function ensureCanopy(satDir, geo, grid, log = () => {}) {
  const meta = path.join(satDir, 'canopy.json');
  if (fs.existsSync(meta)) return Promise.resolve(JSON.parse(fs.readFileSync(meta, 'utf8')));
  if (pending.has(meta)) return pending.get(meta);
  const job = (async () => {
    const [a0, a1, a2] = geo.lat, [b0, b1, b2] = geo.lon;
    const toLL = (x, z) => [a0 + a1 * x + a2 * z, b0 + b1 * x + b2 * z];
    const minX = grid.minX, maxX = grid.minX + (grid.nx - 1) * grid.cell;
    const minZ = grid.minZ, maxZ = grid.minZ + (grid.nz - 1) * grid.cell;
    const res = 1, w = Math.ceil(maxX - minX), h = Math.ceil(maxZ - minZ);
    const out = new Uint8Array(w * h);
    // Which map tiles the course touches, and the mercator box it needs from each
    const corners = [[minX, minZ], [maxX, minZ], [minX, maxZ], [maxX, maxZ]].map(([x, z]) => toLL(x, z));
    const tiles = new Set();
    for (const [la, lo] of corners) tiles.add(quadkey(la, lo));
    const ms = corners.map(([la, lo]) => merc(la, lo));
    const mx0 = Math.min(...ms.map((m) => m[0])) - 20, mx1 = Math.max(...ms.map((m) => m[0])) + 20;
    const my0 = Math.min(...ms.map((m) => m[1])) - 20, my1 = Math.max(...ms.map((m) => m[1])) + 20;
    let found = 0;
    for (const q of tiles) {
      let img;
      try { img = await (await fromUrl(`${BASE}/${q}.tif`)).getImage(); }
      catch (err) { log(`No canopy data for tile ${q} (${err.message})`); continue; }
      const [tx0, ty0, tx1, ty1] = img.getBoundingBox(), pr = img.getResolution()[0];
      const W = img.getWidth(), H = img.getHeight();
      const i0 = Math.max(0, Math.floor((Math.max(mx0, tx0) - tx0) / pr)), i1 = Math.min(W, Math.ceil((Math.min(mx1, tx1) - tx0) / pr));
      const j0 = Math.max(0, Math.floor((ty1 - Math.min(my1, ty1)) / pr)), j1 = Math.min(H, Math.ceil((ty1 - Math.max(my0, ty0)) / pr));
      if (i1 <= i0 || j1 <= j0) continue;
      log(`Downloading tree heights (${(i1 - i0)} × ${(j1 - j0)} m)…`);
      // The file is stored as one strip per row; read it in batches of rows (one big read opens
      // too many connections at once), retrying a batch if the connection drops
      const ww = i1 - i0, data = new Uint8Array(ww * (j1 - j0));
      for (let jj = j0; jj < j1; jj += 150) {
        const je = Math.min(j1, jj + 150);
        for (let attempt = 1; ; attempt++) {
          try {
            data.set((await img.readRasters({ window: [i0, jj, i1, je] }))[0], (jj - j0) * ww);
            break;
          } catch (err) {
            if (attempt >= 4) throw err;
            await new Promise((r) => setTimeout(r, 1000 * attempt));
          }
        }
        log(`Downloading tree heights… ${Math.round(((je - j0) / (j1 - j0)) * 100)}%`);
      }
      // Sample each course cell (nearest map pixel)
      for (let j = 0; j < h; j++) {
        const z = maxZ - (j + 0.5) * res;
        for (let i = 0; i < w; i++) {
          const [la, lo] = toLL(minX + (i + 0.5) * res, z);
          const [mx, my] = merc(la, lo);
          if (mx < tx0 || mx >= tx1 || my <= ty0 || my > ty1) continue;
          const pi = Math.floor((mx - tx0) / pr) - i0, pj = Math.floor((ty1 - my) / pr) - j0;
          if (pi < 0 || pj < 0 || pi >= ww || pj >= j1 - j0) continue;
          out[j * w + i] = data[pj * ww + pi];
        }
      }
      found++;
    }
    const result = { available: found > 0, minX, maxZ, w, h, res, credit: CANOPY_CREDIT };
    fs.mkdirSync(satDir, { recursive: true });
    if (found) fs.writeFileSync(path.join(satDir, 'canopy.bin'), out);
    fs.writeFileSync(meta, JSON.stringify(result));
    log(found ? 'Tree heights ready.' : 'No tree height data here; trees come from the photo.');
    return result;
  })().catch((err) => { log(`Tree heights skipped (${err.message})`); return { available: false }; }) // not cached: retried next time
    .finally(() => pending.delete(meta));
  pending.set(meta, job);
  return job;
}
