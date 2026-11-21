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

const ACTIVITY_ICONS = { pause: 'pause', manual: 'pause', resume: 'play', test: 'move', away: 'moon', back: 'sun', monitor: 'monitor', windows: 'windows', config: 'doc' };

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
          h('span', { text: e.text + (e.count > 1 ? ` ×${e.count}` : '') }),
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

// ------------------------------------------------------------------ pagine del compito successivo

for (const [, items] of NAV) {
  for (const [id, label] of items) {
    if (PAGES[id]) continue;
    PAGES[id] = {
      build(root) {
        const p = panel(label);
        p.body.append(h('div.empty', { text: 'Pagina in arrivo.' }));
        root.append(h('div.page', null, p.el));
      },
    };
  }
}
