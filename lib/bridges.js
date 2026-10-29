// Bluetooth launch monitors through small open-source bridge programs by Eric Thill (MIT /
// Apache-2.0), each of which finds the device over Bluetooth, arms it and serves its shots over the
// Flight Relay Protocol (lib/frp.js) on a local port:
//   Garmin Approach R10     tenover-frp    github.com/divotmaker/10over
//   Square Golf Omni / Home allsquare-frp  github.com/divotmaker/allsquare
// The release zip carries them in bridges/; running from source, the first use downloads the exact
// release below, checked against its SHA-256, into the same folder.

import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawn } from 'node:child_process';
import { EventEmitter } from 'node:events';
import { fileURLToPath } from 'node:url';

export const BRIDGE_DIR = path.join(path.dirname(path.dirname(fileURLToPath(import.meta.url))), 'bridges');

export const BRIDGES = {
  r10: {
    name: 'Garmin R10', program: 'tenover-frp', repo: 'divotmaker/10over', version: 'v0.2.0',
    files: {
      win32: { file: 'tenover-frp-windows-x86_64.exe', sha256: '8ebeb720d1f98fd690a180565858dc0772d0d7c1bf5f7871d376e4ed9f4ae7a5' },
      linux: { file: 'tenover-frp-linux-x86_64', sha256: 'ef6b5a9f2d0df3ee442197ca00c8b51f18e0f39dffe360402b528c43117e7adb' },
    },
  },
  square: {
    name: 'Square Golf', program: 'allsquare-frp', repo: 'divotmaker/allsquare', version: 'v0.2.0',
    files: {
      win32: { file: 'allsquare-frp-windows-x86_64.exe', sha256: '1954bfa58d4c4020d8d3b94d9b2f140af239cd2482c7380b3817107439a63c7d' },
      linux: { file: 'allsquare-frp-linux-x86_64', sha256: 'e0d2827fa06d3a7203a7bd9136f3d4f05ae30fd375eb4400cd32493529972927' },
    },
  },
};

const sha256 = (buf) => crypto.createHash('sha256').update(buf).digest('hex');

// The bridge program for this computer, downloaded if it isn't here yet. Resolves to its path.
export async function ensureBridge(kind, log = console.log) {
  const b = BRIDGES[kind];
  const f = b?.files[process.platform];
  if (!f || process.arch !== 'x64') throw new Error(`The ${b?.name || kind} bridge isn't available for this computer (${process.platform} ${process.arch})`);
  const file = path.join(BRIDGE_DIR, f.file);
  try {
    if (sha256(fs.readFileSync(file)) === f.sha256) return file;
  } catch { /* not there yet */ }
  const url = `https://github.com/${b.repo}/releases/download/${b.version}/${f.file}`;
  log(`Downloading the ${b.name} bridge (${b.program} ${b.version})…`);
  const res = await fetch(url, { redirect: 'follow' });
  if (!res.ok) throw new Error(`Download failed (${res.status})`);
  const buf = Buffer.from(await res.arrayBuffer());
  if (sha256(buf) !== f.sha256) throw new Error('The downloaded bridge failed its checksum');
  fs.mkdirSync(BRIDGE_DIR, { recursive: true });
  fs.writeFileSync(file + '.part', buf, { mode: 0o755 });
  fs.renameSync(file + '.part', file);
  return file;
}

// What a bridge's progress lines mean for the player ("tenover-frp: searching for Garmin R10...")
function readLine(line) {
  const t = line.replace(/^\S+-frp:\s*/, '');
  if (/^(searching|scanning)/i.test(t)) return { state: 'searching', text: 'Searching over Bluetooth…' };
  if (/^connected/i.test(t)) return { state: 'connecting', text: 'Found it, connecting…' };
  if (/^(registered|handshake complete|model )/i.test(t)) return { state: 'connecting', text: 'Setting up…' };
  if (/^(ready|armed)/i.test(t)) return { state: 'ready', text: 'ready' };
  if (/^(connection failed|failed to connect)/i.test(t)) return { state: 'searching', text: 'Not found: is it on, and (for the R10) paired in Windows Bluetooth settings?', error: t };
  if (/failed to open FRP endpoint/i.test(t)) return { state: 'error', text: 'Its port is in use by another program', error: t };
  if (/^(transport error|poll error|device error)/i.test(t)) return { state: 'searching', text: 'Bluetooth connection lost, reconnecting…', error: t };
  return null;
}

// Runs one bridge program, restarting it (after a pause) whenever it stops, e.g. when no device
// was found. Emits 'status' ({ state, text, error }) and 'log' lines.
export class BridgeProcess extends EventEmitter {
  // (command: run this instead of the downloaded bridge, [program, ...its first arguments], for tests)
  constructor(kind, { port, args = [], log = console.log, command = null } = {}) {
    super();
    this.command = command;
    this.kind = kind;
    this.port = port;
    this.args = args;
    this.log = log;
    this.proc = null;
    this.running = false;
    this.timer = null;
    this.status = { state: 'off', text: 'Not connected', error: null };
  }

  get url() { return `ws://127.0.0.1:${this.port}/frp`; }

  setStatus(patch) {
    Object.assign(this.status, patch);
    this.emit('status', { ...this.status });
  }

  async start() {
    if (this.running) return;
    this.running = true;
    this.launch();
  }

  async launch() {
    if (!this.running) return;
    const b = BRIDGES[this.kind];
    let exe;
    try {
      this.setStatus({ state: 'starting', text: `Starting the ${b.name} bridge…`, error: null });
      exe = this.command ? this.command[0] : await ensureBridge(this.kind, (m) => { this.log(m); this.setStatus({ text: m }); });
    } catch (err) {
      this.log(`${b.name}: ${err.message}`);
      this.setStatus({ state: 'error', text: `Couldn't get the ${b.name} bridge`, error: err.message });
      this.timer = setTimeout(() => this.launch(), 30000);
      return;
    }
    if (!this.running) return;
    const proc = spawn(exe, [...(this.command?.slice(1) || []), `127.0.0.1:${this.port}`, ...this.args], { windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
    this.proc = proc;
    this.log(`${b.name}: started ${path.basename(exe)} on port ${this.port}`);
    let buf = '';
    const onData = (d) => {
      buf += d;
      let i;
      while ((i = buf.indexOf('\n')) >= 0) {
        const line = buf.slice(0, i).trim();
        buf = buf.slice(i + 1);
        if (!line) continue;
        this.emit('log', line);
        const s = readLine(line);
        if (s) this.setStatus({ error: null, ...s });
      }
    };
    proc.stdout.setEncoding('utf8').on('data', onData);
    proc.stderr.setEncoding('utf8').on('data', onData);
    proc.on('error', (err) => {
      this.log(`${b.name}: couldn't start the bridge (${err.message})`);
      this.setStatus({ state: 'error', text: "Couldn't start the bridge", error: err.message });
    });
    proc.on('exit', (code) => {
      if (this.proc !== proc) return;
      this.proc = null;
      if (!this.running) return;
      this.log(`${b.name}: bridge stopped (${code}), trying again shortly`);
      if (this.status.state !== 'error' && this.status.state !== 'searching') this.setStatus({ state: 'searching', text: 'Connection lost, reconnecting…' });
      this.timer = setTimeout(() => this.launch(), 4000);
    });
  }

  stop() {
    this.running = false;
    clearTimeout(this.timer);
    const p = this.proc;
    this.proc = null;
    try { p?.kill(); } catch { /* gone */ }
    this.setStatus({ state: 'off', text: 'Not connected', error: null });
  }

  // Start again with other arguments (the Square Golf bridge arms for one club)
  restart(args) {
    this.args = args;
    if (!this.running) return;
    clearTimeout(this.timer);
    const p = this.proc;
    this.proc = null;
    try { p?.kill(); } catch { /* gone */ }
    this.timer = setTimeout(() => this.launch(), 300);
  }
}
