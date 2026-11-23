// Procedura di primo avvio: lingua, schermo da proteggere, cosa succede ora. Copre
// l'interfaccia finche' lo stato dice setup_pending; la protezione parte solo alla fine.
'use strict';

const LANG_NAMES = { en: 'English', it: 'Italiano', es: 'Español', de: 'Deutsch', fr: 'Français' };
const setup = { step: 0, device: null, autostart: false, built: '' };

function setupMonitors() { return (store.monitors && store.monitors.monitors) || []; }

// Schermo scelto: quello cliccato, altrimenti quello che la regola automatica protegge.
function setupDevice() {
  const list = setupMonitors();
  if (setup.device && list.some((m) => m.device === setup.device)) return setup.device;
  const chosen = list.find((m) => m.protected) || list.find((m) => m.primary) || list[0];
  return chosen ? chosen.device : 'auto';
}

function connectionLabel(c) { return c === 'internal' ? t('monitor.connection.internal') : c; }

function setupButtons(...buttons) { return h('div.setup-actions', null, h('span.grow'), ...buttons); }

function setupLanguageStep() {
  const current = (store.config && store.config.language) || 'auto';
  const pick = (id) => send({ cmd: 'setLanguage', id });
  const options = [['auto', t('setup.language.auto')], ...Object.entries(LANG_NAMES)];
  return [
    h('h2', { text: t('setup.language.title') }),
    h('p.lead', { text: t('setup.intro') }),
    h('div.choices.setup-langs', null, ...options.map(([id, label]) =>
      h('button.choice', { type: 'button', cls: id === current ? 'on' : null, on: { click: () => pick(id) } }, h('b', { text: label })))),
    setupButtons(h('button.btn.solid', { type: 'button', on: { click: () => { setup.step = 1; renderSetup(); } } }, t('setup.next'))),
  ];
}

function setupMonitorStep() {
  const device = setupDevice();
  const list = setupMonitors();
  const cards = list.map((m, i) => {
    const details = [`${m.width}×${m.height}`, m.refresh_hz ? `${m.refresh_hz} Hz` : null, m.hdr ? 'HDR' : null,
      m.connection ? connectionLabel(m.connection) : null, m.primary ? t('setup.monitor.primary') : null].filter(Boolean);
    return h('button.choice.setup-monitor', { type: 'button', cls: m.device === device ? 'on' : null,
      on: { click: () => { setup.device = m.device; renderSetup(); } } },
    h('span.setup-num', { text: String(i + 1) }), h('b', { text: m.name || m.device }), h('small', { text: details.join(' · ') }));
  });
  return [
    h('h2', { text: t('setup.monitor.title') }),
    h('p.lead', { text: t('setup.monitor.intro') }),
    list.length ? h('div.setup-monitors', null, ...cards) : h('p.hint.box', { text: t('setup.monitor.none') }),
    setupButtons(
      h('button.btn', { type: 'button', on: { click: () => send({ cmd: 'identifyMonitors' }) } }, icon('monitor'), t('setup.monitor.identify')),
      h('button.btn', { type: 'button', on: { click: () => { setup.step = 0; renderSetup(); } } }, t('setup.back')),
      h('button.btn.solid', { type: 'button', on: { click: () => { setup.device = device; setup.step = 2; renderSetup(); } } }, t('setup.next'))),
  ];
}

function setupFinishStep() {
  const auto = toggle((v) => { setup.autostart = v; auto.set(v); });
  auto.set(setup.autostart);
  const keys = (store.config && store.config.hotkey_emergency_exit) || '';
  const finish = () => send({ cmd: 'finishSetup', device: setupDevice(), autostart: setup.autostart });
  return [
    h('h2', { text: t('setup.finish.title') }),
    h('ul.setup-points', null,
      h('li', { text: t('setup.finish.shift') }),
      h('li', { text: t('setup.finish.dim') }),
      h('li', { text: t('setup.finish.pause') }),
      h('li', { text: t('setup.finish.emergency', { keys }) })),
    settingRow(t('setup.finish.autostart'), t('setup.finish.autostart_desc'), auto.el),
    setupButtons(
      h('button.btn', { type: 'button', on: { click: () => { setup.step = 1; renderSetup(); } } }, t('setup.back')),
      h('button.btn.solid', { type: 'button', on: { click: finish } }, icon('play'), t('setup.finish.start'))),
  ];
}

function renderSetup() {
  const on = !!(store.state && store.state.setup_pending);
  const root = $('setup');
  root.hidden = !on;
  if (!on) { setup.built = ''; return; }
  // Si ridisegna solo quando cambia qualcosa che la procedura mostra.
  const key = [setup.step, setup.device, setup.autostart, store.lang, store.config && store.config.language,
    store.config && store.config.hotkey_emergency_exit, JSON.stringify(setupMonitors())].join('|');
  if (key === setup.built) return;
  setup.built = key;
  const steps = [setupLanguageStep, setupMonitorStep, setupFinishStep];
  root.innerHTML = '';
  root.append(h('div.setup-card', null,
    h('div.setup-head', null, logo(), h('b', { text: 'OLED Guard' }), h('span.grow'),
      h('small', { text: t('setup.step', { n: setup.step + 1, total: steps.length }) })),
    ...steps[setup.step]()));
}
