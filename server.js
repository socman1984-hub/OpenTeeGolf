// OpenTeeGolf server
//  - Serves the web app (http://localhost:8080)
//  - Lists the courses made with the builder (courses/<name>/<name>.course.json)
//  - Course builder: search OpenStreetMap, check a course's mapping, build it; course editor saves
//  - Listens for launch monitor shots using the OpenConnect protocol (TCP 921),
//    which is what the FlightScope FS Golf app uses to send Mevo+ shots to a simulator.
//  - Pushes shots to the browser over Server-Sent Events.
//
// Usage: node server.js
//   env: PORT (8080), OPENCONNECT_PORT (921)

import http from 'node:http';
import net from 'node:net';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { normalizeSpin } from './public/js/physics.js';
import { searchCourses } from './lib/realdata.js';
import { checkCourse, buildCourse, BUILT_DIR } from './lib/builder.js';
import { geoFor, ensureSatellite } from './lib/imagery.js';
import { ensureBuildings } from './lib/buildings.js';
import { ensurePaths } from './lib/paths.js';
import { ensurePhoto, photoMeta, savePhoto } from './lib/coursephoto.js';

const ROOT = path.dirname(fileURLToPath(import.meta.url));
const HTTP_PORT = +(process.env.PORT || 8080);
const OC_PORT = +(process.env.OPENCONNECT_PORT || 921);

// ---------------------------------------------------------------- courses

const COURSE_EXT = '.course.json';

// Each course is a folder: courses/<id>/<id>.course.json (+ .real.json, .sat/, photo)
const summaries = new Map(); // file -> { mtime, summary }
function findCourses() {
  let dirs;
  try { dirs = fs.readdirSync(BUILT_DIR, { withFileTypes: true }).filter((d) => d.isDirectory()); } catch { return []; }
  const out = [];
  for (const d of dirs) {
    const full = courseFile(d.name);
    if (!full) continue;
    try {
      const st = fs.statSync(full);
      let c = summaries.get(full);
      if (!c || c.mtime !== st.mtimeMs) {
        const cd = readCourseData(full);
        c = { mtime: st.mtimeMs, summary: {
          id: d.name,
          name: cd.CourseName || d.name,
          location: cd.Location || '',
          designer: cd.Designer || '',
          holes: (cd.Holes || []).length,
          par: (cd.Holes || []).reduce((s, h) => s + (h.Par || 0), 0),
          lat: +cd.CourseLat || 0, lon: +cd.CourseLong || 0,
          built: cd.GolfSimBuilder?.built || new Date(st.birthtimeMs).toISOString(),
          needsReview: !!cd.GolfSimBuilder?.needsReview,
          osmType: cd.GolfSimBuilder?.osmType || null,
          osmId: cd.GolfSimBuilder?.osmId ?? null,
        } };
        summaries.set(full, c);
      }
      const photo = photoMeta(path.dirname(full));
      out.push({
        ...c.summary,
        editable: true,
        hasReal: fs.existsSync(realFile(full)),
        hasAerial: fs.existsSync(path.join(satDir(full), 'manifest.json')),
        photo: photo ? (photo.file ? 'yes' : 'none') : 'unknown',
        photoCredit: photo?.credit || '',
      });
    } catch (err) {
      console.warn(`Skipping ${full}: ${err.message}`);
    }
  }
  return out.sort((a, b) => a.name.localeCompare(b.name));
}

// Real-world terrain/features and imagery, saved next to the course file
const realFile = (dataFile) => dataFile.slice(0, -COURSE_EXT.length) + '.real.json';
const satDir = (dataFile) => dataFile.slice(0, -COURSE_EXT.length) + '.sat';

function readCourseData(file) {
  return JSON.parse(fs.readFileSync(file, 'utf8').replace(/^﻿/, ''));
}

// Course id = its folder name
function courseFile(id) {
  if (!/^[\w-]+$/.test(id || '')) return null;
  const file = path.join(BUILT_DIR, id, id + COURSE_EXT);
  return fs.existsSync(file) ? file : null;
}
const isEditable = (file) => !!file;

function photoInfo(file) {
  const cd = readCourseData(file);
  return { name: cd.CourseName, lat: +cd.CourseLat, lon: +cd.CourseLong, osmType: cd.GolfSimBuilder?.osmType, osmId: cd.GolfSimBuilder?.osmId };
}

// ---------------------------------------------------------------- browser events (SSE)

const sseClients = new Set();
function broadcast(type, data) {
  const msg = `event: ${type}\ndata: ${JSON.stringify(data)}\n\n`;
  for (const res of sseClients) res.write(msg);
}

const lmState = { connected: false, ready: false, ballDetected: false, device: null, address: null, lastShotAt: null };
const player = { Handed: 'RH', Club: 'DR', DistanceToTarget: 0 };

function setLm(patch) {
  Object.assign(lmState, patch);
  broadcast('lm', lmState);
}

// ---------------------------------------------------------------- OpenConnect (TCP 921)

const lmSockets = new Set();
let lastShot = { key: null, at: 0 };

function sendJson(sock, obj) {
  try { sock.write(JSON.stringify(obj)); } catch { /* socket gone */ }
}

function sendPlayer(sock) {
  sendJson(sock, { Code: 201, Message: 'Player Information', Player: player });
}

// Turn an OpenConnect shot message into what the game needs.
export function toShot(msg, source = 'launch-monitor') {
  const b = msg.BallData || {};
  const { backSpin, sideSpin } = normalizeSpin(b);
  const total = b.TotalSpin ?? Math.hypot(backSpin, sideSpin);
  return {
    source,
    device: msg.DeviceID || 'unknown',
    shotNumber: msg.ShotNumber ?? null,
    speedMph: +b.Speed || 0,
    vla: +b.VLA || 0,
    hla: +b.HLA || 0,
    backSpin,
    sideSpin,
    totalSpin: total,
    spinAxis: b.SpinAxis ?? (Math.atan2(sideSpin, backSpin) * 180) / Math.PI,
    lmCarry: b.CarryDistance ?? null,
    units: msg.Units || 'Yards',
    club: msg.ClubData || null,
    at: Date.now(),
  };
}

function handleMessage(sock, msg) {
  const opts = msg.ShotDataOptions || {};
  if (msg.DeviceID && msg.DeviceID !== lmState.device) setLm({ device: msg.DeviceID });
  if ('LaunchMonitorIsReady' in opts || 'LaunchMonitorBallDetected' in opts) {
    const ready = opts.LaunchMonitorIsReady ?? lmState.ready;
    const ballDetected = opts.LaunchMonitorBallDetected ?? lmState.ballDetected;
    if (ready !== lmState.ready || ballDetected !== lmState.ballDetected) setLm({ ready, ballDetected });
  }
  if (opts.IsHeartBeat || opts.ContainsBallData === false || !msg.BallData) {
    sendJson(sock, { Code: 200, Message: 'Heartbeat received' });
    return;
  }
  const shot = toShot(msg);
  if (shot.speedMph <= 0) {
    sendJson(sock, { Code: 501, Message: 'Ball speed missing' });
    return;
  }
  // Some connectors resend a shot; ignore exact duplicates within a few seconds.
  const key = `${shot.shotNumber}|${shot.speedMph}|${shot.vla}|${shot.hla}`;
  if (key === lastShot.key && Date.now() - lastShot.at < 5000) {
    sendJson(sock, { Code: 200, Message: 'Shot received successfully' });
    return;
  }
  lastShot = { key, at: Date.now() };
  console.log(`Shot #${shot.shotNumber ?? '?'}: ${shot.speedMph.toFixed(1)} mph, VLA ${shot.vla.toFixed(1)}, HLA ${shot.hla.toFixed(1)}, spin ${Math.round(shot.totalSpin)} rpm, axis ${shot.spinAxis.toFixed(1)}`);
  setLm({ lastShotAt: shot.at });
  broadcast('shot', shot);
  sendJson(sock, { Code: 200, Message: 'Shot received successfully' });
}

// Messages arrive as back-to-back JSON objects with no delimiter; split on balanced braces.
function jsonSplitter(onObject) {
  let buf = '';
  return (chunk) => {
    buf += chunk;
    let depth = 0, inStr = false, esc = false, start = -1;
    let consumed = 0;
    for (let i = 0; i < buf.length; i++) {
      const c = buf[i];
      if (inStr) {
        if (esc) esc = false;
        else if (c === '\\') esc = true;
        else if (c === '"') inStr = false;
        continue;
      }
      if (c === '"') inStr = true;
      else if (c === '{') { if (depth++ === 0) start = i; }
      else if (c === '}' && depth > 0 && --depth === 0) {
        const text = buf.slice(start, i + 1);
        consumed = i + 1;
        try { onObject(JSON.parse(text)); } catch (err) { console.warn('Bad JSON from launch monitor:', err.message); }
      }
    }
    buf = buf.slice(consumed);
    if (depth === 0) buf = buf.replace(/^[^{]*/, '');
    if (buf.length > 1e6) buf = '';
  };
}

const ocServer = net.createServer((sock) => {
  sock.setEncoding('utf8');
  sock.setNoDelay(true);
  lmSockets.add(sock);
  const addr = `${sock.remoteAddress}:${sock.remotePort}`;
  console.log(`Launch monitor connected from ${addr}`);
  setLm({ connected: true, address: addr });
  sendPlayer(sock);
  sock.on('data', jsonSplitter((msg) => handleMessage(sock, msg)));
  const gone = () => {
    if (!lmSockets.delete(sock)) return;
    console.log(`Launch monitor disconnected (${addr})`);
    setLm({ connected: lmSockets.size > 0, ready: lmSockets.size > 0 && lmState.ready });
  };
  sock.on('close', gone);
  sock.on('error', gone);
});
ocServer.on('error', (err) => {
  if (err.code === 'EADDRINUSE') {
    console.error(`\n!! Port ${OC_PORT} is already in use. Close any other simulator or connector using it so OpenTeeGolf can receive Mevo+ shots.\n`);
  } else console.error('OpenConnect server error:', err);
});
ocServer.listen(OC_PORT, '0.0.0.0', () => console.log(`OpenConnect listener on port ${OC_PORT} (point FS Golf here)`));

// ---------------------------------------------------------------- HTTP

const MIME = {
  '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8',
  '.json': 'application/json', '.png': 'image/png', '.jpg': 'image/jpeg', '.jpeg': 'image/jpeg', '.svg': 'image/svg+xml',
  '.webp': 'image/webp', '.gif': 'image/gif', '.wasm': 'application/wasm',
};

function serveFile(res, base, rel) {
  const file = path.join(base, rel);
  if (!file.startsWith(base + path.sep) && file !== base) return notFound(res);
  fs.stat(file, (err, st) => {
    if (err || !st.isFile()) return notFound(res);
    res.writeHead(200, { 'Content-Type': MIME[path.extname(file).toLowerCase()] || 'application/octet-stream', 'Cache-Control': 'no-cache' });
    fs.createReadStream(file).pipe(res);
  });
}
const notFound = (res) => { res.writeHead(404); res.end('Not found'); };
const json = (res, obj, code = 200) => { res.writeHead(code, { 'Content-Type': 'application/json' }); res.end(JSON.stringify(obj)); };
const readBody = (req, limit = 1e5) => new Promise((resolve, reject) => {
  let b = '';
  req.on('data', (c) => { b += c; if (b.length > limit) req.destroy(); });
  req.on('end', () => { try { resolve(b ? JSON.parse(b) : {}); } catch (e) { reject(e); } });
});

const PUBLIC = path.join(ROOT, 'public');
const THREE = path.join(ROOT, 'node_modules', 'three');
const JOLT = path.join(ROOT, 'node_modules', 'jolt-physics', 'dist');

const httpServer = http.createServer(async (req, res) => {
  const url = new URL(req.url, 'http://localhost');
  const p = decodeURIComponent(url.pathname);
  try {
    if (p === '/api/courses') return json(res, { dir: BUILT_DIR, courses: findCourses() });
    let m;
    if ((m = p.match(/^\/api\/courses\/([\w-]+)\/data$/))) {
      const f = courseFile(m[1]);
      return f ? json(res, readCourseData(f)) : notFound(res);
    }
    if ((m = p.match(/^\/api\/courses\/([\w-]+)\/real$/))) {
      const f = courseFile(m[1]);
      const rf = f && realFile(f);
      return rf && fs.existsSync(rf) ? serveFile(res, path.dirname(rf), path.basename(rf)) : notFound(res);
    }
    if ((m = p.match(/^\/api\/courses\/([\w-]+)\/satellite$/))) {
      // Aerial imagery manifest (downloads the imagery the first time a course is opened)
      const f = courseFile(m[1]);
      const rf = f && realFile(f);
      if (!rf || !fs.existsSync(rf)) return json(res, { error: 'No real-world data for this course' }, 404);
      const real = JSON.parse(fs.readFileSync(rf, 'utf8'));
      const geo = geoFor(readCourseData(f), real);
      if (!geo) return json(res, { error: 'Course position unknown — re-run the import to enable imagery' }, 404);
      const satDir = rf.replace(/\.real\.json$/i, '.sat');
      const manifest = await ensureSatellite(satDir, geo, real.grid, (msg) => { console.log(`[imagery] ${msg}`); broadcast('imagery', { course: m[1], msg }); });
      return json(res, manifest);
    }
    if ((m = p.match(/^\/api\/courses\/([\w-]+)\/paths$/))) {
      const f = courseFile(m[1]);
      const rf = f && realFile(f);
      if (!rf || !fs.existsSync(rf)) return json(res, []);
      const real = JSON.parse(fs.readFileSync(rf, 'utf8'));
      const cd = readCourseData(f);
      const geo = geoFor(cd, real);
      if (!geo) return json(res, []);
      const oob = (cd.pOOB?.coords || []).map((c) => [c.x, c.z]);
      return json(res, await ensurePaths(rf.replace(/\.real\.json$/i, '.sat'), geo, real.grid, oob, (msg) => console.log(`[paths] ${msg}`)));
    }
    if ((m = p.match(/^\/api\/courses\/([\w-]+)\/buildings$/))) {
      const f = courseFile(m[1]);
      const rf = f && realFile(f);
      if (!rf || !fs.existsSync(rf)) return json(res, []);
      const real = JSON.parse(fs.readFileSync(rf, 'utf8'));
      const geo = geoFor(readCourseData(f), real);
      if (!geo) return json(res, []);
      return json(res, await ensureBuildings(rf.replace(/\.real\.json$/i, '.sat'), geo, real.grid, (msg) => console.log(`[buildings] ${msg}`)));
    }
    if ((m = p.match(/^\/api\/courses\/([\w-]+)\/satellite\/([\w.-]+\.jpg)$/))) {
      const f = courseFile(m[1]);
      const rf = f && realFile(f);
      if (!rf) return notFound(res);
      return serveFile(res, rf.replace(/\.real\.json$/i, '.sat'), m[2]);
    }
    if ((m = p.match(/^\/api\/courses\/([\w-]+)\/photo$/))) {
      const f = courseFile(m[1]);
      if (!f) return notFound(res);
      const dir = path.dirname(f);
      if (req.method === 'PUT') {
        // A picture chosen by the player (raw image bytes)
        const type = (req.headers['content-type'] || '').split(';')[0];
        const ext = { 'image/jpeg': 'jpg', 'image/png': 'png', 'image/webp': 'webp', 'image/gif': 'gif' }[type];
        if (!ext) return json(res, { error: 'Pick a JPG, PNG, WebP or GIF picture' }, 400);
        const chunks = []; let size = 0;
        for await (const c of req) { size += c.length; if (size > 1.2e7) return json(res, { error: 'Picture is too large (12 MB max)' }, 413); chunks.push(c); }
        return json(res, savePhoto(dir, Buffer.concat(chunks), ext, { source: 'user', credit: '' }));
      }
      const force = req.method === 'POST'; // look it up again
      const meta = await ensurePhoto(dir, photoInfo(f), { force, log: (msg) => console.log(`[photo] ${msg}`) });
      if (force) return json(res, meta);
      return meta?.file ? serveFile(res, dir, meta.file) : notFound(res);
    }
    if ((m = p.match(/^\/api\/courses\/([\w-]+)\/aerial$/))) {
      // The middle tile of the downloaded aerial imagery, for menu cards without a photo
      const f = courseFile(m[1]);
      const sd = f && satDir(f);
      let man = null;
      try { man = JSON.parse(fs.readFileSync(path.join(sd, 'manifest.json'), 'utf8')); } catch { return notFound(res); }
      const chunks = man.chunks || [];
      const mid = chunks[Math.floor(chunks.length / 2)];
      return mid ? serveFile(res, sd, mid.file) : notFound(res);
    }
    if (p === '/api/events') {
      res.writeHead(200, { 'Content-Type': 'text/event-stream', 'Cache-Control': 'no-cache', Connection: 'keep-alive' });
      res.write(`event: lm\ndata: ${JSON.stringify(lmState)}\n\n`);
      sseClients.add(res);
      const ping = setInterval(() => res.write(': ping\n\n'), 20000);
      req.on('close', () => { clearInterval(ping); sseClients.delete(res); });
      return;
    }
    if (p === '/api/player' && req.method === 'POST') {
      const b = await readBody(req);
      if (b.club) player.Club = String(b.club);
      if (b.handed) player.Handed = b.handed === 'LH' ? 'LH' : 'RH';
      if (b.distanceToTarget != null) player.DistanceToTarget = +b.distanceToTarget || 0;
      for (const s of lmSockets) sendPlayer(s);
      return json(res, { ok: true, player });
    }
    if (p === '/api/test-shot' && req.method === 'POST') {
      // Accepts the same BallData shape a launch monitor sends.
      const b = await readBody(req);
      const shot = toShot({ DeviceID: 'Test', BallData: b.BallData || b }, 'test');
      broadcast('shot', shot);
      return json(res, { ok: true, shot });
    }
    // ---- course builder
    if (p === '/api/builder/search') {
      const q = (url.searchParams.get('q') || '').trim();
      if (q.length < 3) return json(res, { error: 'Type at least 3 letters' }, 400);
      const results = await searchCourses(q);
      const built = findCourses();
      for (const r of results) r.built = built.find((c) => c.osmId === r.osmId)?.id || null;
      return json(res, { results });
    }
    if (p === '/api/builder/check') {
      const id = url.searchParams.get('id');
      return json(res, await checkCourse(url.searchParams.get('type'), id, (msg) => broadcast('check', { osmId: +id, msg })));
    }
    if (p === '/api/builder/build' && req.method === 'POST') {
      const b = await readBody(req);
      const jobId = Date.now().toString(36);
      const log = (msg) => { console.log(`[build] ${msg}`); broadcast('build', { jobId, msg }); };
      buildCourse({ osmType: b.osmType, osmId: b.osmId, group: b.group ?? null, name: b.name || null, location: b.location || '' }, log)
        .then((r) => { ensurePhoto(path.dirname(r.dataFile), photoInfo(r.dataFile), { log: (m) => console.log(`[photo] ${m}`) }); return r; })
        .then((r) => broadcast('build', { jobId, done: true, courseId: path.basename(path.dirname(r.dataFile)), needsReview: r.needsReview }))
        .catch((err) => { console.error(err); broadcast('build', { jobId, error: err.message }); });
      return json(res, { jobId });
    }
    if ((m = p.match(/^\/api\/courses\/([\w-]+)$/)) && req.method === 'PUT') {
      // Editor save: the course file plus the mapped shapes (terrain is kept)
      const f = courseFile(m[1]);
      if (!f || !isEditable(f)) return json(res, { error: 'Only courses made with the builder can be edited' }, 403);
      const b = await readBody(req, 5e7);
      if (!b.course?.Holes) return json(res, { error: 'Missing course data' }, 400);
      fs.writeFileSync(f, JSON.stringify(b.course, null, 1));
      const rf = realFile(f);
      if (b.features && fs.existsSync(rf)) {
        const real = JSON.parse(fs.readFileSync(rf, 'utf8'));
        real.features = b.features;
        real.edited = new Date().toISOString();
        fs.writeFileSync(rf, JSON.stringify(real));
      }
      return json(res, { ok: true });
    }
    if ((m = p.match(/^\/api\/courses\/([\w-]+)$/)) && req.method === 'DELETE') {
      const f = courseFile(m[1]);
      if (!f || !isEditable(f)) return json(res, { error: 'Only courses made with the builder can be deleted' }, 403);
      fs.rmSync(path.dirname(f), { recursive: true, force: true });
      return json(res, { ok: true });
    }
    if (p.startsWith('/vendor/three/')) return serveFile(res, THREE, p.slice('/vendor/three/'.length));
    if (p.startsWith('/vendor/jolt/')) return serveFile(res, JOLT, p.slice('/vendor/jolt/'.length));
    return serveFile(res, PUBLIC, p === '/' ? 'index.html' : p.slice(1));
  } catch (err) {
    console.error(err);
    json(res, { error: err.message }, 500);
  }
});
httpServer.on('error', (err) => {
  console.error(err.code === 'EADDRINUSE' ? `Port ${HTTP_PORT} is in use; set PORT to another value.` : err);
  process.exit(1);
});
httpServer.listen(HTTP_PORT, () => {
  console.log(`OpenTeeGolf running at http://localhost:${HTTP_PORT}`);
  console.log(`Courses folder: ${BUILT_DIR} (${findCourses().length} course(s) found)`);
});
