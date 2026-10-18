// Cart paths and walking paths on a course (OpenStreetMap lines), in course coordinates,
// cached per course next to its imagery.
import fs from 'node:fs';
import path from 'node:path';
import { overpass, osmApiMap } from './realdata.js';

// Default widths (m) when the map doesn't give one
const WIDTH = { cartpath: 2.4, path: 1.8, footway: 1.6, cycleway: 2, track: 2.6, service: 3.5 };

const pointInPoly = (poly, x, z) => {
  let inside = false;
  for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) {
    const [xi, zi] = poly[i], [xj, zj] = poly[j];
    if (zi > z !== zj > z && x < ((xj - xi) * (z - zi)) / (zj - zi) + xi) inside = !inside;
  }
  return inside;
};

const pending = new Map();
// oob: course boundary [[x, z]] (paths mostly outside it, like streets, are dropped)
export function ensurePaths(satDir, geo, grid, oob, log = () => {}) {
  const file = path.join(satDir, 'paths.json');
  if (fs.existsSync(file)) return Promise.resolve(JSON.parse(fs.readFileSync(file, 'utf8')));
  if (pending.has(file)) return pending.get(file);
  const job = (async () => {
    const [a0, a1, a2] = geo.lat, [b0, b1, b2] = geo.lon;
    const det = a1 * b2 - a2 * b1;
    const toCourse = (lat, lon) => { const dl = lat - a0, dn = lon - b0; return [(b2 * dl - a2 * dn) / det, (a1 * dn - b1 * dl) / det]; };
    const maxX = grid.minX + (grid.nx - 1) * grid.cell, maxZ = grid.minZ + (grid.nz - 1) * grid.cell;
    const c = [[grid.minX, grid.minZ], [maxX, grid.minZ], [grid.minX, maxZ], [maxX, maxZ]].map(([x, z]) => [a0 + a1 * x + a2 * z, b0 + b1 * x + b2 * z]);
    const [s, w, n, e] = [Math.min(...c.map((p) => p[0])), Math.min(...c.map((p) => p[1])), Math.max(...c.map((p) => p[0])), Math.max(...c.map((p) => p[1]))];
    log('Downloading cart paths…');
    let osm;
    try {
      osm = await overpass(`[out:json][timeout:90];(way["golf"~"^(cartpath|path)$"](${s},${w},${n},${e});way["highway"~"^(path|footway|track|service|cycleway)$"](${s},${w},${n},${e}););out geom;`, log, 3);
    } catch {
      try { osm = await osmApiMap({ minlat: s, minlon: w, maxlat: n, maxlon: e }); }
      catch (err) { log(`Cart paths skipped (${err.message})`); return []; } // not cached: retried next time
    }
    const out = [];
    for (const el of osm.elements) {
      const t = el.tags || {};
      if (el.type !== 'way' || !el.geometry || el.geometry.length < 2) continue;
      const golf = t.golf === 'cartpath' || t.golf === 'path';
      const hw = ['path', 'footway', 'track', 'service', 'cycleway'].includes(t.highway);
      if (!golf && !hw) continue;
      if (t.area === 'yes') continue;
      const pts = el.geometry.map((p) => toCourse(p.lat, p.lon).map((v) => Math.round(v * 10) / 10));
      // Streets and paths outside the course aren't cart paths
      if (!golf && oob?.length > 2) {
        const inside = pts.filter(([x, z]) => pointInPoly(oob, x, z)).length;
        if (inside < pts.length * 0.5 && !(t.golf_cart === 'designated' || t.golf_cart === 'yes')) continue;
      }
      const width = parseFloat(t.width) || WIDTH[golf ? t.golf : t.highway] || 2.4;
      out.push({ width: Math.min(6, Math.max(1.2, width)), pts, surface: t.surface || (golf ? 'asphalt' : 'unknown') });
    }
    fs.mkdirSync(satDir, { recursive: true });
    fs.writeFileSync(file, JSON.stringify(out));
    log(`${out.length} cart paths ready.`);
    return out;
  })().finally(() => pending.delete(file));
  pending.set(file, job);
  return job;
}
