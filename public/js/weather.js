// Weather for a round: presets, or live conditions at the course from Open-Meteo (free, no key).
// Wind directions are meteorological: the compass direction the wind blows FROM (0 = north).

// fog: 0 clear air .. 1 thick fog (about 200 m to see)
export const PRESETS = {
  calm:     { label: 'Calm and sunny', windMph: 2, gustMph: 4, tempC: 24, humidity: 0.45, rainMm: 0, cloud: 0.1 },
  breezy:   { label: 'Breezy', windMph: 9, gustMph: 15, tempC: 21, humidity: 0.55, rainMm: 0, cloud: 0.35 },
  windy:    { label: 'Windy', windMph: 18, gustMph: 30, tempC: 16, humidity: 0.6, rainMm: 0, cloud: 0.6 },
  overcast: { label: 'Overcast', windMph: 6, gustMph: 10, tempC: 15, humidity: 0.75, rainMm: 0, cloud: 0.95 },
  fog:      { label: 'Fog', windMph: 1, gustMph: 3, tempC: 11, humidity: 1, rainMm: 0, cloud: 0.85, fog: 1 },
  rain:     { label: 'Rain', windMph: 11, gustMph: 20, tempC: 13, humidity: 0.95, rainMm: 2.5, cloud: 1 },
  storm:    { label: 'Storm', windMph: 22, gustMph: 38, tempC: 12, humidity: 1, rainMm: 7, cloud: 1, fog: 0.3 },
};

// ---- the sun

const RAD = Math.PI / 180;
// The sun's declination (degrees) on a day of the year (1-365)
const declination = (day) => 23.44 * Math.sin((2 * Math.PI * (284 + day)) / 365);
export const dayOfYear = (date = new Date()) => Math.floor((date - new Date(date.getFullYear(), 0, 0)) / 86400000);

// Where the sun stands at latitude lat (degrees) on a day of the year, at a solar time (hours;
// 12 = noon, when it's highest): elevation above the horizon and azimuth clockwise from north,
// both in degrees
export function sunAt(lat, day, hour) {
  const d = declination(day) * RAD, p = lat * RAD, h = (hour - 12) * 15 * RAD;
  const el = Math.asin(Math.sin(p) * Math.sin(d) + Math.cos(p) * Math.cos(d) * Math.cos(h));
  const az = Math.atan2(Math.sin(h), Math.cos(h) * Math.sin(p) - Math.tan(d) * Math.cos(p)) / RAD + 180;
  return { elevation: el / RAD, azimuth: ((az % 360) + 360) % 360 };
}

// Sunrise and sunset (solar hours) at latitude lat on a day of the year
export function daylight(lat, day) {
  const d = declination(day) * RAD, p = lat * RAD;
  const c = (Math.sin(-0.83 * RAD) - Math.sin(p) * Math.sin(d)) / (Math.cos(p) * Math.cos(d));
  if (c <= -1) return [0, 24];   // (midnight sun)
  if (c >= 1) return [11.5, 12.5]; // (polar night: a glimmer at noon)
  const h0 = Math.acos(c) / RAD / 15;
  return [12 - h0, 12 + h0];
}

// The solar time now at longitude lon (hours)
export const solarNow = (lon) => { const t = new Date(); return (((t.getUTCHours() + t.getUTCMinutes() / 60 + lon / 15) % 24) + 24) % 24; };

// Times of day for a round (solar hours, from the day's sunrise and sunset); 'live' is now
export const TIMES = {
  live: { label: 'Now at the course' },
  early: { label: 'Early morning', at: ([rise]) => rise + 0.9 },
  morning: { label: 'Morning', at: ([rise]) => Math.max(rise + 2, 9) },
  midday: { label: 'Midday', at: () => 12.5 },
  afternoon: { label: 'Afternoon', at: ([, set]) => Math.min(set - 2.5, 15.5) },
  golden: { label: 'Golden hour', at: ([, set]) => set - 1.1 },
  sunset: { label: 'Sunset', at: ([, set]) => set - 0.35 },
};
// The solar hour for a time of day (kept inside daylight: the game has no night)
export function timeHour(id, lat, lon, day) {
  const dl = daylight(lat, day);
  const h = id === 'live' ? solarNow(lon ?? 0) : (TIMES[id]?.at || TIMES.midday.at)(dl);
  return Math.min(dl[1] - 0.3, Math.max(dl[0] + 0.3, h));
}
// A solar hour as a clock time, e.g. 6:45 pm (solar time: within an hour or so of the clocks there)
export const clockText = (h) => { const m = Math.round(h * 60), hh = Math.floor(m / 60) % 24, mm = m % 60; return `${((hh + 11) % 12) + 1}:${String(mm).padStart(2, '0')} ${hh < 12 ? 'am' : 'pm'}`; };

const CODES = [
  [0, 'Clear'], [1, 'Mostly clear'], [2, 'Partly cloudy'], [3, 'Overcast'], [45, 'Fog'], [48, 'Fog'],
  [51, 'Drizzle'], [53, 'Drizzle'], [55, 'Drizzle'], [56, 'Freezing drizzle'], [57, 'Freezing drizzle'],
  [61, 'Light rain'], [63, 'Rain'], [65, 'Heavy rain'], [66, 'Freezing rain'], [67, 'Freezing rain'],
  [71, 'Light snow'], [73, 'Snow'], [75, 'Heavy snow'], [77, 'Snow'], [80, 'Showers'], [81, 'Showers'],
  [82, 'Heavy showers'], [85, 'Snow showers'], [86, 'Snow showers'], [95, 'Thunderstorm'], [96, 'Thunderstorm'], [99, 'Thunderstorm'],
];

export async function fetchLiveWeather(lat, lon) {
  const url = 'https://api.open-meteo.com/v1/forecast?latitude=' + lat.toFixed(4) + '&longitude=' + lon.toFixed(4) +
    '&current=temperature_2m,relative_humidity_2m,precipitation,cloud_cover,surface_pressure,weather_code,' +
    'wind_speed_10m,wind_direction_10m,wind_gusts_10m&wind_speed_unit=mph';
  const res = await fetch(url, { signal: AbortSignal.timeout(10000) });
  if (!res.ok) throw new Error(`weather service ${res.status}`);
  const c = (await res.json()).current;
  return {
    label: (CODES.find(([k]) => k === c.weather_code) || [0, 'Current conditions'])[1],
    windMph: c.wind_speed_10m,
    gustMph: Math.max(c.wind_gusts_10m ?? 0, c.wind_speed_10m),
    windFromMet: c.wind_direction_10m,
    tempC: c.temperature_2m,
    humidity: c.relative_humidity_2m / 100,
    pressureHpa: c.surface_pressure,
    rainMm: c.precipitation,
    cloud: c.cloud_cover / 100,
    fog: c.weather_code === 45 || c.weather_code === 48 ? 1 : c.weather_code >= 95 ? 0.3 : 0,
    live: true,
  };
}

// How wet the ground plays (0 dry .. 1 soaked)
export const wetness = (w) => (w.rainMm > 0.05 ? Math.min(1, 0.35 + w.rainMm / 3) : 0);

// A gust for one shot: somewhere between the steady wind and the gust speed, direction wobbling a bit
export function gustFor(w) {
  const r = Math.random();
  const mph = w.windMph + (w.gustMph - w.windMph) * r * r;
  const wobble = (Math.random() - 0.5) * (8 + (w.gustMph - w.windMph) * 1.2);
  return { mph, wobbleDeg: wobble };
}
