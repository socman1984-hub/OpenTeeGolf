// Players and games: the saved player profiles, the formats a round can be played in, and the
// scoring for each. main.js plays the shots; this decides whose turn it is, keeps every side's
// scores and works out who's winning.
//
// A "side" is whatever has its own ball: a player, or in scramble and alternate shot a team.
// Formats with teams but a ball each (best ball, and match play with more than two players) keep
// a side per player and add up the team's best score on each hole.

export const COLOURS = ['#e53935', '#1e88e5', '#fdd835', '#43a047', '#8e24aa', '#fb8c00', '#00acc1', '#ec407a'];

// ------------------------------------------------------------------ profiles (kept by the server)

export async function loadProfiles() {
  try { return (await (await fetch('/api/profiles')).json()).profiles || []; } catch { return []; }
}
export async function saveProfiles(profiles) {
  const r = await fetch('/api/profiles', { method: 'PUT', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ profiles }) });
  if (!r.ok) throw new Error('Could not save the players');
  return (await r.json()).profiles;
}
export const newProfile = (name, i = 0) => ({ id: `p${Date.now().toString(36)}${Math.random().toString(36).slice(2, 6)}`, name, colour: COLOURS[i % COLOURS.length], handicap: 0, hand: 'RH', tee: '', rounds: [] });

// ------------------------------------------------------------------ formats

// teams: players are split into team A and team B; shared: the team plays one ball ('scramble':
// everyone hits and the best shot is played; 'alternate': the players take turns); sides: how many
// sides the game is between (match play: exactly two)
export const FORMATS = {
  stroke: { name: 'Stroke play', about: 'Everyone plays their own ball. Fewest strokes wins.' },
  stableford: { name: 'Stableford', about: 'Points on every hole: 1 for a bogey, 2 for par, 3 for a birdie, 4 for an eagle. Most points wins, and a bad hole costs nothing.' },
  match: { name: 'Match play', about: 'Win holes, not strokes: the lower score takes each hole. Two players, or teams A and B with their best ball.', min: 2 },
  skins: { name: 'Skins', about: 'Every hole is worth a skin, won by an outright lowest score. Ties carry the skin over to the next hole.', min: 2 },
  bestball: { name: 'Best ball (four-ball)', about: 'Teams A and B. Everyone plays their own ball; each hole the team counts its best score.', teams: true, min: 2 },
  scramble: { name: 'Scramble', about: 'Teams: everyone hits, the team picks the best shot and everyone plays the next one from there.', teams: true, shared: 'scramble', min: 1 },
  alternate: { name: 'Alternate shot (foursomes)', about: 'Teams of two share one ball and take turns hitting it, and take turns teeing off.', teams: true, shared: 'alternate', min: 2 },
};
export const TEAM_NAMES = { A: 'Team A', B: 'Team B' };
export const TEAM_COLOURS = { A: '#e53935', B: '#1e88e5' };

// ------------------------------------------------------------------ handicaps

// Strokes a player gets on each hole: their handicap shared over the holes, hardest first (the
// holes' stroke index), scaled for a 9-hole course
export function strokesByHole(handicap, holes) {
  const n = holes.length, ch = Math.round((handicap * n) / 18);
  const order = holes.map((h, i) => ({ i, idx: h.index || i + 1 })).sort((a, b) => a.idx - b.idx || a.i - b.i);
  const out = new Array(n).fill(0);
  order.forEach(({ i }, rank) => { out[i] = Math.floor(Math.abs(ch) / n) + (rank < Math.abs(ch) % n ? 1 : 0); if (ch < 0) out[i] = -out[i]; });
  return out;
}

// ------------------------------------------------------------------ a round

export class Round {
  // players: [{ id, name, colour, handicap, hand, tee, team }] in playing order
  constructor({ players, format = 'stroke', handicaps = false, holes }) {
    this.format = FORMATS[format] ? format : 'stroke';
    this.F = FORMATS[this.format];
    this.handicaps = handicaps;
    this.holes = holes;
    this.players = players.map((p, i) => ({ ...p, team: this.F.teams || (this.format === 'match' && players.length > 2) ? (p.team === 'B' ? 'B' : 'A') : null, colour: p.colour || COLOURS[i % COLOURS.length] }));
    const teamed = this.players.some((p) => p.team);
    // sides: one per team for shared-ball formats, else one per player
    if (this.F.shared) {
      const teams = ['A', 'B'].map((t) => this.players.filter((p) => p.team === t)).filter((m) => m.length);
      this.sides = teams.map((m) => ({ name: TEAM_NAMES[m[0].team], team: m[0].team, colour: TEAM_COLOURS[m[0].team], members: m }));
    } else this.sides = this.players.map((p) => ({ name: p.name, team: p.team, colour: p.colour, members: [p] }));
    for (const s of this.sides) {
      s.scores = holes.map(() => null);
      // (a team's handicap: a tenth of everyone's together in a scramble, half in alternate shot)
      const hc = s.members.length === 1 ? s.members[0].handicap || 0 : s.members.reduce((a, p) => a + (p.handicap || 0), 0) * (this.F.shared === 'scramble' ? 0.1 : 0.5);
      s.handicap = hc;
    }
    // (match play gives strokes off the lowest handicap)
    const low = Math.min(...this.sides.map((s) => s.handicap));
    for (const s of this.sides) s.received = handicaps ? strokesByHole(this.format === 'match' ? s.handicap - low : s.handicap, holes) : holes.map(() => 0);
    this.teamed = teamed;
    this.order = this.sides.map((_, i) => i); // honour on the tee
  }

  get single() { return this.sides.length === 1 && this.sides[0].members.length === 1; }

  // Start a hole: every side on its tee (teeAt(side) gives the spot)
  startHole(h, teeAt) {
    this.hole = h;
    for (const s of this.sides) {
      const t = teeAt(s);
      Object.assign(s, { ball: { ...t }, prevBall: { ...t }, lie: 'tee', strokes: 0, holed: false, picked: false, shots: 0, attempts: [], undo: null });
    }
    // (alternate shot: the players take turns teeing off, hole by hole)
    this.teeTurn = h;
  }

  // Who plays next: anyone still to tee off, in the order of honour; then whoever's furthest
  // from the hole. -1 when everyone has holed out.
  next(distToPin) {
    const open = this.order.filter((i) => !this.sides[i].holed);
    if (!open.length) return -1;
    const tee = open.find((i) => this.sides[i].strokes === 0 && !this.sides[i].attempts.length);
    if (tee != null) return tee;
    // (a scramble side part way through its round of shots keeps going)
    const mid = open.find((i) => this.sides[i].attempts.length);
    if (mid != null) return mid;
    return open.reduce((a, b) => (distToPin(this.sides[b].ball) > distToPin(this.sides[a].ball) ? b : a));
  }

  // Which of a side's players hits now
  hitter(s) {
    if (this.F.shared === 'scramble') return s.members[s.attempts.length % s.members.length];
    if (this.F.shared === 'alternate') return s.members[(s.shots + this.teeTurn) % s.members.length];
    return s.members[0];
  }

  // A scramble side's shot from one of its players: { ball, lie, strokes, holed }. Returns the
  // chosen one once everyone has hit (best: holed, then nearest the hole on the best lie), else null.
  scrambleAttempt(s, a, distToPin) {
    s.attempts.push({ ...a, by: this.hitter(s) });
    if (a.holed) { const best = s.attempts.at(-1); s.attempts = []; return best; }
    if (s.attempts.length < s.members.length) return null;
    const LIE = { green: 0, fringe: 0.3, fairway: 0.5, tee: 0.5, firstcut: 0.8, rough: 1.5, deeprough: 2.5, native: 3, sand: 3, path: 1 };
    const score = (x) => x.strokes * 1000 + distToPin(x.ball) * (1 + (LIE[x.lie] ?? 2) * 0.12);
    const best = s.attempts.reduce((a, b) => (score(b) < score(a) ? b : a));
    s.attempts = [];
    return best;
  }

  // The side has finished the hole
  holeOut(s, strokes, picked = false) {
    s.holed = true; s.picked = picked;
    s.scores[this.hole] = strokes;
  }

  // Everyone done: the honour on the next tee goes to the best score here (ties keep the order)
  endHole() {
    const h = this.hole;
    this.order.sort((a, b) => (this.net(this.sides[a], h) ?? 99) - (this.net(this.sides[b], h) ?? 99));
  }

  net(s, h) { return s.scores[h] == null ? null : s.scores[h] - (s.received[h] || 0); }

  // ---- scoring

  // The teams' best (net) score on each hole, for best ball and teamed match play
  teamScores() {
    return ['A', 'B'].map((t) => {
      const members = this.sides.filter((s) => s.team === t);
      if (!members.length) return null;
      return { team: t, name: TEAM_NAMES[t], colour: TEAM_COLOURS[t], members, scores: this.holes.map((_, h) => { const v = members.map((s) => this.net(s, h)).filter((x) => x != null); return v.length === members.length ? Math.min(...v) : null; }) };
    }).filter(Boolean);
  }

  // The two sides of a match: [{ name, colour, scores (net) }]
  matchSides() {
    if (this.sides.length === 2 && !this.teamed) return this.sides.map((s) => ({ name: s.name, colour: s.colour, scores: this.holes.map((_, h) => this.net(s, h)) }));
    const t = this.teamScores();
    return t.length === 2 ? t : null;
  }

  // Match status after the holes played: { lead (+ first side), played, left, text, decided }
  matchStatus() {
    const m = this.matchSides();
    if (!m) return null;
    let lead = 0, played = 0, decided = null;
    for (let h = 0; h < this.holes.length; h++) {
      const a = m[0].scores[h], b = m[1].scores[h];
      if (a == null || b == null) continue;
      played++;
      lead += a < b ? 1 : a > b ? -1 : 0;
      const left = this.holes.length - played;
      if (!decided && Math.abs(lead) > left) decided = { lead, left };
    }
    const left = this.holes.length - played, who = lead > 0 ? m[0] : m[1];
    let text;
    if (decided) text = `${(decided.lead > 0 ? m[0] : m[1]).name} won ${Math.abs(decided.lead)}${decided.left ? `&${decided.left}` : ' up'}`;
    else if (!lead) text = left ? 'All square' : 'Halved';
    else text = `${who.name} ${Math.abs(lead)} up${left && Math.abs(lead) === left ? ' (dormie)' : ''}`;
    return { sides: m, lead, played, left, text, decided: !!decided || !left };
  }

  // Skins: who won each hole's skins (carried over through ties)
  skins() {
    const won = this.sides.map(() => 0), byHole = [];
    let carry = 0;
    for (let h = 0; h < this.holes.length; h++) {
      const v = this.sides.map((s) => this.net(s, h));
      if (v.some((x) => x == null)) { byHole.push(null); continue; }
      carry++;
      const low = Math.min(...v), at = v.reduce((a, x, i) => (x === low ? a.concat(i) : a), []);
      if (at.length === 1) { won[at[0]] += carry; byHole.push({ side: at[0], skins: carry }); carry = 0; } else byHole.push({ side: -1, carry });
    }
    return { won, byHole, carry };
  }

  stablefordPoints(s, h) {
    const n = this.net(s, h);
    return n == null ? null : Math.max(0, 2 + this.holes[h].par - n);
  }

  // The leaderboard: [{ name, colour, value (text), sub, sort }] best first, and a one-line summary
  standings() {
    const par = (h) => this.holes[h].par;
    const thru = (scores) => scores.filter((x) => x != null).length;
    const toPar = (scores) => { let d = 0; scores.forEach((x, h) => { if (x != null) d += x - par(h); }); return d; };
    const fmt = (d) => (d === 0 ? 'E' : d > 0 ? `+${d}` : `${d}`);
    const nets = (s) => this.holes.map((_, h) => this.net(s, h));
    let rows;
    if (this.format === 'stableford') {
      rows = this.sides.map((s) => { const pts = this.holes.reduce((a, _, h) => a + (this.stablefordPoints(s, h) || 0), 0); return { name: s.name, colour: s.colour, value: `${pts} pts`, sub: `thru ${thru(s.scores)}`, sort: -pts }; });
    } else if (this.format === 'skins') {
      const k = this.skins();
      rows = this.sides.map((s, i) => ({ name: s.name, colour: s.colour, value: `${k.won[i]} skin${k.won[i] === 1 ? '' : 's'}`, sub: `${fmt(toPar(nets(s)))} thru ${thru(s.scores)}`, sort: -k.won[i] }));
      rows.carry = k.carry;
    } else if (this.format === 'match') {
      const m = this.matchStatus();
      if (m) {
        rows = m.sides.map((x, i) => ({ name: x.name, colour: x.colour, value: m.lead === 0 ? 'AS' : (i === 0) === (m.lead > 0) ? `${Math.abs(m.lead)} UP` : '', sub: '', sort: i === 0 ? -m.lead : m.lead }));
        rows.summary = m.text;
      }
    } else if (this.format === 'bestball') {
      rows = this.teamScores().map((t) => { const d = toPar(t.scores); return { name: t.name, colour: t.colour, value: fmt(d), sub: `thru ${thru(t.scores)}`, sort: d }; });
    }
    if (!rows) rows = this.sides.map((s) => { const d = toPar(nets(s)); return { name: s.name, colour: s.colour, value: fmt(d), sub: `${s.scores.reduce((a, x) => a + (x || 0), 0) || ''} thru ${thru(s.scores)}${this.handicaps ? ' net' : ''}`, sort: d }; });
    const summary = rows.summary, carry = rows.carry;
    rows = rows.slice().sort((a, b) => a.sort - b.sort);
    rows.summary = summary || (carry ? `${carry} skin${carry === 1 ? '' : 's'} carried over` : '');
    return rows;
  }

  // What happened on a hole, for the banner: e.g. "Sam wins the hole", "Ryan takes 2 skins"
  holeNews(h) {
    if (this.format === 'match') {
      const m = this.matchSides();
      if (!m) return '';
      const a = m[0].scores[h], b = m[1].scores[h];
      return `${a === b ? 'Hole halved' : `${(a < b ? m[0] : m[1]).name} wins the hole`} · ${this.matchStatus().text}`;
    }
    if (this.format === 'skins') {
      const k = this.skins().byHole[h];
      if (!k) return '';
      return k.side >= 0 ? `${this.sides[k.side].name} takes ${k.skins} skin${k.skins === 1 ? '' : 's'}` : `Tied: ${k.carry} skin${k.carry === 1 ? '' : 's'} carry over`;
    }
    if (this.format === 'stableford') return this.sides.map((s) => `${s.name} ${this.stablefordPoints(s, h)} pts`).join(' · ');
    if (this.format === 'bestball') return this.teamScores().map((t) => `${t.name} ${t.scores[h] ?? '—'}`).join(' · ');
    return this.sides.map((s) => `${s.name} ${s.scores[h] ?? '—'}`).join(' · ');
  }
}
