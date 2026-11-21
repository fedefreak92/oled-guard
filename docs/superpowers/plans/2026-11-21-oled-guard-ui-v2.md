# Nuova interfaccia (progetto 1 di 5) — piano di implementazione

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** rifare l'interfaccia di OLED Guard nello stile del riferimento (barra
laterale, barra in alto, striscia in basso, dieci pagine) con tutte le funzioni di oggi,
piu' luminosita' via software, bordi scuriti, modalita' assente, pagina Monitor,
attivita' recente, accento e dimensione del testo.

**Architecture:** la logica nuova vive in `src/core` come funzioni pure testate
(`shade_model`, `away_model`, `activity_log`, campi di config, messaggi del protocollo).
Lo shader dell'overlay riceve un fattore di luminosita' e i bordi scuriti nel buffer
delle costanti (64 byte), verificato su WARP contro `shade_model`. Il thread UI decide
lo stato "assente" ogni secondo e lo pubblica in un nuovo `SharedShade`; il thread di
render calcola la rampa e ridisegna solo quando il fattore cambia. Il front-end in
`src/ui` e' riscritto da zero, sempre HTML/CSS/JS senza librerie.

**Tech Stack:** C++20, MSVC, CMake + Ninja, D3D11 (ps_5_0 con fxc), WebView2,
HTML/CSS/JS vanilla.

**Spec:** `docs/superpowers/specs/2026-11-21-oled-guard-ui-v2-design.md`

## Global Constraints

- Logica pura in `src/core` senza header di Windows; Win32/D3D in `src/app`; UI in `src/ui` (CSP `default-src 'self'`, niente script o stili inline, niente librerie, niente rete).
- Commenti e testi UI in italiano, identificatori in inglese. Nessuna dipendenza nuova.
- Caratteri: Segoe UI Variable (ripiego Segoe UI) per il testo, Cascadia Mono (ripiego Consolas) per numeri e didascalie tecniche.
- Con luminosita' 100%, bordi spenti, assente non attivo e oscuramento a livello 0, l'output del pixel shader e' identico bit per bit a quello attuale.
- Valori di default e intervalli esattamente come la tabella §5.6 dello spec (riportati nel Task 1).
- Nessuna attesa bloccante nuova sul thread UI o di render (watchdog). Dialoghi e azioni della shell su thread propri (`RunShellAction`).
- L'app reale non mostra mai dati finti: i dati finti esistono solo in modalita' anteprima (`window.chrome.webview` assente).
- Ogni task: `powershell -ExecutionPolicy Bypass -File build.ps1` verde, commit locale con trailer `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Se `build\oled-guard.exe` e' in uso, compilare in `build-check` (ignorata da git). Prove a runtime: avvisare l'utente prima di qualsiasi cosa che compaia a schermo; copia e ripristino di `%APPDATA%\oled-guard\config.json` e `stats.json`.

## Review Focus

1. **Tutto spento** (luminosita' 100, bordi spenti, non assente, nessun oscuramento) → pixel identici bit per bit (Task 6 `shift_ps_shade_neutral_is_bit_exact`).
2. **Video in finestra senza toccare niente per 30 minuti** → mai assente (Task 3 `away_never_with_continuous_motion`).
3. **Input durante la rampa di discesa** → luminosita' piena subito, non una risalita lenta (Task 3 `away_input_restores_immediately`).
4. **Config scritta a mano** con `app_list` in maiuscolo, duplicati, nomi vuoti o piu' lunghi di 260, oppure piu' di 200 voci → minuscolo, senza duplicati, voci invalide scartate con avviso (Task 1 `config_app_list_normalized`).
5. **Schermo protetto scollegato** mentre la pagina Monitor e' aperta → elenco aggiornato, nessuna card marcata protetta, "Automatico" ancora sceglibile (Task 5 `ui_monitors_message_none_protected`, Task 8 invio su `WM_DISPLAYCHANGE`).

---

## Milestone G1 — logica pura

### Task 1: Campi di config e regole delle app

**Files:**
- Modify: `src/core/config.h`, `src/core/config.cpp`, `src/core/app_history.h`, `src/core/app_history.cpp`
- Test: `tests/test_config.cpp`, `tests/test_app_history.cpp`

**Interfaces:**
- Produces: nuovi campi di `Config` (con `operator==` di default che li include):
  ```cpp
  int brightness_pct = 100;            // 20-100
  bool vignette_enabled = false;
  int vignette_strength_pct = 50;      // 0-80
  int vignette_left_pct = 8;           // 0-40
  int vignette_right_pct = 8;          // 0-40
  int vignette_top_pct = 0;            // 0-40
  int vignette_bottom_pct = 12;        // 0-40
  bool away_enabled = false;
  int away_after_min = 10;             // 1-60
  int away_brightness_pct = 30;        // 10-80
  std::string ui_accent = "teal";      // teal|indigo|coral|green|amber|violet
  std::string ui_text_size = "normal"; // small|normal|large|xl
  std::vector<std::string> app_list;   // .exe minuscoli, max 200, ognuno max 260
  ```
- Produces (in `app_history.h`):
  ```cpp
  // Aggiunge l'exe (minuscolo) ad app_list se manca; nessun cambio di regola.
  Config WithListedApp(Config config, std::string_view exe);
  // Toglie l'exe da app_list, denylist, allowlist e dim_exclude.
  Config WithoutApp(Config config, std::string_view exe);
  // Aggiunge o toglie l'exe (minuscolo) da dim_exclude.
  Config WithDimExclude(Config config, std::string_view exe, bool on);
  ```

- [ ] **Step 1: Scrivere i test che falliscono**
  - `config_new_fields_defaults`: `ConfigFromJson("{}")` → valori della tabella sopra, nessun avviso.
  - `config_new_fields_read_and_roundtrip`: JSON con `brightness_pct: 60, vignette_enabled: true, vignette_left_pct: 40, away_after_min: 1, ui_accent: "violet", ui_text_size: "xl", app_list: ["Game.EXE"]` → letti; `ConfigFromJson(ConfigToJson(c)).config == c`.
  - `config_new_fields_out_of_range`: `brightness_pct: 19`, `vignette_strength_pct: 81`, `vignette_top_pct: 41`, `away_after_min: 0`, `away_brightness_pct: 81`, `ui_accent: "pink"`, `ui_text_size: "huge"` → ciascuno resta al default con un avviso che contiene il nome del campo.
  - `config_app_list_normalized`: `app_list: ["A.exe", "a.exe", "", 5, "<261 caratteri>"]` → `{"a.exe"}` e almeno un avviso; 201 voci valide distinte → prime 200 e un avviso che contiene `app_list`.
  - `app_rules_listed_and_removed`: `WithListedApp(c, "Foo.exe")` aggiunge `"foo.exe"` una volta sola anche se chiamata due volte; `WithoutApp` su una config con `foo.exe` in tutte e quattro le liste le svuota; `WithDimExclude(c, "foo.exe", true)` poi `false` torna alla config iniziale.

- [ ] **Step 2: Eseguire i test e vederli fallire** — `build.ps1`: errori di compilazione sui campi mancanti.

- [ ] **Step 3: Implementare** — campi in `config.h`; lettura in `ConfigFromJson` con i `Reader` esistenti (`Int`, `Bool`, `Choice`); `app_list` con `List` seguito da una nuova pulizia (duplicati, lunghezza > 260, limite 200) che aggiunge avvisi `app_list: ...`; scrittura in `ConfigToJson`. Le tre funzioni in `app_history.cpp` con `ToLowerAscii` come `WithRule`.

- [ ] **Step 4: Eseguire i test** — `build.ps1`: tutti verdi.

- [ ] **Step 5: Commit** — `feat(core): campi di config della nuova interfaccia e regole delle app`

### Task 2: Modello di luminosita' e bordi (`shade_model`)

**Files:**
- Create: `src/core/shade_model.h`, `src/core/shade_model.cpp`, `tests/test_shade_model.cpp`
- Modify: `CMakeLists.txt` (aggiunge i due file a `oled_core` e `oled-guard-tests`)

**Interfaces:**
- Consumes: `Config` (Task 1).
- Produces:
  ```cpp
  struct ShadeParams {
    float brightness = 1;         // 0-1, gia' moltiplicato per il fattore assente
    float vignette_strength = 0;  // 0-0,8 (0 se vignette_enabled e' falso)
    float spread[4] = {0, 0, 0, 0};  // sinistra, destra, alto, basso: frazioni 0-0,4
    bool operator==(const ShadeParams&) const = default;
  };
  ShadeParams ToShadeParams(const Config& config, double away_factor);
  bool ShadeIsNeutral(const ShadeParams& p);  // brightness == 1 e vignette_strength == 0
  // Fattore moltiplicativo in luce lineare nel punto (u, v) dello schermo, 0-1 ciascuno
  // (0,0 = angolo in alto a sinistra).
  float ShadeFactor(const ShadeParams& p, float u, float v);
  ```
  Formula (da copiare identica nello shader, Task 6):
  ```
  edge(d, s)  = s <= 0 ? 0 : 1 - smoothstep(0, s, d)
  vignetta    = max(edge(u, s0), edge(1-u, s1), edge(v, s2), edge(1-v, s3))
  fattore     = brightness * (1 - strength * vignetta)
  ```

- [ ] **Step 1: Scrivere i test che falliscono** (`CHECK_NEAR` con tolleranza 1e-5)
  - `shade_neutral_is_one`: `ToShadeParams(Config{}, 1.0)` e' neutro; `ShadeFactor` = 1 in (0,0), (0.5,0.5), (1,1).
  - `shade_brightness_and_away_multiply`: `brightness_pct = 60`, `away_factor = 0.5` → `brightness == 0.3f`, non neutro.
  - `shade_vignette_disabled_ignores_sides`: `vignette_enabled = false` con lati 40% → `vignette_strength == 0`.
  - `shade_vignette_edges`: strength 0.5, sinistra 0.2, altri 0 → fattore 0.5 in u=0, 1 in u≥0.2, fra 0.5 e 1 in u=0.1; in u=1 (lato destro spento) fattore 1.
  - `shade_vignette_corner_uses_max`: sinistra e alto 0.2, strength 0.5 → in (0,0) fattore 0.5 (non 0.25).

- [ ] **Step 2: Eseguirli e vederli fallire.**
- [ ] **Step 3: Implementare** `shade_model.cpp` (smoothstep standard `t*t*(3-2t)` con `t = clamp(d/s, 0, 1)`).
- [ ] **Step 4: Eseguirli: verdi.**
- [ ] **Step 5: Commit** — `feat(core): modello di luminosita' e bordi scuriti`

### Task 3: Modalita' assente (`away_model`)

**Files:**
- Create: `src/core/away_model.h`, `src/core/away_model.cpp`, `tests/test_away_model.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:
  ```cpp
  inline constexpr int kMotionWindowS = 60;
  inline constexpr int kMovingSecondFrames = 8;   // >= 8 frame nuovi in un secondo = secondo "in movimento"
  inline constexpr int kContinuousSeconds = 20;   // >= 20 secondi in movimento negli ultimi 60
  inline constexpr double kAwayRampMs = 10000;

  // Secondi recenti in cui il desktop ha prodotto frame nuovi.
  class MotionWindow {
   public:
    void AddSecond(int new_frames);  // un campione al secondo
    bool Continuous() const;
    void Reset();
  };

  struct AwayInputs {
    double now_ms = 0;
    double idle_ms = 0;          // da GetLastInputInfo
    bool continuous_motion = false;
    bool overlay_shown = false;
    bool enabled = false;
    int after_min = 10;
  };

  class AwayModel {
   public:
    // Aggiorna lo stato; restituisce true se e' cambiato.
    bool Update(const AwayInputs& in);
    bool Away() const;
    double SinceMs() const;  // inizio dell'assenza (valido se Away())
  };

  // Fattore 1 -> target lineare in kAwayRampMs da since_ms; 1 se since_ms < 0.
  double AwayFactorAt(double now_ms, double since_ms, double target);
  ```
  Regole: entra in assente se `enabled && overlay_shown && idle_ms >= after_min*60000 && !continuous_motion`; esce appena una condizione cade (input: `idle_ms` torna piccolo). Il chiamante passa `since_ms = -1` quando non e' assente, quindi il fattore torna 1 al primo ridisegno.

- [ ] **Step 1: Scrivere i test che falliscono**
  - `motion_window_video_is_continuous`: 60 campioni da 24 → `Continuous()`.
  - `motion_window_caret_is_not`: 60 campioni da 2 → non continuo; 19 da 30 e 41 da 0 → non continuo; 20 da 30 → continuo.
  - `away_never_with_continuous_motion`: `idle_ms` da 0 a 30 min a passi di 1 s, `continuous_motion = true` → mai `Away()`.
  - `away_enters_after_idle`: `after_min = 10`, idle 599 999 ms → no; 600 000 → si', `SinceMs() == now_ms`.
  - `away_input_restores_immediately`: dopo l'ingresso, `idle_ms = 500` → `Update` true e non assente.
  - `away_off_when_disabled_or_hidden`: `enabled = false` o `overlay_shown = false` → mai assente; se diventa falso mentre e' assente → esce.
  - `away_factor_ramp`: `AwayFactorAt(since, since, 0.3) == 1`, `(since+5000) ≈ 0.65`, `(since+10000) == 0.3`, `(since+99999) == 0.3`, `since_ms = -1` → 1.

- [ ] **Step 2: Eseguirli e vederli fallire.**
- [ ] **Step 3: Implementare** (`MotionWindow` come anello di 60 interi).
- [ ] **Step 4: Eseguirli: verdi.**
- [ ] **Step 5: Commit** — `feat(core): modello della modalita' assente`

### Task 4: Attivita' recente (`activity_log`)

**Files:**
- Create: `src/core/activity_log.h`, `src/core/activity_log.cpp`, `tests/test_activity_log.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:
  ```cpp
  struct ActivityEvent {
    int64_t t_ms = 0;   // tempo Unix in millisecondi
    std::string kind;   // "pause" | "resume" | "manual" | "test" | "away" | "back" | "monitor" | "windows" | "config"
    std::string text;   // italiano, UTF-8
    int count = 1;      // eventi uguali fusi
  };
  class ActivityLog {
   public:
    static constexpr size_t kMax = 100;
    static constexpr int64_t kMergeMs = 5000;
    // Restituisce l'evento aggiunto o aggiornato (fuso).
    const ActivityEvent& Add(int64_t t_ms, std::string_view kind, std::string_view text);
    const std::deque<ActivityEvent>& Events() const;  // dal piu' recente
    void Clear();
  };
  ```
- [ ] **Step 1: Scrivere i test che falliscono**
  - `activity_newest_first`: due eventi diversi → il secondo in testa.
  - `activity_merges_same_within_5s`: stesso `kind` e `text` a 0 e 4 999 ms → un evento con `count == 2` e `t_ms == 4999`; a 10 100 ms → evento nuovo.
  - `activity_caps_at_100`: 150 eventi distinti → 100, il piu' vecchio rimasto e' il 51°.
  - `activity_clear`.
- [ ] **Step 2-4:** fallire, implementare, verdi.
- [ ] **Step 5: Commit** — `feat(core): registro dell'attivita' recente`

### Task 5: Protocollo e versione

**Files:**
- Modify: `src/core/ui_protocol.h`, `src/core/ui_protocol.cpp`, `CMakeLists.txt`
- Test: `tests/test_ui_protocol.cpp`

**Interfaces:**
- Consumes: `ActivityLog`/`ActivityEvent` (Task 4), `Config::app_list`, `dim_exclude`.
- Produces:
  ```cpp
  // UiState, campi nuovi:
  bool away = false;
  double session_ms = 0;

  struct MonitorItem {
    std::string device;      // \\.\DISPLAY1
    std::string name;
    int width = 0, height = 0, refresh_hz = 0;
    bool hdr = false, primary = false, is_protected = false;
    std::string connection;  // "HDMI" | "DP" | "DVI" | "Interno" | ""
  };
  std::string MonitorsMessage(const std::vector<MonitorItem>& items, const std::string& setting);
  std::string ActivityMessage(const ActivityLog& log);          // {type:"activity", events:[...]}
  std::string ActivityAddMessage(const ActivityEvent& e);       // {type:"activity", add:{t,kind,text,count}}
  std::string AboutMessage(std::string_view version, std::string_view config_path, std::string_view log_dir);
  std::string BrowsedMessage(std::string_view exe);             // {type:"browsed", exe}
  // UiCommandType nuovi: OpenLogFolder, BrowseExe, SetMonitor, ClearActivity, SetDimExclude.
  // UiCommand::id porta il device per SetMonitor ("auto" ammesso), exe/on per SetDimExclude.
  ```
  `StateMessage` aggiunge `away`, `session_ms`. `StatsMessage` aggiunge `pauses_by_reason`: oggetto `{testo del motivo: conteggio}` con i soli motivi > 0. `AppsMessage` include anche le app di `app_list` e `dim_exclude` non presenti nello storico e aggiunge a ogni voce `dim_exclude: bool`. `MonitorsMessage` mette `setting` (es. `"auto"`) nel messaggio.
- Versione: in `CMakeLists.txt` `project(oled_guard VERSION 1.0.0 LANGUAGES CXX)` e `target_compile_definitions(oled-guard PRIVATE OLED_GUARD_VERSION="${PROJECT_VERSION}")` (solo sull'eseguibile; il core la riceve come parametro).

- [ ] **Step 1: Scrivere i test che falliscono**
  - `ui_state_has_away_and_session`.
  - `ui_stats_pauses_by_reason`: due pause `FullscreenWindow` e una `NotificationState` → oggetto con quei due testi, conteggi 2 e 1, nient'altro.
  - `ui_apps_includes_listed_and_excluded`: config con `app_list {"a.exe"}`, `dim_exclude {"b.exe"}`, storico vuoto → due voci; `b.exe` con `dim_exclude: true`, `a.exe` con `rule: "auto"`.
  - `ui_monitors_message_fields`: due schermi, il secondo protetto → campi come sopra, `setting` presente.
  - `ui_monitors_message_none_protected`: elenco con nessuno protetto → tutti `protected: false`, messaggio valido.
  - `ui_activity_messages`: lista intera in ordine dal piu' recente; `add` con `count`.
  - `ui_about_and_browsed_messages`: campi e testo con caratteri da escapare.
  - `ui_parse_new_commands`: `openLogFolder`, `browseExe`, `clearActivity` senza campi; `setMonitor {device:"auto"}` e `{device:"\\\\.\\DISPLAY2"}` validi; `setDimExclude {exe:"x.exe", on:true}` valido.
  - `ui_parse_new_commands_reject`: `setMonitor` senza `device` o con stringa vuota; `setDimExclude` senza `on` o con `exe` di 261 caratteri → `nullopt`.
- [ ] **Step 2-4:** fallire, implementare, verdi.
- [ ] **Step 5: Commit** — `feat(core): messaggi e comandi della nuova interfaccia, versione 1.0.0`

## Milestone G2 — shader

### Task 6: Luminosita' e bordi nel pixel shader

**Files:**
- Modify: `src/app/shift_constants.h`, `src/app/shaders/shift_ps.hlsl`, `src/app/presenter.h`, `src/app/presenter.cpp`
- Test: `tests_gpu/test_shift_ps_gpu.cpp`

**Interfaces:**
- Consumes: `ShadeParams`, `ShadeFactor` (Task 2).
- Produces:
  ```cpp
  inline constexpr unsigned kShiftFlagShade = 8;  // luminosita' o bordi non neutri
  struct ShiftConstants {
    int offset[2];
    unsigned size_minus_one[2];
    float grid_size[2];
    float sdr_white;
    unsigned flags;
    float spread[4];          // sinistra, destra, alto, basso
    float brightness;
    float vignette_strength;
    float pad[2];
  };
  static_assert(sizeof(ShiftConstants) == 64);
  // presenter.h
  HRESULT Draw(ID3D11Texture2D* source, Offset offset, const DimDrawInputs& dim = {},
               const ShadeParams& shade = {}, const std::function<void(ID3D11Texture2D*)>& before_present = {});
  ```
  Nello shader: `if ((flags & (kFlagDim | kFlagShade)) == 0) return c;`; il fattore usa la posizione dello **schermo** `pos.xy / (size_minus_one + 1)` (i bordi non si spostano con l'immagine) e si moltiplica insieme a `(1 - level)` in luce lineare, prima dell'evidenziazione. Il presenter accende `kShiftFlagShade` solo se `!ShadeIsNeutral(shade)`.

- [ ] **Step 1: Scrivere i test che falliscono** (helper `Draw` esteso con `ShiftConstants` completo)
  - `shift_ps_shade_neutral_is_bit_exact`: SDR e HDR, flag senza Shade e Dim → byte identici all'ingresso.
  - `shift_ps_brightness_halves_linear_hdr`: brightness 0.5, flag Shade → ogni canale HDR ≈ metà (tolleranza half).
  - `shift_ps_vignette_matches_model`: HDR, strength 0.6, spread {0.25, 0, 0.1, 0.3} → per 12 pixel campione l'uscita ≈ ingresso × `ShadeFactor(p, (x+0.5)/kW, (y+0.5)/kH)` (tolleranza 2e-3 relativa).
  - `shift_ps_shade_sdr_in_linear`: SDR, brightness 0.5 → valore sRGB uguale a `LinearToSrgb(0.5 × SrgbToLinear(in))` ±1.
  - `shift_ps_shade_and_dim_multiply`: livello 0.5 e brightness 0.5 → HDR ≈ un quarto.
- [ ] **Step 2: Eseguire `build\oled-guard-gpu-tests.exe` e vederli fallire.**
- [ ] **Step 3: Implementare** costanti, shader, `Presenter::Draw` (aggiornare anche la chiamata esistente in `render_thread.cpp` passando `ShadeParams{}` finche' il Task 7 non la collega).
- [ ] **Step 4: Test GPU e unitari verdi** (anche i test GPU esistenti del dimmer).
- [ ] **Step 5: Commit** — `feat(app): luminosita' e bordi scuriti nello shader dell'overlay`

## Milestone G3 — app

### Task 7: `SharedShade`, rampa nel render e rilevamento dell'assenza

**Files:**
- Create: `src/app/shared_shade.h`, `src/app/shared_shade.cpp`
- Modify: `src/app/render_thread.h`, `src/app/render_thread.cpp`, `src/app/app.h`, `src/app/app.cpp`, `CMakeLists.txt` (aggiunge `shared_shade.cpp` a `oled_gpu`)

**Interfaces:**
- Consumes: `ShadeParams`, `ToShadeParams` (Task 2), `MotionWindow`, `AwayModel`, `AwayFactorAt`, `kAwayRampMs` (Task 3).
- Produces:
  ```cpp
  struct ShadeInputs {
    ShadeParams base;         // ToShadeParams(config, 1.0)
    double away_since_ms = -1;  // NowMs() dell'ingresso in assente, -1 se non assente
    double away_target = 1;   // away_brightness_pct / 100
    uint64_t version = 0;
  };
  class SharedShade {
   public:
    void Set(const ShadeInputs& in);  // incrementa version se cambia
    ShadeInputs Get();
    void CountNewFrame();              // thread di render, a ogni NewFrame
    int TakeNewFrames();               // thread UI, una volta al secondo (azzera)
  };
  ```
  `RenderParams` guadagna `SharedShade* shade = nullptr`. Nel ciclo di render: a ogni iterazione `shade_in = params_.shade->Get()`; parametri effettivi = `base` con `brightness *= AwayFactorAt(now, away_since_ms, away_target)`; si ridisegna se la versione e' cambiata o se i parametri effettivi sono diversi dall'ultimo disegno; durante la rampa `next = min(next, now + 50)` (20 Hz). `CountNewFrame()` dove oggi si imposta `have_frame = true` per un `NewFrame`.
  In `App`: membri `SharedShade shade_`, `MotionWindow motion_`, `AwayModel away_`, `double app_start_ms_`; nel timer `kTimerBeat` (1 s): `motion_.AddSecond(shade_.TakeNewFrames())` se l'overlay e' a schermo (altrimenti `Reset()`), `GetLastInputInfo` → `idle_ms`, `away_.Update(...)`, poi `PushShadeInputs()`. `ApplyConfig` chiama `PushShadeInputs()`. `BuildUiState` riempie `away` e `session_ms = now - app_start_ms_`.

- [ ] **Step 1: Implementare** `SharedShade` (mutex come `SharedDim`, contatore `std::atomic<int>`), collegamenti nel render e nell'app.
- [ ] **Step 2: Build e test verdi.**
- [ ] **Step 3: Prova dal vivo (avvisare l'utente prima)** con `config.json` copiato: `brightness_pct: 50` → schermo piu' scuro; `vignette_enabled: true` → bordi scuri e fermi mentre l'immagine si sposta; `away_enabled: true, away_after_min: 1` → dopo 1 minuto senza toccare niente a desktop fermo lo schermo scende in 10 s, al primo movimento del mouse torna subito; un video in finestra per 3 minuti senza toccare niente non scurisce. Ripristinare `config.json`.
- [ ] **Step 4: Commit** — `feat(app): luminosita', bordi e modalita' assente collegati all'overlay`

### Task 8: Elenco dei monitor e scelta dalla pagina

**Files:**
- Modify: `src/app/monitors.h`, `src/app/monitors.cpp`, `src/app/app.cpp`

**Interfaces:**
- Consumes: `MonitorItem`, `MonitorsMessage` (Task 5).
- Produces:
  ```cpp
  struct MonitorDetails {
    MonitorInfo info;
    int refresh_hz = 0;
    std::string connection;  // come MonitorItem::connection
  };
  std::vector<MonitorDetails> ListMonitorDetails();
  ```
  `QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS)`; per ogni percorso `DISPLAYCONFIG_SOURCE_DEVICE_NAME` (confronto con `MonitorInfo::device_name`) e `DISPLAYCONFIG_TARGET_DEVICE_NAME::outputTechnology`: HDMI → `"HDMI"`, `DISPLAYPORT_EXTERNAL` → `"DP"`, `DVI` → `"DVI"`, `INTERNAL`/`DISPLAYPORT_EMBEDDED`/`UDI_EMBEDDED` → `"Interno"`, altro → `""`; frequenza da `targetInfo.refreshRate` arrotondata. Errori: elenco senza frequenza ne' collegamento, mai un errore verso la pagina.
  `App::PushMonitorsToUi()`: `is_protected` = device del `monitor_` attuale; inviato a `ready`, su `WM_DISPLAYCHANGE` e dopo `ApplyConfig` se cambia `monitor`. Comando `SetMonitor` → `SelectMonitorSetting(command->id)` (gia' esistente).
- [ ] **Step 1: Implementare.**
- [ ] **Step 2: Build verde; con `--debug` aprire DevTools e verificare che il messaggio `monitors` della TV riporti 3840×2160, ~143 Hz, HDR, collegamento corretto.**
- [ ] **Step 3: Commit** — `feat(app): elenco dei monitor con frequenza e collegamento`

### Task 9: Attivita', comandi nuovi, app aggiunte, finestra

**Files:**
- Modify: `src/app/app.h`, `src/app/app.cpp`, `src/app/ui_window.cpp`

**Interfaces:**
- Consumes: `ActivityLog` (Task 4); messaggi e comandi del Task 5; `WithListedApp`, `WithoutApp`, `WithDimExclude` (Task 1).
- Produces (comportamento):
  - `ActivityLog activity_` in `App`; `App::Record(kind, text)` aggiunge e, se la pagina e' pronta, invia `ActivityAddMessage`. Eventi: entrata in pausa automatica (`"pause"`, "In pausa: <motivo> · <exe>"), ripresa (`"resume"`), pausa manuale on/off (`"manual"`), prova dello spostamento (`"test"`), assente entrata/uscita (`"away"`/`"back"`), schermo protetto cambiato (`"monitor"`), impostazioni di Windows applicate/ripristinate (`"windows"`), config ricaricata con avvisi (`"config"`).
  - `ready` invia anche `ActivityMessage`, `MonitorsMessage`, `AboutMessage(OLED_GUARD_VERSION, ConfigPath(), LogDir())`.
  - `OpenLogFolder` → `App::OpenLogFolder()` esistente. `ClearActivity` → `activity_.Clear()` + `ActivityMessage`.
  - `BrowseExe` → `RunShellAction` con `IFileOpenDialog` (filtro `*.exe`, nessun proprietario o la finestra dell'interfaccia); il nome del file scelto (solo il nome, minuscolo, UTF-8) viene passato al thread UI con `PostMessageW(main_, WM_APP_BROWSED, 0, (LPARAM)new std::string(...))` (nuovo `WM_APP + 7`), che lo inoltra con `BrowsedMessage`; annullato → niente.
  - `AddApp` → `WithListedApp` (regola invariata, cioe' Automatico se nuova); `RemoveApp` → `WithoutApp` + `app_history_.Remove`; `SetDimExclude` → `WithDimExclude`. Dopo ognuno: `ApplyConfig`, `SaveConfig`, `AppsMessage`.
  - `DimWatch` resta com'e' (la pagina lo accende per Panoramica e Overlay).
  - `ui_window.cpp`: `kWidthDip = 1280`, `kHeightDip = 800`, `kMinWidthDip = 1000`, `kMinHeightDip = 640`.
- [ ] **Step 1: Implementare.**
- [ ] **Step 2: Build e test verdi.**
- [ ] **Step 3: Commit** — `feat(app): attivita' recente, sfoglia, cartella log, app aggiunte e finestra piu' grande`

## Milestone G4 — front-end

Verifica di ogni task del front-end: `preview_start {name: "ui-anteprima"}`, poi per ogni
pagina toccata `read_page` (testi e struttura), `read_console_messages` (nessun
errore), uno screenshot; finestra a 1280×800 e a 1000×640 (`resize_window`).

### Task 10: Guscio, gettoni, componenti e anteprima

**Files:**
- Rewrite: `src/ui/index.html`, `src/ui/app.css`, `src/ui/app.js`
- Create: `src/ui/mock.js` (dati finti dell'anteprima, caricato solo se `window.chrome.webview` manca), `src/ui/pages.js` (le dieci pagine)

**Interfaces:**
- Consumes: messaggi `state`, `stats`, `config`, `apps`, `windows`, `processes`, `dim`, `notice`, `monitors`, `activity`, `about`, `browsed`; comandi del Task 5 e quelli esistenti.
- Produces (in `app.js`, usati da `pages.js`):
  - `store` (ultimo messaggio di ogni tipo) e `on(type, fn)` per ridisegnare le parti interessate.
  - `send(cmd)`, `setConfig(patch)` e `throttledConfig(key)` (al massimo ogni 100 ms durante il trascinamento, sempre l'ultimo valore).
  - Componenti che restituiscono elementi DOM: `toggle(on, onChange)`, `segmented(options, value, onChange)`, `slider({min, max, step, value, format, onInput})`, `dualSlider(...)` (quello di oggi), `choiceCards(options, value, onChange)`, `pill(text, tone)`, `panel(title, subtitle)`, `settingRow(title, desc, control)`.
  - `contrastRatio(hexA, hexB)` (formula WCAG) per le etichette dei colori d'accento.
  - Router: `showPage(id)` con id `panoramica, overlay, monitor, statistiche, profili, tasti, impostazioni, windows, avanzate, informazioni`; accende `dimWatch` solo su Panoramica e Overlay.
- Guscio come §5.2 dello spec: barra laterale con logo a scudo SVG, gruppi PROTEGGI/CONFIGURA/AIUTO, "Novita'" + versione in fondo; barra in alto trascinabile con pausa, stato (`Protetto`, `In pausa · <motivo>`, `Prova in corso`, `Assente`), sessione, icone Statistiche e Impostazioni, pulsanti finestra; striscia in basso con Sessione, Protetto oggi, Totale, Uniformita', Pause oggi, "Apri statistiche".
- Gettoni CSS di §5.1 su `:root`; `--accent` da `ui_accent` (teal `#1fc7a9`, indigo `#7c86ff`, coral `#ff7a59`, green `#5fdc8f`, amber `#f2a641`, violet `#b28cff`); dimensione radice da `ui_text_size` (13 / 14 / 15.5 / 17 px), tutto il resto in `rem`; sotto 1200 px le griglie a due colonne diventano una.
- Attenuazione al 30% dopo 60 s senza interazione (come oggi). Nessun lampo bianco: `background: #000` su `html`.
- [ ] **Step 1: Implementare guscio, componenti, router e `mock.js`** (le pagine possono essere vuote).
- [ ] **Step 2: Verifica in anteprima** — navigazione fra le dieci voci, stato che cambia nella barra in alto, striscia in basso con numeri, cambio d'accento e testo XL visibili subito, nessun errore in console, nessuno scorrimento orizzontale a 1000×640.
- [ ] **Step 3: Commit** — `feat(ui): nuovo guscio con barra laterale, barra di stato e striscia in basso`

### Task 11: Panoramica, Overlay, Monitor

**Files:**
- Modify: `src/ui/pages.js`, `src/ui/app.css`, `src/ui/mock.js`

**Interfaces:**
- Consumes: componenti e store del Task 10.
- Contenuto esattamente come §5.3 dello spec:
  - **Panoramica**: pannello Protezione (radar delle posizioni con scia, offset, conto alla rovescia, app in primo piano, latenza), pannello Zone oscurate ora (miniatura dalla `dim`, %, stato), Azioni rapide (Pausa, Prova lo spostamento, Evidenzia zone oscurate 15 s, Oscuramento, Bordi scuriti), colonna Attivita' recente con "Cancella".
  - **Overlay**: Spostamento dei pixel; Oscuramento delle zone ferme (tutte le regolazioni e regole di visibilita' di oggi, link "Gestisci le app escluse" → Profili app); Bordi scuriti con anteprima 16:9, quattro maniglie trascinabili (0-40%, valore accanto), cursore verticale Intensita' 0-80%, "Ripristina tutti i lati" (torna a 8/8/0/12 e 50%).
  - **Monitor**: testata "Protezione su <nome>", card per schermo (nome, risoluzione, Hz, `PRIMARY`, `HDR`/`SDR`, collegamento), un solo interruttore acceso + voce "Automatico", cursore Luminosita' 20-100% sulla card protetta con la nota "funziona mentre la protezione e' attiva" e la riga sul contrasto non disponibile.
- [ ] **Step 1: Implementare le tre pagine e i dati finti corrispondenti.**
- [ ] **Step 2: Verifica in anteprima** — trascinando una maniglia dei bordi il valore cambia e parte al massimo un `setConfig` ogni 100 ms (contare in `mock.js`); il radar si muove; scegliendo un altro schermo l'interruttore precedente si spegne; console pulita.
- [ ] **Step 3: Commit** — `feat(ui): pagine Panoramica, Overlay e Monitor`

### Task 12: Statistiche, Profili app, Tasti rapidi, Impostazioni, Windows per OLED, Avanzate, Informazioni

**Files:**
- Modify: `src/ui/pages.js`, `src/ui/app.css`, `src/ui/mock.js`

**Interfaces:**
- Consumes: componenti e store del Task 10.
- Contenuto esattamente come §5.3 dello spec:
  - **Statistiche**: quattro riquadri (Protetto in totale, Protetto oggi, Uniformita', Pause oggi), mappa 7×7 delle posizioni, barre di `pauses_by_reason`.
  - **Profili app**: elenco con ricerca (avatar con iniziale, colore ricavato dal nome con un hash fisso; exe; ultima volta e volte; etichette regola e `NON OSCURARE`; elimina), pause automatiche globali, pannello Modifica (tre card-scelta always/auto/never, interruttore Non oscurare, frase che spiega la scelta, Elimina), Aggiungi app (campo, App aperte, Sfoglia... che riempie il campo con `browsed`).
  - **Tasti rapidi**: le tre combinazioni con la registrazione di oggi (`hotkeyCapture`).
  - **Impostazioni**: Avvio; Modalita' assente (interruttore, dopo 1-60 min, scurisci fino a 10-80%); Aspetto (sei colori con `contrastRatio` calcolato su `--panel`, segmenti di dimensione); Diagnostica (livello del log, Apri cartella dei log, percorsi da `about`).
  - **Windows per OLED**: punteggio, Applica tutti / Ripristina come prima, righe, Spegni schermo.
  - **Avanzate**: `dim_bright_threshold_pct` (5-100), `dim_motion_fraction_pct` (10-90), `dim_recover_ms` (100-5000), `pause_enter_delay_ms` (0-10000), `resume_delay_ms` (0-10000), `allow_tearing`, ognuno col nome del campo in Cascadia Mono; "Ripristina i valori predefiniti" manda i default di `Config` per questi sei campi.
  - **Informazioni**: nome, versione da `about`, tre righe su cosa fa l'app, Novita' (voce 1.0.0: "Nuova interfaccia; luminosita' via software; bordi scuriti; modalita' assente; pagina Monitor; attivita' recente; colore d'accento e dimensione del testo"), licenza WebView2, percorsi.
- [ ] **Step 1: Implementare le sette pagine e i dati finti corrispondenti.**
- [ ] **Step 2: Verifica in anteprima** — aggiungere un'app la mostra con regola Automatico e la apre in Modifica; Elimina la toglie; cambiare accento aggiorna le etichette di contrasto; "Ripristina i valori predefiniti" riporta i sei valori; console pulita; screenshot di ogni pagina.
- [ ] **Step 3: Commit** — `feat(ui): pagine Statistiche, Profili app, Tasti rapidi, Impostazioni, Windows, Avanzate, Informazioni`

## Milestone G5 — prova dal vivo

### Task 13: Prova sulla TV e chiusura

**Files:**
- Modify: eventuali correzioni; `src/ui/pages.js` (Novita') se cambia qualcosa.

- [ ] **Step 1: Build completa e test verdi** (unitari e GPU).
- [ ] **Step 2: Avvisare l'utente e preparare la prova** — copia di `config.json` e `stats.json`, chiedere prima di chiudere un `oled-guard.exe` aperto, avvio della nuova build.
- [ ] **Step 3: Checklist breve per l'utente** (lui guarda la TV):
  1. La finestra si apre senza lampo bianco, a 1280×800, con il nuovo aspetto.
  2. Le dieci pagine si aprono; nessun pulsante senza effetto.
  3. Luminosita' al 50% dalla pagina Monitor: lo schermo si scurisce; al 100% torna identico.
  4. Bordi scuriti accesi: bordi scuri fermi mentre l'immagine si sposta; trascinando una maniglia cambiano dal vivo.
  5. Modalita' assente a 1 minuto: desktop fermo → scende in 10 s; mouse → torna subito; un video in finestra non la attiva.
  6. Profili app: aggiungi con Sfoglia..., cambia regola, Non oscurare, elimina.
  7. Attivita' recente mostra pause e riprese mentre si apre e chiude un gioco.
  8. Dopo 60 s senza toccare la finestra si attenua.
- [ ] **Step 4: Ripristinare** `config.json` e `stats.json`; correggere cio' che non va (con test se tocca il core).
- [ ] **Step 5: Commit** — `fix: correzioni dalla prova dal vivo della nuova interfaccia` (se servono).
