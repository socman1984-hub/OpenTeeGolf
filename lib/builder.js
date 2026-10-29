// Course builder: makes a playable course (course JSON + real terrain) for any golf course
// mapped in OpenStreetMap.
//
// Course space is a local metric frame: x = east, z = north (meters), y = height above the lowest
// point of the area. That matches the convention the rest of the app uses.

import fs from 'node:fs';
import path from 'node:path';
import {
  ROOT, fetchCourseElement, fetchCourseFeatures, localFrame, convertFeatures, closeRings, rings,
  fetchElevation, buildGrid,
  courseName as osmCourseName,
} from './realdata.js';
import { geoFromFunction } from './imagery.js';
import { findSea } from './seawater.js';
import { makeCourseData } from '../public/js/coursefile.js';

export const BUILT_DIR = path.join(ROOT, 'courses');
const CELL = 2;
const MARGIN = 120;

// ------------------------------------------------------------------ geometry helpers

const centroid = (poly) => {
  let a = 0, cx = 0, cz = 0;
  for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) {
    const f = poly[j][0] * poly[i][1] - poly[i][0] * poly[j][1];
    a += f; cx += (poly[j][0] + poly[i][0]) * f; cz += (poly[j][1] + poly[i][1]) * f;
  }
  if (Math.abs(a) < 1e-9) return [poly.reduce((s, p) => s + p[0], 0) / poly.length, poly.reduce((s, p) => s + p[1], 0) / poly.length];
  return [cx / (3 * a), cz / (3 * a)];
};
const area = (poly) => {
  let a = 0;
  for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) a += poly[j][0] * poly[i][1] - poly[i][0] * poly[j][1];
  return Math.abs(a / 2);
};
// A centre line (in flow order) widened into water polygons, in pieces of about 150 m so each
// stays a simple shape. Each piece keeps its stretch of centre line. Very narrow ditches are drawn
// at least 2.5 m wide so they read on the terrain.
export function riverRibbon(line, width) {
  const hw = Math.max(2.5, width) / 2;
  // Even spacing (about 4 m) so the outline follows bends smoothly
  const pts = [line[0]];
  for (let i = 1; i < line.length; i++) {
    const [ax, az] = line[i - 1], [bx, bz] = line[i], n = Math.max(1, Math.round(Math.hypot(bx - ax, bz - az) / 4));
    for (let k = 1; k <= n; k++) pts.push([ax + ((bx - ax) * k) / n, az + ((bz - az) * k) / n]);
  }
  const r2 = (v) => Math.round(v * 100) / 100;
  const out = [];
  const piece = Math.max(2, Math.round(150 / 4));
  for (let s = 0; s < pts.length - 1; s += piece) {
    const seg = pts.slice(s, Math.min(pts.length, s + piece + 1));
    const left = [], right = [];
    for (let i = 0; i < seg.length; i++) {
      const a = seg[Math.max(0, i - 1)], b = seg[Math.min(seg.length - 1, i + 1)];
      // (the neighbours outside the piece keep the joins between pieces lined up)
      const pa = i === 0 && s > 0 ? pts[s - 1] : a, pb = i === seg.length - 1 && s + i + 1 < pts.length ? pts[s + i + 1] : b;
      let dx = pb[0] - pa[0], dz = pb[1] - pa[1];
      const l = Math.hypot(dx, dz) || 1;
      dx /= l; dz /= l;
      left.push([r2(seg[i][0] - dz * hw), r2(seg[i][1] + dx * hw)]);
      right.push([r2(seg[i][0] + dz * hw), r2(seg[i][1] - dx * hw)]);
    }
    out.push({ hole: false, pts: [...left, ...right.reverse()], flow: true, line: seg.map(([x, z]) => [r2(x), r2(z)]) });
  }
  return out;
}

export function pointInPoly(poly, x, z) {
  let inside = false;
  for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) {
    const [xi, zi] = poly[i], [xj, zj] = poly[j];
    if (zi > z !== zj > z && x < ((xj - xi) * (z - zi)) / (zj - zi) + xi) inside = !inside;
  }
  return inside;
}
const dist = (a, b) => Math.hypot(a[0] - b[0], a[1] - b[1]);
function convexHull(points) {
  const p = [...points].sort((a, b) => a[0] - b[0] || a[1] - b[1]);
  const cross = (o, a, b) => (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0]);
  const lower = [], upper = [];
  for (const q of p) { while (lower.length >= 2 && cross(lower[lower.length - 2], lower[lower.length - 1], q) <= 0) lower.pop(); lower.push(q); }
  for (const q of p.reverse()) { while (upper.length >= 2 && cross(upper[upper.length - 2], upper[upper.length - 1], q) <= 0) upper.pop(); upper.push(q); }
  return lower.slice(0, -1).concat(upper.slice(0, -1));
}
// Push a polygon outward from its centroid (cheap buffer)
const grow = (poly, m) => { const c = centroid(poly); return poly.map(([x, z]) => { const d = Math.hypot(x - c[0], z - c[1]) || 1; return [x + ((x - c[0]) / d) * m, z + ((z - c[1]) / d) * m]; }); };

export const teeNamesFor = (n) => [
  [], ['White'], ['Blue', 'Red'], ['Blue', 'White', 'Red'], ['Black', 'Blue', 'White', 'Red'],
  ['Black', 'Blue', 'White', 'Green', 'Red'],
][Math.min(n, 5)];

export const slug = (s) => s.normalize('NFKD').replace(/[^\w\s-]/g, '').trim().replace(/\s+/g, '_').slice(0, 60) || 'course';

// ------------------------------------------------------------------ OSM analysis (shared by check + build)

async function loadOsm(osmType, osmId, log) {
  log('Finding the course boundary…', { step: 'boundary' });
  const el = await fetchCourseElement(osmType, osmId, log);
  const bounds = el.bounds;
  log('Downloading greens, fairways, bunkers, tees and water…', { step: 'features' });
  const osm = await fetchCourseFeatures(bounds, log);
  const frame = localFrame((bounds.minlat + bounds.maxlat) / 2, (bounds.minlon + bounds.maxlon) / 2);
  // Course boundary polygon(s) in course space
  let boundary = null;
  if (el.type === 'way' && el.geometry) boundary = el.geometry.map((p) => frame.toEN(p.lat, p.lon));
  else if (el.type === 'relation') {
    const outers = closeRings(rings(el).filter((r) => r.role !== 'inner')).map((r) => r.pts.map((p) => frame.toEN(p.lat, p.lon)));
    boundary = outers.sort((a, b) => area(b) - area(a))[0] || null;
  }
  const inCourse = (p) => !boundary || pointInPoly(grow(boundary, 30), p[0], p[1]);
  const holeLines = osm.elements
    .filter((e) => e.tags?.golf === 'hole' && e.geometry?.length >= 2)
    .map((e) => ({
      ref: e.tags.ref, par: +e.tags.par || null, handicap: +e.tags.handicap || null,
      group: e.tags['golf:course:name'] || e.tags['golf:course'] || '',
      pts: e.geometry.map((p) => frame.toEN(p.lat, p.lon)),
      name: e.tags.name || '',
    }))
    .filter((h) => inCourse(h.pts[h.pts.length - 1]));
  const { features, trees, treeRows, waterLines } = convertFeatures(osm, frame);
  // Keep only features belonging to this course (the bbox can include neighbours)
  for (const k of Object.keys(features)) features[k] = features[k].filter((f) => inCourse(centroid(f.pts)));
  // Rivers and streams mapped as lines: the stretches through (and just around) the course, minus
  // any already mapped as water areas, become ribbons of water as wide as the river
  const nearCourse = boundary ? grow(boundary, 120) : null;
  const near = (p) => !nearCourse || pointInPoly(nearCourse, p[0], p[1]);
  const areas = features.water.filter((f) => !f.hole);
  const inArea = (p) => areas.some((f) => pointInPoly(f.pts, p[0], p[1]));
  for (const line of waterLines) {
    let run = [];
    const flush = () => { if (run.length >= 2) features.water.push(...riverRibbon(run, line.width)); run = []; };
    for (const p of line.pts) {
      if (near(p) && !inArea(p)) run.push(p);
      else { if (run.length) run.push(p); flush(); }
    }
    flush();
  }
  return { el, bounds, osm, frame, boundary, holeLines, features, trees, treeRows };
}

function holeGroups(holeLines) {
  const groups = {};
  for (const h of holeLines) (groups[h.group] ??= []).push(h);
  return Object.entries(groups).map(([name, hs]) => ({
    name,
    holes: hs.length,
    refs: hs.map((h) => h.ref).filter(Boolean),
    par: hs.reduce((s, h) => s + (h.par || 0), 0),
  })).sort((a, b) => b.holes - a.holes);
}

// What's mapped for this course, and how playable a build would be.
export async function checkCourse(osmType, osmId, log = () => {}) {
  const d = await loadOsm(osmType, osmId, log);
  const f = d.features;
  const groups = holeGroups(d.holeLines);
  const count = (k) => f[k].filter((x) => !x.hole).length;
  const summary = {
    name: osmCourseName(d.el.tags) || 'Unnamed course',
    osmType: d.el.type, osmId: d.el.id,
    hasBoundary: !!d.boundary,
    holes: d.holeLines.length,
    groups,
    greens: count('green'), fairways: count('fairway'), bunkers: count('bunker'), tees: count('tee'), water: count('water'),
    trees: d.trees.length,
  };
  const notes = [];
  let quality;
  if (summary.holes >= 9 && summary.greens >= 9) { quality = 'good'; notes.push('Hole routing and greens are mapped — this should build well.'); }
  else if (summary.greens >= 9) { quality = 'partial'; notes.push('Greens are mapped but hole lines are missing; holes will be guessed and should be checked in the editor.'); }
  else if (summary.holes >= 9) { quality = 'partial'; notes.push('Hole lines are mapped but few greens; generated greens will be used where missing.'); }
  else { quality = 'poor'; notes.push('Very little of this course is mapped. You can still build it and lay out holes in the editor.'); }
  if (!summary.fairways) notes.push('No fairways mapped — they can be drawn in the editor.');
  if (!summary.bunkers) notes.push('No bunkers mapped.');
  if (!summary.hasBoundary) notes.push('No course boundary mapped; out of bounds will be generated around the features.');
  return { ...summary, quality, notes };
}

// ------------------------------------------------------------------ build

// id: write over this course folder (updating a course) instead of one named after the course
export async function buildCourse({ osmType, osmId, group = null, name = null, location = '', id: keepId = null }, log = () => {}) {
  log('Downloading course data from OpenStreetMap…');
  const d = await loadOsm(osmType, osmId, log);
  const { frame, features } = d;
  const courseName = name || osmCourseName(d.el.tags) || 'New course';
  const greens = features.green.filter((g) => !g.hole);
  const teeBoxes = features.tee.filter((g) => !g.hole).map((t) => ({ c: centroid(t.pts), poly: t.pts, used: false }));

  // ---- holes
  let lines = d.holeLines;
  const groups = holeGroups(lines);
  const chosen = group ?? groups[0]?.name ?? '';
  lines = lines.filter((h) => h.group === chosen);
  const holes = [];
  let needsReview = false;
  if (lines.length) {
    log(`Laying out ${lines.length} holes from the mapped routing${chosen ? ` (${chosen})` : ''}…`, { step: 'holes' });
    const seen = new Set();
    lines.sort((a, b) => (+a.ref || 999) - (+b.ref || 999));
    lines.forEach((h, i) => {
      let num = +h.ref || i + 1;
      while (seen.has(num)) num++;
      seen.add(num);
      holes.push({ number: num, par: h.par, index: h.handicap, line: h.pts });
    });
  } else if (greens.length) {
    // No routing mapped: one hole per green, chained by proximity, tees guessed.
    needsReview = true;
    log(`No hole routing mapped — guessing ${Math.min(18, greens.length)} holes from the greens…`, { step: 'holes' });
    const left = greens.map((g) => centroid(g.pts));
    let cur = left.shift();
    const order = [cur];
    while (left.length && order.length < 18) {
      left.sort((a, b) => dist(a, cur) - dist(b, cur));
      cur = left.shift();
      order.push(cur);
    }
    const fairways = features.fairway.filter((f) => !f.hole);
    const claimed = new Set();
    order.forEach((g, i) => {
      // Follow the fairway that runs into this green back to the tee boxes behind it.
      const fw = fairways
        .filter((f) => !claimed.has(f) && Math.min(...f.pts.map((p) => dist(p, g))) < 40)
        .sort((a, b) => area(b.pts) - area(a.pts))[0];
      let start = null, aim = null;
      if (fw) {
        claimed.add(fw);
        const far = fw.pts.reduce((m, p) => (dist(p, g) > dist(m, g) ? p : m), fw.pts[0]);
        const fd = dist(far, g), dir = [(far[0] - g[0]) / fd, (far[1] - g[1]) / fd];
        const behind = teeBoxes.filter((t) => {
          if (t.used) return false;
          const vx = t.c[0] - g[0], vz = t.c[1] - g[1];
          const along = vx * dir[0] + vz * dir[1], across = Math.abs(vx * dir[1] - vz * dir[0]);
          return along > fd * 0.8 && along < fd + 260 && across < 50;
        }).sort((a, b) => dist(a.c, far) - dist(b.c, far));
        start = behind[0]?.c || [far[0] + dir[0] * 40, far[1] + dir[1] * 40];
        // Dogleg: aim at the fairway's middle when it bends away from the straight line
        const mid = centroid(fw.pts);
        const ax = start[0] - g[0], az = start[1] - g[1], al = Math.hypot(ax, az) || 1;
        if (Math.abs((mid[0] - g[0]) * (az / al) - (mid[1] - g[1]) * (ax / al)) > 20) aim = mid;
      } else {
        // No fairway: most likely a par 3 — nearest free tee box at par-3 range
        const tee = teeBoxes.filter((t) => !t.used && dist(t.c, g) > 80 && dist(t.c, g) < 230).sort((a, b) => dist(a.c, g) - dist(b.c, g))[0];
        start = tee ? tee.c : [g[0], g[1] - 150];
      }
      holes.push({ number: i + 1, par: null, index: null, line: aim ? [start, aim, g] : [start, g] });
    });
  }
  if (!holes.length) {
    needsReview = true;
    log('Nothing to lay out holes from — creating a single placeholder hole to edit.', { step: 'holes' });
    holes.push({ number: 1, par: 4, index: 1, line: [[0, -150], [0, 150]] });
  }

  // ---- per hole: green, pins, tees, aim points
  const holeLen = (pts) => pts.slice(1).reduce((s, p, i) => s + dist(p, pts[i]), 0);
  for (const h of holes) {
    const end = h.line[h.line.length - 1];
    // Green: the mapped green containing (or nearest to) the end of the hole line
    let green = greens.find((g) => pointInPoly(g.pts, end[0], end[1]))
      || greens.map((g) => [g, dist(centroid(g.pts), end)]).filter(([, dd]) => dd < 45).sort((a, b) => a[1] - b[1])[0]?.[0];
    if (!green) {
      // Make a round green where the line ends
      needsReview = true;
      const pts = [];
      for (let k = 0; k < 24; k++) { const a = (k / 24) * Math.PI * 2; pts.push([end[0] + Math.cos(a) * 14, end[1] + Math.sin(a) * 12]); }
      green = { pts, hole: false, generated: true };
      features.green.push(green);
      greens.push(green);
    }
    const gc = pointInPoly(green.pts, end[0], end[1]) && !green.generated ? centroid(green.pts) : centroid(green.pts);
    h.greenCenter = gc;
    const before = h.line.length > 2 ? h.line[h.line.length - 2] : h.line[0];
    const ax = gc[0] - before[0], az = gc[1] - before[1], al = Math.hypot(ax, az) || 1;
    const fwd = [ax / al, az / al], right = [fwd[1], -fwd[0]];
    const r = Math.sqrt(area(green.pts) / Math.PI);
    // Thursday center, Friday front-left, Saturday back-right, Sunday back-left
    const offsets = { Thursday: [0, 0], Friday: [-0.45, -0.35], Saturday: [0.45, 0.35], Sunday: [0.4, -0.4] };
    h.pins = {};
    for (const [day, [u, w]] of Object.entries(offsets)) {
      let s = 1, p;
      do {
        p = [gc[0] + (fwd[0] * u + right[0] * w) * r * s, gc[1] + (fwd[1] * u + right[1] * w) * r * s];
        s *= 0.7;
      } while (!pointInPoly(green.pts, p[0], p[1]) && s > 0.05);
      h.pins[day] = p;
    }
    // Aim points: the line's bends (at most two, the sharpest)
    const mids = h.line.slice(1, -1).map((p, i) => {
      const a = h.line[i], b = h.line[i + 2];
      const turn = Math.abs(Math.atan2(p[1] - a[1], p[0] - a[0]) - Math.atan2(b[1] - p[1], b[0] - p[0]));
      return { p, turn: Math.min(turn, Math.PI * 2 - turn), i };
    }).sort((a, b) => b.turn - a.turn).slice(0, 2).sort((a, b) => a.i - b.i);
    h.aims = mids.map((m) => m.p);
    // Tees: mapped tee boxes near the start of this hole's line
    const start = h.line[0], next = h.line[1];
    h.teeSpots = [];
    const sl = dist(start, next) || 1, dir = [(next[0] - start[0]) / sl, (next[1] - start[1]) / sl];
    for (const t of teeBoxes) {
      if (t.used) continue;
      // Position along / across the first leg of the hole (tee boxes can sit behind where the line starts)
      const vx = t.c[0] - start[0], vz = t.c[1] - start[1];
      const along = vx * dir[0] + vz * dir[1], across = Math.abs(vx * dir[1] - vz * dir[0]);
      if (across < 25 && along > -90 && along < Math.min(220, sl * 0.55)) h.teeSpots.push(t);
    }
    h.teeSpots.forEach((t) => (t.used = true));
    const spots = h.teeSpots.map((t) => t.c);
    if (!spots.some((c) => dist(c, start) < 15)) spots.push(start);
    // Furthest from the green first; merge spots closer than 8 m
    spots.sort((a, b) => dist(b, gc) - dist(a, gc));
    const merged = [];
    for (const s2 of spots) if (!merged.some((m) => dist(m, s2) < 8)) merged.push(s2);
    const names = teeNamesFor(Math.min(5, merged.length));
    const pick = merged.length <= 5 ? merged : [0, 1, 2, 3, 4].map((k) => merged[Math.round((k * (merged.length - 1)) / 4)]);
    h.tees = Object.fromEntries(pick.map((p, k) => [names[k], p]));
    const len = holeLen([h.tees[names[0]], ...h.aims, gc]);
    if (!h.par) { h.par = len < 230 ? 3 : len < 430 ? 4 : 5; needsReview = true; }
  }
  holes.sort((a, b) => a.number - b.number);
  // Handicap index: mapped, or by length
  if (holes.some((h) => !h.index)) {
    const byLen = [...holes].sort((a, b) => holeLen([Object.values(b.tees)[0], b.greenCenter]) - holeLen([Object.values(a.tees)[0], a.greenCenter]));
    byLen.forEach((h, i) => { if (!h.index) h.index = i + 1; });
  }

  // ---- out of bounds and penalty areas
  let oob = d.boundary;
  if (!oob) {
    const pts = [...holes.flatMap((h) => [...Object.values(h.tees), h.greenCenter, ...h.aims]), ...Object.values(features).flat().flatMap((f) => f.pts)];
    oob = grow(convexHull(pts), 40);
  }
  const hazards = features.water.filter((w) => !w.hole && area(w.pts) > 60).map((w) => w.pts);

  // ---- elevation
  const xs = [...oob.map((p) => p[0]), ...holes.flatMap((h) => [h.greenCenter[0], ...Object.values(h.tees).map((t) => t[0])])];
  const zs = [...oob.map((p) => p[1]), ...holes.flatMap((h) => [h.greenCenter[1], ...Object.values(h.tees).map((t) => t[1])])];
  const b = {
    minX: Math.floor((Math.min(...xs) - MARGIN) / CELL) * CELL, maxX: Math.ceil((Math.max(...xs) + MARGIN) / CELL) * CELL,
    minZ: Math.floor((Math.min(...zs) - MARGIN) / CELL) * CELL, maxZ: Math.ceil((Math.max(...zs) + MARGIN) / CELL) * CELL,
    cell: CELL,
  };
  const [la0, lo0] = frame.toLL([b.minX, b.minZ]), [la1, lo1] = frame.toLL([b.maxX, b.maxZ]);
  log('', { step: 'elevation' });
  const elevation = await fetchElevation({ minLat: la0, maxLat: la1, minLon: lo0, maxLon: lo1 }, log);
  const raw = (x, z) => { const [lat, lon] = frame.toLL([x, z]); return elevation.sample(lat, lon); };
  // Heights relative to the lowest point keep the numbers small; altitude kept separately
  let base = Infinity;
  for (let z = b.minZ; z <= b.maxZ; z += 20) for (let x = b.minX; x <= b.maxX; x += 20) { const v = raw(x, z); if (Number.isFinite(v)) base = Math.min(base, v); }
  if (!Number.isFinite(base)) base = 0;
  base = Math.floor(base);
  log('Building terrain…', { step: 'terrain' });
  const grid = buildGrid(b, (x, z) => raw(x, z) - base);
  const gh = Buffer.from(grid.heights, 'base64');
  // The sea beside the course, when the map leaves it out (OpenStreetMap has only the coastline):
  // from the elevation data and the aerial photo. It becomes water, and a penalty area.
  try {
    const hg = new Float32Array(gh.buffer, gh.byteOffset, grid.nx * grid.nz);
    const seaHeight = (x, z) => {
      const i = Math.round((x - b.minX) / CELL), j = Math.round((z - b.minZ) / CELL);
      return i < 0 || j < 0 || i >= grid.nx || j >= grid.nz ? Infinity : hg[j * grid.nx + i] + base;
    };
    const mapped = ['green', 'fairway', 'tee', 'bunker', 'water'].flatMap((k) => (features[k] || []).filter((f) => !f.hole));
    const keepOut = (x, z) => mapped.some((f) => pointInPoly(f.pts, x, z));
    const sea = await findSea({ grid: { ...grid, minX: b.minX, minZ: b.minZ }, heightAt: seaHeight, toLL: (p) => frame.toLL(p), keepOut, log });
    for (const w of sea) {
      features.water.push({ pts: w.pts, sea: true, ...(w.hole ? { hole: true } : {}) });
      if (!w.hole) hazards.push(w.pts);
    }
  } catch (err) { log(`Looking for the sea skipped (${err.message})`); }
  const hf = new Float32Array(gh.buffer, gh.byteOffset, grid.nx * grid.nz);
  const heightAt = (x, z) => {
    const i = Math.max(0, Math.min(grid.nx - 1, Math.round((x - b.minX) / CELL))), j = Math.max(0, Math.min(grid.nz - 1, Math.round((z - b.minZ) / CELL)));
    return Math.round(hf[j * grid.nx + i] * 1000) / 1000;
  };
  const P = ([x, z]) => ({ x: Math.round(x * 100) / 100, y: heightAt(x, z), z: Math.round(z * 100) / 100 });

  // ---- course file
  const [clat, clon] = frame.toLL([(b.minX + b.maxX) / 2, (b.minZ + b.maxZ) / 2]);
  const courseData = makeCourseData({
    name: courseName, location, lat: clat, lon: clon, altitudeM: base,
    holes: holes.map((h) => ({
      number: h.number, par: h.par, index: h.index,
      tees: Object.fromEntries(Object.entries(h.tees).map(([k, p]) => [k, P(p)])),
      aims: h.aims.map(P), greenCenter: P(h.greenCenter),
      pins: Object.fromEntries(Object.entries(h.pins).map(([k, p]) => [k, P(p)])),
    })),
    hazards: hazards.map((poly) => ({ coords: poly.map(P) })),
    oob: oob.map(P),
    builder: {
      osmType: d.el.type, osmId: d.el.id, group: chosen, needsReview, built: new Date().toISOString(),
      frame: { lat0: frame.lat0, lon0: frame.lon0 },
    },
  });

  const real = {
    version: 1,
    generated: new Date().toISOString(),
    source: {
      osm: `© OpenStreetMap contributors (ODbL) — ${d.el.tags?.name || ''} ${d.el.type}/${d.el.id}`,
      elevation: elevation.source,
    },
    geo: geoFromFunction((x, z) => frame.toLL([x, z])),
    grid,
    features: Object.fromEntries(Object.entries(features).map(([k, list]) => [k, list.map(({ hole, pts, flow, sea }) => (flow ? { hole, pts, flow } : sea ? { hole, pts, sea } : { hole, pts }))])),
    trees: d.trees,
    treeRows: d.treeRows,
  };

  // ---- write
  const id = keepId || slug(courseName);
  const dir = path.join(BUILT_DIR, id);
  fs.mkdirSync(dir, { recursive: true });
  const dataFile = path.join(dir, `${id}.course.json`);
  fs.writeFileSync(dataFile, JSON.stringify(courseData, null, 1));
  fs.writeFileSync(path.join(dir, `${id}.real.json`), JSON.stringify(real));
  log(`Built ${courseName}: ${holes.length} holes, par ${holes.reduce((s, h) => s + h.par, 0)}${needsReview ? ' — some parts were guessed, review them in the editor' : ''}.`);
  return { dataFile, holes: holes.length, needsReview };
}
