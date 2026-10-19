// Public roads around a course (OpenStreetMap), in course coordinates, cached per course next to
// its imagery. Only the land the course's terrain covers is asked for, so roads stop where the
// terrain does. Roads inside the course boundary that carts use are cart paths (lib/paths.js), not
// roads, and car park aisles belong to the car parks (lib/parking.js).
import fs from 'node:fs';
import path from 'node:path';
import { overpass, osmApiMap } from './realdata.js';

const CLASSES = ['motorway', 'trunk', 'primary', 'secondary', 'tertiary', 'unclassified', 'residential', 'living_street', 'service',
  'motorway_link', 'trunk_link', 'primary_link', 'secondary_link', 'tertiary_link'];
// Default widths (m) of the paved surface when the map doesn't give a width or lane count
const WIDTH = { motorway: 11, trunk: 9, primary: 8.5, secondary: 7.5, tertiary: 7, unclassified: 6, residential: 6, living_street: 5, service: 4 };

const pointInPoly = (poly, x, z) => {
  let inside = false;
  for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) {
    const [xi, zi] = poly[i], [xj, zj] = poly[j];
    if (zi > z !== zj > z && x < ((xj - xi) * (z - zi)) / (zj - zi) + xi) inside = !inside;
  }
  return inside;
};

const pending = new Map();
// oob: course boundary [[x, z]] (service roads mostly inside it are cart paths)
// Returns { yellowCentre (two-way roads have a yellow centre line, as in the Americas), roads: [{ kind,
// width, lanes, oneway, pts }] }
export function ensureRoads(satDir, geo, grid, oob, log = () => {}) {
  const file = path.join(satDir, 'roads.json');
  if (fs.existsSync(file)) return Promise.resolve(JSON.parse(fs.readFileSync(file, 'utf8')));
  if (pending.has(file)) return pending.get(file);
  const job = (async () => {
    const [a0, a1, a2] = geo.lat, [b0, b1, b2] = geo.lon;
    const det = a1 * b2 - a2 * b1;
    const toCourse = (lat, lon) => { const dl = lat - a0, dn = lon - b0; return [(b2 * dl - a2 * dn) / det, (a1 * dn - b1 * dl) / det]; };
    const maxX = grid.minX + (grid.nx - 1) * grid.cell, maxZ = grid.minZ + (grid.nz - 1) * grid.cell;
    const c = [[grid.minX, grid.minZ], [maxX, grid.minZ], [grid.minX, maxZ], [maxX, maxZ]].map(([x, z]) => [a0 + a1 * x + a2 * z, b0 + b1 * x + b2 * z]);
    const [s, w, n, e] = [Math.min(...c.map((p) => p[0])), Math.min(...c.map((p) => p[1])), Math.max(...c.map((p) => p[0])), Math.max(...c.map((p) => p[1]))];
    log('Downloading roads…');
    let osm;
    try {
      osm = await overpass(`[out:json][timeout:90];way["highway"~"^(${CLASSES.join('|')})$"](${s},${w},${n},${e});out geom;`, log, 3);
    } catch {
      try { osm = await osmApiMap({ minlat: s, minlon: w, maxlat: n, maxlon: e }); }
      catch (err) { log(`Roads skipped (${err.message})`); return { yellowCentre: false, roads: [] }; } // not cached: retried next time
    }
    const out = { yellowCentre: b0 < -30, roads: [] };
    for (const el of osm.elements) {
      const t = el.tags || {};
      if (el.type !== 'way' || !el.geometry || el.geometry.length < 2 || !CLASSES.includes(t.highway)) continue;
      if (t.area === 'yes' || t.tunnel === 'yes' || t.tunnel === 'building_passage' || t.indoor === 'yes') continue;
      if (/^(parking_aisle|drive-through)$/.test(t.service || '')) continue;
      if (t.golf === 'cartpath' || t.golf_cart === 'designated') continue;
      if (t.surface && !/asphalt|paved|concrete|chipseal/.test(t.surface)) continue; // (gravel and dirt roads: not painted as roads)
      const pts = el.geometry.map((p) => toCourse(p.lat, p.lon).map((v) => Math.round(v * 10) / 10));
      if (t.highway === 'service' && oob?.length > 2 && pts.filter(([x, z]) => pointInPoly(oob, x, z)).length >= pts.length * 0.5) continue;
      const kind = t.highway.replace(/_link$/, '');
      const oneway = t.oneway === 'yes' || t.oneway === '1' || t.oneway === '-1' || t.highway === 'motorway' || t.junction === 'roundabout';
      const lanes = parseInt(t.lanes, 10) || null;
      const link = t.highway.endsWith('_link');
      let width = parseFloat(t.width) || (lanes ? lanes * 3.5 + (lanes > 1 ? 0.5 : 1) : link ? 5 : WIDTH[kind] || 6);
      if (t.service === 'driveway') width = Math.min(width, 3.2);
      out.roads.push({ kind: link ? 'link' : kind, width: Math.min(24, Math.max(2.5, width)), lanes, oneway, pts });
    }
    fs.mkdirSync(satDir, { recursive: true });
    fs.writeFileSync(file, JSON.stringify(out));
    log(`${out.roads.length} roads ready.`);
    return out;
  })().finally(() => pending.delete(file));
  pending.set(file, job);
  return job;
}
