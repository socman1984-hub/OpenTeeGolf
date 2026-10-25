// A picture for each course's menu card, looked up once and saved in the course folder.
// Sources, best first:
//   1. Wikimedia Commons / Wikidata / Wikipedia (from the course's OpenStreetMap tags, or a Wikidata
//      search for a golf course of the same name nearby) — freely licensed, credited on the card
//   2. The course's own website: its share picture (og:image), else its logo (the site's logo
//      image, the logo it declares for search engines, or its app icon). The website comes from the
//      map's tags, Wikidata, a map feature of the same name nearby (the clubhouse), or else the
//      usual web addresses for the name (echohillsgolf.com, ...), taken only when the page names the
//      course and where it is.
// If nothing is found the menu falls back to the course's aerial imagery.
// photo.json records whether the picture is a photo or a logo (kind), so the menu can show a logo
// whole on a plain background rather than cropped.
import fs from 'node:fs';
import path from 'node:path';
import { UA } from './realdata.js';

const MAX_BYTES = 8e6;
const RETRY_DAYS = 14;
// (bumped when the lookup learns new places to look: courses that found nothing look again)
const LOOKUP_VERSION = 2;
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

async function fetchPage(site, ms = 12000) {
  if (!/^https?:\/\//i.test(site)) site = 'https://' + site;
  const res = await get(site, ms);
  if (!/text\/html/i.test(res.headers.get('content-type') || '')) return null;
  return { html: (await res.text()).slice(0, 2e6), url: res.url || site };
}

const attr = (tag, name) => tag.match(new RegExp(`\\s${name}\\s*=\\s*(?:"([^"]*)"|'([^']*)'|([^\\s>]+))`, 'i'))?.slice(1).find((v) => v != null);
// (logos of others: booking systems, social networks, awards, sponsors, card schemes)
const NOT_THEIRS = /golfnow|powered|teesnap|foreup|chronogolf|lightspeed|clubessential\/.*\/(?:pga|usga)|facebook|instagram|twitter|youtube|tiktok|linkedin|tripadvisor|yelp|google|apple|visa|mastercard|award|badge|sponsor|partner|footer|pga[-_ ]?logo|usga|gift/i;

// Pictures the website offers, best first: its share picture, then its logo
function websiteImages({ html, url }) {
  const out = [], seen = new Set();
  const host = new URL(url).hostname.replace(/^www\./, '');
  const add = (src, kind) => {
    if (!src || /^data:/i.test(src) || /\.(svg|ico)(\?|$)/i.test(src)) return;
    let abs;
    try { abs = new URL(src.replace(/&amp;/g, '&').trim(), url).href; } catch { return; }
    if (seen.has(abs)) return;
    seen.add(abs);
    out.push({ url: abs, source: 'website', kind, credit: `${kind === 'logo' ? 'Logo' : 'Photo'}: ${host}`, page: url });
  };
  // (a share picture that is really a logo or an icon waits until after the page's own logo: some
  // sites share a generic golf icon)
  const later = [];
  for (const m of html.match(/<meta[^>]+>/gi) || []) {
    const key = (attr(m, 'property') || attr(m, 'name') || '').toLowerCase();
    if (!/^(og:image(:secure_url|:url)?|twitter:image(:src)?)$/.test(key)) continue;
    const c = attr(m, 'content');
    if (/logo|favicon|icon/i.test(c || '')) later.push(c); else add(c, 'photo');
  }
  const imageSrc = (html.match(/<link[^>]+rel=["']image_src["'][^>]*>/i) || [])[0];
  if (imageSrc) add(attr(imageSrc, 'href'), 'photo');
  // The logo the site declares for search engines (JSON-LD)
  for (const m of html.matchAll(/"logo"\s*:\s*(?:"([^"]+)"|\{[^}]*?"url"\s*:\s*"([^"]+)")/g)) add((m[1] || m[2]).replace(/\\\//g, '/'), 'logo');
  // Its logo on the page: the first images marked as a logo, in the header if there is one
  const imgs = (html.match(/<img[^>]+>/gi) || []).filter((t) => /logo/i.test([attr(t, 'class'), attr(t, 'id'), attr(t, 'alt'), attr(t, 'src')].join(' ')));
  for (const t of imgs.filter((t) => !NOT_THEIRS.test(t)).slice(0, 3)) add(attr(t, 'src') || attr(t, 'data-src') || (attr(t, 'srcset') || '').split(/\s+/)[0], 'logo');
  for (const c of later) add(c, 'logo');
  // Its app icon (a square logo, at least 150 pixels)
  for (const l of html.match(/<link[^>]+rel=["'][^"']*(?:apple-touch-icon|icon)[^"']*["'][^>]*>/gi) || []) {
    const size = +(attr(l, 'sizes') || '').split('x')[0] || (/apple-touch-icon/i.test(l) ? 180 : 0);
    if (size >= 150) add(attr(l, 'href'), 'logo');
  }
  return out;
}

// ---- finding the website when the map doesn't give it

const GENERIC = new Set(['the', 'golf', 'course', 'courses', 'club', 'links', 'country', 'cc', 'gc', 'and', 'at', 'of', 'resort', 'lodge', 'social', 'clubs']);
const nameWords = (name) => String(name || '').toLowerCase().replace(/\(.*?\)/g, ' ').replace(/&/g, ' and ').replace(/[^a-z0-9 ]+/g, ' ').split(/\s+/).filter(Boolean);
const coreWords = (name) => nameWords(name).filter((w) => !GENERIC.has(w));
const STATES = { alabama: 'AL', alaska: 'AK', arizona: 'AZ', arkansas: 'AR', california: 'CA', colorado: 'CO', connecticut: 'CT', delaware: 'DE', florida: 'FL', georgia: 'GA', hawaii: 'HI', idaho: 'ID', illinois: 'IL', indiana: 'IN', iowa: 'IA', kansas: 'KS', kentucky: 'KY', louisiana: 'LA', maine: 'ME', maryland: 'MD', massachusetts: 'MA', michigan: 'MI', minnesota: 'MN', mississippi: 'MS', missouri: 'MO', montana: 'MT', nebraska: 'NE', nevada: 'NV', 'new hampshire': 'NH', 'new jersey': 'NJ', 'new mexico': 'NM', 'new york': 'NY', 'north carolina': 'NC', 'north dakota': 'ND', ohio: 'OH', oklahoma: 'OK', oregon: 'OR', pennsylvania: 'PA', 'rhode island': 'RI', 'south carolina': 'SC', 'south dakota': 'SD', tennessee: 'TN', texas: 'TX', utah: 'UT', vermont: 'VT', virginia: 'VA', washington: 'WA', 'west virginia': 'WV', wisconsin: 'WI', wyoming: 'WY', alberta: 'AB', 'british columbia': 'BC', ontario: 'ON', quebec: 'QC' };
const escRe = (s) => s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');

// Is this page the course's own? It names the course (every distinctive word of its name), golf,
// and where it is (a place in its location, its state or postcode)
function pageIsCourse(html, info) {
  const text = html.replace(/<script[\s\S]*?<\/script>|<style[\s\S]*?<\/style>/gi, ' ').replace(/<[^>]+>/g, ' ').replace(/&amp;/g, '&');
  const low = text.toLowerCase();
  const core = coreWords(info.name);
  if (!core.length || !core.every((w) => new RegExp(`\\b${escRe(w)}\\b`).test(low)) || !/golf/.test(low)) return false;
  // (a place that's also part of the course's name proves nothing: Miami Shores in Troy, Ohio is
  // not the one in Florida)
  const places = String(info.location || '').split(',').map((s) => s.trim())
    .filter((s) => s && !/^(united states|canada|usa)$/i.test(s) && !nameWords(s).some((w) => core.includes(w)));
  for (const p of places) {
    if (/^\d{5}$/.test(p) && text.includes(p)) return true;
    const st = STATES[p.toLowerCase()];
    if (st && (low.includes(p.toLowerCase()) || new RegExp(`[ ,]${st}[ ,.]+\\d{5}`).test(text))) return true;
    if (!st && p.length === 2 && new RegExp(`[ ,]${escRe(p)}[ ,.]+\\d{5}`).test(text)) return true;
    const place = p.toLowerCase().replace(/\b(county|township|city|village|of|near|improvement district|no\.?|\d+)\b/g, ' ').trim();
    if (place.length > 3 && low.includes(place)) return true;
  }
  return false;
}

// A map feature near the course with a website and the course's name (the clubhouse, often)
async function nearbySite(info) {
  const core = coreWords(info.name);
  if (!core.length || !Number.isFinite(info.lat)) return [];
  const q = `[out:json][timeout:20];nwr(around:1200,${info.lat},${info.lon})["name"][~"^(website|contact:website|url)$"~"."];out tags 60;`;
  const res = await fetch('https://overpass-api.de/api/interpreter', { method: 'POST', body: 'data=' + encodeURIComponent(q), headers: { 'User-Agent': UA }, signal: AbortSignal.timeout(25000) });
  if (!res.ok) return [];
  const els = (await res.json()).elements || [];
  return els.filter((e) => core.every((w) => nameWords(e.tags.name).includes(w)))
    .map((e) => e.tags.website || e.tags['contact:website'] || e.tags.url);
}

// The usual web addresses for a course's name, kept only if the page is the course's own
async function guessedSite(info, log) {
  const core = coreWords(info.name).join(''), all = nameWords(info.name).filter((w) => w !== 'the').join('');
  if (core.length < 4) return null;
  const names = [...new Set([`${core}golf.com`, `${all}.com`, `${core}golfcourse.com`, `${core}golfclub.com`, `golf${core}.com`, `${core}.com`, `${core}gc.com`, `${core}cc.com`, `play${core}.com`, `${core}golf.net`, `${core}golf.org`])];
  // (and it must still be the course's own address once any redirects are followed: not a
  // management company's or a domain seller's page that mentions it)
  const words = coreWords(info.name).filter((w) => w.length >= 4);
  for (const d of names) {
    try {
      const page = await fetchPage(`https://${d}`, 8000) || await fetchPage(`https://www.${d}`, 8000);
      const host = page && new URL(page.url).hostname;
      if (page && words.some((w) => host.includes(w)) && pageIsCourse(page.html, info)) { log(`website for ${info.name}: ${page.url}`); return page; }
    } catch { /* not there */ }
  }
  return null;
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
  const pages = [];
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
  if (!sites.length) await tryStep('nearby', async () => { sites.push(...await nearbySite(info)); return null; });
  for (const site of [...new Set(sites)]) await tryStep('website', async () => { const pg = await fetchPage(site); if (pg) pages.push(pg); return null; });
  if (!pages.length) await tryStep('guess', async () => { const pg = await guessedSite(info, log); if (pg) pages.push(pg); return null; });
  for (const pg of pages) out.push(...websiteImages(pg));
  return out;
}

async function download(c) {
  const res = await get(c.url, 20000);
  const type = (res.headers.get('content-type') || '').split(';')[0].trim();
  if (!EXT[type]) throw new Error(`not an image (${type})`);
  const buf = Buffer.from(await res.arrayBuffer());
  if (buf.length > MAX_BYTES || buf.length < (c.kind === 'logo' ? 800 : 4000)) throw new Error('image too large or too small');
  return { buf, ext: EXT[type] };
}

// ---- cache in the course folder: photo.<ext> + photo.json

// (nothing found, but by an older lookup or long enough ago to look again)
export const photoStale = (meta) => !meta.file && meta.source !== 'user' && (meta.v !== LOOKUP_VERSION || Date.now() - Date.parse(meta.tried || 0) >= RETRY_DAYS * 864e5);
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

// info: { name, location, lat, lon, osmType, osmId }; force: look again even if nothing was found before
export function ensurePhoto(dir, info, { force = false, log = () => {} } = {}) {
  const meta = photoMeta(dir);
  if (!force && meta) {
    if (meta.file && fs.existsSync(path.join(dir, meta.file))) return Promise.resolve(meta);
    if (meta.source === 'user' || (meta.v === LOOKUP_VERSION && Date.now() - Date.parse(meta.tried || 0) < RETRY_DAYS * 864e5)) return Promise.resolve(meta);
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
            return savePhoto(dir, buf, ext, { source: c.source, kind: c.kind || 'photo', credit: c.credit, page: c.page, url: c.url });
          } catch (err) { log(`${c.url}: ${err.message}`); }
        }
        const none = { file: null, tried: new Date().toISOString(), v: LOOKUP_VERSION };
        fs.writeFileSync(path.join(dir, 'photo.json'), JSON.stringify(none));
        return none;
      },
    });
    runNext();
  }).finally(() => pending.delete(dir));
  pending.set(dir, job);
  return job;
}
