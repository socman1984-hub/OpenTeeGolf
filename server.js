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
import { ensureCanopy } from './lib/canopy.js';
import { MevoClient, DEFAULT_SETUP } from './lib/mevo.js';

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

// Download progress for the loading screen: counts ("3/12") or percentages in a message become a bar
function reporter(course, task) {
  return (msg) => {
    console.log(`[${task}] ${msg}`);
    const n = /(\d+)\s*\/\s*(\d+)/.exec(msg), pct = /(\d+)%/.exec(msg);
    broadcast('progress', { course, task, msg, done: n ? +n[1] : pct ? +pct[1] : null, total: n ? +n[2] : pct ? 100 : null });
  };
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
  if (settings.lm.connection !== 'direct') setLm({ connected: true, address: addr });
  sendPlayer(sock);
  sock.on('data', jsonSplitter((msg) => handleMessage(sock, msg)));
  const gone = () => {
    if (!lmSockets.delete(sock)) return;
    console.log(`Launch monitor disconnected (${addr})`);
    if (settings.lm.connection !== 'direct') setLm({ connected: lmSockets.size > 0, ready: lmSockets.size > 0 && lmState.ready });
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

// ---------------------------------------------------------------- Mevo+ direct (no FS Golf)

// Saved choice of how shots arrive: 'fsgolf' (FS Golf → OpenConnect above) or 'direct' (this PC
// joins the Mevo+ Wi-Fi and talks to the radar itself, see lib/mevo.js)
const SETTINGS_FILE = process.env.SETTINGS_FILE || path.join(ROOT, 'data', 'settings.json');
const settings = (() => { try { return JSON.parse(fs.readFileSync(SETTINGS_FILE, 'utf8')); } catch { return {}; } })();
settings.lm = { connection: 'fsgolf', host: '192.168.2.1', port: 5100, videoPort: 8080, ...settings.lm };
const saveSettings = () => { fs.mkdirSync(path.dirname(SETTINGS_FILE), { recursive: true }); fs.writeFileSync(SETTINGS_FILE, JSON.stringify(settings, null, 1)); };

// Putter → putting mode, short shots → chipping, everything else full swing
// (the setup page can force full swing or outdoor mode for everything but putts)
const mevoMode = () => {
  if (player.Club === 'PT') return 'putting';
  const forced = settings.lm.setup?.mode;
  if (forced === 'full' || forced === 'outdoor') return forced;
  return player.DistanceToTarget > 0 && player.DistanceToTarget < 40 ? 'chipping' : 'full';
};
const DIRECT_TEXT = {
  connecting: 'Looking for the Mevo+…', searching: 'Mevo+ not found', handshake: 'Connecting to the Mevo+…',
  arming: 'Arming…', ready: 'ready', shot: 'Reading shot…', idle: 'disarmed', off: 'disconnected',
};
let mevo = null;
let mevoOff = false; // the player disconnected it (until they connect again or restart the game)
// Every message to and from the Mevo+, written to data/mevo-log.txt (started fresh each run, kept
// under ~5 MB) so a connection problem can be looked at afterwards
const MEVO_LOG = path.join(path.dirname(SETTINGS_FILE), 'mevo-log.txt');
let mevoLogSize = 0;
function mevoWrite(line, always = false) {
  if (mevoLogSize > 5e6 && !always) return;
  mevoLogSize += line.length + 1;
  try { fs.appendFileSync(MEVO_LOG, `${new Date().toISOString().slice(11, 23)} ${line}\n`); } catch { /* not important */ }
}
try { fs.mkdirSync(path.dirname(MEVO_LOG), { recursive: true }); fs.writeFileSync(MEVO_LOG, ''); } catch { /* ignore */ }
function mevoLog(m) { console.log(m); mevoWrite(m, true); }
function mevoTrace(dir, f) {
  if (f.type === 0xaa && mevo?.state === 'ready') return; // the once-a-second status polls
  const hex = Buffer.from(f.payload).toString('hex').toUpperCase().replace(/(..)(?!$)/g, '$1 ');
  const txt = f.type === 0xe3 || f.type === 0xe5 ? `  "${Buffer.from(f.payload).toString('latin1').replace(/[^\x20-\x7e]/g, '')}"` : '';
  mevoWrite(`${dir} ${f.src.toString(16).padStart(2, '0')}->${f.dest.toString(16).padStart(2, '0')} ${f.type.toString(16).padStart(2, '0')} [${hex}]${txt}`);
}
function startMevo() {
  mevo?.removeAllListeners(); // (its last "off" mustn't overwrite the new one's status)
  mevo?.disconnect(); // disarms it if it was armed
  mevo = null;
  if (settings.lm.connection !== 'direct' || mevoOff) {
    if (mevoOff) setLm({ connected: false, ready: false, battery: null, direct: { state: 'off', text: DIRECT_TEXT.off, error: null, mode: null } });
    else setLm({ direct: null, connected: lmSockets.size > 0, ready: lmSockets.size > 0 && lmState.ready, battery: null });
    return;
  }
  mevo = new MevoClient({ host: settings.lm.host, port: settings.lm.port, mode: mevoMode(), log: mevoLog, trace: mevoTrace });
  if (settings.lm.setup) mevo.setup = { ...DEFAULT_SETUP, ...settings.lm.setup };
  // Everything the setup page shows, about once a second
  mevo.on('status', (s) => broadcast('mevo', s));
  mevo.on('status', (s) => setLm({
    connected: ['ready', 'shot', 'arming', 'idle'].includes(s.state),
    ready: s.state === 'ready',
    device: s.model ? `${s.model} ${s.ssid || ''}`.trim() : 'Mevo+',
    battery: s.battery ?? null,
    direct: { state: s.state, text: DIRECT_TEXT[s.state] || s.state, error: s.error || null, mode: s.mode },
  }));
  mevo.on('shot', (ball) => {
    const shot = toShot({ DeviceID: lmState.device || 'Mevo+', BallData: ball, ClubData: ball.ClubData }, 'mevo');
    console.log(`Shot (direct): ${shot.speedMph.toFixed(1)} mph, VLA ${shot.vla.toFixed(1)}, HLA ${shot.hla.toFixed(1)}, spin ${Math.round(shot.totalSpin)} rpm, axis ${shot.spinAxis.toFixed(1)}`);
    setLm({ lastShotAt: shot.at });
    broadcast('shot', shot);
  });
  mevo.start();
}
startMevo();

// The Mevo+ camera's live MJPEG stream (port 8080 on the Mevo+), passed through to the setup page
// so the browser never has to reach the device itself. The stream's path isn't documented, so the
// usual ones are tried and the first that answers with video is remembered.
const VIDEO_PATHS = ['/', '/stream', '/?action=stream', '/stream.mjpg', '/video', '/mjpeg', '/video.mjpg', '/cam.mjpg'];
let videoPath = null;
function proxyMevoVideo(req, res) {
  if (!mevo) { res.writeHead(409); return res.end('Not connected directly'); }
  const tryPath = (i) => {
    const pathTry = videoPath || VIDEO_PATHS[i];
    if (!pathTry) { res.writeHead(502); return res.end('No video stream found on the Mevo+'); }
    const up = http.get({ host: settings.lm.host, port: settings.lm.videoPort, path: pathTry, timeout: 4000 }, (r) => {
      const type = r.headers['content-type'] || '';
      if (r.statusCode !== 200 || !/multipart|image\/jpeg/i.test(type)) {
        r.resume();
        if (videoPath) { videoPath = null; return tryPath(0); }
        return tryPath(i + 1);
      }
      videoPath = pathTry;
      res.writeHead(200, { 'Content-Type': type, 'Cache-Control': 'no-cache, no-store', Connection: 'close' });
      r.pipe(res);
      req.on('close', () => up.destroy());
    });
    up.on('timeout', () => up.destroy(new Error('timeout')));
    up.on('error', () => {
      if (res.headersSent) return res.end();
      if (videoPath) { videoPath = null; return tryPath(0); }
      tryPath(i + 1);
    });
  };
  tryPath(0);
}

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
const EZTREE = path.join(ROOT, 'node_modules', '@dgreenheck', 'ez-tree', 'build');

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
      const manifest = await ensureSatellite(satDir, geo, real.grid, reporter(m[1], 'imagery'));
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
      return json(res, await ensurePaths(rf.replace(/\.real\.json$/i, '.sat'), geo, real.grid, oob, reporter(m[1], 'paths')));
    }
    if ((m = p.match(/^\/api\/courses\/([\w-]+)\/buildings$/))) {
      const f = courseFile(m[1]);
      const rf = f && realFile(f);
      if (!rf || !fs.existsSync(rf)) return json(res, []);
      const real = JSON.parse(fs.readFileSync(rf, 'utf8'));
      const geo = geoFor(readCourseData(f), real);
      if (!geo) return json(res, []);
      return json(res, await ensureBuildings(rf.replace(/\.real\.json$/i, '.sat'), geo, real.grid, reporter(m[1], 'buildings')));
    }
    if ((m = p.match(/^\/api\/courses\/([\w-]+)\/canopy$/))) {
      // Tree heights (downloaded the first time a course is opened); the grid itself is canopy.bin
      const f = courseFile(m[1]);
      const rf = f && realFile(f);
      if (!rf || !fs.existsSync(rf)) return json(res, { available: false });
      const real = JSON.parse(fs.readFileSync(rf, 'utf8'));
      const geo = geoFor(readCourseData(f), real);
      if (!geo) return json(res, { available: false });
      return json(res, await ensureCanopy(satDir(f), geo, real.grid, reporter(m[1], 'canopy')));
    }
    if ((m = p.match(/^\/api\/courses\/([\w-]+)\/canopy\.bin$/))) {
      const f = courseFile(m[1]);
      return f ? serveFile(res, satDir(f), 'canopy.bin') : notFound(res);
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
      mevo?.setMode(mevoMode());
      return json(res, { ok: true, player });
    }
    if (p === '/api/mevo') {
      // Mevo+ setup page: connection, live device status and the saved setup
      return json(res, {
        connection: settings.lm.connection, host: settings.lm.host,
        state: mevo?.state || 'off', info: mevo?.info || {}, mode: mevo?.mode || null, off: mevoOff,
        setup: { ...DEFAULT_SETUP, ...settings.lm.setup }, custom: !!settings.lm.setup,
      });
    }
    if (p === '/api/mevo/setup' && req.method === 'POST') {
      const b = await readBody(req);
      const num = (v, lo, hi, d) => (Number.isFinite(+v) ? Math.min(hi, Math.max(lo, +v)) : d);
      const d = DEFAULT_SETUP;
      settings.lm.setup = {
        distanceM: num(b.distanceM, 1.5, 6, d.distanceM), surfaceIn: num(b.surfaceIn, 0, 10, d.surfaceIn),
        teeHeightIn: num(b.teeHeightIn, 0, 4, d.teeHeightIn), ballType: b.ballType ? 1 : 0,
        trackPct: num(b.trackPct, 0.6, 1, d.trackPct), mode: ['auto', 'full', 'outdoor'].includes(b.mode) ? b.mode : 'auto',
      };
      saveSettings();
      if (mevo) {
        try {
          await mevo.applySetup(settings.lm.setup);
          mevo.setMode(mevoMode());
        } catch (err) { return json(res, { error: err.message }, 502); }
      }
      return json(res, { ok: true, setup: settings.lm.setup });
    }
    if (p === '/api/mevo/camera' && req.method === 'POST') {
      const b = await readBody(req);
      if (!mevo || !mevo.armed) return json(res, { error: 'The Mevo+ is not connected directly' }, 409);
      try { await mevo.camera(!!b.on); } catch (err) { return json(res, { error: err.message }, 502); }
      return json(res, { ok: true, info: mevo.info });
    }
    if (p === '/api/mevo/power' && req.method === 'POST') {
      // { connect: false } disarms and hangs up; { connect: true } connects again;
      // { armed: false | true } stays connected but stops / starts detecting shots
      const b = await readBody(req);
      if (settings.lm.connection !== 'direct') return json(res, { error: 'The Mevo+ is not connected directly' }, 409);
      try {
        if (b.connect === false) {
          mevoOff = true;
          const m = mevo;
          mevo = null;
          await m?.disconnect();
          broadcast('mevo', { state: 'off', mode: null });
          startMevo();
        } else if (b.connect === true) {
          mevoOff = false;
          if (!mevo) startMevo();
        } else if (typeof b.armed === 'boolean') {
          if (!mevo) return json(res, { error: 'The Mevo+ is disconnected' }, 409);
          await mevo.setArmed(b.armed);
        }
      } catch (err) { return json(res, { error: err.message }, 502); }
      return json(res, { ok: true, state: mevo?.state || 'off' });
    }
    if (p === '/api/mevo/log') {
      res.writeHead(200, { 'Content-Type': 'text/plain; charset=utf-8', 'Content-Disposition': 'attachment; filename="mevo-log.txt"' });
      return fs.createReadStream(MEVO_LOG).on('error', () => res.end('(no log yet)')).pipe(res);
    }
    if (p === '/api/mevo/video') return proxyMevoVideo(req, res);
    if (p === '/api/lm') {
      // How shots arrive: FS Golf (OpenConnect) or the Mevo+ directly
      if (req.method === 'POST') {
        const b = await readBody(req);
        if (b.connection === 'direct' || b.connection === 'fsgolf') settings.lm.connection = b.connection;
        if (typeof b.host === 'string' && /^[\w.-]{1,64}$/.test(b.host)) settings.lm.host = b.host;
        if (Number.isInteger(b.port) && b.port > 0 && b.port < 65536) settings.lm.port = b.port;
        if (Number.isInteger(b.videoPort) && b.videoPort > 0 && b.videoPort < 65536) settings.lm.videoPort = b.videoPort;
        saveSettings();
        mevoOff = false;
        startMevo();
      }
      return json(res, { settings: settings.lm, state: lmState });
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
    if (p.startsWith('/vendor/ez-tree/')) return serveFile(res, EZTREE, p.slice('/vendor/ez-tree/'.length));
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

// Closing the game (Ctrl+C or closing its window) disarms the Mevo+ first, so it isn't left
// watching for shots
let quitting = false;
for (const sig of ['SIGINT', 'SIGTERM', 'SIGHUP', 'SIGBREAK']) {
  process.on(sig, async () => {
    if (quitting) process.exit(0);
    quitting = true;
    if (mevo) {
      console.log('Disarming the Mevo+…');
      await Promise.race([mevo.disconnect(), new Promise((r) => setTimeout(r, 3500))]);
    }
    process.exit(0);
  });
}
