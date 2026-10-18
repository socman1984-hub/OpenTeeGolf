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
  card.scrollIntoView({ behavior: 'smooth', block: 'start' });
  try {
    const r = await api('/api/builder/build', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        osmType: c.osmType, osmId: c.osmId, name: $('b-name').value.trim(), location: current?.location || '',
        group: $('b-group') ? $('b-group').value : null,
      }),
    });
    job = r.jobId;
  } catch (err) {
    logLine(err.message, 'err');
    $('b-build').disabled = false;
  }
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
events.addEventListener('build', (e) => {
  const m = JSON.parse(e.data);
  if (m.jobId !== job) return;
  if (m.msg) logLine(m.msg);
  if (m.error) {
    logLine(`Build failed: ${m.error}`, 'err');
    if ($('b-build')) $('b-build').disabled = false;
  }
  if (m.done) {
    logLine(m.needsReview ? 'Done — some holes were guessed. Open the editor to check tees, pins and par.' : 'Done! The course is ready to play.', 'ok');
    $('play-link').href = playUrl(m.courseId);
    $('edit-link').href = editUrl(m.courseId);
    $('build-done').classList.remove('hidden');
    if ($('b-build')) $('b-build').disabled = false;
    loadMine();
  }
});

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
      <div class="actions"><a class="button primary" href="${playUrl(c.id)}">Play</a><a class="button" href="${editUrl(c.id)}">Edit</a><button>Delete</button></div>`;
    el.querySelector('button').onclick = async () => {
      if (!confirm(`Delete ${c.name}? This removes the built course files.`)) return;
      await api(`/api/courses/${c.id}`, { method: 'DELETE' });
      loadMine();
    };
    box.appendChild(el);
  }
}
loadMine().catch((err) => { $('mine').innerHTML = `<div class="muted">${esc(err.message)}</div>`; });
