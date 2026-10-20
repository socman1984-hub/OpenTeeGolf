// OpenTeeGolf game loop: course setup, receiving shots, rules and HUD.
import { buildCourse, TEE_TYPES } from './course.js';
import { smoothLine } from './roads.js';
import { CourseScene } from './scene.js';
import { simulateShot, LIE_EFFECT, stimpToRoll, SURFACE_NAMES } from './physics.js';
import { loadJolt, CollisionWorld } from './collision.js';
import { PRESETS as WEATHER, fetchLiveWeather, wetness, gustFor } from './weather.js';

const $ = (id) => document.getElementById(id);
const M_TO_YD = 1.09361, M_TO_FT = 3.28084;

const CLUBS = [
  ['DR', 'Driver', 230], ['W3', '3 Wood', 215], ['W5', '5 Wood', 200], ['H4', '4 Hybrid', 190],
  ['I4', '4 Iron', 180], ['I5', '5 Iron', 170], ['I6', '6 Iron', 160], ['I7', '7 Iron', 150],
  ['I8', '8 Iron', 140], ['I9', '9 Iron', 130], ['PW', 'Pitching Wedge', 118], ['GW', 'Gap Wedge', 105],
  ['SW', 'Sand Wedge', 90], ['LW', 'Lob Wedge', 70], ['PT', 'Putter', 0],
];

const S = {
  courses: [],
  selected: null,
  course: null,
  settings: {},
  hole: 0,
  strokes: 0,
  ball: null,
  prevBall: null,
  lie: 'tee',
  heading: 0,
  scores: [],
  phase: 'menu', // menu | ready | flying | holed | done
  anim: null,
  lastShot: null,
  undo: null,
  windRel: [],
};

const scene = new CourseScene($('view'));
scene.onFrame = (dt) => tick(dt);
scene.onDeviceLost = () => toast('The graphics card reset. Reload the page (F5) to keep playing.', 600000);

// ------------------------------------------------------------------ updates

// A newer release on GitHub: offered at the top of the menu (until "Later" for that version).
// Release copies install it themselves and restart; others link to the download.
const updates = { info: null, busy: false };
async function checkUpdates(force = false) {
  const r = await fetch(`/api/version${force ? '?check=1' : ''}`).then((x) => x.json()).catch(() => null);
  if (!r) return null;
  $('app-version').textContent = `OpenTeeGolf ${r.version}`;
  updates.info = r;
  const u = r.update;
  if (u?.available && (force || store.get('golfsim.skipVersion') !== u.latest)) showUpdate(r);
  return r;
}
function showUpdate(r) {
  const u = r.update;
  $('update-title').textContent = `OpenTeeGolf ${u.latest} is available`;
  $('update-sub').textContent = `You have ${r.version}${u.published ? ` · released ${new Date(u.published).toLocaleDateString()}` : ''}`;
  // Release notes, as plain text (commit titles)
  const notes = (u.notes || '').replace(/\*\*/g, '').replace(/^#+\s*/gm, '').trim();
  $('update-notes-text').textContent = notes;
  $('update-notes').classList.toggle('hidden', !notes);
  $('update-now').classList.toggle('hidden', !r.canInstall || !u.asset);
  $('update-download').classList.toggle('hidden', r.canInstall && !!u.asset);
  $('update-download').href = u.url;
  $('update-bar').classList.remove('hidden');
}
$('update-later').onclick = () => {
  if (updates.info?.update?.latest) store.set('golfsim.skipVersion', updates.info.update.latest);
  $('update-bar').classList.add('hidden');
};
$('check-updates').onclick = async (e) => {
  e.preventDefault();
  $('check-updates').textContent = 'Checking…';
  const r = await checkUpdates(true);
  $('check-updates').textContent = !r ? 'Couldn’t check' : r.update?.error ? 'Couldn’t reach GitHub' : r.update?.available ? 'Update available ↑' : 'You have the newest version';
};
$('update-now').onclick = async () => {
  const u = updates.info?.update;
  if (!u || updates.busy) return;
  if (!confirm(`Update OpenTeeGolf to ${u.latest}?\n\nIt downloads about ${Math.round((u.asset?.size || 0) / 1e6)} MB, then the game restarts. Your courses and settings are kept.`)) return;
  updates.busy = true;
  $('update-now').disabled = $('update-later').disabled = true;
  $('update-progress').classList.remove('hidden');
  $('update-sub').textContent = 'Starting…';
  const r = await fetch('/api/update/install', { method: 'POST' }).then((x) => x.json()).catch(() => ({ restarting: true }));
  if (r.error) {
    $('update-sub').textContent = r.error;
    $('update-now').disabled = $('update-later').disabled = false;
    updates.busy = false;
    return;
  }
  // The game restarts: wait for the new version to answer, then reload the page
  $('update-sub').textContent = `Restarting with ${u.latest}… this page reloads by itself`;
  $('update-fill').style.width = '100%';
  const t0 = Date.now();
  const poll = async () => {
    const v = await fetch('/api/version').then((x) => x.json()).catch(() => null);
    if (v?.version === u.latest) return location.reload();
    if (Date.now() - t0 > 180000) { $('update-sub').textContent = 'Taking longer than expected. If the game window closed, start it again with "Start OpenTeeGolf".'; return; }
    setTimeout(poll, 1500);
  };
  setTimeout(poll, 3000);
};
function updateEvent(m) {
  if (!updates.busy) return;
  if (m.stage === 'download' && m.total) {
    $('update-fill').style.width = `${Math.round((m.done / m.total) * 90)}%`;
    $('update-sub').textContent = `Downloading… ${Math.round(m.done / 1e6)} of ${Math.round(m.total / 1e6)} MB`;
  } else if (m.msg) $('update-sub').textContent = m.msg;
}
checkUpdates();

// ------------------------------------------------------------------ menu

// Per-viewer lists kept in the browser: favourite courses and when each was last played
const store = {
  get(key, def) { try { return JSON.parse(localStorage.getItem(key)) ?? def; } catch { return def; } },
  set(key, v) { try { localStorage.setItem(key, JSON.stringify(v)); } catch { /* storage unavailable */ } },
};
const menu = { favs: new Set(store.get('golfsim.favs', [])), played: store.get('golfsim.played', {}), favOnly: false, cards: new Map() };

// Lets a search for "ohio" find "Batavia, OH"
const US_STATES = { AL: 'alabama', AK: 'alaska', AZ: 'arizona', AR: 'arkansas', CA: 'california', CO: 'colorado', CT: 'connecticut', DE: 'delaware', FL: 'florida', GA: 'georgia', HI: 'hawaii', ID: 'idaho', IL: 'illinois', IN: 'indiana', IA: 'iowa', KS: 'kansas', KY: 'kentucky', LA: 'louisiana', ME: 'maine', MD: 'maryland', MA: 'massachusetts', MI: 'michigan', MN: 'minnesota', MS: 'mississippi', MO: 'missouri', MT: 'montana', NE: 'nebraska', NV: 'nevada', NH: 'new hampshire', NJ: 'new jersey', NM: 'new mexico', NY: 'new york', NC: 'north carolina', ND: 'north dakota', OH: 'ohio', OK: 'oklahoma', OR: 'oregon', PA: 'pennsylvania', RI: 'rhode island', SC: 'south carolina', SD: 'south dakota', TN: 'tennessee', TX: 'texas', UT: 'utah', VT: 'vermont', VA: 'virginia', WA: 'washington', WV: 'west virginia', WI: 'wisconsin', WY: 'wyoming' };
const searchText = (c) => `${c.name} ${c.location} ${c.designer} ${(c.location.match(/\b[A-Z]{2}\b/g) || []).map((a) => US_STATES[a] || '').join(' ')}`.toLowerCase();

async function loadCourses() {
  const r = await fetch('/api/courses').then((r) => r.json());
  S.courses = r.courses;
  menu.cards.clear();
  $('courses-dir').title = `Courses folder: ${r.dir}`;
  if (!r.courses.length) {
    $('course-tools').classList.add('hidden');
    $('course-list').innerHTML = `<div class="muted">No courses yet. <a href="/builder.html">Build one from OpenStreetMap</a>, and it will show up here.</div>`;
    return;
  }
  $('course-tools').classList.remove('hidden');
  $('course-sort').value = store.get('golfsim.sort', 'played');
  if (!$('course-sort').value) $('course-sort').value = 'played';
  // A course can be preselected with ?course=<id> (from the builder/editor), otherwise the last one played
  const want = new URLSearchParams(location.search).get('course');
  const lastPlayed = Object.entries(menu.played).sort((a, b) => b[1] - a[1])[0]?.[0];
  const first = r.courses.find((c) => c.id === want) || r.courses.find((c) => c.id === lastPlayed) || null;
  renderCourses();
  const pick = first || visibleCourses()[0];
  if (pick) selectCourse(pick);
  document.querySelector('.course.sel')?.scrollIntoView({ block: 'nearest' });
}

function visibleCourses() {
  const q = $('course-search').value.trim().toLowerCase().split(/\s+/).filter(Boolean);
  const list = S.courses.filter((c) => {
    if (menu.favOnly && !menu.favs.has(c.id)) return false;
    const text = searchText(c);
    return q.every((w) => text.includes(w));
  });
  const byName = (a, b) => a.name.localeCompare(b.name);
  const region = (c) => c.location.split(',').map((s) => s.trim()).reverse().join(',');
  const cmp = {
    played: (a, b) => (menu.played[b.id] || 0) - (menu.played[a.id] || 0) || byName(a, b),
    name: byName,
    built: (a, b) => String(b.built).localeCompare(String(a.built)) || byName(a, b),
    location: (a, b) => region(a).localeCompare(region(b)) || byName(a, b),
  }[$('course-sort').value] || byName;
  return list.sort(cmp);
}

const playedText = (t) => {
  if (!t) return '';
  const d = Math.floor((Date.now() - t) / 864e5);
  return d < 1 ? 'played today' : d < 2 ? 'played yesterday' : d < 30 ? `played ${d} days ago` : `played ${new Date(t).toLocaleDateString()}`;
};

function renderCourses() {
  const list = $('course-list');
  const shown = visibleCourses();
  $('course-count').textContent = shown.length === S.courses.length ? `${S.courses.length} course${S.courses.length === 1 ? '' : 's'}` : `${shown.length} of ${S.courses.length}`;
  $('fav-filter').classList.toggle('active', menu.favOnly);
  list.innerHTML = '';
  if (!shown.length) {
    list.innerHTML = `<div class="muted">${menu.favOnly && !menu.favs.size ? 'No favorites yet. Tap the ☆ on a course to add it.' : 'No courses match.'} <a href="/builder.html">Build a new course</a></div>`;
    return;
  }
  for (const c of shown) list.appendChild(cardFor(c));
}

// Cards are made once and reused, so filtering doesn't reload their pictures
function cardFor(c) {
  if (menu.cards.has(c.id)) return menu.cards.get(c.id);
  {
    const el = document.createElement('div');
    el.className = 'course' + (S.selected?.id === c.id ? ' sel' : '');
    el.dataset.id = c.id;
    el.innerHTML = `<div class="pic"><div class="noimg">⛳</div>
        <button class="fav" title="Favorite">${menu.favs.has(c.id) ? '★' : '☆'}</button>
        <button class="photo-btn" title="Change picture">📷</button>
        <span class="credit"></span></div>
      <div class="info"><b></b><small class="where"></small><small class="meta"></small></div>`;
    el.querySelector('b').textContent = c.name;
    el.querySelector('.where').textContent = c.location;
    el.querySelector('.meta').textContent = [`${c.holes} holes · par ${c.par}`, playedText(menu.played[c.id])].filter(Boolean).join(' · ');
    el.querySelector('.credit').textContent = c.photoCredit || '';
    el.querySelector('.credit').title = c.photoCredit || '';
    loadCardImage(el.querySelector('.pic'), c);
    const edit = document.createElement('a');
    edit.href = `/editor.html?course=${encodeURIComponent(c.id)}`;
    edit.className = 'edit-link';
    edit.textContent = 'Edit';
    edit.onclick = (e) => e.stopPropagation();
    const links = document.createElement('span');
    links.className = 'card-links';
    links.appendChild(edit);
    el.querySelector('.info').appendChild(links);
    if (c.osmId) {
      const upd = document.createElement('a');
      upd.href = `/builder.html?update=${encodeURIComponent(c.id)}`;
      upd.className = 'edit-link';
      upd.textContent = 'Update map';
      upd.title = 'Download this course from OpenStreetMap again and rebuild it';
      upd.onclick = (e) => e.stopPropagation();
      links.prepend(upd);
    }
    el.querySelector('.fav').onclick = (e) => {
      e.stopPropagation();
      if (menu.favs.has(c.id)) menu.favs.delete(c.id); else menu.favs.add(c.id);
      store.set('golfsim.favs', [...menu.favs]);
      if (menu.favOnly) renderCourses(); else e.target.textContent = menu.favs.has(c.id) ? '★' : '☆';
    };
    el.querySelector('.photo-btn').onclick = (e) => { e.stopPropagation(); photoMenu(c, e.target); };
    el.onclick = () => selectCourse(c);
    menu.cards.set(c.id, el);
    return el;
  }
}

// Photo (found online or chosen by the player), else the course's aerial imagery, else a plain tile
function loadCardImage(pic, c, bust = '') {
  const srcs = [];
  if (c.photo !== 'none') srcs.push(`/api/courses/${c.id}/photo${bust}`);
  if (c.hasAerial) srcs.push(`/api/courses/${c.id}/aerial`);
  pic.querySelector('img')?.remove();
  pic.classList.remove('has-img');
  if (!srcs.length) return;
  const img = new Image();
  img.loading = 'lazy';
  img.alt = '';
  img.onload = () => pic.classList.add('has-img');
  img.onerror = () => { const next = srcs.shift(); if (next) img.src = next; else img.remove(); };
  img.src = srcs.shift();
  pic.prepend(img);
}

function photoMenu(c, anchor) {
  document.querySelector('.photo-menu')?.remove();
  const m = document.createElement('div');
  m.className = 'photo-menu panel';
  m.innerHTML = '<button data-a="pick">Choose a picture…</button><button data-a="find">Look online again</button>';
  const r = anchor.getBoundingClientRect();
  m.style.left = `${Math.min(r.left, innerWidth - 200)}px`;
  m.style.top = `${r.bottom + 4}px`;
  document.body.appendChild(m);
  const outside = (e) => { if (!m.contains(e.target)) close(); };
  const close = () => { m.remove(); document.removeEventListener('pointerdown', outside, true); };
  setTimeout(() => document.addEventListener('pointerdown', outside, true));
  const card = () => document.querySelector(`.course[data-id="${c.id}"]`);
  const done = (meta) => {
    if (!meta || meta.error) { if (meta?.error) alert(meta.error); return; }
    c.photo = meta.file ? 'yes' : 'none';
    c.photoCredit = meta.credit || '';
    const el = card();
    if (!el) return;
    el.querySelector('.credit').textContent = c.photoCredit;
    el.querySelector('.credit').title = c.photoCredit;
    loadCardImage(el.querySelector('.pic'), c, `?t=${Date.now()}`);
  };
  m.querySelector('[data-a="pick"]').onclick = () => {
    close();
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = 'image/jpeg,image/png,image/webp,image/gif';
    input.onchange = async () => {
      const f = input.files[0];
      if (!f) return;
      const res = await fetch(`/api/courses/${c.id}/photo`, { method: 'PUT', headers: { 'Content-Type': f.type }, body: f });
      done(await res.json().catch(() => null));
    };
    input.click();
  };
  m.querySelector('[data-a="find"]').onclick = async () => {
    close();
    card()?.querySelector('.pic').classList.add('busy');
    const meta = await fetch(`/api/courses/${c.id}/photo`, { method: 'POST' }).then((r) => r.json()).catch(() => null);
    card()?.querySelector('.pic').classList.remove('busy');
    if (meta && !meta.file) alert('No picture found online for this course. You can choose one from your computer instead.');
    done(meta);
  };
}

$('course-search').oninput = () => renderCourses();
$('course-search').onkeydown = (e) => {
  if (e.key === 'Enter') { const first = visibleCourses()[0]; if (first) selectCourse(first); }
  if (e.key === 'Escape') { $('course-search').value = ''; renderCourses(); }
};
$('course-sort').onchange = () => { store.set('golfsim.sort', $('course-sort').value); renderCourses(); };
$('fav-filter').onclick = () => { menu.favOnly = !menu.favOnly; renderCourses(); };

let selectToken = 0;
async function selectCourse(c) {
  menu.cards.forEach((n, id) => n.classList.toggle('sel', id === c.id));
  S.selected = c;
  $('selected-name').textContent = c.name;
  $('btn-start').disabled = true;
  const token = ++selectToken;
  const courseData = await fetch(`/api/courses/${c.id}/data`).then((r) => r.json());
  if (token !== selectToken) return; // another course was picked meanwhile
  S.selectedData = courseData;
  const tees = new Set();
  courseData.Holes.forEach((h) => h.Tees.forEach((t) => t.Position && TEE_TYPES.includes(t.TeeType) && t.Distance > 0 && tees.add(t.TeeType)));
  const yards = Object.fromEntries((courseData.TeeTypeTotalDistance || []).map((t) => [t.TeeType, t.Distance]));
  $('opt-tee').innerHTML = TEE_TYPES.filter((t) => tees.has(t))
    .map((t) => `<option value="${t}">${t}${yards[t] ? ` (${Math.round(yards[t] * M_TO_YD)} yd)` : ''}</option>`).join('');
  $('opt-tee').value = tees.has('White') ? 'White' : [...tees][0];
  $('opt-start').innerHTML = courseData.Holes.map((h, i) => `<option value="${i}">${h.HoleNumber}</option>`).join('');
  if (courseData.DefaultStimp) $('opt-stimp').value = courseData.DefaultStimp;
  $('opt-terrain-wrap').classList.toggle('hidden', !c.hasReal);
  $('opt-terrain').value = c.hasReal ? 'real' : 'gen';
  $('btn-start').disabled = false;
}

// ------------------------------------------------------------------ loading screen

// One row per step: label, a bar (with counts when they're known), what's happening, and how long
// since it last moved. A step with no news for a while turns amber, so a stuck download is obvious.
const loader = {
  rows: new Map(),
  open(title) {
    $('loading').classList.remove('hidden');
    $('loading-title').textContent = title;
    $('loading-steps').innerHTML = '';
    $('loading-stall').classList.add('hidden');
    this.rows.clear();
    this.course = null;
    clearInterval(this.timer);
    this.timer = setInterval(() => this.tick(), 1000);
  },
  close() { clearInterval(this.timer); $('loading').classList.add('hidden'); },
  row(id, label, { stallAfter = 30 } = {}) {
    let r = this.rows.get(id);
    if (!r) {
      const el = document.createElement('div');
      el.className = 'lstep run';
      el.innerHTML = '<span class="licon"></span><span class="llabel"></span><span class="ltime"></span><div class="lbar"><i></i></div><div class="ldetail"></div>';
      el.querySelector('.llabel').textContent = label;
      $('loading-steps').appendChild(el);
      r = { el, t0: Date.now(), last: Date.now(), stallAfter, state: 'run' };
      this.rows.set(id, r);
    }
    return r;
  },
  // Start a step, or report progress on it (done/total for a bar, msg for the detail line)
  step(id, label, { done = null, total = null, msg = '', stallAfter } = {}) {
    const r = this.row(id, label, { stallAfter });
    if (r.state !== 'run') return r;
    r.last = Date.now();
    r.el.classList.remove('stalled');
    const bar = r.el.querySelector('.lbar');
    bar.classList.toggle('indeterminate', !(total > 0));
    if (total > 0) bar.firstChild.style.width = `${Math.min(100, (done / total) * 100)}%`;
    r.el.querySelector('.ldetail').textContent = msg || (total > 0 ? `${done} of ${total}` : '');
    return r;
  },
  finish(id, msg = '') {
    const r = this.rows.get(id);
    if (!r || r.state !== 'run') return;
    r.state = 'done';
    r.el.className = 'lstep done';
    r.el.querySelector('.lbar').firstChild.style.width = '100%';
    r.el.querySelector('.ldetail').textContent = msg;
    r.el.querySelector('.ltime').textContent = this.fmt(Date.now() - r.t0);
  },
  fail(id, msg) {
    const r = this.rows.get(id);
    if (!r) return;
    r.state = 'failed';
    r.el.className = 'lstep failed';
    r.el.querySelector('.ldetail').textContent = msg;
  },
  fmt: (ms) => (ms < 60000 ? `${Math.round(ms / 1000)} s` : `${Math.floor(ms / 60000)} min ${Math.round((ms % 60000) / 1000)} s`),
  tick() {
    let stalled = null;
    for (const r of this.rows.values()) {
      if (r.state !== 'run') continue;
      r.el.querySelector('.ltime').textContent = this.fmt(Date.now() - r.t0);
      const quiet = Date.now() - r.last;
      if (quiet > r.stallAfter * 1000) {
        r.el.classList.add('stalled');
        stalled = { r, quiet };
      }
    }
    $('loading-stall').classList.toggle('hidden', !stalled);
    if (stalled) {
      $('loading-stall-text').textContent = `${stalled.r.el.querySelector('.llabel').textContent}: no progress for ${this.fmt(stalled.quiet)}. `
        + 'The map and imagery servers can be slow; it may still finish. You can also go back and try again later.';
    }
  },
  // Messages from the server's downloads (aerial imagery, buildings, paths, tree heights)
  serverEvent(p) {
    if (!this.course || p.course !== this.course) return;
    const label = { imagery: 'Aerial imagery', buildings: 'Buildings', paths: 'Cart paths', parking: 'Car parks', roads: 'Roads', canopy: 'Tree heights' }[p.task];
    if (label) this.step(p.task, label, { done: p.done, total: p.total, msg: p.msg, stallAfter: 60 });
  },
};
$('loading-back').onclick = () => location.reload();

// Fetch with a byte counter (for the big course files)
async function fetchProgress(url, onBytes, type = 'json') {
  const res = await fetch(url);
  if (!res.ok) throw new Error(`${res.status}`);
  const total = +res.headers.get('content-length') || 0;
  if (!res.body || !total) return type === 'json' ? res.json() : res.arrayBuffer();
  const reader = res.body.getReader(), parts = [];
  let got = 0;
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    parts.push(value); got += value.length;
    onBytes(got, total);
  }
  const buf = new Uint8Array(got);
  let o = 0;
  for (const p of parts) { buf.set(p, o); o += p.length; }
  return type === 'json' ? JSON.parse(new TextDecoder().decode(buf)) : buf.buffer;
}
const mb = (n) => `${(n / 1048576).toFixed(1)} MB`;
// Lets the page repaint between heavy steps
const breathe = () => new Promise((r) => setTimeout(r, 16));

$('btn-start').onclick = async () => {
  menu.played[S.selected.id] = Date.now();
  store.set('golfsim.played', menu.played);
  $('menu').classList.add('hidden');
  loader.open(`Loading ${S.selected.name}`);
  loader.course = S.selected.id;
  await breathe();
  S.settings = {
    tee: $('opt-tee').value,
    pin: $('opt-pin').value,
    units: $('opt-units').value,
    stimp: +$('opt-stimp').value || 10,
    weather: $('opt-weather').value,
    quality: $('opt-quality').value,
    windMph: +$('opt-wind').value || 0,
    windDir: $('opt-winddir').value,
    gimme: +$('opt-gimme').value,
    autoPutt: $('opt-putting').value === 'auto',
    autoPuttR: +$('opt-autoputt-r').value,
  };
  const useReal = S.selected.hasReal && $('opt-terrain').value === 'real';
  // Graphics quality changes vegetation density, so a change rebuilds the course
  if (scene.quality?.name !== S.settings.quality) { scene.setQuality(S.settings.quality); S.courseId = null; }
  savePrefs();
  const key = `${S.selected.id}|${useReal}`;
  if (!S.course || S.courseId !== key) {
    const id = S.selected.id;
    let real = null;
    if (useReal) {
      loader.step('terrain-data', 'Terrain and map data');
      real = await fetchProgress(`/api/courses/${id}/real`, (got, total) => loader.step('terrain-data', 'Terrain and map data', { done: got, total, msg: `${mb(got)} of ${mb(total)}` })).catch(() => null);
      if (!real) { loader.fail('terrain-data', 'Could not load; using generated terrain'); toast('Real terrain could not be loaded — using generated terrain'); }
      else loader.finish('terrain-data');
    }
    // Aerial imagery, buildings, cart paths and tree heights (downloaded and cached by the server
    // the first time; it reports progress over the event stream)
    let imagery = null, buildings = [], canopy = null;
    if (real) {
      const watch = (task, label, url, fallback) => {
        loader.step(task, label, { stallAfter: 60, msg: 'Asking the server…' });
        return fetch(url).then((r) => (r.ok ? r.json() : fallback)).catch(() => fallback)
          .then((v) => { loader.finish(task); return v; });
      };
      S.imageryLoading = true;
      const [manifest, b, paths, parking, roads, cm] = await Promise.all([
        watch('imagery', 'Aerial imagery', `/api/courses/${id}/satellite`, null),
        watch('buildings', 'Buildings', `/api/courses/${id}/buildings`, []),
        watch('paths', 'Cart paths', `/api/courses/${id}/paths`, []),
        watch('parking', 'Car parks', `/api/courses/${id}/parking`, null),
        watch('roads', 'Roads', `/api/courses/${id}/roads`, null),
        watch('canopy', 'Tree heights', `/api/courses/${id}/canopy`, null),
      ]);
      S.imageryLoading = false;
      real.paths = (paths || []).map((p) => ({ ...p, pts: smoothLine(p.pts) })); // (straight runs straight, bends rounded)
      real.parking = parking;
      real.roads = roads;
      if (!manifest?.chunks) loader.fail('imagery', 'Not available; the course will be painted instead');
      if (cm?.available) {
        const buf = await fetchProgress(`/api/courses/${id}/canopy.bin`, (got, total) => loader.step('canopy-file', 'Tree height map', { done: got, total, msg: `${mb(got)} of ${mb(total)}` }), 'buffer').catch(() => null);
        loader.finish('canopy-file');
        if (buf) canopy = { ...cm, heights: new Uint8Array(buf) };
      }
      if (manifest?.chunks) imagery = { manifest, url: (f) => `/api/courses/${id}/satellite/${f}` };
      buildings = b;
    }
    loader.step('course', 'Course layout');
    await breathe();
    S.course = buildCourse(S.selectedData, real);
    S.courseId = key;
    S.geo = real?.geo || null;
    loader.finish('course');
    // The scene reports its own steps (photos, painting, trees, graphics)
    let current = null;
    await scene.build(S.course, { imagery, buildings, canopy, buildingTypes: real?.buildingTypes || {} }, async (stepId, label, done = null, total = null, msg = '') => {
      if (current && current !== stepId) loader.finish(current);
      current = stepId;
      loader.step(stepId, label, { done, total, msg });
      await breathe();
    });
    if (current) loader.finish(current);
    // (the trees seen in the aerial photo, kept with the course so the editor shows the same ones)
    if (scene.photoTreeList) {
      fetch(`/api/courses/${id}/phototrees`, { method: 'PUT', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ trees: scene.photoTreeList }) }).catch(() => {});
      scene.photoTreeList = null;
    }
    // Solid things the ball can hit (Jolt): terrain, trunks, buildings, the flagstick
    S.collider?.dispose();
    S.collider = null;
    loader.step('collisions', 'Collisions');
    await breathe();
    try {
      S.collider = new CollisionWorld(await loadJolt(), S.course, { trees: scene.trees?.all || [], buildings: buildings.concat(scene.carBoxes || []) });
      loader.finish('collisions');
    } catch (err) {
      loader.fail('collisions', 'Using the simpler built-in checks');
      console.warn('Jolt collisions unavailable, using the built-in checks', err);
    }
    $('credits').textContent = [
      scene.imagery?.attribution,
      real ? 'Map data © OpenStreetMap contributors' : '',
      real?.source?.elevation ? `Elevation: ${real.source.elevation.split(',')[0]}` : '',
      canopy ? 'Tree heights: Meta & WRI (CC BY 4.0)' : '',
    ].filter(Boolean).join(' · ');
  }
  S.windRel = S.course.holes.map(() => (S.settings.windDir === 'rand' ? Math.random() * 360 : +S.settings.windDir));
  if (S.settings.weather === 'live') loader.step('weather', 'Weather at the course');
  await setupWeather();
  loader.finish('weather');
  S.scores = S.course.holes.map(() => null);
  loader.close();
  $('hud').classList.remove('hidden');
  document.querySelectorAll('.unit').forEach((n) => (n.textContent = S.settings.units));
  startHole(+$('opt-start').value);
};

// ------------------------------------------------------------------ helpers

const dist = (a, b) => Math.hypot(a.x - b.x, a.z - b.z);
const fmtLen = (m) => (S.settings.units === 'm' ? `${Math.round(m)} m` : `${Math.round(m * M_TO_YD)} yd`);
const fmtNum = (m) => (S.settings.units === 'm' ? m : m * M_TO_YD);
const fmtPutt = (m) => (S.settings.units === 'm' ? `${m.toFixed(1)} m` : `${Math.round(m * M_TO_FT)} ft`);
const hole = () => S.course.holes[S.hole];
const pin = () => {
  const h = hole();
  const p = h.pins[S.settings.pin] || Object.values(h.pins)[0] || h.greenCenter;
  return { x: p.x, y: S.course.heightAt(p.x, p.z) ?? p.y, z: p.z };
};
const headingTo = (from, to) => Math.atan2(to.x - from.x, to.z - from.z);
const onGreen = () => S.lie === 'green';

function teePosition(h) {
  if (h.tees[S.settings.tee]) return h.tees[S.settings.tee];
  // Fall back to the closest available tee in the standard order
  const order = TEE_TYPES;
  const i = order.indexOf(S.settings.tee);
  for (let d = 1; d < order.length; d++) for (const j of [i + d, i - d]) if (h.tees[order[j]]) return h.tees[order[j]];
  return Object.values(h.tees)[0];
}

// ------------------------------------------------------------------ weather

// Where the course is and which way north points in course space (0° = course +z)
function courseLocation() {
  const g = S.geo, b = S.course.bounds;
  if (g) {
    const cx = (b.minX + b.maxX) / 2, cz = (b.minZ + b.maxZ) / 2;
    const det = g.lat[1] * g.lon[2] - g.lat[2] * g.lon[1];
    // course direction in which latitude increases and longitude doesn't = north
    const nx = g.lon[2] / det, nz = -g.lon[1] / det;
    return { lat: g.lat[0] + g.lat[1] * cx + g.lat[2] * cz, lon: g.lon[0] + g.lon[1] * cx + g.lon[2] * cz, northDeg: (Math.atan2(nx, nz) * 180) / Math.PI };
  }
  const lat = +S.selectedData.CourseLat, lon = +S.selectedData.CourseLong;
  return { lat: Number.isFinite(lat) && lat ? lat : null, lon: Number.isFinite(lon) && lon ? lon : null, northDeg: 0 };
}

async function setupWeather() {
  const mode = S.settings.weather;
  const loc = courseLocation();
  let w;
  if (mode === 'live') {
    if (loc.lat == null) { toast('This course has no location — using breezy weather'); }
    else {
      try { w = await fetchLiveWeather(loc.lat, loc.lon); } catch (err) { toast(`Live weather unavailable (${err.message}) — using breezy weather`, 4000); }
    }
    w ||= { ...WEATHER.breezy, windFromMet: Math.random() * 360 };
  } else if (mode === 'custom') {
    w = { label: 'Custom', windMph: S.settings.windMph, gustMph: S.settings.windMph * 1.35, tempC: 20, humidity: 0.5, rainMm: 0, cloud: 0.3, relative: true };
  } else {
    w = { ...(WEATHER[mode] || WEATHER.breezy), windFromMet: Math.random() * 360 };
  }
  w.northDeg = loc.northDeg;
  S.weather = w;
  S.shotWind = null;
}

// The steady wind for the current hole, in course space
function wind() {
  const w = S.weather;
  if (!w || w.windMph < 0.5) return { mps: 0, fromDeg: 0 };
  let fromDeg;
  if (w.relative) {
    const h = hole();
    fromDeg = (headingTo(teePosition(h), h.greenCenter) * 180) / Math.PI + S.windRel[S.hole];
  } else fromDeg = w.windFromMet + w.northDeg;
  return { mps: w.windMph * 0.44704, fromDeg };
}

// The wind for this shot: the steady wind plus a gust
function shotWind() {
  const base = wind(), w = S.weather;
  if (!base.mps) return base;
  const g = gustFor(w);
  return { mps: g.mph * 0.44704, fromDeg: base.fromDeg + g.wobbleDeg };
}

function applyWeatherToScene() {
  const w = S.weather, wd = wind();
  scene.setWeather({ windMps: wd.mps, windFromDeg: wd.fromDeg, rainMm: w.rainMm, cloud: w.cloud });
}

function toast(msg, ms = 2500) {
  const t = $('toast');
  t.textContent = msg;
  t.classList.remove('hidden');
  clearTimeout(toast.timer);
  toast.timer = setTimeout(() => t.classList.add('hidden'), ms);
}

function banner(html, ms = 2600) {
  const b = $('banner');
  b.innerHTML = html;
  b.classList.remove('hidden');
  clearTimeout(banner.timer);
  if (ms) banner.timer = setTimeout(() => b.classList.add('hidden'), ms);
}

function scoreName(strokes, par) {
  const d = strokes - par;
  if (strokes === 1) return 'Hole in one!';
  return { '-3': 'Albatross!', '-2': 'Eagle!', '-1': 'Birdie!', 0: 'Par', 1: 'Bogey', 2: 'Double bogey', 3: 'Triple bogey' }[d] ?? `+${d}`;
}

// ------------------------------------------------------------------ hole flow

function startHole(i) {
  S.hole = i;
  const h = hole();
  S.strokes = 0;
  S.lie = 'tee';
  const t = teePosition(h);
  // Sit the ball on the terrain (real terrain can differ from the stored tee height)
  S.ball = { x: t.x, y: S.course.heightAt(t.x, t.z) ?? t.y, z: t.z };
  S.prevBall = { ...S.ball };
  S.undo = null;
  S.phase = 'ready';
  scene.clearTrails();
  scene.setPin(pin());
  applyWeatherToScene();
  S.tween = null;
  scene.setPuttCircle(S.settings.autoPutt ? pin() : null, S.settings.autoPuttR);
  resetAim();
  scene.viewOverview(S.ball, pin());
  scene.snapCamera();
  setTimeout(() => { if (S.phase === 'ready' && S.strokes === 0 && scene.mode === 'overview') addressView(); }, 1800);
  const len = t.distance ?? dist(t, pin());
  banner(`Hole ${h.number}<small>Par ${h.par} · ${fmtLen(len)}</small>`);
  updateHud();
}

function resetAim() {
  const h = hole();
  const p = pin();
  let target = p;
  // From the tee on long holes aim down the fairway (first aim point within reach)
  if (S.strokes === 0 && h.par >= 4 && h.aims.length) target = h.aims[0];
  S.heading = headingTo(S.ball, target);
  S.aimDist = dist(S.ball, target);
  autoClub();
  refreshAim();
}

// Point the shot at a course position (clicked in 3D or on the minimap)
function aimAt(p) {
  if (S.phase !== 'ready' || !p) return;
  const d = dist(S.ball, p);
  if (d < 1) return;
  S.heading = headingTo(S.ball, p);
  S.aimDist = d;
  refreshAim();
}

function aimPoint() {
  return { x: S.ball.x + Math.sin(S.heading) * S.aimDist, z: S.ball.z + Math.cos(S.heading) * S.aimDist };
}

function refreshAim() {
  const d = dist(S.ball, pin());
  if (!S.aimDist) S.aimDist = d;
  const len = onGreen() ? d + 1 : S.aimDist;
  scene.setAim(S.ball, S.heading, len, onGreen());
  scene.setBall(S.ball);
  scene.settleBall(S.ball, S.lie);
  scene.placeGrass(S.ball);
  if (onGreen()) {
    // Putt read at the hole: distance and how much it climbs or falls
    const pp = pin(), rise = pp.y - S.ball.y;
    const riseTxt = Math.abs(rise) < 0.02 ? 'level' : S.settings.units === 'm'
      ? `${Math.abs(rise * 100).toFixed(0)} cm ${rise > 0 ? 'uphill' : 'downhill'}`
      : `${Math.abs(rise * 39.37).toFixed(1)} in ${rise > 0 ? 'uphill' : 'downhill'}`;
    scene.setAimTarget({ ...pp }, `${fmtPutt(dist(S.ball, pp))} · ${riseTxt}`, false);
  }
  else {
    const t = aimPoint();
    const ts = S.course.surfaceAt(t.x, t.z);
    scene.setAimTarget({ ...t, y: S.course.heightAt(t.x, t.z) ?? S.ball.y }, `${fmtLen(S.aimDist)}${ts ? ` · ${SURFACE_NAMES[ts] || ts}` : ''}`);
  }
  // Slope grid on the green (G toggles it)
  const nearGreen = S.lie === 'green' || S.lie === 'fringe' || (S.lie === 'firstcut' && dist(S.ball, pin()) < 25);
  if (nearGreen && S.phase === 'ready' && S.showGrid !== false) {
    const key = `${S.hole}|${S.ball.x.toFixed(2)}|${S.ball.z.toFixed(2)}`;
    if (S.gridKey !== key) { scene.showPuttGrid(S.ball, pin()); S.gridKey = key; }
  } else if (S.gridKey) { scene.hidePuttGrid(); S.gridKey = null; }
  if (scene.mode === 'address') addressView();
  updateHud();
  drawMinimap();
}

function addressView() {
  scene.viewAddress(S.ball, S.heading, onGreen(), pin());
}

function autoClub() {
  const d = dist(S.ball, pin()) * M_TO_YD;
  let club = 'PT';
  if (!onGreen()) {
    const fits = CLUBS.filter((c) => c[2] > 0 && c[2] >= d * 0.95);
    club = (fits[fits.length - 1] || CLUBS[0])[0];
    if (S.strokes === 0 && hole().par >= 4) club = 'DR';
  }
  setClub(club);
}

function setClub(club) {
  $('club').value = club;
  fetch('/api/player', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ club, distanceToTarget: Math.round(dist(S.ball, pin()) * M_TO_YD) }),
  }).catch(() => {});
}

// ------------------------------------------------------------------ shots

function onShot(shot) {
  // A swing while the last ball is still moving: finish that one at once and play this one
  // from where it stops (rather than losing it)
  if (S.phase === 'flying') {
    S.pendingShot = shot;
    skipAnimation();
    toast('New shot — skipping to where the last ball stopped');
    return;
  }
  if (S.phase !== 'ready') {
    toast('Not ready for a shot');
    return;
  }
  showShotData(shot, null);
  const lieFx = LIE_EFFECT[S.lie] || LIE_EFFECT.rough;
  const play = {
    ...shot,
    speedMph: shot.speedMph * lieFx.speed,
    backSpin: shot.backSpin * lieFx.spin,
    sideSpin: shot.sideSpin * lieFx.spin,
  };
  const res = simulateShot(S.course, S.ball, play, {
    heading: S.heading,
    altitudeM: S.course.altitudeM,
    wind: (S.shotWind = shotWind()),
    tempC: S.weather?.tempC,
    humidity: S.weather?.humidity,
    pressureHpa: S.weather?.pressureHpa,
    wet: S.weather ? wetness(S.weather) : 0,
    stimp: S.settings.stimp,
    pin: pin(),
    trees: scene.trees,
    collider: S.collider,
  });
  S.undo = { ball: { ...S.ball }, strokes: S.strokes, lie: S.lie, heading: S.heading };
  S.prevBall = { ...S.ball };
  S.strokes++;
  S.phase = 'flying';
  S.lastShot = { shot, res };
  S.anim = { res, t: 0 };
  scene.spinTilt = res.spinTilt || 0;
  scene.hideAim();
  scene.hidePuttGrid();
  S.gridKey = null;
  scene.keepTrailAsGhost();
  if (scene.mode === 'free') setFreeView(false);
  if (res.points.length > 2 && !(onGreen() && shot.vla < 6)) scene.followBall(res.carryPoint || res.rest, res.rest, pin());
  if (S.lie !== 'tee' && lieFx.speed < 1) toast(`From the ${(SURFACE_NAMES[S.lie] || S.lie).toLowerCase()}: −${Math.round((1 - lieFx.speed) * 100)}% ball speed, −${Math.round((1 - lieFx.spin) * 100)}% spin`);
  updateHud();
}

function tick(dt) {
  if (S.tween) {
    const tw = S.tween;
    tw.t = Math.min(1, tw.t + dt / tw.dur);
    const k = tw.t * tw.t * (3 - 2 * tw.t);
    const x = tw.from.x + (tw.to.x - tw.from.x) * k, z = tw.from.z + (tw.to.z - tw.from.z) * k;
    const ground = S.course.heightAt(x, z) ?? tw.to.y;
    scene.setBall({ x, z, y: tw.t < 0.85 ? ground : ground - 0.03 * ((tw.t - 0.85) / 0.15) });
    if (tw.t >= 1) S.tween = null;
  }
  const a = S.anim;
  if (!a) return;
  a.t += dt;
  const pts = a.res.points;
  let n = pts.findIndex((p) => p.t > a.t);
  if (n === -1) n = pts.length;
  const p = pts[Math.max(0, n - 1)];
  scene.setBall(p);
  scene.setTrail(pts, n);
  // Show carry once the ball has landed
  if (a.res.carryPoint && a.t >= a.res.carryPoint.t && !a.carryShown) {
    a.carryShown = true;
    showShotData(S.lastShot.shot, { ...a.res, total: null });
  }
  if (n >= pts.length) finishShot(a.res);
}

function skipAnimation() {
  if (S.anim) S.anim.t = 1e9;
}

function finishShot(res) {
  S.anim = null;
  showShotData(S.lastShot.shot, res);
  const h = hole();
  let surface = S.course.surfaceAt(res.rest.x, res.rest.z);
  let msg = null;

  if (res.result === 'holed') {
    S.ball = { ...res.rest };
    return holeOut();
  }
  if (res.result === 'oob' || surface === 'oob' || surface == null) {
    S.strokes++; // stroke and distance
    S.ball = { ...S.prevBall };
    msg = 'Out of bounds<small>Stroke and distance — replay from previous spot</small>';
  } else if (res.result === 'water' || surface === 'water') {
    S.strokes++;
    S.ball = waterDrop(res);
    msg = 'Penalty area<small>+1 stroke — drop at point of entry</small>';
  } else {
    S.ball = { ...res.rest };
  }
  S.lie = S.course.surfaceAt(S.ball.x, S.ball.z) || 'rough';
  // Free relief from a cart path: nearest spot off the path, no nearer the hole
  if (S.lie === 'path' && !msg) {
    const drop = pathRelief(S.ball);
    if (drop) { S.ball = drop; S.lie = S.course.surfaceAt(drop.x, drop.z) || 'rough'; msg = null; S.reliefNote = 'Free relief from the cart path'; }
  }
  if (msg) banner(msg);
  else {
    // Where it landed and where it finished
    const landed = res.carryPoint ? S.course.surfaceAt(res.carryPoint.x, res.carryPoint.z) : null;
    const name = (x) => SURFACE_NAMES[x] || x;
    const hits = res.treeHits || [];
    const tree = hits.includes('pole') ? 'Off the flagstick! ' : hits.includes('building') ? 'Off the building! ' : hits.includes('trunk') ? 'Off the trunk! ' : hits.includes('branch') ? 'Hit a branch! ' : hits.includes('leaves') ? 'Clipped the leaves. ' : '';
    const relief = S.reliefNote ? ` · ${S.reliefNote}` : '';
    S.reliefNote = null;
    if (relief) toast(`${tree}Free relief from the cart path — dropped in the ${name(S.lie).toLowerCase()}`, 4000);
    else if (landed && landed !== S.lie) toast(`${tree}Landed: ${name(landed)} → finished: ${name(S.lie)}`, 3500);
    else toast(`${tree}In the ${name(S.lie).toLowerCase()}`, tree ? 3500 : 2500);
  }

  const d = dist(S.ball, pin());
  // Auto-putt: on the green the hole is done — 1 putt inside the circle, 2 outside
  if (S.lie === 'green' && S.settings.autoPutt && !msg) {
    const putts = d <= S.settings.autoPuttR ? 1 : 2;
    S.strokes += putts;
    return holeOut(false, `Auto-putt: ${putts} putt${putts > 1 ? 's' : ''} from ${fmtPutt(d)}`);
  }
  if (S.lie === 'green' && S.settings.gimme && d <= S.settings.gimme && !msg) {
    S.strokes++;
    return holeOut(false, `Gimme from ${fmtPutt(d)} — counts as 1 putt`);
  }
  if (S.strokes >= h.par + 5) {
    return holeOut(true, 'Maximum strokes reached — picked up');
  }
  const next = () => {
    S.phase = 'ready';
    // Old tracers just get in the way when putting
    if (S.lie === 'green') scene.clearTrails(); else scene.keepTrailAsGhost();
    scene.setGhostsVisible(S.showTraces !== false);
    resetAimToPin();
    addressView();
    const pending = S.pendingShot;
    S.pendingShot = null;
    if (pending) onShot(pending);
  };
  // (straight on if another shot is already waiting)
  if (S.pendingShot) next(); else setTimeout(next, msg ? 1200 : 900);
  updateHud();
}

function resetAimToPin() {
  S.heading = headingTo(S.ball, pin());
  S.aimDist = dist(S.ball, pin());
  autoClub();
  refreshAim();
}

// Nearest point off the cart path (searching outward), not closer to the hole
function pathRelief(ball) {
  const p = pin(), d0 = dist(ball, p);
  for (let r = 0.5; r <= 8; r += 0.25) {
    let best = null;
    for (let k = 0; k < 24; k++) {
      const a = (k / 24) * Math.PI * 2, x = ball.x + Math.cos(a) * r, z = ball.z + Math.sin(a) * r;
      const s = S.course.surfaceAt(x, z);
      if (!s || s === 'path' || s === 'water' || s === 'oob') continue;
      const dd = Math.hypot(x - p.x, z - p.z);
      if (dd < d0 - 0.01) continue;
      if (!best || dd < best.dd) best = { x, z, dd };
    }
    // Step a little further so the ball is clear of the edge
    if (best) {
      const ex = best.x + (best.x - ball.x) / r * 0.3, ez = best.z + (best.z - ball.z) / r * 0.3;
      const ok = S.course.surfaceAt(ex, ez) !== 'path';
      const x = ok ? ex : best.x, z = ok ? ez : best.z;
      return { x, y: S.course.heightAt(x, z) ?? ball.y, z };
    }
  }
  return null;
}

// Drop where the ball last crossed into the penalty area, moved a little back to dry land.
function waterDrop(res) {
  const hz = S.course.hazards.find((z) => z.dropZone && S.course.surfaceAt(res.rest.x, res.rest.z) === 'water');
  if (hz?.dropZone) return { ...hz.dropZone, y: S.course.heightAt(hz.dropZone.x, hz.dropZone.z) ?? hz.dropZone.y };
  const pts = res.points;
  for (let i = pts.length - 1; i >= 0; i--) {
    const s = S.course.surfaceAt(pts[i].x, pts[i].z);
    if (s && s !== 'water' && s !== 'oob') {
      // Step back toward the shot origin until safely clear of the water
      const ox = S.prevBall.x - pts[i].x, oz = S.prevBall.z - pts[i].z, ol = Math.hypot(ox, oz) || 1;
      for (let back = 2; back < 40; back += 1) {
        const x = pts[i].x + (ox / ol) * back, z = pts[i].z + (oz / ol) * back;
        const ss = S.course.surfaceAt(x, z);
        if (ss && ss !== 'water' && ss !== 'oob') return { x, y: S.course.heightAt(x, z), z };
      }
      return { x: pts[i].x, y: S.course.heightAt(pts[i].x, pts[i].z), z: pts[i].z };
    }
  }
  return { ...S.prevBall };
}

// note: why the hole ended without the ball physically dropping (gimme, auto-putt, pick-up)
function holeOut(pickedUp = false, note = '') {
  const h = hole();
  S.phase = 'holed';
  if (S.pendingShot) { S.pendingShot = null; toast('Hole finished — the next swing was not played'); }
  S.scores[S.hole] = pickedUp ? h.par + 5 : S.strokes;
  scene.hidePuttGrid(); S.gridKey = null;
  if (note && !pickedUp) {
    // Show the ball rolling into the cup so what you see matches the score
    const p = pin(), from = { ...S.ball };
    S.tween = { from, to: { x: p.x, y: p.y - 0.03, z: p.z }, t: 0, dur: Math.min(1.2, 0.3 + dist(from, p) * 0.15) };
  } else scene.setBall(S.ball);
  const total = pickedUp ? h.par + 5 : S.strokes;
  const toPar = total === h.par ? 'E' : (total > h.par ? '+' : '') + (total - h.par);
  banner(`${pickedUp ? 'Picked up' : scoreName(total, h.par)}<small>Hole ${h.number}: ${total} (${toPar})${note ? ` · ${note}` : ''}</small>`, 3800);
  updateHud();
  setTimeout(() => showScorecard(true), 2600);
}

function nextHole() {
  $('scorecard').classList.add('hidden');
  if (S.hole + 1 < S.course.holes.length) startHole(S.hole + 1);
  else {
    S.phase = 'done';
    showScorecard(false, true);
  }
}

function mulligan() {
  if (!S.undo || S.phase !== 'ready') return;
  Object.assign(S, { ball: S.undo.ball, strokes: S.undo.strokes, lie: S.undo.lie, heading: S.undo.heading });
  S.undo = null;
  scene.clearTrails();
  toast('Mulligan — replay the shot');
  refreshAim();
  addressView();
}

// ------------------------------------------------------------------ HUD

function updateHud() {
  if (!S.course) return;
  const h = hole();
  $('h-num').textContent = h.number;
  $('h-par').textContent = h.par;
  $('h-hcp').textContent = h.index;
  const len = teePosition(h)?.distance;
  $('h-len').textContent = len ? fmtLen(len) : '';
  $('h-stroke').textContent = S.phase === 'holed' ? S.strokes : S.strokes + 1;
  const d = dist(S.ball, pin());
  $('h-topin').textContent = onGreen() ? fmtPutt(d) : fmtLen(d);
  $('h-lie').textContent = SURFACE_NAMES[S.lie] || S.lie;
  $('h-lie').className = `lie lie-${S.lie}`;
  let over = 0, played = 0;
  S.scores.forEach((s, i) => { if (s != null) { over += s - S.course.holes[i].par; played++; } });
  $('h-total').textContent = played ? `${over === 0 ? 'E' : (over > 0 ? '+' : '') + over} thru ${played}` : 'E';
  // Wind arrow relative to where we're aiming (up = helping)
  const w = wind();
  if (w.mps) {
    const blowRel = w.fromDeg + 180 - (S.heading * 180) / Math.PI;
    $('wind-arrow').style.transform = `rotate(${blowRel - 90}deg)`;
    const wx = S.weather;
    const gust = wx && wx.gustMph > wx.windMph + 3 ? `, gusts ${Math.round(wx.gustMph)}` : '';
    // Help/hurt read relative to the aim line
    const along = Math.cos((blowRel * Math.PI) / 180);
    const feel = Math.abs(along) < 0.4 ? 'cross' : along > 0 ? 'helping' : 'into you';
    $('wind-text').textContent = `${Math.round(wx?.windMph ?? 0)} mph${gust} · ${feel}`;
  } else {
    $('wind-arrow').style.transform = 'rotate(-90deg)';
    $('wind-text').textContent = 'Calm';
  }
  const wx = S.weather;
  if (wx) {
    const temp = S.settings.units === 'm' ? `${Math.round(wx.tempC)}°C` : `${Math.round(wx.tempC * 1.8 + 32)}°F`;
    $('weather-text').textContent = `${wx.live ? 'Live: ' : ''}${wx.label} · ${temp}${wetness(wx) ? ' · wet ground' : ''}`;
  }
}

function showShotData(shot, res) {
  const set = (id, v) => ($(id).textContent = v);
  set('d-speed', shot.speedMph.toFixed(1));
  set('d-vla', shot.vla.toFixed(1));
  set('d-hla', `${Math.abs(shot.hla).toFixed(1)}${shot.hla > 0.05 ? 'R' : shot.hla < -0.05 ? 'L' : ''}`);
  set('d-spin', Math.round(Math.hypot(shot.backSpin, shot.sideSpin)));
  const axis = shot.spinAxis ?? (Math.atan2(shot.sideSpin, shot.backSpin) * 180) / Math.PI;
  set('d-axis', `${Math.abs(axis).toFixed(1)}${axis > 0.05 ? 'R' : axis < -0.05 ? 'L' : ''}`);
  const putt = onGreen() && !res?.carryPoint;
  const f = (m) => (m == null ? '—' : fmtNum(m).toFixed(putt ? 1 : 0));
  set('d-carry', res ? f(res.carry) : '—');
  set('d-total', res && res.total != null ? f(res.total) : '—');
  set('d-off', res && res.total != null ? `${Math.abs(fmtNum(res.offline)).toFixed(0)}${res.offline > 0.5 ? 'R' : res.offline < -0.5 ? 'L' : ''}` : '—');
  set('d-apex', res ? f(res.apex) : '—');
}

function drawMinimap() {
  const cv = $('minimap'), ctx = cv.getContext('2d');
  const src = scene.minimapCanvas || scene.courseCanvas;
  if (!src || !S.ball) return; // (nothing to show until the ball is placed)
  const W = cv.width, H = cv.height;
  const { minX, maxZ } = S.course.bounds;
  const RES = (S.course.bounds.maxX - minX) / src.width;
  const px = (p) => [(p.x - minX) / RES, (maxZ - p.z) / RES];
  const h = hole();
  const tee = teePosition(h), g = pin();
  const [tx, ty] = px(tee), [gx, gy] = px(g);
  const ang = Math.atan2(gy - ty, gx - tx);
  const len = Math.hypot(gx - tx, gy - ty);
  const s = (H * 0.8) / (len + 40 / RES);
  ctx.clearRect(0, 0, W, H);
  ctx.save();
  ctx.translate(W / 2, H / 2);
  ctx.scale(s, s);
  ctx.rotate(-Math.PI / 2 - ang);
  ctx.translate(-(tx + gx) / 2, -(ty + gy) / 2);
  ctx.imageSmoothingQuality = 'high';
  ctx.drawImage(src, 0, 0);
  // Remember the map transform so clicks can be turned back into course positions
  S.minimap = { matrix: ctx.getTransform(), RES, minX, maxZ };
  const trace = (poly) => {
    ctx.beginPath();
    poly.forEach(([x, z], i) => { const q = px({ x, z }); i ? ctx.lineTo(q[0], q[1]) : ctx.moveTo(q[0], q[1]); });
    ctx.closePath();
  };
  if (!scene.imagery) {
    // Water overlay (the aerial photo already shows it)
    ctx.fillStyle = 'rgba(47,111,143,0.95)';
    for (const wb of S.course.waterBodies) { trace(wb.poly); ctx.fill(); }
  }
  const dotR = 3 / s;
  // Aim line to the aim point
  const [bx, by] = px(S.ball);
  const [ax, ay] = px(aimPoint());
  ctx.strokeStyle = 'rgba(255,255,255,0.9)';
  ctx.lineWidth = 1.5 / s;
  ctx.setLineDash([6 / s, 4 / s]);
  ctx.beginPath();
  ctx.moveTo(bx, by);
  ctx.lineTo(ax, ay);
  ctx.stroke();
  ctx.setLineDash([]);
  if (!onGreen()) {
    ctx.strokeStyle = '#ffe14d'; ctx.lineWidth = 2 / s;
    ctx.beginPath(); ctx.arc(ax, ay, dotR * 1.6, 0, 7); ctx.stroke();
  }
  ctx.fillStyle = '#e8282b';
  ctx.beginPath(); ctx.arc(gx, gy, dotR * 1.2, 0, 7); ctx.fill();
  ctx.fillStyle = '#fff';
  ctx.strokeStyle = '#000';
  ctx.lineWidth = 1 / s;
  ctx.beginPath(); ctx.arc(bx, by, dotR, 0, 7); ctx.fill(); ctx.stroke();
  // Hovered spot: lines from the ball and on to the pin
  const hov = S.mmHover;
  if (hov) {
    const [hx, hy] = px(hov);
    ctx.lineWidth = 1.5 / s;
    ctx.strokeStyle = 'rgba(127,212,255,0.95)';
    ctx.beginPath(); ctx.moveTo(bx, by); ctx.lineTo(hx, hy); ctx.stroke();
    ctx.setLineDash([4 / s, 4 / s]);
    ctx.beginPath(); ctx.moveTo(hx, hy); ctx.lineTo(gx, gy); ctx.stroke();
    ctx.setLineDash([]);
    ctx.lineWidth = 2 / s;
    ctx.beginPath(); ctx.arc(hx, hy, dotR * 1.4, 0, 7); ctx.stroke();
  } else if (!onGreen()) {
    // The aim point leaves this much to the pin
    ctx.strokeStyle = 'rgba(255,255,255,0.45)';
    ctx.lineWidth = 1 / s;
    ctx.setLineDash([3 / s, 4 / s]);
    ctx.beginPath(); ctx.moveTo(ax, ay); ctx.lineTo(gx, gy); ctx.stroke();
    ctx.setLineDash([]);
  }
  ctx.restore();
  // Yardages, drawn upright in screen space
  const fmt = onGreen() ? fmtPutt : fmtLen;
  const scr = (p) => { const [x, y] = px(p); const q = S.minimap.matrix.transformPoint(new DOMPoint(x, y)); return [q.x, q.y]; };
  const mid = (a, b) => ({ x: (a.x + b.x) / 2, z: (a.z + b.z) / 2 });
  if (hov) {
    const surf = S.course.surfaceAt(hov.x, hov.z);
    const [hx, hy] = scr(hov);
    mapLabel(ctx, `${fmt(dist(S.ball, hov))}${surf ? ` · ${SURFACE_NAMES[surf] || surf}` : ''}`, hx, hy - 30, '#7fd4ff');
    if (dist(hov, g) > 1) {
      // On the line to the pin, or under the cursor when that's too close to the first tag
      let [lx, ly] = scr(mid(hov, g));
      if (Math.abs(ly - (hy - 30)) < 36 && Math.abs(lx - hx) < 160) [lx, ly] = [hx, hy + 30];
      mapLabel(ctx, `${fmt(dist(hov, g))} to pin`, lx, ly, '#fff');
    }
  } else if (!onGreen()) {
    const t = aimPoint();
    mapLabel(ctx, fmtLen(S.aimDist), ...scr(mid(S.ball, t)), '#ffe14d');
    if (dist(t, g) > 5) mapLabel(ctx, `${fmtLen(dist(t, g))} left`, ...scr(mid(t, g)), '#fff');
  }
  ctx.fillStyle = 'rgba(0,0,0,0.55)';
  ctx.fillRect(0, H - 40, W, 40);
  ctx.fillStyle = '#fff';
  ctx.font = '22px system-ui';
  ctx.fillText(`To pin ${fmt(dist(S.ball, g))} · click to aim`, 12, H - 13);
}

// A small dark tag with text, centred on (x, y) and kept inside the minimap
function mapLabel(ctx, text, x, y, color) {
  ctx.font = 'bold 22px system-ui';
  const w = ctx.measureText(text).width + 16, h = 32;
  const cx = Math.min(Math.max(x, w / 2 + 4), ctx.canvas.width - w / 2 - 4);
  const cy = Math.min(Math.max(y, h / 2 + 4), ctx.canvas.height - 44 - h / 2);
  ctx.fillStyle = 'rgba(0,0,0,0.72)';
  ctx.beginPath(); ctx.roundRect(cx - w / 2, cy - h / 2, w, h, 8); ctx.fill();
  ctx.fillStyle = color;
  ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
  ctx.fillText(text, cx, cy + 1);
  ctx.textAlign = 'start'; ctx.textBaseline = 'alphabetic';
}

// Minimap: click/drag to aim (in free view too: the camera stays where it is)
function minimapPoint(e) {
  const cv = $('minimap'), m = S.minimap;
  if (!m) return null;
  const p = m.matrix.inverse().transformPoint(new DOMPoint(e.offsetX * (cv.width / cv.clientWidth), e.offsetY * (cv.height / cv.clientHeight)));
  return { x: m.minX + p.x * m.RES, z: m.maxZ - p.y * m.RES };
}
function minimapInput(e) {
  const p = minimapPoint(e);
  if (!p) return;
  aimAt(p);
  drawMinimap();
}
$('minimap').addEventListener('pointerdown', (e) => { e.target.setPointerCapture(e.pointerId); minimapInput(e); });
$('minimap').addEventListener('pointermove', (e) => {
  if (e.buttons & 1) { minimapInput(e); return; }
  // Hovering shows the distance to that spot and on to the pin
  S.mmHover = minimapPoint(e);
  if (!S.mmFrame) S.mmFrame = requestAnimationFrame(() => { S.mmFrame = null; if (S.course) drawMinimap(); });
});
$('minimap').addEventListener('pointerleave', () => { S.mmHover = null; if (S.course) drawMinimap(); });

// 3D view: a click (not a drag) on the ground aims there
let downAt = null;
scene.renderer.domElement.addEventListener('pointerdown', (e) => { if (e.button === 0) downAt = [e.clientX, e.clientY]; });
scene.renderer.domElement.addEventListener('pointerup', (e) => {
  if (!downAt || e.button !== 0) return;
  const moved = Math.hypot(e.clientX - downAt[0], e.clientY - downAt[1]);
  downAt = null;
  if (moved > 5 || S.phase !== 'ready') return;
  const p = scene.pick(e.clientX, e.clientY);
  if (p) aimAt(p);
});

// Free view: roam anywhere on the course
let freeTimer = null;
function setFreeView(on) {
  if (!S.course) return;
  scene.setFree(on);
  $('free-btn').classList.toggle('active', on);
  $('free-btn').textContent = on ? 'Back to ball (F)' : 'Free view (F)';
  $('free-help').classList.toggle('hidden', !on);
  if (!on) { scene.freeKeys.clear(); if (S.phase === 'ready') addressView(); }
  clearInterval(freeTimer);
  if (on) freeTimer = setInterval(drawMinimap, 150);
  drawMinimap();
}
// (drop the focus, or Space - which flies up in free view - would press the button again)
$('free-btn').onclick = (e) => { e.currentTarget.blur(); setFreeView(scene.mode !== 'free'); };

// Full screen: the whole page fills the screen (no browser bars or tabs); U or the button toggles,
// Esc leaves (the browser handles that one)
let fsChanged = 0;
function toggleFullscreen() {
  if (document.fullscreenElement) document.exitFullscreen?.().catch(() => {});
  else document.documentElement.requestFullscreen?.({ navigationUI: 'hide' }).catch(() => toast('Full screen isn’t available here', 2000));
}
document.addEventListener('fullscreenchange', () => {
  fsChanged = performance.now();
  $('fs-btn').textContent = document.fullscreenElement ? '⛶ Exit full screen (U)' : '⛶ Full screen (U)';
});
$('fs-btn').onclick = (e) => { e.currentTarget.blur(); toggleFullscreen(); };

function showScorecard(afterHole = false, final = false) {
  const holes = S.course.holes;
  const half = (from, to) => holes.slice(from, to);
  const cls = (s, par) => (s == null ? '' : s - par <= -2 ? 's-eagle' : s - par === -1 ? 's-birdie' : s - par === 1 ? 's-bogey' : s - par >= 2 ? 's-double' : '');
  const sum = (arr) => arr.reduce((a, b) => a + (b || 0), 0);
  const table = (from, to, label) => {
    const hs = half(from, to);
    const sc = S.scores.slice(from, to);
    return `<table class="sc"><tr><th>Hole</th>${hs.map((h) => `<th>${h.number}</th>`).join('')}<th>${label}</th></tr>
      <tr><th>${S.settings.tee}</th>${hs.map((h) => `<td>${h.tees[S.settings.tee] ? Math.round(fmtNum(h.tees[S.settings.tee].distance)) : '—'}</td>`).join('')}<td></td></tr>
      <tr><th>Par</th>${hs.map((h) => `<td>${h.par}</td>`).join('')}<td>${sum(hs.map((h) => h.par))}</td></tr>
      <tr><th>Score</th>${hs.map((h, i) => `<td class="${from + i === S.hole && !final ? 'cur' : ''}"><span class="${cls(sc[i], h.par)}">${sc[i] ?? ''}</span></td>`).join('')}<td>${sc.some((x) => x != null) ? sum(sc) : ''}</td></tr></table>`;
  };
  const front = Math.min(9, holes.length);
  $('sc-table').innerHTML = table(0, front, 'Out') + (holes.length > 9 ? '<br>' + table(9, holes.length, 'In') : '');
  const total = sum(S.scores), par = sum(holes.filter((_, i) => S.scores[i] != null).map((h) => h.par));
  $('sc-title').textContent = final ? `Round complete — ${total} (${total - par === 0 ? 'E' : (total > par ? '+' : '') + (total - par)})` : `${S.course.name} — Scorecard`;
  $('sc-next').classList.toggle('hidden', !afterHole);
  $('sc-next').textContent = S.hole + 1 < holes.length ? `Hole ${holes[S.hole + 1]?.number} →` : 'Finish round';
  $('scorecard').classList.remove('hidden');
}

$('sc-close').onclick = () => {
  $('scorecard').classList.add('hidden');
  if (S.phase === 'done') openMenu();
};
$('sc-next').onclick = nextHole;

function openMenu() {
  S.phase = 'menu';
  S.anim = null;
  $('hud').classList.add('hidden');
  $('scorecard').classList.add('hidden');
  $('menu').classList.remove('hidden');
}

// ------------------------------------------------------------------ club / test shot UI

$('club').innerHTML = CLUBS.map(([code, name]) => `<option value="${code}">${name}</option>`).join('');
$('club').onchange = () => setClub($('club').value);

const PRESETS = {
  driver: { speed: 150, vla: 12, hla: 0, spin: 2700, axis: 0 },
  '7i': { speed: 118, vla: 17, hla: 0, spin: 6800, axis: 0 },
  pw: { speed: 95, vla: 25, hla: 0, spin: 9000, axis: 0 },
  chip: { speed: 40, vla: 22, hla: 0, spin: 4000, axis: 0 },
};
document.querySelectorAll('#test-panel [data-p]').forEach((b) => {
  b.onclick = () => {
    let p = PRESETS[b.dataset.p];
    if (b.dataset.p === 'putt') {
      // Speed to roll ~40 cm past the hole on a flat green
      const d = S.course ? dist(S.ball, pin()) + 0.4 : 3;
      const v = Math.sqrt(2 * stimpToRoll(S.settings.stimp || 10) * 9.81 * d);
      p = { speed: +(v / 0.44704).toFixed(2), vla: 1, hla: 0, spin: 0, axis: 0 };
    }
    $('t-speed').value = p.speed; $('t-vla').value = p.vla; $('t-hla').value = p.hla;
    $('t-spin').value = p.spin; $('t-axis').value = p.axis;
  };
});
$('t-hit').onclick = () => {
  const total = +$('t-spin').value, axis = +$('t-axis').value;
  fetch('/api/test-shot', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ Speed: +$('t-speed').value, VLA: +$('t-vla').value, HLA: +$('t-hla').value, TotalSpin: total, SpinAxis: axis }),
  });
};

// ------------------------------------------------------------------ keyboard

const FREE_KEYS = { w: 'w', a: 'a', s: 's', d: 'd', q: 'q', e: 'e', ' ': 'space', shift: 'shift' };
window.addEventListener('keyup', (e) => {
  const k = FREE_KEYS[e.key.toLowerCase()];
  if (k) scene.freeKeys.delete(k);
});
window.addEventListener('blur', () => scene.freeKeys.clear());
window.addEventListener('keydown', (e) => {
  if (e.target.matches('input, select')) return;
  if (S.phase === 'menu') return;
  // Free view: WASD to move, Q/E down/up (Space up too), Shift faster
  if (scene.mode === 'free' && FREE_KEYS[e.key.toLowerCase()]) { scene.freeKeys.add(FREE_KEYS[e.key.toLowerCase()]); e.preventDefault(); return; }
  const step = (e.shiftKey ? 0.2 : 1) * (onGreen() ? 0.5 : 1) * (Math.PI / 180);
  switch (e.key) {
    case 'f': case 'F': setFreeView(scene.mode !== 'free'); break;
    case 'u': case 'U': toggleFullscreen(); break;
    case 'p': case 'P': setFps(!S.showFps); break;
    case 'h': case 'H': setTraces(S.showTraces === false); toast(S.showTraces ? 'Previous shot traces on' : 'Previous shot traces off', 1500); break;
    case 'g': case 'G':
      S.showGrid = S.showGrid === false;
      S.gridKey = null;
      if (!S.showGrid) scene.hidePuttGrid();
      refreshAim();
      toast(S.showGrid ? 'Green grid on' : 'Green grid off', 1500);
      break;
    case 'ArrowLeft': if (S.phase === 'ready') { S.heading -= step; refreshAim(); } e.preventDefault(); break;
    case 'ArrowRight': if (S.phase === 'ready') { S.heading += step; refreshAim(); } e.preventDefault(); break;
    case 'r': case 'R': if (S.phase === 'ready') resetAimToPin(); break;
    case 'v': case 'V':
      if (scene.mode === 'overview') addressView(); else scene.viewOverview(S.ball, pin());
      break;
    case ' ': skipAnimation(); e.preventDefault(); break;
    case 'm': case 'M': mulligan(); break;
    case 't': case 'T': $('test-panel').classList.toggle('hidden'); break;
    case 'c': case 'C':
      if ($('scorecard').classList.contains('hidden')) showScorecard(S.phase === 'holed');
      else $('scorecard').classList.add('hidden');
      break;
    case 'n': case 'N':
      if (S.phase === 'holed') nextHole();
      else if (S.phase === 'ready' && confirm('Pick up and move to the next hole?')) holeOut(true);
      break;
    case 'Enter': if (S.phase === 'holed') nextHole(); break;
    case 'Escape':
      // (Esc that just took the page out of full screen does nothing else)
      if (document.fullscreenElement || performance.now() - fsChanged < 500) break;
      if (scene.mode === 'free') setFreeView(false);
      else if (!$('scorecard').classList.contains('hidden')) $('scorecard').classList.add('hidden');
      else if (confirm('Quit this round and return to the menu?')) openMenu();
      break;
  }
});

// ------------------------------------------------------------------ launch monitor events

// Whose turn it is, for the Mevo+ (it's armed only when it's time to hit, in the mode for the club)
let turnSent = null;
setInterval(() => {
  const ready = S.phase === 'ready';
  if (ready === turnSent) return;
  turnSent = ready;
  const body = { ready };
  if (ready && S.ball && S.course) Object.assign(body, { club: $('club').value, distanceToTarget: Math.round(dist(S.ball, pin()) * M_TO_YD) });
  fetch('/api/turn', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) }).catch(() => { turnSent = null; });
}, 250);

// The Mevo+ badge above the shot numbers: is it armed, did it see the swing, did it read the shot
const radar = { direct: false, state: 'off', mode: null, flash: null, flashUntil: 0, seenShot: undefined, seenMiss: undefined };
function renderRadar() {
  const el = $('radar');
  el.classList.toggle('hidden', !radar.direct || S.phase === 'menu');
  if (!radar.direct) return;
  const MODE = { chipping: ' · chipping mode', putting: ' · putting mode', outdoor: ' · outdoor mode' };
  let cls, txt;
  if (radar.flash && Date.now() < radar.flashUntil) [cls, txt] = radar.flash;
  else {
    switch (radar.state) {
      case 'ready': cls = 'armed'; txt = `Armed — swing away${MODE[radar.mode] || ''}`; break;
      case 'arming': cls = 'busy'; txt = 'Arming…'; break;
      case 'shot': cls = 'reading'; txt = 'Ball seen — reading shot…'; break;
      case 'idle': cls = 'busy'; txt = S.phase === 'ready' ? 'Arming…' : S.phase === 'flying' ? 'Disarmed until your next shot' : 'Disarmed'; break;
      case 'searching': cls = ''; txt = 'Mevo+ not found — retrying'; break;
      case 'connecting': case 'handshake': cls = 'busy'; txt = 'Connecting to the Mevo+…'; break;
      default: cls = ''; txt = 'Mevo+ disconnected';
    }
  }
  el.className = `panel ${cls}${el.classList.contains('hidden') ? ' hidden' : ''}`;
  $('radar-text').textContent = txt;
}
function radarEvent(s) {
  radar.state = s.state;
  if (s.mode !== undefined) radar.mode = s.mode;
  // (the first event only records what was already there; later changes flash)
  const shotAt = s.lastShot?.at ?? null, missAt = s.lastMiss ?? null;
  if (radar.seenShot !== undefined && shotAt && shotAt !== radar.seenShot) {
    radar.flash = ['read', `Shot read — ${s.lastShot.Speed.toFixed(1)} mph`]; radar.flashUntil = Date.now() + 4000;
  }
  if (radar.seenMiss !== undefined && missAt && missAt !== radar.seenMiss) {
    radar.flash = ['missed', 'Swing seen, but no ball flight read — try again']; radar.flashUntil = Date.now() + 5000;
  }
  if (s.state === 'shot') radar.flash = null;
  radar.seenShot = shotAt; radar.seenMiss = missAt;
  renderRadar();
}
setInterval(renderRadar, 500);

function connectEvents() {
  const es = new EventSource('/api/events?game=1');
  es.addEventListener('mevo', (e) => radarEvent(JSON.parse(e.data)));
  es.addEventListener('update', (e) => updateEvent(JSON.parse(e.data)));
  es.addEventListener('shot', (e) => {
    const shot = JSON.parse(e.data);
    if (S.phase === 'menu') return;
    onShot(shot);
  });
  es.addEventListener('progress', (e) => loader.serverEvent(JSON.parse(e.data)));
  es.addEventListener('lm', (e) => {
    const lm = JSON.parse(e.data);
    const dot = $('lm-dot');
    dot.className = 'dot' + (lm.connected ? (lm.ready ? ' ready' : ' on') : '');
    const battery = lm.battery != null ? ` · ${lm.battery}%` : '';
    if (lm.direct) {
      // Direct connection: say what it's doing (looking, connecting, arming, ready)
      // (the detection mode follows the club: say when it isn't full swing)
      const mode = { chipping: ' · chipping mode', putting: ' · putting mode', outdoor: ' · outdoor mode' }[lm.direct.mode] || '';
      $('lm-text').textContent = lm.direct.state === 'ready' ? `${lm.device} — ready${mode}${battery}`
        : lm.direct.state === 'shot' ? `${lm.device} — reading shot`
          : lm.direct.state === 'idle' ? `${lm.device} — disarmed${battery}`
            : lm.direct.state === 'off' ? 'Mevo+ disconnected'
          : lm.direct.text;
      $('lm-text').title = lm.direct.error || '';
      const st = lm.direct.state;
      $('lm-arm').classList.toggle('hidden', !['ready', 'idle', 'off'].includes(st));
      $('lm-arm').textContent = st === 'ready' ? 'disarm' : st === 'idle' ? 'arm' : 'connect';
      $('lm-arm').dataset.state = st;
    } else {
      $('lm-arm').classList.add('hidden');
      $('lm-text').textContent = lm.connected
        ? `${lm.device || 'Launch monitor'}${lm.ready ? ' — ready' : ' — connected'}`
        : 'Mevo+ not connected';
      $('lm-text').title = '';
    }
    showLmHelp(lm);
    radar.direct = !!lm.direct;
    if (lm.direct?.state) radar.state = lm.direct.state;
    renderRadar();
  });
  // Disarm when you're done (or arm / reconnect); the setup page also has Disconnect
  $('lm-arm').onclick = async () => {
    const st = $('lm-arm').dataset.state;
    const body = st === 'ready' ? { armed: false } : st === 'idle' ? { armed: true } : { connect: true };
    $('lm-arm').disabled = true;
    const r = await fetch('/api/mevo/power', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) })
      .then((x) => x.json()).catch((err) => ({ error: err.message }));
    $('lm-arm').disabled = false;
    if (r.error) toast(r.error);
  };
  es.onerror = () => {
    $('lm-dot').className = 'dot';
    $('lm-text').textContent = 'Server offline';
  };
}

// ---- launch monitor setting (menu)

function showLmHelp(lm = null) {
  const direct = $('opt-lm').value === 'direct';
  let html = direct
    ? 'Join this PC to the <b>Mevo+ Wi-Fi</b> (network <b>FS M2-…</b>; the password is the device\'s 9-digit serial), and close FS Golf. '
      + 'OpenTeeGolf connects to the radar itself and switches between full swing, chipping and putting modes on its own.'
    : 'Open the <b>FS Golf</b> PC app, connect it to your Mevo+, and choose its GSPro/OpenConnect connection (address 127.0.0.1, port 921).';
  if (direct && lm?.direct) {
    html += `<br>Status: <b>${lm.direct.state === 'ready' ? `${lm.device} ready` : lm.direct.text}</b>${lm.direct.error ? ` (${lm.direct.error})` : ''}`;
  }
  $('lm-help').innerHTML = html;
}
fetch('/api/lm').then((r) => r.json()).then((r) => { $('opt-lm').value = r.settings.connection; showLmHelp(r.state); }).catch(() => {});
$('opt-lm').onchange = async () => {
  showLmHelp();
  await fetch('/api/lm', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ connection: $('opt-lm').value }) }).catch(() => {});
};

connectEvents();
loadCourses().catch((err) => {
  $('course-list').innerHTML = `<div class="muted">Could not load courses: ${err.message}</div>`;
});

// Expose for debugging in the console
window.golfsim = { S, scene, onShot };

// Grey out the custom wind settings unless "Custom" weather is chosen
function syncWeatherOptions() {
  const custom = $('opt-weather').value === 'custom';
  document.querySelectorAll('.custom-wx').forEach((el) => el.classList.toggle('off', !custom));
}
$('opt-weather').addEventListener('change', syncWeatherOptions);
syncWeatherOptions();

// ------------------------------------------------------------------ FPS overlay

function setFps(on) {
  S.showFps = on;
  $('opt-fps').checked = on;
  $('perf').classList.toggle('hidden', !on);
  savePrefs();
}
scene.stats.onUpdate = ({ fps, worstMs, calls, triangles, backend }) => {
  if (!S.showFps) return;
  const tri = triangles > 1e6 ? `${(triangles / 1e6).toFixed(1)}M` : `${Math.round(triangles / 1e3)}k`;
  $('perf').innerHTML = `<b>${Math.round(fps)} FPS</b> · ${(1000 / Math.max(1, fps)).toFixed(1)} ms<br>slowest frame ${Math.round(worstMs)} ms<br>${calls} draws · ${tri} triangles<br>graphics: ${scene.quality?.name} · ${backend || ""}`;
  $('perf').classList.toggle('slow', fps < 30 || worstMs > 100);
};
$('opt-fps').addEventListener('change', () => setFps($('opt-fps').checked));

// Remember graphics choices between visits
function savePrefs() {
  try { localStorage.setItem('golfsim.prefs', JSON.stringify({ quality: $('opt-quality').value, fps: !!S.showFps, traces: S.showTraces !== false })); } catch { /* storage unavailable */ }
}
try {
  const prefs = JSON.parse(localStorage.getItem('golfsim.prefs') || '{}');
  if (prefs.quality) $('opt-quality').value = prefs.quality;
  if (prefs.fps) setFps(true);
} catch { /* ignore */ }

// Previous shot traces on/off (H)
function setTraces(on) {
  S.showTraces = on;
  $('opt-traces').checked = on;
  scene.setGhostsVisible(on);
  try { const pr = JSON.parse(localStorage.getItem('golfsim.prefs') || '{}'); pr.traces = on; localStorage.setItem('golfsim.prefs', JSON.stringify(pr)); } catch { /* ignore */ }
}
$('opt-traces').addEventListener('change', () => setTraces($('opt-traces').checked));
try { const pr = JSON.parse(localStorage.getItem('golfsim.prefs') || '{}'); if (pr.traces === false) setTraces(false); } catch { /* ignore */ }

// Show the auto-putt circle size only when auto-putt is chosen
function syncPuttingOptions() {
  document.querySelectorAll('.autoputt-opt').forEach((el) => el.classList.toggle('off', $('opt-putting').value !== 'auto'));
}
$('opt-putting').addEventListener('change', syncPuttingOptions);
syncPuttingOptions();
