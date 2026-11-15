// The course queue (helper mode, for the Unreal game): courses to build, update or put in the game,
// done in the background while the player plays. Builds (downloading map data, heights and aerial
// photos - the network's work) run two at a time; getting a course ready for the game (the hidden
// browser's export - the graphics card's work) one at a time, the next course building meanwhile.
// Kept in <data>/queue.json: what's left carries on the next time the game starts.
//
// An item: { id, kind: 'build' | 'update' | 'export', name, args (a build's: osmType, osmId, group,
// name, location, surface), courseId, stage: 'build' | 'export', state: 'waiting' | 'run' | 'done' |
// 'failed', msg, frac, log: [..], added, started, ended, needsReview }

import fs from 'node:fs';
import path from 'node:path';

const BUILDS_AT_ONCE = 2;
const KEEP_FINISHED = 30;

let items = [], file = null, deps = null, saveTimer = null;

// deps: { build(args, log) -> { courseId, needsReview }, update(courseId, log) -> { courseId },
// exportCourse(courseId, onProgress({ frac, msg })) -> Promise, exportBusy() -> bool, cancelExport() }
export function initQueue(queueFile, d) {
  file = queueFile;
  deps = d;
  try {
    items = JSON.parse(fs.readFileSync(file, 'utf8')).items || [];
    // (what was going on when the game closed: from the start of its step again)
    for (const it of items) {
      if (it.state !== 'run') continue;
      it.state = 'waiting';
      it.frac = 0;
      it.msg = 'Carrying on where it stopped…';
    }
  } catch { items = []; }
  pump();
  // (and now and then: an export started some other way has finished, freeing its slot)
  setInterval(pump, 5000).unref();
}

function save() {
  if (!file || saveTimer) return;
  saveTimer = setTimeout(() => {
    saveTimer = null;
    try {
      fs.mkdirSync(path.dirname(file), { recursive: true });
      fs.writeFileSync(file + '.part', JSON.stringify({ items }));
      fs.renameSync(file + '.part', file);
    } catch (err) { console.error('[queue] not saved:', err.message); }
  }, 500);
}

function note(it, msg, extra = {}) {
  Object.assign(it, extra);
  if (msg) {
    it.msg = String(msg).slice(0, 300);
    // (an export's "…N%" messages: the last of each step kept, not every percent)
    const stem = (m) => m.split('…')[0];
    if (it.log.length && stem(it.log[it.log.length - 1]) === stem(it.msg)) it.log[it.log.length - 1] = it.msg;
    else it.log.push(it.msg);
    if (it.log.length > 40) it.log.shift();
  }
  save();
}

// What the game sees: each item, waiting ones saying where they are in line
export function queueState() {
  let b = 0, e = 0;
  return {
    items: items.map((it) => {
      const o = { ...it, seconds: it.started ? Math.round(((it.ended || Date.now()) - it.started) / 1000) : 0 };
      if (it.state === 'waiting' && !(it.msg || '').startsWith('Carrying on')) {
        const n = it.stage === 'export' ? ++e : ++b;
        const ord = n === 2 ? '2nd' : n === 3 ? '3rd' : `${n}th`;
        o.msg = it.stage === 'export' ? (n === 1 ? 'Built: next to be got ready for the game' : `Built: waiting to be got ready for the game (${ord} in line)`)
          : n === 1 ? 'Next to start' : `Waiting (${ord} in line)`;
      }
      return o;
    }),
  };
}

// Add a course: { kind, name, args, courseId }
export function addToQueue({ kind, name, args, courseId }) {
  if (!['build', 'update', 'export'].includes(kind)) return { error: 'No such kind of job' };
  // (the same course already waiting or going: not twice)
  const same = items.find((it) => (it.state === 'waiting' || it.state === 'run') && it.kind === kind
    && (kind === 'build' ? it.args?.osmId === args?.osmId : it.courseId === courseId));
  if (same) return { ok: true, item: same, already: true };
  const it = {
    id: Date.now().toString(36) + Math.random().toString(36).slice(2, 6), kind, name: name || courseId || 'A course',
    args: kind === 'build' ? args : undefined, courseId: kind === 'build' ? null : courseId,
    stage: kind === 'export' ? 'export' : 'build', state: 'waiting', msg: '', frac: 0, log: [], added: Date.now(), started: 0, ended: 0,
  };
  items.push(it);
  // (the finished ones beyond the last few: forgotten)
  const done = items.filter((x) => x.state === 'done' || x.state === 'failed');
  for (const x of done.slice(0, Math.max(0, done.length - KEEP_FINISHED))) items.splice(items.indexOf(x), 1);
  console.log(`[queue] added ${kind} of ${it.name}`);
  save();
  pump();
  return { ok: true, item: it };
}

// Take one out: a waiting one goes; one getting ready for the game is stopped; a build going on
// can't be stopped mid-way, so it's marked to stop when its step ends. A finished one is cleared.
export function removeFromQueue(id) {
  const it = items.find((x) => x.id === id);
  if (!it) return { error: 'Not in the queue' };
  if (it.state === 'run' && it.stage === 'export') { it.cancelled = true; deps.cancelExport(); return { ok: true }; }
  if (it.state === 'run') { it.cancelled = true; note(it, 'Stopping after this step…'); return { ok: true }; }
  items.splice(items.indexOf(it), 1);
  save();
  return { ok: true };
}

// Try a failed one again (from the step that failed)
export function retryInQueue(id) {
  const it = items.find((x) => x.id === id);
  if (!it || it.state !== 'failed') return { error: 'Nothing to try again' };
  Object.assign(it, { state: 'waiting', msg: '', frac: 0, ended: 0, error: undefined, cancelled: false });
  save();
  pump();
  return { ok: true };
}

export function clearFinished() {
  items = items.filter((x) => x.state === 'waiting' || x.state === 'run');
  save();
  return { ok: true };
}

function fail(it, msg) {
  note(it, msg, { state: 'failed', error: msg, ended: Date.now() });
  console.log(`[queue] ${it.kind} of ${it.name} failed: ${msg}`);
}

// Start what can start
function pump() {
  if (!deps) return;
  let building = items.filter((x) => x.state === 'run' && x.stage === 'build').length;
  for (const it of items) {
    if (building >= BUILDS_AT_ONCE) break;
    if (it.state !== 'waiting' || it.stage !== 'build') continue;
    building++;
    runBuild(it);
  }
  if (!items.some((x) => x.state === 'run' && x.stage === 'export') && !deps.exportBusy()) {
    const it = items.find((x) => x.state === 'waiting' && x.stage === 'export');
    if (it) runExport(it);
  }
}

async function runBuild(it) {
  note(it, it.kind === 'update' ? 'Starting the update…' : 'Starting the build…', { state: 'run', started: it.started || Date.now(), frac: 0 });
  const log = (msg) => { if (msg) note(it, msg); };
  try {
    const r = it.kind === 'update' ? await deps.update(it.courseId, log) : await deps.build(it.args, log);
    it.courseId = r.courseId;
    it.needsReview = !!r.needsReview;
    if (it.cancelled) fail(it, 'Stopped');
    else if (r.needsReview) note(it, 'Built, but OpenStreetMap has no holes mapped there, so they were guessed: look them over in the editor, then put it in the game', { state: 'done', ended: Date.now() });
    else {
      // (smaller guesses - pars from each hole's length, a green drawn in: said, and on it goes)
      const g = (r.guessed || []).map((k) => (k === 'pars' ? 'some pars (from the hole lengths)' : k === 'greens' ? 'a green or two' : k));
      it.guessNote = g.length ? `${g.join(' and ')} guessed: change them in the editor if they're wrong` : '';
      note(it, (it.kind === 'update' ? 'Updated' : 'Built') + (g.length ? `: ${it.guessNote}` : ''), { stage: 'export', state: 'waiting', frac: 0 });
    }
  } catch (err) {
    fail(it, err.message || String(err));
  }
  pump();
}

async function runExport(it) {
  note(it, 'Getting it ready for the game…', { state: 'run', stage: 'export', started: it.started || Date.now(), frac: 0 });
  try {
    await deps.exportCourse(it.courseId, ({ frac, msg }) => note(it, msg, { frac: Math.max(it.frac, Math.min(1, +frac || 0)) }));
    note(it, it.guessNote ? `In the game (${it.guessNote})` : 'In the game', { state: 'done', frac: 1, ended: Date.now() });
    console.log(`[queue] ${it.name} is in the game`);
  } catch (err) {
    fail(it, it.cancelled ? 'Stopped' : err.message || String(err));
  }
  pump();
}

