// Mevo+ setup page: live camera view for lining up, level (tilt/roll), battery and device status,
// and the setup sent to the radar (distance to ball, surface and tee height, ball type, mode).
const $ = (id) => document.getElementById(id);
const FT = 0.3048;
let info = {}, state = 'off', camOn = false;

const STATE = {
  off: 'Disconnected', connecting: 'Looking for it…', searching: 'Not found', handshake: 'Connecting…', arming: 'Arming…',
  ready: 'Ready (armed)', shot: 'Reading shot…', idle: 'Disarmed',
};
let direct = false, busy = false;
const MODE = { full: 'Full swing', chipping: 'Chipping', putting: 'Putting', outdoor: 'Outdoor' };

// Level: green within 1°, amber to 2.5°, red beyond
const levelClass = (v) => (Math.abs(v) <= 1 ? 'good' : Math.abs(v) <= 2.5 ? 'warn' : 'bad');

function render() {
  const ready = state === 'ready' || state === 'shot';
  $('conn').querySelector('.dot').className = 'dot' + (ready ? ' ready' : state !== 'off' && state !== 'searching' ? ' on' : '');
  $('conn-text').textContent = ready ? `${info.model || 'Mevo+'} ${info.ssid || ''} connected` : STATE[state] || state;
  $('state').textContent = STATE[state] || state;
  // Arm / disarm while connected; disconnect when done (or connect again)
  $('arm-btn').classList.toggle('hidden', !direct || !['ready', 'idle'].includes(state));
  $('arm-btn').textContent = state === 'idle' ? 'Arm' : 'Disarm';
  $('power-btn').classList.toggle('hidden', !direct);
  $('power-btn').textContent = state === 'off' ? 'Connect' : 'Disconnect';
  for (const id of ['arm-btn', 'power-btn']) $(id).disabled = busy;
  $('conn-error').textContent = state === 'searching' && info.error ? `Last try: ${info.error}. Trying again… (details in data\\mevo-log.txt)` : '';
  $('mode').textContent = MODE[info.mode] || '–';

  // Battery
  if (info.battery != null) {
    $('battery').textContent = `${info.battery}%`;
    $('battery').className = info.battery > 40 ? 'good' : info.battery > 15 ? 'warn' : 'bad';
    $('battery-sub').textContent = info.charging ? 'Battery · charging' : `Battery${info.batteryV ? ` · ${info.batteryV.toFixed(2)} V` : ''}`;
  }
  if (info.tempC != null || info.dspTempC != null) {
    const c = info.tempC ?? info.dspTempC;
    $('temp').textContent = `${Math.round(c * 9 / 5 + 32)}°F`;
    $('temp').title = `${c.toFixed(1)} °C`;
  }
  $('shots').textContent = info.shots ?? 0;

  // Level
  if (info.tilt != null && info.roll != null) {
    $('tilt').textContent = `${info.tilt > 0 ? '+' : ''}${info.tilt.toFixed(1)}°`;
    $('roll').textContent = `${info.roll > 0 ? '+' : ''}${info.roll.toFixed(1)}°`;
    $('tilt').className = levelClass(info.tilt);
    $('roll').className = levelClass(info.roll);
    // 5° from level reaches the edge of the bubble
    const k = 50 / 5, x = Math.max(-50, Math.min(50, info.roll * k)), y = Math.max(-50, Math.min(50, -info.tilt * k));
    const b = $('bubble');
    b.style.left = `${50 + x * 0.82}%`;
    b.style.top = `${50 + y * 0.82}%`;
    const level = Math.abs(info.tilt) <= 1 && Math.abs(info.roll) <= 1;
    b.classList.toggle('off', !level);
    $('level-hint').textContent = level ? 'Level. Good.'
      : `Adjust the Mevo+ until both read close to 0° (within 1°).${Math.abs(info.roll) > 1 ? ` It leans to the ${info.roll > 0 ? 'right' : 'left'}.` : ''}${Math.abs(info.tilt) > 1 ? ` It points ${info.tilt > 0 ? 'up' : 'down'}.` : ''}`;
  }

  // Last shot
  const s = info.lastShot;
  if (s) {
    const ago = Math.round((Date.now() - s.at) / 1000);
    $('last-shot').innerHTML = `Last shot (${ago < 60 ? `${ago} s` : `${Math.round(ago / 60)} min`} ago): <b>${s.Speed.toFixed(1)} mph</b> ball · `
      + `launch ${s.VLA.toFixed(1)}° · direction ${s.HLA > 0 ? 'R' : 'L'} ${Math.abs(s.HLA).toFixed(1)}° · `
      + `spin ${Math.round(s.TotalSpin)} rpm, axis ${s.SpinAxis.toFixed(1)}°`
      + (s.ClubData ? ` · club ${s.ClubData.Speed.toFixed(1)} mph, smash ${(s.Speed / s.ClubData.Speed).toFixed(2)}` : '')
      + (s.CarryDistance ? ` · carry ${Math.round(s.CarryDistance)} yd (Mevo+)` : '');
  }
  const fw = info.firmware;
  if (info.lastText) {
    const ago = Math.round((Date.now() - info.lastTextAt) / 1000);
    $('last-text').textContent = `"${info.lastText}" (${ago < 60 ? `${ago} s` : `${Math.round(ago / 60)} min`} ago)`;
  }
  $('fw').textContent = [
    info.model && `Model: ${info.model}`, info.ssid && `Network: ${info.ssid}`,
    fw?.dsp && `DSP: ${fw.dsp}`, fw?.avr && `AVR: ${fw.avr}`, fw?.camera && `Camera: ${fw.camera}`,
  ].filter(Boolean).join('\n') || 'Not connected.';
  $('cam-btn').disabled = !ready;
  if (!ready && camOn) stopVideo('The Mevo+ is not connected.');
}

// ---- live camera view (relayed by the server from the Mevo+)

function startVideo() {
  const img = $('video');
  $('video-msg').textContent = 'Connecting to the camera…';
  img.onload = () => { $('video-msg').textContent = ''; };
  img.onerror = () => { $('video-msg').textContent = 'No picture from the camera yet. It can take up to 30 s after starting; try again shortly.'; img.removeAttribute('src'); };
  img.src = `/api/mevo/video?t=${Date.now()}`;
}
function stopVideo(msg = 'Live view is off.') {
  camOn = false;
  $('video').removeAttribute('src');
  $('video-msg').textContent = msg;
  $('cam-btn').textContent = 'Start live view';
}
$('cam-btn').onclick = async () => {
  const btn = $('cam-btn');
  btn.disabled = true;
  if (camOn) {
    stopVideo();
    await fetch('/api/mevo/camera', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ on: false }) }).catch(() => {});
    btn.disabled = false;
    return;
  }
  $('video-msg').textContent = 'Starting the camera (10–30 s)…';
  const r = await fetch('/api/mevo/camera', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ on: true }) })
    .then((x) => x.json()).catch((err) => ({ error: err.message }));
  btn.disabled = false;
  if (r.error) { $('video-msg').textContent = `Camera: ${r.error}`; return; }
  camOn = true;
  btn.textContent = 'Stop live view';
  startVideo();
};
$('guides').onchange = () => { $('guide').style.display = $('guides').checked ? '' : 'none'; };

// ---- setup form

function fillSetup(st) {
  $('s-dist').value = (st.distanceM / FT).toFixed(1);
  $('s-surface').value = st.surfaceIn;
  $('s-tee').value = st.teeHeightIn;
  $('s-ball').value = String(st.ballType);
  $('s-track').value = st.trackPct;
  $('s-mode').value = st.mode;
  $('s-turn').checked = st.armOnTurn !== false;
}
$('setup').onsubmit = async (e) => {
  e.preventDefault();
  const body = {
    distanceM: +$('s-dist').value * FT, surfaceIn: +$('s-surface').value, teeHeightIn: +$('s-tee').value,
    ballType: +$('s-ball').value, trackPct: +$('s-track').value, mode: $('s-mode').value, armOnTurn: $('s-turn').checked,
  };
  $('save').disabled = true;
  $('save-msg').textContent = state === 'ready' ? 'Sending to the Mevo+ and re-arming…' : 'Saving…';
  const r = await fetch('/api/mevo/setup', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) })
    .then((x) => x.json()).catch((err) => ({ error: err.message }));
  $('save').disabled = false;
  if (r.error) { $('save-msg').textContent = `Couldn't apply: ${r.error}`; return; }
  fillSetup(r.setup);
  $('save-msg').textContent = state === 'ready' || state === 'arming' ? 'Saved and applied.' : 'Saved; it will be applied when the Mevo+ connects.';
};

$('enable-direct').onclick = async () => {
  await fetch('/api/lm', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ connection: 'direct' }) });
  load();
};

async function power(body) {
  busy = true; render();
  const r = await fetch('/api/mevo/power', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) })
    .then((x) => x.json()).catch((err) => ({ error: err.message }));
  busy = false;
  if (r.error) $('save-msg').textContent = r.error;
  load();
}
$('arm-btn').onclick = () => power({ armed: state === 'idle' });
$('power-btn').onclick = () => power({ connect: state === 'off' });

async function load() {
  const r = await fetch('/api/mevo').then((x) => x.json());
  $('not-direct').classList.toggle('hidden', r.connection === 'direct');
  direct = r.connection === 'direct';
  state = r.state;
  info = { ...r.info, mode: r.mode };
  fillSetup(r.setup);
  render();
}

// Live updates (the server forwards the Mevo+ status about once a second)
const es = new EventSource('/api/events');
es.addEventListener('mevo', (e) => {
  const s = JSON.parse(e.data);
  state = s.state;
  info = { ...info, ...s, error: s.error || null };
  render();
});
setInterval(render, 5000); // keeps "last shot … ago" fresh
load();
