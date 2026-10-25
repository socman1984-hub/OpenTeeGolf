// Course builder page: search OpenStreetMap, check a course's mapping, build it.
const $ = (id) => document.getElementById(id);
const esc = (s) => String(s ?? '').replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[c]);
const playUrl = (id) => `/?course=${encodeURIComponent(id)}`;
const editUrl = (id) => `/editor.html?course=${encodeURIComponent(id)}`;

let current = null; // selected search result
let job = null;
let jobKind = 'build'; // 'build' (a new course: on to the editor's tree review) or 'update'

async function api(url, opts) {
  const res = await fetch(url, opts);
  const body = await res.json().catch(() => ({}));
  if (!res.ok) throw new Error(body.error || `Request failed (${res.status})`);
  return body;
}

// ------------------------------------------------------------------ map
// Every golf course in OpenStreetMap (blue, gathered into numbered clusters until you zoom in), the
// ones you've built (green, yellow-ringed) and the ones a search finds (orange), on a street map or
// the satellite view. Click a course to check and build it; a search zooms the map to what it
// found. A pin's popup has the same buttons as its row in the list; hovering a row lights up its pin.

const map = L.map('course-map', { worldCopyJump: true, zoomControl: true }).setView([38, -40], 2);
const streets = L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png', { maxZoom: 19, attribution: '© OpenStreetMap contributors' });
const satellite = L.tileLayer('https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}', { maxZoom: 19, attribution: 'Imagery © Esri, Maxar, Earthstar Geographics' });
streets.addTo(map);
L.control.layers({ Map: streets, Satellite: satellite }, null, { position: 'topright' }).addTo(map);
const worldLayer = L.markerClusterGroup({
  chunkedLoading: true, showCoverageOnHover: false, maxClusterRadius: 60, disableClusteringAtZoom: 12,
  iconCreateFunction: (c) => { const n = c.getChildCount(), s = n < 100 ? 30 : n < 1000 ? 38 : 46; return L.divIcon({ className: '', html: `<div class="world-cluster" style="width:${s}px;height:${s}px">${n < 1000 ? n : `${Math.round(n / 100) / 10}k`}</div>`, iconSize: [s, s] }); },
}).addTo(map);
const builtLayer = L.layerGroup().addTo(map), foundLayer = L.layerGroup().addTo(map);
const pin = (built) => L.divIcon({ className: '', html: `<div class="course-pin${built ? ' built' : ' found'}"></div>`, iconSize: built ? [18, 18] : [14, 14], iconAnchor: built ? [9, 9] : [7, 7] });
// (a 14 px dot in a 28 px target, so it's easy to click)
const worldPin = L.divIcon({ className: 'world-hit', html: '<div class="course-pin world"></div>', iconSize: [28, 28], iconAnchor: [14, 14] });
const actionsHtml = (r) => r.built
  ? `<a class="button" href="${playUrl(r.built)}">Play</a><a class="button" href="${editUrl(r.built)}">Edit</a>${r.osmId ? '<button class="primary">Rebuild…</button>' : ''}`
  : '<button class="primary">Check</button>';
function addPin(layer, r) {
  const m = L.marker([r.lat, r.lon], { icon: pin(!!r.built), title: r.name, zIndexOffset: r.built ? 500 : 0 });
  const el = document.createElement('div');
  el.className = 'course-pop';
  el.innerHTML = `<b>${esc(r.name)}</b><small>${esc(r.location || '')}${r.built ? ' · built' : ''}</small><div class="actions">${actionsHtml(r)}</div>`;
  el.querySelector('button')?.addEventListener('click', () => { map.closePopup(); check(r); });
  m.bindPopup(el);
  m.on('click', () => rows.get(r.osmId)?.scrollIntoView({ behavior: 'smooth', block: 'nearest' }));
  layer.addLayer(m);
  return m;
}
const pins = new Map(), rows = new Map(); // (by OpenStreetMap id: the found courses' pins and list rows)
const hot = (id, on) => { pins.get(id)?.getElement()?.firstChild?.classList.toggle('hot', on); rows.get(id)?.classList.toggle('hot', on); };

let builtIds = new Set();
async function loadBuiltPins() {
  const { courses } = await api('/api/courses');
  builtLayer.clearLayers();
  const mine = courses.filter((c) => c.lat || c.lon);
  builtIds = new Set(courses.map((c) => c.osmId).filter(Boolean));
  for (const c of mine) addPin(builtLayer, { ...c, built: c.id });
}

// Every course in the world: a small popup, made when it's opened, with the Check button
const TYPES = { n: 'node', w: 'way', r: 'relation' };
async function loadWorld() {
  await builtReady;
  let have = 0;
  for (;;) {
    try {
      const w = await api(`/api/builder/world?from=${have}`);
      have += w.courses.length;
      const markers = [];
      for (const [lat, lon, t, id, name] of w.courses) {
        if (builtIds.has(id)) continue;
        const m = L.marker([lat, lon], { icon: worldPin, title: name || 'Unnamed golf course' });
        m.course = { name: name || 'Unnamed golf course', location: '', lat, lon, osmType: TYPES[t], osmId: id };
        markers.push(m);
      }
      if (markers.length) worldLayer.addLayers(markers);
      $('map-note').textContent = w.done
        ? `${have.toLocaleString()} courses · click one to build it, or search to narrow it down`
        : `Gathering every golf course in OpenStreetMap: ${have.toLocaleString()} so far (${Math.round(w.progress * 100)}% of the world; the first time takes a few minutes)`;
      if (w.done) return;
    } catch (err) {
      $('map-note').textContent = `Couldn't load the world's golf courses (${err.message}); search still works.`;
      return;
    }
    await new Promise((r) => setTimeout(r, 4000));
  }
}
worldLayer.on('click', (e) => {
  const r = e.layer.course;
  if (!r) return;
  const el = document.createElement('div');
  el.className = 'course-pop';
  el.innerHTML = `<b>${esc(r.name)}</b><small class="where">&nbsp;</small><div class="actions"><button class="primary">Check</button></div>`;
  el.querySelector('button').onclick = () => { map.closePopup(); check(r); };
  e.layer.bindPopup(el).openPopup();
  // (where it is, for the popup and the course's location)
  if (!r.location) api(`/api/builder/where?lat=${r.lat}&lon=${r.lon}`).then(({ location }) => { r.location = location; el.querySelector('.where').textContent = location; }).catch(() => {});
  else el.querySelector('.where').textContent = r.location;
});
const builtReady = loadBuiltPins().catch(() => {});
// (each kind of course can be hidden: just your own, say)
for (const [id, layer] of [['show-built', builtLayer], ['show-found', foundLayer], ['show-world', worldLayer]]) {
  $(id).onchange = () => { if ($(id).checked) map.addLayer(layer); else map.removeLayer(layer); };
}
loadWorld();

function showFound(results, { fit = true } = {}) {
  foundLayer.clearLayers(); pins.clear();
  // (a course you've built is shown by its own pin)
  for (const r of results) if (!r.built && Number.isFinite(r.lat)) pins.set(r.osmId, addPin(foundLayer, r));
  const pts = results.filter((r) => Number.isFinite(r.lat)).map((r) => [r.lat, r.lon]);
  if (fit && pts.length) map.fitBounds(L.latLngBounds(pts).pad(0.25), { maxZoom: 13 });
}

// ------------------------------------------------------------------ search

function renderResults(results, empty) {
  const box = $('results');
  rows.clear();
  if (!results.length) { box.innerHTML = `<div class="muted">${empty}</div>`; return; }
  box.innerHTML = '';
  for (const r of results) {
    const el = document.createElement('div');
    el.className = 'result';
    el.innerHTML = `<div class="info"><b>${esc(r.name)}${r.built ? '<span class="badge good">built</span>' : ''}</b><small>${esc(r.location)}</small></div>
      <div class="actions">
        <button class="show" title="Show it on the map">Map</button>
        ${r.built ? `<a class="button" href="${playUrl(r.built)}">Play</a><a class="button" href="${editUrl(r.built)}">Edit</a>` : ''}
        <button class="primary">${r.built ? 'Rebuild…' : 'Check'}</button>
      </div>`;
    el.querySelector('.primary').onclick = () => check(r);
    el.querySelector('.show').onclick = () => {
      if (!Number.isFinite(r.lat)) return;
      map.setView([r.lat, r.lon], 15);
      $('course-map').scrollIntoView({ behavior: 'smooth', block: 'nearest' });
      const m = pins.get(r.osmId) || builtLayer.getLayers().find((x) => x.options.title === r.name);
      m?.openPopup();
    };
    el.onmouseenter = () => hot(r.osmId, true);
    el.onmouseleave = () => hot(r.osmId, false);
    rows.set(r.osmId, el);
    box.appendChild(el);
  }
}

$('search-form').onsubmit = async (e) => {
  e.preventDefault();
  const q = $('q').value.trim();
  const box = $('results');
  box.innerHTML = '<div class="muted"><span class="spin"></span>Searching OpenStreetMap…</div>';
  $('search-btn').disabled = true;
  try {
    const { results } = await api(`/api/builder/search?q=${encodeURIComponent(q)}`);
    showFound(results);
    renderResults(results, `No golf course called “${esc(q)}” was found in OpenStreetMap. Try the club's full name, or add the town (e.g. “Muirfield Village Dublin Ohio”), or find it on the map.`);
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
        <label>Grass colours<select id="b-surface"><option value="game">Game colours (even, groomed)</option><option value="photo">Aerial photo colours</option></select></label>
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
  jobKind = 'build';
  card.scrollIntoView({ behavior: 'smooth', block: 'start' });
  try {
    await startJob(api('/api/builder/build', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        osmType: c.osmType, osmId: c.osmId, name: $('b-name').value.trim(), location: current?.location || '',
        group: $('b-group') ? $('b-group').value : null,
        surface: $('b-surface')?.value || 'game',
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
const UPDATE_STEPS = [...BUILD_STEPS, ['paths', 'Cart paths'], ['buildings', 'Buildings'], ['parking', 'Car parks'], ['roads', 'Roads'], ['imagery', 'Aerial photo (the clearest source)']];
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
    loadBuiltPins().catch(() => {});
    // A new course: straight on to the editor to check the trees found automatically that stand
    // in the line of play (and the holes, if any were guessed)
    if (jobKind === 'build') {
      logLine('Opening the editor to review the trees in the line of play…');
      setTimeout(() => { location.href = `${editUrl(m.courseId)}&review=trees`; }, 1500);
    }
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
      loadBuiltPins().catch(() => {});
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
  jobKind = 'update';
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
