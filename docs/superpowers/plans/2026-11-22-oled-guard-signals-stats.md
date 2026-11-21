# Segnali dal vivo e statistiche (progetto 2 di 5) — piano di implementazione

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** misurare ogni secondo rischio, movimento e luminosita' sulla GPU, accumulare
storico, mappa di usura e uso per app, e mostrarli nella striscia in basso, nella pagina
Statistiche e in un avviso nella barra delle applicazioni.

**Architecture:** il rilevamento dell'oscuramento produce anche luminanza media e
istante dell'ultimo cambio reale per cella; un nuovo compute shader `CSSummary` riduce le
celle a 32×18 zone e pochi totali in un buffer letto in modo asincrono una volta al
secondo. Il comportamento e' definito da `ComputeSummary` (core, oracolo dei test WARP)
sopra `DimModel`. Il thread UI accumula `SignalRing`, `History` (salvata in
`history.json`) e `AlertModel`, e invia i messaggi `signals` e `history` alla pagina.

**Tech Stack:** C++20, MSVC, CMake + Ninja, D3D11 (cs_5_0 con fxc), WebView2 + HTML/CSS/JS vanilla.

**Spec:** `docs/superpowers/specs/2026-11-22-oled-guard-signals-stats-design.md`

## Global Constraints

- Logica pura in `src/core` senza header di Windows; Win32/D3D in `src/app`; UI in `src/ui` (CSP `default-src 'self'`, niente inline, niente librerie, niente rete).
- Commenti e testi UI in italiano, identificatori in inglese. Nessuna dipendenza nuova.
- Nessun pixel del desktop letto dalla CPU: solo il buffer del riassunto. Letture GPU solo con `D3D11_MAP_FLAG_DO_NOT_WAIT`; nessuna attesa nuova su thread UI o di render (watchdog).
- Al massimo 8 UAV per passaggio (`cs_5_0`): `CSDetect` usa u0-u7.
- "Ferma" = contenuto invariato da almeno 60 s; "in movimento" = cambiato negli ultimi 10 s; sempre dall'istante di cambio **reale** (`content_change`), mai da `last_change` (che `CSActivate` riporta a "adesso").
- Rischio misurato prima dell'oscuramento; Rischio non protetto = celle a rischio con livello ≤ 0,05; mappa di usura con la luce dopo oscuramento, luminosita' e bordi.
- Soglie dell'avviso: low 0,10, medium 0,20, high 0,35; azzeramento dopo 120 s di fila sotto soglia; al massimo un avviso ogni 3 600 s.
- Didascalia del movimento: < 0,02 "interfaccia ferma", ≤ 0,15 "lavoro", > 0,15 "video o animazioni".
- Giorno di fila: almeno 1 800 s di overlay a schermo. Settimana: da lunedi'.
- Versione 1.1.0. Ogni task: `build.ps1` (o `build-check`) verde e commit con trailer `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Script di modifica con Python: aprire i file con `newline=''` (mai convertire LF in CRLF).
- Prove a runtime: avvisare l'utente prima; copia e ripristino di `config.json`, `stats.json`, `windows-backup.json`, `history.json`; se `build\oled-guard.exe` e' in uso compilare in `build-check`.

## Review Focus

1. **Pagina bianca ferma sotto il mouse** (finestra attiva con `dim_activate = both`) → conta come ferma e a rischio, non in movimento (Task 2 `summary_active_window_still_counts_as_still`, Task 7 `gpu_summary_matches_model_active_window`).
2. **HDR molto luminoso** (luminanza 10-50× il bianco SDR, celle parziali ai bordi, risoluzioni non multiple di 16) → somme senza overflow, valori come il modello (Task 7 `gpu_summary_matches_model_hdr_odd_size`).
3. **Buco nelle misure** (pausa per un gioco, app chiusa una notte, sospensione del PC) → secchi del grafico vuoti, non zero; giorni di fila che si interrompono solo per giorni davvero mancati (Task 4 `history_timeline_gaps_are_empty`, `history_streak_counts_consecutive_days`).
4. **`history.json` mancante, rovinato o di una versione futura** → storico vuoto con avviso nel log, mai errore all'avvio (Task 5 `history_json_bad_input_is_empty`).
5. **Rischio protetto alto tutto il giorno** (oscuramento attivo) → nessun avviso; avviso solo per il rischio non protetto, al massimo uno all'ora (Task 6 `alert_ignores_when_below_and_limits_rate`).

---

## Milestone H1 — logica pura

### Task 1: Config dell'avviso e privacy, versione 1.1.0

**Files:**
- Modify: `src/core/config.h`, `src/core/config.cpp`, `CMakeLists.txt`
- Test: `tests/test_config.cpp`

**Interfaces:**
- Produces: campi di `Config`:
  ```cpp
  bool alert_enabled = true;
  std::string alert_threshold = "medium";  // low | medium | high
  int alert_after_min = 60;                // 5-240
  bool app_stats_enabled = true;
  ```
  `project(oled_guard VERSION 1.1.0 ...)`.

- [ ] **Step 1: Test che falliscono** — `config_alert_privacy_defaults` (valori sopra, nessun avviso), `config_alert_privacy_read_roundtrip` (`alert_threshold: "HIGH"` → "high", `alert_after_min: 5`, `app_stats_enabled: false`, roundtrip uguale), `config_alert_privacy_invalid` (`alert_threshold: "extreme"`, `alert_after_min: 4` e `241` → default con avviso contenente il nome del campo).
- [ ] **Step 2: Vederli fallire** (compilazione).
- [ ] **Step 3: Implementare** con i `Reader` esistenti (`Bool`, `Choice`, `Int`) e `ConfigToJson`; versione in CMake.
- [ ] **Step 4: Test verdi.**
- [ ] **Step 5: Commit** — `feat(core): config dell'avviso e della privacy, versione 1.1.0`

### Task 2: Cambio reale e luminanza per cella nel modello; `ComputeSummary`

**Files:**
- Modify: `src/core/dim_model.h`, `src/core/dim_model.cpp`
- Create: `src/core/summary_model.h`, `src/core/summary_model.cpp`, `tests/test_summary_model.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `ShadeParams`, `ShadeFactor` (`shade_model.h`).
- Produces (in `DimModel`):
  ```cpp
  // Istante dell'ultimo cambio reale del contenuto (stessa regola delle deviazioni
  // brevi di last_change, ma le finestre attive non lo toccano).
  double ContentChange(int cx, int cy) const;
  // Luminanza media della cella nell'ultimo frame (1 = bianco SDR).
  float MeanLuma(int cx, int cy) const;
  int Width() const; int Height() const;
  ```
- Produces (`summary_model.h`):
  ```cpp
  inline constexpr int kZoneCols = 32, kZoneRows = 18, kZones = kZoneCols * kZoneRows;
  inline constexpr double kStillS = 60, kMovingS = 10, kProtectedLevel = 0.05;
  struct FrameSummary {
    double risk = 0;                 // frazione dell'area (0-1)
    double risk_unprotected = 0;
    double motion = 0;
    double brightness = 0;           // luminanza media, 1 = bianco
    double light_still_before = 0;   // luce media sulle celle ferme (per area totale)
    double light_still_removed = 0;
    std::array<double, kZones> zone_light{};  // luce media arrivata al pannello per zona
  };
  // Zona di una cella: floor(cx*32/cols), floor(cy*18/rows).
  int ZoneOf(int cx, int cy, int cols, int rows);
  // Pesi per area in pixel (celle parziali ai bordi). Luce arrivata = luma*(1-livello)*fattore
  // al centro della cella; luce tolta = luma - luce arrivata.
  FrameSummary ComputeSummary(const DimModel& m, double now_s, const ShadeParams& shade);
  std::string_view MotionCaption(double motion);
  ```
- [ ] **Step 1: Test che falliscono** (scene da `tests/dim_scenes.h`, `CHECK_NEAR` 1e-9)
  - `model_content_change_ignores_active_zones`: una finestra attiva con contenuto fermo → `ContentChange` resta all'istante del suo ultimo cambio mentre `last_change` (via livello 0) riparte.
  - `model_mean_luma`: cella meta' bianca (1,0) e meta' nera → 0,5; cella parziale ai bordi (larghezza non multipla di 16) → media sui soli pixel reali.
  - `summary_still_bright_is_risk`: immagine 64×48 tutta nera con una cella bianca ferma da 61 s → `risk == 1/12`, `motion == 0`; a 59 s → `risk == 0`.
  - `summary_active_window_still_counts_as_still`: la stessa cella dentro una finestra attiva → `risk == 1/12`.
  - `summary_unprotected_follows_level`: con `Update` fino a livello > 0,05 → `risk_unprotected == 0`, `risk` invariato.
  - `summary_motion_and_brightness`: meta' immagine cambiata 5 s fa → `motion == 0.5`; immagine uniforme a 0,4 → `brightness == 0.4`.
  - `summary_light_removed_and_zones`: livello L e `ShadeParams{brightness 0.5}` → `light_still_removed / light_still_before == 1 - (1-L)*0.5`; `zone_light` della zona della cella = luce arrivata media della zona.
  - `summary_motion_caption`: 0,01 → "interfaccia ferma", 0,1 → "lavoro", 0,2 → "video o animazioni".
- [ ] **Step 2: Vederli fallire.**
- [ ] **Step 3: Implementare** — in `DimModel::OnFrame` un vettore `content_change_` aggiornato con la stessa regola di `last_change_` prima che `Update` applichi le finestre attive, e `mean_luma_`; `ComputeSummary` in un solo giro sulle celle.
- [ ] **Step 4: Test verdi** (anche tutti i test esistenti di `dim_model`).
- [ ] **Step 5: Commit** — `feat(core): riassunto dei segnali per zona e cambio reale delle celle`

### Task 3: Anello dei segnali

**Files:**
- Create: `src/core/signals.h`, `src/core/signals.cpp`, `tests/test_signals.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:
  ```cpp
  struct SignalSample { bool measured = false; double risk = 0, motion = 0, brightness = 0; };
  struct ChannelStats { double peak = 0, mean = 0; int samples = 0; };
  class SignalRing {
   public:
    static constexpr int kSize = 60;
    void Push(const SignalSample& s);
    std::vector<SignalSample> Samples() const;  // dal piu' vecchio, al massimo 60
    ChannelStats Risk() const; ChannelStats Motion() const; ChannelStats Brightness() const;  // solo misurati
    SignalSample Last() const;
  };
  ```
- [ ] **Step 1: Test che falliscono** — `signals_ring_keeps_last_60` (70 push → 60, il primo e' l'11°), `signals_stats_skip_unmeasured` (misurati 0,2 e 0,4 + 3 non misurati → peak 0,4, mean 0,3, samples 2), `signals_empty_ring` (stats a zero, `Last().measured == false`).
- [ ] **Step 2-4:** fallire, implementare, verdi.
- [ ] **Step 5: Commit** — `feat(core): anello dei segnali dal vivo`

### Task 4: Storico (`History`): grafico, mappa, uso giornaliero, interrogazioni

**Files:**
- Create: `src/core/history.h`, `src/core/history.cpp`, `tests/test_history.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `FrameSummary`, `kZones` (Task 2).
- Produces:
  ```cpp
  enum class ContentKind { StillWork, Video, Games, Away, ManualPause };
  inline constexpr int kContentKinds = 5;
  inline constexpr int kBucketS = 300, kTimelineBuckets = 2016, kDaysKept = 8, kAppsPerDay = 50;
  inline constexpr double kStreakDayS = 1800;

  // Un secondo di vita dell'app, gia' classificato dal chiamante.
  struct SecondSample {
    int64_t local_s = 0;          // secondi locali dall'epoca (ora locale come se fosse UTC)
    int day_key = 0;              // AAAAMMGG locale
    const FrameSummary* summary = nullptr;  // null = nessuna misura in questo secondo
    bool overlay_shown = false;
    std::optional<ContentKind> content;     // nessuna = non conta (blocco, errori)
    std::string app;              // exe in primo piano ("" = nessuna o statistiche per app spente)
    bool app_paused = false;      // tempo dell'app in pausa (altrimenti protetto)
    std::string monitor;          // nome amichevole del monitor protetto
  };
  struct WearView {
    std::array<double, kZones> normalized{};  // 0-1 rispetto alla zona peggiore
    int hottest = -1; double hottest_hours = 0; std::string hottest_label;  // "in alto a sinistra"...
    double concentration = 0; int over_double = 0; double measured_hours = 0; int64_t since_local_s = 0;
  };
  struct AppView { std::string exe; double protected_s = 0, paused_s = 0; };
  class History {
   public:
    void AddSecond(const SecondSample& s);
    void CountSession(int day_key);
    void Clear();
    // Media del rischio per secchio, dal piu' vecchio; nullopt dove non ci sono misure.
    std::vector<std::optional<double>> Timeline(int64_t now_local_s, int buckets) const;
    double RiskPeak(int64_t now_local_s, int buckets) const;
    WearView Wear(const std::string& monitor) const;
    std::vector<AppView> TopApps(int today_key, int n) const;          // ultimi 7 giorni
    std::array<double, kContentKinds> ContentShares(int today_key) const;  // frazioni, somma 1 (o tutte 0)
    std::pair<int, int> Streak(int today_key) const;                  // attuale, record
    std::optional<double> WearAvoidedToday(int today_key) const;      // frazione, nullopt senza dati
    std::optional<double> WearAvoidedTotal() const;
    int SessionsTotal() const; int SessionsThisWeek(int today_key) const;
    double ProtectedThisWeekS(int today_key) const;
    JsonValue ToJson() const;
    static History FromJson(const JsonValue& v);  // campi sbagliati ignorati
  };
  int DayKeyAddDays(int day_key, int days);  // calendario gregoriano
  int Weekday(int day_key);                  // 0 = lunedi'
  ```
  Regole: la mappa somma `zone_light × 1 s / 3600` per monitor; luce ferma prima/tolta sommata per giorno e in totale; i giorni oltre `kDaysKept` escono; al salvataggio restano le `kAppsPerDay` app piu' usate per giorno; i giorni "di fila" si tengono in un elenco di date (ultimi 400 giorni).

- [ ] **Step 1: Test che falliscono**
  - `history_timeline_buckets_average`: 300 s con rischio 0,2 poi 300 s con 0,4 → due secchi 0,2 e 0,4.
  - `history_timeline_gaps_are_empty`: un secchio senza misure fra due misurati → `nullopt`; dopo un salto di 3 giorni i secchi intermedi sono `nullopt` e quelli piu' vecchi di 2016 secchi spariscono.
  - `history_wear_map_hotspot`: 3 600 s con `zone_light` 1 nella zona 0 e 0,25 nelle altre → `hottest == 0`, `hottest_hours == 1`, label "in alto a sinistra", `concentration` = 1 / media, `over_double == 1`, `normalized[0] == 1`.
  - `history_wear_map_per_monitor`: secondi su due monitor → mappe separate.
  - `history_top_apps_and_content`: secondi su 2 app (protetti/in pausa) e categorie in 3 giorni → ordine per totale, quote che sommano 1, un giorno di 8 giorni fa escluso.
  - `history_streak_counts_consecutive_days`: giorni con 1 800 s il 1°, 2°, 3° e 5° → oggi = 5 → (1, 3); oggi = 6 senza secondi → (1, 3) (vale fino a ieri); oggi = 7 → (0, 3); passaggio fra fine mese e fine anno corretto.
  - `history_wear_avoided`: luce tolta 0,3 su 1 prima → 0,3; senza misure → `nullopt`.
  - `history_sessions_week`: sessioni lunedi', domenica precedente e oggi (mercoledi') → settimana 2, totale 3.
  - `history_weekday_and_add_days`: 20261123 → lunedi' (0); 20261231 + 1 → 20270101; 20240228 + 1 → 20240229.
- [ ] **Step 2-4:** fallire, implementare, verdi.
- [ ] **Step 5: Commit** — `feat(core): storico di rischio, usura, app e contenuti`

### Task 5: Persistenza di `History`

**Files:**
- Modify: `src/core/history.cpp`
- Test: `tests/test_history.cpp`

**Interfaces:**
- Produces: `History::ToJson` / `FromJson` con campo `"version": 1`; versione diversa o campi di tipo sbagliato → parti ignorate.
- [ ] **Step 1: Test che falliscono** — `history_json_roundtrip` (storico con tutti i tipi di dati → stesse interrogazioni dopo `FromJson(ToJson())`), `history_json_bad_input_is_empty` (`{}`, `[]`, `{"version": 99}`, numeri negativi o stringhe al posto di numeri → interrogazioni vuote, nessuna eccezione), `history_json_keeps_top_apps_per_day` (60 app in un giorno → 50 salvate, le piu' usate).
- [ ] **Step 2-4:** fallire, implementare, verdi.
- [ ] **Step 5: Commit** — `feat(core): salvataggio dello storico`

### Task 6: Modello dell'avviso

**Files:**
- Create: `src/core/alert_model.h`, `src/core/alert_model.cpp`, `tests/test_alert_model.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:
  ```cpp
  double AlertThreshold(std::string_view level);  // low 0.10, medium 0.20, high 0.35 (altro: medium)
  struct AlertParams { bool enabled = true; double threshold = 0.20; int after_min = 60; };
  class AlertModel {
   public:
    // unprotected_risk: nullopt se non misurato (pausa) = sotto soglia. Vero = mostra l'avviso ora.
    bool Update(double now_ms, std::optional<double> unprotected_risk, const AlertParams& p);
    double AboveForMs(double now_ms) const;  // 0 se sotto soglia
  };
  ```
- [ ] **Step 1: Test che falliscono**
  - `alert_fires_after_sustained_risk`: rischio 0,25 ogni secondo, soglia 0,20, 60 min → falso fino a 3 599 s, vero a 3 600 s.
  - `alert_short_dips_do_not_reset`: cali di 60 s sotto soglia ogni 10 min → scatta comunque a 60 min; un calo di 121 s → il conteggio riparte.
  - `alert_ignores_when_below_and_limits_rate`: rischio non protetto 0,05 con soglia 0,20 per 5 ore → mai; rischio alto continuo con `after_min = 5` → avvisi a 5 min, poi non prima di 60 min dal precedente.
  - `alert_disabled_and_unmeasured`: `enabled = false` → mai; `nullopt` per 3 minuti conta come sotto soglia.
- [ ] **Step 2-4:** fallire, implementare, verdi.
- [ ] **Step 5: Commit** — `feat(core): avviso per il rischio non protetto`

### Task 7: Protocollo dei segnali e dello storico

**Files:**
- Modify: `src/core/ui_protocol.h`, `src/core/ui_protocol.cpp`
- Test: `tests/test_ui_protocol.cpp`

**Interfaces:**
- Consumes: `SignalRing`, `MotionCaption`, `History` views (Task 2-4).
- Produces:
  ```cpp
  std::string SignalsMessage(const SignalSample& s);       // {type:"signals", measured, risk, motion, brightness, caption}
  std::string SignalsHistoryMessage(const SignalRing& r);  // {type:"signals", history:[[r,m,b]|null x <=60]}
  struct HistoryPayload {
    std::vector<std::optional<double>> timeline;  // 2016 secchi
    int64_t timeline_end_local_s = 0;
    double risk_peak_24h = 0;
    WearView wear; std::string wear_monitor;
    std::vector<AppView> top_apps; std::vector<std::string> top_app_rules;  // "auto" | "always" | "never"
    std::array<double, kContentKinds> content{};
    int sessions_total = 0, sessions_week = 0; int streak = 0, streak_best = 0;
    std::optional<double> avoided_today, avoided_total; double protected_week_s = 0;
  };
  std::string HistoryMessage(const HistoryPayload& p);    // {type:"history", ...} timeline con null
  // UiCommandType::ClearHistory ("clearHistory", senza campi)
  ```
- [ ] **Step 1: Test che falliscono** — `ui_signals_messages` (campi, didascalia, `history` con `null` per i non misurati), `ui_history_message_fields` (tutti i campi, `timeline` di 2016 con `null`, `avoided_today` null se assente, `content` di 5 valori), `ui_parse_clear_history`.
- [ ] **Step 2-4:** fallire, implementare, verdi.
- [ ] **Step 5: Commit** — `feat(core): messaggi dei segnali e dello storico`

## Milestone H2 — GPU

### Task 8: Luminanza e cambio reale in `CSDetect`, nuovo `CSSummary`

**Files:**
- Modify: `src/app/shaders/dim_cs.hlsl`, `src/app/dimmer.h`, `src/app/dimmer.cpp`, `CMakeLists.txt` (nuovo entry point `CSSummary`)
- Test: `tests_gpu/test_dimmer_gpu.cpp` (o `tests_gpu/test_summary_gpu.cpp` nuovo, aggiunto all'eseguibile dei test GPU)

**Interfaces:**
- Consumes: `FrameSummary`, `ComputeSummary`, `ZoneOf`, `kZones` (Task 2), `ShadeParams`.
- Produces (in `Dimmer`):
  ```cpp
  // Riassunto per i segnali (una volta al secondo): dispatch + copia in uno dei due
  // staging; TryReadSummary legge il piu' vecchio pronto senza attendere.
  void RequestSummary(double now_ms, const ShadeParams& shade);
  bool TryReadSummary(FrameSummary& out, double& measured_ms);
  ```
  In `CSDetect`: `luma_out` (u6, `float`, luminanza media della cella) e `content_change_out` (u7, `float`, stessa regola di `last_change_out` prima di `CSActivate`; al primo frame/ripresa come `last_change_out`). `CSSummary`: un thread per cella, somme a virgola fissa con `InterlockedAdd` in un `RWByteAddressBuffer` azzerato con `ClearUnorderedAccessViewUint`; scale e disposizione scelte dal builder e definite in un solo posto in C++ e in HLSL; nessun overflow per luminanze fino a 64× il bianco e griglie fino a 7680×4320 (480×270 celle).
- [ ] **Step 1: Test che falliscono** (WARP, `Sim` esistente esteso con `RequestSummary`/`TryReadSummary` in polling)
  - `gpu_summary_matches_model_sdr`: scena "icone sul desktop" dopo 90 s → ogni campo entro 1e-3 assoluto da `ComputeSummary`, ogni zona entro 1e-3.
  - `gpu_summary_matches_model_hdr_odd_size`: 1 366×768, HDR con zone a 40× il bianco → come sopra (tolleranza relativa 1e-3 sulle luci).
  - `gpu_summary_matches_model_active_window`: finestra attiva ferma → `risk` come il modello (non 0).
  - `gpu_summary_with_dim_and_shade`: livelli > 0 e `ShadeParams` con bordi → luce tolta e zone come il modello.
  - `gpu_summary_never_waits`: `TryReadSummary` subito dopo `RequestSummary` puo' fallire ma non blocca; entro 50 tentativi riesce.
- [ ] **Step 2: Eseguire `build-check\oled-guard-gpu-tests.exe` e vederli fallire.**
- [ ] **Step 3: Implementare shader e `Dimmer`.**
- [ ] **Step 4: Test GPU (tutti, anche quelli esistenti) e unitari verdi.**
- [ ] **Step 5: Commit** — `feat(app): riassunto dei segnali sulla GPU`

## Milestone H3 — app

### Task 9: Cadenza nel render e `SharedSignals`

**Files:**
- Create: `src/app/shared_signals.h`, `src/app/shared_signals.cpp`
- Modify: `src/app/render_thread.h`, `src/app/render_thread.cpp`, `CMakeLists.txt`

**Interfaces:**
- Consumes: `Dimmer::RequestSummary/TryReadSummary` (Task 8), `SharedShade` (progetto 1).
- Produces:
  ```cpp
  class SharedSignals {
   public:
    void Publish(const FrameSummary& s, double measured_ms);
    // L'ultimo riassunto se misurato da al massimo max_age_ms.
    std::optional<FrameSummary> Latest(double now_ms, double max_age_ms);
  };
  ```
  `RenderParams::signals` (`SharedSignals*`). Nel ciclo: se `dim_ok && have_frame` e sono passati ≥ 1 000 ms dall'ultima richiesta → `RequestSummary(now, shade_now)`; ogni giro `TryReadSummary` e, se riesce, `Publish`; `next = min(next, ultima richiesta + 1000)` e, con una lettura in sospeso, `min(next, now + 16)`. Nessun ridisegno in piu'.
- [ ] **Step 1: Implementare.**
- [ ] **Step 2: Build e tutti i test verdi.**
- [ ] **Step 3: Commit** — `feat(app): riassunto dei segnali ogni secondo dal render`

### Task 10: Segnali, storico, sessioni e avviso nell'app

**Files:**
- Modify: `src/app/app.h`, `src/app/app.cpp`, `src/app/paths.h`/`paths.cpp` (se serve `HistoryPath`), `src/app/stats_file.*` (o nuovo `src/app/history_file.*`)

**Interfaces:**
- Consumes: `SharedSignals` (Task 9), `SignalRing`, `History`, `AlertModel`, messaggi (Task 3-7).
- Produces (comportamento):
  - Avvio: `History` da `AppDataDir()/history.json` (mancante o rovinato: vuoto + `LogWarn`), `CountSession(oggi)`.
  - `OnSecond`: `Latest(now, 3000)` → `SignalSample` (non misurato se assente) → `ring_.Push`; `SecondSample` con `local_s`/`day_key` dall'ora locale, `content` (pausa `Manual` → `ManualPause`; `NotificationState`/`FullscreenWindow`/`Denylisted` → `Games`; assente → `Away`; misurato con movimento > 0,15 → `Video`; overlay a schermo → `StillWork`; altro → nessuna), `app` = primo piano se `app_stats_enabled` e (overlay a schermo o pausa causata da app), `monitor` = nome del monitor protetto → `history_.AddSecond`; `AlertModel::Update(now, risk_unprotected se misurato)` con i parametri della config → se vero `tray_.ShowBalloon` con il testo dello spec (minuti e % arrotondati) e `Record("alert", ...)`.
  - A pagina pronta: `SignalsMessage` ogni secondo; a `ready` anche `SignalsHistoryMessage` e `HistoryMessage`; `HistoryMessage` ogni 60 s.
  - `history.json` salvato con `kTimerStatsSave` e all'uscita (scrittura atomica come `SaveStats`).
  - `ClearHistory` → `history_.Clear()`, salvataggio, `HistoryMessage`, evento "Storico cancellato".
  - Icona dell'attivita' `alert` nel front-end (Task 12).
- [ ] **Step 1: Implementare.**
- [ ] **Step 2: Build e tutti i test verdi.**
- [ ] **Step 3: Commit** — `feat(app): segnali dal vivo, storico e avviso`

## Milestone H4 — interfaccia

Verifica di ogni task: anteprima (`ui-anteprima`), `read_console_messages` pulito (dopo un
ricaricamento senza cache), screenshot, 1 280×800 e 1 000×640 senza scorrimento
orizzontale. I dati finti stanno solo in `mock.js`.

### Task 11: Striscia dei segnali dal vivo

**Files:**
- Modify: `src/ui/index.html`, `src/ui/app.js`, `src/ui/app.css`, `src/ui/mock.js`

**Interfaces:**
- Consumes: messaggi `signals` (campi del Task 7), `history` (`risk_peak_24h`, `avoided_today`).
- Produces: `store.signals` (ultimi 60 campioni `{measured, risk, motion, brightness}`) e `store.history`; funzione `sparkline(svg, values, colorVar)` riusabile.
- Striscia come §6.6 dello spec: intestazione "● SEGNALI DAL VIVO · ULTIMI 60 S" e "Apri statistiche ›"; tre riquadri con titolo, valore corrente in %, grafico a linea con area sfumata (Rischio `--warn`, Movimento `--info`, Luminosita' `--accent`), riga sotto in monospazio (`picco N · media N`, `media N · <didascalia>`, `media N (APL)`); quarto riquadro con Sessione, Totale protetto, Picco rischio 24 h, Usura evitata oggi. Senza misure: linea interrotta e "in pausa".
- [ ] **Step 1: Implementare** (i grafici si ridisegnano al massimo una volta al secondo).
- [ ] **Step 2: Verifica in anteprima** come sopra; in `mock.js` una pausa simulata mostra il buco nella linea.
- [ ] **Step 3: Commit** — `feat(ui): striscia dei segnali dal vivo`

### Task 12: Pagina Statistiche completa

**Files:**
- Modify: `src/ui/pages.js`, `src/ui/app.css`, `src/ui/mock.js`

**Interfaces:**
- Consumes: `store.history` (Task 11), `store.stats` (progetto 1).
- Contenuto come §6.6 dello spec: quattro riquadri (Protetto in totale + questa settimana; Sessioni + questa settimana; Usura evitata oggi + totale; Giorni di fila + record); Mappa di usura 32×18 con scala blu scuro → verde acqua → verde → ambra → rosso, legenda, quattro numeri (misurato da / ore, zona peggiore, concentrazione, zone oltre il doppio) e nota "tempo in pausa non misurato"; Grafico del rischio con segmenti 6 h / 24 h / 7 g (72 / 288 / 2016 secchi; 7 g raggruppato a 1 h), area ambra, griglia 25/50/75%, orari, vuoti, riga "picco · media · attuale"; App piu' usate (fino a 8: avatar, nome, etichetta della regola, barra protetto/in pausa, tempo); Usura per contenuto (barra unica a colori, legenda con %); sotto, le parti del progetto 1 (posizioni dello spostamento, pause di oggi). La riga "arriveranno con il prossimo aggiornamento" si toglie. Icona `alert` nell'attivita'.
- [ ] **Step 1: Implementare.**
- [ ] **Step 2: Verifica in anteprima** — cambio 6 h/24 h/7 g, mappa con zona peggiore evidenziata, nessun overflow.
- [ ] **Step 3: Commit** — `feat(ui): pagina Statistiche con mappa di usura, grafico e app`

### Task 13: Notifiche e Privacy nelle Impostazioni

**Files:**
- Modify: `src/ui/pages.js`, `src/ui/mock.js`

**Interfaces:**
- Consumes: campi del Task 1, comando `clearHistory`.
- Pannello "Notifiche" (avviso attivo; soglia Bassa/Media/Alta con le percentuali; "Avvisa dopo" 5-240 min) e pannello "Privacy" (statistiche per app con descrizione "restano sul PC"; "Cancella lo storico" che chiede conferma con un secondo clic entro 5 s: il pulsante diventa "Conferma: cancella").
- [ ] **Step 1: Implementare.**
- [ ] **Step 2: Verifica in anteprima** — doppio clic necessario per cancellare, un solo clic non fa nulla dopo 5 s.
- [ ] **Step 3: Commit** — `feat(ui): notifiche e privacy nelle impostazioni`

## Milestone H5 — prova dal vivo

### Task 14: Prova sulla TV

- [ ] **Step 1: Build completa e test verdi.**
- [ ] **Step 2: Avvisare l'utente, copiare `config.json`, `stats.json`, `windows-backup.json`, `history.json`, avviare `build-check\oled-guard.exe --debug`** (dire all'utente quale exe riaprire se chiude l'app).
- [ ] **Step 3: Checklist breve per l'utente**:
  1. La striscia in basso mostra i tre grafici che si muovono; trascinando una finestra il Movimento sale.
  2. Una pagina bianca ferma per 2 minuti fa salire il Rischio.
  3. Luminosita' cambia passando da una finestra scura a una chiara.
  4. Statistiche: mappa, grafico (6 h), app e contenuti popolati dopo qualche minuto.
  5. Con soglia Bassa, "Avvisa dopo" 5 min e oscuramento spento, dopo 5 minuti di pagina bianca ferma arriva l'avviso; con l'oscuramento acceso no.
  6. Cancella lo storico chiede conferma e azzera la pagina.
- [ ] **Step 4: Ripristinare i file**, correggere cio' che non va (con test se tocca il core), commit.
