// Weather for a round: presets, or live conditions at the course from Open-Meteo (free, no key).
// Wind directions are meteorological: the compass direction the wind blows FROM (0 = north).

export const PRESETS = {
  calm:   { label: 'Calm and sunny', windMph: 2, gustMph: 4, tempC: 24, humidity: 0.45, rainMm: 0, cloud: 0.1 },
  breezy: { label: 'Breezy', windMph: 9, gustMph: 15, tempC: 21, humidity: 0.55, rainMm: 0, cloud: 0.35 },
  windy:  { label: 'Windy', windMph: 18, gustMph: 30, tempC: 16, humidity: 0.6, rainMm: 0, cloud: 0.6 },
  rain:   { label: 'Rain', windMph: 11, gustMph: 20, tempC: 13, humidity: 0.95, rainMm: 2.5, cloud: 1 },
};

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
