// Mowing patterns for fairways and greens. Chosen per course in the editor (saved in the course
// file as Mowing: { fairway, green }) and drawn by the terrain shader in scene.js.
// The order is the shader's pattern number, so add new patterns at the end.

export const MOW_PATTERNS = [
  { id: 'none', label: 'None (plain)' },
  { id: 'stripes', label: 'Straight stripes' },
  { id: 'diagonal', label: 'Diagonal stripes' },
  { id: 'checker', label: 'Checkerboard' },
  { id: 'diamond', label: 'Diamonds' },
];
export const MOW_DEFAULTS = { fairway: 'stripes', green: 'checker' };
// Width of one mower pass (m): a fairway unit cuts about 3.5 m, a green mower less
export const MOW_WIDTH = { fairway: 3.5, green: 1 };

export const mowIndex = (id) => Math.max(0, MOW_PATTERNS.findIndex((p) => p.id === id));
// How tall the course's rough and its tall native grass (heather) grow, chosen in the course
// editor: a scale on the grass's height (the rough's "long" is how it used to be)
export const GRASS_HEIGHTS = {
  rough: [{ id: 'short', label: 'Short', scale: 0.55 }, { id: 'medium', label: 'Medium', scale: 0.8 }, { id: 'long', label: 'Long', scale: 1.1 }],
  native: [{ id: 'short', label: 'Short', scale: 0.7 }, { id: 'medium', label: 'Medium', scale: 1 }, { id: 'tall', label: 'Tall', scale: 1.3 }],
};
const GRASS_DEFAULTS = { rough: 'medium', native: 'medium' };
// The course's choices ({ rough, native } ids) and their scales
export const courseGrass = (courseData) => {
  const pick = (k) => (GRASS_HEIGHTS[k].some((o) => o.id === courseData?.Grass?.[k]) ? courseData.Grass[k] : GRASS_DEFAULTS[k]);
  const rough = pick('rough'), native = pick('native');
  return { rough, native, roughScale: GRASS_HEIGHTS.rough.find((o) => o.id === rough).scale, nativeScale: GRASS_HEIGHTS.native.find((o) => o.id === native).scale };
};

// The colours of the fairways, greens, tees and rough: the game's own, or the aerial photo's
// (the photo shows through, with the game's shapes, edges, mowing stripes and grass detail on top)
export const SURFACE_LOOKS = [
  { id: 'game', label: 'Game colours (even, groomed)' },
  { id: 'photo', label: 'Aerial photo colours' },
];
// Each part of the course can take its colours from the game or the photo: { fairway, green,
// rough } ('game' | 'photo'). Saved as 'photo' when every part is, as older versions read it.
export const SURFACE_PARTS = [
  { id: 'fairway', label: 'Fairways & tees' },
  { id: 'green', label: 'Greens' },
  { id: 'rough', label: 'Rough' },
];
export const courseSurface = (courseData) => {
  const s = courseData?.Surface, all = (v) => Object.fromEntries(SURFACE_PARTS.map((p) => [p.id, v]));
  if (s === 'photo') return all('photo');
  if (s && typeof s === 'object') return Object.fromEntries(SURFACE_PARTS.map((p) => [p.id, s[p.id] === 'photo' ? 'photo' : 'game']));
  return all('game');
};
// How to save it (null: all the game's, the default)
export const surfaceData = (surface) => {
  const v = SURFACE_PARTS.map((p) => surface?.[p.id] === 'photo');
  return v.every(Boolean) ? 'photo' : v.some(Boolean) ? Object.fromEntries(SURFACE_PARTS.map((p, i) => [p.id, v[i] ? 'photo' : 'game'])) : null;
};

export const courseMowing = (courseData) => ({
  fairway: MOW_PATTERNS.some((p) => p.id === courseData?.Mowing?.fairway) ? courseData.Mowing.fairway : MOW_DEFAULTS.fairway,
  green: MOW_PATTERNS.some((p) => p.id === courseData?.Mowing?.green) ? courseData.Mowing.green : MOW_DEFAULTS.green,
});

// -1..1 light/dark at (u, v) metres, u across the mowing direction and v along it (the same
// pattern the shader draws, for the editor's preview)
export function mowShade(id, u, v, width) {
  const s = (a) => Math.max(-1, Math.min(1, Math.sin((a / width) * Math.PI) * 6));
  const d1 = (u + v) / Math.SQRT2, d2 = (v - u) / Math.SQRT2;
  switch (id) {
    case 'stripes': return s(u);
    case 'diagonal': return s(d1);
    case 'checker': return s(u) * s(v);
    case 'diamond': return s(d1) * s(d2);
    default: return 0;
  }
}
