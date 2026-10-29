// The menu's Players & game section: pick who's playing from the saved profiles (click a name to
// add or drop them; the order they're picked is the order they tee off), put them in teams when the
// game needs teams, choose the game and whether handicaps count, and add or edit players.
import { loadProfiles, saveProfiles, newProfile, FORMATS, COLOURS } from './players.js';

const $ = (id) => document.getElementById(id);
const esc = (s) => String(s).replace(/[&<>"]/g, (c) => `&#${c.charCodeAt(0)};`);
const TEES = ['', 'Black', 'Blue', 'White', 'Gold', 'Green', 'Red'];
const KEY = 'golfsim.players.round';

const state = { profiles: [], picked: [], teams: {}, format: 'stroke', handicaps: false };
try { Object.assign(state, JSON.parse(localStorage.getItem(KEY) || '{}')); } catch { /* none saved */ }
const remember = () => { try { localStorage.setItem(KEY, JSON.stringify({ picked: state.picked, teams: state.teams, format: state.format, handicaps: state.handicaps })); } catch { /* storage unavailable */ } };

export function profiles() { return state.profiles; }

// Who's playing, the game and handicaps, for a round: { players, format, handicaps }, or null for
// the usual one-player round when nobody is picked
export function roundSetup() {
  const players = state.picked.map((id) => state.profiles.find((p) => p.id === id)).filter(Boolean).map((p) => ({ ...p, team: state.teams[p.id] || 'A' }));
  if (!players.length) return null;
  return { players, format: state.format, handicaps: state.handicaps };
}

// Whether the picked players can play the chosen game (null, or what's missing)
function problem() {
  const F = FORMATS[state.format], n = state.picked.length;
  if (!n) return null;
  if (n < (F.min || 1)) return `${F.name} needs at least ${F.min} players.`;
  const teams = new Set(state.picked.map((id) => state.teams[id] || 'A'));
  if (state.format === 'match' && n > 2 && teams.size < 2) return 'With more than two players, put them in teams A and B.';
  if (F.teams && state.format !== 'scramble' && teams.size < 2) return 'Put the players in two teams (click A / B).';
  if (state.format === 'alternate') for (const t of teams) if (state.picked.filter((id) => (state.teams[id] || 'A') === t).length !== 2) return 'Alternate shot needs two players in each team.';
  return null;
}
export const setupProblem = problem;

function render() {
  const F = FORMATS[state.format];
  const teamsOn = F.teams || (state.format === 'match' && state.picked.length > 2);
  $('pl-chips').innerHTML = state.profiles.map((p) => {
    const at = state.picked.indexOf(p.id), on = at >= 0, team = state.teams[p.id] || 'A';
    return `<button type="button" class="pl-chip${on ? ' on' : ''}" data-id="${p.id}" title="${on ? 'Playing: click to leave out' : 'Click to add to this round'}">`
      + `<i style="background:${p.colour}"></i>${on ? `<b class="pl-order">${at + 1}</b>` : ''}${esc(p.name)}${p.handicap ? `<small>${p.handicap}</small>` : ''}`
      + `${on && teamsOn ? `<span class="pl-team t${team}" data-team="${p.id}" title="Team: click to switch">${team}</span>` : ''}</button>`;
  }).join('') || '<span class="muted">No players yet: add yourself and your friends.</span>';
  $('pl-format').value = state.format;
  $('pl-handicaps').checked = state.handicaps;
  const prob = problem();
  $('pl-about').innerHTML = state.picked.length
    ? `${esc(F.about)}${prob ? `<br><b class="warn">${esc(prob)}</b>` : ''}`
    : 'Pick players to play together, or leave everyone out for a round on your own.';
  $('btn-start').dataset.blocked = prob ? '1' : '';
}

export async function setupPlayersMenu() {
  $('pl-format').innerHTML = Object.entries(FORMATS).map(([k, f]) => `<option value="${k}">${f.name}</option>`).join('');
  state.profiles = await loadProfiles();
  state.picked = state.picked.filter((id) => state.profiles.some((p) => p.id === id));
  render();
  $('pl-chips').onclick = (e) => {
    const t = e.target.closest('[data-team]');
    if (t) { e.stopPropagation(); state.teams[t.dataset.team] = (state.teams[t.dataset.team] || 'A') === 'A' ? 'B' : 'A'; remember(); return render(); }
    const c = e.target.closest('.pl-chip');
    if (!c) return;
    const id = c.dataset.id, at = state.picked.indexOf(id);
    if (at >= 0) state.picked.splice(at, 1);
    else {
      state.picked.push(id);
      // (new team players alternate A, B, A, B)
      if (!state.teams[id]) state.teams[id] = state.picked.length % 2 ? 'A' : 'B';
    }
    remember(); render();
  };
  $('pl-format').onchange = () => { state.format = $('pl-format').value; remember(); render(); };
  $('pl-handicaps').onchange = () => { state.handicaps = $('pl-handicaps').checked; remember(); render(); };
  $('pl-edit').onclick = () => openEditor();
  $('pl-add').onclick = () => openEditor(true);
}

// ---- the players editor: a row per player (name, colour, handicap, hand, tees), add, remove

function openEditor(addOne = false) {
  const list = state.profiles.map((p) => ({ ...p }));
  if (addOne) list.push(newProfile(`Player ${list.length + 1}`, list.length));
  const draw = () => {
    $('pe-list').innerHTML = list.map((p, i) => `<div class="pe-row" data-i="${i}">
      <input class="pe-name" value="${esc(p.name)}" maxlength="40" placeholder="Name">
      <select class="pe-colour" style="background:${p.colour}" title="Colour">${COLOURS.map((c) => `<option value="${c}" style="background:${c}" ${c === p.colour ? 'selected' : ''}>&nbsp;</option>`).join('')}</select>
      <label title="Handicap index (0 for scratch; up to 54)">HCP <input class="pe-hcp" type="number" min="-10" max="54" step="0.1" value="${p.handicap}"></label>
      <select class="pe-hand" title="Right or left handed"><option value="RH" ${p.hand !== 'LH' ? 'selected' : ''}>Right</option><option value="LH" ${p.hand === 'LH' ? 'selected' : ''}>Left</option></select>
      <select class="pe-tee" title="Tees this player plays (or the round's)">${TEES.map((t) => `<option value="${t}" ${t === (p.tee || '') ? 'selected' : ''}>${t ? `${t} tees` : "Round's tees"}</option>`).join('')}</select>
      <button type="button" class="pe-del" title="Remove this player">✕</button>
      ${p.rounds?.length ? `<small class="pe-stats">${p.rounds.length} round${p.rounds.length === 1 ? '' : 's'} played</small>` : ''}
    </div>`).join('') || '<div class="muted">No players yet.</div>';
    const last = $('pe-list').querySelector('.pe-row:last-child .pe-name');
    if (addOne && last) { last.focus(); last.select(); addOne = false; }
  };
  const read = () => {
    for (const row of $('pe-list').querySelectorAll('.pe-row')) {
      const p = list[+row.dataset.i];
      p.name = row.querySelector('.pe-name').value.trim() || p.name;
      p.colour = row.querySelector('.pe-colour').value;
      p.handicap = +row.querySelector('.pe-hcp').value || 0;
      p.hand = row.querySelector('.pe-hand').value;
      p.tee = row.querySelector('.pe-tee').value;
    }
  };
  $('pe-list').onclick = (e) => {
    const del = e.target.closest('.pe-del');
    if (!del) return;
    read();
    const i = +del.closest('.pe-row').dataset.i;
    if (!confirm(`Remove ${list[i].name}? Their round history goes too.`)) return;
    list.splice(i, 1); draw();
  };
  $('pe-list').oninput = (e) => { if (e.target.matches('.pe-colour')) e.target.style.background = e.target.value; };
  $('pe-add').onclick = () => { read(); list.push(newProfile(`Player ${list.length + 1}`, list.length)); addOne = true; draw(); };
  $('pe-cancel').onclick = () => $('players-modal').classList.add('hidden');
  $('pe-save').onclick = async () => {
    read();
    try {
      state.profiles = await saveProfiles(list.filter((p) => p.name.trim()));
      state.picked = state.picked.filter((id) => state.profiles.some((p) => p.id === id));
      remember(); render();
      $('players-modal').classList.add('hidden');
    } catch (err) { alert(err.message); }
  };
  draw();
  $('players-modal').classList.remove('hidden');
}

// After a round: each player's score added to their history
export async function recordRound(entries) {
  if (!entries.length) return;
  for (const e of entries) {
    const p = state.profiles.find((x) => x.id === e.id);
    if (p) (p.rounds ||= []).push(e.round);
  }
  try { state.profiles = await saveProfiles(state.profiles); } catch { /* kept for next time */ }
}
