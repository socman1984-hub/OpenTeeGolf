// The snapshot of every golf course mapped in OpenStreetMap that ships with the game
// (lib/world-courses.json.gz: { made, rows: [[lat, lon, n|w|r, id, name], ...] }), so the course
// builder's map is full the first time it opens. Walks the world's tiles as the server does (those
// fetched in the last month come from data/cache), then writes the snapshot.
//
//   node scripts/world-courses.js        (unreal/make_release.ps1 runs it before each release)

import fs from 'node:fs';
import zlib from 'node:zlib';
import { worldCoursesAll, WORLD_SNAPSHOT } from '../lib/realdata.js';

const rows = await worldCoursesAll((m) => console.log(m));
// (in a steady order, so the file only changes where the courses do)
rows.sort((a, b) => a[3] - b[3]);
const json = JSON.stringify({ made: new Date().toISOString().slice(0, 10), rows });
fs.writeFileSync(WORLD_SNAPSHOT, zlib.gzipSync(json, { level: 9 }));
console.log(`${rows.length} golf courses in ${WORLD_SNAPSHOT} (${(fs.statSync(WORLD_SNAPSHOT).size / 1024 / 1024).toFixed(2)} MB)`);
