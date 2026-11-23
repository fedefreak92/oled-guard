// Le dieci pagine dell'interfaccia. Ogni pagina costruisce il suo DOM una volta
// (build) e lo aggiorna dallo store (update) a ogni messaggio; tick() 4 volte al secondo.
'use strict';

const cfg = () => store.config;

// ------------------------------------------------------------------ disegni condivisi

// Radar: griglia (2a+1)x(2a+1) con mappa di calore del tempo totale e scia delle mosse.
function drawRadar(svg) {
  const s = store.state, st = store.stats, c = cfg();
  const a = (c && c.amplitude_px) || (s && s.amplitude) || 2;
  const mid = 150, step = a === 1 ? 90 : (a === 2 ? 58 : 42);
  const pos = (x, y) => [mid + x * step, mid + y * step];
  const cell = (x, y) => (st ? st.cells[(y + 3) * 7 + (x + 3)] : 0);
  let max = 0;
  for (let y = -a; y <= a; y++) for (let x = -a; x <= a; x++) max = Math.max(max, cell(x, y));
  let out = `<circle class="ring" cx="${mid}" cy="${mid}" r="142"/><circle class="ring in" cx="${mid}" cy="${mid}" r="95"/>` +
    `<circle class="ring in" cx="${mid}" cy="${mid}" r="48"/><line class="axis" x1="${mid}" y1="8" x2="${mid}" y2="292"/>` +
    `<line class="axis" x1="8" y1="${mid}" x2="292" y2="${mid}"/>`;
  const size = Math.min(16, step * 0.32);
  for (let y = -a; y <= a; y++) {
    for (let x = -a; x <= a; x++) {
      const [px, py] = pos(x, y);
      const heat = max > 0 ? cell(x, y) / max : 0;
      out += `<rect class="cell" x="${px - size / 2}" y="${py - size / 2}" width="${size}" height="${size}" rx="2" opacity="${(0.12 + heat * 0.55).toFixed(2)}"/>`;
    }
  }
  const active = s && !s.paused;
  if (active && s.trail && s.trail.length > 1) {
    for (let i = 0; i < s.trail.length - 1; i++) {
      const [x1, y1] = pos(...s.trail[i]), [x2, y2] = pos(...s.trail[i + 1]);
      out += `<line class="trail" x1="${x1}" y1="${y1}" x2="${x2}" y2="${y2}" opacity="${(0.25 + 0.6 * (i + 1) / s.trail.length).toFixed(2)}"/>`;
    }
  }
  if (active) {
    const [nx, ny] = pos(s.offset[0], s.offset[1]);
    const big = size + 6;
    out += `<rect class="now" x="${nx - big / 2}" y="${ny - big / 2}" width="${big}" height="${big}" rx="3"/>` +
      `<circle class="now-ring" cx="${nx}" cy="${ny}" r="${big}"/>`;
  }
  if (svg.dataset.last !== out) { svg.innerHTML = out; svg.dataset.last = out; }
}
function radarSvg() {
  const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  svg.setAttribute('viewBox', '0 0 300 300');
  svg.setAttribute('class', 'radar');
  svg.setAttribute('aria-label', 'Posizioni dello spostamento');
  return svg;
}

// Miniatura delle zone oscurate (livelli 0-f), colore ambra.
function drawDimMap(cv) {
  const ctx = cv.getContext('2d'), W = cv.width, H = cv.height, d = store.dim;
  ctx.fillStyle = '#000';
  ctx.fillRect(0, 0, W, H);
  if (d && d.cells && d.cols > 0 && d.rows > 0) {
    const cw = W / d.cols, ch = H / d.rows;
    for (let y = 0; y < d.rows; y++) {
      for (let x = 0; x < d.cols; x++) {
        const v = parseInt(d.cells[y * d.cols + x], 16);
        if (!v) continue;
        ctx.fillStyle = `rgba(242,166,65,${(v / 15).toFixed(3)})`;
        ctx.fillRect(Math.floor(x * cw), Math.floor(y * ch), Math.ceil(cw), Math.ceil(ch));
      }
    }
  } else {
    ctx.fillStyle = '#56666b';
    ctx.font = '22px "Cascadia Mono", Consolas, monospace';
    ctx.textAlign = 'center';
    ctx.fillText(d && d.status === 'paused' ? 'in pausa' : 'mappa non ancora disponibile', W / 2, H / 2);
  }
}
function dimMapCanvas() { return h('canvas.dimmap', { width: 640, height: 360, 'aria-label': 'Zone oscurate' }); }

const DIM_STATUS = { active: 'Attivo', waiting: 'In attesa', suspended: 'Sospeso', off: 'Spento', paused: 'In pausa' };
function dimStatusText() {
  const d = store.dim;
  if (!d) return '-';
  return (DIM_STATUS[d.status] || d.status) + (d.suspended_by ? ` · ${d.suspended_by}` : '');
}
function highlightLeft() { return Math.max(0, store.highlightUntil - performance.now()); }
function canHighlight() { const d = store.dim; return !!d && !['off', 'suspended', 'paused'].includes(d.status); }

const ACTIVITY_ICONS = { pause: 'pause', manual: 'pause', resume: 'play', test: 'move', away: 'moon', back: 'sun', monitor: 'monitor', windows: 'windows', config: 'doc', alert: 'bolt' };

// ------------------------------------------------------------------ panoramica

PAGES.panoramica = {
  build(root) {
    // Protezione
    const prot = panel('Protezione');
    const sub = prot.el.querySelector('.panel-head');
    const monLabel = h('span.sub');
    sub.insertBefore(monLabel, sub.children[1]);
    const svg = radarSvg();
    const count = h('div.big-count', null, h('span', { text: '--:--' }), h('small', { text: 'PROSSIMA MOSSA' }));
    const kv = (label) => { const b = h('b', { text: '-' }); return [h('div.kv', null, h('span', { text: label }), b), b]; };
    const [kOffset, vOffset] = kv('Posizione attuale');
    const [kFg, vFg] = kv('In primo piano');
    const [kLat, vLat] = kv('Latenza aggiunta');
    const [kAmp, vAmp] = kv('Spostamento massimo');
    prot.body.classList.add('pad');
    prot.body.append(h('div.protect', null, svg, h('div', null, count, h('div', null, kOffset, kFg, kLat, kAmp))));

    // Azioni rapide
    const quick = panel('Azioni rapide');
    quick.body.classList.add('pad');
    const qa = (ic, title, onClick, withToggle) => {
      const sm = h('small');
      const tg = withToggle ? toggle(() => {}) : null;
      const el = h('button.qa', { type: 'button', on: { click: onClick } },
        h('div.qa-top', null, h('span.qa-icon', null, icon(ic)), tg ? tg.el : null),
        h('div', null, h('b', { text: title }), sm));
      return { el, sm, tg };
    };
    const qPause = qa('pause', 'Pausa', () => send({ cmd: 'setManualPause', on: !(store.state && store.state.manual_pause) }), true);
    const qTest = qa('move', 'Prova lo spostamento', () => send({ cmd: 'testShift' }), false);
    const qHigh = qa('eye', 'Evidenzia zone', () => send({ cmd: 'dimHighlight' }), false);
    const qDim = qa('sun', 'Oscuramento', () => cfg() && setConfig({ dim_enabled: !cfg().dim_enabled }), true);
    const qVig = qa('vignette', 'Bordi scuriti', () => cfg() && setConfig({ vignette_enabled: !cfg().vignette_enabled }), true);
    quick.body.append(h('div.quick', null, qPause.el, qTest.el, qHigh.el, qDim.el, qVig.el));

    // Zone oscurate
    const zones = panel('Zone oscurate ora');
    zones.body.classList.add('pad');
    const cv = dimMapCanvas();
    const [kSt, vSt] = kv('Stato');
    const [kArea, vArea] = kv('Zone oscurate');
    const [kMax, vMax] = kv('Oscuramento più forte');
    zones.body.append(cv, h('div', null, kSt, kArea, kMax));

    // Attivita' recente
    const clear = h('button.btn.small', { type: 'button', text: 'Cancella', on: { click: () => send({ cmd: 'clearActivity' }) } });
    const act = panel('Attività recente', 'da quando l\'app è aperta', clear);
    const list = h('div.activity');
    act.body.append(list);

    root.append(h('div.page', null, h('div.cols', null,
      h('div.stack', null, prot.el, quick.el),
      h('div.stack', null, zones.el, act.el))));

    let lastActivity = null, lastDim = null;
    const renderActivity = () => {
      if (store.activity === lastActivity) return;
      lastActivity = store.activity;
      list.innerHTML = '';
      if (!store.activity.length) list.append(h('div.empty', { text: 'Nessun evento per ora.' }));
      for (const e of store.activity.slice(0, 30)) {
        list.append(h('div.act', { cls: e.kind },
          h('span.ai', null, icon(ACTIVITY_ICONS[e.kind] || 'info')),
          h('span', { text: t(e.key, e.params) + (e.count > 1 ? ` ×${e.count}` : '') }),
          h('time', { text: fmtWhen(e.t) })));
      }
    };

    const view = {
      update() {
        const s = store.state, c = cfg(), d = store.dim;
        if (s) {
          setText(monLabel, s.monitor ? `${s.monitor} · ${s.width}×${s.height} ${s.hdr ? 'HDR' : 'SDR'}` : '');
          setText(vOffset, s.paused ? '-' : `${s.offset[0] >= 0 ? '+' : ''}${s.offset[0]}, ${s.offset[1] >= 0 ? '+' : ''}${s.offset[1]} px`);
          setText(vFg, s.foreground || '-');
          setText(vLat, s.latency_ms ? `${s.latency_ms.toFixed(1).replace('.', ',')} ms` : '-');
          setText(count.lastChild, s.paused ? 'IN PAUSA' : (s.testing_shift ? 'PROVA · PROSSIMA MOSSA' : 'PROSSIMA MOSSA'));
          qPause.tg.set(s.manual_pause);
          qPause.el.classList.toggle('on', s.manual_pause);
          setText(qPause.sm, s.manual_pause ? 'Protezione ferma' : 'Protezione attiva');
          qTest.el.classList.toggle('on', s.testing_shift);
          setText(qTest.sm, s.testing_shift ? 'Prova in corso' : '30 s accelerati');
        }
        if (c) {
          setText(vAmp, `${c.amplitude_px} px`);
          qDim.tg.set(c.dim_enabled);
          qDim.el.classList.toggle('on', c.dim_enabled);
          setText(qDim.sm, c.dim_enabled ? 'Zone ferme scurite' : 'Spento');
          qVig.tg.set(c.vignette_enabled);
          qVig.el.classList.toggle('on', c.vignette_enabled);
          setText(qVig.sm, c.vignette_enabled ? `Intensità ${c.vignette_strength_pct}%` : 'Spenti');
        }
        drawRadar(svg);
        if (d !== lastDim) {
          lastDim = d;
          drawDimMap(cv);
          setText(vSt, dimStatusText());
          setText(vArea, d && d.cells ? `${fmtPct(d.dimmed_pct, 1)} dello schermo` : '-');
          setText(vMax, d && d.cells ? fmtPct(d.max_level * 100) : '-');
        }
        renderActivity();
        view.tick();
      },
      tick() {
        const s = store.state;
        let text = '--:--';
        if (s && !s.paused) {
          const sec = Math.ceil(Math.max(0, store.nextMoveAt - performance.now()) / 1000);
          text = `${pad2(Math.floor(sec / 60))}:${pad2(sec % 60)}`;
        }
        setText(count.firstChild, text);
        const left = highlightLeft();
        qHigh.el.disabled = !canHighlight();
        qHigh.el.classList.toggle('on', left > 0);
        setText(qHigh.sm, left > 0 ? `Evidenziate · ${Math.ceil(left / 1000)} s` : '15 s sullo schermo');
      },
    };
    return view;
  },
};

// ------------------------------------------------------------------ overlay

// Sfasamento medio atteso dei clic con posizioni uniformi sulla griglia.
function meanClickOffset(a) {
  let sum = 0, n = 0;
  for (let y = -a; y <= a; y++) for (let x = -a; x <= a; x++) { sum += Math.hypot(x, y); n++; }
  return sum / n;
}

// Stessa formula di src/core/shade_model.cpp (fattore in luce lineare).
function shadeFactor(p, u, v) {
  const edge = (d, s) => { if (s <= 0) return 0; const t = Math.min(1, Math.max(0, d / s)); return 1 - t * t * (3 - 2 * t); };
  const vig = Math.max(edge(u, p.l), edge(1 - u, p.r), edge(v, p.t), edge(1 - v, p.b));
  return 1 - p.strength * vig;
}

const VIG_DEFAULTS = { vignette_left_pct: 8, vignette_right_pct: 8, vignette_top_pct: 0, vignette_bottom_pct: 12, vignette_strength_pct: 50 };

function vignetteEditor() {
  const W = 192, H = 108;
  const cv = h('canvas', { width: W, height: H });
  const stage = h('div.vig-stage', null, cv);
  const sides = {
    l: { key: 'vignette_left_pct', axis: 'x', dir: 1 },
    r: { key: 'vignette_right_pct', axis: 'x', dir: -1 },
    t: { key: 'vignette_top_pct', axis: 'y', dir: 1 },
    b: { key: 'vignette_bottom_pct', axis: 'y', dir: -1 },
  };
  const local = { l: 8, r: 8, t: 0, b: 12, strength: 50 };
  let dragging = null;
  for (const [id, s] of Object.entries(sides)) {
    s.guide = h('div.vig-guide', { cls: s.axis === 'x' ? 'v' : 'h' });
    s.handle = h('div.vig-handle', { cls: s.axis === 'y' ? 'v' : null, title: 'Trascina' });
    s.label = h('div.vig-label');
    s.send = throttledConfig(s.key);
    stage.append(s.guide, s.handle, s.label);
    s.handle.addEventListener('pointerdown', (e) => { dragging = id; s.handle.setPointerCapture(e.pointerId); e.preventDefault(); });
    s.handle.addEventListener('pointermove', (e) => {
      if (dragging !== id) return;
      const r = stage.getBoundingClientRect();
      const f = s.axis === 'x' ? (e.clientX - r.left) / r.width : (e.clientY - r.top) / r.height;
      const v = Math.round(Math.min(40, Math.max(0, (s.dir > 0 ? f : 1 - f) * 100)));
      if (v === local[id]) return;
      local[id] = v;
      paint();
      s.send(v);
    });
    const end = () => { if (dragging === id) dragging = null; };
    s.handle.addEventListener('pointerup', end);
    s.handle.addEventListener('pointercancel', end);
  }
  const strength = slider({ min: 0, max: 80, step: 1, format: (v) => `${v}%`, onInput: (v) => { local.strength = v; paint(); sendStrength(v); } });
  const sendStrength = throttledConfig('vignette_strength_pct');
  const strengthValue = h('b');
  const side = h('div.vig-side', null, h('small', { text: 'INTENSITÀ' }), strengthValue, strength.input);
  const reset = h('button.btn.small', { type: 'button', on: { click: () => setConfig({ ...VIG_DEFAULTS }) } }, icon('reset'), 'Ripristina tutti i lati');
  const el = h('div', null,
    h('div.vig-head', null, h('span', { text: 'Anteprima · trascina le maniglie' }), reset),
    h('div.vig', null, stage, side));

  function paint() {
    const p = { l: local.l / 100, r: local.r / 100, t: local.t / 100, b: local.b / 100, strength: local.strength / 100 };
    const ctx = cv.getContext('2d'), img = ctx.createImageData(W, H);
    for (let y = 0; y < H; y++) {
      for (let x = 0; x < W; x++) {
        const u = (x + 0.5) / W, v = (y + 0.5) / H;
        // Desktop finto: sfondo blu scuro con una finestra chiara al centro.
        let rgb = [38 + 40 * u, 62 + 30 * v, 92 + 20 * (1 - u)];
        if (u > 0.3 && u < 0.7 && v > 0.25 && v < 0.75) rgb = [196, 206, 210];
        if (v > 0.92) rgb = [24, 30, 36];
        const f = shadeFactor(p, u, v);
        const i = (y * W + x) * 4;
        // Moltiplicazione in luce lineare, come lo shader.
        for (let k = 0; k < 3; k++) {
          const lin = Math.pow(rgb[k] / 255, 2.2) * f;
          img.data[i + k] = Math.round(Math.pow(lin, 1 / 2.2) * 255);
        }
        img.data[i + 3] = 255;
      }
    }
    ctx.putImageData(img, 0, 0);
    const place = (id, x, y) => {
      const s = sides[id];
      if (s.axis === 'x') {
        s.guide.style.left = `${x}%`;
        s.handle.style.left = `${x}%`;
        s.handle.style.top = '50%';
        s.label.style.left = `calc(${x}% ${id === 'l' ? '+ 0.4rem' : '- 3.2rem'})`;
        s.label.style.top = 'calc(50% - 2.2rem)';
      } else {
        s.guide.style.top = `${y}%`;
        s.handle.style.top = `${y}%`;
        s.handle.style.left = '50%';
        s.label.style.top = `calc(${y}% ${id === 't' ? '+ 0.4rem' : '- 1.7rem'})`;
        s.label.style.left = 'calc(50% + 1.2rem)';
      }
      setText(s.label, `${local[id]}%`);
    };
    place('l', local.l, 0);
    place('r', 100 - local.r, 0);
    place('t', 0, local.t);
    place('b', 0, 100 - local.b);
    setText(strengthValue, `${local.strength}%`);
  }

  return {
    el,
    set(c) {
      if (!dragging) {
        local.l = c.vignette_left_pct; local.r = c.vignette_right_pct;
        local.t = c.vignette_top_pct; local.b = c.vignette_bottom_pct;
      }
      local.strength = c.vignette_strength_pct;
      strength.set(c.vignette_strength_pct);
      stage.classList.toggle('stage-off', !c.vignette_enabled);
      paint();
    },
  };
}

PAGES.overlay = {
  build(root) {
    // Spostamento
    const shift = panel('Spostamento dei pixel', 'pixel shift');
    const amp = segmented([[1, '1 px'], [2, '2 px'], [3, '3 px']], (v) => setConfig({ amplitude_px: v }));
    const mini = h('span.mini-grid');
    const click = h('p');
    const dwellSend = throttle((lo, hi) => setConfig({ dwell_min_s: lo, dwell_max_s: hi }), 100);
    const dwell = dualSlider({ min: 5, max: 180, step: 1, format: (lo, hi) => `${lo}-${hi} s`, onInput: dwellSend });
    const transSend = throttle((lo, hi) => setConfig({ transition_min_s: lo, transition_max_s: hi }), 100);
    const trans = dualSlider({ min: 0.5, max: 15, step: 0.5, format: (lo, hi) => `${String(lo).replace('.', ',')}-${String(hi).replace('.', ',')} s`, onInput: transSend });
    const testBtn = h('button.btn.solid', { type: 'button', on: { click: () => send({ cmd: 'testShift' }) } }, icon('move'), 'Prova ora');
    shift.body.append(
      settingRow('Spostamento massimo', 'Di quanti pixel al massimo si sposta l\'immagine. Più ampio protegge di più, ma i bordi si muovono di più.', amp.el),
      settingRow('Posizioni usate', click, mini),
      settingRow('Sosta fra una mossa e l\'altra', 'Tempo casuale in questo intervallo.', dwell.el, { wide: true }),
      settingRow('Durata della transizione', 'La mossa avviene piano, un pixel alla volta.', trans.el, { wide: true }),
      settingRow('Prova', 'Spostamento accelerato per 30 secondi, per vederlo.', testBtn));

    // Bordi scuriti
    const vigToggle = toggle((v) => setConfig({ vignette_enabled: v }));
    const vig = panel('Bordi scuriti', 'vignettatura', h('span.muted', { text: 'Attivi' }), vigToggle.el);
    vig.body.classList.add('pad');
    const editor = vignetteEditor();
    vig.body.append(editor.el, h('p.hint', { text: 'I bordi restano fermi mentre l\'immagine si sposta. Funzionano mentre la protezione è attiva.' }));

    // Oscuramento
    const dimToggle = toggle((v) => setConfig({ dim_enabled: v }));
    const dim = panel('Oscuramento delle zone ferme', null, h('span.muted', { text: 'Attivo' }), dimToggle.el);
    const cv = dimMapCanvas();
    const highBtn = h('button.btn.small', { type: 'button', on: { click: () => send({ cmd: 'dimHighlight' }) } }, icon('eye'), 'Evidenzia sullo schermo · 15 s');
    const status = h('span.muted');
    const keepOrder = (patch) => {
      const c = cfg() || {};
      if (patch.dim_delay_s !== undefined && c.dim_large_delay_s < patch.dim_delay_s) patch.dim_large_delay_s = Math.max(300, patch.dim_delay_s);
      if (patch.dim_large_delay_s !== undefined && c.dim_delay_s > patch.dim_large_delay_s) patch.dim_delay_s = patch.dim_large_delay_s;
      setConfig(patch);
    };
    const mk = (key, min, max, step, fmt, toCfg) => {
      const sendIt = throttle((v) => keepOrder({ [key]: toCfg(v) }), 100);
      return slider({ min, max, step, format: fmt, onInput: sendIt });
    };
    const strength = mk('dim_strength_pct', 10, 80, 1, (v) => `${v}%`, (v) => v);
    const delay = mk('dim_delay_s', 0.5, 60, 0.5, (v) => fmtMin(v * 60), (v) => Math.round(v * 60));
    const ramp = mk('dim_ramp_s', 0.5, 60, 0.5, (v) => fmtMin(v * 60), (v) => Math.round(v * 60));
    const winMode = segmented([['desktop', 'Come il desktop'], ['video', 'Solo loghi su video']], (v) => setConfig({ dim_window_mode: v }));
    const activate = segmented([['hover', 'Sotto il mouse'], ['click', 'Cliccata'], ['both', 'Entrambe'], ['never', 'Mai']], (v) => setConfig({ dim_activate: v }));
    const largeToggle = toggle((v) => setConfig({ dim_large_enabled: v }));
    const largeDelay = mk('dim_large_delay_s', 5, 240, 5, (v) => fmtMin(v * 60), (v) => Math.round(v * 60));
    const largeStrength = mk('dim_large_strength_pct', 5, 80, 1, (v) => `${v}%`, (v) => v);
    const rowsLarge = [
      settingRow('Oscura anche le finestre ferme', 'Documenti e pagine immobili (solo con "Solo loghi su video").', largeToggle.el),
      settingRow('Inizia quando è ferma da', null, largeDelay.el, { wide: true }),
      settingRow('Intensità massima delle finestre ferme', null, largeStrength.el, { wide: true }),
    ];
    const exLink = h('button.btn.small', { type: 'button', on: { click: () => showPage('profili') } }, icon('apps'), 'Gestisci le app escluse');
    const activateHint = h('p.hint');
    dim.body.append(
      h('div', { cls: 'row col' }, cv, h('div.add-row', null, status, h('span.grow'), highBtn)),
      settingRow('Intensità massima', 'Quanto si scuriscono icone, etichette e loghi fermi.', strength.el, { wide: true }),
      settingRow('Inizia quando è fermo da', null, delay.el, { wide: true }),
      settingRow('Durata della discesa', 'La luminosità scende piano, senza scatti.', ramp.el, { wide: true }),
      settingRow('Cosa si scurisce nelle finestre', null, winMode.el),
      settingRow('Torna normale la finestra', null, activate.el),
      activateHint,
      ...rowsLarge,
      settingRow('App escluse', 'Con queste app in primo piano l\'oscuramento si sospende.', exLink));

    root.append(h('div.page', null, h('div.cols.even', null, h('div.stack', null, shift.el, vig.el), h('div.stack', null, dim.el))));

    let lastDim = null;
    const view = {
      update() {
        const c = cfg();
        if (c) {
          amp.set(c.amplitude_px);
          const side = 2 * c.amplitude_px + 1;
          if (mini.childElementCount !== side * side) {
            mini.innerHTML = '';
            mini.style.gridTemplateColumns = `repeat(${side}, 0.6rem)`;
            for (let i = 0; i < side * side; i++) mini.append(h('i', { cls: i === (side * side - 1) / 2 ? 'now' : null }));
          }
          setText(click, `${side * side} posizioni · sfasamento medio dei clic ~${meanClickOffset(c.amplitude_px).toFixed(1).replace('.', ',')} px`);
          dwell.set(c.dwell_min_s, c.dwell_max_s);
          trans.set(c.transition_min_s, c.transition_max_s);
          vigToggle.set(c.vignette_enabled);
          editor.set(c);
          dimToggle.set(c.dim_enabled);
          strength.set(c.dim_strength_pct);
          delay.set(c.dim_delay_s / 60);
          ramp.set(c.dim_ramp_s / 60);
          winMode.set(c.dim_window_mode);
          activate.set(c.dim_activate);
          largeToggle.set(c.dim_large_enabled);
          largeDelay.set(c.dim_large_delay_s / 60);
          largeStrength.set(c.dim_large_strength_pct);
          const video = c.dim_window_mode === 'video';
          rowsLarge.forEach((r) => r.classList.toggle('off', !video));
          setText(activateHint, c.dim_activate === 'never'
            ? 'Nessuna finestra torna normale da sola: tutte si scuriscono allo stesso modo.'
            : 'La finestra che usi resta normale; quando la lasci, il conteggio riparte da zero.');
        }
        if (store.dim !== lastDim) {
          lastDim = store.dim;
          drawDimMap(cv);
          const d = store.dim;
          setText(status, d ? `${dimStatusText()}${d.cells ? ` · ${fmtPct(d.dimmed_pct, 1)} dello schermo` : ''}` : '');
        }
        view.tick();
      },
      tick() {
        const left = highlightLeft();
        highBtn.disabled = !canHighlight();
        highBtn.classList.toggle('on', left > 0);
        setText(highBtn.lastChild, left > 0 ? `Evidenziate · ${Math.ceil(left / 1000)} s` : 'Evidenzia sullo schermo · 15 s');
      },
    };
    return view;
  },
};

// ------------------------------------------------------------------ monitor

PAGES.monitor = {
  build(root) {
    // Spenta la scelta automatica, resta fisso lo schermo protetto adesso.
    const autoToggle = toggle((v) => {
      const p = store.monitors && store.monitors.monitors.find((x) => x.protected);
      if (v) send({ cmd: 'setMonitor', device: 'auto' });
      else if (p) send({ cmd: 'setMonitor', device: p.device });
    });
    const name = h('b.accent');
    const countLabel = h('span.muted');
    const head = h('section.panel', null, h('div.mon-head', null,
      h('b', null, 'Protezione su ', name), countLabel, h('span.grow'), h('span.muted', { text: 'Scelta automatica' }), autoToggle.el));
    const list = h('div.stack');
    root.append(h('div.page', null, head, list));

    const sendBrightness = throttledConfig('brightness_pct');
    let lastMonitors = null, bright = null;
    const build = () => {
      const m = store.monitors;
      list.innerHTML = '';
      bright = null;
      if (!m || !m.monitors.length) { list.append(h('div.empty', { text: 'Nessuno schermo trovato.' })); return; }
      const protectedName = (m.monitors.find((x) => x.protected) || {}).name;
      m.monitors.forEach((mon, i) => {
        const tg = toggle((v) => {
          if (v) send({ cmd: 'setMonitor', device: mon.device });
          else toast('Per cambiare schermo accendi l\'interruttore di un altro, oppure la scelta automatica.');
        });
        tg.set(mon.protected);
        const pills = h('div.pills', null,
          mon.primary ? pill('Primary', 'accent') : null,
          pill(mon.hdr ? 'HDR' : 'SDR', mon.hdr ? 'info' : null),
          mon.connection ? pill(mon.connection) : null);
        const top = h('div.mon-top', null, tg.el,
          h('div.grow', null, h('h4', { text: `Schermo ${i + 1}` }), h('p', { text: [mon.name, `${mon.width} × ${mon.height}`, mon.refresh_hz ? `${mon.refresh_hz} Hz` : null].filter(Boolean).join(' · ') })),
          pills);
        let right;
        if (mon.protected) {
          bright = slider({ min: 20, max: 100, step: 1, format: (v) => `${v}%`, onInput: sendBrightness });
          right = h('div', null,
            settingRow('Luminosità', 'Scurisce l\'immagine via software. Funziona mentre la protezione è attiva.', bright.el, { wide: true }),
            h('p.hint.box', { text: 'Contrasto e nitidezza non sono disponibili: la TV non accetta questi comandi dal PC (DDC/CI).' }));
        } else {
          right = h('p.hint', { text: `Non protetto. Accendi l'interruttore per proteggere questo schermo al posto di ${protectedName || 'quello attuale'}.` });
        }
        list.append(h('section.panel.mon', { cls: mon.protected ? null : 'off' }, top,
          h('div.mon-body', null, h('div.mon-thumb', { text: `${mon.width} × ${mon.height}` }), right)));
      });
    };
    const view = {
      update() {
        const m = store.monitors, c = cfg();
        if (m !== lastMonitors) {
          lastMonitors = m;
          build();
        }
        if (m) {
          const p = m.monitors.find((x) => x.protected);
          setText(name, p ? p.name : 'nessuno schermo');
          setText(countLabel, `${m.monitors.length} ${m.monitors.length === 1 ? 'schermo collegato' : 'schermi collegati'}`);
          autoToggle.set(m.setting === 'auto');
        }
        if (bright && c) bright.set(c.brightness_pct);
      },
    };
    return view;
  },
};

// ------------------------------------------------------------------ statistiche

// Scala della mappa di usura: blu scuro -> verde acqua -> verde -> ambra -> rosso.
const WEAR_SCALE = [[0, [14, 26, 36]], [0.3, [23, 52, 74]], [0.5, [31, 111, 106]], [0.7, [79, 157, 93]], [0.85, [211, 154, 69]], [1, [224, 96, 91]]];
function wearColor(t) {
  const v = Math.min(1, Math.max(0, t));
  for (let i = 1; i < WEAR_SCALE.length; i++) {
    const [p1, c1] = WEAR_SCALE[i], [p0, c0] = WEAR_SCALE[i - 1];
    if (v <= p1) {
      const k = (v - p0) / (p1 - p0);
      return `rgb(${c0.map((c, j) => Math.round(c + (c1[j] - c) * k)).join(',')})`;
    }
  }
  return 'rgb(224,96,91)';
}

const CONTENT_KINDS = [
  ['Interfaccia ferma e lavoro', 'var(--accent)'],
  ['Video e animazioni', 'var(--info)'],
  ['Giochi e schermo intero', '#b28cff'],
  ['Assente', '#5fdc8f'],
  ['Pausa manuale', 'var(--warn)'],
];
const RANGES = { '6h': 72, '24h': 288, '7g': 2016 };

// Grafico del rischio: area ambra, griglia 25/50/75%, vuoti dove non ci sono misure.
function drawRiskChart(box, values, endLocalS, bucketS) {
  const W = 600, H = 170, top = 8, bottom = 22, left = 30;
  const n = values.length;
  const x = (i) => left + (n <= 1 ? 0 : (i / (n - 1)) * (W - left - 4));
  const y = (v) => top + (1 - Math.min(1, v)) * (H - top - bottom);
  let grid = '';
  for (const g of [0.25, 0.5, 0.75]) {
    grid += `<line class="grid" x1="${left}" x2="${W}" y1="${y(g)}" y2="${y(g)}"/><text class="axis" x="0" y="${y(g) + 4}">${g * 100}%</text>`;
  }
  let line = '', area = '', run = [];
  const flush = () => {
    if (run.length) {
      const pts = run.map(([i, v]) => `${x(i).toFixed(1)},${y(v).toFixed(1)}`);
      line += `M${pts.join('L')}`;
      area += `M${x(run[0][0]).toFixed(1)},${y(0)}L${pts.join('L')}L${x(run[run.length - 1][0]).toFixed(1)},${y(0)}Z`;
    }
    run = [];
  };
  values.forEach((v, i) => { if (v === null) flush(); else run.push([i, v]); });
  flush();
  // Etichette: ora locale (il secondo "locale" trattato come UTC).
  let labels = '';
  for (let k = 0; k <= 4; k++) {
    const i = Math.round((k / 4) * (n - 1));
    const d = new Date((endLocalS - (n - 1 - i) * bucketS) * 1000);
    const text = k === 4 ? 'ora' : (bucketS >= 3600
      ? d.toLocaleDateString('it-IT', { weekday: 'short', timeZone: 'UTC' })
      : `${pad2(d.getUTCHours())}:${pad2(d.getUTCMinutes())}`);
    labels += `<text class="axis" x="${Math.min(W - 24, Math.max(left, x(i) - 14))}" y="${H - 4}">${text}</text>`;
  }
  box.innerHTML = `<svg viewBox="0 0 ${W} ${H}" preserveAspectRatio="none">${grid}<path class="area" d="${area}"/><path class="line" d="${line}"/>${labels}</svg>`;
}

PAGES.statistiche = {
  build(root) {
    const stat = (label) => { const b = h('b', { text: '-' }), sp = h('span'); return [h('div.stat', null, h('small', { text: label }), b, sp), b, sp]; };
    const [sTot, vTot, nTot] = stat('Protetto in totale');
    const [sSes, vSes, nSes] = stat('Sessioni');
    const [sAvo, vAvo, nAvo] = stat('Usura evitata oggi');
    const [sStr, vStr, nStr] = stat('Giorni di fila');
    vTot.className = 'accent';
    vSes.className = 'info';
    vAvo.className = 'accent';
    vStr.className = 'warn';

    // Mappa di usura
    const mapSub = h('span.sub');
    const wear = panel('Mappa di usura del pannello');
    wear.el.querySelector('.panel-head').insertBefore(mapSub, wear.el.querySelector('.panel-head .grow'));
    wear.body.classList.add('pad');
    const grid = h('div.wear-grid');
    const cells = [];
    for (let i = 0; i < 576; i++) { const c = h('i'); cells.push(c); grid.append(c); }
    const legend = h('div.legend', null, h('span', { text: 'sinistra' }), h('span.grow'), h('span', { text: 'meno' }),
      ...[0.1, 0.35, 0.6, 0.78, 0.95].map((t) => { const i = h('i'); i.style.background = wearColor(t); return i; }),
      h('span', { text: 'più' }), h('span.grow'), h('span', { text: 'destra' }));
    const wkv = (label) => { const b = h('b', { text: '-' }), s = h('span'); return [h('div.wear-stat', null, h('small', { text: label }), b, s), b, s]; };
    const [w1, w1b, w1s] = wkv('Misurato');
    const [w2, w2b, w2s] = wkv('Zona peggiore');
    const [w3, w3b, w3s] = wkv('Concentrazione');
    const [w4, w4b, w4s] = wkv('Zone oltre il doppio');
    const wearEmpty = h('div.empty', { text: 'Nessuna misura ancora: la mappa si riempie mentre la protezione è attiva.' });
    wear.body.append(grid, legend, h('div.wear-stats', null, w1, w2, w3, w4), wearEmpty,
      h('p.hint', { text: 'Luce arrivata al pannello × tempo, dopo oscuramento, luminosità e bordi. Il tempo in pausa (giochi, video a tutto schermo) non è misurato.' }));

    // Grafico del rischio
    let range = '24h';
    const rangeSeg = segmented([['6h', '6 h'], ['24h', '24 h'], ['7g', '7 g']], (v) => { range = v; lastHistory = null; view.update(); });
    const chartPanel = panel('Rischio nel tempo', null, rangeSeg.el);
    chartPanel.body.classList.add('pad');
    const chartInfo = h('div.mono.muted.chart-info');
    const chart = h('div.chart');
    chartPanel.body.append(chartInfo, chart);

    // App piu' usate
    const appsPanel = panel('App più usate', '7 giorni');
    const appsList = h('div.top-apps');
    appsPanel.body.append(appsList);

    // Contenuti
    const contentPanel = panel('Usura per contenuto', '7 giorni');
    contentPanel.body.classList.add('pad');
    const contentBar = h('div.content-bar');
    const contentLegend = h('div.content-legend');
    contentPanel.body.append(contentBar, contentLegend);

    // Parti del progetto 1: posizioni dello spostamento e pause di oggi.
    const heatPanel = panel('Posizioni dello spostamento', 'tempo su ogni posizione · totale');
    heatPanel.body.classList.add('pad');
    const heat = h('div.heat');
    const heatCells = [];
    for (let i = 0; i < 49; i++) { const b = h('b'); const c = h('i', null, b); heatCells.push([c, b]); heat.append(c); }
    const uniform = h('p.hint');
    heatPanel.body.append(heat, uniform);
    const pausePanel = panel('Pause di oggi', 'per motivo');
    const bars = h('div.bars');
    pausePanel.body.append(bars);

    root.append(h('div.page', null, h('div.stats4', null, sTot, sSes, sAvo, sStr),
      h('div.cols.even', null, wear.el, chartPanel.el),
      h('div.cols.even', null, appsPanel.el, contentPanel.el),
      h('div.cols.even', null, heatPanel.el, pausePanel.el)));

    let lastHistory = null, lastStats = null;
    const view = {
      update() {
        const hist = store.history, st = store.stats, c = cfg(), s = store.state;
        if (st) {
          setText(vTot, fmtHours(st.total_ms));
          setText(nTot, hist ? `+${fmtHours(hist.protected_week_s * 1000)} questa settimana` : 'con l\'overlay a schermo');
        }
        if (hist && hist !== lastHistory) {
          lastHistory = hist;
          setText(vSes, String(hist.sessions_total));
          setText(nSes, `+${hist.sessions_week} questa settimana`);
          setText(vAvo, hist.avoided_today === null ? 'n/d' : `${Math.round(hist.avoided_today * 100)}%`);
          setText(nAvo, hist.avoided_total === null ? 'luce tolta alle zone ferme' : `totale ${Math.round(hist.avoided_total * 100)}%`);
          setText(vStr, `${hist.streak} ${hist.streak === 1 ? 'giorno' : 'giorni'}`);
          setText(nStr, `record ${hist.streak_best}`);

          const w = hist.wear;
          setText(mapSub, w.monitor || '');
          const has = w.hottest >= 0;
          grid.hidden = !has;
          wearEmpty.hidden = has;
          cells.forEach((cell, i) => {
            cell.style.background = has ? wearColor(w.cells[i]) : 'transparent';
            cell.classList.toggle('hot', has && i === w.hottest);
          });
          const since = has ? new Date(w.since * 1000).toLocaleDateString('it-IT', { day: '2-digit', month: '2-digit', timeZone: 'UTC' }) : '-';
          setText(w1b, has ? `${Math.round(w.measured_hours)} h` : '-');
          setText(w1s, has ? `dal ${since}` : '');
          setText(w2b, has ? t(w.hottest_key) : '-');
          setText(w2s, has ? `${w.hottest_hours.toFixed(1).replace('.', ',')} h a bianco pieno` : '');
          setText(w3b, has ? `${w.concentration.toFixed(2).replace('.', ',')}×` : '-');
          setText(w3s, 'zona peggiore / media');
          setText(w4b, has ? `${w.over_double}/576` : '-');
          setText(w4s, has ? `${fmtPct((w.over_double / 576) * 100, 1)} del pannello` : '');

          const n = RANGES[range];
          let vals = hist.timeline.slice(-n);
          let bucketS = 300;
          if (range === '7g') {  // a ore
            const hourly = [];
            for (let i = 0; i < vals.length; i += 12) {
              const part = vals.slice(i, i + 12).filter((v) => v !== null);
              hourly.push(part.length ? part.reduce((a, b) => a + b, 0) / part.length : null);
            }
            vals = hourly;
            bucketS = 3600;
          }
          rangeSeg.set(range);
          drawRiskChart(chart, vals, hist.timeline_end, bucketS);
          const m = vals.filter((v) => v !== null);
          const cur = [...vals].reverse().find((v) => v !== null);
          setText(chartInfo, m.length
            ? `picco ${Math.round(Math.max(...m) * 100)}% · media ${Math.round((m.reduce((a, b) => a + b, 0) / m.length) * 100)}% · attuale ${cur === undefined ? '-' : Math.round(cur * 100) + '%'}`
            : 'nessuna misura in questo intervallo');

          appsList.innerHTML = '';
          if (!hist.top_apps.length) appsList.append(h('div.empty', { text: 'Ancora nessuna app registrata.' }));
          const topTotal = Math.max(1, ...hist.top_apps.map((a) => a.protected_s + a.paused_s));
          for (const a of hist.top_apps) {
            const info = RULE_INFO[a.rule] || RULE_INFO.auto;
            const prot = h('i.prot'), paus = h('i.paus');
            prot.style.width = `${(a.protected_s / topTotal) * 100}%`;
            paus.style.width = `${(a.paused_s / topTotal) * 100}%`;
            appsList.append(h('div.top-app', null, avatar(a.exe),
              h('div.grow', null, h('div.top-app-name', null, h('b', { text: a.exe }), pill(info[2], info[3])), h('div.split-bar', null, prot, paus)),
              h('span.mono', { text: fmtClock((a.protected_s + a.paused_s) * 1000) })));
          }
          if (hist.top_apps.length) appsList.append(h('div.legend.left', null, h('i.prot'), h('span', { text: 'protetto' }), h('i.paus'), h('span', { text: 'in pausa' })));

          contentBar.innerHTML = '';
          contentLegend.innerHTML = '';
          const total = hist.content.reduce((a, b) => a + b, 0);
          if (total <= 0) contentLegend.append(h('div.empty', { text: 'Ancora nessun dato.' }));
          hist.content.forEach((share, k) => {
            const [label, color] = CONTENT_KINDS[k];
            if (share > 0) { const seg = h('i'); seg.style.width = `${share * 100}%`; seg.style.background = color; contentBar.append(seg); }
            if (total > 0) {
              const dot = h('i');
              dot.style.background = color;
              contentLegend.append(h('div', null, dot, h('span', { text: label }), h('b', { text: `${Math.round(share * 100)}%` })));
            }
          });
        }
        if (st && st !== lastStats) {
          lastStats = st;
          const a = (c && c.amplitude_px) || 2;
          let max = 0;
          for (let y = -a; y <= a; y++) for (let x = -a; x <= a; x++) max = Math.max(max, st.cells[(y + 3) * 7 + x + 3]);
          heatCells.forEach(([cell, fill], i) => {
            const x = i % 7 - 3, y = Math.floor(i / 7) - 3;
            const inside = Math.abs(x) <= a && Math.abs(y) <= a;
            cell.classList.toggle('out', !inside);
            fill.style.opacity = inside && max > 0 ? (0.1 + 0.9 * st.cells[i] / max).toFixed(2) : '0';
          });
          setText(uniform, st.uniformity === null ? 'Uniformità: servono 10 minuti di dati.' : `Uniformità ${fmtPct(st.uniformity, 1)}: 100% = usura identica su tutte le posizioni.`);
          bars.innerHTML = '';
          const entries = Object.entries(st.pauses_by_reason || {}).sort((x, y) => y[1] - x[1]);
          if (!entries.length) bars.append(h('div.empty', { text: 'Nessuna pausa oggi.' }));
          const top = entries.length ? entries[0][1] : 1;
          for (const [reason, n] of entries) {
            const fill = h('i');
            fill.style.width = `${(n / top) * 100}%`;
            bars.append(h('div.bar-row', null, h('span', { text: t(reason) }), h('b', { text: String(n) }), h('div.bar', null, fill)));
          }
        }
        const now = s && !s.paused ? (s.offset[1] + 3) * 7 + s.offset[0] + 3 : -1;
        heatCells.forEach(([cell], i) => cell.classList.toggle('now', i === now));
      },
    };
    return view;
  },
};

// ------------------------------------------------------------------ profili app

const RULE_INFO = {
  always: ['Metti in pausa', 'La protezione si ferma quando l\'app è in primo piano', 'Sempre pausa', 'warn'],
  auto: ['Automatico', 'Pausa solo se è a schermo intero o un gioco', 'Automatico', 'accent'],
  never: ['Non mettere mai in pausa', 'Protezione attiva anche a schermo intero', 'Mai pausa', 'info'],
};
const RULE_EXPLAIN = {
  always: 'Quando quest\'app è in primo piano la protezione si mette in pausa, anche se è in finestra.',
  auto: 'La protezione si mette in pausa solo se quest\'app va a schermo intero o è un gioco DirectX esclusivo.',
  never: 'La protezione resta attiva anche quando quest\'app è a schermo intero (utile per video e browser).',
};

PAGES.profili = {
  build(root) {
    let selected = null, pendingSelect = null, filter = '';
    // Elenco
    const search = h('input', { placeholder: 'Cerca un\'app...', spellcheck: 'false' });
    search.addEventListener('input', () => { filter = search.value.trim().toLowerCase(); renderList(); });
    const listPanel = panel('Profili app');
    listPanel.el.querySelector('.panel-head').append(h('label.field', null, icon('search'), search));
    const count = h('span.sub');
    listPanel.el.querySelector('.panel-head').insertBefore(count, listPanel.el.querySelector('.panel-head .grow'));
    const list = h('div.applist');
    listPanel.body.style.padding = '0';
    listPanel.body.append(list);

    // Aggiungi
    const addInput = h('input', { placeholder: 'nome dell\'app, es. gioco.exe', maxlength: '260', spellcheck: 'false' });
    const procBox = h('div.proc', { hidden: true });
    const addTyped = () => {
      let exe = addInput.value.trim().toLowerCase();
      if (!exe) return;
      if (!/\.exe$/.test(exe)) exe += '.exe';
      pendingSelect = exe;
      send({ cmd: 'addApp', exe });
      addInput.value = '';
      procBox.hidden = true;
    };
    addInput.addEventListener('keydown', (e) => { if (e.key === 'Enter') addTyped(); });
    const addPanel = panel('Aggiungi un\'app');
    addPanel.body.classList.add('pad');
    addPanel.body.append(h('div.add-row', null, h('label.field', null, icon('plus'), addInput),
      h('button.btn', { type: 'button', on: { click: () => { if (!procBox.hidden) { procBox.hidden = true; return; } send({ cmd: 'listProcesses' }); } } }, icon('list'), 'App aperte'),
      h('button.btn', { type: 'button', on: { click: () => send({ cmd: 'browseExe' }) } }, icon('folder'), 'Sfoglia...'),
      h('button.btn.solid', { type: 'button', on: { click: addTyped } }, icon('plus'), 'Aggiungi')), procBox);

    // Pause automatiche globali
    const autoNotif = toggle((v) => setConfig({ pause_on_notification_state: v }));
    const autoFull = toggle((v) => setConfig({ pause_on_fullscreen_window: v }));
    const autoPanel = panel('Pausa automatica', 'per tutte le app');
    autoPanel.body.append(
      settingRow('Giochi e presentazioni', 'Giochi DirectX a schermo intero, presentazioni, schermo assente.', autoNotif.el),
      settingRow('Qualsiasi finestra a tutto schermo', 'Borderless, video, browser a tutto schermo.', autoFull.el));

    // Modifica
    const editPanel = panel('Modifica');
    const delBtn = h('button.btn.small.danger', { type: 'button', on: { click: () => { if (selected) { send({ cmd: 'removeApp', exe: selected }); selected = null; } } } }, icon('trash'), 'Elimina');
    editPanel.el.querySelector('.panel-head').append(delBtn);
    editPanel.body.classList.add('pad');
    const editName = h('div.edit-name');
    const editInfo = h('p.hint');
    const rules = choiceCards(Object.entries(RULE_INFO).map(([k, v]) => [k, v[0], v[1]]), (rule) => selected && send({ cmd: 'setAppRule', exe: selected, rule }));
    const explain = h('p.hint.box');
    const dimEx = toggle((v) => selected && send({ cmd: 'setDimExclude', exe: selected, on: v }));
    const editBody = h('div', null,
      h('div.edit-title', { text: 'App' }), editName, editInfo,
      h('div.edit-title', { text: 'Quando quest\'app è in primo piano' }), rules.el, explain,
      h('div.edit-title', { text: 'Oscuramento' }),
      settingRow('Non oscurare mentre è in primo piano', 'Sospende l\'oscuramento delle zone ferme (utile per app di grafica).', dimEx.el));
    const editEmpty = h('div.empty', { text: 'Scegli un\'app dall\'elenco per modificarla.' });
    editPanel.body.append(editEmpty, editBody);

    root.append(h('div.page', null, h('div.cols', null,
      h('div.stack', null, listPanel.el, addPanel.el, autoPanel.el),
      h('div.stack', null, editPanel.el))));

    const apps = () => store.apps || [];
    function renderList() {
      list.innerHTML = '';
      const shown = apps().filter((a) => !filter || a.exe.includes(filter));
      setText(count, `${apps().length}`);
      if (!shown.length) list.append(h('div.empty', { text: filter ? 'Nessuna app corrisponde alla ricerca.' : 'Nessuna app ancora: aggiungine una qui sotto.' }));
      for (const a of shown) {
        const info = RULE_INFO[a.rule] || RULE_INFO.auto;
        const trash = h('button.trash', { type: 'button', title: 'Elimina', on: { click: (e) => { e.stopPropagation(); if (selected === a.exe) selected = null; send({ cmd: 'removeApp', exe: a.exe }); } } }, icon('trash'));
        const row = h('button.app', { type: 'button', cls: a.exe === selected ? 'on' : null, on: { click: () => { selected = a.exe; renderList(); renderEdit(); } } },
          avatar(a.exe),
          h('span.name', null, h('b', { text: a.exe }), h('small', { text: a.last_seen_s ? `ultima pausa · ${fmtWhen(a.last_seen_s * 1000)} · ${a.count} ${a.count === 1 ? 'volta' : 'volte'}` : 'aggiunta a mano' })),
          h('span.tags', null, pill(info[2], info[3]), a.dim_exclude ? pill('Non oscurare') : null),
          trash);
        list.append(row);
      }
    }
    function renderEdit() {
      const a = apps().find((x) => x.exe === selected);
      editEmpty.hidden = !!a;
      editBody.hidden = !a;
      delBtn.hidden = !a;
      if (!a) return;
      editName.innerHTML = '';
      editName.append(avatar(a.exe), h('span', { text: a.exe }));
      setText(editInfo, a.reason_key ? `Ultima pausa: ${t(a.reason_key).toLowerCase()} · ${fmtWhen(a.last_seen_s * 1000)}` : 'Aggiunta a mano: non ha ancora messo in pausa la protezione.');
      rules.set(a.rule);
      setText(explain, RULE_EXPLAIN[a.rule] || '');
      dimEx.set(a.dim_exclude);
    }

    const offProcesses = on('processes', (msg) => {
      procBox.innerHTML = '';
      for (const p of msg.processes) procBox.append(h('button', { type: 'button', text: p, on: { click: () => { addInput.value = p; procBox.hidden = true; addInput.focus(); } } }));
      procBox.hidden = msg.processes.length === 0;
    });
    const offBrowsed = on('browsed', (msg) => { addInput.value = msg.exe; addInput.focus(); });

    let lastApps = null;
    return {
      leave() { offProcesses(); offBrowsed(); },
      update() {
        const c = cfg();
        if (c) { autoNotif.set(c.pause_on_notification_state); autoFull.set(c.pause_on_fullscreen_window); }
        if (store.apps !== lastApps) {
          lastApps = store.apps;
          if (pendingSelect && apps().some((a) => a.exe === pendingSelect)) { selected = pendingSelect; pendingSelect = null; }
          if (selected && !apps().some((a) => a.exe === selected)) selected = null;
          renderList();
          renderEdit();
        }
      },
    };
  },
};

// ------------------------------------------------------------------ tasti rapidi

const KEY_NAMES = { ' ': 'Space', Pause: 'Pause', Insert: 'Insert', Delete: 'Delete', Home: 'Home', End: 'End', PageUp: 'PageUp', PageDown: 'PageDown' };
function keyName(e) {
  if (/^F([1-9]|1[0-9]|2[0-4])$/.test(e.key)) return e.key;
  if (KEY_NAMES[e.key]) return KEY_NAMES[e.key];
  if (/^Key[A-Z]$/.test(e.code)) return e.code.slice(3);
  if (/^Digit[0-9]$/.test(e.code)) return e.code.slice(5);
  return null;
}

PAGES.tasti = {
  build(root) {
    const HINT = 'Clicca una combinazione e premi i tasti nuovi (Esc per annullare).';
    const hint = h('p.hint.box', { text: HINT });
    let capturing = null;
    const keys = {};
    const p = panel('Tasti rapidi', 'validi in tutto Windows');
    const defs = [
      ['hotkey_toggle', 'Pausa / riprendi', 'Ferma o riavvia la protezione.'],
      ['hotkey_display_off', 'Spegni schermo ora', 'La TV va in standby; si riaccende muovendo il mouse.'],
      ['hotkey_emergency_exit', 'Uscita di emergenza', 'Chiude subito OLED Guard. Funziona anche se l\'interfaccia è bloccata.'],
    ];
    const show = (key) => setText(keys[key], cfg() ? cfg()[key].replace(/\+/g, ' + ') : '-');
    function stop(restore) {
      if (!capturing) return;
      keys[capturing].classList.remove('rec');
      if (restore) show(capturing);
      capturing = null;
      send({ cmd: 'hotkeyCapture', on: false });
    }
    for (const [key, title, desc] of defs) {
      keys[key] = h('button.key', { type: 'button', on: { click: () => {
        stop(true);
        capturing = key;
        keys[key].classList.add('rec');
        setText(keys[key], 'premi la combinazione...');
        send({ cmd: 'hotkeyCapture', on: true });
      } } });
      p.body.append(settingRow(title, desc, keys[key]));
    }
    root.append(h('div.page', null, p.el, hint));
    const onKey = (e) => {
      if (!capturing) return;
      e.preventDefault();
      if (e.key === 'Escape') { stop(true); return; }
      const name = keyName(e);
      if (!name) return;  // solo modificatori finora
      const mods = [];
      if (e.ctrlKey) mods.push('Ctrl');
      if (e.altKey) mods.push('Alt');
      if (e.shiftKey) mods.push('Shift');
      if (e.metaKey) mods.push('Win');
      if (!mods.length) { setText(hint, 'Serve almeno un tasto fra Ctrl, Alt, Shift e Win.'); return; }
      const combo = [...mods, name].join('+');
      const key = capturing;
      setText(keys[key], combo.replace(/\+/g, ' + '));
      stop(false);
      setText(hint, HINT);
      setConfig({ [key]: combo });
    };
    // Se la finestra perde il focus o si cambia pagina, le hotkey globali non devono restare sospese.
    const onBlur = () => stop(true);
    window.addEventListener('keydown', onKey);
    window.addEventListener('blur', onBlur);
    return {
      update() { for (const [key] of defs) if (key !== capturing) show(key); },
      leave() { stop(true); window.removeEventListener('keydown', onKey); window.removeEventListener('blur', onBlur); },
    };
  },
};

// ------------------------------------------------------------------ impostazioni

PAGES.impostazioni = {
  build(root) {
    const autostart = toggle((v) => setConfig({ autostart: v }));
    const start = panel('Avvio', 'con Windows');
    start.el.classList.add('accented');
    start.body.append(settingRow('Avvia con Windows', 'OLED Guard parte da solo all\'accesso e resta nella barra delle applicazioni.', autostart.el));

    const awayToggle = toggle((v) => setConfig({ away_enabled: v }));
    const awayAfter = slider({ min: 1, max: 60, step: 1, format: (v) => `${v} min`, onInput: throttledConfig('away_after_min') });
    const awayLevel = slider({ min: 10, max: 80, step: 1, format: (v) => `${v}%`, onInput: throttledConfig('away_brightness_pct') });
    const awayRows = [
      settingRow('Dopo quanto tempo', 'Minuti senza mouse e tastiera, con lo schermo fermo.', awayAfter.el, { wide: true }),
      settingRow('Scurisci fino a', 'Luminosità finale; la discesa dura 10 secondi.', awayLevel.el, { wide: true }),
    ];
    const away = panel('Modalità assente', 'quando non sei al PC');
    away.el.classList.add('accented');
    away.body.append(settingRow('Scurisci quando non ci sei', 'Torna normale subito, al primo movimento. Un video in riproduzione non la fa scattare.', awayToggle.el), ...awayRows);

    const swatches = h('div.swatches');
    const swatchEls = {};
    for (const [name, color] of Object.entries(ACCENTS)) {
      const i = h('i');
      i.style.background = color;
      swatchEls[name] = h('button.swatch', { type: 'button', title: name, on: { click: () => setConfig({ ui_accent: name }) } }, i,
        h('small', { text: `${contrastRatio(color, PANEL_COLOR).toFixed(1).replace('.', ',')}:1` }));
      swatches.append(swatchEls[name]);
    }
    const size = segmented([['small', 'Piccolo'], ['normal', 'Normale'], ['large', 'Grande'], ['xl', 'XL']], (v) => setConfig({ ui_text_size: v }));
    const look = panel('Aspetto', 'colore d\'accento · dimensione del testo');
    look.el.classList.add('accented');
    look.body.append(
      settingRow('Colore d\'accento', 'Interruttori, selezioni e stato. Il numero è il contrasto sul fondo dei pannelli.', swatches, { wide: true }),
      settingRow('Dimensione del testo', 'Ingrandisce testi, spazi e icone insieme.', size.el, { wide: true }));

    const level = segmented([['error', 'Errori'], ['warn', 'Avvisi'], ['info', 'Normale'], ['debug', 'Dettagliato']], (v) => setConfig({ log_level: v }));
    const cfgPath = h('div.path'), logPath = h('div.path');
    const diag = panel('Diagnostica', 'log e file');
    diag.el.classList.add('accented');
    diag.body.append(
      settingRow('Dettaglio del log', 'Il livello Dettagliato serve solo per cercare un problema.', level.el, { wide: true }),
      settingRow('Cartella dei log', logPath, h('button.btn', { type: 'button', on: { click: () => send({ cmd: 'openLogFolder' }) } }, icon('folder'), 'Apri')),
      settingRow('File di configurazione', cfgPath, h('span')));

    // Notifiche: avviso per il rischio non protetto.
    const alertToggle = toggle((v) => setConfig({ alert_enabled: v }));
    const alertLevel = segmented([['low', 'Bassa · 10%'], ['medium', 'Media · 20%'], ['high', 'Alta · 35%']], (v) => setConfig({ alert_threshold: v }));
    const alertAfter = slider({ min: 5, max: 240, step: 5, format: (v) => `${v} min`, onInput: throttledConfig('alert_after_min') });
    const alertRows = [
      settingRow('Soglia', 'Quanta parte dello schermo ferma, luminosa e non scurita fa partire il conteggio.', alertLevel.el, { wide: true }),
      settingRow('Avvisa dopo', 'Minuti di fila sopra la soglia. Al massimo un avviso all\'ora.', alertAfter.el, { wide: true }),
    ];
    const notif = panel('Notifiche', 'quando avvisarti');
    notif.el.classList.add('accented');
    notif.body.append(settingRow('Avviso nella barra delle applicazioni', 'Solo per le zone ferme e luminose che la protezione non sta già scurendo.', alertToggle.el), ...alertRows);

    // Privacy: statistiche per app e storico.
    const appStats = toggle((v) => setConfig({ app_stats_enabled: v }));
    let confirmUntil = 0, confirmTimer = 0;
    const clearBtn = h('button.btn.danger', { type: 'button' }, icon('trash'), h('span', { text: 'Cancella lo storico' }));
    // Serve un secondo clic entro 5 s: un clic per sbaglio non cancella nulla.
    clearBtn.addEventListener('click', () => {
      if (performance.now() < confirmUntil) {
        confirmUntil = 0;
        clearTimeout(confirmTimer);
        setText(clearBtn.lastChild, 'Cancella lo storico');
        send({ cmd: 'clearHistory' });
        return;
      }
      confirmUntil = performance.now() + 5000;
      setText(clearBtn.lastChild, 'Conferma: cancella');
      clearTimeout(confirmTimer);
      confirmTimer = setTimeout(() => { confirmUntil = 0; setText(clearBtn.lastChild, 'Cancella lo storico'); }, 5000);
    });
    const privacy = panel('Privacy', 'cosa resta sul PC');
    privacy.el.classList.add('accented');
    privacy.body.append(
      settingRow('Statistiche per app', 'Tempo in primo piano di ogni app, per la pagina Statistiche. Restano sul PC.', appStats.el),
      settingRow('Cancella lo storico', 'Azzera grafico, mappa di usura, app e giorni di fila (history.json).', clearBtn));

    root.append(h('div.page', null, h('div.cols.even', null, h('div.stack', null, start.el, away.el, notif.el), h('div.stack', null, look.el, privacy.el, diag.el))));
    return {
      leave() { clearTimeout(confirmTimer); },
      update() {
        const c = cfg();
        if (c) {
          alertToggle.set(c.alert_enabled);
          alertLevel.set(c.alert_threshold);
          alertAfter.set(c.alert_after_min);
          alertRows.forEach((r) => r.classList.toggle('off', !c.alert_enabled));
          appStats.set(c.app_stats_enabled);
          autostart.set(c.autostart);
          awayToggle.set(c.away_enabled);
          awayAfter.set(c.away_after_min);
          awayLevel.set(c.away_brightness_pct);
          awayRows.forEach((r) => r.classList.toggle('off', !c.away_enabled));
          Object.entries(swatchEls).forEach(([name, el]) => el.classList.toggle('on', name === c.ui_accent));
          size.set(c.ui_text_size);
          level.set(c.log_level);
        }
        if (store.about) { setText(cfgPath, store.about.config_path); setText(logPath, store.about.log_dir); }
      },
    };
  },
};

// ------------------------------------------------------------------ windows per oled

PAGES.windows = {
  build(root) {
    const score = h('b', { text: '-' });
    const applyAll = h('button.btn.solid', { type: 'button', on: { click: () => send({ cmd: 'windowsApplyAll' }) } }, icon('check'), 'Applica tutti i consigli');
    const restore = h('button.btn', { type: 'button', on: { click: () => send({ cmd: 'windowsRestoreAll' }) } }, icon('reset'), 'Ripristina come prima');
    const head = h('section.panel', null, h('div.score', null, score, h('span', { text: 'Impostazioni consigliate attive' }), h('span.grow'), applyAll, restore));
    const list = panel('Impostazioni di Windows per un OLED');
    const off = h('button.btn', { type: 'button', on: { click: () => send({ cmd: 'displayOff' }) } }, icon('power'), 'Spegni schermo');
    const offPanel = panel('Schermo');
    offPanel.body.append(settingRow('Spegni lo schermo adesso', 'La TV va in standby; si riaccende muovendo il mouse.', off));
    root.append(h('div.page', null, head, list.el, offPanel.el,
      h('p.hint', { text: 'Ogni modifica parte solo da un tuo clic. Il valore originale viene salvato e "Ripristina come prima" lo rimette.' })));
    let last = null;
    return {
      update() {
        const w = store.windows;
        if (!w || w === last) return;
        last = w;
        setText(score, `${w.score}/${w.total}`);
        restore.disabled = !w.items.some((i) => i.restorable);
        applyAll.disabled = w.items.every((i) => i.ok || i.managed);
        list.body.innerHTML = '';
        for (const it of w.items) {
          let action;
          if (it.managed) action = pill('Gestito dal sistema');
          else if (it.ok) action = pill('Fatto', 'accent');
          else action = h('button.btn.small', { type: 'button', title: 'Applica il valore consigliato', on: { click: () => send({ cmd: 'windowsApply', id: it.id }) } }, t(it.recommended_key, it.recommended_params));
          list.body.append(h('div.row', null,
            h('span.check', { cls: it.ok ? 'ok' : null }, it.ok ? icon('check') : null),
            h('div.label', null, h('b', { text: t(`windows.${it.id}.label`) }), h('p', { text: `Ora: ${t(it.current_key, it.current_params)}` })),
            h('div.control', null, action)));
        }
      },
    };
  },
};

// ------------------------------------------------------------------ avanzate

const ADVANCED = [
  ['dim_bright_threshold_pct', 'Soglia di luminosità', 'Da quale luminosità un elemento fermo viene scurito (% del bianco).', 5, 100, 1, (v) => `${v}%`, 25],
  ['dim_motion_fraction_pct', 'Quota di movimento', 'Quanto deve muoversi il contenuto intorno a un logo perché conti come "video".', 10, 90, 1, (v) => `${v}%`, 25],
  ['dim_recover_ms', 'Ritorno normale', 'Quanto ci mette una zona a tornare normale quando cambia.', 100, 5000, 100, (v) => `${String(v / 1000).replace('.', ',')} s`, 500],
  ['pause_enter_delay_ms', 'Ritardo della pausa', 'Attesa prima di mettere in pausa (evita pause per un attimo).', 0, 10000, 100, (v) => `${String(v / 1000).replace('.', ',')} s`, 300],
  ['resume_delay_ms', 'Ritardo della ripresa', 'Attesa prima di riprendere dopo un gioco o un video.', 0, 10000, 250, (v) => `${String(v / 1000).replace('.', ',')} s`, 2500],
];

PAGES.avanzate = {
  build(root) {
    const reset = h('button.btn.small', { type: 'button', on: { click: () => {
      const patch = { allow_tearing: false };
      for (const [key, , , , , , , def] of ADVANCED) patch[key] = def;
      setConfig(patch);
    } } }, icon('reset'), 'Ripristina i valori predefiniti');
    const p = panel('Parametri del motore', 'valori fini, di solito non servono', reset);
    const sliders = ADVANCED.map(([key, title, desc, min, max, step, fmt]) => {
      const s = slider({ min, max, step, format: fmt, onInput: throttledConfig(key) });
      p.body.append(settingRow(title, desc, s.el, { wide: true, code: key }));
      return [key, s];
    });
    const tearing = toggle((v) => setConfig({ allow_tearing: v }));
    p.body.append(settingRow('Tearing', 'Solo con VRR (G-Sync/FreeSync) attivo: riduce la latenza dell\'overlay.', tearing.el, { code: 'allow_tearing' }));
    root.append(h('div.page', null, h('p.hint.box', { text: 'Le impostazioni dell\'app, l\'aspetto e la diagnostica sono in Impostazioni.' }), p.el));
    return {
      update() {
        const c = cfg();
        if (!c) return;
        for (const [key, s] of sliders) s.set(c[key]);
        tearing.set(c.allow_tearing);
      },
    };
  },
};

// ------------------------------------------------------------------ informazioni

const CHANGES = [
  ['1.1.0', [
    'Segnali dal vivo nella striscia in basso: rischio, movimento e luminosità degli ultimi 60 secondi',
    'Statistiche: mappa di usura del pannello, rischio nel tempo, app più usate, usura per contenuto',
    'Sessioni, giorni di fila e usura evitata',
    'Avviso quando zone ferme e luminose restano senza protezione troppo a lungo',
    'Impostazioni: notifiche e privacy, con la cancellazione dello storico',
  ]],
  ['1.0.0', [
    'Nuova interfaccia con barra laterale, barra di stato e riepilogo in basso',
    'Luminosità via software dello schermo protetto',
    'Bordi scuriti regolabili lato per lato',
    'Modalità assente: lo schermo si scurisce quando non ci sei',
    'Pagina Monitor con la scelta dello schermo da proteggere',
    'Attività recente nella Panoramica',
    'Colore d\'accento e dimensione del testo',
  ]],
];

PAGES.informazioni = {
  build(root) {
    const version = h('span.mono.accent');
    const intro = h('section.panel', null, h('div.panel-body.pad', null, h('div.about', null, logo(),
      h('div', null, h('h2', null, 'OLED Guard ', version),
        h('p', { text: 'Protegge lo schermo OLED dal burn-in: sposta l\'immagine di pochi pixel, scurisce piano le zone ferme e luminose e si mette in pausa da solo con giochi e video a schermo intero.' })))));
    const news = panel('Novità');
    news.body.classList.add('pad');
    news.body.append(h('ul.changes', null, CHANGES.map(([v, items]) => h('li', null, h('h5', { text: `v${v}` }), h('ul', null, items.map((t) => h('li', { text: t })))))));
    const files = panel('File e licenze');
    const cfgPath = h('div.path'), logPath = h('div.path');
    files.body.append(
      settingRow('Configurazione', cfgPath, h('span')),
      settingRow('Log', logPath, h('button.btn.small', { type: 'button', on: { click: () => send({ cmd: 'openLogFolder' }) } }, icon('folder'), 'Apri')),
      settingRow('Microsoft Edge WebView2', 'Usato per disegnare questa finestra. Licenza di tipo BSD di Microsoft (testo in third_party/webview2).', h('span')));
    root.append(h('div.page', null, intro, h('div.cols.even', null, news.el, files.el)));
    return {
      update() {
        if (!store.about) return;
        setText(version, `v${store.about.version}`);
        setText(cfgPath, store.about.config_path);
        setText(logPath, store.about.log_dir);
      },
    };
  },
};
