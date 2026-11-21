# OLED Guard UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Finestra di OLED Guard in WebView2 (stile "Strumento di precisione") con stato dal vivo, statistiche, impostazioni, giochi e app, impostazioni di Windows per OLED.

**Architecture:** Logica pura nuova in `src/core` (statistiche, storico app, protocollo JSON fra app e pagina) testata come il resto. `UiWindow` nel thread UI ospita WebView2 su richiesta e scambia messaggi JSON con `src/ui` (HTML/CSS/JS senza librerie). `windows_settings` legge/applica/ripristina le 9 impostazioni.

**Tech Stack:** C++20, MSVC, CMake + Ninja, Windows SDK, WebView2 SDK 1.0.4314.35 (header + `WebView2LoaderStatic.lib`), HTML/CSS/JS vanilla.

**Spec:** `docs/superpowers/specs/2026-11-21-oled-guard-ui-design.md` (e il design base `docs/superpowers/specs/2026-11-21-oled-guard-design.md`).

## Global Constraints

- Unica dipendenza esterna: WebView2 SDK 1.0.4314.35 in `third_party/webview2/` (solo `include/`, `x64/WebView2LoaderStatic.lib`, `LICENSE.txt`).
- Front-end senza librerie, nessuna richiesta di rete; host virtuale `oledguard.local`.
- `src/core` senza header Windows; commenti/UI in italiano, identificatori in inglese; `/W4 /WX`.
- WebView2 vive solo a finestra aperta; thread UI mai bloccato in attesa di WebView2.
- Impostazioni di Windows cambiate solo su comando esplicito dalla pagina; originale salvato prima.
- Prove a runtime: copia e ripristino di `%APPDATA%\oled-guard\*` e della chiave Run; nessuna impostazione di Windows cambiata dall'esecutore (le prova l'utente).
- Commit con trailer `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

1. **`stats.json` mancante o rovinato** → si riparte da zero senza errori. Test in Task 1 (`wear_stats_from_garbage_is_empty`).
2. **Comando dalla pagina malformato o sconosciuto** → ignorato. Test in Task 3 (`ui_command_rejects_bad_input`).
3. **Patch di config non valida dalla pagina** (ampiezza 9) → rifiutata con avvisi, config invariata. Test in Task 3 (`config_patch_invalid_is_rejected`).
4. **Regola su un .exe con maiuscole o gia' in un'altra lista** → nessun duplicato, spostamento fra liste. Test in Task 2 (`app_rule_moves_between_lists`).
5. **Mezzanotte con l'app aperta** → "oggi" azzerato, totale conservato. Test in Task 1 (`wear_stats_day_rollover`).

---

## Milestone F1 — logica pura

### Task 1: WearStats

**Files:** Create `src/core/wear_stats.h`, `src/core/wear_stats.cpp`, `tests/test_wear_stats.cpp`; Modify `CMakeLists.txt`.

**Interfaces:**
- Consumes: `Offset`, `PauseReason`, `JsonValue`.
- Produces:
  - `class WearStats { public: static constexpr int kRadius = 3; void AddProtectedTime(Offset at, double ms); void CountPause(PauseReason reason); void RollDay(int day_key); double CellMs(Offset at) const; double TodayProtectedMs() const; double TotalProtectedMs() const; int TodayPauses() const; int TodayPauses(PauseReason reason) const; std::optional<double> Uniformity(int amplitude) const; JsonValue ToJson() const; static WearStats FromJson(const JsonValue& v); };`
  - `day_key` = AAAAMMGG (es. 20261121). Celle fuori da ±3 ignorate. `CellMs` e' cumulativo (tutti i giorni).
  - `Uniformity(a)`: sulle (2a+1)² celle dentro ±a: se la somma < 600000 ms → `nullopt`; altrimenti `clamp(100 × (1 − σ/μ), 0, 100)` (σ deviazione standard di popolazione).

- [ ] **Step 1: Test** in `tests/test_wear_stats.cpp`:

```cpp
TEST(wear_stats_accumulates_cells_and_totals) {
  oled::WearStats s; s.RollDay(20261121);
  s.AddProtectedTime({1,-1}, 1000); s.AddProtectedTime({1,-1}, 500); s.AddProtectedTime({4,0}, 999);
  CHECK_NEAR(s.CellMs({1,-1}), 1500, 1e-9); CHECK_NEAR(s.CellMs({4,0}), 0, 1e-9);
  CHECK_NEAR(s.TodayProtectedMs(), 2499, 1e-9); CHECK_NEAR(s.TotalProtectedMs(), 2499, 1e-9);
}
TEST(wear_stats_day_rollover) {
  oled::WearStats s; s.RollDay(20261121); s.AddProtectedTime({0,0}, 5000); s.CountPause(oled::PauseReason::FullscreenWindow);
  s.RollDay(20261121); CHECK_NEAR(s.TodayProtectedMs(), 5000, 1e-9);
  s.RollDay(20261122); CHECK_NEAR(s.TodayProtectedMs(), 0, 1e-9); CHECK_NEAR(s.TotalProtectedMs(), 5000, 1e-9);
  CHECK_EQ(s.TodayPauses(), 0); CHECK_NEAR(s.CellMs({0,0}), 5000, 1e-9);
}
TEST(wear_stats_counts_pauses_by_reason) {
  oled::WearStats s; s.RollDay(1);
  s.CountPause(oled::PauseReason::FullscreenWindow); s.CountPause(oled::PauseReason::FullscreenWindow); s.CountPause(oled::PauseReason::Manual);
  CHECK_EQ(s.TodayPauses(), 3); CHECK_EQ(s.TodayPauses(oled::PauseReason::FullscreenWindow), 2);
}
TEST(wear_stats_uniformity) {
  oled::WearStats s; s.RollDay(1);
  CHECK(!s.Uniformity(1).has_value());
  for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) s.AddProtectedTime({x,y}, 100000);
  CHECK_NEAR(*s.Uniformity(1), 100.0, 1e-9);
  s.AddProtectedTime({0,0}, 800000);  // una cella molto piu' usata
  CHECK(*s.Uniformity(1) < 50.0);
}
TEST(wear_stats_json_roundtrip) { /* stato con celle, totali, oggi e pause → ToJson → FromJson → stessi valori */ }
TEST(wear_stats_from_garbage_is_empty) {
  auto s = oled::WearStats::FromJson(oled::JsonValue("boh"));
  CHECK_NEAR(s.TotalProtectedMs(), 0, 1e-9);
  auto t = oled::WearStats::FromJson(*oled::ParseJson(R"({"cells":[1,"x"],"total_ms":-5})").value);
  CHECK_NEAR(t.TotalProtectedMs(), 0, 1e-9);
}
```
(Il roundtrip va scritto per esteso con i valori del commento.)

- [ ] **Step 2:** `.\build.ps1` → errore di link/compilazione.
- [ ] **Step 3:** Implementa `WearStats` (array 7×7 di double; `today_day_key`, `today_ms`, `total_ms`, conteggi pause oggi per motivo). `FromJson` ignora campi mancanti, di tipo sbagliato o negativi.
- [ ] **Step 4:** `.\build.ps1` → tutti `[PASS]`.
- [ ] **Step 5:** Commit `feat(core): statistiche di usura e uniformita'`.

### Task 2: AppHistory e regole delle app

**Files:** Create `src/core/app_history.h`, `src/core/app_history.cpp`, `tests/test_app_history.cpp`; Modify `CMakeLists.txt`.

**Interfaces:**
- Consumes: `Config`, `PauseReason`, `JsonValue`, `ToLowerAscii`.
- Produces:
  - `enum class AppRule { Auto, AlwaysPause, NeverPause };`
  - `struct AppEntry { std::string exe; PauseReason reason; int64_t last_seen_s; int count; };`
  - `class AppHistory { public: static constexpr size_t kMax = 50; void Record(std::string_view exe, PauseReason reason, int64_t now_s); const std::vector<AppEntry>& Entries() const; JsonValue ToJson() const; static AppHistory FromJson(const JsonValue& v); };` — `Entries()` dalla piu' recente; exe salvato in minuscolo; `Record` con exe vuoto non fa nulla.
  - `AppRule RuleFor(const Config& c, std::string_view exe);` — allowlist → `NeverPause`, denylist → `AlwaysPause`, altrimenti `Auto` (confronto case-insensitive).
  - `Config WithRule(Config c, std::string_view exe, AppRule rule);` — toglie l'exe da entrambe le liste e lo aggiunge (minuscolo) a quella della regola.

- [ ] **Step 1: Test:**

```cpp
TEST(app_history_records_most_recent_first) {
  oled::AppHistory h; h.Record("Game.exe", oled::PauseReason::FullscreenWindow, 100); h.Record("vlc.exe", oled::PauseReason::FullscreenWindow, 200);
  h.Record("GAME.EXE", oled::PauseReason::NotificationState, 300);
  CHECK_EQ(h.Entries().size(), size_t{2}); CHECK_EQ(h.Entries()[0].exe, std::string("game.exe"));
  CHECK_EQ(h.Entries()[0].count, 2); CHECK(h.Entries()[0].reason == oled::PauseReason::NotificationState); CHECK_EQ(h.Entries()[0].last_seen_s, int64_t{300});
}
TEST(app_history_is_capped) { /* 60 exe diversi → 50 voci, la piu' vecchia (exe0) assente */ }
TEST(app_history_ignores_empty_exe) { oled::AppHistory h; h.Record("", oled::PauseReason::Manual, 1); CHECK(h.Entries().empty()); }
TEST(app_rule_moves_between_lists) {
  oled::Config c; c.denylist = {"game.exe"};
  CHECK(oled::RuleFor(c, "GAME.exe") == oled::AppRule::AlwaysPause);
  c = oled::WithRule(c, "Game.EXE", oled::AppRule::NeverPause);
  CHECK(c.denylist.empty()); CHECK_EQ(c.allowlist.size(), size_t{1}); CHECK_EQ(c.allowlist[0], std::string("game.exe"));
  c = oled::WithRule(c, "game.exe", oled::AppRule::NeverPause); CHECK_EQ(c.allowlist.size(), size_t{1});
  c = oled::WithRule(c, "game.exe", oled::AppRule::Auto); CHECK(c.allowlist.empty() && c.denylist.empty());
}
TEST(app_history_json_roundtrip) { /* 2 voci → ToJson → FromJson → stessi campi; FromJson(garbage) → vuoto */ }
```

- [ ] **Step 2–4:** build rosso → implementa → build verde.
- [ ] **Step 5:** Commit `feat(core): storico delle app e regole di pausa per app`.

### Task 3: Protocollo UI e hotkey "spegni schermo"

**Files:** Create `src/core/ui_protocol.h`, `src/core/ui_protocol.cpp`, `tests/test_ui_protocol.cpp`; Modify `src/core/config.h`, `src/core/config.cpp`, `tests/test_config.cpp`, `CMakeLists.txt`.

**Interfaces:**
- Consumes: `Config`, `ConfigFromJson`, `ConfigToJson`, `WearStats`, `AppHistory`, `AppRule`, `RuleFor`, `Offset`, `PauseReason`, `PauseReasonText`, `ParseJson`, `SerializeJson`.
- Produces:
  - `Config::hotkey_display_off = "Ctrl+Alt+Shift+O"`; le tre hotkey devono essere diverse fra loro, altrimenti tutte e tre ai default + avviso (stessa regola di `FixHotkeys` estesa).
  - `struct UiState { bool paused=false; bool manual_pause=false; bool testing_shift=false; PauseReason reason=PauseReason::None; std::string foreground_process; Offset offset; std::vector<Offset> trail; double next_move_ms=0; int amplitude=2; std::string monitor_name; int width=0, height=0; bool hdr=false; double latency_ms=0; };`
  - `struct WindowsItem { std::string id, label, current, recommended; bool ok=false; bool restorable=false; bool managed=false; };`
  - Messaggi (stringhe JSON compatte, campo `type`): `std::string StateMessage(const UiState&)` (`"state"`, `reason` = testo italiano, `trail` = array di `[x,y]`); `std::string StatsMessage(const WearStats&, int amplitude)` (`"stats"`: `cells` 7×7 riga per riga in ms, `uniformity` numero o `null`, `today_ms`, `total_ms`, `pauses_today`); `std::string ConfigMessage(const Config&)` (`"config"`, oggetto = `ConfigToJson`); `std::string AppsMessage(const AppHistory&, const Config&)` (`"apps"`: `exe`, `reason`, `last_seen_s`, `count`, `rule` ∈ `auto|always|never`, piu' le voci di deny/allowlist non presenti nello storico); `std::string WindowsMessage(const std::vector<WindowsItem>&)` (`"windows"`: `items`, `score`, `total`); `std::string ProcessesMessage(const std::vector<std::string>&)`.
  - `enum class UiCommandType { Ready, SetConfig, SetManualPause, TestShift, SetAppRule, AddApp, RemoveApp, ListProcesses, WindowsApply, WindowsApplyAll, WindowsRestoreAll, DisplayOff, HotkeyCapture, Window };`
  - `struct UiCommand { UiCommandType type; JsonValue patch; bool on=false; std::string exe; AppRule rule=AppRule::Auto; std::string id; std::string action; };`
  - `std::optional<UiCommand> ParseUiCommand(std::string_view json);` — campo `cmd` con i nomi di spec §5.3 (`ready`, `setConfig`, `setManualPause`, `testShift`, `setAppRule`, `addApp`, `removeApp`, `listProcesses`, `windowsApply`, `windowsApplyAll`, `windowsRestoreAll`, `displayOff`, `hotkeyCapture`, `window`); campi richiesti e tipati (`patch` oggetto; `on` bool; `exe` stringa non vuota, max 260; `rule` ∈ auto|always|never; `id` stringa; `action` ∈ minimize|maximize|close); altrimenti `nullopt`.
  - `ConfigLoadResult ApplyConfigPatch(const Config& current, const JsonValue& patch);` — fonde le chiavi della patch sull'oggetto di `ConfigToJson(current)` e valida con `ConfigFromJson`; chiavi sconosciute → avviso.

- [ ] **Step 1: Test** (config): `config_display_off_hotkey_default` (default `Ctrl+Alt+Shift+O`), `config_three_hotkeys_must_differ` (`hotkey_display_off` = `Ctrl+Alt+Shift+P` → tutte ai default, ≥1 avviso), e il roundtrip esistente esteso al nuovo campo.
- [ ] **Step 2: Test** (protocollo):

```cpp
TEST(ui_state_message_fields) {
  oled::UiState s; s.paused = true; s.reason = oled::PauseReason::FullscreenWindow; s.offset = {1,-1}; s.trail = {{0,0},{1,-1}}; s.hdr = true; s.width = 3840;
  auto v = *oled::ParseJson(oled::StateMessage(s)).value;
  CHECK_EQ(v.Find("type")->AsString(), std::string("state")); CHECK(v.Find("paused")->AsBool());
  CHECK_EQ(v.Find("reason")->AsString(), std::string("Finestra a tutto schermo"));
  CHECK_EQ(v.Find("trail")->AsArray().size(), size_t{2}); CHECK(v.Find("hdr")->AsBool());
}
TEST(ui_stats_message_has_49_cells_and_null_uniformity) { /* WearStats vuoto → cells 49 numeri, uniformity null */ }
TEST(ui_apps_message_includes_rules) { /* storico con game.exe + config.denylist {game.exe}, allowlist {mpv.exe} → 2 voci, game rule "always", mpv rule "never" */ }
TEST(ui_windows_message_score) { /* 3 item, 2 ok → score 2, total 3 */ }
TEST(ui_command_parses_known_commands) {
  CHECK(oled::ParseUiCommand(R"({"cmd":"ready"})")->type == oled::UiCommandType::Ready);
  auto r = oled::ParseUiCommand(R"({"cmd":"setAppRule","exe":"Game.exe","rule":"never"})");
  CHECK(r && r->rule == oled::AppRule::NeverPause && r->exe == "Game.exe");
  CHECK(oled::ParseUiCommand(R"({"cmd":"window","action":"close"})")->action == "close");
  CHECK(oled::ParseUiCommand(R"({"cmd":"setConfig","patch":{"amplitude_px":3}})").has_value());
}
TEST(ui_command_rejects_bad_input) {
  for (const char* bad : {"", "[]", "{}", R"({"cmd":"boh"})", R"({"cmd":"setManualPause"})", R"({"cmd":"setManualPause","on":"si"})",
                          R"({"cmd":"setAppRule","exe":"","rule":"never"})", R"({"cmd":"setAppRule","exe":"a.exe","rule":"spesso"})",
                          R"({"cmd":"window","action":"explode"})", R"({"cmd":"setConfig","patch":5})"})
    CHECK(!oled::ParseUiCommand(bad).has_value());
}
TEST(config_patch_applies_valid_values) { oled::Config c; auto r = oled::ApplyConfigPatch(c, *oled::ParseJson(R"({"amplitude_px":3})").value); CHECK(!r.parse_failed && r.warnings.empty()); CHECK_EQ(r.config.amplitude_px, 3); CHECK_EQ(r.config.resume_delay_ms, 2500); }
TEST(config_patch_invalid_is_rejected) { oled::Config c; auto r = oled::ApplyConfigPatch(c, *oled::ParseJson(R"({"amplitude_px":9})").value); CHECK(!r.warnings.empty()); CHECK_EQ(r.config.amplitude_px, 2); }
```
(I test con commento vanno scritti per esteso.)

- [ ] **Step 3–5:** build rosso → implementa → build verde.
- [ ] **Step 6:** Commit `feat(core): protocollo JSON dell'interfaccia e hotkey spegni schermo` — **fine F1**.

---

## Milestone F2 — WebView2 e finestra

### Task 4: SDK WebView2, UiWindow, apertura dalla tray

**Files:** Create `third_party/webview2/{include/*.h, x64/WebView2LoaderStatic.lib, LICENSE.txt}`, `src/app/ui_window.h`, `src/app/ui_window.cpp`, `src/ui/index.html` (provvisoria: barra del titolo + testo "OLED Guard" + script che invia `ready` e mostra l'ultimo messaggio); Modify `CMakeLists.txt` (include, link `WebView2LoaderStatic.lib` + eventuali `version.lib`; copia `src/ui` in `$<TARGET_FILE_DIR>/ui` post-build), `src/app/app.h`, `src/app/app.cpp`, `.gitignore` (niente).

**Interfaces:**
- Produces:
  - `class UiWindow { public: using MessageHandler = std::function<void(const std::string& utf8_json)>; bool Open(HINSTANCE instance, MessageHandler on_message, bool devtools); void Close(); bool IsOpen() const; bool IsReady() const; void Post(const std::string& utf8_json); HWND Hwnd() const; };`
  - `IsReady()` diventa true quando arriva il comando `ready`. `Post` senza effetto se non pronta.
  - Finestra: `WS_OVERLAPPEDWINDOW`; `WM_NCCALCSIZE` toglie solo la barra del titolo (bordi di ridimensionamento invisibili di Windows 11 restano), `WM_NCHITTEST` → `HTTOP` nei primi 6 px; `DWMWA_USE_IMMERSIVE_DARK_MODE` = 1, `DWMWA_WINDOW_CORNER_PREFERENCE` = `DWMWCP_DONOTROUND`, `DWMWA_BORDER_COLOR` = `RGB(0x1d,0x1d,0x1d)`; sfondo della classe nero; `WM_GETMINMAXINFO` min 900×600 DIP; dimensione iniziale 1100×720 DIP centrata sul monitor della TV.
  - WebView2: ambiente con cartella `%LOCALAPPDATA%\oled-guard\webview2`; controller → `put_DefaultBackgroundColor({255,0,0,0})` prima di `Navigate`; settings: `AreDefaultContextMenusEnabled` false, `IsZoomControlEnabled` false, `IsStatusBarEnabled` false, `AreBrowserAcceleratorKeysEnabled` false, `AreDevToolsEnabled` = devtools, `IsNonClientRegionSupportEnabled` true (ICoreWebView2Settings9); `SetVirtualHostNameToFolderMapping(L"oledguard.local", <exe>\ui, DENY_CORS)`; `NavigationStarting` annulla URI che non iniziano con `https://oledguard.local/`; `NewWindowRequested` → `Handled` + niente; naviga a `https://oledguard.local/index.html`.
  - `App`: `CoInitializeEx(COINIT_APARTMENTTHREADED)` in `Run`; voce tray `Apri OLED Guard` (prima, `MF_DEFAULT`), `WM_LBUTTONDBLCLK` sulla tray apre; opzione `--open-ui` apre all'avvio; `OnUiMessage(const std::string&)` per ora logga a livello debug e risponde a `ready` con `ConfigMessage`; chiusura della finestra → `UiWindow::Close()`.

- [ ] **Step 1: Download e verifica** del pacchetto (gia' autorizzato dall'utente): scarica `https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/1.0.4314.35/microsoft.web.webview2.1.0.4314.35.nupkg` in una cartella nuova e vuota dello scratchpad, verifica la firma del pacchetto con `Get-AuthenticodeSignature` sui file `.dll`/`.lib` se firmati e la dimensione (~9,4 MB), estrai (zip), copia in `third_party/webview2/` solo `build/native/include/*.h`, `build/native/x64/WebView2LoaderStatic.lib`, `LICENSE.txt`.
- [ ] **Step 2:** Implementa `UiWindow`, le modifiche a CMake e App.
- [ ] **Step 3:** `.\build.ps1` → compila senza warning, test verdi; `build\ui\index.html` esiste.
- [ ] **Step 4: Prova a runtime** (fuori sandbox, avvisando l'utente): `oled-guard.exe --open-ui --debug` per 8 s → nel log `Interfaccia pronta` (comando `ready` ricevuto); processo `msedgewebview2.exe` figlio presente; poi `WM_CLOSE` alla finestra UI → entro 3 s nessun `msedgewebview2.exe` figlio; l'app resta viva; poi chiusura pulita.
- [ ] **Step 5:** Commit `feat(app): finestra WebView2 con barra del titolo propria` — **fine F2**. Checklist utente: apertura dalla tray senza lampo bianco, trascinamento dalla barra, ridimensionamento, chiusura e riapertura.

---

## Milestone F3 — interfaccia

### Task 5: Front-end completo con modalita' anteprima e collegamento allo stato

**Files:** Create `src/ui/app.css`, `src/ui/app.js`; Modify `src/ui/index.html`, `src/app/app.h`, `src/app/app.cpp`.

**Interfaces:**
- Consumes: tutti i messaggi di Task 3; `UiWindow`.
- Produces (pagina): quattro schede come in `.superpowers/brainstorm/325-1795245298/content/stile-visivo.html` (B) e `schede-stile-b.html`; barra del titolo con `app-region: drag` e pulsanti che inviano `window`; attenuazione dopo 60 s (`body.dim` → `filter: brightness(.3)`, transizione 1 s); radar dimensionato sull'ampiezza (3×3, 5×5, 7×7) con mappa di calore dai `cells`, scia dalle ultime posizioni, conto alla rovescia da `next_move_ms`; modalita' anteprima se `window.chrome?.webview` manca (dati finti e animati, comandi solo in console).
- Produces (app):
  - `App` invia `state` ogni 250 ms a finestra pronta (timer gia' esistente del polling pausa), `stats` ogni 5 s, `config`/`apps`/`windows` all'apertura e a ogni cambiamento.
  - Trail: ultime 4 posizioni distinte disegnate (aggiornate quando cambia l'offset letto in `Tick`).
  - Latenza: `DwmGetCompositionTimingInfo` → `1000 / (rateRefresh.uiNumerator / uiDenominator)`.
  - `WearStats`: in `Tick`, se `overlay_shown_`, `AddProtectedTime(offset corrente, now − last_tick)`; `RollDay` con la data locale; all'ingresso in pausa automatica `CountPause(reason)`.
  - Comandi gestiti qui: `ready`, `setConfig` (con `ApplyConfigPatch` → `ApplyConfig` + `SaveConfig`; avvisi → notifica e `config` reinviato), `setManualPause`, `window`.

- [ ] **Step 1:** Scrivi la pagina; aprila nel pannello browser da `src/ui/index.html` (modalita' anteprima) e confronta con i mockup approvati con uno screenshot per scheda.
- [ ] **Step 2:** Collega l'App; `.\build.ps1` verde.
- [ ] **Step 3: Prova a runtime**: `--open-ui --debug` 10 s → nel log nessun errore, almeno 30 messaggi `state` inviati (contatore a livello debug), un comando `setConfig` simulato da script non serve: verifica a mano dall'utente.
- [ ] **Step 4:** Commit `feat(ui): interfaccia completa in stile strumento` — **fine F3**. Checklist utente: radar che segue lo spostamento, conto alla rovescia, cambio ampiezza dalla pagina applicato e salvato, attenuazione dopo 60 s.

---

## Milestone F4 — Windows per OLED

### Task 6: windows_settings, spegni schermo

**Files:** Create `src/app/windows_settings.h`, `src/app/windows_settings.cpp`; Modify `src/app/app.h`, `src/app/app.cpp`, `src/ui/app.js`.

**Interfaces:**
- Consumes: `WindowsItem`, `WindowsMessage`, `JsonValue`, `AppDataDir()`.
- Produces:
  - `class WindowsSettings { public: explicit WindowsSettings(std::filesystem::path backup_file); std::vector<WindowsItem> ReadAll() const; bool Apply(const std::string& id); void ApplyAll(); void RestoreAll(); };`
  - Id fissi: `dark_theme`, `taskbar_autohide`, `display_timeout`, `screensaver`, `desktop_icons`, `transparency`, `black_wallpaper`, `mouse_vanish`, `accent_titlebar`; meccanismi e valori consigliati come spec §5.5 (spegni schermo 5 min AC, salvaschermo 10 min). Etichette italiane come nel mockup.
  - Backup: al primo `Apply` di un id, valore originale salvato in `windows-backup.json`; `RestoreAll` ripristina gli id salvati e li toglie dal file; `restorable` = id presente nel backup; `managed` = lettura o scrittura rifiutata dal sistema.
  - `void TurnDisplayOff(HWND any_own_window);` → `SendMessage(hwnd, WM_SYSCOMMAND, SC_MONITORPOWER, 2)` passato a `DefWindowProc`.
  - `App`: comandi `windowsApply`, `windowsApplyAll`, `windowsRestoreAll`, `displayOff`; hotkey `hotkey_display_off` registrata come la toggle (id 2), sospesa con `hotkeyCapture`.

- [ ] **Step 1:** Implementa. **Solo letture** nelle prove dell'esecutore.
- [ ] **Step 2:** `.\build.ps1` verde. Prova a runtime: `--open-ui --debug` → nel log l'elenco delle 9 voci con stato attuale letto (es. `dark_theme: scuro`), nessuna modifica; confronto a mano con le impostazioni reali.
- [ ] **Step 3:** Commit `feat(app): impostazioni di Windows per OLED e spegni schermo` — **fine F4**. Checklist utente: ogni "Applica" funziona e il punteggio sale; "Ripristina come prima" rimette tutto; "Spegni schermo" e `Ctrl+Alt+Shift+O`.

---

## Milestone F5 — giochi, hotkey, prova, persistenza

### Task 7: Giochi e app, registrazione hotkey, "Prova ora", stats.json

**Files:** Create `src/app/stats_file.h`, `src/app/stats_file.cpp`; Modify `src/app/app.h`, `src/app/app.cpp`, `src/app/pause_monitor.*` (elenco app aperte), `src/ui/app.js`.

**Interfaces:**
- Consumes: `AppHistory`, `RuleFor`, `WithRule`, `WearStats`, `ProcessesMessage`, `AppsMessage`.
- Produces:
  - `struct StatsData { WearStats wear; AppHistory apps; std::optional<RECT> ui_rect; }; StatsData LoadStats(const std::filesystem::path&); bool SaveStats(const std::filesystem::path&, const StatsData&);` — `stats.json` in `AppDataDir()`, scrittura atomica (temp + `MoveFileExW`), file rovinato → `StatsData{}` + avviso nel log; salvataggio ogni 60 s e all'uscita.
  - `std::vector<std::string> ListWindowedProcesses();` — .exe (minuscoli, unici, ordinati) dei processi con almeno una finestra top-level visibile e con titolo, escluso questo processo.
  - App: all'ingresso in una pausa automatica (o manuale) `apps.Record(processo in primo piano, motivo, ora Unix)`; comandi `setAppRule` (`WithRule` → `ApplyConfig` + `SaveConfig`), `addApp` (= `setAppRule always`), `removeApp` (= `Auto` + tolto dallo storico), `listProcesses`, `testShift` (`Reconfigure` con tempi 3–5 s / 1–2 s per 30 s, poi tempi della config; `testing_shift` nello stato), `hotkeyCapture {on}` (deregistra/riregistra toggle e display-off; l'emergenza resta sempre attiva).
  - Pagina: registrazione hotkey da `keydown` → testo tipo `Ctrl+Alt+Shift+P` → `setConfig`; tabella app con le tre regole; "App aperte" → `listProcesses`.
  - Posizione/dimensione della finestra salvate in `ui_rect` alla chiusura e riusate all'apertura (se ancora su un monitor collegato).

- [ ] **Step 1:** Implementa.
- [ ] **Step 2:** `.\build.ps1` verde. Prova a runtime (con copia/ripristino di `%APPDATA%\oled-guard`): avvio 70 s con overlay attivo → `stats.json` creato con `total_ms` > 60000; `stats.json` rovinato → avvio senza errori e file riscritto valido.
- [ ] **Step 3:** Commit `feat(app): giochi e app, registrazione hotkey, prova spostamento, statistiche persistenti` — **fine F5**. Checklist utente: storico dopo un gioco, regole che funzionano, "App aperte", registrazione hotkey, "Prova ora", riapertura nella stessa posizione.
