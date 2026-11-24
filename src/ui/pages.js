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
  svg.setAttribute('aria-label', t('overview.radar_label'));
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
    ctx.fillText(d && d.status === 'paused' ? t('dim.map_paused') : t('dim.map_pending'), W / 2, H / 2);
  }
}
function dimMapCanvas() { return h('canvas.dimmap', { width: 640, height: 360, 'aria-label': t('dim.map_label') }); }

function dimStatusText() {
  const d = store.dim;
  if (!d) return '-';
  return t('dim.status.' + d.status) + (d.suspended_by ? ` · ${d.suspended_by}` : '');
}
function highlightLeft() { return Math.max(0, store.highlightUntil - performance.now()); }
function canHighlight() { const d = store.dim; return !!d && !['off', 'suspended', 'paused'].includes(d.status); }

const ACTIVITY_ICONS = { pause: 'pause', manual: 'pause', resume: 'play', test: 'move', away: 'moon', back: 'sun', monitor: 'monitor', windows: 'windows', config: 'doc', alert: 'bolt' };

// ------------------------------------------------------------------ panoramica

PAGES.panoramica = {
  build(root) {
    // Protezione
    const prot = panel(t('overview.protection'));
    const sub = prot.el.querySelector('.panel-head');
    const monLabel = h('span.sub');
    sub.insertBefore(monLabel, sub.children[1]);
    const svg = radarSvg();
    const count = h('div.big-count', null, h('span', { text: '--:--' }), h('small', { text: t('overview.next_move') }));
    const kv = (label) => { const b = h('b', { text: '-' }); return [h('div.kv', null, h('span', { text: label }), b), b]; };
    const [kOffset, vOffset] = kv(t('overview.offset'));
    const [kFg, vFg] = kv(t('overview.foreground'));
    const [kLat, vLat] = kv(t('overview.latency'));
    const [kAmp, vAmp] = kv(t('overview.amplitude'));
    prot.body.classList.add('pad');
    prot.body.append(h('div.protect', null, svg, h('div', null, count, h('div', null, kOffset, kFg, kLat, kAmp))));

    // Azioni rapide
    const quick = panel(t('overview.quick'));
    quick.body.classList.add('pad');
    const qa = (ic, title, onClick, withToggle) => {
      const sm = h('small');
      const tg = withToggle ? toggle(() => {}) : null;
      const el = h('button.qa', { type: 'button', on: { click: onClick } },
        h('div.qa-top', null, h('span.qa-icon', null, icon(ic)), tg ? tg.el : null),
        h('div', null, h('b', { text: title }), sm));
      return { el, sm, tg };
    };
    const qPause = qa('pause', t('overview.qa.pause'), () => send({ cmd: 'setManualPause', on: !(store.state && store.state.manual_pause) }), true);
    const qTest = qa('move', t('overview.qa.test'), () => send({ cmd: 'testShift' }), false);
    const qHigh = qa('eye', t('overview.qa.highlight'), () => send({ cmd: 'dimHighlight' }), false);
    const qDim = qa('sun', t('overview.qa.dim'), () => cfg() && setConfig({ dim_enabled: !cfg().dim_enabled }), true);
    const qVig = qa('vignette', t('overview.qa.vignette'), () => cfg() && setConfig({ vignette_enabled: !cfg().vignette_enabled }), true);
    quick.body.append(h('div.quick', null, qPause.el, qTest.el, qHigh.el, qDim.el, qVig.el));

    // Zone oscurate
    const zones = panel(t('overview.zones'));
    zones.body.classList.add('pad');
    const cv = dimMapCanvas();
    const [kSt, vSt] = kv(t('overview.zones.status'));
    const [kArea, vArea] = kv(t('overview.zones.area'));
    const [kMax, vMax] = kv(t('overview.zones.max'));
    zones.body.append(cv, h('div', null, kSt, kArea, kMax));

    // Attivita' recente
    const clear = h('button.btn.small', { type: 'button', text: t('overview.activity.clear'), on: { click: () => send({ cmd: 'clearActivity' }) } });
    const act = panel(t('overview.activity'), t('overview.activity.since'), clear);
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
      if (!store.activity.length) list.append(h('div.empty', { text: t('overview.activity.empty') }));
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
          setText(vLat, s.latency_ms ? `${fmtNumber(s.latency_ms, 1)} ms` : '-');
          setText(count.lastChild, s.paused ? t('overview.paused_caps') : (s.testing_shift ? t('overview.test_next_move') : t('overview.next_move')));
          qPause.tg.set(s.manual_pause);
          qPause.el.classList.toggle('on', s.manual_pause);
          setText(qPause.sm, s.manual_pause ? t('overview.qa.pause.on') : t('overview.qa.pause.off'));
          qTest.el.classList.toggle('on', s.testing_shift);
          setText(qTest.sm, s.testing_shift ? t('status.test') : t('overview.qa.test.sub'));
        }
        if (c) {
          setText(vAmp, `${c.amplitude_px} px`);
          qDim.tg.set(c.dim_enabled);
          qDim.el.classList.toggle('on', c.dim_enabled);
          setText(qDim.sm, c.dim_enabled ? t('overview.qa.dim.on') : t('dim.status.off'));
          qVig.tg.set(c.vignette_enabled);
          qVig.el.classList.toggle('on', c.vignette_enabled);
          setText(qVig.sm, c.vignette_enabled ? t('overview.qa.vignette.on', { pct: fmtPct(c.vignette_strength_pct) }) : t('overview.qa.vignette.off'));
        }
        drawRadar(svg);
        if (d !== lastDim) {
          lastDim = d;
          drawDimMap(cv);
          setText(vSt, dimStatusText());
          setText(vArea, d && d.cells ? t('dim.of_screen', { pct: fmtPct(d.dimmed_pct, 1) }) : '-');
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
        setText(qHigh.sm, left > 0 ? t('dim.highlighted', { s: Math.ceil(left / 1000) }) : t('overview.qa.highlight.sub'));
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
    s.handle = h('div.vig-handle', { cls: s.axis === 'y' ? 'v' : null, title: t('overlay.vig.drag') });
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
  const side = h('div.vig-side', null, h('small', { text: t('overlay.vig.strength_caps') }), strengthValue, strength.input);
  const reset = h('button.btn.small', { type: 'button', on: { click: () => setConfig({ ...VIG_DEFAULTS }) } }, icon('reset'), t('overlay.vig.reset'));
  const el = h('div', null,
    h('div.vig-head', null, h('span', { text: t('overlay.vig.preview') }), reset),
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
    const shift = panel(t('overlay.shift'), 'pixel shift');
    const amp = segmented([[1, '1 px'], [2, '2 px'], [3, '3 px']], (v) => setConfig({ amplitude_px: v }));
    const mini = h('span.mini-grid');
    const click = h('p');
    const dwellSend = throttle((lo, hi) => setConfig({ dwell_min_s: lo, dwell_max_s: hi }), 100);
    const dwell = dualSlider({ min: 5, max: 180, step: 1, format: (lo, hi) => `${lo}-${hi} s`, onInput: dwellSend });
    const transSend = throttle((lo, hi) => setConfig({ transition_min_s: lo, transition_max_s: hi }), 100);
    const trans = dualSlider({ min: 0.5, max: 15, step: 0.5, format: (lo, hi) => `${fmtNumber(lo, lo % 1 ? 1 : 0)}-${fmtNumber(hi, hi % 1 ? 1 : 0)} s`, onInput: transSend });
    const testBtn = h('button.btn.solid', { type: 'button', on: { click: () => send({ cmd: 'testShift' }) } }, icon('move'), t('overlay.test_now'));
    shift.body.append(
      settingRow(t('overview.amplitude'), t('overlay.amplitude.desc'), amp.el),
      settingRow(t('overlay.positions'), click, mini),
      settingRow(t('overlay.dwell'), t('overlay.dwell.desc'), dwell.el, { wide: true }),
      settingRow(t('overlay.transition'), t('overlay.transition.desc'), trans.el, { wide: true }),
      settingRow(t('overlay.test'), t('overlay.test.desc'), testBtn));

    // Bordi scuriti
    const vigToggle = toggle((v) => setConfig({ vignette_enabled: v }));
    const vig = panel(t('overview.qa.vignette'), t('overlay.vig.sub'), h('span.muted', { text: t('overlay.vig.enabled') }), vigToggle.el);
    vig.body.classList.add('pad');
    const editor = vignetteEditor();
    vig.body.append(editor.el, h('p.hint', { text: t('overlay.vig.hint') }));

    // Oscuramento
    const dimToggle = toggle((v) => setConfig({ dim_enabled: v }));
    const dim = panel(t('overlay.dim'), null, h('span.muted', { text: t('dim.status.active') }), dimToggle.el);
    const cv = dimMapCanvas();
    const highBtn = h('button.btn.small', { type: 'button', on: { click: () => send({ cmd: 'dimHighlight' }) } }, icon('eye'), t('overlay.highlight'));
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
    const winMode = segmented([['desktop', t('overlay.win.desktop')], ['video', t('overlay.win.video')]], (v) => setConfig({ dim_window_mode: v }));
    const activate = segmented([['hover', t('overlay.act.hover')], ['click', t('overlay.act.click')], ['both', t('overlay.act.both')], ['never', t('overlay.act.never')]], (v) => setConfig({ dim_activate: v }));
    const largeToggle = toggle((v) => setConfig({ dim_large_enabled: v }));
    const largeDelay = mk('dim_large_delay_s', 5, 240, 5, (v) => fmtMin(v * 60), (v) => Math.round(v * 60));
    const largeStrength = mk('dim_large_strength_pct', 5, 80, 1, (v) => `${v}%`, (v) => v);
    const rowsLarge = [
      settingRow(t('overlay.large'), t('overlay.large.desc'), largeToggle.el),
      settingRow(t('overlay.large.delay'), null, largeDelay.el, { wide: true }),
      settingRow(t('overlay.large.strength'), null, largeStrength.el, { wide: true }),
    ];
    const exLink = h('button.btn.small', { type: 'button', on: { click: () => showPage('profili') } }, icon('apps'), t('overlay.excluded.manage'));
    const activateHint = h('p.hint');
    dim.body.append(
      h('div', { cls: 'row col' }, cv, h('div.add-row', null, status, h('span.grow'), highBtn)),
      settingRow(t('overlay.strength'), t('overlay.strength.desc'), strength.el, { wide: true }),
      settingRow(t('overlay.delay'), null, delay.el, { wide: true }),
      settingRow(t('overlay.ramp'), t('overlay.ramp.desc'), ramp.el, { wide: true }),
      settingRow(t('overlay.win'), null, winMode.el),
      settingRow(t('overlay.act'), null, activate.el),
      activateHint,
      ...rowsLarge,
      settingRow(t('overlay.excluded'), t('overlay.excluded.desc'), exLink));

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
          setText(click, t('overlay.positions.desc', { n: side * side, px: fmtNumber(meanClickOffset(c.amplitude_px), 1) }));
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
            ? t('overlay.act.never_hint')
            : t('overlay.act.hint'));
        }
        if (store.dim !== lastDim) {
          lastDim = store.dim;
          drawDimMap(cv);
          const d = store.dim;
          setText(status, d ? `${dimStatusText()}${d.cells ? ` · ${t('dim.of_screen', { pct: fmtPct(d.dimmed_pct, 1) })}` : ''}` : '');
        }
        view.tick();
      },
      tick() {
        const left = highlightLeft();
        highBtn.disabled = !canHighlight();
        highBtn.classList.toggle('on', left > 0);
        setText(highBtn.lastChild, left > 0 ? t('dim.highlighted', { s: Math.ceil(left / 1000) }) : t('overlay.highlight'));
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
      h('b', null, t('monitor.protecting'), ' ', name), countLabel, h('span.grow'), h('span.muted', { text: t('monitor.auto') }), autoToggle.el));
    const list = h('div.stack');
    root.append(h('div.page', null, head, list));

    const sendBrightness = throttledConfig('brightness_pct');
    let lastMonitors = null, bright = null;
    const build = () => {
      const m = store.monitors;
      list.innerHTML = '';
      bright = null;
      if (!m || !m.monitors.length) { list.append(h('div.empty', { text: t('monitor.none') })); return; }
      const protectedName = (m.monitors.find((x) => x.protected) || {}).name;
      m.monitors.forEach((mon, i) => {
        const tg = toggle((v) => {
          if (v) send({ cmd: 'setMonitor', device: mon.device });
          else toast(t('monitor.switch_hint'));
        });
        tg.set(mon.protected);
        const pills = h('div.pills', null,
          mon.primary ? pill(t('monitor.primary'), 'accent') : null,
          pill(mon.hdr ? 'HDR' : 'SDR', mon.hdr ? 'info' : null),
          mon.connection ? pill(mon.connection === 'internal' ? t('monitor.connection.internal') : mon.connection) : null);
        const top = h('div.mon-top', null, tg.el,
          h('div.grow', null, h('h4', { text: t('monitor.n', { n: i + 1 }) }), h('p', { text: [mon.name, `${mon.width} × ${mon.height}`, mon.refresh_hz ? `${mon.refresh_hz} Hz` : null].filter(Boolean).join(' · ') })),
          pills);
        let right;
        if (mon.protected) {
          bright = slider({ min: 20, max: 100, step: 1, format: (v) => `${v}%`, onInput: sendBrightness });
          right = h('div', null,
            settingRow(t('monitor.brightness'), t('monitor.brightness.desc'), bright.el, { wide: true }),
            h('p.hint.box', { text: t('monitor.ddc_hint') }));
        } else {
          right = h('p.hint', { text: t('monitor.not_protected', { name: protectedName || t('monitor.current') }) });
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
          setText(name, p ? p.name : t('monitor.nothing'));
          setText(countLabel, tp('monitor.connected', m.monitors.length));
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

// Tipi di contenuto nello stesso ordine dello storico: [chiave del testo, colore].
const CONTENT_KINDS = [
  ['stats.content.work', 'var(--accent)'],
  ['stats.content.video', 'var(--info)'],
  ['stats.content.games', '#b28cff'],
  ['stats.content.away', '#5fdc8f'],
  ['stats.content.manual', 'var(--warn)'],
];
const RANGES = { '6h': 72, '24h': 288, '7d': 2016 };

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
    const text = k === 4 ? t('stats.chart.now') : (bucketS >= 3600
      ? d.toLocaleDateString(store.lang, { weekday: 'short', timeZone: 'UTC' })
      : `${pad2(d.getUTCHours())}:${pad2(d.getUTCMinutes())}`);
    labels += `<text class="axis" x="${Math.min(W - 24, Math.max(left, x(i) - 14))}" y="${H - 4}">${text}</text>`;
  }
  box.innerHTML = `<svg viewBox="0 0 ${W} ${H}" preserveAspectRatio="none">${grid}<path class="area" d="${area}"/><path class="line" d="${line}"/>${labels}</svg>`;
}

PAGES.statistiche = {
  build(root) {
    const stat = (label) => { const b = h('b', { text: '-' }), sp = h('span'); return [h('div.stat', null, h('small', { text: label }), b, sp), b, sp]; };
    const [sTot, vTot, nTot] = stat(t('strip.total'));
    const [sSes, vSes, nSes] = stat(t('stats.sessions'));
    const [sAvo, vAvo, nAvo] = stat(t('strip.avoided_today'));
    const [sStr, vStr, nStr] = stat(t('stats.streak'));
    vTot.className = 'accent';
    vSes.className = 'info';
    vAvo.className = 'accent';
    vStr.className = 'warn';

    // Mappa di usura
    const mapSub = h('span.sub');
    const wear = panel(t('stats.wear'));
    wear.el.querySelector('.panel-head').insertBefore(mapSub, wear.el.querySelector('.panel-head .grow'));
    wear.body.classList.add('pad');
    const grid = h('div.wear-grid');
    // Celle create sulla griglia ricevuta (cols x rows, dalle proporzioni dello schermo).
    const cells = [];
    const sizeGrid = (cols, rows) => {
      if (cells.length === cols * rows && grid.dataset.cols === String(cols)) return;
      grid.innerHTML = '';
      cells.length = 0;
      grid.dataset.cols = String(cols);
      grid.style.gridTemplateColumns = `repeat(${cols}, 1fr)`;
      for (let i = 0; i < cols * rows; i++) { const c = h('i'); cells.push(c); grid.append(c); }
    };
    const legend = h('div.legend', null, h('span', { text: t('stats.wear.left') }), h('span.grow'), h('span', { text: t('stats.wear.less') }),
      ...[0.1, 0.35, 0.6, 0.78, 0.95].map((t) => { const i = h('i'); i.style.background = wearColor(t); return i; }),
      h('span', { text: t('stats.wear.more') }), h('span.grow'), h('span', { text: t('stats.wear.right') }));
    const wkv = (label) => { const b = h('b', { text: '-' }), s = h('span'); return [h('div.wear-stat', null, h('small', { text: label }), b, s), b, s]; };
    const [w1, w1b, w1s] = wkv(t('stats.wear.measured'));
    const [w2, w2b, w2s] = wkv(t('stats.wear.worst'));
    const [w3, w3b, w3s] = wkv(t('stats.wear.concentration'));
    const [w4, w4b, w4s] = wkv(t('stats.wear.over_double'));
    const wearEmpty = h('div.empty', { text: t('stats.wear.empty') });
    wear.body.append(grid, legend, h('div.wear-stats', null, w1, w2, w3, w4), wearEmpty,
      h('p.hint', { text: t('stats.wear.hint') }));

    // Grafico del rischio
    let range = '24h';
    const rangeSeg = segmented([['6h', t('stats.range.6h')], ['24h', t('stats.range.24h')], ['7d', t('stats.range.7d')]], (v) => { range = v; lastHistory = null; view.update(); });
    const chartPanel = panel(t('stats.chart'), null, rangeSeg.el);
    chartPanel.body.classList.add('pad');
    const chartInfo = h('div.mono.muted.chart-info');
    const chart = h('div.chart');
    chartPanel.body.append(chartInfo, chart);

    // App piu' usate
    const appsPanel = panel(t('stats.apps'), t('stats.days7'));
    const appsList = h('div.top-apps');
    appsPanel.body.append(appsList);

    // Contenuti
    const contentPanel = panel(t('stats.content'), t('stats.days7'));
    contentPanel.body.classList.add('pad');
    const contentBar = h('div.content-bar');
    const contentLegend = h('div.content-legend');
    contentPanel.body.append(contentBar, contentLegend);

    // Parti del progetto 1: posizioni dello spostamento e pause di oggi.
    const heatPanel = panel(t('overview.radar_label'), t('stats.heat.sub'));
    heatPanel.body.classList.add('pad');
    const heat = h('div.heat');
    const heatCells = [];
    for (let i = 0; i < 49; i++) { const b = h('b'); const c = h('i', null, b); heatCells.push([c, b]); heat.append(c); }
    const uniform = h('p.hint');
    heatPanel.body.append(heat, uniform);
    const pausePanel = panel(t('stats.pauses'), t('stats.pauses.sub'));
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
          setText(nTot, hist ? t('stats.this_week', { value: fmtHours(hist.protected_week_s * 1000) }) : t('stats.total.sub'));
        }
        if (hist && hist !== lastHistory) {
          lastHistory = hist;
          setText(vSes, String(hist.sessions_total));
          setText(nSes, t('stats.this_week', { value: hist.sessions_week }));
          setText(vAvo, hist.avoided_today === null ? t('common.na') : fmtPct(hist.avoided_today * 100));
          setText(nAvo, hist.avoided_total === null ? t('stats.avoided.sub') : t('stats.avoided.total', { pct: fmtPct(hist.avoided_total * 100) }));
          setText(vStr, tp('stats.days', hist.streak));
          setText(nStr, t('stats.record', { n: hist.streak_best }));

          const w = hist.wear;
          setText(mapSub, w.monitor || '');
          const has = w.hottest >= 0;
          sizeGrid(w.cols || 32, w.rows || 18);
          grid.hidden = !has;
          wearEmpty.hidden = has;
          cells.forEach((cell, i) => {
            cell.style.background = has ? wearColor(w.cells[i]) : 'transparent';
            cell.classList.toggle('hot', has && i === w.hottest);
          });
          const since = has ? new Date(w.since * 1000).toLocaleDateString(store.lang, { day: '2-digit', month: '2-digit', timeZone: 'UTC' }) : '-';
          setText(w1b, has ? `${Math.round(w.measured_hours)} h` : '-');
          setText(w1s, has ? t('stats.wear.since', { date: since }) : '');
          setText(w2b, has ? t(w.hottest_key) : '-');
          setText(w2s, has ? t('stats.wear.full_white', { h: fmtNumber(w.hottest_hours, 1) }) : '');
          setText(w3b, has ? `${fmtNumber(w.concentration, 2)}×` : '-');
          setText(w3s, t('stats.wear.ratio'));
          setText(w4b, has ? `${w.over_double}/${cells.length}` : '-');
          setText(w4s, has ? t('stats.wear.of_panel', { pct: fmtPct((w.over_double / cells.length) * 100, 1) }) : '');

          const n = RANGES[range];
          let vals = hist.timeline.slice(-n);
          let bucketS = 300;
          if (range === '7d') {  // a ore
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
            ? t('stats.chart.info', { peak: fmtPct(Math.max(...m) * 100), mean: fmtPct((m.reduce((a, b) => a + b, 0) / m.length) * 100), now: cur === undefined ? '-' : fmtPct(cur * 100) })
            : t('stats.chart.empty'));

          appsList.innerHTML = '';
          if (!hist.top_apps.length) appsList.append(h('div.empty', { text: t('stats.apps.empty') }));
          const topTotal = Math.max(1, ...hist.top_apps.map((a) => a.protected_s + a.paused_s));
          for (const a of hist.top_apps) {
            const info = ruleInfo(a.rule);
            const prot = h('i.prot'), paus = h('i.paus');
            prot.style.width = `${(a.protected_s / topTotal) * 100}%`;
            paus.style.width = `${(a.paused_s / topTotal) * 100}%`;
            appsList.append(h('div.top-app', null, avatar(a.exe),
              h('div.grow', null, h('div.top-app-name', null, h('b', { text: a.exe }), pill(info[2], info[3])), h('div.split-bar', null, prot, paus)),
              h('span.mono', { text: fmtClock((a.protected_s + a.paused_s) * 1000) })));
          }
          if (hist.top_apps.length) appsList.append(h('div.legend.left', null, h('i.prot'), h('span', { text: t('stats.apps.protected') }), h('i.paus'), h('span', { text: t('signals.paused') })));

          contentBar.innerHTML = '';
          contentLegend.innerHTML = '';
          const total = hist.content.reduce((a, b) => a + b, 0);
          if (total <= 0) contentLegend.append(h('div.empty', { text: t('stats.content.empty') }));
          hist.content.forEach((share, k) => {
            const [label, color] = CONTENT_KINDS[k];
            if (share > 0) { const seg = h('i'); seg.style.width = `${share * 100}%`; seg.style.background = color; contentBar.append(seg); }
            if (total > 0) {
              const dot = h('i');
              dot.style.background = color;
              contentLegend.append(h('div', null, dot, h('span', { text: t(label) }), h('b', { text: fmtPct(share * 100) })));
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
          setText(uniform, st.uniformity === null ? t('stats.uniformity.pending') : t('stats.uniformity', { pct: fmtPct(st.uniformity, 1) }));
          bars.innerHTML = '';
          const entries = Object.entries(st.pauses_by_reason || {}).sort((x, y) => y[1] - x[1]);
          if (!entries.length) bars.append(h('div.empty', { text: t('stats.pauses.empty') }));
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

// Regole per app: [titolo, descrizione, etichetta breve, tono]; testi nella lingua attiva.
const RULE_TONES = { always: 'warn', auto: 'accent', never: 'info' };
const RULES = ['always', 'auto', 'never'];
function ruleInfo(rule) {
  const r = RULES.includes(rule) ? rule : 'auto';
  return [t(`rule.${r}.title`), t(`rule.${r}.desc`), t(`rule.${r}.short`), RULE_TONES[r]];
}
function ruleExplain(rule) { return RULES.includes(rule) ? t(`rule.${rule}.explain`) : ''; }

PAGES.profili = {
  build(root) {
    let selected = null, pendingSelect = null, filter = '';
    // Elenco
    const search = h('input', { placeholder: t('apps.search'), spellcheck: 'false' });
    search.addEventListener('input', () => { filter = search.value.trim().toLowerCase(); renderList(); });
    const listPanel = panel(t('nav.profili'));
    listPanel.el.querySelector('.panel-head').append(h('label.field', null, icon('search'), search));
    const count = h('span.sub');
    listPanel.el.querySelector('.panel-head').insertBefore(count, listPanel.el.querySelector('.panel-head .grow'));
    const list = h('div.applist');
    listPanel.body.style.padding = '0';
    listPanel.body.append(list);

    // Aggiungi
    const addInput = h('input', { placeholder: t('apps.add.placeholder'), maxlength: '260', spellcheck: 'false' });
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
    const addPanel = panel(t('apps.add'));
    addPanel.body.classList.add('pad');
    addPanel.body.append(h('div.add-row', null, h('label.field', null, icon('plus'), addInput),
      h('button.btn', { type: 'button', on: { click: () => { if (!procBox.hidden) { procBox.hidden = true; return; } send({ cmd: 'listProcesses' }); } } }, icon('list'), t('apps.add.open')),
      h('button.btn', { type: 'button', on: { click: () => send({ cmd: 'browseExe' }) } }, icon('folder'), t('apps.add.browse')),
      h('button.btn.solid', { type: 'button', on: { click: addTyped } }, icon('plus'), t('apps.add.button'))), procBox);

    // Pause automatiche globali
    const autoNotif = toggle((v) => setConfig({ pause_on_notification_state: v }));
    const autoFull = toggle((v) => setConfig({ pause_on_fullscreen_window: v }));
    const autoProtected = toggle((v) => setConfig({ pause_on_protected_window: v }));
    const autoPanel = panel(t('apps.auto'), t('apps.auto.sub'));
    autoPanel.body.append(
      settingRow(t('apps.auto.games'), t('apps.auto.games.desc'), autoNotif.el),
      settingRow(t('apps.auto.full'), t('apps.auto.full.desc'), autoFull.el),
      settingRow(t('apps.auto.protected.title'), t('apps.auto.protected.desc'), autoProtected.el));

    // Modifica
    const editPanel = panel(t('apps.edit'));
    const delBtn = h('button.btn.small.danger', { type: 'button', on: { click: () => { if (selected) { send({ cmd: 'removeApp', exe: selected }); selected = null; } } } }, icon('trash'), t('apps.delete'));
    editPanel.el.querySelector('.panel-head').append(delBtn);
    editPanel.body.classList.add('pad');
    const editName = h('div.edit-name');
    const editInfo = h('p.hint');
    const rules = choiceCards(RULES.map((r) => { const v = ruleInfo(r); return [r, v[0], v[1]]; }), (rule) => selected && send({ cmd: 'setAppRule', exe: selected, rule }));
    const explain = h('p.hint.box');
    const dimEx = toggle((v) => selected && send({ cmd: 'setDimExclude', exe: selected, on: v }));
    const editBody = h('div', null,
      h('div.edit-title', { text: t('apps.edit.app') }), editName, editInfo,
      h('div.edit-title', { text: t('apps.edit.when') }), rules.el, explain,
      h('div.edit-title', { text: t('overview.qa.dim') }),
      settingRow(t('apps.edit.no_dim'), t('apps.edit.no_dim.desc'), dimEx.el));
    const editEmpty = h('div.empty', { text: t('apps.edit.empty') });
    editPanel.body.append(editEmpty, editBody);

    root.append(h('div.page', null, h('div.cols', null,
      h('div.stack', null, listPanel.el, addPanel.el, autoPanel.el),
      h('div.stack', null, editPanel.el))));

    const apps = () => store.apps || [];
    function renderList() {
      list.innerHTML = '';
      const shown = apps().filter((a) => !filter || a.exe.includes(filter));
      setText(count, `${apps().length}`);
      if (!shown.length) list.append(h('div.empty', { text: filter ? t('apps.list.no_match') : t('apps.list.empty') }));
      for (const a of shown) {
        const info = ruleInfo(a.rule);
        const trash = h('button.trash', { type: 'button', title: t('apps.delete'), on: { click: (e) => { e.stopPropagation(); if (selected === a.exe) selected = null; send({ cmd: 'removeApp', exe: a.exe }); } } }, icon('trash'));
        const row = h('button.app', { type: 'button', cls: a.exe === selected ? 'on' : null, on: { click: () => { selected = a.exe; renderList(); renderEdit(); } } },
          avatar(a.exe),
          h('span.name', null, h('b', { text: a.exe }), h('small', { text: a.last_seen_s ? t('apps.list.last_pause', { when: fmtWhen(a.last_seen_s * 1000), times: tp('apps.list.times', a.count) }) : t('apps.list.manual') })),
          h('span.tags', null, pill(info[2], info[3]), a.dim_exclude ? pill(t('apps.list.no_dim')) : null),
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
      setText(editInfo, a.reason_key ? t('apps.edit.last_pause', { reason: t(a.reason_key).toLowerCase(), when: fmtWhen(a.last_seen_s * 1000) }) : t('apps.edit.manual'));
      rules.set(a.rule);
      setText(explain, ruleExplain(a.rule));
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
        if (c) { autoNotif.set(c.pause_on_notification_state); autoFull.set(c.pause_on_fullscreen_window); autoProtected.set(c.pause_on_protected_window); }
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
    const HINT = t('keys.hint');
    const hint = h('p.hint.box', { text: HINT });
    let capturing = null;
    const keys = {};
    const p = panel(t('nav.tasti'), t('keys.sub'));
    const defs = [
      ['hotkey_toggle', t('keys.toggle'), t('keys.toggle.desc')],
      ['hotkey_emergency_exit', t('keys.emergency'), t('keys.emergency.desc')],
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
        setText(keys[key], t('keys.press'));
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
      if (!mods.length) { setText(hint, t('keys.need_modifier')); return; }
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
    const language = segmented([['auto', t('settings.language.auto')], ...Object.entries(LANG_NAMES)], (v) => send({ cmd: 'setLanguage', id: v }));
    const langPanel = panel(t('settings.language'), t('settings.language.sub'));
    langPanel.el.classList.add('accented');
    langPanel.body.append(settingRow(t('settings.language'), t('settings.language.desc'), language.el, { wide: true }));

    const start = panel(t('settings.start'), t('settings.start.sub'));
    start.el.classList.add('accented');
    start.body.append(settingRow(t('tray.autostart'), t('settings.start.desc'), autostart.el));

    const awayToggle = toggle((v) => setConfig({ away_enabled: v }));
    const awayAfter = slider({ min: 1, max: 60, step: 1, format: (v) => `${v} min`, onInput: throttledConfig('away_after_min') });
    const awayLevel = slider({ min: 10, max: 80, step: 1, format: (v) => `${v}%`, onInput: throttledConfig('away_brightness_pct') });
    const awayRows = [
      settingRow(t('settings.away.after'), t('settings.away.after.desc'), awayAfter.el, { wide: true }),
      settingRow(t('settings.away.level'), t('settings.away.level.desc'), awayLevel.el, { wide: true }),
    ];
    const away = panel(t('settings.away'), t('settings.away.sub'));
    away.el.classList.add('accented');
    away.body.append(settingRow(t('settings.away.toggle'), t('settings.away.toggle.desc'), awayToggle.el), ...awayRows);

    const swatches = h('div.swatches');
    const swatchEls = {};
    for (const [name, color] of Object.entries(ACCENTS)) {
      const i = h('i');
      i.style.background = color;
      swatchEls[name] = h('button.swatch', { type: 'button', title: name, on: { click: () => setConfig({ ui_accent: name }) } }, i,
        h('small', { text: `${fmtNumber(contrastRatio(color, PANEL_COLOR), 1)}:1` }));
      swatches.append(swatchEls[name]);
    }
    const size = segmented([['small', t('settings.size.small')], ['normal', t('settings.size.normal')], ['large', t('settings.size.large')], ['xl', 'XL']], (v) => setConfig({ ui_text_size: v }));
    const look = panel(t('settings.look'), t('settings.look.sub'));
    look.el.classList.add('accented');
    look.body.append(
      settingRow(t('settings.accent'), t('settings.accent.desc'), swatches, { wide: true }),
      settingRow(t('settings.size'), t('settings.size.desc'), size.el, { wide: true }));

    const level = segmented([['error', t('settings.log.error')], ['warn', t('settings.log.warn')], ['info', t('settings.log.info')], ['debug', t('settings.log.debug')]], (v) => setConfig({ log_level: v }));
    const cfgPath = h('div.path'), logPath = h('div.path');
    const diag = panel(t('settings.diag'), t('settings.diag.sub'));
    diag.el.classList.add('accented');
    diag.body.append(
      settingRow(t('settings.log'), t('settings.log.desc'), level.el, { wide: true }),
      settingRow(t('settings.log.folder'), logPath, h('button.btn', { type: 'button', on: { click: () => send({ cmd: 'openLogFolder' }) } }, icon('folder'), t('common.open'))),
      settingRow(t('settings.config_file'), cfgPath, h('span')));

    // Notifiche: avviso per il rischio non protetto.
    const alertToggle = toggle((v) => setConfig({ alert_enabled: v }));
    const alertLevel = segmented([['low', t('settings.alert.low', { pct: fmtPct(10) })], ['medium', t('settings.alert.medium', { pct: fmtPct(20) })], ['high', t('settings.alert.high', { pct: fmtPct(35) })]], (v) => setConfig({ alert_threshold: v }));
    const alertAfter = slider({ min: 5, max: 240, step: 5, format: (v) => `${v} min`, onInput: throttledConfig('alert_after_min') });
    const alertRows = [
      settingRow(t('settings.alert.threshold'), t('settings.alert.threshold.desc'), alertLevel.el, { wide: true }),
      settingRow(t('settings.alert.after'), t('settings.alert.after.desc'), alertAfter.el, { wide: true }),
    ];
    const notif = panel(t('settings.notif'), t('settings.notif.sub'));
    notif.el.classList.add('accented');
    notif.body.append(settingRow(t('settings.alert'), t('settings.alert.desc'), alertToggle.el), ...alertRows);

    // Privacy: statistiche per app e storico.
    const appStats = toggle((v) => setConfig({ app_stats_enabled: v }));
    let confirmUntil = 0, confirmTimer = 0;
    const clearBtn = h('button.btn.danger', { type: 'button' }, icon('trash'), h('span', { text: t('settings.clear') }));
    // Serve un secondo clic entro 5 s: un clic per sbaglio non cancella nulla.
    clearBtn.addEventListener('click', () => {
      if (performance.now() < confirmUntil) {
        confirmUntil = 0;
        clearTimeout(confirmTimer);
        setText(clearBtn.lastChild, t('settings.clear'));
        send({ cmd: 'clearHistory' });
        return;
      }
      confirmUntil = performance.now() + 5000;
      setText(clearBtn.lastChild, t('settings.clear.confirm'));
      clearTimeout(confirmTimer);
      confirmTimer = setTimeout(() => { confirmUntil = 0; setText(clearBtn.lastChild, t('settings.clear')); }, 5000);
    });
    const privacy = panel(t('settings.privacy'), t('settings.privacy.sub'));
    privacy.el.classList.add('accented');
    privacy.body.append(
      settingRow(t('settings.app_stats'), t('settings.app_stats.desc'), appStats.el),
      settingRow(t('settings.clear'), t('settings.clear.desc'), clearBtn));

    root.append(h('div.page', null, h('div.cols.even', null, h('div.stack', null, langPanel.el, start.el, away.el, notif.el), h('div.stack', null, look.el, privacy.el, diag.el))));
    return {
      leave() { clearTimeout(confirmTimer); },
      update() {
        const c = cfg();
        if (c) {
          language.set(c.language || 'auto');
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
    const applyAll = h('button.btn.solid', { type: 'button', on: { click: () => send({ cmd: 'windowsApplyAll' }) } }, icon('check'), t('windows.apply_all'));
    const restore = h('button.btn', { type: 'button', on: { click: () => send({ cmd: 'windowsRestoreAll' }) } }, icon('reset'), t('windows.restore'));
    const head = h('section.panel', null, h('div.score', null, score, h('span', { text: t('windows.score') }), h('span.grow'), applyAll, restore));
    const list = panel(t('windows.title'));
    root.append(h('div.page', null, head, list.el,
      h('p.hint', { text: t('windows.hint') })));
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
          if (it.managed) action = pill(t('windows.managed'));
          else if (it.ok) action = pill(t('windows.done'), 'accent');
          else action = h('button.btn.small', { type: 'button', title: t('windows.apply_one'), on: { click: () => send({ cmd: 'windowsApply', id: it.id }) } }, t(it.recommended_key, it.recommended_params));
          list.body.append(h('div.row', null,
            h('span.check', { cls: it.ok ? 'ok' : null }, it.ok ? icon('check') : null),
            h('div.label', null, h('b', { text: t(`windows.${it.id}.label`) }), h('p', { text: t('windows.now', { value: t(it.current_key, it.current_params) }) })),
            h('div.control', null, action)));
        }
      },
    };
  },
};

// ------------------------------------------------------------------ avanzate

// [chiave di config, min, max, passo, formato, predefinito]; titolo e descrizione: "advanced.<chiave>".
const secs = (v) => `${fmtNumber(v / 1000, v % 1000 ? (v % 100 ? 2 : 1) : 0)} s`;
const ADVANCED = [
  ['dim_bright_threshold_pct', 5, 100, 1, (v) => fmtPct(v), 25],
  ['dim_motion_fraction_pct', 10, 90, 1, (v) => fmtPct(v), 25],
  ['dim_recover_ms', 100, 5000, 100, secs, 500],
  ['pause_enter_delay_ms', 0, 10000, 100, secs, 300],
  ['resume_delay_ms', 0, 10000, 250, secs, 2500],
];

PAGES.avanzate = {
  build(root) {
    const reset = h('button.btn.small', { type: 'button', on: { click: () => {
      const patch = { allow_tearing: false };
      for (const [key, , , , , def] of ADVANCED) patch[key] = def;
      setConfig(patch);
    } } }, icon('reset'), t('advanced.reset'));
    const p = panel(t('advanced.title'), t('advanced.sub'), reset);
    const sliders = ADVANCED.map(([key, min, max, step, fmt]) => {
      const s = slider({ min, max, step, format: fmt, onInput: throttledConfig(key) });
      p.body.append(settingRow(t(`advanced.${key}`), t(`advanced.${key}.desc`), s.el, { wide: true, code: key }));
      return [key, s];
    });
    const tearing = toggle((v) => setConfig({ allow_tearing: v }));
    p.body.append(settingRow(t('advanced.tearing'), t('advanced.tearing.desc'), tearing.el, { code: 'allow_tearing' }));
    const diag = panel(t('advanced.diag'), t('advanced.diag.sub'));
    diag.body.append(
      settingRow(t('advanced.diag.export'), t('advanced.diag.export.desc'),
        h('button.btn', { type: 'button', on: { click: () => send({ cmd: 'exportDiagnostics' }) } }, icon('doc'), t('advanced.diag.button'))),
      h('p.hint.box', { text: t('advanced.diag.privacy') }));
    root.append(h('div.page', null, h('p.hint.box', { text: t('advanced.hint') }), diag.el, p.el));
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

// Novita' per versione: [versione, numero di voci]; testi "about.changes.<versione>.<n>".
const CHANGES = [['1.2.0', 7], ['1.1.0', 5], ['1.0.0', 7]];

PAGES.informazioni = {
  build(root) {
    const version = h('span.mono.accent');
    const intro = h('section.panel', null, h('div.panel-body.pad', null, h('div.about', null, logo(),
      h('div', null, h('h2', null, 'OLED Guard ', version),
        h('p', { text: t('about.intro') }),
        h('p.hint', { text: t('about.panel_care') }),
        h('p.hint', { text: t('about.browser_video') })))));
    const news = panel(t('shell.news'));
    news.body.classList.add('pad');
    news.body.append(h('ul.changes', null, CHANGES.map(([v, n]) => h('li', null, h('h5', { text: `v${v}` }),
      h('ul', null, Array.from({ length: n }, (_, i) => h('li', { text: t(`about.changes.${v}.${i + 1}`) })))))));
    const files = panel(t('about.files'));
    const cfgPath = h('div.path'), logPath = h('div.path');
    files.body.append(
      settingRow(t('notice.config.title'), cfgPath, h('span')),
      settingRow(t('about.log'), logPath, h('button.btn.small', { type: 'button', on: { click: () => send({ cmd: 'openLogFolder' }) } }, icon('folder'), t('common.open'))),
      settingRow('Microsoft Edge WebView2', t('about.webview2'), h('span')),
      settingRow(t('about.license'), t('about.license.desc'), h('span')));
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
