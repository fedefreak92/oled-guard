# Altri schermi, lingue e Windows 10 (versione 1.2.0) — piano di implementazione

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** rendere OLED Guard usabile da chi ha un monitor o una TV OLED qualsiasi, su
Windows 10 (2004+) e 11, in inglese, italiano, spagnolo, tedesco e francese, e
pubblicarla come 1.2.0.

**Architecture:** un modulo di traduzioni in `src/core` (dizionari JSON, ripiego
sull'inglese, plurali) letto dal C++ e passato alla pagina con un messaggio `strings`;
tutti i testi per l'utente diventano chiavi. Le novita' di comportamento (finestre
protette, primo avvio, schermo mancante, griglia delle zone per proporzioni,
diagnostica) hanno la loro logica pura in `src/core`, testata, e un sottile strato
Win32 in `src/app`.

**Tech Stack:** C++20, MSVC, CMake + Ninja, D3D11 (cs_5_0 con fxc), WebView2 + HTML/CSS/JS vanilla, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-11-23-oled-guard-multi-setup-design.md`

## Global Constraints

- Logica pura in `src/core` senza header di Windows; Win32/D3D in `src/app`; UI in `src/ui` (CSP `default-src 'self'`, niente inline, niente librerie, niente rete).
- Commenti in italiano, identificatori in inglese, **log in italiano**; ogni testo per l'utente passa da una chiave di traduzione. Nessuna dipendenza nuova.
- Lingue: `en`, `it`, `es`, `de`, `fr`; file `src/ui/i18n/<lingua>.json`; ripiego: lingua scelta → `en` → chiave (con avviso nel log). `_meta.review`: `"native"` per en e it, `"machine"` per es, de, fr.
- Windows minimo: build **19041** (Windows 10 2004). Runtime C++ statico: l'exe non dipende da `MSVCP*`/`VCRUNTIME*`.
- Nessun nome di marca o modello nei testi e nelle regole; "lo schermo", mai "la TV" da solo.
- Versione **1.2.0**. Ogni task: `build.ps1` (o `build-check`) verde e commit con trailer `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Script di modifica con Python: `newline=''`.
- Prove a runtime: avvisare l'utente prima; copia e ripristino di `config.json`, `stats.json`, `windows-backup.json`, `history.json` e della chiave Run **con processi lanciati fuori dal pacchetto dell'app Claude** (`Invoke-CimMethod Win32_Process -MethodName Create`, script in `build-check\prova-tv-*.ps1`); se `build\oled-guard.exe` e' in uso compilare in `build-check`. Ogni punto della checklist si conferma dal log (orari, eventi) prima di dirlo fatto.
- Verifica dell'interfaccia: anteprima `ui-anteprima`, console pulita dopo ricaricamento senza cache, 1 280×800 e 1 000×640 senza scorrimento orizzontale.

## Review Focus

1. **Finestra protetta ridotta a icona, nascosta ("cloaked", es. su un altro desktop virtuale) o sull'altro schermo** → nessuna pausa; la pausa scatta solo se e' visibile sullo schermo protetto (Task 6 `pause_protected_window_needs_overlap`; verifica dal vivo nel Task 22).
2. **Cambio di lingua a meta' sessione** → attivita' recente, tray e pagina tutte nella nuova lingua, senza riavvio (Task 3 `activity_stores_key_and_params`, Task 12 verifica in anteprima).
3. **Primo avvio senza WebView2** (Windows 10 senza runtime) → la protezione parte comunque con la regola automatica e una notifica lo dice (Task 9 `startup_without_ui_starts_protection`).
4. **Utente della 1.1.0 con `history.json` v1 su uno schermo 16:9** → la mappa resta, non riparte da zero (Task 15 `history_v1_map_kept_on_same_grid`).
5. **Chiave mancante in tedesco** → testo inglese, mai vuoto ne' chiave a vista (Task 2 `i18n_missing_key_falls_back_to_english`).

---

## Milestone M0 — verifica preliminare

### Task 1: Prova delle finestre protette e nomi delle app di streaming (usa-e-getta)

**Files:**
- Create (non nel repository, in `build-check\probe\`): una piccola console C++ che elenca le finestre di primo livello visibili con processo, classe e `GetWindowDisplayAffinity`; una finestra di prova WinForms compilata con `csc` (.NET 4) che chiama `SetWindowDisplayAffinity(handle, 0x11)` (WDA_EXCLUDEFROMCAPTURE) e un'altra con `0x01` (WDA_MONITOR).
- Modify: `docs/superpowers/specs/2026-11-23-oled-guard-multi-setup-design.md` (§6.3, nuovo paragrafo "Esito del task 1").

- [ ] **Step 1:** Avvisare l'utente (compaiono due piccole finestre di prova), avviarle, eseguire la console: la riga di ciascuna deve riportare 0x11 e 0x01; una finestra normale 0x0.
- [ ] **Step 2:** Se l'utente usa KeePassXC o un'app simile, ripetere con quella aperta.
- [ ] **Step 3:** Nomi dei processi di Netflix, Disney+, Prime Video e Apple TV per Windows: documentazione e pagine del Microsoft Store; installarle solo con l'ok esplicito dell'utente. Annotare per ciascuna il nome del `.exe` e se e' un'app UWP (processo dietro `ApplicationFrameHost.exe`).
- [ ] **Step 4:** Scrivere l'esito nello spec. **Se `GetWindowDisplayAffinity` non legge le finestre di altri processi: fermarsi e riferire all'utente** prima dei Task 6-7 (ripiego: solo elenco per nome).
- [ ] **Step 5: Commit** — `docs: esito della prova sulle finestre protette`

## Milestone M1 — traduzioni

### Task 2: Dizionari, ripiego, plurali e scelta della lingua (core)

**Files:**
- Create: `src/core/i18n.h`, `src/core/i18n.cpp`, `tests/test_i18n.cpp`, `src/ui/i18n/en.json`, `src/ui/i18n/it.json` (per ora con `_meta` e le chiavi dei test)
- Modify: `CMakeLists.txt` (sorgenti; `target_compile_definitions(oled-guard-tests PRIVATE OLED_SOURCE_DIR="${CMAKE_SOURCE_DIR}")`)

**Interfaces:**
- Produces:
  ```cpp
  inline constexpr std::array<std::string_view, 5> kLanguages = {"en", "it", "es", "de", "fr"};
  using I18nParams = std::vector<std::pair<std::string, std::string>>;
  // "auto": prima lingua di `preferred` (es. "de-AT") supportata per prefisso, altrimenti "en".
  // Codice supportato: quello. Altro: "en".
  std::string PickLanguage(std::string_view setting, std::span<const std::string> preferred);
  bool PluralIsOne(std::string_view lang, double n);  // fr: n < 2; altre: n == 1
  class Translations {
   public:
    // `active` e `english`: oggetti JSON piatti (chiave -> stringa), "_meta" ignorato.
    static Translations FromJson(std::string lang, const JsonValue& active, const JsonValue& english);
    const std::string& Language() const;
    std::string Get(std::string_view key) const;                          // ripiego en, poi la chiave
    std::string Format(std::string_view key, const I18nParams& p) const;  // sostituisce {nome}
    std::string Plural(std::string_view key, double n, I18nParams p = {}) const;  // key_one/key_other, {n}
    JsonValue Dictionary() const;  // en sovrascritto dalla lingua attiva, senza _meta
    std::vector<std::string> MissingKeys() const;  // chiavi di en assenti nella lingua attiva
  };
  // Differenze fra due dizionari: chiavi mancanti o in piu', segnaposto diversi. Vuoto = uguali.
  std::vector<std::string> CompareDictionaries(const JsonValue& reference, const JsonValue& other);
  ```
- [ ] **Step 1: Test che falliscono** — `i18n_pick_language` (`"auto"` con `{"de-AT","en-US"}` → `"de"`; con `{"pt-BR"}` → `"en"`; `"fr"` → `"fr"`; `"xx"` → `"en"`), `i18n_plural_rules` (fr 0 e 1 one, 2 other; it 1 one, 0 other), `i18n_missing_key_falls_back_to_english` (chiave solo in en → testo en; in nessuno → la chiave), `i18n_format_params` (`"{n} min di {app}"` → `"5 min di code.exe"`; segnaposto senza valore resta invariato), `i18n_plural_picks_form`, `i18n_compare_dictionaries` (chiave mancante, chiave in piu', `{n}` contro `{count}`), `i18n_files_match_english` (legge `OLED_SOURCE_DIR "/src/ui/i18n/*.json"`: ogni file presente ha `_meta.name`, `_meta.review` valido e `CompareDictionaries(en, file)` vuoto).
- [ ] **Step 2-4:** fallire, implementare, verdi.
- [ ] **Step 5: Commit** — `feat(core): traduzioni con ripiego sull'inglese e plurali`

### Task 3: Messaggi con chiavi: pause, attivita', avvisi, Windows per OLED, config 1.2.0

**Files:**
- Modify: `src/core/pause_rules.*`, `src/core/activity_log.*`, `src/core/ui_protocol.*`, `src/core/config.*`, `CMakeLists.txt` (versione 1.2.0), test esistenti di questi moduli; in `src/app/app.cpp` e `src/app/windows_settings.cpp` solo i punti di chiamata delle funzioni cambiate (chiavi al posto dei testi), perche' la build resti verde

**Interfaces:**
- Consumes: `Translations`, `I18nParams` (Task 2).
- Produces:
  ```cpp
  std::string_view PauseReasonKey(PauseReason r);  // "pause.reason.manual", ... ; PauseReasonText resta per il log
  struct ActivityEvent { int64_t t_ms = 0; std::string kind; std::string key; I18nParams params; int count = 1; };
  const ActivityEvent& ActivityLog::Add(int64_t t_ms, std::string_view kind, std::string_view key, I18nParams params = {});
  // fusione: stesso kind, key e params entro kMergeMs
  struct WindowsItem { std::string id; std::string current_key; I18nParams current_params;
                       std::string recommended_key; I18nParams recommended_params;
                       bool ok = false, restorable = false, managed = false; };  // etichetta: "windows.<id>.label"
  std::string StringsMessage(const Translations& t);  // {"type":"strings","lang":..,"strings":{..}}
  std::string NoticeMessage(std::string_view key, const I18nParams& p = {});
  // Config: std::string language = "auto"; (auto | en | it | es | de | fr, altro -> default con avviso)
  // UiCommandType::SetLanguage (id = codice o "auto")
  ```
  Le chiavi usate qui finiscono in `en.json` e `it.json`.
- [ ] **Step 1: Test che falliscono** — `pause_reason_keys_exist` (ogni `PauseReason` ha una chiave presente in `en.json`), `activity_stores_key_and_params` (due Add uguali entro 5 s → uno con count 2; params diversi → due), `ui_activity_message_has_keys` (il messaggio porta `key` e `params`, nessun campo `text`), `ui_strings_message`, `ui_notice_message_key`, `ui_windows_message_keys`, `config_language_roundtrip_and_invalid`, `ui_parse_set_language` (`"auto"`, `"de"`; `"pt"` rifiutato).
- [ ] **Step 2-4:** fallire, implementare (aggiornare i test esistenti che leggevano `text`/`label`), verdi.
- [ ] **Step 5: Commit** — `feat(core): messaggi e attivita' con chiavi di traduzione, versione 1.2.0`

### Task 4: Traduzioni nell'app: caricamento, lingua dal vivo, testi del C++

**Files:**
- Create: `src/app/i18n_files.h`, `src/app/i18n_files.cpp`
- Modify: `src/app/app.cpp`, `src/app/app.h`, `src/app/tray.cpp`, `src/app/windows_settings.cpp`, `src/app/ui_window.cpp`, `CMakeLists.txt`

**Interfaces:**
- Consumes: Task 2-3.
- Produces:
  ```cpp
  std::vector<std::string> PreferredUiLanguages();  // GetUserPreferredUILanguages, es. {"it-IT","en-US"}
  Translations LoadTranslations(const std::filesystem::path& ui_dir, std::string_view lang);  // file illeggibile: dizionario vuoto + LogWarn
  // App: Translations tr_; std::wstring T(std::string_view key, const I18nParams& p = {}) const;
  void App::ApplyLanguage();  // ricarica tr_, invia StringsMessage, ricostruisce la tray, LogInfo con MissingKeys().size()
  ```
- [ ] **Step 1: Implementare** — lingua scelta all'avvio prima della tray; `strings` inviato alla pagina prima dello stato iniziale al comando `ready`; `SetLanguage` salva la config e chiama `ApplyLanguage`. Tutti i testi per l'utente del C++ passano da chiavi: notifiche (`ShowNotice(key_title, key_body, params)`), menu e suggerimento della tray, motivi di pausa, finestra "Scegli l'app" (titolo e filtro), avviso del rischio, valori di Windows per OLED, config non valida, hotkey non disponibili, WebView2 assente. Chiavi in `en.json` e `it.json`.
- [ ] **Step 2: Verifica** — `rg -n 'L"[A-Z][a-z]' src/app` non trova testi per l'utente (solo classi di finestra, chiavi di registro, nomi di file); build e tutti i test verdi.
- [ ] **Step 3: Commit** — `feat(app): testi dell'app tradotti e lingua cambiabile dal vivo`

### Task 5: Traduzioni nella pagina: guscio, formati, anteprima

**Files:**
- Modify: `src/ui/app.js`, `src/ui/index.html`, `src/ui/main.js`, `src/ui/mock.js`, `src/ui/i18n/en.json`, `src/ui/i18n/it.json`

**Interfaces:**
- Consumes: messaggi `strings`, `activity` con chiavi (Task 3).
- Produces (in `app.js`, usati dai Task 9-10 e 12):
  ```js
  t(key, params)        // testo con {segnaposto}; chiave assente: inglese gia' nel dizionario, altrimenti la chiave + console.warn
  tp(key, n, params)    // plurale con Intl.PluralRules(lang): key_one / key_other
  fmtNumber(v, digits), fmtPct(v, digits), fmtMin(s), fmtHours(ms), fmtWhen(unixMs), fmtClock(ms)  // con Intl e store.lang
  // index.html: attributo data-i18n="chiave" applicato da applyStaticStrings() a ogni messaggio strings
  ```
- [ ] **Step 1: Implementare** — al messaggio `strings` si salvano lingua e dizionario e si ridisegnano guscio e pagina corrente; barra laterale, barra di stato, striscia dei segnali, attivita' recente (`t(e.key, e.params)`), notifiche (`notice` con chiave). In anteprima `mock.js` legge `i18n/<lingua>.json` con `fetch` (lingua da `?lang=`, default `it`).
- [ ] **Step 2: Verifica in anteprima** — `?lang=en` e `?lang=it`: guscio e Panoramica nella lingua giusta, numeri con il separatore giusto (en `12.5%`, it `12,5%`), console pulita.
- [ ] **Step 3: Commit** — `feat(ui): guscio e formati tradotti`

## Milestone M2 — finestre protette

### Task 6: Regola di pausa per finestre protette (core)

**Files:**
- Modify: `src/core/pause_rules.*`, `src/core/config.*`, `tests/test_pause_rules.cpp`, `tests/test_config.cpp`, `src/ui/i18n/en.json`, `src/ui/i18n/it.json`

**Interfaces:**
- Produces:
  ```cpp
  // PauseInputs: bool protected_window = false; std::string protected_process;
  // PauseReason::ProtectedWindow — dopo MonitorMissing, prima di allowlist/denylist; immediata; non CausedByApp.
  // PauseRulesConfig / Config: bool pause_on_protected_window = true;
  // Config: denylist predefinita = nomi verificati nel Task 1.
  // Area minima: l'intersezione finestra-schermo e' almeno 64x64 px.
  bool OverlapsEnough(ScreenRect window, ScreenRect monitor, long min_side = 64);
  ```
- [ ] **Step 1: Test che falliscono** — `pause_protected_window` (protetta → `ProtectedWindow`; con l'impostazione spenta → `None`; allowlist del processo in primo piano non la scavalca), `pause_protected_window_is_immediate_not_app`, `pause_protected_window_needs_overlap` (finestra fuori dallo schermo, 63×200 dentro, 64×64 dentro → solo l'ultima), `config_protected_window_default_and_denylist` (default true; denylist predefinita = nomi del Task 1).
- [ ] **Step 2-4:** fallire, implementare, verdi.
- [ ] **Step 5: Commit** — `feat(core): pausa per le finestre escluse dalla cattura`

### Task 7: Rilevamento delle finestre protette e processi UWP (app + pagina)

**Files:**
- Modify: `src/app/pause_monitor.cpp`, `src/app/pause_monitor.h`, `src/app/app.cpp`, `src/ui/pages.js` (interruttore nella pagina Profili app), `src/ui/i18n/en.json`, `src/ui/i18n/it.json`

**Interfaces:**
- Consumes: Task 6.
- Produces:
  ```cpp
  // Nome del processo "vero" di una finestra: per ApplicationFrameHost.exe quello della figlia
  // Windows.UI.Core.CoreWindow (pid diverso), altrimenti quello della finestra. Minuscolo, UTF-8.
  std::string WindowProcessName(HWND hwnd);
  // Prima finestra visibile, non iconica, non cloaked (DWMWA_CLOAKED), non dell'app, che OverlapsEnough
  // lo schermo e ha GetWindowDisplayAffinity != WDA_NONE. nullopt se nessuna.
  std::optional<std::string> FindProtectedWindow(const RECT& monitor);
  ```
  Collect usa `WindowProcessName` anche per `foreground_process` (regole per app e statistiche). Attivita': chiave `activity.pause_protected` con `{app}`.
- [ ] **Step 1: Implementare.**
- [ ] **Step 2: Build e tutti i test verdi**; dal vivo con la finestra di prova del Task 1 (avvisare l'utente, file copiati fuori dal pacchetto): pausa entro 1 s all'apertura, ripresa dopo la chiusura col ritardo normale, nessuna pausa con la finestra ridotta a icona; attivita' con il nome del processo. Log a conferma.
- [ ] **Step 3: Commit** — `feat(app): pausa automatica con finestre protette visibili`

## Milestone M3 — compatibilita'

### Task 8: Runtime statico, controllo di Windows, WebView2 assente, zip di release

**Files:**
- Create: `src/core/winver.h` (header-only), `tests/test_winver.cpp`
- Modify: `CMakeLists.txt`, `src/app/main.cpp`, `src/app/ui_window.cpp`, `.github/workflows/ci.yml`, `src/ui/i18n/en.json`, `src/ui/i18n/it.json`

**Interfaces:**
- Produces:
  ```cpp
  inline constexpr uint32_t kMinWindowsBuild = 19041;
  constexpr bool IsSupportedWindowsBuild(uint32_t build) { return build >= kMinWindowsBuild; }
  ```
  CMake: `set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")` prima dei target; `OLED_GUARD_COMMIT` dal `git rev-parse --short HEAD` in configurazione (`"unknown"` se fallisce).
- [ ] **Step 1: Test che fallisce** — `winver_minimum` (19040 no, 19041 si', 26300 si').
- [ ] **Step 2: Implementare** — in `main.cpp`, prima di tutto il resto, build letta con `RtlGetVersion` (da `ntdll.dll` con `GetProcAddress`); sotto il minimo `MessageBoxW` con `startup.windows_too_old` dalla lingua di Windows (testo inglese fisso nel codice se i file non si leggono) e uscita 1. Notifica WebView2 assente: `notice.webview2_missing` con il link `https://developer.microsoft.com/microsoft-edge/webview2/` e la frase che la protezione resta attiva dalla tray. CI: dopo la build, `dumpbin /dependents build\oled-guard.exe` non deve contenere `MSVCP` ne' `VCRUNTIME` (passo che fallisce altrimenti); zip `oled-guard-<versione>-windows-x64.zip` con `Compress-Archive` e file `.sha256` con `Get-FileHash`, caricati come artefatto.
- [ ] **Step 3: Verifica** — build e test verdi; `dumpbin /dependents` in locale come sopra; `dumpbin /imports` dell'exe: ogni funzione importata da `user32`, `dxgi`, `d3d11`, `dcomp`, `dwmapi`, `shcore`, `kernel32` esiste da Windows 10 2004 o prima (elenco annotato nel messaggio di commit; le eccezioni vanno caricate con `GetProcAddress`).
- [ ] **Step 4: Commit** — `feat: runtime statico, controllo della versione di Windows e zip di release`

## Milestone M4 — primo avvio e testi neutri

### Task 9: Regola automatica, schermo mancante, decisione d'avvio (core)

**Files:**
- Create: `src/core/startup.h`, `src/core/startup.cpp`, `tests/test_startup.cpp`
- Modify: `src/core/monitor_select.*`, `tests/test_monitor_select.cpp`, `src/core/ui_protocol.*`, `tests/test_ui_protocol.cpp`, `CMakeLists.txt`

**Interfaces:**
- Produces:
  ```cpp
  // MonitorDesc: + bool hdr = false; std::string connection;  ("HDMI" | "DP" | ...)
  // SelectMonitor "auto": primo con hdr && connection == "HDMI", poi il primario, poi il primo.
  struct StartupDecision { bool setup_pending = false; bool start_protection = false; bool notify_auto_choice = false; };
  StartupDecision DecideStartup(bool config_exists, bool ui_available);
  // true una sola volta quando lo schermo manca da >= 30 s di fila e l'app vive da >= 60 s; si riarma quando torna.
  class MissingMonitorNotifier { public: bool Update(bool missing, double uptime_ms, double now_ms); };
  // UiState: + bool setup_pending. Comandi: IdentifyMonitors; FinishSetup (id = device o "auto", on = avvio automatico).
  ```
- [ ] **Step 1: Test che falliscono** — `monitor_auto_prefers_hdr_hdmi` (TV HDR via HDMI non principale batte il principale DP; nessun HDR HDMI → principale; nessuno principale → primo; nessun riferimento a "LG TV"), `startup_first_run_waits_for_setup` (config assente + UI → setup, niente protezione), `startup_without_ui_starts_protection` (config assente, UI no → protezione + notifica), `startup_existing_config` (→ protezione, niente setup), `missing_monitor_notifies_once` (29 s no, 30 s si', di nuovo no; tornato e rimancato → di nuovo; uptime 50 s → no), `ui_parse_setup_commands`, `ui_state_setup_pending`.
- [ ] **Step 2-4:** fallire, implementare, verdi.
- [ ] **Step 5: Commit** — `feat(core): regola automatica senza marche, primo avvio e schermo mancante`

### Task 10: Primo avvio nell'app: procedura, Identifica, tray ridotta, avviso schermo mancante

**Files:**
- Create: `src/app/identify_window.h`, `src/app/identify_window.cpp`, `src/ui/setup.js`
- Modify: `src/app/app.cpp`, `src/app/app.h`, `src/app/monitors.cpp` (hdr e collegamento in `MonitorDesc`), `src/ui/index.html`, `src/ui/main.js`, `src/ui/app.css`, `src/ui/mock.js`, `src/ui/i18n/en.json`, `src/ui/i18n/it.json`, `CMakeLists.txt`

**Interfaces:**
- Consumes: Task 9, `t()` (Task 5), `MonitorsMessage`.
- Produces:
  ```cpp
  // Una finestra per schermo: topmost, trasparente ai clic, non attivabile, sfondo nero al 60%,
  // numero grande (1, 2, ...) e nome dello schermo; si chiude dopo 3000 ms. Non blocca il thread UI.
  void ShowMonitorIdentifiers(HINSTANCE instance, const std::vector<MonitorInfo>& monitors);
  ```
  ```js
  renderSetup(root)  // setup.js: tre passi (lingua, schermo con "Identifica", cosa succede + "Avvia con Windows" spento)
  ```
- [ ] **Step 1: Implementare** — `DecideStartup` all'avvio; con `setup_pending` niente overlay, niente salvataggi della config, tray con solo "Apri OLED Guard" ed "Esci", interfaccia aperta sulla procedura (al posto delle pagine). `FinishSetup` scrive la config (monitor scelto con `MonitorSettingForDevice`, lingua, autostart) e avvia la protezione. Chiudere la finestra a procedura non finita: notifica `notice.setup_unfinished`. `MissingMonitorNotifier` nel tick: notifica `notice.monitor_missing` che apre la pagina Monitor al clic. In `mock.js` `?setup=1` mostra la procedura.
- [ ] **Step 2: Verifica in anteprima** (en e it, due dimensioni) e dal vivo con config assente (file copiati e ripristinati fuori dal pacchetto; avvisare l'utente): procedura, Identifica su ogni schermo, protezione che parte solo dopo "Proteggi questo schermo"; log a conferma.
- [ ] **Step 3: Commit** — `feat: procedura di primo avvio e identificazione degli schermi`

## Milestone M5 — pagine tradotte e testi neutri

### Task 11: Pagine Panoramica, Overlay, Monitor, Statistiche tradotte e neutre

**Files:**
- Modify: `src/ui/pages.js` (sezioni di queste quattro pagine), `src/ui/i18n/en.json`, `src/ui/i18n/it.json`

- [ ] **Step 1: Implementare** — ogni testo con `t`/`tp` e i formati del Task 5; testi neutri ("lo schermo"; standby e DDC/CI in una frase valida per TV e monitor).
- [ ] **Step 2: Verifica in anteprima** — `?lang=en` e `?lang=it`, quattro pagine, due dimensioni, console pulita; `rg -n "text: '|'[A-Z][a-zà-ù]+ [a-zà-ù]+" src/ui/pages.js` non trova testi italiani rimasti in queste sezioni.
- [ ] **Step 3: Commit** — `feat(ui): pagine di protezione tradotte`

### Task 12: Pagine Profili app, Tasti rapidi, Impostazioni (lingua), Windows per OLED, Avanzate, Informazioni

**Files:**
- Modify: `src/ui/pages.js`, `src/ui/mock.js` (nomi di schermi generici), `src/ui/i18n/en.json`, `src/ui/i18n/it.json`

- [ ] **Step 1: Implementare** — testi con chiavi; Impostazioni: selettore "Lingua" con "Automatica (lingua di Windows)" e i cinque nomi nativi da `_meta.name` (comando `setLanguage`); Windows per OLED con `windows.<id>.label` e i valori del Task 3; Informazioni: la riga sulla cura del pannello integrata nello schermo e quella sui video protetti nel browser (pausa a schermo intero, regola per il browser).
- [ ] **Step 2: Verifica in anteprima** come nel Task 11, piu' cambio lingua dal selettore (guscio, pagina e attivita' cambiano senza ricaricare). Test unitario `i18n_ui_keys_exist`: ogni `t('...')`/`tp('...')` letterale in `src/ui/*.js` esiste in `en.json` (letti da `OLED_SOURCE_DIR`).
- [ ] **Step 3: Commit** — `feat(ui): impostazioni e pagine di configurazione tradotte, scelta della lingua`

## Milestone M6 — mappa per proporzioni

### Task 13: Griglia delle zone dalle proporzioni (core)

**Files:**
- Modify: `src/core/summary_model.*`, `tests/test_summary_model.cpp`, `tests/dim_scenes.h` se serve

**Interfaces:**
- Produces:
  ```cpp
  inline constexpr int kMaxZones = 640;
  struct ZoneGrid { int cols = 32, rows = 18; int Count() const { return cols * rows; } bool operator==(const ZoneGrid&) const = default; };
  ZoneGrid GridForSize(int width, int height);  // a = w/h; rows = round(sqrt(576/a)); cols = round(rows*a); Count() <= 640
  int ZoneOf(int cx, int cy, int cols, int rows, ZoneGrid grid);
  // FrameSummary: + ZoneGrid grid; zone_light diventa std::array<double, kMaxZones> (usate le prime grid.Count())
  // kSummaryWords = kSummaryZoneBase + kMaxZones * 2
  FrameSummary DecodeSummary(const std::vector<uint32_t>& words, double total_px, ZoneGrid grid);
  ```
  `ComputeSummary` ricava la griglia dalle dimensioni in pixel del modello (accessor da aggiungere a `DimModel` se manca).
- [ ] **Step 1: Test che falliscono** — `grid_for_size_examples` (3840×2160 → 32×18; 1920×1200 → 30×19; 3440×1440 → 38×16; 5120×1440 → 46×13; 1600×1200 → 28×21), `grid_for_size_bounds` (larghezza da 1 000 a 4 000 su altezza 1 000 a passi di 1: `Count()` fra 500 e 640, nessuna dimensione 0), `summary_zones_follow_grid` (scena 21:9: luce solo nella zona in basso a destra → indice `Count()-1`), test esistenti aggiornati.
- [ ] **Step 2-4:** fallire, implementare, verdi.
- [ ] **Step 5: Commit** — `feat(core): griglia della mappa di usura dalle proporzioni dello schermo`

### Task 14: Griglia nello shader `CSSummary` e nel `Dimmer`

**Files:**
- Modify: `src/app/shaders/dim_cs.hlsl`, `src/app/dimmer.cpp`, `src/app/dimmer.h`, `tests_gpu/test_dimmer_gpu.cpp`

**Interfaces:**
- Consumes: `ZoneGrid`, `GridForSize`, `kMaxZones`, `DecodeSummary` (Task 13).
- `DimConstants` riceve `zone_cols`, `zone_rows` (al posto delle costanti `kZoneCols`/`kZoneRows` dello shader); il buffer del riassunto ha `kSummaryWords` del Task 13.
- [ ] **Step 1: Test GPU che falliscono** — `gpu_summary_matches_model_ultrawide` (430×180), `gpu_summary_matches_model_superwide` (512×144), `gpu_summary_matches_model_16_10_odd` (321×201); stessi confronti col modello dei test esistenti.
- [ ] **Step 2: Eseguire `build-check\oled-guard-gpu-tests.exe` e vederli fallire.**
- [ ] **Step 3: Implementare.**
- [ ] **Step 4: Test GPU (tutti) e unitari verdi.**
- [ ] **Step 5: Commit** — `feat(app): riassunto sulla GPU con la griglia dello schermo`

### Task 15: Storico v2 e pagina Statistiche con la griglia

**Files:**
- Modify: `src/core/history.*`, `tests/test_history.cpp`, `src/core/ui_protocol.*`, `tests/test_ui_protocol.cpp`, `src/ui/pages.js`, `src/ui/mock.js`

**Interfaces:**
- Consumes: `ZoneGrid` (Task 13).
- Produces: `WearView` + `ZoneGrid grid` e `normalized` come `std::vector<double>` di `grid.Count()`; la mappa per monitor salva la sua griglia; `History::ToJson` scrive `"version": 2` e `cols`/`rows` per monitor; `FromJson` legge v1 come 32×18 e v2; un secondo con griglia diversa da quella salvata azzera solo la mappa di quel monitor. `HistoryMessage` aggiunge `wear_cols`, `wear_rows`. Etichetta della zona peggiore calcolata sulla griglia.
- [ ] **Step 1: Test che falliscono** — `history_v1_map_kept_on_same_grid` (file v1 con ore nella zona 0, secondi 16:9 → ore conservate e sommate), `history_grid_change_resets_map_only` (secondi 21:9 dopo 16:9 → mappa nuova 38×16, timeline e sessioni intatte), `history_v2_roundtrip`, `history_json_bad_input_is_empty` esteso (`cols` 0, `rows` negativi, prodotto oltre 640, lunghezza delle ore sbagliata), `ui_history_message_grid`.
- [ ] **Step 2-4:** fallire, implementare, verdi.
- [ ] **Step 5: Verifica in anteprima** — `mock.js` con griglia 38×16 (`?grid=21x9`) e 32×18: mappa con proporzioni giuste, zona peggiore evidenziata, nessun overflow.
- [ ] **Step 6: Commit** — `feat: storico v2 e mappa di usura con le proporzioni dello schermo`

## Milestone M7 — diagnostica e segnalazioni

### Task 16: Testo della diagnostica (core)

**Files:**
- Create: `src/core/diagnostics_report.h`, `src/core/diagnostics_report.cpp`, `tests/test_diagnostics_report.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:
  ```cpp
  struct DiagGpu { std::string name; std::string driver; };
  struct DiagMonitor { MonitorItem item; double sdr_white_nits = 0; int rotation_deg = 0; std::string adapter; };
  struct DiagnosticsInput {
    std::string version, commit; uint32_t windows_build = 0;
    std::string language; std::vector<std::string> system_languages;
    std::vector<DiagGpu> gpus; std::vector<DiagMonitor> monitors;
    std::string config_json; std::vector<std::string> render_stats; std::vector<std::string> log_lines;
  };
  // Testo inglese a sezioni ("## System", "## GPUs", "## Displays", "## Config", "## Render", "## Log (last N lines)");
  // del log solo le ultime 500 righe.
  std::string BuildDiagnostics(const DiagnosticsInput& in);
  ```
- [ ] **Step 1: Test che falliscono** — `diagnostics_sections_in_order`, `diagnostics_keeps_last_500_log_lines` (800 righe → la prima e' la 301ª), `diagnostics_marks_protected_display`, `diagnostics_empty_input_is_valid` (nessun crash, sezioni presenti con "none").
- [ ] **Step 2-4:** fallire, implementare, verdi.
- [ ] **Step 5: Commit** — `feat(core): testo della diagnostica`

### Task 17: Esporta diagnostica (app, pagina, tray) e moduli di GitHub

**Files:**
- Create: `src/app/diagnostics_export.h`, `src/app/diagnostics_export.cpp`, `.github/ISSUE_TEMPLATE/bug_report.yml`, `.github/ISSUE_TEMPLATE/translation.yml`, `.github/ISSUE_TEMPLATE/config.yml`
- Modify: `src/app/app.cpp`, `src/app/render_thread.*` (ultime statistiche di render come righe), `src/core/ui_protocol.*` (comando `ExportDiagnostics`), `tests/test_ui_protocol.cpp`, `src/ui/pages.js` (Avanzate), `src/ui/i18n/en.json`, `src/ui/i18n/it.json`, `CMakeLists.txt`

**Interfaces:**
- Consumes: `BuildDiagnostics` (Task 16), `OLED_GUARD_COMMIT` (Task 8).
- Produces:
  ```cpp
  // Raccoglie i dati (GPU con versione del driver da IDXGIAdapter::CheckInterfaceSupport, schermi, config,
  // statistiche di render, coda del log), scrive Documenti\oled-guard-diagnostica-AAAAMMGG-HHMM.txt in UTF-8
  // e lo seleziona in Esplora file (SHOpenFolderAndSelectItems). Restituisce il percorso o nullopt.
  std::optional<std::filesystem::path> ExportDiagnostics(const DiagnosticsInput& base);
  ```
- [ ] **Step 1: Test che fallisce** — `ui_parse_export_diagnostics`.
- [ ] **Step 2: Implementare** — pulsante in Avanzate con l'avviso sulla privacy (nomi delle app nelle regole; leggere prima di condividere) e voce della tray; moduli GitHub in inglese con i campi dello spec §6.7 (bug: schermo marca/modello, TV o monitor, GPU, Windows, HDR, cosa succede, diagnostica allegata obbligatoria; traduzione: lingua, chiave, testo attuale, proposta).
- [ ] **Step 3: Verifica** — build e test verdi; dal vivo (file dell'utente non toccati): il file compare in Documenti, Esplora file lo seleziona, contiene le sei sezioni e nessun testo italiano.
- [ ] **Step 4: Commit** — `feat: esporta diagnostica e moduli di segnalazione`

## Milestone M8 — lingue e documentazione

### Task 18: Spagnolo, tedesco, francese

**Files:**
- Create: `src/ui/i18n/es.json`, `src/ui/i18n/de.json`, `src/ui/i18n/fr.json`

- [ ] **Step 1: Tradurre** da `en.json` (non dall'italiano), `_meta.review: "machine"`, stessi segnaposto; tono come in en; termini fissi: "overlay", "HDR", "pixel shift" restano in inglese dove lo sono in en.
- [ ] **Step 2: Test** — `i18n_files_match_english` verde per tutti e cinque.
- [ ] **Step 3: Commit** — `feat(ui): spagnolo, tedesco e francese (da rileggere)`

### Task 19: Pseudo-lingua e verifica dell'impaginazione

**Files:**
- Modify: `src/ui/mock.js`, `src/ui/app.css` (solo correzioni), `src/ui/pages.js`/`setup.js` (solo correzioni)

- [ ] **Step 1: Implementare** in `mock.js` `?lang=xx`: ogni testo di `en.json` allungato del 40% e con accenti (`Ŝéţţîñĝš ~~~~`), segnaposto intatti.
- [ ] **Step 2: Verifica in anteprima** — `xx` poi `de` su tutte le pagine e sulla procedura di primo avvio, 1 280×800 e 1 000×640: nessun testo tagliato, nessuno scorrimento orizzontale, nessun testo rimasto non trasformato in `xx` (sarebbe un testo non tradotto). Correggere e ripetere.
- [ ] **Step 3: Commit** — `fix(ui): impaginazione con testi lunghi`

### Task 20: README nelle cinque lingue e screenshot in inglese

**Files:**
- Create: `README.es.md`, `README.de.md`, `README.fr.md`
- Modify: `README.md`, `README.it.md`, `docs/screenshots/*.png`

- [ ] **Step 1: Aggiornare** `README.md` e `README.it.md` (pubblico: monitor e TV OLED; Windows 10 2004+ "tested less" e 11; lingue; primo avvio; finestre protette e video nel browser; diagnostica e segnalazioni; runtime incluso); collegamenti alle cinque lingue in cima a ciascuno.
- [ ] **Step 2: Tradurre** in es, de, fr con la riga "traduzione da rileggere" in cima.
- [ ] **Step 3: Screenshot** — rigenerare i tre screenshot con l'anteprima in inglese (`?lang=en`, Chrome senza finestra, 1 280×800 a scala 1,5, come per quelli attuali).
- [ ] **Step 4: Commit** — `docs: README in cinque lingue e screenshot in inglese`

## Milestone M9 — prove e release

### Task 21: Windows 10 in macchina virtuale

- [ ] **Step 1:** Chiedere all'utente l'ok per installare VirtualBox o VMware Workstation e scaricare un'immagine di Windows 10 22H2 (dimensioni da dire). Senza ok: annotare "Windows 10 non provato" nel README e saltare il task.
- [ ] **Step 2:** Nella VM, con lo zip della CI: avvio, procedura di primo avvio, protezione (o motivo per cui la duplicazione non e' disponibile nella VM, dal log), pausa con la finestra di prova del Task 1, cambio lingua, diagnostica. Avvio senza WebView2 se la VM ne e' priva.
- [ ] **Step 3:** Correggere cio' che non va (con test se tocca il core), annotare nel README cosa e' stato provato. Commit.

### Task 22: Prova sulla TV dell'utente e release 1.2.0

- [ ] **Step 1: Build completa e test verdi.**
- [ ] **Step 2: Avvisare l'utente, copiare i file e la chiave Run fuori dal pacchetto, avviare `build-check\oled-guard.exe --debug`** (dire all'utente quale exe riaprire).
- [ ] **Step 3: Checklist, un gruppo alla volta**, ognuno confermato dal log prima di passare al successivo:
  1. Config assente: procedura, Identifica, protezione solo dopo la scelta.
  2. HDR acceso e spento; 60 e 120/144 Hz; 4K, 1440p, 1080p; scala 100/150/200%.
  3. Finestra protetta (di prova o KeePassXC): pausa e ripresa; ridotta a icona: nessuna pausa. Netflix in Edge a schermo intero: pausa.
  4. Cambio di lingua dal vivo nelle cinque lingue: tray, pagina, attivita'.
  5. Statistiche: mappa con le proporzioni giuste; lo storico della 1.1.0 c'e' ancora.
  6. Esporta diagnostica.
  7. Se c'e' un secondo schermo: scelta e Identifica con due schermi.
- [ ] **Step 4: Ripristinare i file**, correggere cio' che non va, commit.
- [ ] **Step 5: Release** — solo con l'ok dell'utente: unione in `main`, push, release 1.2.0 su GitHub dallo zip della CI con il suo SHA-256.
