// Updates: is there a newer OpenTeeGolf release on GitHub, and installing it.
//
// Installing (only in a release copy, not a development folder):
//   1. the release zip is downloaded to the temp folder and checked against the SHA-256 GitHub lists
//   2. it is unpacked there, and a small script (apply.cmd) is written and started
//   3. the server exits; the script waits for it, copies the new files over the old ones (never
//      courses/ or data/, so built courses and settings stay), and starts the game again
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawn, execFileSync } from 'node:child_process';
import { ROOT, UA } from './realdata.js';

export const REPO = 'socman1984-hub/OpenTeeGolf';
const PKG = JSON.parse(fs.readFileSync(path.join(ROOT, 'package.json'), 'utf8'));
export const VERSION = PKG.version;
// (under the system temp folder: unpacked inside the game folder, the deepest files could pass
// Windows' 260-character path limit)
const UPDATE_DIR = path.join(os.tmpdir(), 'OpenTeeGolf-update');
// (testing only: a local copy of GitHub's "latest release" answer, and downloads from anywhere)
const TEST_FEED = process.env.OPENTEEGOLF_RELEASES_URL || null;

// "0.3.10" > "0.3.9"
export function newer(a, b) {
  const pa = String(a).replace(/^v/, '').split('.').map((n) => parseInt(n, 10) || 0);
  const pb = String(b).replace(/^v/, '').split('.').map((n) => parseInt(n, 10) || 0);
  for (let i = 0; i < Math.max(pa.length, pb.length); i++) if ((pa[i] || 0) !== (pb[i] || 0)) return (pa[i] || 0) > (pb[i] || 0);
  return false;
}

// A release copy (unzipped from GitHub, with its own Node.js) can update itself; a development
// folder (a git checkout) is never overwritten - update it with git instead
export function canInstall() {
  return process.platform === 'win32' && fs.existsSync(path.join(ROOT, 'node', 'node.exe')) && !fs.existsSync(path.join(ROOT, '.git'));
}

let cached = null, cachedAt = 0;
// The newest release, looked up at most every few hours: { latest, available, notes, url, asset }
export async function checkForUpdate({ force = false } = {}) {
  if (!force && cached && Date.now() - cachedAt < 3 * 3600e3) return cached;
  const res = await fetch(TEST_FEED || `https://api.github.com/repos/${REPO}/releases/latest`, {
    headers: { 'User-Agent': UA, Accept: 'application/vnd.github+json' },
    signal: AbortSignal.timeout(15000),
  });
  if (!res.ok) throw new Error(`GitHub answered ${res.status}`);
  const r = await res.json();
  const latest = String(r.tag_name || '').replace(/^v/, '');
  const asset = (r.assets || []).find((a) => /-win-x64\.zip$/i.test(a.name));
  cached = {
    current: VERSION,
    latest,
    available: !!latest && newer(latest, VERSION),
    published: r.published_at || null,
    notes: String(r.body || '').slice(0, 4000),
    url: r.html_url || `https://github.com/${REPO}/releases/latest`,
    asset: asset ? { name: asset.name, size: asset.size, url: asset.browser_download_url, sha256: /^sha256:/.test(asset.digest || '') ? asset.digest.slice(7) : null } : null,
  };
  cachedAt = Date.now();
  return cached;
}

let installing = false;
// Download, check, unpack and hand over to the update script. progress({ stage, done, total, msg })
export async function installUpdate(progress = () => {}) {
  if (installing) throw new Error('An update is already being installed');
  if (!canInstall()) throw new Error('This copy can’t update itself (it isn’t a release download). Download the new version from GitHub instead.');
  const u = await checkForUpdate({ force: true });
  if (!u.available) throw new Error('You already have the newest version');
  if (!u.asset) throw new Error('The new release has no Windows download yet. Try again in a few minutes.');
  if (!TEST_FEED && !/^https:\/\/github\.com\//.test(u.asset.url)) throw new Error('Unexpected download address');
  installing = true;
  try {
    fs.rmSync(UPDATE_DIR, { recursive: true, force: true });
    fs.mkdirSync(UPDATE_DIR, { recursive: true });
    // 1. Download (following GitHub's redirect to its file servers), hashing as it arrives
    const zip = path.join(UPDATE_DIR, u.asset.name);
    const res = await fetch(u.asset.url, { headers: { 'User-Agent': UA }, signal: AbortSignal.timeout(30 * 60e3) });
    if (!res.ok || !res.body) throw new Error(`Download failed (${res.status})`);
    const total = +res.headers.get('content-length') || u.asset.size || 0;
    const hash = crypto.createHash('sha256');
    const out = fs.createWriteStream(zip);
    let done = 0, lastReport = 0;
    for await (const chunk of res.body) {
      hash.update(chunk);
      if (!out.write(chunk)) await new Promise((r) => out.once('drain', r));
      done += chunk.length;
      if (Date.now() - lastReport > 250) { lastReport = Date.now(); progress({ stage: 'download', done, total, msg: `Downloading ${u.latest}…` }); }
    }
    await new Promise((r, j) => out.end((e) => (e ? j(e) : r())));
    progress({ stage: 'download', done, total, msg: 'Downloaded' });
    // 2. Check it is exactly the file GitHub published
    const sum = hash.digest('hex');
    if (u.asset.sha256 ? sum !== u.asset.sha256.toLowerCase() : done !== u.asset.size) {
      throw new Error('The download didn’t match the published release (it may have been cut short). Nothing was changed; try again.');
    }
    // 3. Unpack (Windows has tar, which reads zips) and find the app folder inside
    progress({ stage: 'unpack', msg: 'Unpacking…' });
    const unpacked = path.join(UPDATE_DIR, 'files');
    fs.mkdirSync(unpacked);
    execFileSync(path.join(process.env.SystemRoot || 'C:\\Windows', 'System32', 'tar.exe'), ['-xf', zip, '-C', unpacked], { stdio: 'ignore', windowsHide: true });
    let src = unpacked;
    const top = fs.readdirSync(unpacked);
    if (top.length === 1 && fs.statSync(path.join(unpacked, top[0])).isDirectory()) src = path.join(unpacked, top[0]);
    if (!fs.existsSync(path.join(src, 'server.js')) || !fs.existsSync(path.join(src, 'node', 'node.exe'))) throw new Error('The download doesn’t look like an OpenTeeGolf release');
    // 4. The script that swaps the files once this server has stopped
    const parentIsConsole = isCmd(process.ppid);
    const script = [
      '@echo off',
      `title Updating OpenTeeGolf to ${u.latest}`,
      `echo Updating OpenTeeGolf to ${u.latest}...`,
      // wait for the game's server to stop
      ':wait',
      // (tasklist lists only that process, or prints "No tasks" once it's gone)
      `tasklist /FI "PID eq ${process.pid}" /NH 2>nul | find /I "node" >nul && (timeout /t 1 /nobreak >nul & goto wait)`,
      // the old game window (it would otherwise read its updated start script mid-run)
      parentIsConsole ? `taskkill /PID ${process.ppid} /F >nul 2>&1` : 'rem',
      `robocopy "${src}" "${ROOT}" /E /XD "${path.join(src, 'courses')}" "${path.join(src, 'data')}" /R:3 /W:1 /NFL /NDL /NJH /NJS /NP >nul`,
      'if errorlevel 8 (echo The update could not be copied. Your previous version is still there. & pause & exit /b 1)',
      `cd /d "${ROOT}"`,
      'start "OpenTeeGolf" "Start OpenTeeGolf.bat" --no-browser',
      'exit /b 0',
    ].join('\r\n');
    const cmd = path.join(UPDATE_DIR, 'apply.cmd');
    fs.writeFileSync(cmd, script);
    progress({ stage: 'restart', msg: `Restarting with ${u.latest}…` });
    spawn('cmd.exe', ['/c', `start "OpenTeeGolf update" /min "${cmd}"`], { detached: true, stdio: 'ignore', windowsHide: true, windowsVerbatimArguments: true }).unref();
    return { version: u.latest };
  } catch (err) {
    installing = false;
    throw err;
  }
}

// After an update the downloaded files are no longer needed: removed when the game next starts
export function cleanUpAfterUpdate() {
  try { if (fs.existsSync(UPDATE_DIR)) fs.rmSync(UPDATE_DIR, { recursive: true, force: true }); } catch { /* in use: next time */ }
}

// Is this process (by id) a command prompt? (the game's console window)
function isCmd(pid) {
  try {
    const out = execFileSync('tasklist', ['/FI', `PID eq ${pid}`, '/FO', 'CSV', '/NH'], { encoding: 'utf8', windowsHide: true });
    return /^"cmd\.exe"/i.test(out.trim());
  } catch { return false; }
}
