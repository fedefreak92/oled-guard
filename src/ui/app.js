// OLED Guard - interfaccia. Riceve messaggi JSON dall'app ("type") e invia comandi ("cmd").
// Senza WebView2 (aperta in un browser) gira in modalita' anteprima con dati finti.
'use strict';

const $ = (id) => document.getElementById(id);
const webview = window.chrome && window.chrome.webview;
const preview = !webview;

const model = {
  state: null,
  stats: null,
  config: null,
  apps: [],
  windows: null,
  nextMoveAt: 0,
};

// ------------------------------------------------------------------ comunicazione

function send(cmd) {
  if (webview) webview.postMessage(JSON.stringify(cmd));
  else Mock.command(cmd);
}

function receive(msg) {
  switch (msg.type) {
    case 'state':
      model.state = msg;
      model.nextMoveAt = performance.now() + msg.next_move_ms;
      renderState();
      break;
    case 'stats': model.stats = msg; renderStats(); renderRadar(); break;
    case 'config': model.config = msg.config; renderConfig(); break;
    case 'apps': model.apps = msg.apps; renderApps(); break;
    case 'windows': model.windows = msg; renderWindows(); break;
    case 'processes': renderProcesses(msg.processes); break;
    case 'notice': toast(msg.text); break;
  }
}

if (webview) webview.addEventListener('message', (e) => receive(e.data));

// ------------------------------------------------------------------ utilita'

function fmtDuration(ms, withSeconds) {
  const s = Math.floor(ms / 1000);
  const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), sec = s % 60;
  const pad = (n) => String(n).padStart(2, '0');
  return withSeconds ? `${pad(h)}:${pad(m)}:${pad(sec)}` : `${h} h ${pad(m)} min`;
}

function fmtWhen(unixS) {
  if (!unixS) return '-';
  const d = new Date(unixS * 1000), now = new Date();
  const hm = d.toLocaleTimeString('it-IT', { hour: '2-digit', minute: '2-digit' });
  const sameDay = d.toDateString() === now.toDateString();
  const yesterday = new Date(now - 86400000).toDateString() === d.toDateString();
  if (sameDay) return (now - d < 120000) ? 'adesso' : `oggi ${hm}`;
  if (yesterday) return `ieri ${hm}`;
  return d.toLocaleDateString('it-IT', { weekday: 'short', day: 'numeric', month: 'short' }) + ` ${hm}`;
}

let toastTimer = 0;
function toast(text) {
  const t = $('toast');
  t.textContent = text;
  t.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { t.hidden = true; }, 4000);
}

function setConfig(patch) { send({ cmd: 'setConfig', patch }); }

function debounce(fn, ms) {
  let t = 0;
  return (...a) => { clearTimeout(t); t = setTimeout(() => fn(...a), ms); };
}

// ------------------------------------------------------------------ schede e finestra

document.querySelectorAll('#tabs button').forEach((b) => b.addEventListener('click', () => {
  document.querySelectorAll('#tabs button').forEach((x) => x.classList.toggle('on', x === b));
  document.querySelectorAll('.tab').forEach((t) => t.classList.toggle('on', t.id === 'tab-' + b.dataset.tab));
}));

document.querySelectorAll('[data-win]').forEach((b) => b.addEventListener('click', () => send({ cmd: 'window', action: b.dataset.win })));
document.querySelector('.cap').addEventListener('dblclick', (e) => { if (!e.target.closest('button')) send({ cmd: 'window', action: 'maximize' }); });

// Attenuazione dopo 60 s senza interazione.
let idleTimer = 0;
function wake() {
  document.body.classList.remove('dim');
  clearTimeout(idleTimer);
  idleTimer = setTimeout(() => document.body.classList.add('dim'), 60000);
}
['mousemove', 'mousedown', 'keydown', 'wheel'].forEach((ev) => window.addEventListener(ev, wake, { passive: true }));
wake();

// ------------------------------------------------------------------ stato

function renderState() {
  const s = model.state;
  if (!s) return;
  const pill = $('statusPill');
  if (s.testing_shift) { pill.textContent = 'PROVA IN CORSO'; pill.className = 'st test'; }
  else if (s.paused) { pill.textContent = 'IN PAUSA · ' + s.reason.toUpperCase(); pill.className = 'st paused'; }
  else { pill.textContent = 'ATTIVO'; pill.className = 'st'; }
  const mon = s.monitor ? ` · ${s.monitor.toUpperCase()} · ${s.width}x${s.height} ${s.hdr ? 'HDR' : 'SDR'}` : '';
  $('capTitle').textContent = 'OLED GUARD' + mon;
  $('vOffset').textContent = s.paused ? '-' : `${s.offset[0] >= 0 ? '+' : ''}${s.offset[0]}, ${s.offset[1] >= 0 ? '+' : ''}${s.offset[1]} px`;
  $('vForeground').textContent = s.paused ? `${s.foreground || '-'} (${s.reason.toLowerCase()})` : (s.foreground || '-');
  $('vLatency').textContent = s.latency_ms ? `${s.latency_ms.toFixed(1)} ms` : '-';
  const btn = $('btnPause');
  btn.classList.toggle('on', s.manual_pause);
  btn.textContent = s.manual_pause ? 'Riprendi' : 'Pausa manuale';
  $('countdownLabel').textContent = s.paused ? 'IN PAUSA' : (s.testing_shift ? 'PROVA · PROSSIMA MOSSA' : 'PROSSIMA MOSSA');
  renderRadar();
}

function tickCountdown() {
  const s = model.state;
  let text = '--:--';
  if (s && !s.paused) {
    const left = Math.max(0, model.nextMoveAt - performance.now());
    const sec = Math.ceil(left / 1000);
    text = `${String(Math.floor(sec / 60)).padStart(2, '0')}:${String(sec % 60).padStart(2, '0')}`;
  }
  $('countdown').textContent = text;
}
setInterval(tickCountdown, 250);

function renderStats() {
  const st = model.stats;
  if (!st) return;
  $('vToday').textContent = fmtDuration(st.today_ms, true);
  $('vTotal').textContent = fmtDuration(st.total_ms, false);
  $('vUniform').textContent = st.uniformity === null ? 'n/d (servono 10 min)' : `${st.uniformity.toFixed(1)} %`;
  $('vPauses').textContent = String(st.pauses_today);
}

// Radar: griglia (2a+1)x(2a+1) con mappa di calore del tempo totale e scia delle mosse.
function renderRadar() {
  const svg = $('radar');
  const s = model.state, st = model.stats;
  const a = (model.config && model.config.amplitude_px) || (s && s.amplitude) || 2;
  const c = 150, step = a === 1 ? 90 : (a === 2 ? 58 : 42);
  const pos = (x, y) => [c + x * step, c + y * step];
  const cell = (x, y) => (st ? st.cells[(y + 3) * 7 + (x + 3)] : 0);
  let max = 0;
  for (let y = -a; y <= a; y++) for (let x = -a; x <= a; x++) max = Math.max(max, cell(x, y));

  let out = '';
  out += `<circle cx="${c}" cy="${c}" r="142" fill="none" stroke="#333"/>`;
  out += `<circle cx="${c}" cy="${c}" r="95" fill="none" stroke="#151515"/>`;
  out += `<circle cx="${c}" cy="${c}" r="48" fill="none" stroke="#151515"/>`;
  out += `<line x1="${c}" y1="8" x2="${c}" y2="292" stroke="#1f1f1f"/><line x1="8" y1="${c}" x2="292" y2="${c}" stroke="#1f1f1f"/>`;
  const size = Math.min(16, step * 0.32);
  for (let y = -a; y <= a; y++) {
    for (let x = -a; x <= a; x++) {
      const [px, py] = pos(x, y);
      const heat = max > 0 ? cell(x, y) / max : 0;
      const op = (0.12 + heat * 0.6).toFixed(2);
      out += `<rect x="${px - size / 2}" y="${py - size / 2}" width="${size}" height="${size}" fill="#ffb020" opacity="${op}"/>`;
    }
  }
  if (s && !s.paused && s.trail && s.trail.length > 1) {
    for (let i = 0; i < s.trail.length - 1; i++) {
      const [x1, y1] = pos(...s.trail[i]), [x2, y2] = pos(...s.trail[i + 1]);
      const op = (0.25 + 0.6 * (i + 1) / s.trail.length).toFixed(2);
      out += `<line x1="${x1}" y1="${y1}" x2="${x2}" y2="${y2}" stroke="#ffb020" stroke-width="1.5" opacity="${op}"/>`;
    }
  }
  if (s && !s.paused) {
    const [nx, ny] = pos(s.offset[0], s.offset[1]);
    const big = size + 6;
    out += `<rect x="${nx - big / 2}" y="${ny - big / 2}" width="${big}" height="${big}" fill="#ffb020" filter="url(#glow)"/>`;
  }
  svg.innerHTML = `<defs><filter id="glow" x="-1" y="-1" width="3" height="3"><feGaussianBlur stdDeviation="4" result="b"/><feMerge><feMergeNode in="b"/><feMergeNode in="SourceGraphic"/></feMerge></filter></defs>` + out;
}

$('btnPause').addEventListener('click', () => send({ cmd: 'setManualPause', on: !(model.state && model.state.manual_pause) }));
$('btnTestFromStato').addEventListener('click', () => send({ cmd: 'testShift' }));

// ------------------------------------------------------------------ spostamento

// Sfasamento medio atteso dei clic con posizioni uniformi sulla griglia.
function meanClickOffset(a) {
  let sum = 0, n = 0;
  for (let y = -a; y <= a; y++) for (let x = -a; x <= a; x++) { sum += Math.hypot(x, y); n++; }
  return sum / n;
}

function renderConfig() {
  const c = model.config;
  if (!c) return;
  document.querySelectorAll('#ampSeg button').forEach((b) => b.classList.toggle('on', Number(b.dataset.amp) === c.amplitude_px));
  const mini = $('ampMini');
  const side = 2 * c.amplitude_px + 1;
  mini.style.gridTemplateColumns = `repeat(${side}, 12px)`;
  mini.innerHTML = Array.from({ length: side * side }, (_, i) => `<i${i === (side * side - 1) / 2 ? ' class="now"' : ''}></i>`).join('');
  $('vClick').textContent = `~${meanClickOffset(c.amplitude_px).toFixed(1)} px`;
  setDual('dualDwell', c.dwell_min_s, c.dwell_max_s);
  setDual('dualTrans', c.transition_min_s, c.transition_max_s);
  $('vDwell').textContent = `${c.dwell_min_s}-${c.dwell_max_s} s`;
  $('vTrans').textContent = `${c.transition_min_s}-${c.transition_max_s} s`;
  $('rangeResume').value = c.resume_delay_ms;
  $('vResume').textContent = `${(c.resume_delay_ms / 1000).toFixed(2).replace(/\.?0+$/, '')} s`;
  document.querySelectorAll('[data-hotkey]').forEach((b) => { if (!b.classList.contains('rec')) b.textContent = c[b.dataset.hotkey].replace(/\+/g, ' + '); });
  document.querySelectorAll('[data-toggle]').forEach((b) => { const on = !!c[b.dataset.toggle]; b.classList.toggle('on', on); b.textContent = on ? 'ON' : 'OFF'; });
  renderRadar();
}

document.querySelectorAll('#ampSeg button').forEach((b) => b.addEventListener('click', () => setConfig({ amplitude_px: Number(b.dataset.amp) })));
document.querySelectorAll('[data-toggle]').forEach((b) => b.addEventListener('click', () => {
  if (!model.config) return;
  setConfig({ [b.dataset.toggle]: !model.config[b.dataset.toggle] });
}));

// Slider a doppia maniglia.
function setDual(id, lo, hi) {
  const box = $(id), [a, b] = box.querySelectorAll('input');
  if (document.activeElement === a || document.activeElement === b) return;
  a.value = lo; b.value = hi;
  paintDual(box);
}
function paintDual(box) {
  const [a, b] = box.querySelectorAll('input');
  let fill = box.querySelector('.fill');
  if (!fill) { fill = document.createElement('div'); fill.className = 'fill'; box.appendChild(fill); }
  const min = Number(a.min), max = Number(a.max);
  const lo = Math.min(a.value, b.value), hi = Math.max(a.value, b.value);
  fill.style.left = `${((lo - min) / (max - min)) * 100}%`;
  fill.style.width = `${((hi - lo) / (max - min)) * 100}%`;
}
function wireDual(id, keyMin, keyMax, label, unit) {
  const box = $(id), inputs = box.querySelectorAll('input');
  const push = debounce((lo, hi) => setConfig({ [keyMin]: lo, [keyMax]: hi }), 400);
  inputs.forEach((inp) => inp.addEventListener('input', () => {
    const lo = Math.min(Number(inputs[0].value), Number(inputs[1].value));
    const hi = Math.max(Number(inputs[0].value), Number(inputs[1].value));
    paintDual(box);
    $(label).textContent = `${lo}-${hi} ${unit}`;
    push(lo, hi);
  }));
}
wireDual('dualDwell', 'dwell_min_s', 'dwell_max_s', 'vDwell', 's');
wireDual('dualTrans', 'transition_min_s', 'transition_max_s', 'vTrans', 's');

const pushResume = debounce((v) => setConfig({ resume_delay_ms: v }), 400);
$('rangeResume').addEventListener('input', (e) => {
  const v = Number(e.target.value);
  $('vResume').textContent = `${(v / 1000).toFixed(2).replace(/\.?0+$/, '')} s`;
  pushResume(v);
});

$('btnTest').addEventListener('click', () => send({ cmd: 'testShift' }));

// Registrazione delle hotkey: si preme la combinazione, Esc annulla.
let capturing = null;
const KEY_NAMES = { ' ': 'Space', Pause: 'Pause', Insert: 'Insert', Delete: 'Delete', Home: 'Home', End: 'End', PageUp: 'PageUp', PageDown: 'PageDown' };
function keyName(e) {
  if (/^F([1-9]|1[0-9]|2[0-4])$/.test(e.key)) return e.key;
  if (KEY_NAMES[e.key]) return KEY_NAMES[e.key];
  if (/^Key[A-Z]$/.test(e.code)) return e.code.slice(3);
  if (/^Digit[0-9]$/.test(e.code)) return e.code.slice(5);
  return null;
}
function stopCapture(restore) {
  if (!capturing) return;
  capturing.classList.remove('rec');
  if (restore && model.config) capturing.textContent = model.config[capturing.dataset.hotkey].replace(/\+/g, ' + ');
  capturing = null;
  send({ cmd: 'hotkeyCapture', on: false });
}
document.querySelectorAll('[data-hotkey]').forEach((b) => b.addEventListener('click', () => {
  stopCapture(true);
  capturing = b;
  b.classList.add('rec');
  b.textContent = 'premi la combinazione...';
  send({ cmd: 'hotkeyCapture', on: true });
}));
window.addEventListener('keydown', (e) => {
  if (!capturing) return;
  e.preventDefault();
  if (e.key === 'Escape') { stopCapture(true); return; }
  const name = keyName(e);
  if (!name) return;  // solo modificatori finora
  const mods = [];
  if (e.ctrlKey) mods.push('Ctrl');
  if (e.altKey) mods.push('Alt');
  if (e.shiftKey) mods.push('Shift');
  if (e.metaKey) mods.push('Win');
  if (!mods.length) { $('hotkeyHint').textContent = 'Serve almeno un tasto fra Ctrl, Alt, Shift e Win.'; return; }
  const combo = [...mods, name].join('+');
  const key = capturing.dataset.hotkey;
  capturing.textContent = combo.replace(/\+/g, ' + ');
  stopCapture(false);
  $('hotkeyHint').textContent = 'Clicca un tasto rapido e premi la nuova combinazione (Esc per annullare).';
  setConfig({ [key]: combo });
});

// ------------------------------------------------------------------ giochi e app

const RULES = [['auto', 'AUTO'], ['always', 'SEMPRE PAUSA'], ['never', 'MAI PAUSA']];
function renderApps() {
  const body = $('appsBody');
  body.innerHTML = '';
  $('appsEmpty').hidden = model.apps.length > 0;
  for (const app of model.apps) {
    const tr = document.createElement('tr');
    const cells = [app.exe, app.reason || 'regola manuale', fmtWhen(app.last_seen_s), app.count || '-'];
    cells.forEach((text, i) => { const td = document.createElement('td'); td.textContent = String(text); if (i === 0) td.className = 'exe'; tr.appendChild(td); });
    const td = document.createElement('td');
    td.className = 'rules';
    for (const [rule, label] of RULES) {
      const b = document.createElement('button');
      b.className = 'tag' + (app.rule === rule ? ' on' : '');
      b.textContent = label;
      b.addEventListener('click', () => send({ cmd: 'setAppRule', exe: app.exe, rule }));
      td.appendChild(b);
    }
    tr.appendChild(td);
    body.appendChild(tr);
  }
}

function addTypedApp() {
  const exe = $('addExe').value.trim();
  if (!exe) return;
  send({ cmd: 'addApp', exe: /\.exe$/i.test(exe) ? exe : exe + '.exe' });
  $('addExe').value = '';
}
$('btnAdd').addEventListener('click', addTypedApp);
$('addExe').addEventListener('keydown', (e) => { if (e.key === 'Enter') addTypedApp(); });
$('btnProcesses').addEventListener('click', () => {
  const list = $('procList');
  if (!list.hidden) { list.hidden = true; return; }
  send({ cmd: 'listProcesses' });
});
function renderProcesses(processes) {
  const list = $('procList');
  list.innerHTML = '';
  for (const p of processes) {
    const b = document.createElement('button');
    b.textContent = p;
    b.addEventListener('click', () => { $('addExe').value = p; list.hidden = true; $('addExe').focus(); });
    list.appendChild(b);
  }
  list.hidden = processes.length === 0;
}

// ------------------------------------------------------------------ windows per oled

function renderWindows() {
  const w = model.windows;
  if (!w) return;
  $('winScore').textContent = `${w.score}/${w.total}`;
  $('btnRestoreAll').disabled = !w.items.some((i) => i.restorable);
  $('btnApplyAll').disabled = w.items.every((i) => i.ok || i.managed);
  const list = $('winList');
  list.innerHTML = '';
  for (const it of w.items) {
    const row = document.createElement('div');
    row.className = 'chk';
    const mark = document.createElement('span');
    mark.className = 'mark ' + (it.ok ? 'ok' : 'no');
    mark.textContent = it.ok ? '■' : '□';
    const label = document.createElement('span');
    label.textContent = it.label;
    const cur = document.createElement('span');
    cur.className = 'cur';
    cur.textContent = it.current;
    const act = document.createElement('span');
    act.className = 'act';
    if (it.managed) {
      act.innerHTML = '<span class="managed">gestito dal sistema</span>';
    } else if (!it.ok) {
      const b = document.createElement('button');
      b.className = 'btn';
      b.textContent = it.recommended;
      b.title = 'Applica il valore consigliato';
      b.addEventListener('click', () => send({ cmd: 'windowsApply', id: it.id }));
      act.appendChild(b);
    }
    row.append(mark, label, cur, act);
    list.appendChild(row);
  }
}
$('btnApplyAll').addEventListener('click', () => send({ cmd: 'windowsApplyAll' }));
$('btnRestoreAll').addEventListener('click', () => send({ cmd: 'windowsRestoreAll' }));
$('btnDisplayOff').addEventListener('click', () => send({ cmd: 'displayOff' }));

// ------------------------------------------------------------------ anteprima (browser)

const Mock = {
  config: {
    monitor: 'auto', amplitude_px: 2, dwell_min_s: 20, dwell_max_s: 60, transition_min_s: 2, transition_max_s: 5,
    pause_on_notification_state: true, pause_on_fullscreen_window: true, pause_enter_delay_ms: 300, resume_delay_ms: 2500,
    denylist: ['netflix.exe'], allowlist: ['vlc.exe'], hotkey_toggle: 'Ctrl+Alt+Shift+P', hotkey_emergency_exit: 'Ctrl+Alt+Shift+F12',
    hotkey_display_off: 'Ctrl+Alt+Shift+O', allow_tearing: false, autostart: true, log_level: 'info',
  },
  offset: [0, 0], trail: [[0, 0]], paused: false, manual: false, testing: false, next: 6000,
  cells: Array.from({ length: 49 }, (_, i) => { const x = i % 7 - 3, y = Math.floor(i / 7) - 3; return Math.abs(x) <= 2 && Math.abs(y) <= 2 ? 3e6 + Math.random() * 1.2e6 : 0; }),
  windows: [
    ['dark_theme', 'Tema scuro di sistema e app', 'scuro', 'Scuro', true],
    ['taskbar_autohide', 'Barra delle applicazioni a scomparsa', 'sempre visibile', 'Applica', false],
    ['display_timeout', 'Spegni lo schermo dopo', 'mai', '5 min', false],
    ['screensaver', 'Salvaschermo nero', 'dopo 10 min', '10 min', true],
    ['desktop_icons', 'Icone del desktop nascoste', 'visibili', 'Nascondi', false],
    ['transparency', 'Effetti di trasparenza disattivati', 'disattivati', 'Disattiva', true],
    ['black_wallpaper', 'Sfondo nero', 'tinta unita nera', 'Nero', true],
    ['mouse_vanish', 'Nascondi il puntatore mentre scrivi', 'attivo', 'Attiva', true],
    ['accent_titlebar', 'Colore d\'accento sulle barre del titolo', 'spento', 'Spegni', true],
  ],
  apps: [
    { exe: 'cyberpunk2077.exe', reason: 'Gioco o app a schermo intero', last_seen_s: Date.now() / 1000 - 30, count: 12, rule: 'auto' },
    { exe: 'chrome.exe', reason: 'Finestra a tutto schermo', last_seen_s: Date.now() / 1000 - 86400 - 3000, count: 4, rule: 'auto' },
    { exe: 'vlc.exe', reason: 'Finestra a tutto schermo', last_seen_s: Date.now() / 1000 - 90000, count: 2, rule: 'never' },
    { exe: 'netflix.exe', reason: 'Pausa manuale', last_seen_s: Date.now() / 1000 - 400000, count: 1, rule: 'always' },
  ],
  start() {
    receive({ type: 'config', config: this.config });
    receive({ type: 'apps', apps: this.apps });
    this.sendWindows();
    this.sendStats();
    setInterval(() => this.step(), 250);
    setInterval(() => this.sendStats(), 5000);
  },
  step() {
    this.next -= 250;
    if (this.next <= 0) {
      const a = this.config.amplitude_px;
      let nx, ny;
      do { nx = Math.round(Math.random() * 2 * a) - a; ny = Math.round(Math.random() * 2 * a) - a; } while (nx === this.offset[0] && ny === this.offset[1]);
      this.offset = [Math.sign(nx - this.offset[0]) + this.offset[0], Math.sign(ny - this.offset[1]) + this.offset[1]];
      this.trail = [...this.trail, this.offset].slice(-4);
      this.next = this.testing ? 1500 : 4000 + Math.random() * 4000;
    }
    if (!this.paused) this.cells[(this.offset[1] + 3) * 7 + (this.offset[0] + 3)] += 250;
    receive({ type: 'state', paused: this.paused, manual_pause: this.manual, testing_shift: this.testing,
      reason: this.manual ? 'Pausa manuale' : 'Attivo', foreground: 'chrome.exe', offset: this.offset, trail: this.trail,
      next_move_ms: this.next, amplitude: this.config.amplitude_px, monitor: 'LG TV SSCR2', width: 3840, height: 2160, hdr: true, latency_ms: 6.9 });
  },
  sendStats() {
    receive({ type: 'stats', cells: this.cells, uniformity: 94.2, today_ms: 22360000, total_ms: 1.4e9, pauses_today: 3 });
  },
  sendWindows() {
    const items = this.windows.map(([id, label, current, recommended, ok]) => ({ id, label, current, recommended, ok, restorable: false, managed: false }));
    receive({ type: 'windows', items, score: items.filter((i) => i.ok).length, total: items.length });
  },
  command(c) {
    console.log('[anteprima] comando', c);
    if (c.cmd === 'setConfig') { Object.assign(this.config, c.patch); receive({ type: 'config', config: this.config }); }
    if (c.cmd === 'setManualPause') { this.manual = c.on; this.paused = c.on; }
    if (c.cmd === 'testShift') { this.testing = true; this.next = 500; setTimeout(() => { this.testing = false; }, 30000); }
    if (c.cmd === 'setAppRule' || c.cmd === 'addApp') {
      const rule = c.rule || 'always';
      const found = this.apps.find((a) => a.exe === c.exe.toLowerCase());
      if (found) found.rule = rule; else this.apps.push({ exe: c.exe.toLowerCase(), reason: '', last_seen_s: 0, count: 0, rule });
      receive({ type: 'apps', apps: this.apps });
    }
    if (c.cmd === 'listProcesses') receive({ type: 'processes', processes: ['chrome.exe', 'code.exe', 'discord.exe', 'spotify.exe', 'steam.exe'] });
    if (c.cmd === 'windowsApply') { const w = this.windows.find((x) => x[0] === c.id); if (w) { w[4] = true; w[2] = w[3].toLowerCase(); } this.sendWindows(); }
    if (c.cmd === 'windowsApplyAll') { this.windows.forEach((w) => { w[4] = true; }); this.sendWindows(); }
    if (c.cmd === 'displayOff') toast('Anteprima: qui lo schermo si spegnerebbe.');
    if (c.cmd === 'window') toast(`Anteprima: comando finestra "${c.action}".`);
  },
};

if (preview) Mock.start();
else send({ cmd: 'ready' });
