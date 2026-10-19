// Course builder page: search OpenStreetMap, check a course's mapping, build it.
const $ = (id) => document.getElementById(id);
const esc = (s) => String(s ?? '').replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[c]);
const playUrl = (id) => `/?course=${encodeURIComponent(id)}`;
const editUrl = (id) => `/editor.html?course=${encodeURIComponent(id)}`;

let current = null; // selected search result
let job = null;

async function api(url, opts) {
  const res = await fetch(url, opts);
  const body = await res.json().catch(() => ({}));
  if (!res.ok) throw new Error(body.error || `Request failed (${res.status})`);
  return body;
}

// ------------------------------------------------------------------ search

$('search-form').onsubmit = async (e) => {
  e.preventDefault();
  const q = $('q').value.trim();
  const box = $('results');
  box.innerHTML = '<div class="muted"><span class="spin"></span>Searching OpenStreetMap…</div>';
  $('search-btn').disabled = true;
  try {
    const { results } = await api(`/api/builder/search?q=${encodeURIComponent(q)}`);
    if (!results.length) {
      box.innerHTML = `<div class="muted">No golf course called “${esc(q)}” was found in OpenStreetMap. Try the club's full name, or add the town (e.g. “Muirfield Village Dublin Ohio”).</div>`;
      return;
    }
    box.innerHTML = '';
    for (const r of results) {
      const el = document.createElement('div');
      el.className = 'result';
      el.innerHTML = `<div class="info"><b>${esc(r.name)}${r.built ? '<span class="badge good">built</span>' : ''}</b><small>${esc(r.location)}</small></div>
        <div class="actions">
          ${r.built ? `<a class="button" href="${playUrl(r.built)}">Play</a><a class="button" href="${editUrl(r.built)}">Edit</a>` : ''}
          <button class="primary">${r.built ? 'Rebuild…' : 'Check'}</button>
        </div>`;
      el.querySelector('button').onclick = () => check(r);
      box.appendChild(el);
    }
  } catch (err) {
    box.innerHTML = `<div class="muted">Search failed: ${esc(err.message)}</div>`;
  } finally {
    $('search-btn').disabled = false;
  }
};

// ------------------------------------------------------------------ check

async function check(r) {
  current = r;
  const card = $('check-card'), out = $('check');
  card.classList.remove('hidden');
  out.innerHTML = `<div class="muted"><span class="spin"></span>Checking what's mapped for ${esc(r.name)}… (first time can take up to a minute)</div>`;
  card.scrollIntoView({ behavior: 'smooth', block: 'start' });
  try {
    const c = await api(`/api/builder/check?type=${r.osmType}&id=${r.osmId}`);
    const stat = (n, label) => `<div class="stat"><b>${n}</b><small>${label}</small></div>`;
    const groups = c.groups.length > 1
      ? `<label>Which course<select id="b-group">${c.groups.map((g) => `<option value="${esc(g.name)}">${esc(g.name || 'Main course')} — ${g.holes} holes${g.par ? `, par ${g.par}` : ''}</option>`).join('')}</select></label>`
      : '';
    out.innerHTML = `
      <div><b>${esc(c.name)}</b> <span class="badge ${c.quality}">${{ good: 'well mapped', partial: 'partly mapped', poor: 'barely mapped' }[c.quality]}</span></div>
      <div class="stats">
        ${stat(c.holes, 'hole lines')}${stat(c.greens, 'greens')}${stat(c.fairways, 'fairways')}
        ${stat(c.bunkers, 'bunkers')}${stat(c.tees, 'tee boxes')}${stat(c.water, 'water')}
      </div>
      <ul class="notes">${c.notes.map((n) => `<li>${esc(n)}</li>`).join('')}</ul>
      <div class="form-row">
        <label>Course name<input id="b-name" value="${esc(c.groups.length > 1 && c.groups[0].name ? `${c.name} – ${c.groups[0].name}` : c.name)}"></label>
        ${groups}
      </div>
      <button class="primary" id="b-build">Build course</button>`;
    if ($('b-group')) {
      $('b-group').onchange = () => {
        const g = $('b-group').value;
        $('b-name').value = g ? `${c.name} – ${g}` : c.name;
      };
    }
    $('b-build').onclick = () => build(c);
  } catch (err) {
    out.innerHTML = `<div class="muted">Couldn't check this course: ${esc(err.message)}</div>`;
  }
}

// ------------------------------------------------------------------ build

async function build(c) {
  $('b-build').disabled = true;
  const card = $('build-card');
  card.classList.remove('hidden');
  $('build-done').classList.add('hidden');
  $('log').innerHTML = '';
  logLine('Starting…');
  startProgress(BUILD_STEPS);
  card.scrollIntoView({ behavior: 'smooth', block: 'start' });
  try {
    await startJob(api('/api/builder/build', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        osmType: c.osmType, osmId: c.osmId, name: $('b-name').value.trim(), location: current?.location || '',
        group: $('b-group') ? $('b-group').value : null,
      }),
    }));
  } catch (err) {
    logLine(err.message, 'err');
    endProgress(false);
    $('b-build').disabled = false;
  }
}

// ------------------------------------------------------------------ build progress
// The server names each step as it starts ({ step }, and for downloads { at, total, unit });
// the page shows them as a checklist with an overall bar and a running clock, so a slow download
// visibly isn't stuck.

const BUILD_STEPS = [
  ['boundary', 'Finding the course in OpenStreetMap'],
  ['features', 'Downloading greens, fairways, bunkers, tees and water'],
  ['holes', 'Laying out the holes'],
  ['elevation', 'Downloading elevation'],
  ['terrain', 'Building the terrain'],
];
const UPDATE_STEPS = [...BUILD_STEPS, ['paths', 'Cart paths'], ['buildings', 'Buildings'], ['parking', 'Car parks'], ['roads', 'Roads']];
let prog = null;

function startProgress(steps) {
  prog = { steps: steps.map(([id, label]) => ({ id, label, state: 'todo', detail: '', frac: 0, t0: 0, t1: 0 })), t0: Date.now(), end: null };
  $('progress').classList.remove('hidden', 'finished', 'failed');
  clearInterval(startProgress.timer);
  startProgress.timer = setInterval(renderProgress, 1000);
  renderProgress();
}

function progressStep(m) {
  if (!prog) return;
  let i = prog.steps.findIndex((s) => s.id === m.step);
  if (i < 0) { prog.steps.push({ id: m.step, label: m.step, state: 'todo', detail: '', frac: 0, t0: 0, t1: 0 }); i = prog.steps.length - 1; }
  // (starting a step finishes the ones before it, even any the server skipped)
  prog.steps.forEach((s, k) => { if (k < i && s.state !== 'done') { s.state = 'done'; s.t1 ||= Date.now(); } });
  const s = prog.steps[i];
  if (s.state !== 'active') { s.state = 'active'; s.t0 = Date.now(); s.detail = ''; s.frac = 0; }
  if (m.total) {
    s.frac = Math.min(1, m.at / m.total);
    s.detail = m.unit === 'bytes' ? `${(m.at / 1048576).toFixed(1)} of ${(m.total / 1048576).toFixed(1)} MB`
      : m.unit === 'tiles' ? `${m.at} of ${m.total} tiles` : `${m.at} of ${m.total}`;
  }
  renderProgress();
}

function progressNote(msg) {
  // The latest message shows under the step it belongs to (e.g. "server busy — retrying")
  const s = prog?.steps.find((x) => x.state === 'active');
  if (s && msg && !/^(Updating|Downloading course data)/.test(msg)) { s.detail = msg.replace(/…$/, ''); renderProgress(); }
}

function endProgress(ok) {
  if (!prog) return;
  clearInterval(startProgress.timer);
  prog.end = Date.now();
  for (const s of prog.steps) {
    if (ok) { if (s.state !== 'done') { s.state = 'done'; s.t1 ||= prog.end; } }
    else if (s.state === 'active') { s.state = 'failed'; s.t1 = prog.end; }
  }
  $('progress').classList.add(ok ? 'finished' : 'failed');
  renderProgress();
}

const secs = (ms) => { const t = Math.round(ms / 1000); return t < 60 ? `${t} s` : `${Math.floor(t / 60)} min ${String(t % 60).padStart(2, '0')} s`; };
function renderProgress() {
  if (!prog) return;
  const now = prog.end || Date.now(), n = prog.steps.length;
  const doneN = prog.steps.filter((s) => s.state === 'done').length, act = prog.steps.find((s) => s.state === 'active');
  const pct = Math.round(((doneN + (act ? act.frac : 0)) / n) * 100);
  const title = prog.end ? ($('progress').classList.contains('failed') ? 'Stopped' : 'Finished') : act ? `Step ${prog.steps.indexOf(act) + 1} of ${n}` : 'Starting';
  // (a step running long is usually the public OpenStreetMap servers being busy)
  const slow = act && !act.frac && now - act.t0 > 25000 && !act.detail ? 'Still working — the public map servers can take a minute or two when they’re busy' : '';
  $('progress').innerHTML = `
    <div class="bar"><i style="width:${prog.end && doneN === n ? 100 : pct}%"></i><b></b></div>
    <div class="summary"><span>${title}</span><span>${secs(now - prog.t0)}</span></div>
    <ol>${prog.steps.map((s) => `<li class="${s.state}"><span class="mark"></span>
      <span>${esc(s.label)}${s.state === 'active' && (s.detail || slow) ? `<span class="detail">${esc(s.detail || slow)}</span>` : ''}</span>
      <span class="time">${s.state === 'active' ? secs(now - s.t0) : s.t1 && s.t0 ? secs(s.t1 - s.t0) : ''}</span></li>`).join('')}</ol>`;
}

function logLine(text, cls = '') {
  const d = document.createElement('div');
  d.textContent = text;
  if (cls) d.className = cls;
  $('log').appendChild(d);
  $('log').scrollTop = 1e9;
}

const events = new EventSource('/api/events');
// Progress while checking (e.g. "OpenStreetMap server busy, retrying…")
events.addEventListener('check', (e) => {
  const m = JSON.parse(e.data);
  const line = document.querySelector('#check .muted');
  if (current && m.osmId === +current.osmId && line && line.querySelector('.spin')) {
    line.innerHTML = `<span class="spin"></span>${esc(m.msg)}`;
  }
});
// (a job's first events can arrive before the request that started it has answered with its id:
// they're held until it does)
let early = [];
const startJob = async (request) => {
  job = 'pending'; early = [];
  try { job = (await request).jobId; } catch (err) { job = null; throw err; }
  for (const m of early) if (m.jobId === job) onBuild(m);
  early = [];
};
events.addEventListener('build', (e) => {
  const m = JSON.parse(e.data);
  if (job === 'pending') early.push(m);
  else if (m.jobId === job) onBuild(m);
});
function onBuild(m) {
  if (m.step) progressStep(m);
  if (m.msg) { logLine(m.msg); if (!m.step) progressNote(m.msg); }
  if (m.error) {
    logLine(`Build failed: ${m.error}`, 'err');
    endProgress(false);
    $('log-box').open = true;
    if ($('b-build')) $('b-build').disabled = false;
    for (const b of document.querySelectorAll('#mine .update')) b.disabled = false;
  }
  if (m.done) {
    endProgress(true);
    logLine(m.needsReview ? 'Done — some holes were guessed. Open the editor to check tees, pins and par.' : 'Done! The course is ready to play.', 'ok');
    $('play-link').href = playUrl(m.courseId);
    $('edit-link').href = editUrl(m.courseId);
    $('build-done').classList.remove('hidden');
    if ($('b-build')) $('b-build').disabled = false;
    loadMine();
  }
}

// ------------------------------------------------------------------ your courses

async function loadMine() {
  const box = $('mine');
  const { courses } = await api('/api/courses');
  const mine = courses.filter((c) => c.editable);
  if (!mine.length) { box.innerHTML = '<div class="muted">Courses you build appear here.</div>'; return; }
  box.innerHTML = '';
  for (const c of mine) {
    const el = document.createElement('div');
    el.className = 'result';
    el.innerHTML = `<div class="info"><b>${esc(c.name)}${c.needsReview ? '<span class="badge partial">needs review</span>' : ''}</b>
      <small>${esc(c.location)} · ${c.holes} holes · par ${c.par}</small></div>
      <div class="actions"><a class="button primary" href="${playUrl(c.id)}">Play</a><a class="button" href="${editUrl(c.id)}">Edit</a>${c.osmId ? '<button class="update" title="Download this course from OpenStreetMap again and rebuild it">Update from OpenStreetMap</button>' : ''}<button class="delete">Delete</button></div>`;
    el.querySelector('.update')?.addEventListener('click', () => updateCourse(c));
    el.querySelector('.delete').onclick = async () => {
      if (!confirm(`Delete ${c.name}? This removes the built course files.`)) return;
      await api(`/api/courses/${c.id}`, { method: 'DELETE' });
      loadMine();
    };
    box.appendChild(el);
  }
}
loadMine().then(() => {
  // From the main menu's "Update map" link
  const id = new URLSearchParams(location.search).get('update');
  if (id) api('/api/courses').then(({ courses }) => { const c = courses.find((x) => x.id === id); if (c) updateCourse(c); });
}).catch((err) => { $('mine').innerHTML = `<div class="muted">${esc(err.message)}</div>`; });

// Rebuild a course from fresh OpenStreetMap data (greens, fairways, bunkers, water, rivers, trees,
// cart paths, buildings), in place
async function updateCourse(c) {
  if (!confirm(`Update ${c.name} from OpenStreetMap?\n\nEverything mapped is downloaded again and the course is rebuilt. Changes you made in the course editor (tees, pins, par, shapes) will be replaced by what's in OpenStreetMap now.`)) return;
  $('build-card').classList.remove('hidden');
  $('build-done').classList.add('hidden');
  $('log').innerHTML = '';
  logLine(`Updating ${c.name} from OpenStreetMap…`);
  startProgress(UPDATE_STEPS);
  $('build-card').scrollIntoView({ behavior: 'smooth', block: 'start' });
  for (const b of document.querySelectorAll('#mine .update')) b.disabled = true;
  try {
    await startJob(api(`/api/courses/${c.id}/update`, { method: 'POST' }));
  } catch (err) {
    logLine(err.message, 'err');
    endProgress(false);
    for (const b of document.querySelectorAll('#mine .update')) b.disabled = false;
  }
}
