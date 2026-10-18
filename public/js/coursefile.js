// Builds the course file object (holes, tees, pins, hazards, boundary) from simple course data.
// Shared by the course builder (server) and the course editor (browser).
//
// holes: [{ number, par, index, tees: {Black: {x,y,z}, ...}, aims: [{x,y,z}], greenCenter: {x,y,z},
//           pins: {Thursday: {x,y,z}, ...} }]
// hazards: [{ coords: [{x,y,z}], innerOOB }], oob: [{x,y,z}]

export const TEE_ORDER = ['Black', 'Blue', 'White', 'Green', 'Yellow', 'Red', 'Junior', 'Par3'];
export const PIN_DAYS = ['Thursday', 'Friday', 'Saturday', 'Sunday'];

export function makeCourseData({ name, location = '', lat = 0, lon = 0, altitudeM = 0, holes, hazards, oob, builder, designer = 'OpenTeeGolf course builder', stimp = 10 }) {
  const pathLen = (pts) => pts.slice(1).reduce((s, p, i) => s + Math.hypot(p.x - pts[i].x, p.z - pts[i].z), 0);
  const totals = Object.fromEntries(TEE_ORDER.map((t) => [t, 0]));
  const Holes = holes.map((h) => {
    const Tees = TEE_ORDER.map((t) => {
      const p = h.tees[t];
      const distance = p ? pathLen([p, ...h.aims, h.greenCenter]) : 0;
      totals[t] += distance;
      return { TeeType: t, Enabled: true, Distance: Math.round(distance * 1000) / 1000, Position: p || null };
    });
    [0, 1].forEach((k) => Tees.push({ TeeType: `AimPoint${k + 1}`, Enabled: true, Distance: 0, Position: h.aims[k] || null }));
    Tees.push({ TeeType: 'GreenCenterPoint', Enabled: false, Distance: 0, Position: h.greenCenter });
    return {
      Enabled: true, HoleNumber: h.number, Par: h.par, Index: h.index, Tees,
      Pins: PIN_DAYS.map((day) => ({ Day: day, Position: h.pins[day] || h.greenCenter })),
    };
  });
  return {
    gkversion: 3.0,
    CourseName: name,
    Location: location,
    Designer: designer,
    CourseLat: lat,
    CourseLong: lon,
    DefaultStimp: stimp,
    altitudeV2: Math.round(altitudeM * 3.28084),
    pOOB: { pointCount: oob.length, coords: oob },
    Holes,
    Hazards: hazards.map((hz) => ({
      pointCount: hz.coords.length, coords: hz.coords, DZpos: { x: 0, y: 0, z: 0 },
      freeDrop: false, innerOOB: !!hz.innerOOB, noAIL: false, hasDZ: false, islandGreen: false,
    })),
    TeeTypeTotalDistance: TEE_ORDER.map((t) => ({ TeeType: t, Enabled: true, Distance: Math.round(totals[t] * 1000) / 1000, Position: null })),
    GolfSimBuilder: builder,
  };
}
