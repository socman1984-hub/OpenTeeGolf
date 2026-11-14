// Building a course for the OpenTeeGolf Unreal game from the menu (main.js "Export to Unreal"): once
// the game has written the course's export (exports/<id>/, public/js/export-unreal.js), the server
// runs unreal/make_pack.ps1 <id> - Unreal headless, 5 to 10 minutes - and, if asked, then
// unreal/make_release.ps1, which packages the double-click game with every course built so far.
// One job at a time; the menu asks for its state every few seconds.

import { spawn } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';

export class UnrealBuild {
  constructor(root) {
    this.root = root;
    this.job = null; // { kind: 'pack' | 'release', course, state: 'running' | 'done' | 'failed', lines, started, ended, code }
  }

  busy() { return this.job?.state === 'running'; }

  // kind 'pack': make_pack.ps1 <course>; 'release': make_release.ps1
  start(kind, course) {
    if (this.busy()) throw new Error(`Already building (${this.job.kind}${this.job.course ? ` of ${this.job.course}` : ''})`);
    if (kind === 'pack' && !fs.existsSync(path.join(this.root, 'exports', course, 'play.json'))) throw new Error('The course has not been exported yet');
    const script = path.join(this.root, 'unreal', kind === 'pack' ? 'make_pack.ps1' : 'make_release.ps1');
    // (scripts are blocked on a stock Windows: allowed for this run only)
    const args = ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', script, ...(kind === 'pack' ? [course] : [])];
    const job = { kind, course: course || null, state: 'running', lines: [], started: Date.now(), ended: null, code: null };
    this.job = job;
    const child = spawn('powershell.exe', args, { cwd: this.root, windowsHide: true });
    const take = (buf) => {
      for (const line of buf.toString().split(/\r?\n/)) {
        const t = line.trim();
        if (t) { job.lines.push(t); if (job.lines.length > 300) job.lines.shift(); }
      }
    };
    child.stdout.on('data', take);
    child.stderr.on('data', take);
    child.on('error', (err) => { job.lines.push(String(err.message || err)); job.state = 'failed'; job.ended = Date.now(); });
    child.on('close', (code) => {
      job.code = code;
      job.ended = Date.now();
      job.state = code === 0 ? 'done' : 'failed';
      console.log(`[unreal] ${kind}${course ? ` ${course}` : ''} ${job.state} (exit ${code})`);
    });
    console.log(`[unreal] ${kind}${course ? ` ${course}` : ''} started`);
    return this.state();
  }

  // What the menu shows: the job, how long it's taken, and what it's doing now - the script's last
  // line, or while Unreal builds the course, the last step from its log
  state() {
    const j = this.job;
    if (!j) return { state: 'idle' };
    let now = j.lines[j.lines.length - 1] || '';
    if (j.kind === 'pack' && j.state === 'running') {
      try {
        // (Unreal's own log, written as it goes: the course's pack log only comes at the end)
        const file = path.join(this.root, 'unreal', 'OpenTeeGolf', 'Saved', 'Logs', 'OpenTeeGolf.log');
        if (fs.statSync(file).mtimeMs < j.started) throw new Error('an earlier build\'s log');
        const log = fs.readFileSync(file, 'utf8');
        const steps = log.match(/\[OpenTeeGolf\][^\r\n]*/g);
        if (steps?.length) now = steps[steps.length - 1].replace('[OpenTeeGolf]', '').trim();
        else now = 'Starting Unreal…';
      } catch { /* (not started yet) */ }
    }
    return {
      state: j.state, kind: j.kind, course: j.course, now: j.state === 'running' ? now.slice(0, 200) : '',
      seconds: Math.round(((j.ended || Date.now()) - j.started) / 1000),
      tail: j.state === 'failed' ? j.lines.slice(-12) : undefined,
      // (the script's recent lines: the export screen tells the release's stages from them)
      recent: j.state === 'running' ? j.lines.slice(-40) : undefined,
    };
  }
}
