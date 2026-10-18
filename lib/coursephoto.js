// A picture for each course's menu card, looked up once and saved in the course folder.
// Sources, best first:
//   1. Wikimedia Commons / Wikidata / Wikipedia (from the course's OpenStreetMap tags, or a Wikidata
//      search for a golf course of the same name nearby) — freely licensed, credited on the card
//   2. The course's own website (its og:image share picture)
// If nothing is found the menu falls back to the course's aerial imagery.
import fs from 'node:fs';
import path from 'node:path';
import { UA } from './realdata.js';

const MAX_BYTES = 8e6;
const RETRY_DAYS = 14;
const GOLF_COURSE = ['Q1048525', 'Q11281340', 'Q2335128']; // golf course, golf club, golf links
const EXT = { 'image/jpeg': 'jpg', 'image/png': 'png', 'image/webp': 'webp', 'image/gif': 'gif' };

const get = async (url, ms = 15000) => {
  const res = await fetch(url, { headers: { 'User-Agent': UA, 'Accept-Language': 'en' }, signal: AbortSignal.timeout(ms), redirect: 'follow' });
  if (!res.ok) throw new Error(`${res.status} ${url}`);
  return res;
};
const getJson = async (url) => (await get(url)).json();
const stripHtml = (s) => (s || '').replace(/<[^>]*>/g, '').replace(/\s+/g, ' ').trim();
const km = (a, b, c, d) => {
  const r = Math.PI / 180, x = (d - b) * r * Math.cos(((a + c) / 2) * r), y = (c - a) * r;
  return Math.hypot(x, y) * 6371;
};

// ---- Wikimedia helpers

const commonsUrl = (file) => `https://commons.wikimedia.org/wiki/Special:FilePath/${encodeURIComponent(file.replace(/^File:/i, ''))}?width=800`;

async function commonsCredit(file) {
  try {
    const title = 'File:' + file.replace(/^File:/i, '');
    const r = await getJson(`https://commons.wikimedia.org/w/api.php?action=query&format=json&prop=imageinfo&iiprop=extmetadata&titles=${encodeURIComponent(title)}`);
    const meta = Object.values(r.query?.pages || {})[0]?.imageinfo?.[0]?.extmetadata || {};
    const artist = stripHtml(meta.Artist?.value), license = stripHtml(meta.LicenseShortName?.value);
    return { credit: [artist, license].filter(Boolean).join(', '), page: `https://commons.wikimedia.org/wiki/${encodeURIComponent(title)}` };
  } catch { return { credit: '', page: '' }; }
}

async function commonsFile(file, source) {
  if (/^Category:/i.test(file)) {
    const r = await getJson(`https://commons.wikimedia.org/w/api.php?action=query&format=json&list=categorymembers&cmtype=file&cmlimit=10&cmtitle=${encodeURIComponent(file)}`);
    file = (r.query?.categorymembers || []).map((m) => m.title).find((t) => /\.(jpe?g|png|webp)$/i.test(t));
    if (!file) return null;
  }
  return { url: commonsUrl(file), source, ...(await commonsCredit(file)) };
}

async function wikidataEntity(qid) {
  const r = await getJson(`https://www.wikidata.org/wiki/Special:EntityData/${qid}.json`);
  return r.entities?.[qid];
}
const claim = (ent, p) => ent?.claims?.[p]?.[0]?.mainsnak?.datavalue?.value;

// A golf course on Wikidata with the same name within a few km
async function searchWikidata(name, lat, lon) {
  const r = await getJson(`https://www.wikidata.org/w/api.php?action=wbsearchentities&format=json&language=en&type=item&limit=7&search=${encodeURIComponent(name)}`);
  for (const hit of r.search || []) {
    const ent = await wikidataEntity(hit.id);
    const types = (ent?.claims?.P31 || []).map((c) => c.mainsnak?.datavalue?.value?.id);
    const pos = claim(ent, 'P625');
    if (!pos || km(lat, lon, pos.latitude, pos.longitude) > 4) continue;
    if (!types.some((t) => GOLF_COURSE.includes(t)) && !/golf|links/i.test(hit.label || '')) continue;
    return ent;
  }
  return null;
}

// ---- the course's own website

async function websiteImage(site) {
  if (!/^https?:\/\//i.test(site)) site = 'https://' + site;
  const res = await get(site, 12000);
  if (!/text\/html/i.test(res.headers.get('content-type') || '')) return null;
  const html = (await res.text()).slice(0, 2e6);
  const pick = (re) => html.match(re)?.[1];
  const found = pick(/<meta[^>]+property=["']og:image(?::secure_url)?["'][^>]+content=["']([^"']+)/i)
    || pick(/<meta[^>]+content=["']([^"']+)["'][^>]+property=["']og:image["']/i)
    || pick(/<meta[^>]+name=["']twitter:image["'][^>]+content=["']([^"']+)/i)
    || pick(/<link[^>]+rel=["']image_src["'][^>]+href=["']([^"']+)/i);
  if (!found || /logo|favicon|icon/i.test(found)) return null;
  const host = new URL(res.url || site).hostname.replace(/^www\./, '');
  return { url: new URL(found.replace(/&amp;/g, '&'), res.url || site).href, source: 'website', credit: `Photo: ${host}`, page: res.url || site };
}

// ---- lookup

async function candidates(info, log) {
  const out = [];
  let tags = {};
  if (info.osmType && info.osmId) {
    try { tags = (await getJson(`https://api.openstreetmap.org/api/0.6/${info.osmType}/${info.osmId}.json`)).elements?.[0]?.tags || {}; }
    catch (err) { log(`map tags: ${err.message}`); }
  }
  const sites = [tags.website, tags['contact:website'], tags.url].filter(Boolean);
  const tryStep = async (label, fn) => { try { const c = await fn(); if (c) out.push(c); } catch (err) { log(`${label}: ${err.message}`); } };

  if (tags.image) {
    await tryStep('image tag', async () => {
      const m = tags.image.match(/(File:[^?#]+)/i);
      if (m) return commonsFile(decodeURIComponent(m[1]).replace(/_/g, ' '), 'wikimedia');
      return /\.(jpe?g|png|webp)(\?|$)/i.test(tags.image) ? { url: tags.image, source: 'map', credit: '', page: tags.image } : null;
    });
  }
  if (tags.wikimedia_commons) await tryStep('commons', () => commonsFile(tags.wikimedia_commons, 'wikimedia'));

  let ent = null;
  await tryStep('wikidata', async () => {
    ent = tags.wikidata ? await wikidataEntity(tags.wikidata) : await searchWikidata(info.name, info.lat, info.lon);
    if (!ent) return null;
    const site = claim(ent, 'P856');
    if (site) sites.push(site);
    const img = claim(ent, 'P18');
    return img ? commonsFile(img, 'wikimedia') : null;
  });
  const wp = tags.wikipedia || (ent?.sitelinks?.enwiki && `en:${ent.sitelinks.enwiki.title}`);
  if (wp) {
    await tryStep('wikipedia', async () => {
      const [lang, ...t] = wp.includes(':') ? wp.split(':') : ['en', wp];
      const s = await getJson(`https://${lang}.wikipedia.org/api/rest_v1/page/summary/${encodeURIComponent(t.join(':').replace(/ /g, '_'))}`);
      const url = s.thumbnail?.source ? s.thumbnail.source.replace(/\/\d+px-/, '/800px-') : s.originalimage?.source;
      return url ? { url, source: 'wikipedia', credit: 'Wikipedia', page: s.content_urls?.desktop?.page || '' } : null;
    });
  }
  for (const site of [...new Set(sites)]) await tryStep('website', () => websiteImage(site));
  return out;
}

async function download(c) {
  const res = await get(c.url, 20000);
  const type = (res.headers.get('content-type') || '').split(';')[0].trim();
  if (!EXT[type]) throw new Error(`not an image (${type})`);
  const buf = Buffer.from(await res.arrayBuffer());
  if (buf.length > MAX_BYTES || buf.length < 4000) throw new Error('image too large or too small');
  return { buf, ext: EXT[type] };
}

// ---- cache in the course folder: photo.<ext> + photo.json

export const photoMeta = (dir) => { try { return JSON.parse(fs.readFileSync(path.join(dir, 'photo.json'), 'utf8')); } catch { return null; } };

export function savePhoto(dir, buf, ext, meta) {
  for (const f of fs.readdirSync(dir)) if (/^photo\.(jpg|png|webp|gif)$/.test(f)) fs.rmSync(path.join(dir, f));
  const file = `photo.${ext}`;
  fs.writeFileSync(path.join(dir, file), buf);
  const m = { file, ...meta, saved: new Date().toISOString() };
  fs.writeFileSync(path.join(dir, 'photo.json'), JSON.stringify(m, null, 1));
  return m;
}

// Lookups run two at a time so a long course list doesn't hammer the services
const pending = new Map();
const queue = [];
let running = 0;
const runNext = () => {
  while (running < 2 && queue.length) {
    const { fn, resolve } = queue.shift();
    running++;
    fn().then(resolve, () => resolve(null)).finally(() => { running--; runNext(); });
  }
};

// info: { name, lat, lon, osmType, osmId }; force: look again even if nothing was found before
export function ensurePhoto(dir, info, { force = false, log = () => {} } = {}) {
  const meta = photoMeta(dir);
  if (!force && meta) {
    if (meta.file && fs.existsSync(path.join(dir, meta.file))) return Promise.resolve(meta);
    if (meta.source === 'user' || Date.now() - Date.parse(meta.tried || 0) < RETRY_DAYS * 864e5) return Promise.resolve(meta);
  }
  if (pending.has(dir)) return pending.get(dir);
  const job = new Promise((resolve) => {
    queue.push({
      resolve,
      fn: async () => {
        for (const c of await candidates(info, log)) {
          try {
            const { buf, ext } = await download(c);
            log(`picture for ${info.name} from ${c.source}`);
            return savePhoto(dir, buf, ext, { source: c.source, credit: c.credit, page: c.page, url: c.url });
          } catch (err) { log(`${c.url}: ${err.message}`); }
        }
        const none = { file: null, tried: new Date().toISOString() };
        fs.writeFileSync(path.join(dir, 'photo.json'), JSON.stringify(none));
        return none;
      },
    });
    runNext();
  }).finally(() => pending.delete(dir));
  pending.set(dir, job);
  return job;
}
