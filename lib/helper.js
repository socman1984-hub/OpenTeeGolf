// Helper mode (OTG_HELPER=1): the server running hidden for the Unreal game, which finds, builds
// and edits courses through it (unreal/OpenTeeGolf: UOTGHelperSubsystem). No launch monitor
// listeners (the game has its own), and the export to Unreal runs in a headless browser: the web
// game opened at /?export=<course id> loads the course out of sight, exports it (export-unreal.js)
// and reports how far it is (POST /api/helper/progress), which the game reads (GET /api/helper/job).

import { spawn, execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

export const HELPER = process.env.OTG_HELPER === '1';

const BROWSERS = [
  process.env.OTG_BROWSER,
  path.join(process.env['ProgramFiles(x86)'] || 'C:\\Program Files (x86)', 'Microsoft', 'Edge', 'Application', 'msedge.exe'),
  path.join(process.env.ProgramFiles || 'C:\\Program Files', 'Microsoft', 'Edge', 'Application', 'msedge.exe'),
  path.join(process.env.ProgramFiles || 'C:\\Program Files', 'Google', 'Chrome', 'Application', 'chrome.exe'),
  path.join(process.env.LOCALAPPDATA || '', 'Google', 'Chrome', 'Application', 'chrome.exe'),
].filter(Boolean);
export const findBrowser = () => BROWSERS.find((b) => { try { return fs.statSync(b).isFile(); } catch { return false; } }) || null;

// The one export at a time: { course, state: 'start' | 'run' | 'done' | 'failed', frac, msg, started, updated }
let job = null, browser = null, timer = null, profile = null;
const MAX_MS = 45 * 60e3;       // (a big course takes 10-20 minutes)
const QUIET_MS = 4 * 60e3;      // (no report for this long: the page has hung)

export const jobState = () => (job ? { ...job, seconds: Math.round((Date.now() - job.started) / 1000) } : { state: 'none' });

// Browsers left from an export that never finished (the game closed mid-way): every process on an
// OpenTeeGolf-export-* profile, and the profiles (when the helper starts)
const killMatching = (leaf) => {
  if (process.platform !== 'win32') return;
  const ps = `Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -like '*${leaf.replace(/'/g, '')}*' -and ($_.Name -eq 'msedge.exe' -or $_.Name -eq 'chrome.exe') } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }`;
  try { execFileSync('powershell.exe', ['-NoProfile', '-NonInteractive', '-Command', ps], { stdio: 'ignore', windowsHide: true, timeout: 20000 }); } catch { /* none */ }
};
export function cleanStale() {
  killMatching('OpenTeeGolf-export-');
  for (const d of fs.readdirSync(os.tmpdir()).filter((n) => n.startsWith('OpenTeeGolf-export-'))) fs.rm(path.join(os.tmpdir(), d), { recursive: true, force: true }, () => {});
}

// The browser goes: every process using this export's own profile folder (Edge starts itself
// again as it opens, so the process started here may already have gone, its browser running on)
function stopBrowser() {
  clearInterval(timer);
  timer = null;
  if (browser && browser.exitCode === null) browser.kill('SIGKILL');
  browser = null;
  const dir = profile;
  profile = null;
  if (!dir) return;
  killMatching(path.basename(dir));
  setTimeout(() => fs.rm(dir, { recursive: true, force: true, maxRetries: 5, retryDelay: 500 }, () => {}), 1500).unref();
}

function finish(state, msg) {
  if (!job || job.state === 'done' || job.state === 'failed') return;
  Object.assign(job, { state, msg, updated: Date.now(), ...(state === 'done' ? { frac: 1 } : {}) });
  console.log(`[helper] export of ${job.course}: ${state}${msg ? ` (${msg})` : ''}`);
  stopBrowser();
}

// Start exporting a course (the browser at /?export=<id> on this server's own port)
export function startExport(course, port, { visible = false } = {}) {
  if (job && (job.state === 'start' || job.state === 'run')) return { error: `Already exporting ${job.course}` };
  const exe = findBrowser();
  if (!exe) return { error: 'No Edge or Chrome on this computer to export with' };
  // (a profile of its own each time: a browser still running on an old one would take the page)
  profile = path.join(os.tmpdir(), `OpenTeeGolf-export-${process.pid}-${Date.now().toString(36)}`);
  fs.mkdirSync(profile, { recursive: true });
  const url = `http://localhost:${port}/?export=${encodeURIComponent(course)}`;
  const args = [
    ...(visible ? [] : ['--headless=new']),
    `--user-data-dir=${profile}`, '--no-first-run', '--no-default-browser-check', '--disable-extensions',
    '--enable-unsafe-webgpu', '--enable-gpu', '--ignore-gpu-blocklist',
    '--disable-background-timer-throttling', '--disable-renderer-backgrounding', '--disable-backgrounding-occluded-windows',
    '--window-size=1280,800', url,
  ];
  job = { course, state: 'start', frac: 0, msg: 'Starting the export…', started: Date.now(), updated: Date.now() };
  console.log(`[helper] exporting ${course} with ${path.basename(exe)}`);
  browser = spawn(exe, args, { stdio: 'ignore', windowsHide: true });
  browser.on('error', (err) => finish('failed', `The browser didn't start: ${err.message}`));
  // (Edge may hand the page to a browser already running and exit at once: only a quiet page counts)
  timer = setInterval(() => {
    if (!job) return;
    const now = Date.now();
    if (now - job.started > MAX_MS) finish('failed', 'The export took too long');
    else if (now - job.updated > QUIET_MS) finish('failed', 'The export stopped answering');
  }, 5000);
  timer.unref();
  return { ok: true, job: jobState() };
}

// A report from the page (main.js helperReport)
export function progress({ course, state, frac, msg }) {
  if (!job || job.course !== course || job.state === 'done' || job.state === 'failed') return;
  if (state === 'done' || state === 'failed') return finish(state, String(msg || ''));
  Object.assign(job, { state: 'run', frac: Math.max(job.frac, Math.min(1, +frac || 0)), msg: String(msg || job.msg).slice(0, 300), updated: Date.now() });
}

export function cancelExport() {
  finish('failed', 'Cancelled');
}

// Builds and updates of courses (server.js broadcasts 'build' events; the game asks for the latest):
// { jobId: { msg, step, done, error, courseId, needsReview, log: [last messages] } }
const builds = new Map();
export function noteBuild(e) {
  const b = builds.get(e.jobId) || { msg: '', step: null, done: false, error: null, courseId: null, log: [], started: Date.now() };
  if (e.msg) { b.msg = e.msg; b.log.push(e.msg); if (b.log.length > 40) b.log.shift(); }
  if (e.step) b.step = e.step;
  if (e.done) Object.assign(b, { done: true, courseId: e.courseId, needsReview: !!e.needsReview });
  if (e.error) b.error = e.error;
  builds.set(e.jobId, b);
  if (builds.size > 20) builds.delete(builds.keys().next().value);
}
export const buildState = (id) => builds.get(id) || { error: 'No such build' };

// The game that started this helper (OTG_PARENT_PID): when it's gone, so is the helper
export function watchParent() {
  const pid = +process.env.OTG_PARENT_PID;
  if (!pid) return;
  setInterval(() => {
    try { process.kill(pid, 0); } catch { console.log('[helper] the game has closed'); stopBrowser(); process.exit(0); }
  }, 3000).unref();
}

process.on('exit', stopBrowser);
