// Modalita' anteprima: dati finti e animati quando la pagina e' aperta in un browser
// (senza WebView2). Nell'app reale questo file non fa nulla.
'use strict';

if (preview) {
  const now = () => Date.now();
  window.Mock = {
    setConfigCount: 0,  // per verificare il limite di frequenza dei cursori
    config: {
      monitor: 'auto', amplitude_px: 2, dwell_min_s: 20, dwell_max_s: 60, transition_min_s: 2, transition_max_s: 5,
      pause_on_notification_state: true, pause_on_fullscreen_window: true, pause_enter_delay_ms: 300, resume_delay_ms: 2500,
      denylist: ['netflix.exe'], allowlist: ['vlc.exe'], hotkey_toggle: 'Ctrl+Alt+Shift+P', hotkey_emergency_exit: 'Ctrl+Alt+Shift+F12',
      hotkey_display_off: 'Ctrl+Alt+Shift+O', allow_tearing: false, autostart: true, log_level: 'info',
      dim_enabled: true, dim_strength_pct: 40, dim_delay_s: 60, dim_ramp_s: 120, dim_recover_ms: 500,
      dim_window_mode: 'desktop', dim_activate: 'both',
      dim_large_enabled: false, dim_large_delay_s: 1200, dim_large_strength_pct: 20, dim_bright_threshold_pct: 25,
      dim_motion_fraction_pct: 25, dim_exclude: ['photoshop.exe'],
      brightness_pct: 100, vignette_enabled: true, vignette_strength_pct: 50, vignette_left_pct: 8, vignette_right_pct: 8,
      vignette_top_pct: 0, vignette_bottom_pct: 12, away_enabled: false, away_after_min: 10, away_brightness_pct: 30,
      ui_accent: 'teal', ui_text_size: 'normal', app_list: ['code.exe'],
      alert_enabled: true, alert_threshold: 'medium', alert_after_min: 60, app_stats_enabled: true,
    },
    history: [
      { exe: 'cyberpunk2077.exe', reason: 'Gioco o app a schermo intero', last_seen_s: now() / 1000 - 30, count: 12 },
      { exe: 'chrome.exe', reason: 'Finestra a tutto schermo', last_seen_s: now() / 1000 - 86400 - 3000, count: 4 },
      { exe: 'vlc.exe', reason: 'Finestra a tutto schermo', last_seen_s: now() / 1000 - 90000, count: 2 },
      { exe: 'netflix.exe', reason: 'Pausa manuale', last_seen_s: now() / 1000 - 400000, count: 1 },
    ],
    monitorList: [
      { device: '\\\\.\\DISPLAY1', name: 'LG TV SSCR2', width: 3840, height: 2160, refresh_hz: 144, hdr: true, primary: true, connection: 'HDMI' },
      { device: '\\\\.\\DISPLAY2', name: 'DELL U2723QE', width: 2560, height: 1440, refresh_hz: 60, hdr: false, primary: false, connection: 'DP' },
    ],
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
    offset: [0, 0], trail: [[0, 0]], paused: false, manual: false, testing: 0, next: 6000, t0: now(),
    away: false, highlightUntil: 0, dimT: 0,
    cells: Array.from({ length: 49 }, (_, i) => { const x = i % 7 - 3, y = Math.floor(i / 7) - 3; return Math.abs(x) <= 2 && Math.abs(y) <= 2 ? 3e6 + Math.random() * 1.2e6 : 0; }),
    activity: [],

    start() {
      this.log('resume', 'Protezione attiva', now() - 3600000);
      this.log('pause', 'In pausa: Gioco o app a schermo intero · cyberpunk2077.exe', now() - 3000000);
      this.log('resume', 'Protezione attiva', now() - 1500000);
      this.log('test', 'Prova dello spostamento (30 s)', now() - 600000);
      receive({ type: 'config', config: this.config });
      receive({ type: 'about', version: '1.1.0', config_path: 'C:\\Users\\utente\\AppData\\Roaming\\oled-guard\\config.json', log_dir: 'C:\\Users\\utente\\AppData\\Roaming\\oled-guard\\logs' });
      receive({ type: 'activity', events: this.activity });
      this.sendApps();
      this.sendMonitors();
      this.sendWindows();
      this.sendStats();
      this.sendSignalsHistory();
      this.sendHistory();
      this.step();
      setInterval(() => this.sendSignal(), 1000);
      setInterval(() => this.sendHistory(), 60000);
      setInterval(() => this.step(), 250);
      setInterval(() => this.sendStats(), 5000);
      setInterval(() => this.sendDim(), 1000);
    },
    log(kind, text, t = now()) {
      const e = { t, kind, text, count: 1 };
      this.activity = [e, ...this.activity].slice(0, 100);
      receive({ type: 'activity', add: e });
    },
    rule(exe) {
      if (this.config.allowlist.includes(exe)) return 'never';
      if (this.config.denylist.includes(exe)) return 'always';
      return 'auto';
    },
    sendApps() {
      const c = this.config, seen = new Set();
      const apps = this.history.map((e) => { seen.add(e.exe); return { ...e, rule: this.rule(e.exe), dim_exclude: c.dim_exclude.includes(e.exe) }; });
      for (const list of [c.denylist, c.allowlist, c.app_list, c.dim_exclude]) {
        for (const exe of list) {
          if (seen.has(exe)) continue;
          seen.add(exe);
          apps.push({ exe, reason: '', last_seen_s: 0, count: 0, rule: this.rule(exe), dim_exclude: c.dim_exclude.includes(exe) });
        }
      }
      receive({ type: 'apps', apps });
    },
    protectedDevice() {
      const m = this.monitorList.find((x) => x.device === this.config.monitor || x.name === this.config.monitor);
      return (m || this.monitorList.find((x) => x.primary)).device;
    },
    sendMonitors() {
      const dev = this.protectedDevice();
      receive({ type: 'monitors', setting: this.config.monitor, monitors: this.monitorList.map((m) => ({ ...m, protected: m.device === dev })) });
    },
    sendWindows() {
      const items = this.windows.map(([id, label, current, recommended, ok]) => ({ id, label, current, recommended, ok, restorable: false, managed: false }));
      receive({ type: 'windows', items, score: items.filter((i) => i.ok).length, total: items.length });
    },
    sendStats() {
      const by = { 'Gioco o app a schermo intero': 2, 'Finestra a tutto schermo': 1 };
      receive({ type: 'stats', cells: this.cells, uniformity: 96.2, today_ms: 22320000 + (now() - this.t0), total_ms: 511200000, pauses_today: 3, pauses_by_reason: by });
    },
    signalAt(t) {
      const s = t / 1000;
      const motion = Math.max(0, 0.09 + 0.05 * Math.sin(s / 3) + (Math.sin(s / 11) > 0.8 ? 0.2 : 0));
      return { risk: 0.22 + 0.04 * Math.sin(s / 7), motion, brightness: 0.41 + 0.03 * Math.sin(s / 5) };
    },
    sendSignal() {
      if (this.paused) { receive({ type: 'signals', measured: false, risk: 0, motion: 0, brightness: 0, caption: '' }); return; }
      const v = this.signalAt(now());
      const caption = v.motion < 0.02 ? 'interfaccia ferma' : (v.motion <= 0.15 ? 'lavoro' : 'video o animazioni');
      receive({ type: 'signals', measured: true, ...v, caption });
    },
    sendSignalsHistory() {
      const t = now();
      const history = [];
      for (let i = 59; i >= 0; i--) {
        if (i > 30 && i < 38) { history.push(null); continue; }  // breve pausa: buco nella linea
        const v = this.signalAt(t - i * 1000);
        history.push([v.risk, v.motion, v.brightness]);
      }
      receive({ type: 'signals', history });
    },
    sendHistory() {
      const end = Math.floor(now() / 1000) - new Date().getTimezoneOffset() * 60;
      const timeline = [];
      for (let i = 2015; i >= 0; i--) {
        const s = end - i * 300;
        const hour = Math.floor((s % 86400) / 3600);
        if (hour < 8) { timeline.push(null); continue; }  // PC spento di notte
        const x = s / 3600;
        timeline.push(Math.max(0, 0.3 + 0.25 * Math.sin(x / 2.3) + 0.1 * Math.sin(x * 1.7)));
      }
      const cells = [];
      for (let y = 0; y < 18; y++) {
        for (let x = 0; x < 32; x++) {
          let v = 0.15 + 0.1 * Math.random();
          if (x < 5 && y < 6) v = 0.75 + 0.25 * Math.random();   // icone in alto a sinistra
          if (x > 26 && y < 2) v = 0.6 + 0.2 * Math.random();    // orologio
          if (y >= 17) v = 0.5 + 0.2 * Math.random();            // barra delle applicazioni
          if (x > 10 && x < 22 && y > 12 && y < 16) v = 0.45;
          cells.push(v);
        }
      }
      const max = Math.max(...cells);
      receive({
        type: 'history', timeline, timeline_end: end, risk_peak_24h: 0.82,
        wear: {
          monitor: 'LG TV SSCR2', cells: cells.map((v) => v / max), hottest: 0, hottest_hours: 38.5,
          hottest_label: 'in alto a sinistra', concentration: 1.86, over_double: 14, measured_hours: 142,
          since: end - 45 * 86400,
        },
        top_apps: [
          { exe: 'code.exe', protected_s: 51720, paused_s: 0, rule: 'auto' },
          { exe: 'chrome.exe', protected_s: 32400, paused_s: 7200, rule: 'auto' },
          { exe: 'cyberpunk2077.exe', protected_s: 0, paused_s: 30600, rule: 'auto' },
          { exe: 'discord.exe', protected_s: 14400, paused_s: 0, rule: 'never' },
          { exe: 'vlc.exe', protected_s: 3600, paused_s: 5400, rule: 'never' },
        ],
        content: [0.42, 0.24, 0.18, 0.11, 0.05],
        sessions_total: 47, sessions_week: 3, streak: 12, streak_best: 19,
        avoided_today: 0.31, avoided_total: 0.27, protected_week_s: 18720,
      });
    },
    sendDim() {
      if (!dimWatching) return;
      this.dimT++;
      const cols = 64, rows = 36;
      let cells = '';
      for (let y = 0; y < rows; y++) {
        for (let x = 0; x < cols; x++) {
          let v = 0;
          if (y >= 34) v = 10;
          if (x < 3 && y < 20 && y % 3 !== 2) v = 12;
          if (x > 55 && y < 3) v = 14;
          if (x > 20 && x < 44 && y > 8 && y < 26) v = 0;
          cells += v.toString(16);
        }
      }
      receive({
        type: 'dim', status: this.paused ? 'paused' : (this.config.dim_enabled ? 'active' : 'off'), suspended_by: '',
        cols, rows, cells, dimmed_pct: 11.4, max_level: 0.4, sdr_white_nits: 240, hdr: true,
        highlight_left_ms: Math.max(0, this.highlightUntil - now()),
      });
    },
    step() {
      const t = now();
      if (!this.paused) {
        this.next -= 250;
        if (this.next <= 0) {
          const a = this.config.amplitude_px;
          const r = () => Math.round(Math.random() * 2 * a) - a;
          this.offset = [r(), r()];
          this.trail = [...this.trail, this.offset].slice(-4);
          this.next = this.testing > t ? 1500 : 20000 + Math.random() * 40000;
        }
      }
      const paused = this.manual;
      if (paused !== this.paused) {
        this.paused = paused;
        this.log(paused ? 'manual' : 'resume', paused ? 'In pausa: Pausa manuale' : 'Protezione attiva');
      }
      receive({
        type: 'state', paused: this.paused, manual_pause: this.manual, testing_shift: this.testing > t,
        reason: this.paused ? 'Pausa manuale' : 'Nessuna', foreground: 'chrome.exe', offset: this.offset, trail: this.trail,
        next_move_ms: this.next, amplitude: this.config.amplitude_px, monitor: 'LG TV SSCR2', width: 3840, height: 2160,
        hdr: true, latency_ms: 6.9, away: this.away, session_ms: t - this.t0 + 2852000,
      });
    },
    command(cmd) {
      const c = this.config;
      switch (cmd.cmd) {
        case 'ready': break;
        case 'setConfig':
          this.setConfigCount++;
          Object.assign(c, cmd.patch);
          receive({ type: 'config', config: c });
          if ('dim_exclude' in cmd.patch) this.sendApps();
          if ('monitor' in cmd.patch) this.sendMonitors();
          break;
        case 'setManualPause': this.manual = cmd.on; this.step(); break;
        case 'testShift': this.testing = now() + 30000; this.next = 500; this.log('test', 'Prova dello spostamento (30 s)'); break;
        case 'setAppRule':
          c.denylist = c.denylist.filter((e) => e !== cmd.exe);
          c.allowlist = c.allowlist.filter((e) => e !== cmd.exe);
          if (cmd.rule === 'always') c.denylist.push(cmd.exe);
          if (cmd.rule === 'never') c.allowlist.push(cmd.exe);
          receive({ type: 'config', config: c });
          this.sendApps();
          break;
        case 'addApp':
          if (!c.app_list.includes(cmd.exe.toLowerCase())) c.app_list.push(cmd.exe.toLowerCase());
          receive({ type: 'config', config: c });
          this.sendApps();
          break;
        case 'removeApp':
          for (const k of ['app_list', 'denylist', 'allowlist', 'dim_exclude']) c[k] = c[k].filter((e) => e !== cmd.exe);
          this.history = this.history.filter((e) => e.exe !== cmd.exe);
          receive({ type: 'config', config: c });
          this.sendApps();
          break;
        case 'setDimExclude':
          c.dim_exclude = c.dim_exclude.filter((e) => e !== cmd.exe);
          if (cmd.on) c.dim_exclude.push(cmd.exe.toLowerCase());
          receive({ type: 'config', config: c });
          this.sendApps();
          break;
        case 'listProcesses': receive({ type: 'processes', processes: ['chrome.exe', 'code.exe', 'discord.exe', 'explorer.exe', 'spotify.exe', 'steam.exe'] }); break;
        case 'browseExe': setTimeout(() => receive({ type: 'browsed', exe: 'eldenring.exe' }), 400); break;
        case 'setMonitor':
          c.monitor = cmd.device;
          receive({ type: 'config', config: c });
          this.sendMonitors();
          this.log('monitor', 'Schermo protetto: ' + (this.monitorList.find((m) => m.device === this.protectedDevice()) || {}).name);
          break;
        case 'clearHistory':
          this.clearedHistory = (this.clearedHistory || 0) + 1;
          receive({ type: 'notice', text: 'Anteprima: storico cancellato.' });
          break;
        case 'clearActivity': this.activity = []; receive({ type: 'activity', events: [] }); break;
        case 'openLogFolder': receive({ type: 'notice', text: 'Anteprima: qui si aprirebbe la cartella dei log.' }); break;
        case 'windowsApply': this.windows = this.windows.map((w) => (w[0] === cmd.id ? [w[0], w[1], w[3], w[3], true] : w)); this.sendWindows(); break;
        case 'windowsApplyAll': this.windows = this.windows.map((w) => [w[0], w[1], w[3], w[3], true]); this.sendWindows(); break;
        case 'windowsRestoreAll': receive({ type: 'notice', text: 'Anteprima: impostazioni ripristinate.' }); break;
        case 'displayOff': receive({ type: 'notice', text: 'Anteprima: lo schermo si spegnerebbe ora.' }); break;
        case 'dimHighlight': this.highlightUntil = now() + 15000; this.sendDim(); break;
        default: break;
      }
    },
  };
}
