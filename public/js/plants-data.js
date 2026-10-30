// The shrubs, flowers and grasses plants.js draws, where each grows and the course's own choices
// about them: plain data, with no 3D, so the course editor and course.js can use it too.

// The kinds: their name and group in the course editor, cell of the picture, form, size (w across,
// h tall, m), how far off it's still drawn (m), whether it casts a shadow, tint (multiplies the
// picture) and its Cartoon colour
export const PLANTS = {
  boxwood: { label: 'Boxwood', group: 'shrub', about: 'rounded evergreen shrub', cell: 0, form: 'bush', w: 1.2, h: 0.9, far: 260, shadow: true, toon: 0x2f6e2a },
  hedge: { label: 'Clubhouse hedge', group: 'shrub', about: 'clipped boxwood along the clubhouse walls', cell: 0, form: 'hedge', w: 0.9, h: 1.1, far: 300, shadow: true, toon: 0x2f6e2a },
  azalea: { label: 'Azalea', group: 'shrub', about: 'pink flowers, woods edges (the South)', cell: 1, form: 'bush', w: 1.6, h: 1.2, far: 260, shadow: true, toon: 0xf2609a },
  rhododendron: { label: 'Rhododendron', group: 'shrub', about: 'big purple flower heads, woods edges', cell: 2, form: 'bush', w: 2.4, h: 2.0, far: 280, shadow: true, toon: 0xb05ac8 },
  holly: { label: 'Holly shrub', group: 'shrub', about: 'dark spiny leaves, red berries', cell: 3, form: 'bush', w: 1.6, h: 1.7, far: 260, shadow: true, toon: 0x1f5a2a },
  sagebrush: { label: 'Sagebrush', group: 'desert', about: 'grey-blue scrub (dry country)', cell: 12, form: 'bush', w: 1.3, h: 0.9, far: 220, shadow: true, toon: 0x9aae94 },
  creosote: { label: 'Creosote bush', group: 'desert', about: 'olive-green desert scrub', cell: 12, form: 'bush', w: 1.8, h: 1.5, far: 240, shadow: true, tint: [0.7, 0.95, 0.55], toon: 0x6f8a4a, cards: 20 },
  fern: { label: 'Fern', group: 'ground', about: 'under the trees', cell: 4, form: 'fern', w: 1.4, h: 0.75, far: 75, shadow: false, toon: 0x4f9a3a },
  fountaingrass: { label: 'Fountain grass', group: 'grass', about: 'arching grass with buff plumes', cell: 5, form: 'clump', w: 1.2, h: 1.0, far: 170, shadow: false, toon: 0xb8b078 },
  pampas: { label: 'Pampas grass', group: 'grass', about: 'tall cream plumes', cell: 6, form: 'clump', w: 2.0, h: 2.6, far: 280, shadow: true, toon: 0xece4c8 },
  muhly: { label: 'Pink muhly grass', group: 'grass', about: 'a pink haze in autumn', cell: 7, form: 'clump', w: 1.3, h: 1.0, far: 190, shadow: false, toon: 0xe28ab8 },
  bedred: { label: 'Red flower beds', group: 'flowers', about: 'behind tees and by the clubhouse', cell: 8, form: 'flowers', w: 1.2, h: 0.45, far: 170, shadow: false, toon: 0xe0403a },
  bedyellow: { label: 'Yellow flower beds', group: 'flowers', about: 'behind tees, by the clubhouse and ponds', cell: 9, form: 'flowers', w: 1.2, h: 0.45, far: 170, shadow: false, toon: 0xf2c232 },
  bedpurple: { label: 'Purple flower beds', group: 'flowers', about: 'salvia spikes; irises by ponds', cell: 10, form: 'flowers', w: 1.2, h: 0.5, far: 170, shadow: false, toon: 0x7a58c8 },
  bedwhite: { label: 'White and pink flower beds', group: 'flowers', about: 'behind tees and by the clubhouse', cell: 11, form: 'flowers', w: 1.2, h: 0.45, far: 170, shadow: false, toon: 0xf4f0ea },
  yucca: { label: 'Yucca', group: 'desert', about: 'spiky rosette, tall flower stalk', cell: 13, form: 'clump', w: 1.4, h: 1.9, far: 230, shadow: true, toon: 0x7f9a66 },
  palmetto: { label: 'Saw palmetto', group: 'ground', about: 'low fans under the pines (Florida)', cell: 14, form: 'palmetto', w: 1.9, h: 1.2, far: 150, shadow: false, toon: 0x4f8a48 },
  pricklypear: { label: 'Prickly pear', group: 'desert', about: 'flat cactus pads', cell: 15, form: 'clump', w: 1.3, h: 1.1, far: 170, shadow: false, tint: [0.72, 0.84, 0.8], toon: 0x6f9a52 },
};

// What grows where, by the course's region (treeplace.js treeRegion): weighted mixes for woods
// edges, the woods floor, ornamental grasses, flower bed colours, pond banks and (dry country) the
// rough. Missing: none of that kind.
export const ARID = { edge: { creosote: 3, sagebrush: 1, pricklypear: 2, yucca: 2 }, grass: { fountaingrass: 2, muhly: 1, yucca: 1 }, beds: ['bedyellow', 'bedred', 'bedpurple'], bank: { fountaingrass: 2, pampas: 1 }, rough: { creosote: 4, sagebrush: 2, pricklypear: 2, yucca: 2 }, hedge: 'boxwood', beds_p: 0.7 };
export const PLANT_REGIONS = {
  temperate: { edge: { azalea: 2, rhododendron: 2, holly: 1, boxwood: 1 }, floor: { fern: 1 }, grass: { fountaingrass: 3, pampas: 1 }, beds: ['bedred', 'bedyellow', 'bedpurple', 'bedwhite'], bank: { fountaingrass: 2, bedyellow: 1, bedpurple: 1 }, beds_p: 0.45 },
  southeast: { edge: { azalea: 6, holly: 2, rhododendron: 1, boxwood: 1 }, floor: { fern: 1 }, grass: { muhly: 3, fountaingrass: 2, pampas: 1 }, beds: ['bedred', 'bedyellow', 'bedwhite', 'bedpurple'], bank: { muhly: 1, pampas: 1, bedyellow: 1 }, beds_p: 0.7 },
  subtropical: { edge: { palmetto: 6, azalea: 1, holly: 1 }, floor: { palmetto: 2, fern: 1 }, grass: { muhly: 2, fountaingrass: 2, pampas: 2 }, beds: ['bedred', 'bedyellow', 'bedwhite', 'bedpurple'], bank: { pampas: 2, fountaingrass: 1, bedyellow: 1 }, beds_p: 0.7 },
  desert: ARID,
  sonoran: ARID,
  calcoast: { edge: { sagebrush: 2, boxwood: 1, rhododendron: 1 }, floor: { fern: 1 }, grass: { fountaingrass: 2, pampas: 1 }, beds: ['bedpurple', 'bedyellow', 'bedwhite'], bank: { pampas: 1, fountaingrass: 1 }, beds_p: 0.45 },
  socal: { edge: { sagebrush: 2, yucca: 1, boxwood: 1 }, grass: { fountaingrass: 2, muhly: 1 }, beds: ['bedpurple', 'bedred', 'bedyellow'], bank: { pampas: 1, fountaingrass: 1 }, rough: { sagebrush: 3, yucca: 1, pricklypear: 1 }, beds_p: 0.6 },
  mountain: { edge: { sagebrush: 3, boxwood: 1 }, grass: { fountaingrass: 2 }, beds: ['bedpurple', 'bedyellow', 'bedred'], bank: { fountaingrass: 1, bedyellow: 1 }, rough: { sagebrush: 1 }, beds_p: 0.35 },
  pacificnw: { edge: { rhododendron: 5, holly: 1, boxwood: 1 }, floor: { fern: 3 }, grass: { fountaingrass: 2 }, beds: ['bedpurple', 'bedwhite', 'bedred'], bank: { fountaingrass: 1, bedyellow: 1 }, beds_p: 0.4 },
  mediterranean: { edge: { sagebrush: 1, boxwood: 2 }, grass: { pampas: 1, fountaingrass: 2 }, beds: ['bedpurple', 'bedred', 'bedwhite'], bank: { pampas: 1 }, beds_p: 0.45 },
  tropical: { edge: { palmetto: 3, azalea: 1 }, floor: { palmetto: 1, fern: 1 }, grass: { fountaingrass: 2, pampas: 1 }, beds: ['bedred', 'bedyellow', 'bedwhite'], bank: { pampas: 1 }, beds_p: 0.7 },
};
// The groups as the course editor lists them
export const PLANT_GROUPS = [
  { id: 'shrub', label: 'Shrubs' }, { id: 'flowers', label: 'Flower beds' }, { id: 'grass', label: 'Ornamental grasses' },
  { id: 'ground', label: 'Under the trees' }, { id: 'desert', label: 'Desert plants' },
];
// The kinds that grow on a course in this region on their own (the clubhouse hedge everywhere)
export function plantsHere(region) {
  const R = PLANT_REGIONS[region] || PLANT_REGIONS.temperate, out = new Set(['hedge']);
  for (const k of ['edge', 'floor', 'grass', 'bank', 'rough']) for (const id of Object.keys(R[k] || {})) out.add(id);
  for (const id of R.beds || []) out.add(id);
  return out;
}
// The course's own choices (the course editor; saved as Plants: { off, extra, amount }): kinds
// turned off, kinds added that don't grow in the region on their own, and how many (1 = as usual)
export const coursePlants = (courseData) => {
  const P = courseData?.Plants, list = (v) => (Array.isArray(v) ? v.filter((id) => PLANTS[id]) : []);
  const amount = Number.isFinite(+P?.amount) ? Math.min(2, Math.max(0, +P.amount)) : 1;
  return { off: list(P?.off), extra: list(P?.extra), amount };
};
// The region's mixes with the course's choices applied: kinds turned off taken out, added ones put
// in where their kind grows (shrubs and desert plants along the woods, desert plants in the scrub
// too, ferns and palmetto under the trees, grasses round greens, beds and ponds, flower colours in
// the beds)
export function courseMixes(R, off, extra) {
  const mix = (m) => Object.fromEntries(Object.entries(m || {}).filter(([id]) => !off.has(id)));
  const out = { ...R, edge: mix(R.edge), floor: mix(R.floor), grass: mix(R.grass), bank: mix(R.bank), rough: R.rough ? mix(R.rough) : null, beds: (R.beds || []).filter((id) => !off.has(id)) };
  for (const id of extra) {
    if (off.has(id) || !PLANTS[id]) continue;
    const g = PLANTS[id].group;
    if (g === 'shrub' && id !== 'hedge') out.edge[id] = out.edge[id] || 2;
    if (g === 'desert') { out.edge[id] = out.edge[id] || 2; if (out.rough) out.rough[id] = out.rough[id] || 2; }
    if (g === 'ground') out.floor[id] = out.floor[id] || 2;
    if (g === 'grass') { out.grass[id] = out.grass[id] || 2; out.bank[id] = out.bank[id] || 1; }
    if (g === 'flowers' && !out.beds.includes(id)) out.beds.push(id);
  }
  return out;
}
