// The sound of the streams: three looped recordings (a trickle, a running creek and rapids), mixed
// by how fast the nearest water runs and how sharply it drops, louder as the camera comes near and
// panned towards it. Browsers only allow sound after the player has clicked or pressed a key, so
// it starts then. (Recordings: CC0 field recordings from Freesound, via Creek Water Studio by
// bjlin-meshy; see NOTICE.md.)
//
// chains: the course's streams (streams.js: pts, hw, speed, fall per sample, course coordinates).
// Course x is the scene's mirrored.

const FILES = { trickle: 'audio/trickle.mp3', creek: 'audio/creek.mp3', rapids: 'audio/rapids.mp3' };
const HEAR = 60;              // m: silent beyond this from the water's edge

let ctx = null, master = null, volume = 0.6;
const buffers = {};

// One audio context for the page, made (and its recordings fetched) on the first click or key
function unlock() {
  if (ctx) { if (ctx.state === 'suspended' && !document.hidden) ctx.resume(); return; }
  try { ctx = new AudioContext(); } catch { return; }
  master = ctx.createGain();
  master.gain.value = volume;
  master.connect(ctx.destination);
  for (const [k, url] of Object.entries(FILES)) {
    fetch(url).then((r) => r.arrayBuffer()).then((b) => ctx.decodeAudioData(b)).then((buf) => { buffers[k] = buf; })
      .catch(() => { /* no sound then */ });
  }
}
for (const ev of ['pointerdown', 'keydown']) addEventListener(ev, unlock, { capture: true });
// (quiet while the game is in a background tab)
document.addEventListener('visibilitychange', () => { if (!ctx) return; if (document.hidden) ctx.suspend(); else ctx.resume(); });

// Volume 0..1 for all game sound
export function setVolume(v) {
  volume = v;
  if (master) master.gain.setTargetAtTime(v, ctx.currentTime, 0.05);
}

// A bang (the boom range): a burst of noise through a closing low-pass filter over a deep thump,
// bigger with size (about 0.5 to 3). pop: a short, bright pop instead (balloons, melons).
export function boom(size = 1, pop = false) {
  if (!ctx || ctx.state !== 'running') return;
  const t = ctx.currentTime, len = pop ? 0.25 : 0.8 + 0.5 * size;
  const noise = ctx.createBuffer(1, Math.ceil(ctx.sampleRate * len), ctx.sampleRate), d = noise.getChannelData(0);
  for (let i = 0; i < d.length; i++) d[i] = (Math.random() * 2 - 1) * Math.pow(1 - i / d.length, pop ? 6 : 2.2);
  const src = ctx.createBufferSource(), lp = ctx.createBiquadFilter(), gain = ctx.createGain();
  src.buffer = noise;
  lp.type = 'lowpass';
  lp.frequency.setValueAtTime(pop ? 6000 : 2600, t);
  lp.frequency.exponentialRampToValueAtTime(pop ? 1500 : 180, t + len);
  gain.gain.value = pop ? 0.5 : Math.min(1, 0.45 + 0.2 * size);
  src.connect(lp).connect(gain).connect(master);
  src.start(t);
  if (pop) return;
  const thump = ctx.createOscillator(), tg = ctx.createGain();
  thump.frequency.setValueAtTime(90, t);
  thump.frequency.exponentialRampToValueAtTime(32, t + 0.6);
  tg.gain.setValueAtTime(Math.min(1, 0.5 + 0.2 * size), t);
  tg.gain.exponentialRampToValueAtTime(0.001, t + 0.7 + 0.2 * size);
  thump.connect(tg).connect(master);
  thump.start(t);
  thump.stop(t + 1.2 + 0.2 * size);
}

export class CreekSound {
  constructor(chains) {
    this.chains = chains.filter((c) => c.speed && c.pts.length > 1);
    this.voices = null;
    this.wait = 0;
  }

  // The looping recordings, each through its own level, and a pan for them all
  start() {
    this.pan = ctx.createStereoPanner();
    this.pan.connect(master);
    this.voices = {};
    for (const k of Object.keys(FILES)) {
      const src = ctx.createBufferSource(), gain = ctx.createGain();
      src.buffer = buffers[k]; src.loop = true;
      gain.gain.value = 0;
      src.connect(gain).connect(this.pan);
      // (each loop from a different point, so the three don't line up)
      src.start(0, Math.random() * buffers[k].duration);
      this.voices[k] = { src, gain };
    }
  }

  update(camera, dt) {
    if (!this.chains.length || !ctx || ctx.state !== 'running') return;
    if (!this.voices) { if (Object.keys(FILES).every((k) => buffers[k])) this.start(); else return; }
    // (the nearest water a few times a second is plenty)
    this.wait -= dt;
    if (this.wait > 0) return;
    this.wait = 0.15;
    const cx = -camera.position.x, cz = camera.position.z;
    let best = Infinity, near = null;
    for (const c of this.chains) {
      for (let i = 0; i < c.pts.length; i++) {
        const [x, z] = c.pts[i], d = Math.hypot(x - cx, z - cz) - (c.hw?.[i] || 0);
        if (d < best) { best = d; near = { c, i, x, z }; }
      }
    }
    const t = ctx.currentTime;
    let mix = { trickle: 0, creek: 0, rapids: 0 }, pan = 0;
    if (near && best < HEAR) {
      const above = Math.max(0, camera.position.y - (near.c.level?.[near.i] ?? camera.position.y));
      const d = Math.hypot(Math.max(0, best), above);
      // (falls off with distance, gently at first)
      const loud = Math.max(0, 1 - d / HEAR) ** 2;
      // (how lively: from a slow trickle through a running creek to rapids where it's fast or drops)
      const lively = Math.min(1, near.c.speed[near.i] / 2.5 + (near.c.fall?.[near.i] || 0) * 6);
      mix = {
        trickle: loud * Math.max(0, 1 - lively * 2),
        creek: loud * (1 - Math.abs(lively - 0.5) * 2) * 0.9 + loud * 0.15,
        rapids: loud * Math.max(0, lively * 2 - 1),
      };
      // (panned towards the water, less so when it's all round)
      // (the camera's right is the first column of its world matrix; the water in the scene is at -x)
      const e = camera.matrixWorld.elements, dx = -near.x - camera.position.x, dz = near.z - camera.position.z;
      const len = Math.hypot(dx, dz) || 1;
      pan = ((dx * e[0] + dz * e[2]) / len) * Math.min(1, d / 8) * 0.7;
    }
    for (const [k, v] of Object.entries(this.voices)) v.gain.gain.setTargetAtTime(mix[k], t, 0.4);
    this.pan.pan.setTargetAtTime(Math.max(-1, Math.min(1, pan)), t, 0.3);
  }

  dispose() {
    if (!this.voices) return;
    for (const v of Object.values(this.voices)) { v.src.stop(); v.src.disconnect(); v.gain.disconnect(); }
    this.pan.disconnect();
    this.voices = null;
  }
}
