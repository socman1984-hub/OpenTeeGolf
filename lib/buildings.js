// Building footprints around a course (OpenStreetMap), in course coordinates, cached per course.
// Each: { h (m, to the top), pts, kind (the building tag), levels, roof (shape), roofColour,
// wallColour, material, name } - whatever the map says, for drawing it as a real house, shed or
// clubhouse. (Older caches have only h and pts: they're refreshed once when online.)
import fs from 'node:fs';
import path from 'node:path';
import { overpass, osmApiMap } from './realdata.js';

const LEVEL_M = 3;
const DEFAULT_HEIGHT = { house: 7, detached: 7, residential: 9, apartments: 12, garage: 3.5, garages: 3.5, shed: 3, roof: 4, commercial: 8, retail: 6, industrial: 9, school: 9, church: 12, yes: 6.5 };

const pending = new Map();
export function ensureBuildings(satDir, geo, grid, log = () => {}) {
  const file = path.join(satDir, 'buildings.json');
  let old = null;
  if (fs.existsSync(file)) {
    const cached = JSON.parse(fs.readFileSync(file, 'utf8'));
    if (!cached.length || 'kind' in cached[0]) return Promise.resolve(cached);
    old = cached;
  }
  if (pending.has(file)) return pending.get(file);
  const job = (async () => {
    // Course <-> lat/lon (affine both ways)
    const [a0, a1, a2] = geo.lat, [b0, b1, b2] = geo.lon;
    const det = a1 * b2 - a2 * b1;
    const toCourse = (lat, lon) => {
      const dl = lat - a0, dn = lon - b0;
      return [(b2 * dl - a2 * dn) / det, (a1 * dn - b1 * dl) / det];
    };
    const maxX = grid.minX + (grid.nx - 1) * grid.cell, maxZ = grid.minZ + (grid.nz - 1) * grid.cell;
    const c = [[grid.minX, grid.minZ], [maxX, grid.minZ], [grid.minX, maxZ], [maxX, maxZ]]
      .map(([x, z]) => [a0 + a1 * x + a2 * z, b0 + b1 * x + b2 * z]);
    const box = `${Math.min(...c.map((p) => p[0]))},${Math.min(...c.map((p) => p[1]))},${Math.max(...c.map((p) => p[0]))},${Math.max(...c.map((p) => p[1]))}`;
    log('Downloading building outlines…');
    let osm;
    try {
      osm = await overpass(`[out:json][timeout:90];way["building"](${box});out geom;`, log, 3);
    } catch {
      try {
        const [s, w, n, e] = box.split(',').map(Number);
        osm = await osmApiMap({ minlat: s, minlon: w, maxlat: n, maxlon: e });
      } catch (err) {
        // Not cached, so it is retried next time (an old-style cache is used meanwhile)
        log(`Buildings skipped (${err.message})`);
        return old || [];
      }
    }
    const out = [];
    for (const e of osm.elements) {
      if (!e.tags?.building || !e.geometry || e.geometry.length < 4) continue;
      const t = e.tags || {};
      let h = parseFloat(t.height) || (parseFloat(t['building:levels']) ? parseFloat(t['building:levels']) * LEVEL_M + 1 : 0);
      if (!h) h = DEFAULT_HEIGHT[t.building] || DEFAULT_HEIGHT.yes;
      const b = { h: Math.round(h * 10) / 10, pts: e.geometry.map((p) => toCourse(p.lat, p.lon).map((v) => Math.round(v * 10) / 10)), kind: t.building };
      const levels = parseFloat(t['building:levels']);
      if (levels > 0) b.levels = levels;
      if (t['roof:shape']) b.roof = t['roof:shape'];
      if (t['roof:colour']) b.roofColour = t['roof:colour'];
      if (t['building:colour']) b.wallColour = t['building:colour'];
      if (t['building:material']) b.material = t['building:material'];
      if (t.name) b.name = t.name;
      if (t.amenity || t.leisure || t.club) b.use = t.amenity || t.leisure || (t.club && 'club');
      out.push(b);
    }
    fs.mkdirSync(satDir, { recursive: true });
    fs.writeFileSync(file, JSON.stringify(out));
    log(`${out.length} buildings ready.`);
    return out;
  })().finally(() => pending.delete(file));
  pending.set(file, job);
  return job;
}
