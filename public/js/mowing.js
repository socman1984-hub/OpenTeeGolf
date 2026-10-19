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
