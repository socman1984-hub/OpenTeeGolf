// What each building around a course is (house, clubhouse, garage, shed, barn, porch, commercial),
// shared by the game (which draws each its own way, see buildings.js) and the course editor (where
// the player can change it). No three.js here, so the editor can use it too.
//
// The guess, in order:
//   1. the player's choice in the editor (types: { key: type })
//   2. what OpenStreetMap says (the building tag, amenity, leisure...)
//   3. the clubhouse: of the buildings big enough to be one, the closest to where the round starts
//      and turns (hole 1's and 10's tees, 9's and 18's greens)
//   4. tiny ones are sheds
//   5. anything else is a house
// A building's key is its centre, rounded to the metre ("x,z"), so a choice sticks to it.

export const BUILDING_TYPES = [
  { id: 'house', label: 'House', colour: '#e8c46a' },
  { id: 'clubhouse', label: 'Clubhouse', colour: '#ff7a59' },
  { id: 'garage', label: 'Garage', colour: '#9fb6c9' },
  { id: 'shed', label: 'Shed', colour: '#b8a07a' },
  { id: 'barn', label: 'Barn', colour: '#c0473a' },
  { id: 'porch', label: 'Porch / shelter (roof on posts)', colour: '#7fd1b9' },
  { id: 'commercial', label: 'Commercial (shop, office, school)', colour: '#b48fe0' },
];
const IDS = new Set(BUILDING_TYPES.map((t) => t.id));
export const isBuildingType = (t) => IDS.has(t);

const FROM_TAG = {
  house: 'house', detached: 'house', residential: 'house', semidetached_house: 'house', terrace: 'house', bungalow: 'house',
  cabin: 'house', villa: 'house', farm: 'house', static_caravan: 'house', apartments: 'commercial', dormitory: 'commercial',
  garage: 'garage', garages: 'garage',
  shed: 'shed', hut: 'shed', storage_tank: 'shed', service: 'shed', toilets: 'shed', kiosk: 'shed',
  barn: 'barn', farm_auxiliary: 'barn', stable: 'barn', cowshed: 'barn', greenhouse: 'barn',
  roof: 'porch', carport: 'porch', shelter: 'porch', pavilion: 'porch', canopy: 'porch',
  clubhouse: 'clubhouse',
  commercial: 'commercial', retail: 'commercial', industrial: 'commercial', warehouse: 'commercial', office: 'commercial',
  school: 'commercial', hotel: 'commercial', church: 'commercial', civic: 'commercial', public: 'commercial', hospital: 'commercial',
  university: 'commercial', kindergarten: 'commercial', sports_hall: 'commercial', government: 'commercial', supermarket: 'commercial',
};

export function footprintInfo(pts) {
  let a = 0, cx = 0, cz = 0;
  const n = pts.length - (pts.length > 1 && pts[0][0] === pts[pts.length - 1][0] && pts[0][1] === pts[pts.length - 1][1] ? 1 : 0);
  for (let i = 0; i < n; i++) {
    const [x0, z0] = pts[i], [x1, z1] = pts[(i + 1) % n], c = x0 * z1 - x1 * z0;
    a += c; cx += (x0 + x1) * c; cz += (z0 + z1) * c;
  }
  if (Math.abs(a) < 1e-6) { let sx = 0, sz = 0; for (let i = 0; i < n; i++) { sx += pts[i][0]; sz += pts[i][1]; } return { area: 0, cx: sx / n, cz: sz / n }; }
  return { area: Math.abs(a) / 2, cx: cx / (3 * a), cz: cz / (3 * a) };
}
export const buildingKey = (b) => { const { cx, cz } = footprintInfo(b.pts); return `${Math.round(cx)},${Math.round(cz)}`; };

// buildings: [{ pts, kind?, use?, name? }]; start: [{ x, z }] where the round starts and turns
// (hole 1 tee, 9 green, 10 tee, 18 green - whichever exist); types: the player's choices.
// Returns [{ type, key, auto }] (auto: the guess, before the player's choice).
export function classifyBuildings(buildings, start = [], types = {}) {
  const info = buildings.map((b) => ({ ...footprintInfo(b.pts), key: buildingKey(b) }));
  const guess = buildings.map((b) => {
    const tag = String(b.kind || '').toLowerCase(), use = String(b.use || '').toLowerCase(), name = String(b.name || '').toLowerCase();
    if (/club ?house|pro ?shop/.test(name) || use === 'club' || tag === 'clubhouse') return 'clubhouse';
    if (FROM_TAG[tag]) return FROM_TAG[tag];
    if (['restaurant', 'cafe', 'bar', 'pub', 'golf_course', 'sports_centre'].includes(use)) return 'clubhouse';
    return null;
  });
  // The clubhouse, when the map doesn't say: a building of 150 m² or more, within 350 m of the
  // places the round starts and turns, the closest to them on average
  if (!guess.includes('clubhouse') && start.length) {
    let best = -1, bestD = Infinity;
    info.forEach((f, i) => {
      if (guess[i] && guess[i] !== 'house' && guess[i] !== 'commercial') return;
      if (f.area < 150) return;
      const d = start.reduce((s, p) => s + Math.hypot(p.x - f.cx, p.z - f.cz), 0) / start.length;
      if (d < 350 && d < bestD) { bestD = d; best = i; }
    });
    if (best >= 0) guess[best] = 'clubhouse';
  }
  return buildings.map((b, i) => {
    const auto = guess[i] || (info[i].area < 20 ? 'shed' : 'house');
    const chosen = types[info[i].key];
    return { key: info[i].key, auto, type: isBuildingType(chosen) ? chosen : auto };
  });
}

// Where the round starts and turns, from holes with tees and greens: [{ tees: [{x, z}], green: {x, z} }]
export function roundTurns(holes) {
  const out = [];
  const tee = (h) => h?.tees?.[0], green = (h) => h?.green;
  const add = (p) => { if (p && Number.isFinite(p.x) && Number.isFinite(p.z)) out.push({ x: p.x, z: p.z }); };
  add(tee(holes[0])); add(green(holes[8])); add(tee(holes[9])); add(green(holes[17]));
  return out;
}
