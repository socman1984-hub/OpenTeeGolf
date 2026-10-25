// Shared access to public real-world data:
//   - OpenStreetMap (Nominatim search, Overpass features)
//   - Elevation: USGS 3DEP 1 m lidar (US), falling back to global Terrarium tiles (AWS open data)
// plus helpers to turn OSM geometry into course-space polygons and a heightfield grid.

import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';

export const ROOT = path.join(path.dirname(fileURLToPath(import.meta.url)), '..');
// Public map services ask apps to identify themselves; the project homepage (package.json) gives them a contact
const PKG = JSON.parse(fs.readFileSync(path.join(ROOT, 'package.json'), 'utf8'));
export const UA = `OpenTeeGolf/${PKG.version} (open-source golf simulator${PKG.homepage ? `; ${PKG.homepage}` : ''})`;
const CACHE = path.join(ROOT, 'data', 'cache');
const OVERPASS = [
  'https://overpass-api.de/api/interpreter',
  'https://overpass.private.coffee/api/interpreter',
  'https://maps.mail.ru/osm/tools/overpass/api/interpreter',
];
const DEM_3DEP = 'https://elevation.nationalmap.gov/arcgis/rest/services/3DEPElevation/ImageServer/exportImage';
const TERRARIUM = 'https://s3.amazonaws.com/elevation-tiles-prod/terrarium';

export const median = (a) => { const s = [...a].sort((x, y) => x - y); return s[Math.floor(s.length / 2)]; };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function cachePath(name) {
  fs.mkdirSync(CACHE, { recursive: true });
  return path.join(CACHE, name);
}

// ------------------------------------------------------------------ OpenStreetMap

// Inside withFreshOsm(fn), OpenStreetMap is asked again instead of using saved answers (the new
// answers replace the saved ones). Used when updating a course from OpenStreetMap.
let freshDepth = 0;
export async function withFreshOsm(fn) {
  freshDepth++;
  try { return await fn(); } finally { freshDepth--; }
}

export async function overpass(query, log = () => {}, attempts = 6) {
  // Responses are cached so repeated checks/builds don't hit the servers again.
  // (keyed by a hash of the whole query: a key from just its end missed changes in the middle)
  const key = crypto.createHash('sha1').update(query).digest('hex');
  const cacheFile = cachePath(`osm-${key}.json`);
  if (!freshDepth && fs.existsSync(cacheFile)) return JSON.parse(fs.readFileSync(cacheFile, 'utf8'));
  // The public Overpass servers are busy at times; retry and rotate servers.
  let last;
  for (let attempt = 1; attempt <= attempts; attempt++) {
    const server = OVERPASS[(attempt - 1) % OVERPASS.length];
    try {
      const res = await fetch(server, {
        method: 'POST',
        headers: { 'User-Agent': UA, 'Content-Type': 'application/x-www-form-urlencoded' },
        body: 'data=' + encodeURIComponent(query),
        signal: AbortSignal.timeout(100000),
      });
      const text = await res.text();
      if (!res.ok) {
        // Servers answer overload with an HTML page; keep the message short and readable
        const busy = [429, 502, 503, 504].includes(res.status);
        throw new Error(busy ? `OpenStreetMap server busy (${res.status})` : `OpenStreetMap request failed (${res.status})`);
      }
      const json = JSON.parse(text);
      if (json.remark && /error/i.test(json.remark)) throw new Error(`Overpass: ${json.remark}`);
      fs.writeFileSync(cacheFile, text);
      return json;
    } catch (err) {
      last = err;
      if (attempt < attempts) log(`${err.message.split('\n')[0].slice(0, 70)} — retrying (attempt ${attempt + 1} of ${attempts})…`);
      if (attempt < attempts) await sleep(attempt * 3000);
    }
  }
  throw new Error(`${last.message.split('\n')[0]} — the public OpenStreetMap servers are overloaded. Please try again in a few minutes.`);
}

// Typo-tolerant search over OpenStreetMap golf courses (Photon by Komoot)
async function photonSearch(q) {
  const url = `https://photon.komoot.io/api/?limit=15&osm_tag=leisure:golf_course&q=${encodeURIComponent(q)}`;
  const res = await fetch(url, { headers: { 'User-Agent': UA } });
  if (!res.ok) throw new Error(`Photon search failed (${res.status})`);
  const body = await res.json();
  return (body.features || []).map((f) => {
    const p = f.properties;
    return {
      name: p.name || 'Unnamed golf course',
      location: [p.city || p.district, p.county, p.state, p.country].filter(Boolean).filter((v, i, a) => a.indexOf(v) === i).join(', '),
      lat: f.geometry.coordinates[1],
      lon: f.geometry.coordinates[0],
      osmType: { N: 'node', W: 'way', R: 'relation' }[p.osm_type] || p.osm_type,
      osmId: p.osm_id,
    };
  });
}

let lastNominatim = 0;
// Search OpenStreetMap for golf courses by name: Photon (tolerates typos), then Nominatim
// (exact names), then golf courses near whatever place the name matched.
// Where a spot is, as "town, county, state, country" (Photon), for a course picked on the map
export async function placeName(lat, lon) {
  const res = await fetch(`https://photon.komoot.io/reverse?lat=${lat}&lon=${lon}&limit=1`, { headers: { 'User-Agent': UA }, signal: AbortSignal.timeout(15000) });
  if (!res.ok) return '';
  const p = (await res.json()).features?.[0]?.properties || {};
  return [p.city || p.town || p.village || p.district, p.county, p.state, p.country].filter(Boolean).filter((v, i, a) => a.indexOf(v) === i).join(', ');
}

// Every golf course mapped in OpenStreetMap, for the builder's map: [lat, lon, type (n/w/r), id,
// name] each. The whole world in one request is too much for the public servers, so it's
// gathered a tile at a time (15 by 30 degrees; a tile the server can't manage is split in four),
// in the background, the most golf-dense latitudes first. Each tile is kept for a month. The map
// asks for what's new since it last asked (from: how many it has), so courses appear tile by tile.
const WORLD_DAYS = 30;
const world = { rows: [], ids: new Set(), done: false, tiles: 0, tilesDone: 0, running: null };
function worldTiles() {
  const out = [];
  for (let s = -60; s < 75; s += 15) for (let w = -180; w < 180; w += 30) out.push([s, w, s + 15, w + 30]);
  // (the latitudes with most courses first: North America, Europe, Japan, then the rest)
  const rank = (s) => (s >= 30 && s < 60 ? 0 : s >= 15 && s < 30 ? 1 : s >= -45 && s < -15 ? 2 : 3);
  return out.sort((a, b) => rank(a[0]) - rank(b[0]));
}
async function worldTile([s, w, n, e], depth = 0) {
  const file = cachePath(`world-${s}_${w}_${n}_${e}.json`);
  try { if (Date.now() - fs.statSync(file).mtimeMs < WORLD_DAYS * 864e5) return JSON.parse(fs.readFileSync(file, 'utf8')); } catch { /* not saved yet */ }
  const query = `[out:json][timeout:180];nwr["leisure"="golf_course"](${s},${w},${n},${e});out center tags;`;
  let last;
  for (let attempt = 0; attempt < 3; attempt++) {
    try {
      const res = await fetch(OVERPASS[attempt % OVERPASS.length], {
        method: 'POST', headers: { 'User-Agent': UA, 'Content-Type': 'application/x-www-form-urlencoded' },
        body: 'data=' + encodeURIComponent(query), signal: AbortSignal.timeout(200000),
      });
      if (!res.ok) throw new Error(`OpenStreetMap request failed (${res.status})`);
      const json = await res.json();
      if (json.remark && /error|timed out|runtime/i.test(json.remark)) throw new Error(json.remark);
      const rows = [];
      for (const el of json.elements || []) {
        const c = el.center || el;
        if (Number.isFinite(c.lat)) rows.push([Math.round(c.lat * 1e4) / 1e4, Math.round(c.lon * 1e4) / 1e4, el.type[0], el.id, el.tags?.name || '']);
      }
      fs.writeFileSync(file, JSON.stringify(rows));
      return rows;
    } catch (err) { last = err; await sleep(4000 * (attempt + 1)); }
  }
  // (too big for the server: the four quarters, one at a time)
  if (depth < 2) {
    const ms = (s + n) / 2, mw = (w + e) / 2, rows = [];
    for (const q of [[s, w, ms, mw], [s, mw, ms, e], [ms, w, n, mw], [ms, mw, n, e]]) rows.push(...await worldTile(q, depth + 1));
    fs.writeFileSync(file, JSON.stringify(rows));
    return rows;
  }
  throw last;
}
export function worldCourses(from = 0) {
  if (!world.running && !world.done) {
    const tiles = worldTiles();
    world.tiles = tiles.length;
    world.running = (async () => {
      for (const t of tiles) {
        try {
          for (const r of await worldTile(t)) if (!world.ids.has(r[3])) { world.ids.add(r[3]); world.rows.push(r); }
        } catch (err) { console.warn(`[world] tile ${t.join(',')}: ${err.message}`); }
        world.tilesDone++;
      }
      world.done = true;
      world.running = null;
    })();
  }
  return { courses: world.rows.slice(from), total: world.rows.length, done: world.done, progress: world.tiles ? world.tilesDone / world.tiles : 0 };
}

export async function searchCourses(q) {
  const errors = [];
  const attempt = async (fn) => { try { return await fn(); } catch (err) { errors.push(err); return []; } };
  const run = async (text) => {
    const wait = lastNominatim + 1100 - Date.now();
    if (wait > 0) await sleep(wait);
    lastNominatim = Date.now();
    const url = `https://nominatim.openstreetmap.org/search?format=jsonv2&limit=20&q=${encodeURIComponent(text)}`;
    const res = await fetch(url, { headers: { 'User-Agent': UA, 'Accept-Language': 'en' } });
    if (!res.ok) throw new Error(`OpenStreetMap search failed (${res.status})`);
    return res.json();
  };
  const isCourse = (r) => r.type === 'golf_course' || (r.category === 'leisure' && /golf/i.test(r.type));
  const toResult = (r) => ({
    name: r.name || r.display_name.split(',')[0],
    location: r.display_name.split(',').slice(1).map((s) => s.trim()).filter(Boolean).slice(-4).join(', '),
    lat: +r.lat,
    lon: +r.lon,
    osmType: r.osm_type,
    osmId: r.osm_id,
  });
  let out = await attempt(() => photonSearch(q));
  let raw = await attempt(() => run(q));
  let results = raw.filter(isCourse);
  if (!results.length && !out.length && !/golf/i.test(q)) {
    const more = await attempt(() => run(`${q} golf`));
    raw = raw.concat(more);
    results = more.filter(isCourse);
  }
  out = out.concat(results.map(toResult));
  if (!out.length && raw.length) {
    // The name matched something else (a clubhouse, restaurant, town…): look for course areas around it.
    const near = [];
    for (const r of raw.slice(0, 3)) {
      const a = await attempt(() => overpass(`[out:json][timeout:30];nwr["leisure"="golf_course"](around:2500,${r.lat},${r.lon});out tags center;`, () => {}, 2));
      for (const e of a.elements || []) {
        const c = e.center || { lat: e.lat, lon: e.lon };
        near.push({
          name: e.tags?.name || 'Unnamed golf course',
          location: `near ${r.display_name.split(',').slice(0, 3).map((s) => s.trim()).join(', ')}`,
          lat: c.lat, lon: c.lon, osmType: e.type, osmId: e.id,
        });
      }
    }
    // Best name match first
    const words = q.toLowerCase().split(/\s+/).filter((w) => w.length > 2);
    const score = (n) => words.filter((w) => n.toLowerCase().includes(w)).length;
    out = near.sort((a, b) => score(b.name) - score(a.name));
  }
  if (!out.length && errors.length) {
    throw new Error('The OpenStreetMap search services are busy or unreachable right now. Please try again in a minute.');
  }
  const seen = new Set();
  return out.filter((r) => !seen.has(`${r.osmType}${r.osmId}`) && seen.add(`${r.osmType}${r.osmId}`));
}

// ---- Fallback: the main OpenStreetMap API (used when every Overpass server is down)

const OSM_API = 'https://api.openstreetmap.org/api/0.6';
async function osmApi(pathQuery) {
  const cacheFile = cachePath(`osmapi-${Buffer.from(pathQuery).toString('base64url').slice(-60)}.json`);
  if (fs.existsSync(cacheFile)) return JSON.parse(fs.readFileSync(cacheFile, 'utf8'));
  const res = await fetch(`${OSM_API}${pathQuery}`, { headers: { 'User-Agent': UA }, signal: AbortSignal.timeout(120000) });
  const text = await res.text();
  if (res.status === 400 && /too many nodes|limit/i.test(text)) throw new Error('This area has too much map data to download directly from OpenStreetMap');
  if (!res.ok) throw new Error(`OpenStreetMap API request failed (${res.status})`);
  fs.writeFileSync(cacheFile, text);
  return JSON.parse(text);
}

// Turn raw API data (ways reference node ids) into Overpass "out geom" style elements
function assembleGeometry(elements) {
  const nodes = new Map(), ways = new Map();
  for (const e of elements) {
    if (e.type === 'node') nodes.set(e.id, e);
    else if (e.type === 'way') ways.set(e.id, e);
  }
  const geom = (w) => w.nodes.map((id) => nodes.get(id)).filter(Boolean).map((n) => ({ lat: n.lat, lon: n.lon }));
  const bbox = (pts) => pts.length ? {
    minlat: Math.min(...pts.map((p) => p.lat)), maxlat: Math.max(...pts.map((p) => p.lat)),
    minlon: Math.min(...pts.map((p) => p.lon)), maxlon: Math.max(...pts.map((p) => p.lon)),
  } : null;
  const out = [];
  for (const e of elements) {
    if (e.type === 'node' && e.tags) out.push({ type: 'node', id: e.id, lat: e.lat, lon: e.lon, tags: e.tags });
    else if (e.type === 'way') { const g = geom(e); out.push({ type: 'way', id: e.id, tags: e.tags || {}, geometry: g, bounds: bbox(g) }); }
    else if (e.type === 'relation') {
      const members = e.members.map((m) => (m.type === 'way' && ways.has(m.ref)
        ? { type: 'way', ref: m.ref, role: m.role, geometry: geom(ways.get(m.ref)) }
        : { type: m.type, ref: m.ref, role: m.role }));
      out.push({ type: 'relation', id: e.id, tags: e.tags || {}, members, bounds: bbox(members.flatMap((m) => m.geometry || [])) });
    }
  }
  return out;
}

export const osmApiMap = async (b) => ({ elements: assembleGeometry((await osmApi(`/map.json?bbox=${b.minlon},${b.minlat},${b.maxlon},${b.maxlat}`)).elements) });

// The course element itself (boundary + tags), with geometry
export async function fetchCourseElement(osmType, osmId, log) {
  const t = { way: 'way', relation: 'relation', node: 'node', w: 'way', r: 'relation', n: 'node' }[osmType];
  if (!t) throw new Error(`Unknown OSM type ${osmType}`);
  let r;
  try {
    r = await overpass(`[out:json][timeout:60];${t}(${+osmId});out geom;`, log, 2);
  } catch {
    log('Query servers are down — using the main OpenStreetMap API instead…');
    const raw = await osmApi(t === 'node' ? `/node/${+osmId}.json` : `/${t}/${+osmId}/full.json`);
    r = { elements: assembleGeometry(raw.elements).filter((e) => e.type === t && e.id === +osmId) };
  }
  if (!r.elements.length) throw new Error('That course no longer exists in OpenStreetMap');
  let el = r.elements[0];
  if (t === 'node') {
    // A point only: look for a mapped course area around it
    const near = { minlat: el.lat - 0.012, maxlat: el.lat + 0.012, minlon: el.lon - 0.015, maxlon: el.lon + 0.015 };
    const a = await overpass(`[out:json][timeout:60];nwr["leisure"="golf_course"](around:1500,${el.lat},${el.lon});out geom;`, log, 2)
      .catch(() => osmApiMap(near).then((m) => ({ elements: m.elements.filter((e) => e.tags?.leisure === 'golf_course') })));
    const area = a.elements.filter((e) => e.bounds).sort((x, y) => dist2ll(x, el) - dist2ll(y, el))[0];
    if (area) el = { ...area, tags: { ...area.tags, ...el.tags } };
    else el = { ...el, bounds: { minlat: el.lat - 0.008, maxlat: el.lat + 0.008, minlon: el.lon - 0.01, maxlon: el.lon + 0.01 } };
  }
  return el;
}
const dist2ll = (e, p) => {
  const c = e.bounds ? [(e.bounds.minlat + e.bounds.maxlat) / 2, (e.bounds.minlon + e.bounds.maxlon) / 2] : [e.lat, e.lon];
  return (c[0] - p.lat) ** 2 + (c[1] - p.lon) ** 2;
};

// Every golf feature, water body and tree inside the course's bounding box
export async function fetchCourseFeatures(bounds, log, pad = 0.004) {
  const b = { minlat: bounds.minlat - pad, minlon: bounds.minlon - pad, maxlat: bounds.maxlat + pad, maxlon: bounds.maxlon + pad };
  const box = `${b.minlat},${b.minlon},${b.maxlat},${b.maxlon}`;
  try {
    return await overpass(`[out:json][timeout:90];(
  nwr["golf"](${box});
  nwr["natural"~"^(water|wood|tree_row)$"](${box});
  nwr["landuse"~"^(forest|reservoir|basin)$"](${box});
  way["waterway"~"^(river|stream|canal|ditch|drain|riverbank)$"](${box});
  nwr["natural"="tree"](${box});
);out geom;`, log, 3);
  } catch {
    log('Query servers are down — downloading the area from the main OpenStreetMap API instead…');
    return osmApiMap(b);
  }
}

// Local metric frame (x = east, z = north, meters) around a point
export function localFrame(lat0, lon0) {
  const mLat = 111132.92 - 559.82 * Math.cos((2 * lat0 * Math.PI) / 180);
  const mLon = 111412.84 * Math.cos((lat0 * Math.PI) / 180);
  return {
    lat0, lon0, mLat, mLon,
    toEN: (lat, lon) => [(lon - lon0) * mLon, (lat - lat0) * mLat],
    toLL: ([e, n]) => [lat0 + n / mLat, lon0 + e / mLon],
  };
}

export function rings(e) {
  if (e.type === 'way' && e.geometry) return [{ role: 'outer', pts: e.geometry }];
  if (e.type === 'relation') return e.members.filter((m) => m.geometry).map((m) => ({ role: m.role || 'outer', pts: m.geometry }));
  return [];
}

// Join open member ways of multipolygons into closed rings
export function closeRings(parts) {
  const out = [], open = parts.filter((p) => p.pts.length > 1).map((p) => ({ ...p, pts: [...p.pts] }));
  const same = (a, b) => a.lat === b.lat && a.lon === b.lon;
  while (open.length) {
    const cur = open.shift();
    let grew = true;
    while (!same(cur.pts[0], cur.pts[cur.pts.length - 1]) && grew) {
      grew = false;
      for (let i = 0; i < open.length; i++) {
        const o = open[i].pts, end = cur.pts[cur.pts.length - 1];
        if (same(end, o[0])) cur.pts.push(...o.slice(1));
        else if (same(end, o[o.length - 1])) cur.pts.push(...[...o].reverse().slice(1));
        else continue;
        open.splice(i, 1); grew = true; break;
      }
    }
    out.push(cur);
  }
  return out;
}

export function featureKind(t) {
  if (t.golf === 'green') return 'green';
  if (t.golf === 'fairway') return 'fairway';
  if (t.golf === 'bunker') return 'bunker';
  if (t.golf === 'tee') return 'tee';
  if (t.golf === 'water_hazard' || t.golf === 'lateral_water_hazard' || t.natural === 'water') return 'water';
  if (t.waterway === 'riverbank' || t.landuse === 'reservoir' || t.landuse === 'basin') return 'water';
  if (t.golf === 'rough') return 'rough';
  if (t.natural === 'wood' || t.landuse === 'forest') return 'woods';
  return null;
}

// OSM elements -> polygons in course space. toCourse maps [east, north] -> [x, z].
export function convertFeatures(osm, frame, toCourse = (p) => p) {
  const r2 = (v) => Math.round(v * 100) / 100;
  const conv = (pts) => pts.map((p) => toCourse(frame.toEN(p.lat, p.lon)).map(r2));
  const features = { green: [], fairway: [], bunker: [], tee: [], water: [], rough: [], woods: [] };
  const trees = [], treeRows = [], waterLines = [];
  for (const e of osm.elements) {
    const t = e.tags || {};
    // Rivers, streams and ditches mapped as a centre line (in the direction of flow); the builder
    // turns them into water ribbons. Culverts and underground stretches aren't seen.
    if (/^(river|stream|canal|ditch|drain)$/.test(t.waterway || '') && e.geometry?.length >= 2) {
      if (t.tunnel && t.tunnel !== 'no') continue;
      if (t.location === 'underground' || t.covered === 'yes') continue;
      const w = parseFloat(String(t.width || '').replace(',', '.'));
      const width = Number.isFinite(w) && w > 0.5 && w < 200 ? w : { river: 12, canal: 8, stream: 3, ditch: 2, drain: 2 }[t.waterway];
      waterLines.push({ pts: conv(e.geometry), width, kind: t.waterway });
      continue;
    }
    if (t.natural === 'tree' && e.type === 'node') {
      // [x, z] plus, when mapped, [height m, crown diameter m, 'n' needle-leaved / 'b' broadleaved]
      const p = toCourse(frame.toEN(e.lat, e.lon)).map(r2);
      const num = (v) => { const n = parseFloat(String(v ?? '').replace(',', '.')); return Number.isFinite(n) && n > 0 && n < 120 ? r2(n) : null; };
      const h = num(t.height), c = num(t.diameter_crown), leaf = t.leaf_type === 'needleleaved' ? 'n' : t.leaf_type === 'broadleaved' ? 'b' : null;
      trees.push(h || c || leaf ? [...p, h, c, leaf] : p);
      continue;
    }
    if (t.natural === 'tree_row' && e.geometry) { treeRows.push(conv(e.geometry)); continue; }
    const k = featureKind(t);
    if (!k) continue;
    for (const r of closeRings(rings(e))) {
      if (r.pts.length < 4) continue;
      const f = { hole: r.role !== 'outer', pts: conv(r.pts) };
      // Flowing water (its surface follows the banks downhill rather than lying flat)
      if (k === 'water' && (t.waterway === 'riverbank' || /^(river|stream|canal|stream_pool|rapids|ditch|drain)$/.test(t.water || ''))) f.flow = true;
      features[k].push(f);
    }
  }
  return { features, trees, treeRows, waterLines };
}

// ------------------------------------------------------------------ elevation

// Returns { sample(lat, lon) -> meters | NaN, source }
export async function fetchElevation(want, log = () => {}) {
  try {
    const usgs = await fetch3dep(want, log);
    if (usgs) return usgs;
  } catch (err) {
    log(`USGS lidar unavailable (${err.message.slice(0, 80)}); using global elevation instead`);
  }
  return fetchTerrarium(want, log);
}

async function fetch3dep(want, log) {
  // Only worth trying inside the US (incl. Alaska/Hawaii/territories roughly)
  const inUS = want.minLon > -180 && want.maxLon < -60 && want.minLat > 15 && want.maxLat < 72;
  if (!inUS) return null;
  const mLat = 111132.92;
  // The image service uses square pixels in degrees and widens the extent to fit, so ask with
  // square pixels (~1 m north-south) and then use the extent it reports back.
  const px = 1 / mLat;
  const W = Math.min(8000, Math.ceil((want.maxLon - want.minLon) / px));
  const H = Math.min(8000, Math.ceil((want.maxLat - want.minLat) / px));
  log(`Downloading USGS lidar elevation (${W}×${H}, ~1 m)…`);
  const url = `${DEM_3DEP}?bbox=${want.minLon},${want.minLat},${want.maxLon},${want.maxLat}&bboxSR=4326&imageSR=4326` +
    `&size=${W},${H}&format=bsq&pixelType=F32&interpolation=RSP_BilinearInterpolation&f=json`;
  // f=json reports the extent; the identical request with f=image returns the pixels.
  const meta = await (await fetch(url, { headers: { 'User-Agent': UA } })).json();
  if (!meta.extent || meta.width !== W || meta.height !== H) throw new Error('unexpected response');
  const dem = { minLat: meta.extent.ymin, maxLat: meta.extent.ymax, minLon: meta.extent.xmin, maxLon: meta.extent.xmax };
  const res = await fetch(url.replace(/f=json$/, 'f=image'), { headers: { 'User-Agent': UA } });
  if (!res.ok) throw new Error(`HTTP ${res.status}`);
  // (read as it arrives, reporting how much has come: W × H 4-byte heights in all)
  const parts = [];
  let got = 0, shown = 0;
  for await (const chunk of res.body) {
    parts.push(chunk); got += chunk.length;
    if (Date.now() - shown > 400) { shown = Date.now(); log('', { step: 'elevation', at: got, total: W * H * 4, unit: 'bytes' }); }
  }
  const raw = Buffer.concat(parts);
  if (raw.length < W * H * 4) throw new Error('short response');
  const elev = new Float32Array(raw.buffer.slice(raw.byteOffset, raw.byteOffset + W * H * 4));
  let bad = 0;
  for (let i = 0; i < elev.length; i += 97) if (!(Number.isFinite(elev[i]) && elev[i] > -1000)) bad++;
  if (bad / (elev.length / 97) > 0.2) throw new Error('no lidar coverage here');
  const sample = (lat, lon) => {
    // Row 0 is the north edge; pixel centers
    const fx = ((lon - dem.minLon) / (dem.maxLon - dem.minLon)) * W - 0.5;
    const fy = ((dem.maxLat - lat) / (dem.maxLat - dem.minLat)) * H - 0.5;
    const i = Math.max(0, Math.min(W - 2, Math.floor(fx))), j = Math.max(0, Math.min(H - 2, Math.floor(fy)));
    const u = Math.max(0, Math.min(1, fx - i)), v = Math.max(0, Math.min(1, fy - j));
    const k = j * W + i, a = elev[k], b = elev[k + 1], c = elev[k + W], d = elev[k + W + 1];
    if (![a, b, c, d].every((x) => Number.isFinite(x) && x > -1000)) return NaN;
    return a * (1 - u) * (1 - v) + b * u * (1 - v) + c * (1 - u) * v + d * u * v;
  };
  return { sample, source: 'USGS 3D Elevation Program (3DEP) 1 m lidar, public domain', resolution: 1 };
}

// Global fallback: Terrarium-encoded elevation tiles (AWS Open Data, ~4 m at zoom 15).
// Coarser zooms cover wide areas (the distant hills around a course use zoom 10 or 11).
export async function fetchTerrarium(want, log, Z = 15) {
  const tileX = (lon) => ((lon + 180) / 360) * 2 ** Z;
  const tileY = (lat) => {
    const r = (lat * Math.PI) / 180;
    return ((1 - Math.log(Math.tan(r) + 1 / Math.cos(r)) / Math.PI) / 2) * 2 ** Z;
  };
  const x0 = Math.floor(tileX(want.minLon)), x1 = Math.floor(tileX(want.maxLon));
  const y0 = Math.floor(tileY(want.maxLat)), y1 = Math.floor(tileY(want.minLat));
  const count = (x1 - x0 + 1) * (y1 - y0 + 1);
  if (count > 400) throw new Error('Area too large for elevation tiles');
  if (Z === 15) log(`Downloading global elevation tiles (${count} tiles, ~4 m)…`);
  const tiles = new Map();
  for (let ty = y0; ty <= y1; ty++) for (let tx = x0; tx <= x1; tx++) {
    log('', { step: 'elevation', at: tiles.size, total: count, unit: 'tiles' });
    const file = cachePath(`terrarium-${Z}-${tx}-${ty}.png`);
    let buf;
    if (fs.existsSync(file)) buf = fs.readFileSync(file);
    else {
      const res = await fetch(`${TERRARIUM}/${Z}/${tx}/${ty}.png`, { headers: { 'User-Agent': UA } });
      if (!res.ok) throw new Error(`Elevation tile ${tx},${ty}: HTTP ${res.status}`);
      buf = Buffer.from(await res.arrayBuffer());
      fs.writeFileSync(file, buf);
    }
    const png = decodePng(buf);
    const h = new Float32Array(png.width * png.height);
    for (let i = 0; i < h.length; i++) {
      const o = i * png.channels;
      h[i] = png.data[o] * 256 + png.data[o + 1] + png.data[o + 2] / 256 - 32768;
    }
    tiles.set(`${tx},${ty}`, { w: png.width, h });
  }
  const at = (gx, gy) => {
    // gx, gy in global pixel coordinates at zoom Z
    const tx = Math.floor(gx / 256), ty = Math.floor(gy / 256);
    const t = tiles.get(`${tx},${ty}`);
    if (!t) return NaN;
    const px = Math.min(255, Math.max(0, Math.floor(gx - tx * 256))), py = Math.min(255, Math.max(0, Math.floor(gy - ty * 256)));
    return t.h[py * t.w + px];
  };
  const sample = (lat, lon) => {
    const gx = tileX(lon) * 256 - 0.5, gy = tileY(lat) * 256 - 0.5;
    const i = Math.floor(gx), j = Math.floor(gy), u = gx - i, v = gy - j;
    const a = at(i, j), b = at(i + 1, j), c = at(i, j + 1), d = at(i + 1, j + 1);
    return a * (1 - u) * (1 - v) + b * u * (1 - v) + c * (1 - u) * v + d * u * v;
  };
  return { sample, source: 'Terrarium elevation tiles (AWS Open Data: SRTM, 3DEP and others)', resolution: 4 };
}

// Minimal PNG decoder for 8-bit RGB/RGBA, non-interlaced images
export function decodePng(buf) {
  let pos = 8, width = 0, height = 0, colorType = 0, bitDepth = 8;
  const idat = [];
  while (pos < buf.length) {
    const len = buf.readUInt32BE(pos), type = buf.toString('ascii', pos + 4, pos + 8);
    const data = buf.subarray(pos + 8, pos + 8 + len);
    if (type === 'IHDR') { width = data.readUInt32BE(0); height = data.readUInt32BE(4); bitDepth = data[8]; colorType = data[9]; }
    else if (type === 'IDAT') idat.push(data);
    else if (type === 'IEND') break;
    pos += 12 + len;
  }
  if (bitDepth !== 8 || (colorType !== 2 && colorType !== 6)) throw new Error('Unsupported PNG format');
  const ch = colorType === 6 ? 4 : 3;
  const raw = zlib.inflateSync(Buffer.concat(idat));
  const stride = width * ch;
  const out = Buffer.alloc(stride * height);
  for (let y = 0; y < height; y++) {
    const f = raw[y * (stride + 1)];
    const src = y * (stride + 1) + 1, dst = y * stride, prev = dst - stride;
    for (let x = 0; x < stride; x++) {
      const r = raw[src + x];
      const a = x >= ch ? out[dst + x - ch] : 0;
      const b = y > 0 ? out[prev + x] : 0;
      const c = x >= ch && y > 0 ? out[prev + x - ch] : 0;
      let v;
      switch (f) {
        case 0: v = r; break;
        case 1: v = r + a; break;
        case 2: v = r + b; break;
        case 3: v = r + ((a + b) >> 1); break;
        case 4: { const p = a + b - c, pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c); v = r + (pa <= pb && pa <= pc ? a : pb <= pc ? b : c); break; }
        default: throw new Error('Bad PNG filter');
      }
      out[dst + x] = v & 255;
    }
  }
  return { width, height, channels: ch, data: out };
}

// Sample a height function onto a regular grid; gaps are filled from neighbours.
export function buildGrid({ minX, minZ, maxX, maxZ, cell }, heightAt) {
  const nx = Math.round((maxX - minX) / cell) + 1, nz = Math.round((maxZ - minZ) / cell) + 1;
  const heights = new Float32Array(nx * nz);
  let missing = 0;
  for (let j = 0; j < nz; j++) for (let i = 0; i < nx; i++) {
    const d = heightAt(minX + i * cell, minZ + j * cell);
    if (Number.isFinite(d)) heights[j * nx + i] = d; else { heights[j * nx + i] = NaN; missing++; }
  }
  for (let pass = 0; pass < 200 && missing; pass++) {
    for (let k = 0; k < heights.length; k++) {
      if (!Number.isNaN(heights[k])) continue;
      const nb = [k - 1, k + 1, k - nx, k + nx].filter((q) => q >= 0 && q < heights.length && !Number.isNaN(heights[q]));
      if (nb.length) { heights[k] = nb.reduce((s, q) => s + heights[q], 0) / nb.length; missing--; }
    }
  }
  if (missing) heights.fill(0); // nothing usable at all
  return { minX, minZ, cell, nx, nz, heights: Buffer.from(heights.buffer).toString('base64') };
}
