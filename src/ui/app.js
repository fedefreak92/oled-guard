// OLED Guard - interfaccia: comunicazione con l'app, stato, componenti e guscio.
// Riceve messaggi JSON dall'app ("type") e invia comandi ("cmd"). Senza WebView2
// (pagina aperta in un browser) gira in modalita' anteprima con dati finti (mock.js).
'use strict';

const $ = (id) => document.getElementById(id);
const webview = window.chrome && window.chrome.webview;
const preview = !webview;

// ------------------------------------------------------------------ stato e messaggi

// Ultimo messaggio di ogni tipo; "activity" tiene la lista intera.
const store = {
  state: null, stats: null, config: null, apps: null, windows: null, dim: null,
  monitors: null, activity: [], about: null, history: null,
  signals: [], signalCaptionKey: '', signalsVersion: 0,
  lang: 'en', strings: {},
  nextMoveAt: 0, highlightUntil: 0,
};
const listeners = {};
// Restituisce la funzione che toglie l'ascoltatore (da chiamare quando si lascia la pagina).
function on(type, fn) {
  (listeners[type] = listeners[type] || []).push(fn);
  return () => { listeners[type] = listeners[type].filter((f) => f !== fn); };
}
function emit(type, msg) { (listeners[type] || []).forEach((fn) => fn(msg)); }

function send(cmd) {
  if (webview) webview.postMessage(JSON.stringify(cmd));
  else if (window.Mock) window.Mock.command(cmd);
}

// ------------------------------------------------------------------ traduzioni

// Il dizionario arriva dall'app (messaggio "strings"): inglese gia' sovrascritto dalla
// lingua attiva. I segnaposto {nome} prendono i valori di `params`; un parametro che
// finisce in "_key" e' a sua volta una chiave e riempie {nome-senza-_key}. Un solo
// passaggio, come Translations::Format: un valore che contiene {altro} resta com'e'.
const missingWarned = new Set();
function t(key, params) {
  if (!key) return '';
  let text = store.strings[key];
  if (text === undefined) {
    if (!missingWarned.has(key) && Object.keys(store.strings).length) { missingWarned.add(key); console.warn('testo mancante:', key); }
    text = key;
  }
  if (!params) return text;
  const values = new Map();
  for (const [name, value] of Object.entries(params)) {
    const isKey = name.endsWith('_key');
    const slot = isKey ? name.slice(0, -4) : name;
    if (!values.has(slot)) values.set(slot, isKey ? t(value) : String(value));
  }
  return text.replace(/\{([^{}]*)\}/g, (token, slot) => (values.has(slot) ? values.get(slot) : token));
}
function tp(key, n, params) {
  const form = new Intl.PluralRules(store.lang).select(n) === 'one' ? '_one' : '_other';
  return t(key + form, { n: fmtNumber(n), ...params });
}
// Chiave arrivata dall'app (attivita', avvisi): se e' un plurale (key_one/key_other) e
// c'e' il parametro n, sceglie la forma come tp().
function tMsg(key, params) {
  if (params && params.n !== undefined && store.strings[key] === undefined && store.strings[key + '_other'] !== undefined) {
    const { n, ...rest } = params;
    return tp(key, Number(n), rest);
  }
  return t(key, params);
}
// Testi fissi di index.html: data-i18n (contenuto) e data-i18n-title (suggerimento).
function applyStaticStrings() {
  document.documentElement.lang = store.lang;
  document.querySelectorAll('[data-i18n]').forEach((el) => setText(el, t(el.dataset.i18n)));
  document.querySelectorAll('[data-i18n-title]').forEach((el) => { el.title = t(el.dataset.i18nTitle); });
}

function receive(msg) {
  switch (msg.type) {
    case 'strings':
      store.lang = msg.lang;
      store.strings = msg.strings || {};
      applyStaticStrings();
      buildNav();
      if (current) showPage(current.id);
      document.body.classList.remove('loading');
      break;
    case 'state':
      store.state = msg;
      store.nextMoveAt = performance.now() + msg.next_move_ms;
      break;
    case 'config': store.config = msg.config; applyLook(); break;
    case 'apps': store.apps = msg.apps; break;
    case 'dim':
      store.dim = msg;
      store.highlightUntil = performance.now() + msg.highlight_left_ms;
      break;
    case 'activity':
      if (msg.events) store.activity = msg.events;
      else if (msg.add) {
        const a = msg.add, first = store.activity[0];
        const same = first && first.kind === a.kind && first.key === a.key && JSON.stringify(first.params) === JSON.stringify(a.params);
        if (same && a.count > 1) store.activity[0] = a;
        else store.activity = [a, ...store.activity].slice(0, 100);
      }
      break;
    case 'signals':
      if (msg.history) {
        store.signals = msg.history.map((s) => (s ? { measured: true, risk: s[0], motion: s[1], brightness: s[2] } : { measured: false }));
      } else {
        store.signals = [...store.signals, msg].slice(-60);
        store.signalCaptionKey = msg.caption_key || '';
      }
      store.signalsVersion++;
      break;
    case 'notice': toast(tMsg(msg.key, msg.params)); return;
    case 'navigate': showPage(msg.page); return;
    case 'processes': case 'browsed': break;
    default: store[msg.type] = msg;
  }
  emit(msg.type, msg);
  scheduleRender();
}
if (webview) webview.addEventListener('message', (e) => receive(e.data));

function setConfig(patch) { send({ cmd: 'setConfig', patch }); }

// Al massimo un invio ogni `ms` durante il trascinamento, sempre l'ultimo valore.
function throttle(fn, ms = 100) {
  let last = 0, timer = 0, pending = null;
  const fire = () => { last = performance.now(); timer = 0; const a = pending; pending = null; fn(...a); };
  return (...args) => {
    pending = args;
    if (timer) return;
    const wait = ms - (performance.now() - last);
    if (wait <= 0) fire();
    else timer = setTimeout(fire, wait);
  };
}
// setConfig limitato per un campo (cursori).
const throttledConfig = (key, map = (v) => v) => throttle((v) => setConfig({ [key]: map(v) }), 100);

// ------------------------------------------------------------------ formati

const pad2 = (n) => String(n).padStart(2, '0');
// Numeri, percentuali e date nella lingua attiva.
function fmtNumber(v, digits = 0) {
  return new Intl.NumberFormat(store.lang, { minimumFractionDigits: digits, maximumFractionDigits: digits }).format(v);
}
// v in punti percentuali (12.5 -> "12,5%" in italiano).
const fmtPct = (v, digits = 0) =>
  new Intl.NumberFormat(store.lang, { style: 'percent', minimumFractionDigits: digits, maximumFractionDigits: digits }).format(v / 100);
const fmtMin = (s) => t('unit.min', { n: fmtNumber(Math.round(s / 6) / 10, Number.isInteger(Math.round(s / 6) / 10) ? 0 : 1) });
function fmtClock(ms) {
  const s = Math.max(0, Math.floor(ms / 1000));
  const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60);
  return h > 0 ? t('unit.clock_hm', { h, m: pad2(m) }) : t('unit.clock_ms', { m, s: pad2(s % 60) });
}
function fmtHours(ms) {
  const h = ms / 3600000;
  if (h < 1) return t('unit.min', { n: fmtNumber(Math.round(ms / 60000)) });
  return t('unit.h', { n: h < 10 ? fmtNumber(h, 1) : fmtNumber(Math.round(h)) });
}
function fmtWhen(unixMs) {
  if (!unixMs) return t('time.never');
  const d = new Date(unixMs), now = new Date();
  const time = d.toLocaleTimeString(store.lang, { hour: '2-digit', minute: '2-digit' });
  if (d.toDateString() === now.toDateString()) return (now - d < 120000) ? t('time.now') : t('time.today_at', { time });
  if (new Date(now - 86400000).toDateString() === d.toDateString()) return t('time.yesterday_at', { time });
  return t('time.date_at', { date: d.toLocaleDateString(store.lang, { day: 'numeric', month: 'short' }), time });
}

// ------------------------------------------------------------------ DOM

// h('div.a.b', {on: {click}, title: 'x'}, figli...)
function h(tag, props, ...children) {
  const [name, ...classes] = tag.split('.');
  const el = document.createElement(name || 'div');
  if (classes.length) el.className = classes.join(' ');
  if (props) {
    for (const [k, v] of Object.entries(props)) {
      if (v === undefined || v === null || v === false) continue;
      if (k === 'on') for (const [ev, fn] of Object.entries(v)) el.addEventListener(ev, fn);
      else if (k === 'text') el.textContent = v;
      else if (k === 'html') el.innerHTML = v;
      else if (k === 'cls') el.className += ' ' + v;
      else el.setAttribute(k, v === true ? '' : v);
    }
  }
  for (const c of children.flat()) {
    if (c === null || c === undefined || c === false) continue;
    el.append(c instanceof Node ? c : document.createTextNode(String(c)));
  }
  return el;
}
function setText(el, text) { if (el.textContent !== text) el.textContent = text; }

// Icone a tratto (24x24), colore dal testo.
const ICONS = {
  grid: '<rect x="3.5" y="3.5" width="7" height="7" rx="1.5"/><rect x="13.5" y="3.5" width="7" height="7" rx="1.5"/><rect x="3.5" y="13.5" width="7" height="7" rx="1.5"/><rect x="13.5" y="13.5" width="7" height="7" rx="1.5"/>',
  circle: '<circle cx="12" cy="12" r="8"/>',
  monitor: '<rect x="3" y="4" width="18" height="12" rx="2"/><path d="M8 20h8M12 16v4"/>',
  chart: '<path d="M5 20V11M10 20V5M15 20v-7M20 20V9"/>',
  apps: '<rect x="3.5" y="3.5" width="7" height="7" rx="1.5"/><rect x="13.5" y="3.5" width="7" height="7" rx="1.5"/><rect x="3.5" y="13.5" width="7" height="7" rx="1.5"/><path d="M17 13.5v7M13.5 17h7"/>',
  keyboard: '<rect x="2.5" y="6" width="19" height="12" rx="2"/><path d="M6 10h.01M10 10h.01M14 10h.01M18 10h.01M7 14h10"/>',
  gear: '<circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.7 1.7 0 0 0 .3 1.8l.1.1a2 2 0 1 1-2.8 2.8l-.1-.1a1.7 1.7 0 0 0-1.8-.3 1.7 1.7 0 0 0-1 1.5V21a2 2 0 1 1-4 0v-.1a1.7 1.7 0 0 0-1.1-1.5 1.7 1.7 0 0 0-1.8.3l-.1.1a2 2 0 1 1-2.8-2.8l.1-.1a1.7 1.7 0 0 0 .3-1.8 1.7 1.7 0 0 0-1.5-1H3a2 2 0 1 1 0-4h.1a1.7 1.7 0 0 0 1.5-1.1 1.7 1.7 0 0 0-.3-1.8l-.1-.1a2 2 0 1 1 2.8-2.8l.1.1a1.7 1.7 0 0 0 1.8.3H9a1.7 1.7 0 0 0 1-1.5V3a2 2 0 1 1 4 0v.1a1.7 1.7 0 0 0 1 1.5 1.7 1.7 0 0 0 1.8-.3l.1-.1a2 2 0 1 1 2.8 2.8l-.1.1a1.7 1.7 0 0 0-.3 1.8V9a1.7 1.7 0 0 0 1.5 1H21a2 2 0 1 1 0 4h-.1a1.7 1.7 0 0 0-1.5 1z"/>',
  windows: '<rect x="3.5" y="3.5" width="7.5" height="7.5" rx="1"/><rect x="13" y="3.5" width="7.5" height="7.5" rx="1"/><rect x="3.5" y="13" width="7.5" height="7.5" rx="1"/><rect x="13" y="13" width="7.5" height="7.5" rx="1"/>',
  doc: '<path d="M14 3H7a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2V8z"/><path d="M14 3v5h5M9 13h6M9 17h6"/>',
  info: '<circle cx="12" cy="12" r="9"/><path d="M12 11v6M12 7.5h.01"/>',
  book: '<path d="M4 5a2 2 0 0 1 2-2h13v16H6a2 2 0 0 0-2 2z"/><path d="M4 19V5"/>',
  pause: '<path d="M9 6v12M15 6v12"/>',
  play: '<path d="M8 5.5v13l10-6.5z"/>',
  min: '<path d="M4 12h16"/>',
  max: '<rect x="5" y="5" width="14" height="14" rx="1"/>',
  close: '<path d="M6 6l12 12M18 6L6 18"/>',
  move: '<path d="M12 3v18M3 12h18M12 3l-3 3M12 3l3 3M12 21l-3-3M12 21l3-3M3 12l3-3M3 12l3 3M21 12l-3-3M21 12l-3 3"/>',
  eye: '<path d="M2 12s3.5-7 10-7 10 7 10 7-3.5 7-10 7S2 12 2 12z"/><circle cx="12" cy="12" r="3"/>',
  moon: '<path d="M20 14.5A8 8 0 1 1 9.5 4a6.5 6.5 0 0 0 10.5 10.5z"/>',
  vignette: '<rect x="3" y="5" width="18" height="14" rx="2"/><rect x="7" y="9" width="10" height="6" rx="1" stroke-dasharray="2 2"/>',
  search: '<circle cx="11" cy="11" r="7"/><path d="M20 20l-4-4"/>',
  plus: '<path d="M12 5v14M5 12h14"/>',
  trash: '<path d="M4 7h16M9 7V4h6v3M6 7l1 13h10l1-13"/>',
  folder: '<path d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/>',
  list: '<path d="M8 6h13M8 12h13M8 18h13M3.5 6h.01M3.5 12h.01M3.5 18h.01"/>',
  check: '<path d="M5 12.5l4.5 4.5L19 7.5"/>',
  reset: '<path d="M4 12a8 8 0 1 0 2.4-5.7L4 8.6"/><path d="M4 4v4.6h4.6"/>',
  power: '<path d="M12 3v8"/><path d="M6.3 7a8 8 0 1 0 11.4 0"/>',
  sun: '<circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"/>',
  bolt: '<path d="M13 3L5 14h6l-1 7 8-11h-6z"/>',
};
function icon(name) {
  const span = document.createElement('span');
  span.style.display = 'contents';
  span.innerHTML = `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">${ICONS[name] || ''}</svg>`;
  return span;
}
// Scudo del logo (disegno proprio).
function logo() {
  const span = document.createElement('span');
  span.style.display = 'contents';
  span.innerHTML = '<svg viewBox="0 0 48 48"><defs><linearGradient id="lg" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#2ee6c4"/><stop offset="1" stop-color="#127a6a"/></linearGradient></defs>' +
    '<path d="M24 3 6 9.5v13c0 11 7.6 19.4 18 22.5 10.4-3.1 18-11.5 18-22.5v-13z" fill="#0c1316" stroke="url(#lg)" stroke-width="2.5"/>' +
    '<rect x="14" y="16" width="20" height="13" rx="2" fill="none" stroke="#e7eef0" stroke-width="2"/>' +
    '<rect x="17" y="19" width="14" height="7" rx="1" fill="url(#lg)" opacity="0.85"/><path d="M20 33h8" stroke="#e7eef0" stroke-width="2" stroke-linecap="round"/></svg>';
  return span;
}

// ------------------------------------------------------------------ componenti
// Ogni componente restituisce { el, set(valore) }; set non tocca un controllo che
// l'utente sta usando.

function toggle(onChange) {
  const el = h('button.toggle', { type: 'button', role: 'switch' });
  let value = false;
  el.addEventListener('click', (e) => { e.stopPropagation(); onChange(!value); });
  return { el, set(v) { value = !!v; el.classList.toggle('on', value); el.setAttribute('aria-checked', String(value)); } };
}

function segmented(options, onChange) {
  const el = h('div.seg', { role: 'radiogroup' });
  const buttons = options.map(([value, label]) => {
    const b = h('button', { type: 'button', text: label, on: { click: () => onChange(value) } });
    b.dataset.value = String(value);
    el.append(b);
    return b;
  });
  return { el, set(v) { buttons.forEach((b) => b.classList.toggle('on', b.dataset.value === String(v))); } };
}

// Segue il trascinamento sull'elemento stesso (pointer capture): nessun ascoltatore su
// window, cosi' una pagina lasciata non resta in memoria.
function trackDrag(input, onChange) {
  input.addEventListener('pointerdown', (e) => { input.setPointerCapture(e.pointerId); onChange(true); });
  const end = () => onChange(false);
  input.addEventListener('pointerup', end);
  input.addEventListener('lostpointercapture', end);
}

function paintRange(input) {
  const min = Number(input.min), max = Number(input.max);
  input.style.setProperty('--p', `${((Number(input.value) - min) / (max - min)) * 100}%`);
}

// Cursore con valore a destra. onInput riceve il numero mentre si trascina.
function slider({ min, max, step = 1, format = (v) => String(v), onInput }) {
  const input = h('input', { type: 'range', min, max, step });
  const value = h('span.value');
  const el = h('div.slider', null, input, value);
  let dragging = false;
  trackDrag(input, (v) => { dragging = v; });
  input.addEventListener('input', () => {
    const v = Number(input.value);
    paintRange(input);
    setText(value, format(v));
    onInput(v);
  });
  return {
    el, input,
    set(v) {
      if (dragging) return;
      input.value = v;
      paintRange(input);
      setText(value, format(Number(input.value)));
    },
    disable(off) { input.disabled = off; },
  };
}

// Cursore a doppia maniglia (minimo e massimo).
function dualSlider({ min, max, step = 1, format, onInput }) {
  const a = h('input', { type: 'range', min, max, step });
  const b = h('input', { type: 'range', min, max, step });
  const fill = h('div.fill');
  const box = h('div.dual', null, h('div.track'), fill, a, b);
  const value = h('span.value');
  const el = h('div.slider', null, box, value);
  let dragging = false;
  const range = () => [Math.min(Number(a.value), Number(b.value)), Math.max(Number(a.value), Number(b.value))];
  const paint = () => {
    const [lo, hi] = range();
    fill.style.left = `${((lo - min) / (max - min)) * 100}%`;
    fill.style.width = `${((hi - lo) / (max - min)) * 100}%`;
    setText(value, format(lo, hi));
  };
  [a, b].forEach((inp) => {
    trackDrag(inp, (v) => { dragging = v; });
    inp.addEventListener('input', () => { paint(); onInput(...range()); });
  });
  return { el, set(lo, hi) { if (dragging) return; a.value = lo; b.value = hi; paint(); } };
}

// Schede-scelta con titolo e sottotitolo.
function choiceCards(options, onChange) {
  const el = h('div.choices');
  const cards = options.map(([value, title, desc]) => {
    const c = h('button.choice', { type: 'button', on: { click: () => onChange(value) } }, h('b', { text: title }), h('small', { text: desc }));
    c.dataset.value = value;
    el.append(c);
    return c;
  });
  return { el, set(v) { cards.forEach((c) => c.classList.toggle('on', c.dataset.value === String(v))); } };
}

function pill(text, tone) { return h('span.pill', { cls: tone, text }); }

// Pannello con intestazione; `right` sono elementi a destra del titolo.
function panel(title, sub, ...right) {
  const body = h('div.panel-body');
  const el = h('section.panel', null,
    h('header.panel-head', null, h('h3', { text: title }), sub ? h('span.sub', { text: sub }) : null, h('span.grow'), ...right),
    body);
  return { el, body };
}

// Riga impostazione: titolo e descrizione a sinistra, controllo a destra.
// wide: controllo largo (cursore) sotto l'etichetta, a tutta larghezza.
function settingRow(title, desc, control, opts = {}) {
  const descEl = typeof desc === 'string' ? h('p', { text: desc }) : desc;
  return h('div.row', { cls: [opts.cls, opts.wide ? 'stacked' : null].filter(Boolean).join(' ') || null },
    h('div.label', null, h('b', { text: title }), descEl || null, opts.code ? h('code', { text: opts.code }) : null),
    h('div.control', null, control));
}

// Rapporto di contrasto WCAG fra due colori #rrggbb.
function contrastRatio(a, b) {
  const lum = (hex) => {
    const c = [1, 3, 5].map((i) => parseInt(hex.slice(i, i + 2), 16) / 255)
      .map((v) => (v <= 0.03928 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4)));
    return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
  };
  const [hi, lo] = [lum(a), lum(b)].sort((x, y) => y - x);
  return (hi + 0.05) / (lo + 0.05);
}

// Colore stabile ricavato dal nome (avatar delle app).
function nameHue(name) {
  let hash = 0;
  for (const ch of name) hash = (hash * 31 + ch.charCodeAt(0)) >>> 0;
  return hash % 360;
}
function avatar(exe) {
  const el = h('span.avatar', { text: (exe[0] || '?').toUpperCase() });
  el.style.background = `hsl(${nameHue(exe)} 55% 42%)`;
  return el;
}

let toastTimer = 0;
function toast(text) {
  const t = $('toast');
  t.textContent = text;
  t.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { t.hidden = true; }, 4000);
}

// ------------------------------------------------------------------ aspetto

const ACCENTS = {
  teal: '#1fc7a9', indigo: '#7c86ff', coral: '#ff7a59', green: '#5fdc8f', amber: '#f2a641', violet: '#b28cff',
};
const TEXT_SIZES = { small: 13, normal: 14, large: 15.5, xl: 17 };
const PANEL_COLOR = '#0c1316';
function applyLook() {
  const c = store.config;
  if (!c) return;
  const accent = ACCENTS[c.ui_accent] || ACCENTS.teal;
  const root = document.documentElement.style;
  root.setProperty('--accent', accent);
  root.setProperty('--accent-soft', accent + '22');
  root.fontSize = `${TEXT_SIZES[c.ui_text_size] || 14}px`;
}

// ------------------------------------------------------------------ guscio e pagine

// Gruppi e pagine della barra laterale: [chiave del gruppo, [[id, icona], ...]]; il testo
// di ogni voce e' la chiave "nav.<id>".
const NAV = [
  ['nav.group.protect', [['panoramica', 'grid'], ['overlay', 'circle'], ['monitor', 'monitor'], ['statistiche', 'chart']]],
  ['nav.group.configure', [['profili', 'apps'], ['tasti', 'keyboard'], ['impostazioni', 'gear'], ['windows', 'windows']]],
  ['nav.group.help', [['avanzate', 'doc'], ['informazioni', 'info']]],
];
const PAGES = {};  // id -> { build(root) -> { update() } }, definiti in pages.js
let current = null;  // { id, view }

function showPage(id) {
  if (!PAGES[id]) return;
  if (current && current.view.leave) current.view.leave();
  const root = $('content');
  root.innerHTML = '';
  root.scrollTop = 0;
  const view = PAGES[id].build(root) || {};
  current = { id, view };
  document.querySelectorAll('#nav button').forEach((b) => b.classList.toggle('on', b.dataset.page === id));
  updateDimWatch();
  renderNow();
}

function buildNav() {
  const nav = $('nav');
  nav.innerHTML = '';
  for (const [group, items] of NAV) {
    nav.append(h('h6', { text: t(group) }));
    for (const [id, ic] of items) {
      const b = h('button', { type: 'button', on: { click: () => showPage(id) } }, icon(ic), h('span', { text: t('nav.' + id) }));
      b.dataset.page = id;
      b.classList.toggle('on', !!current && current.id === id);
      nav.append(b);
    }
  }
}

function buildShell() {
  $('brandLogo').append(logo());
  buildNav();
  $('newsIcon').append(icon('book'));
  $('btnNews').addEventListener('click', () => showPage('informazioni'));
  $('btnTopStats').append(icon('chart'));
  $('btnTopStats').addEventListener('click', () => showPage('statistiche'));
  $('btnTopSettings').append(icon('gear'));
  $('btnTopSettings').addEventListener('click', () => showPage('impostazioni'));
  $('btnOpenStats').addEventListener('click', () => showPage('statistiche'));
  $('btnTopPause').addEventListener('click', () => send({ cmd: 'setManualPause', on: !(store.state && store.state.manual_pause) }));
  const winIcons = { minimize: 'min', maximize: 'max', close: 'close' };
  document.querySelectorAll('[data-win]').forEach((b) => {
    b.append(icon(winIcons[b.dataset.win]));
    b.addEventListener('click', () => send({ cmd: 'window', action: b.dataset.win }));
  });
  document.querySelector('.top .fill').addEventListener('dblclick', () => send({ cmd: 'window', action: 'maximize' }));
}

// Grafico a linea con area sfumata; i campioni non misurati interrompono la linea.
// values: numeri o null; max: valore in cima al grafico.
function sparkline(box, values, max) {
  const W = 120, H = 30, n = 60;
  const x = (i) => ((i + n - values.length) / (n - 1)) * W;
  const y = (v) => H - Math.min(1, v / max) * H;
  let line = '', area = '';
  let run = [];
  const flush = () => {
    if (run.length > 0) {
      const pts = run.map(([i, v]) => `${x(i).toFixed(1)},${y(v).toFixed(1)}`);
      line += `M${pts.join('L')}`;
      area += `M${x(run[0][0]).toFixed(1)},${H}L${pts.join('L')}L${x(run[run.length - 1][0]).toFixed(1)},${H}Z`;
    }
    run = [];
  };
  values.forEach((v, i) => { if (v === null || v === undefined) flush(); else run.push([i, v]); });
  flush();
  const svg = `<svg viewBox="0 0 ${W} ${H}" preserveAspectRatio="none"><path class="area" d="${area}"/><path class="line" d="${line}"/></svg>`;
  if (box.dataset.svg !== svg) { box.innerHTML = svg; box.dataset.svg = svg; }
}

function channel(key) {
  const vals = store.signals.map((s) => (s.measured ? s[key] : null));
  const m = vals.filter((v) => v !== null);
  return { vals, peak: m.length ? Math.max(...m) : 0, mean: m.length ? m.reduce((a, b) => a + b, 0) / m.length : 0, any: m.length > 0 };
}

let lastSignalsVersion = -1;
function renderSignals() {
  if (store.signalsVersion === lastSignalsVersion) return;
  lastSignalsVersion = store.signalsVersion;
  const last = store.signals[store.signals.length - 1];
  const live = !!(last && last.measured);
  const pct = (v) => fmtPct(v * 100);
  const n = (v) => fmtNumber(Math.round(v * 100));
  const risk = channel('risk'), motion = channel('motion'), bright = channel('brightness');
  sparkline($('sparkRisk'), risk.vals, 1);
  sparkline($('sparkMotion'), motion.vals, 1);
  sparkline($('sparkBright'), bright.vals, Math.max(1, bright.peak));
  setText($('sigRisk'), live ? pct(last.risk) : '–');
  setText($('sigMotion'), live ? pct(last.motion) : '–');
  setText($('sigBright'), live ? pct(last.brightness) : '–');
  const paused = t('signals.paused');
  setText($('sigRiskSub'), live ? t('signals.peak_mean', { peak: n(risk.peak), mean: n(risk.mean) }) : paused);
  setText($('sigMotionSub'), live ? t('signals.mean_caption', { mean: n(motion.mean), caption_key: store.signalCaptionKey }) : paused);
  setText($('sigBrightSub'), live ? t('signals.mean_apl', { mean: n(bright.mean) }) : paused);
}

function statusInfo(s) {
  if (!s) return { cls: 'paused', text: '...' };
  if (s.testing_shift) return { cls: 'test', text: t('status.test') };
  if (s.paused) return { cls: 'paused', text: t('status.paused', { reason_key: s.reason_key }) };
  if (s.away) return { cls: 'away', text: t('status.away') };
  return { cls: '', text: t('status.protected') };
}

function renderShell() {
  const s = store.state, st = store.stats, about = store.about;
  const info = statusInfo(s);
  const status = $('topStatus');
  status.className = 'status ' + info.cls;
  setText(status.lastElementChild, info.text);
  const pauseBtn = $('btnTopPause');
  const manual = !!(s && s.manual_pause);
  if (pauseBtn.dataset.icon !== String(manual)) {
    pauseBtn.innerHTML = '';
    pauseBtn.append(icon(manual ? 'play' : 'pause'));
    pauseBtn.dataset.icon = String(manual);
  }
  pauseBtn.title = manual ? t('shell.resume') : t('shell.pause_manual');
  pauseBtn.classList.toggle('on', manual);
  const session = s ? fmtClock(s.session_ms) : '-';
  setText($('topSession'), session);
  setText($('sSession'), session);
  const version = about ? `v${about.version}` : 'v-';
  ['brandVersion', 'footVersion', 'topVersion'].forEach((id) => setText($(id), version));
  if (st) setText($('sTotal'), fmtHours(st.total_ms));
  const hist = store.history;
  if (hist) {
    setText($('sPeak'), fmtPct(hist.risk_peak_24h * 100));
    setText($('sAvoided'), hist.avoided_today === null ? t('common.na') : fmtPct(hist.avoided_today * 100));
  }
  renderSignals();
}

// Ridisegno raggruppato: piu' messaggi nello stesso frame producono un solo passaggio.
let renderQueued = false;
function scheduleRender() {
  if (renderQueued) return;
  renderQueued = true;
  requestAnimationFrame(renderNow);
}
function renderNow() {
  renderQueued = false;
  renderSetup();
  renderShell();
  if (current && current.view.update) current.view.update();
}

// La mappa dell'oscuramento arriva solo mentre una pagina la mostra: altrimenti l'app
// non legge nulla dalla GPU.
let dimWatching = false;
function updateDimWatch() {
  const want = !!current && (current.id === 'panoramica' || current.id === 'overlay') && !document.hidden;
  if (want === dimWatching) return;
  dimWatching = want;
  send({ cmd: 'dimWatch', on: want });
}
document.addEventListener('visibilitychange', updateDimWatch);

// Attenuazione dopo 60 s senza interazione.
let idleTimer = 0;
function wake() {
  document.body.classList.remove('dim');
  clearTimeout(idleTimer);
  idleTimer = setTimeout(() => document.body.classList.add('dim'), 60000);
}
['mousemove', 'mousedown', 'keydown', 'wheel'].forEach((ev) => window.addEventListener(ev, wake, { passive: true }));

// Conto alla rovescia e tempi che scorrono fra un messaggio e l'altro.
setInterval(() => { if (current && current.view.tick) current.view.tick(); }, 250);
