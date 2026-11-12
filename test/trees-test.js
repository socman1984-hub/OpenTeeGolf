// Checks that one real tree becomes one tree: in the canopy height map and in the aerial photo
// (where its shadow joins its crown in one long dark shape).
import { treesFromCanopy, treesFromPhoto, removeTrees } from '../public/js/treeplace.js';

let failed = 0;
const check = (ok, what) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); if (!ok) failed++; };
const W = 200, H = 200;

// ---- a canopy map with one crown 14 m across, bumpy on top
const heights = new Uint8Array(W * H);
for (let j = 0; j < H; j++) for (let i = 0; i < W; i++) {
  const d = Math.hypot(i - 100, j - 100);
  if (d < 7) heights[j * W + i] = 14 + ((i * 7 + j * 13) % 3) * 2 - d * 0.4;
}
const cm = { w: W, h: H, minX: 0, maxZ: H, res: 1, heights };
check(treesFromCanopy(cm, () => true).length === 1, 'a bumpy crown in the height map is one tree');

// ---- a photo: speckled grass, dark crowns each with a shadow running up-left of it
function photo(crowns) {
  const data = new Uint8ClampedArray(W * H * 4);
  for (let j = 0; j < H; j++) for (let i = 0; i < W; i++) {
    const k = (j * W + i) * 4, v = (i * 31 + j * 17) % 11;
    let c = [70 + v, 120 + v, 50 + v];
    for (const [x, y, r] of crowns) {
      for (let s = 0; s <= 12; s++) if (Math.hypot(i - (x - s), j - (y - s)) < r) c = [8, 14, 8]; // shadow
    }
    for (const [x, y, r] of crowns) if (Math.hypot(i - x, j - y) < r) c = [48 + v, 85 + v, 40 + v]; // lit crown
    data[k] = c[0]; data[k + 1] = c[1]; data[k + 2] = c[2]; data[k + 3] = 255;
  }
  return { data, w: W, h: H, res: 1, minX: 0, maxZ: H };
}
const dist = (t, x, y) => Math.hypot(t.x - x, H - t.z - y);
const lone = treesFromPhoto(photo([[100, 100, 6]]));
check(lone.length === 1, `one tree and its shadow in the photo is one tree (got ${lone.length})`);
check(lone.length > 0 && dist(lone[0], 100, 100) < 4, 'and it stands on the crown, not the shadow');
check(lone.length > 0 && Math.abs(lone[0].r - 6) < 2, 'with a crown the size of the real one');
const three = [[50, 60, 5], [120, 60, 7], [90, 150, 6]];
check(treesFromPhoto(photo(three)).length === 3, 'three trees apart are three trees');
check(treesFromPhoto(photo(three), { allowed: (x) => x > 70 }).length === 2, 'and allowed() rules spots out');

// ---- taking a tree out in the editor: a mark anywhere on its crown does it
const big = { x: 0, z: 0, r: 8, h: 15 };
check(removeTrees([big], [[4, 0]]).length === 0, 'a removal on a big crown removes the tree');
check(removeTrees([big], [[9, 0]]).length === 1, 'and one beyond it leaves the tree');

if (failed) { console.error(`${failed} tree checks failed`); process.exit(1); }
console.log('all tree checks passed');
