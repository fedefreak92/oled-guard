# Oscuramento delle aree statiche — piano di implementazione

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** riconoscere sulla GPU gli elementi piccoli fermi e luminosi del desktop e
abbassarne gradualmente la luminanza nell'overlay, con una scheda "Oscuramento"
nell'interfaccia per vederli e regolarli.

**Architecture:** una griglia di celle 16×16 px vive in texture GPU possedute da una
nuova classe `Dimmer` sul thread di render: un compute shader confronta ogni frame nuovo
con la cache precedente, altri passaggi calcolano densita', classificazione e livello;
il pixel shader esistente moltiplica i colori per `1 − level` letto con interpolazione
bilineare. Il comportamento e' definito da un modello di riferimento puro su CPU
(`DimModel`, in `src/core`) testato in TDD e usato come oracolo dei test degli shader su
device WARP. La UI riceve la mappa con una lettura asincrona solo a scheda visibile.

**Tech Stack:** C++20, MSVC, CMake + Ninja, D3D11 (cs_5_0/ps_5_0 con fxc), DXGI,
WebView2 + HTML/CSS/JS vanilla.

**Spec:** `docs/superpowers/specs/2026-11-21-oled-guard-dimming-design.md`

## Global Constraints

- Logica pura in `src/core` senza header di Windows; codice Win32/D3D in `src/app`; UI in `src/ui` (CSP senza inline, niente librerie).
- Commenti e testi UI in italiano, identificatori in inglese. Nessuna dipendenza nuova.
- Pixel del desktop mai letti dalla CPU. Unica lettura ammessa: la texture dei livelli, asincrona (`D3D11_MAP_FLAG_DO_NOT_WAIT`), solo mentre `watch` e' attivo.
- Nessuna attesa bloccante nuova sul thread di render (watchdog: 3 s).
- Con livello 0 ovunque (oscuramento spento, sospeso, o nulla da oscurare) l'output del pixel shader e' identico bit per bit a quello attuale.
- Valori di default e intervalli esattamente come la tabella §5.1 dello spec (riportati nel Task 1).
- Costanti dell'algoritmo: celle 16 px; finestre 33×33 celle (raggio 16); "luminosa" = ≥ 1/8 dei pixel con luminanza ≥ soglia × bianco; "contenuto" = luminosa oppure (max − min luminanza) ≥ 0,10 × bianco; ritorni: contenuto cambiato `dim_recover_ms`, comando 1 s, grande area 30 s; ridisegni 500 ms.
- Ogni milestone: `.\build.ps1` verde, commit locale con trailer `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`, checklist visiva breve per l'utente.
- Prove a runtime: avvisare l'utente prima; copia e ripristino di `%APPDATA%\oled-guard\config.json` e `stats.json`; se `build\oled-guard.exe` e' in uso, compilare in una cartella separata (`cmake -B build-dim`).

## Review Focus

1. **Oscuramento spento o nulla da oscurare** → immagine identica bit per bit a prima (Task 6, test `ShiftPsLevelZeroIsBitExact`).
2. **Risoluzioni non multiple di 16** (es. 1366×768, 2560×1080) → celle parziali gestite senza letture fuori texture e con densita' normalizzata ai bordi (Task 2 `PartialCellsAtEdges`, Task 5 `GpuMatchesModelOddSize`).
3. **Sospensione del PC o salto di tempo** (dt enorme fra due ridisegni) → nessun livello oltre il bersaglio, nessun NaN (Task 2 `HugeDtDoesNotOvershoot`).
4. **Config scritta a mano** con `dim_large_delay_s < dim_delay_s` o nomi .exe in maiuscolo nelle esclusioni → default + avviso, nomi in minuscolo (Task 1).
5. **Cambio di formato o risoluzione a meta' sessione** (toggle HDR, cambio modalita') → il dimmer si ricrea con la nuova griglia e riparte da zero (Task 5 `ResetChangesGrid`).

---

## Milestone G1 — logica pura

### Task 1: Campi di config dell'oscuramento

**Files:**
- Modify: `src/core/config.h`, `src/core/config.cpp`
- Test: `tests/test_config.cpp`

**Interfaces:**
- Produces: nuovi campi di `Config`:
  ```cpp
  bool dim_enabled = true;
  int dim_strength_pct = 40;          // 10-80
  int dim_delay_s = 180;              // 30-3600
  int dim_ramp_s = 300;               // 30-3600
  int dim_recover_ms = 500;           // 100-5000
  bool dim_large_enabled = false;
  int dim_large_delay_s = 1200;       // 300-14400
  int dim_large_strength_pct = 20;    // 5-80
  int dim_bright_threshold_pct = 25;  // 5-100
  int dim_large_fraction_pct = 40;    // 10-90
  std::vector<std::string> dim_exclude;
  ```

- [ ] **Step 1: Test che falliscono** in `tests/test_config.cpp`:
  - `DimDefaults`: `Config{}` ha i valori sopra; `ConfigToJson` → `ConfigFromJson` round-trip uguale (`CHECK(result.config == original)`) con tutti i campi dim cambiati a valori validi non di default.
  - `DimRangesValidated`: `{"dim_strength_pct": 90}` → resta 40 e 1 avviso; `{"dim_delay_s": 10}` → 180; `{"dim_recover_ms": 6000}` → 500; `{"dim_large_fraction_pct": 5}` → 40.
  - `DimLargeDelayNotBeforeDelay`: `{"dim_delay_s": 600, "dim_large_delay_s": 400}` → entrambi ai default (180, 1200) e un avviso che nomina `dim_large_delay_s`.
  - `DimExcludeLowercased`: `{"dim_exclude": ["Photoshop.EXE", " vlc.exe "]}` → `{"photoshop.exe", "vlc.exe"}`.
- [ ] **Step 2:** `.\build.ps1` → i nuovi test falliscono (compilazione o FAIL).
- [ ] **Step 3: Implementare** lettura con `r.Int/Bool/List`, serializzazione in `ConfigToJson` (dopo `log_level`), e la regola `dim_large_delay_s ≥ dim_delay_s` accanto a `FixRange` (testo: `"dim_delay_s/dim_large_delay_s: le grandi aree devono iniziare dopo gli elementi piccoli, uso 180-1200"`).
- [ ] **Step 4:** `.\build.ps1` → tutti verdi.

### Task 2: Parametri e modello di riferimento (`DimModel`)

**Files:**
- Create: `src/core/dim_params.h`, `src/core/dim_params.cpp`, `src/core/dim_model.h`, `src/core/dim_model.cpp`
- Modify: `CMakeLists.txt` (sorgenti di `oled_core` e test)
- Test: `tests/test_dim_model.cpp`

**Interfaces:**
- Consumes: campi `dim_*` di `Config` (Task 1).
- Produces:
  ```cpp
  // dim_params.h
  struct DimParams {
    bool enabled = true;
    double strength = 0.40;        // frazione 0-1
    double delay_s = 180, ramp_s = 300, recover_s = 0.5;
    bool large_enabled = false;
    double large_delay_s = 1200, large_strength = 0.20;
    double bright_threshold = 0.25;  // frazione del bianco SDR
    double large_fraction = 0.40;
    bool operator==(const DimParams&) const = default;
  };
  DimParams ToDimParams(const Config& c);
  inline constexpr int kDimCellPx = 16;
  inline constexpr int kDimWindowRadius = 16;       // finestre 33x33 celle
  inline constexpr double kDimBrightPixelFraction = 0.125;
  inline constexpr double kDimDetailRange = 0.10;
  inline constexpr double kDimMaxLevel = 0.8;       // limite superiore di strength
  inline constexpr double kDimCommandRecoverS = 1.0;
  inline constexpr double kDimLargeRecoverS = 30.0;

  // dim_model.h — riferimento su CPU dell'algoritmo §5.2 dello spec
  struct LumaImage { int width = 0, height = 0; std::vector<float> luma; };  // luminanza lineare / bianco SDR
  class DimModel {
   public:
    DimModel(int width, int height, double now_s);  // tutte le celle: last_change = now, level = 0
    void OnFrame(const LumaImage& frame, double now_s);  // primo frame: tutte le celle cambiate
    void Update(double now_s, double dt_s, const DimParams& p, bool suspended);
    int Cols() const; int Rows() const;
    float Level(int cx, int cy) const;
    bool Bright(int cx, int cy) const; bool Content(int cx, int cy) const;
    bool Large(int cx, int cy) const;      // valore dell'ultimo Update
    double Target(int cx, int cy) const;   // valore dell'ultimo Update
    const std::vector<float>& Levels() const;  // riga per riga, Cols()*Rows()
  };
  ```

Algoritmo di `Update` (lo spec §5.2 e' la fonte; qui le decisioni che non fissa):
```
age = now - last_change
M = bright && age >= delay_s ; C = content && age >= delay_s
D(c) = somma C nella finestra 33x33 tagliata allo schermo / numero di celle in quella finestra
Large(c) = max di D nella finestra 33x33 tagliata >= large_fraction
cap = !enabled || suspended ? 0
      : M && !Large ? strength
      : M && Large && large_enabled ? large_strength : 0
start, ramp_time = Large ? (large_delay_s, ramp_s) : (delay_s, ramp_s)
target = cap * clamp((age - start) / ramp_time, 0, 1)
se level < target: level = min(target, level + cap / ramp_s * dt)
se level > target: rate = max di quelle che valgono:
   kDimMaxLevel / recover_s           se age < delay_s oppure !bright      (contenuto cambiato)
   kDimMaxLevel / kDimCommandRecoverS se suspended || !enabled || (cap > 0 && level > cap)
   kDimMaxLevel / kDimLargeRecoverS   altrimenti (diventata grande area / non piu' elemento)
   level = max(target, level - rate * dt)
```
`OnFrame`: per ogni cella (anche parziale) confronta i pixel con il frame precedente;
se almeno uno differisce → `last_change = now`. `bright` = pixel con luma ≥ soglia
almeno 1/8 dei pixel **presenti** nella cella; `content` = bright oppure max−min ≥
`kDimDetailRange`. La soglia di `bright` dipende da `DimParams`: `OnFrame` usa
l'ultima `bright_threshold` ricevuta da `Update` (default 0,25 prima del primo Update).

- [ ] **Step 1: Test che falliscono** in `tests/test_dim_model.cpp` (immagini sintetiche 1024×576 = 64×36 celle salvo dove indicato; `dt` = 0,5 s; `p` = `DimParams{}` salvo dove indicato; helper locale `Run(model, frames, from_s, to_s)` che chiama `OnFrame` quando il frame cambia e `Update` ogni 0,5 s):
  - `ToDimParamsConverts`: `Config{}` → strength 0,40, delay 180, ramp 300, recover 0,5, large 1200/0,20, soglia 0,25, frazione 0,40.
  - `GridSizeRoundsUp`: 1000×600 → 63×38 celle.
  - `StaticSmallBarDims`: fondo nero, barra bianca (luma 1) alta 80 px in fondo, larga tutto lo schermo; nella meta' alta un quadrato 256×256 con rumore che cambia a ogni frame. A t = 180 s livello della cella (10, 33) = 0; a t = 330 s ≈ 0,20 (±0,01); a t = 480 s = 0,40; `Large` falso.
  - `LargeWhiteAreaNotDimmed`: tutto lo schermo bianco fermo → a t = 600 s ogni livello = 0, `Large` vero al centro e negli angoli.
  - `DarkTextAreaIsLarge`: fondo nero con "testo" nelle righe di celle 0–30 (y < 496): righe di trattini luma 0,8 ogni 24 px (riga k a y = 24k), trattini di 8 px accesi e 8 spenti, per il 90% della larghezza; le righe con k multiplo di 3 sono "in grassetto" alte 4 px (32 px accesi per cella = 1/8 → `Bright`), le altre alte 3 px (solo `Content`). Le celle luminose sono poche (densita' ≈ 0,2: senza la regola B sarebbero "elementi piccoli" e si scurirebbero), quelle con contenuto ≈ 0,6. Nelle ultime 5 righe di celle icone bianche 32×32 ogni 96 px → a t = 600 s: celle del testo con `Content` vero, `Large` vero, livello 0 anche per quelle `Bright`; le icone (riga 32) `Large` vero e livello 0 (limite accettato).
  - `IconsOnBlackDim`: solo icone bianche 32×32 sparse ogni 200 px su nero → livelli = 0,40 a t = 480 s.
  - `ChangeRecoversFast`: dopo `StaticSmallBarDims` a 480 s, cambiare un pixel della cella (10, 33) → dopo 0,5 s livello di quella cella = 0; cella (40, 33) invariata 0,40.
  - `BecomingLargeRecoversSlow`: barra dimmata a 0,40, poi la meta' alta diventa bianca e ferma: dopo che quella zona supera `delay` la barra e' `Large`; livello della barra scende di 0,8/30 al secondo (a +15 s dalla classificazione ≈ 0,0).
  - `SuspendRecoversInOneSecond`: barra a 0,40, `Update(..., suspended=true)` → dopo 0,5 s = 0,0 (0,8/1 × 0,5 = 0,4).
  - `LargeAreasOptional`: schermo bianco fermo, `large_enabled = true`: a t = 1200 s livello 0; a t = 1350 s ≈ 0,10; a t = 1500 s = 0,20.
  - `PartialCellsAtEdges` (Review Focus 2): 1000×600, icona bianca 8×8 nell'angolo in basso a destra (cella parziale 62,37) → `Bright` vero e livello 0,40 a 480 s; nessun crash.
  - `HugeDtDoesNotOvershoot` (Review Focus 3): barra ferma, un solo `Update(now=10000, dt=10000)` → livello = target = 0,40, nessun NaN; `Update` con `dt = 0` lascia i livelli invariati.
- [ ] **Step 2:** `.\build.ps1` → falliscono.
- [ ] **Step 3: Implementare** `ToDimParams` e `DimModel` (somme di finestra con prefissi o separabili: libero).
- [ ] **Step 4:** `.\build.ps1` → verdi. Se `DarkTextAreaIsLarge` o `StaticSmallBarDims` non passano con `large_fraction = 0,40`, **fermarsi** e riferire i valori di D misurati invece di cambiare le scene di test: e' una taratura dello spec.

### Task 3: Regola dei ridisegni (`DimRedrawSchedule`)

**Files:**
- Create: `src/core/dim_schedule.h`, `src/core/dim_schedule.cpp`
- Test: `tests/test_dim_schedule.cpp`

**Interfaces:**
- Consumes: `DimParams`, costanti del Task 2.
- Produces:
  ```cpp
  class DimRedrawSchedule {
   public:
    void OnNewFrame(double now_ms);
    void Poke(double now_ms);          // parametri cambiati, sospensione, evidenziazione on/off
    void OnRedraw(double now_ms);
    // Istante assoluto del prossimo ridisegno dovuto all'oscuramento; +infinito se nessuno.
    double NextRedrawAt(double now_ms, const DimParams& p, double frame_period_ms) const;
  };
  inline constexpr double kDimSlowRedrawMs = 500;
  ```
  Regole: finestra veloce dopo l'ultimo frame `[f, f + recover_s*1000 + 100]` e dopo l'ultimo poke `[k, k + 1100]` → `last_redraw + frame_period`; finestra lenta fino a `max(f, k) + (max(delay_s, large_enabled ? large_delay_s : 0) + ramp_s + kDimLargeRecoverS + 3) * 1000` → `last_redraw + 500`; con `!p.enabled` solo la finestra veloce del poke; mai prima di `now_ms`.

- [ ] **Step 1: Test che falliscono**: `NoActivityNoRedraw` (schedule nuovo → +inf), `FastAfterFrame` (frame a 1000, redraw a 1000 → prossimo 1007 con periodo 7), `SlowAfterFastWindow` (redraw a 1700, frame a 1000 → 2200), `StopsAfterBound` (frame a 0, default → +inf a partire da `(180+300+30+3)*1000`), `LargeExtendsBound` (`large_enabled` → limite con 1200), `PokeOpensFastWindow`, `DisabledOnlyPokeWindow` (`enabled=false`, frame recente → +inf; dopo un poke veloce per 1100 ms e poi +inf), `NeverInThePast` (risultato ≥ now).
- [ ] **Step 2:** `.\build.ps1` → falliscono. **Step 3:** implementare. **Step 4:** verdi.

### Task 4: Protocollo della scheda

**Files:**
- Modify: `src/core/ui_protocol.h`, `src/core/ui_protocol.cpp`
- Test: `tests/test_ui_protocol.cpp`

**Interfaces:**
- Produces:
  ```cpp
  struct DimMapSummary { int cols = 0, rows = 0; std::string cells; double dimmed_pct = 0; double max_level = 0; };
  // Riduce con il massimo per blocchi finche' cols <= max_cols (fattore intero, uguale sui due assi),
  // quantizza a 16 passi (0-9, a-f) con round(level / kDimMaxLevel * 15).
  // dimmed_pct e max_level sulla griglia piena (celle con level > 0.01).
  DimMapSummary SummarizeDimLevels(const std::vector<float>& levels, int cols, int rows, int max_cols);
  struct DimUiState {
    std::string status;        // "active" | "waiting" | "suspended" | "off" | "paused"
    std::string suspended_by;  // .exe se sospeso da un'app
    DimMapSummary map;         // vuota se non disponibile
    double sdr_white_nits = 0; bool hdr = false; double highlight_left_ms = 0;
  };
  std::string DimMessage(const DimUiState& s);
  // {type:"dim", status, suspended_by, cols, rows, cells, dimmed_pct, max_level, sdr_white_nits, hdr, highlight_left_ms}
  enum class UiCommandType { ..., DimWatch, DimHighlight };  // "dimWatch" {on}, "dimHighlight" {}
  ```
- [ ] **Step 1: Test che falliscono**: `SummarizeKeepsSmallGrid` (4×2, livelli `{0,0.8,0.4,0, ...}` → `"0f80..."`, `max_level` 0,8), `SummarizeDownsamplesWithMax` (240×135, una cella a 0,4 → con max 120: 120×68, carattere `'8'` nel blocco giusto, `dimmed_pct` = 100/32400), `DimMessageFields` (parse del JSON prodotto e controllo dei campi), `ParseDimCommands` (`{"cmd":"dimWatch","on":true}` → `DimWatch`, on; senza `on` → nullopt; `{"cmd":"dimHighlight"}` → `DimHighlight`).
- [ ] **Step 2–4:** rosso, implementare, verde.
- [ ] **Step 5: Commit G1**
  ```
  git add src/core tests CMakeLists.txt
  git commit -m "feat(core): modello dell'oscuramento, regola dei ridisegni e protocollo"
  ```
  (con il trailer Co-Authored-By). Nessuna checklist visiva in G1: niente cambia a schermo.

---

## Milestone G2 — pipeline GPU

### Task 5: Compute shader e `Dimmer`, con test WARP

**Files:**
- Create: `src/app/shaders/dim_cs.hlsl`, `src/app/dimmer.h`, `src/app/dimmer.cpp`, `tests_gpu/test_dimmer_gpu.cpp`, `tests_gpu/gpu_test_util.h`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `DimParams`, costanti, `DimModel` (oracolo nei test).
- Produces:
  ```cpp
  class Dimmer {
   public:
    // Crea texture della griglia e shader per una cattura width x height. Non logga (usato nei test).
    HRESULT Init(ID3D11Device* device, UINT width, UINT height, DXGI_FORMAT format, double now_ms);
    void Shutdown();
    bool Ready() const;
    UINT Cols() const; UINT Rows() const;
    // Frame nuovo, prima che la cache venga sovrascritta. sdr_white: valore del bianco nel formato della cattura (1.0 in SDR).
    HRESULT Detect(ID3D11Texture2D* frame, ID3D11Texture2D* previous_cache, double now_ms, const DimParams& p, float sdr_white);
    // Classificazione e livello; dt dal precedente Update (il primo usa 0).
    void Update(double now_ms, const DimParams& p, bool suspended);
    ID3D11ShaderResourceView* LevelsSrv() const;  // R32_FLOAT, Cols x Rows
    // Lettura asincrona (Task 9): RequestReadback copia in staging; TryReadback mappa con DO_NOT_WAIT.
    void RequestReadback();
    bool TryReadback(std::vector<float>& levels);
  };
  ```
  Texture della griglia (UAV + SRV): `last_change` R32_FLOAT, `flags` R32_UINT (bit 0 bright, bit 1 content), `sums` R32G32_FLOAT (somma, conteggio) per i passaggi separabili, `density` R32_FLOAT, `large` R32_FLOAT, `levels` R32_FLOAT. Il primo frame dopo `Init` marca tutte le celle cambiate (flag interno `first_frame_`).
  Shader: un file `dim_cs.hlsl` con entry point `CSDetect` (gruppo 16×16 thread = una cella, riduzione in groupshared: changed/count bright/min/max; SDR = decodifica sRGB esatta da UNORM; luma Rec.709 / sdr_white), `CSDensityH`, `CSDensityV`, `CSMaxH`, `CSMaxV`, `CSLevel` (gruppi 8×8). Constant buffer unico `DimConstants` (now_s, dt_s, parametri, cols, rows, width, height, is_sdr, sdr_white, flag suspended/first_frame).
  CMake: nuova funzione `add_shader_entry(var file entry profile)` che genera `${SHADER_OUT}/${var}.h` con `/E ${entry} /Vn g_${var}`; libreria statica `oled_gpu` con `dimmer.cpp` + header shader, usata da `oled-guard` e da `oled-guard-gpu-tests` (`tests/test_main.cpp` + `tests_gpu/*.cpp`, link `oled_core oled_gpu d3d11 dxgi`), registrata con `add_test`.
  Se `CreateShaderResourceView(frame)` fallisce, `Detect` copia `frame` in una texture propria (creata alla prima necessita') e usa quella.

- [ ] **Step 1: Test che falliscono** in `tests_gpu/test_dimmer_gpu.cpp` con device WARP (`D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, ...)`); `gpu_test_util.h` crea texture da `LumaImage` (in HDR: R16G16B16A16_FLOAT con rgb = luma × white; in SDR: B8G8R8A8 con valore sRGB codificato della luma) e legge `LevelsSrv` via staging:
  - `GpuMatchesModelBar`: scena `StaticSmallBarDims` del Task 2, HDR con white 2,5; simulazione 0–480 s a passi di 0,5 s → `|gpu − model| ≤ 0,002` per ogni cella a 330 s e 480 s.
  - `GpuMatchesModelDarkText`: scena `DarkTextAreaIsLarge`, SDR → stessi livelli del modello a 600 s.
  - `GpuMatchesModelOddSize` (Review Focus 2): 1000×600 con l'icona nella cella parziale.
  - `GpuChangeRecovers`: come `ChangeRecoversFast`.
  - `ResetChangesGrid` (Review Focus 5): `Init` 1024×576 poi `Init` 1920×1080 → `Cols/Rows` = 120×68, livelli tutti 0.
- [ ] **Step 2:** `.\build.ps1` → falliscono.
- [ ] **Step 3: Implementare** shader, `Dimmer`, CMake.
- [ ] **Step 4:** `.\build.ps1` → verdi (anche `oled-guard-gpu-tests`).

### Task 6: Applicazione nel pixel shader

**Files:**
- Create: `src/app/shift_constants.h`
- Modify: `src/app/shaders/shift_ps.hlsl`, `src/app/presenter.h`, `src/app/presenter.cpp`
- Test: `tests_gpu/test_shift_ps_gpu.cpp`

**Interfaces:**
- Consumes: `Dimmer::LevelsSrv()`, `Cols/Rows`.
- Produces:
  ```cpp
  // shift_constants.h — condiviso fra presenter e test
  struct ShiftConstants {
    int offset[2]; unsigned size_minus_one[2];
    float grid_size[2];       // cols*16, rows*16 in pixel
    float sdr_white;          // valore del bianco nel formato di uscita
    unsigned flags;           // bit0 is_sdr, bit1 highlight, bit2 dimming attivo
  };  // multiplo di 16 byte
  struct DimDrawInputs { ID3D11ShaderResourceView* levels = nullptr; UINT cols = 0, rows = 0; float sdr_white = 1; bool highlight = false; };
  HRESULT Presenter::Draw(ID3D11Texture2D* source, Offset offset, const DimDrawInputs& dim);
  ```
  Shader: `level = levels.SampleLevel(linear_clamp, (src + 0.5) / grid_size, 0)`; se `!(flags & 4) || level <= 0` → ritorna il pixel com'e' (bit-exact); HDR: `rgb *= 1 − level`; SDR: decodifica sRGB, moltiplica, ricodifica; alfa invariata. Evidenziazione: `rgb = lerp(rgb, ambra × sdr_white, saturate(level / 0.8) × 0.7)` con ambra = sRGB(#ffb020) linearizzata (in SDR ricodificata). Sampler `D3D11_FILTER_MIN_MAG_MIP_LINEAR`, `CLAMP`, creato in `Presenter::Init`. Senza dimmer pronto, `levels == nullptr` → flag bit2 spento.
- [ ] **Step 1: Test che falliscono** in `tests_gpu/test_shift_ps_gpu.cpp` (WARP, render target offscreen dello stesso formato, vertex shader `fullscreen_vs`): `ShiftPsLevelZeroIsBitExact` (Review Focus 1; texture con valori casuali FP16 e BGRA, livelli 0 → `memcmp` uguale alla sorgente con offset 0), `ShiftPsHalvesLinearHdr` (livello 0,5 uniforme, HDR → rgb dimezzato ±1e-3), `ShiftPsSdrDimsInLinear` (BGRA 255 → livello 0,5 → valore codificato = sRGB(0,5) = 188 ±1).
- [ ] **Step 2–4:** rosso, implementare, verde. `presenter.cpp` usa `shift_constants.h` al posto della struct locale.

### Task 7: Cattura, render thread e `SharedDim`

**Files:**
- Create: `src/app/shared_dim.h`, `src/app/shared_dim.cpp`
- Modify: `src/app/capture.h`, `src/app/capture.cpp`, `src/app/render_thread.h`, `src/app/render_thread.cpp`

**Interfaces:**
- Consumes: `Dimmer` (Task 5), `Presenter::Draw` (Task 6), `DimRedrawSchedule` (Task 3).
- Produces:
  ```cpp
  // shared_dim.h — mutex; scritto dal thread UI, letto dal render (la mappa al contrario)
  struct DimInputs { DimParams params; bool suspended = false; float sdr_white_scrgb = 2.5f;
                     double highlight_until_ms = 0; bool watch = false; uint64_t version = 0; };
  struct DimMap { std::vector<float> levels; int cols = 0, rows = 0; double time_ms = 0; };
  class SharedDim {
   public:
    void Set(const DimInputs& in);   // incrementa version se qualcosa cambia (esclusi i livelli)
    DimInputs Get();
    void PublishMap(DimMap map);
    std::optional<DimMap> TakeMap(); // l'ultima, una volta sola
  };
  // RenderParams: aggiungere `SharedDim* dim = nullptr;` e `double frame_period_ms = 7;`
  // DesktopCapture::Acquire(UINT timeout_ms, ID3D11Texture2D* cache,
  //                         const std::function<void(ID3D11Texture2D* frame)>& before_copy = {});
  ```
  Render thread: a ogni ciclo `Get()`; se `version` cambia → `schedule.Poke`; `before_copy` chiama `dimmer.Detect` (white = `sdr_white_scrgb` in HDR, 1 in SDR); timeout di acquisizione = minimo fra prossimo offset e `schedule.NextRedrawAt`; si ridisegna se frame nuovo, offset cambiato o `now ≥ NextRedrawAt`; prima di `Draw` → `dimmer.Update`; evidenziazione attiva se `now < highlight_until_ms`. `dimmer.Init` dentro `reshape()` e dopo `ACCESS_LOST`; se fallisce → log di avviso e overlay senza oscuramento (`DimDrawInputs{}`).
- [ ] **Step 1:** implementare (codice Win32/D3D: nessun test unitario; i test GPU dei Task 5–6 coprono gli shader).
- [ ] **Step 2:** `.\build.ps1` → verde (compilazione + tutti i test).

### Task 8: Bianco SDR, wiring nell'app ed esclusioni

**Files:**
- Modify: `src/app/monitors.h`, `src/app/monitors.cpp`, `src/app/app.h`, `src/app/app.cpp`

**Interfaces:**
- Produces: `std::optional<float> QuerySdrWhiteLevel(const MonitorInfo& monitor);` (scRGB: `SDRWhiteLevel / 1000`, via `QueryDisplayConfig` + `DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL` sul target del path il cui `viewGdiDeviceName` = `monitor.device_name`).
- App: membro `SharedDim dim_`; `PushDimInputs()` chiamata da `ApplyConfig`, da `Tick` quando cambiano processo in primo piano / sospensione / bianco SDR, e dai comandi. `suspended` = processo in primo piano (minuscolo) in `config_.dim_exclude`. Bianco SDR riletto all'avvio dell'overlay e ogni 5 s in `Tick` (se fallisce: 2,5 in HDR + `LogWarn` una volta). `frame_period_ms` = `latency_ms_` gia' calcolata. `RenderParams.dim = &dim_`.
- [ ] **Step 1:** implementare.
- [ ] **Step 2:** `.\build.ps1` → verde.
- [ ] **Step 3: Prova a schermo** (avvisare l'utente prima; copia di `config.json`/`stats.json`; config temporanea con `dim_delay_s: 30`, `dim_ramp_s: 30`): avviare `oled-guard.exe --debug`, controllare nel log "Cattura avviata" e l'assenza di errori del dimmer; ripristinare i file.
- [ ] **Step 4: Commit G2** `feat(app): oscuramento delle aree statiche sulla GPU`.
- [ ] **Step 5: Checklist per l'utente** (con `dim_delay_s: 30`, `dim_ramp_s: 30` per non aspettare 8 minuti):
  1. Icone del desktop su sfondo scuro: dopo ~1 min si sono scurite senza che si veda il passaggio.
  2. Passa il mouse su un'icona della barra che si illumina: torna normale in mezzo secondo.
  3. Una pagina web chiara ferma e un editor scuro fermo: non si scuriscono.
  4. Nessun quadretto visibile ai bordi delle zone scurite.
  5. Metti un'app in `dim_exclude` e portala in primo piano: tutto torna normale in ~1 s.
  6. Disattiva HDR in Windows e ripeti il punto 1.

---

## Milestone G3 — evidenziazione e mappa

### Task 9: "Evidenzia sullo schermo" e lettura della mappa

**Files:**
- Modify: `src/app/dimmer.cpp` (readback), `src/app/render_thread.cpp`, `src/app/app.h`, `src/app/app.cpp`

**Interfaces:**
- Consumes: `Dimmer::RequestReadback/TryReadback`, `SharedDim::PublishMap/TakeMap`, `SummarizeDimLevels`, `DimMessage`, comandi `DimWatch`/`DimHighlight`.
- Render: con `watch` attivo, una `RequestReadback` al secondo dopo un `Update`; `TryReadback` a ogni ciclo successivo finche' riesce → `PublishMap`. Con `watch` spento nessuna staging creata.
- App: `DimWatch {on}` → `watch`; alla chiusura della finestra `watch = false`. `DimHighlight` → `highlight_until_ms = now + 15000` (rifiutato con `NoticeMessage("Oscuramento spento o sospeso: niente da evidenziare")` se spento/sospeso/overlay non a schermo). Messaggio `dim` 1 volta al secondo da `PushUiUpdates` mentre `watch` e' attivo, con `status`: `paused` se l'app e' in pausa, `off` se `!dim_enabled`, `suspended` (+ exe), `waiting` se l'overlay e' a schermo da meno di `dim_delay_s`, altrimenti `active`; `sdr_white_nits` = white × 80.
- [ ] **Step 1:** test GPU `GpuReadbackMatchesSrv` in `tests_gpu/test_dimmer_gpu.cpp`: dopo `RequestReadback`, `TryReadback` ritorna vero entro 10 tentativi (con `Flush` fra uno e l'altro) e i valori coincidono con quelli letti dal test.
- [ ] **Step 2–3:** implementare; `.\build.ps1` verde.
- [ ] **Step 4: Commit G3** `feat(app): evidenziazione sullo schermo e mappa dell'oscuramento`.
- Checklist G3 insieme a G4 (serve la scheda per provarla).

---

## Milestone G4 — scheda "Oscuramento"

### Task 10: Front-end

**Files:**
- Modify: `src/ui/index.html`, `src/ui/app.css`, `src/ui/app.js`

**Interfaces:**
- Consumes: messaggio `dim` (Task 4/9), `config` con i campi `dim_*`, comandi `dimWatch`, `dimHighlight`, `setConfig`, `listProcesses`.
- Struttura: quinta scheda `data-tab="oscuramento"` dopo "Giochi e app", layout dello schema §5.6 dello spec: a sinistra `<canvas id="dimMap">` 16:9 + righe Stato / Zone oscurate / Oscuramento / Bianco SDR + pulsante `#btnHighlight` (testo "Evidenzia sullo schermo · 15 s", durante il conto alla rovescia "Evidenziato · 12 s", disattivato con stato `off`/`suspended`/`paused`); a destra "ELEMENTI PICCOLI" (toggle `dim_enabled`, slider intensita' 10–80 %, inizia dopo 0,5–60 min passo 0,5, discesa 0,5–60 min passo 0,5) e "GRANDI AREE" (toggle `dim_large_enabled`, inizia dopo 5–240 min, intensita' 5–80 %) e "SOSPENDI CON QUESTE APP" (elenco `dim_exclude` con rimozione, input + "App aperte" + "Aggiungi", che fa `setConfig({dim_exclude: [...]})`).
  - Slider in minuti convertiti in secondi interi per la config; debounce 400 ms come le altre schede; riuso delle classi `.row`, `.toggle`, `.sec`, `.btn`, `range.single`.
  - `dimWatch {on:true}` quando la scheda diventa visibile e la pagina non e' nascosta; `{on:false}` al cambio scheda e su `visibilitychange` nascosto.
  - Canvas: sfondo #000, contorno #333, cella con `rgba(255,176,32, v/15)`; testo stato in maiuscolo (`ATTIVO`, `IN ATTESA`, `SOSPESO · APP.EXE`, `SPENTO`, `IN PAUSA`).
  - Elenco processi condiviso: `renderProcesses` deve sapere quale input riempire (parametro o stato "chi ha chiesto").
  - Anteprima (`Mock`): campi `dim_*` di default in `Mock.config`; messaggio `dim` finto 1/s con barra in fondo (ultime 5 righe a livelli crescenti) e 6 icone sparse; `dimHighlight` → conto alla rovescia finto.
- [ ] **Step 1:** implementare.
- [ ] **Step 2:** verifica in anteprima: aprire `src/ui/index.html` nel browser integrato (file locale), scheda Oscuramento: miniatura animata, slider aggiornano le etichette, nessun errore in console; screenshot.
- [ ] **Step 3:** `.\build.ps1` verde.
- [ ] **Step 4: Prova a schermo** (avviso + copie dei file): aprire l'interfaccia, scheda Oscuramento, controllare nel log che i messaggi `dim` partano solo a scheda aperta.
- [ ] **Step 5: Commit G4** `feat(ui): scheda Oscuramento`.
- [ ] **Step 6: Checklist per l'utente (G3+G4)**:
  1. Scheda Oscuramento: la miniatura mostra in ambra la barra e le icone scurite.
  2. "Evidenzia sullo schermo": per 15 s le zone scurite diventano ambra sul desktop vero, poi tornano normali.
  3. Sposta l'intensita' a 70%: la barra si scurisce lentamente fino al nuovo livello; riportala a 40%: risale in ~1 s.
  4. Spegni l'oscuramento: tutto normale in ~1 s; lo stato dice SPENTO.
  5. Aggiungi un'app alle esclusioni dalla scheda e portala in primo piano: stato SOSPESO · nome.
  6. Chiudi la finestra: nessun costo (stato invariato nel Task Manager rispetto a prima di aprirla).

---

## Revisione finale

- [ ] Revisore indipendente (subagent, modello piu' capace) su tutto il lavoro da `e43d424..HEAD` contro spec e piano; applicare le correzioni, `.\build.ps1` verde, commit `fix: correzioni dalla revisione finale dell'oscuramento`.
- [ ] Messaggio finale all'utente: checklist complessiva + elenco delle decisioni prese.
