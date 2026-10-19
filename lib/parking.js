// Car parks around a course (OpenStreetMap), in course coordinates, cached per course next to its
// imagery: the parking areas, the aisles drawn through them and any bays mapped one by one.
// The game lays out the bays and parks cars in them (public/js/parking.js).
import fs from 'node:fs';
import path from 'node:path';
import { overpass, osmApiMap } from './realdata.js';

const pending = new Map();
export function ensureParking(satDir, geo, grid, log = () => {}) {
  const file = path.join(satDir, 'parking.json');
  if (fs.existsSync(file)) return Promise.resolve(JSON.parse(fs.readFileSync(file, 'utf8')));
  if (pending.has(file)) return pending.get(file);
  const job = (async () => {
    const [a0, a1, a2] = geo.lat, [b0, b1, b2] = geo.lon;
    const det = a1 * b2 - a2 * b1;
    const toCourse = (lat, lon) => { const dl = lat - a0, dn = lon - b0; return [(b2 * dl - a2 * dn) / det, (a1 * dn - b1 * dl) / det]; };
    const maxX = grid.minX + (grid.nx - 1) * grid.cell, maxZ = grid.minZ + (grid.nz - 1) * grid.cell;
    const c = [[grid.minX, grid.minZ], [maxX, grid.minZ], [grid.minX, maxZ], [maxX, maxZ]].map(([x, z]) => [a0 + a1 * x + a2 * z, b0 + b1 * x + b2 * z]);
    const [s, w, n, e] = [Math.min(...c.map((p) => p[0])), Math.min(...c.map((p) => p[1])), Math.max(...c.map((p) => p[0])), Math.max(...c.map((p) => p[1]))];
    const bb = `${s},${w},${n},${e}`;
    log('Downloading car parks…');
    let osm;
    try {
      osm = await overpass(`[out:json][timeout:90];(way["amenity"="parking"](${bb});way["amenity"="parking_space"](${bb});way["service"="parking_aisle"](${bb}););out geom;`, log, 3);
    } catch {
      try { osm = await osmApiMap({ minlat: s, minlon: w, maxlat: n, maxlon: e }); }
      catch (err) { log(`Car parks skipped (${err.message})`); return { lots: [], aisles: [], spaces: [] }; } // not cached: retried next time
    }
    const pts = (el) => el.geometry.map((p) => toCourse(p.lat, p.lon).map((v) => Math.round(v * 10) / 10));
    const closed = (el) => {
      const g = el.geometry, a = g[0], b = g[g.length - 1];
      return g.length >= 4 && (el.nodes ? el.nodes[0] === el.nodes[el.nodes.length - 1] : a.lat === b.lat && a.lon === b.lon);
    };
    const out = { lots: [], aisles: [], spaces: [] };
    for (const el of osm.elements) {
      const t = el.tags || {};
      if (el.type !== 'way' || !el.geometry || el.geometry.length < 2) continue;
      if (t.amenity === 'parking' && closed(el)) {
        // Only open-air car parks (not garages or underground ones)
        if (/underground|multi-storey|rooftop/.test(t.parking || '')) continue;
        out.lots.push({ pts: pts(el).slice(0, -1), surface: t.surface || 'asphalt', capacity: parseInt(t.capacity, 10) || null });
      } else if (t.amenity === 'parking_space' && closed(el)) {
        out.spaces.push({ pts: pts(el).slice(0, -1) });
      } else if (t.service === 'parking_aisle') {
        out.aisles.push({ pts: pts(el) });
      }
    }
    fs.mkdirSync(satDir, { recursive: true });
    fs.writeFileSync(file, JSON.stringify(out));
    log(`${out.lots.length} car parks ready.`);
    return out;
  })().finally(() => pending.delete(file));
  pending.set(file, job);
  return job;
}
