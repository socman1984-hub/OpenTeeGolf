// Checks the games in public/js/players.js: turn order, scramble shots, handicap strokes and the
// scoring of stroke play, Stableford, match play, skins and best ball.
// Usage: node test/players-test.js
import { Round, strokesByHole } from '../public/js/players.js';

let failed = 0;
const check = (ok, msg) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${msg}`); if (!ok) failed++; };
const holes = Array.from({ length: 18 }, (_, i) => ({ number: i + 1, par: [4, 3, 5][i % 3], index: ((i * 7) % 18) + 1 }));
const P = (name, extra = {}) => ({ id: name, name, handicap: 0, hand: 'RH', ...extra });
const play = (R, h, scores) => { R.startHole(h, () => ({ x: 0, y: 0, z: 0 })); R.sides.forEach((s, i) => R.holeOut(s, scores[i])); R.endHole(); };

// handicap strokes
const k = strokesByHole(20, holes);
check(k.reduce((a, b) => a + b, 0) === 20 && k[holes.findIndex((h) => h.index === 1)] === 2 && k[holes.findIndex((h) => h.index === 18)] === 1, 'a 20 handicap: 2 strokes on the two hardest holes, 1 on the rest');
check(strokesByHole(9, holes.slice(0, 9)).reduce((a, b) => a + b, 0) === 5, 'halved (rounded) on a 9-hole course');

// turn order: honour on the tee, then furthest away
let R = new Round({ players: [P('Ann'), P('Bob'), P('Cy')], format: 'stroke', holes });
R.startHole(0, () => ({ x: 0, y: 0, z: 0 }));
check(R.next(() => 0) === 0, 'first hole: the first player tees off');
R.sides[0].strokes = 1; R.sides[0].ball = { x: 0, z: 200 };
check(R.next(() => 0) === 1, 'then the next player still on the tee');
R.sides[1].strokes = 1; R.sides[1].ball = { x: 0, z: 150 }; R.sides[2].strokes = 1; R.sides[2].ball = { x: 0, z: 250 };
const d = (b) => 400 - b.z;
check(R.next(d) === 1, 'everyone off the tee: the one furthest from the hole');
R.holeOut(R.sides[1], 4);
check(R.next(d) === 0, 'holed out players are skipped');
R = new Round({ players: [P('Ann'), P('Bob')], format: 'stroke', holes });
play(R, 0, [5, 3]);
R.startHole(1, () => ({ x: 0, y: 0, z: 0 }));
check(R.next(() => 0) === 1, 'the honour goes to the best score on the last hole');

// stroke play and Stableford
R = new Round({ players: [P('Ann'), P('Bob')], format: 'stroke', holes });
play(R, 0, [4, 5]); play(R, 1, [3, 2]);
let st = R.standings();
check(st[0].name === 'Ann' && st[0].value === 'E' && st[1].value === 'E', 'stroke play: both level (Ann par par, Bob bogey birdie)');
R = new Round({ players: [P('Ann'), P('Bob')], format: 'stableford', holes });
play(R, 0, [4, 7]); play(R, 1, [2, 3]);
st = R.standings();
check(st[0].name === 'Ann' && st[0].value === '5 pts' && st[1].value === '2 pts', 'Stableford: par 2 + birdie 3; triple bogey 0 + par 2');
R = new Round({ players: [P('Ann'), P('Bob', { handicap: 18 })], format: 'stableford', handicaps: true, holes });
play(R, 0, [4, 5]);
check(R.stablefordPoints(R.sides[1], 0) === 2, 'Stableford with handicaps: a net par is 2 points');

// match play
R = new Round({ players: [P('Ann'), P('Bob')], format: 'match', holes });
play(R, 0, [4, 5]); play(R, 1, [3, 3]); play(R, 2, [6, 5]); play(R, 3, [3, 4]);
check(R.matchStatus().text === 'Ann 1 up', 'match play: won, halved, lost, won = 1 up');
for (let h = 4; h < 14; h++) play(R, h, [3, 4]);
check(/Ann won \d+&\d+/.test(R.matchStatus().text) && R.matchStatus().decided, `match decided early: "${R.matchStatus().text}"`);
R = new Round({ players: [P('Ann', { team: 'A' }), P('Bob', { team: 'B' }), P('Cy', { team: 'A' }), P('Di', { team: 'B' })], format: 'match', holes });
play(R, 0, [6, 5, 3, 6]);
check(R.matchStatus().text === 'Team A 1 up', 'four-ball match: the team\'s best ball wins the hole');

// skins
R = new Round({ players: [P('Ann'), P('Bob'), P('Cy')], format: 'skins', holes });
play(R, 0, [4, 4, 5]); play(R, 1, [3, 3, 3]); play(R, 2, [5, 4, 6]);
const sk = R.skins();
check(sk.won[1] === 3 && sk.won[0] === 0, 'skins: two ties carry, the third hole wins all three');

// best ball
R = new Round({ players: [P('Ann', { team: 'A' }), P('Bob', { team: 'B' }), P('Cy', { team: 'A' }), P('Di', { team: 'B' })], format: 'bestball', holes });
play(R, 0, [6, 4, 3, 5]);
st = R.standings();
check(st[0].name === 'Team A' && st[0].value === '-1', 'best ball: team A counts its 3');

// scramble: one ball per team, everyone hits, the best shot is played
R = new Round({ players: [P('Ann', { team: 'A' }), P('Bob', { team: 'A' }), P('Cy', { team: 'B' })], format: 'scramble', holes });
check(R.sides.length === 2 && R.sides[0].members.length === 2, 'scramble: a side per team');
R.startHole(0, () => ({ x: 0, y: 0, z: 0 }));
const s0 = R.sides[0];
check(R.hitter(s0).name === 'Ann', 'Ann hits first');
check(R.scrambleAttempt(s0, { ball: { x: 0, z: 200 }, lie: 'rough', strokes: 1 }, d) === null, 'after one shot: the next player hits');
check(R.hitter(s0).name === 'Bob', 'then Bob');
const best = R.scrambleAttempt(s0, { ball: { x: 0, z: 190 }, lie: 'fairway', strokes: 1 }, d);
check(best && best.by.name === 'Bob', 'Bob\'s fairway shot chosen over Ann\'s, just further on in the rough');

// alternate shot: the players take turns, and turns teeing off
R = new Round({ players: [P('Ann', { team: 'A' }), P('Bob', { team: 'A' }), P('Cy', { team: 'B' }), P('Di', { team: 'B' })], format: 'alternate', holes });
R.startHole(0, () => ({ x: 0, y: 0, z: 0 }));
const a = R.sides[0];
check(R.hitter(a).name === 'Ann', 'hole 1: Ann tees off'); a.shots++;
check(R.hitter(a).name === 'Bob', 'then Bob');
R.startHole(1, () => ({ x: 0, y: 0, z: 0 }));
check(R.hitter(R.sides[0]).name === 'Bob', 'hole 2: Bob tees off');

console.log(failed ? `${failed} game checks FAILED` : 'all game checks passed');
process.exit(failed ? 1 : 0);
