# OLED Guard Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** App Windows (C++20) che ridisegna il desktop della TV OLED in un overlay click-through spostato di 1–3 px, lentamente, con pausa automatica in giochi/fullscreen.

**Architecture:** libreria statica `oled_core` (logica pura, niente `<windows.h>`, testata da `oled-guard-tests.exe`) + eseguibile `oled-guard.exe` (Win32/D3D11/DXGI/DirectComposition). Thread UI (tray, pausa, finestra), thread di render (duplicazione + swap chain, esiste solo quando attivo), thread di emergenza (hotkey di uscita + watchdog).

**Tech Stack:** C++20, MSVC (VS Build Tools 2026), CMake 4.3 + Ninja (inclusi nei Build Tools), Windows SDK 10.0.26100, fxc per gli shader.

**Spec:** `docs/superpowers/specs/2026-11-21-oled-guard-design.md`

## Global Constraints

- C++20, CMake, MSVC; solo Windows SDK. Nessuna libreria esterna.
- Nessun readback su CPU: i pixel restano sulla GPU.
- `src/core/` non include mai `<windows.h>` né header Windows.
- Commenti e testi UI in italiano, identificatori in inglese. Sorgenti UTF-8 (`/utf-8`).
- Warning: `/W4 /permissive-`; `UNICODE`, `_UNICODE`, `NOMINMAX`, `WIN32_LEAN_AND_MEAN`.
- Commit locali (nessun remote) con trailer `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Config in `%APPDATA%\oled-guard\config.json`, log in `%APPDATA%\oled-guard\logs\`.
- Default: `amplitude_px` 2 (1–3), dwell 20–60 s, transition 2–5 s, `pause_enter_delay_ms` 300, `resume_delay_ms` 2500, `hotkey_toggle` `Ctrl+Alt+Shift+P`, `hotkey_emergency_exit` `Ctrl+Alt+Shift+F12`, `allow_tearing` false.

## Review Focus

1. **Config salvato da Blocco note con BOM UTF-8** → deve caricarsi normalmente. Test in Task 5 (`json_skips_utf8_bom`).
2. **Ripresa dopo una pausa lunga** (partita di 2 ore) → l'offset riparte da dove era, nessun salto. Test in Task 3 (`scheduler_resume_keeps_offset`).
3. **Nome processo con maiuscole diverse** (`Game.EXE` vs denylist `game.exe`) → deve combaciare. Test in Task 4 (`deny_is_case_insensitive`).
4. **Tempo non monotono fra thread** (query con `now` minore dell'ultima) → nessun ritorno indietro né crash. Test in Task 3 (`scheduler_tolerates_time_going_backwards`).
5. **Le due hotkey uguali in config** → avviso e default per entrambe, nessuna hotkey che ne nasconde un'altra. Test in Task 6 (`config_rejects_equal_hotkeys`).

---

## Milestone A — logica pura + test

### Task 1: Scaffolding, build.ps1, mini-framework di test, PRNG

**Files:**
- Create: `CMakeLists.txt`, `build.ps1`, `.gitignore`, `src/core/rng.h`, `src/core/rng.cpp`, `tests/test_framework.h`, `tests/test_main.cpp`, `tests/test_rng.cpp`

**Interfaces:**
- Produces:
  - `namespace oled { class Rng { public: explicit Rng(uint64_t seed); uint64_t Next(); int UniformInt(int lo, int hi); /* inclusivo */ double UniformReal(double lo, double hi); /* [lo,hi) */ }; }`
  - Test framework: `TEST(name)` registra una funzione; `CHECK(expr)`, `CHECK_EQ(a, b)`, `CHECK_NEAR(a, b, eps)` registrano il fallimento con file/riga e continuano; `test_main.cpp` esegue tutti i test, stampa `[PASS]/[FAIL] name`, ritorna 1 se qualcuno fallisce. Filtro opzionale: primo argomento = sottostringa del nome.
  - Target CMake: `oled_core` (static, `src/core/*.cpp` via lista esplicita), `oled-guard-tests` (registrato con `add_test`).

- [ ] **Step 1: Scrivi `CMakeLists.txt`** — `cmake_minimum_required(VERSION 3.25)`, `project(oled_guard CXX)`, `CMAKE_CXX_STANDARD 20` required, opzioni MSVC dei Global Constraints, `enable_testing()`, target `oled_core` e `oled-guard-tests`. `.gitignore`: `build/`.

- [ ] **Step 2: Scrivi `build.ps1`** — `param([ValidateSet('Release','Debug')][string]$Config='Release', [switch]$Clean)`; `$ErrorActionPreference='Stop'`; trova VS con `vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`; importa `Common7\Tools\Microsoft.VisualStudio.DevShell.dll` ed esegue `Enter-VsDevShell -VsInstallPath $path -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'`; `-Clean` cancella `build/`; poi `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=$Config`, `cmake --build build`, `ctest --test-dir build --output-on-failure`; dopo ogni comando, se `$LASTEXITCODE -ne 0` → `exit $LASTEXITCODE`. Messaggi in italiano.

- [ ] **Step 3: Scrivi i test che falliscono** in `tests/test_rng.cpp`:

```cpp
TEST(rng_same_seed_same_sequence) {
  oled::Rng a(42), b(42);
  for (int i = 0; i < 100; ++i) CHECK_EQ(a.Next(), b.Next());
}
TEST(rng_uniform_int_in_range_and_covers_all) {
  oled::Rng r(7); int seen[7] = {};
  for (int i = 0; i < 7000; ++i) { int v = r.UniformInt(-3, 3); CHECK(v >= -3 && v <= 3); seen[v + 3]++; }
  for (int c : seen) CHECK(c > 800 && c < 1200);
}
TEST(rng_uniform_real_in_range) {
  oled::Rng r(1);
  for (int i = 0; i < 1000; ++i) { double v = r.UniformReal(2.0, 5.0); CHECK(v >= 2.0 && v < 5.0); }
}
```

- [ ] **Step 4: Esegui** `.\build.ps1` — atteso: errore di compilazione/link (Rng non definito).

- [ ] **Step 5: Implementa `Rng`** — splitmix64 per inizializzare lo stato di xoshiro256**; `UniformInt` con rifiuto (niente bias del modulo); `UniformReal` con i 53 bit alti. Niente `std::uniform_*_distribution` (non portabili fra implementazioni).

- [ ] **Step 6: Esegui** `.\build.ps1` — atteso: 3 `[PASS]`, ctest `100% tests passed`, exit 0.

- [ ] **Step 7: Commit** — `git add -A; git commit -m "build: scaffolding CMake, build.ps1, framework di test, PRNG"`

### Task 2: OffsetGenerator

**Files:**
- Create: `src/core/offset.h`, `src/core/offset_generator.cpp`, `tests/test_offset_generator.cpp`

**Interfaces:**
- Consumes: `oled::Rng`
- Produces:
  - `struct Offset { int x = 0; int y = 0; bool operator==(const Offset&) const = default; };`
  - `class OffsetGenerator { public: OffsetGenerator(uint64_t seed, int amplitude); Offset Current() const; Offset NextTarget(); void SetAmplitude(int amplitude); int Amplitude() const; };`
  - `NextTarget()` sceglie uniformemente fra i punti con |x|,|y| ≤ A diversi da `Current()`, aggiorna `Current()` e lo ritorna. Parte da (0,0). Se `Current()` è fuori dai limiti dopo `SetAmplitude`, il target è comunque dentro i limiti.

- [ ] **Step 1: Scrivi i test**:

```cpp
TEST(generator_starts_at_origin) { oled::OffsetGenerator g(1, 2); CHECK(g.Current() == oled::Offset{0, 0}); }
TEST(generator_never_repeats_consecutively) {
  oled::OffsetGenerator g(3, 1); oled::Offset prev = g.Current();
  for (int i = 0; i < 10000; ++i) { auto t = g.NextTarget(); CHECK(!(t == prev)); prev = t; }
}
TEST(generator_stays_within_amplitude) {
  for (int a = 1; a <= 3; ++a) { oled::OffsetGenerator g(5, a);
    for (int i = 0; i < 5000; ++i) { auto t = g.NextTarget(); CHECK(std::abs(t.x) <= a && std::abs(t.y) <= a); } }
}
TEST(generator_is_deterministic_with_seed) {
  oled::OffsetGenerator a(99, 2), b(99, 2);
  for (int i = 0; i < 200; ++i) CHECK(a.NextTarget() == b.NextTarget());
}
TEST(generator_covers_grid_uniformly) {  // 25 punti, 25000 estrazioni → ~1000 ciascuno
  oled::OffsetGenerator g(11, 2); int counts[5][5] = {};
  for (int i = 0; i < 25000; ++i) { auto t = g.NextTarget(); counts[t.y + 2][t.x + 2]++; }
  for (auto& row : counts) for (int c : row) CHECK(c > 850 && c < 1150);
}
TEST(generator_shrinking_amplitude_brings_target_inside) {
  oled::OffsetGenerator g(2, 3);
  while (std::abs(g.Current().x) < 3) g.NextTarget();
  g.SetAmplitude(1); auto t = g.NextTarget();
  CHECK(std::abs(t.x) <= 1 && std::abs(t.y) <= 1);
}
```

- [ ] **Step 2: Esegui** `.\build.ps1` — atteso: errore di compilazione.
- [ ] **Step 3: Implementa `OffsetGenerator`** — estrai un indice in `[0, (2A+1)² - 1)` (cioè una cella in meno se la corrente è nella griglia) e salta la cella corrente; `SetAmplitude` clampa a [1, 3].
- [ ] **Step 4: Esegui** `.\build.ps1` — atteso: tutti `[PASS]`.
- [ ] **Step 5: Commit** — `feat(core): generatore di offset con random walk limitato`

### Task 3: OffsetAnimator, ShiftScheduler, SharedShift

**Files:**
- Create: `src/core/offset_animator.cpp`, `src/core/shift_scheduler.h`, `src/core/shift_scheduler.cpp`, `tests/test_offset_animator.cpp`, `tests/test_shift_scheduler.cpp`
- Modify: `src/core/offset.h` (dichiarazione `OffsetAnimator`)

**Interfaces:**
- Consumes: `Offset`, `OffsetGenerator`, `Rng`
- Produces:
  - `class OffsetAnimator { public: OffsetAnimator(Offset from, Offset to, double duration_ms); Offset At(double t_ms) const; std::optional<double> NextStepAt(double t_ms) const; int StepCount() const; };` (`t_ms` relativo all'inizio della transizione)
  - `struct ShiftTiming { double dwell_min_ms, dwell_max_ms, transition_min_ms, transition_max_ms; };`
  - `class ShiftScheduler { public: ShiftScheduler(uint64_t seed, int amplitude, ShiftTiming timing, double now_ms); Offset OffsetAt(double now_ms); double NextChangeAt(double now_ms); void Reconfigure(int amplitude, ShiftTiming timing, double now_ms); void Resume(double now_ms); };`
  - `class SharedShift` — stessa API di `ShiftScheduler`, ogni metodo protetto da `std::mutex`. Usata da thread UI e render.

Algoritmo dell'animator (non determinato dai test, va seguito):
```
n = max(|dx|, |dy|);  punto k = from + (lround(k*dx/n), lround(k*dy/n))   // std::lround
s(u) = 3u² − 2u³,  u = clamp(t/D, 0, 1)
indice(t) = min(n, floor(s(u)*n + 0.5))
NextStepAt(t): i = indice(t); se i == n → nullopt;
               u* = inversa di s per (i + 0.5)/n (bisezione, 50 iterazioni);
               ritorna ceil(u* · D) in ms (se ≤ t, t + 1)
```

Scheduler: parte in sosta a (0,0) con `dwell_end = now + UniformReal(dwell)`. `OffsetAt(now)`: se `now < last_now` usa `last_now`. Fine sosta → `NextTarget()`, transizione di durata `UniformReal(transition)` che inizia a `dwell_end`; fine transizione → sosta che inizia alla fine della transizione. Regola di recupero: se una fase è finita da più di 5000 ms, la fase successiva inizia a `now` (niente raffiche di transizioni). `NextChangeAt`: in sosta `dwell_end`; in transizione `start + NextStepAt(now - start)` o la fine. `Resume(now)`: sosta alla posizione dell'ultimo `OffsetAt`, con nuova durata da `now`. `Reconfigure`: aggiorna ampiezza e tempi; se la posizione corrente è fuori dai nuovi limiti, `dwell_end = now`.

- [ ] **Step 1: Scrivi i test dell'animator**:

```cpp
using oled::Offset; using oled::OffsetAnimator;
TEST(animator_endpoints) { OffsetAnimator a({0,0},{2,-1},3000);
  CHECK(a.At(0) == Offset{0,0}); CHECK(a.At(3000) == Offset{2,-1}); CHECK(a.At(99999) == Offset{2,-1}); CHECK(a.StepCount() == 2); }
TEST(animator_midpoint) { OffsetAnimator a({0,0},{2,-1},3000); CHECK(a.At(1500) == Offset{1,-1}); }
TEST(animator_steps_are_single_pixel_and_monotonic) {
  OffsetAnimator a({-3,2},{3,-3},5000); Offset prev = a.At(0); int changes = 0;
  for (int t = 1; t <= 5000; ++t) { Offset o = a.At(t);
    CHECK(std::abs(o.x - prev.x) <= 1 && std::abs(o.y - prev.y) <= 1);
    CHECK(std::abs(3 - o.x) <= std::abs(3 - prev.x)); CHECK(std::abs(-3 - o.y) <= std::abs(-3 - prev.y));
    if (!(o == prev)) ++changes; prev = o; }
  CHECK_EQ(changes, 6);
}
TEST(animator_next_step_at_is_exact) {
  OffsetAnimator a({0,0},{3,0},3000); double t = 0; int steps = 0;
  while (auto next = a.NextStepAt(t)) { CHECK(*next > t); CHECK(a.At(*next - 1) == a.At(t)); CHECK(!(a.At(*next) == a.At(t))); t = *next; ++steps; }
  CHECK_EQ(steps, 3); CHECK(a.At(t) == Offset{3,0});
}
TEST(animator_zero_duration_jumps_to_end) { OffsetAnimator a({0,0},{1,1},0); CHECK(a.At(0) == Offset{1,1}); CHECK(!a.NextStepAt(0)); }
```

- [ ] **Step 2: Scrivi i test dello scheduler** (`ShiftTiming kT{20000,60000,2000,5000}`):

```cpp
TEST(scheduler_holds_origin_during_first_dwell) { oled::ShiftScheduler s(1,2,kT,0);
  for (double t = 0; t < 20000; t += 100) CHECK(s.OffsetAt(t) == oled::Offset{0,0}); }
TEST(scheduler_moves_within_dwell_plus_transition) { oled::ShiftScheduler s(1,2,kT,0);
  CHECK(!(s.OffsetAt(65000) == oled::Offset{0,0})); }
TEST(scheduler_phase_durations_within_limits) {
  // simula 2 ore a passi di 50 ms; misura le soste (offset fermo) e le transizioni
  // atteso: ogni sosta ∈ [20000-50, 60000+50+5000], nessun cambio di offset > 1 px fra due passi
}
TEST(scheduler_next_change_is_when_offset_changes) {
  oled::ShiftScheduler s(4,2,kT,0); double t = 0;
  for (int i = 0; i < 50; ++i) { double n = s.NextChangeAt(t); CHECK(n > t); auto o = s.OffsetAt(t);
    CHECK(s.OffsetAt(n - 1) == o); t = n; }
}
TEST(scheduler_resume_keeps_offset) {
  oled::ShiftScheduler s(8,2,kT,0); auto before = s.OffsetAt(200000);
  s.Resume(200000 + 7.2e6); CHECK(s.OffsetAt(200000 + 7.2e6) == before);
  CHECK(s.OffsetAt(200000 + 7.2e6 + 19999) == before);
}
TEST(scheduler_tolerates_time_going_backwards) {
  oled::ShiftScheduler s(9,2,kT,0); auto a = s.OffsetAt(100000); CHECK(s.OffsetAt(50000) == a); }
TEST(scheduler_reconfigure_shrinks_amplitude) {
  oled::ShiftScheduler s(10,3,kT,0); double t = 0;
  while (std::abs(s.OffsetAt(t).x) < 2 && std::abs(s.OffsetAt(t).y) < 2) t += 1000;
  s.Reconfigure(1, kT, t); auto o = s.OffsetAt(t + 6000);
  CHECK(std::abs(o.x) <= 1 && std::abs(o.y) <= 1);
}
```
(Il test `scheduler_phase_durations_within_limits` va scritto per esteso con le asserzioni del commento.)

- [ ] **Step 3: Esegui** `.\build.ps1` — atteso: errore di compilazione.
- [ ] **Step 4: Implementa** animator, scheduler, `SharedShift`.
- [ ] **Step 5: Esegui** `.\build.ps1` — atteso: tutti `[PASS]`.
- [ ] **Step 6: Commit** — `feat(core): animazione a passi interi e scheduler dello shift`

### Task 4: Regole di pausa e isteresi

**Files:**
- Create: `src/core/pause_rules.h`, `src/core/pause_rules.cpp`, `tests/test_pause_rules.cpp`

**Interfaces:**
- Produces:
  - `enum class NotificationState { AcceptsNotifications, NotPresent, Busy, RunningD3DFullScreen, PresentationMode, QuietTime, App };`
  - `struct PauseInputs { bool manual = false, session_locked = false, session_inactive = false, secure_desktop = false, capture_failed = false, display_rotated = false, monitor_missing = false; NotificationState notification_state = NotificationState::AcceptsNotifications; bool foreground_covers_monitor = false; std::string foreground_process; };`
  - `enum class PauseReason { None, Manual, SessionLocked, SessionInactive, SecureDesktop, CaptureFailed, DisplayRotated, MonitorMissing, Denylisted, NotificationState, FullscreenWindow };`
  - `struct PauseRulesConfig { bool pause_on_notification_state = true; bool pause_on_fullscreen_window = true; std::vector<std::string> denylist, allowlist; };`
  - `PauseReason EvaluatePause(const PauseInputs&, const PauseRulesConfig&);` — ordine e regole come spec §5.7. Stati che mettono in pausa: `NotPresent`, `Busy`, `RunningD3DFullScreen`, `PresentationMode`. Confronto dei nomi .exe case-insensitive (ASCII).
  - `bool IsImmediate(PauseReason);` — true per Manual, SessionLocked, SessionInactive, SecureDesktop, CaptureFailed, DisplayRotated, MonitorMissing.
  - `std::string_view PauseReasonText(PauseReason);` — UTF-8 italiano, es. `"Pausa manuale"`, `"Schermo bloccato"`, `"Gioco o app a schermo intero"`, `"Finestra a tutto schermo"`, `"Processo in denylist"`, `"Errore di cattura"`, `"Monitor non trovato"`, `"Display ruotato"`, `"Desktop sicuro (UAC)"`, `"Sessione non attiva"`, `"Attivo"`.
  - `class PauseHysteresis { public: PauseHysteresis(double enter_delay_ms, double resume_delay_ms); bool Update(PauseReason raw, double now_ms); bool Paused() const; PauseReason Reason() const; void SetDelays(double enter_delay_ms, double resume_delay_ms); };` — la prima `Update` adotta lo stato grezzo subito; entrata immediata se `IsImmediate`, altrimenti dopo `enter_delay` di pausa grezza continua; uscita dopo `resume_delay` di `None` continuo; mentre è in pausa `Reason()` segue il motivo grezzo corrente (se non `None`).

- [ ] **Step 1: Scrivi i test** — uno per regola, più priorità e isteresi:

```cpp
using namespace oled;
TEST(nothing_means_active) { CHECK(EvaluatePause({}, {}) == PauseReason::None); }
TEST(manual_wins_over_allowlist) { PauseInputs in; in.manual = true; in.foreground_process = "code.exe";
  PauseRulesConfig c; c.allowlist = {"code.exe"}; CHECK(EvaluatePause(in, c) == PauseReason::Manual); }
TEST(lock_wins_over_allowlist) { PauseInputs in; in.session_locked = true; in.foreground_process = "code.exe";
  PauseRulesConfig c; c.allowlist = {"code.exe"}; CHECK(EvaluatePause(in, c) == PauseReason::SessionLocked); }
TEST(allowlist_overrides_fullscreen_and_d3d) { PauseInputs in; in.foreground_process = "mpv.exe";
  in.foreground_covers_monitor = true; in.notification_state = NotificationState::RunningD3DFullScreen;
  PauseRulesConfig c; c.allowlist = {"mpv.exe"}; CHECK(EvaluatePause(in, c) == PauseReason::None); }
TEST(deny_is_case_insensitive) { PauseInputs in; in.foreground_process = "Game.EXE";
  PauseRulesConfig c; c.denylist = {"game.exe"}; CHECK(EvaluatePause(in, c) == PauseReason::Denylisted); }
TEST(notification_states_that_pause) {
  for (auto s : {NotificationState::NotPresent, NotificationState::Busy, NotificationState::RunningD3DFullScreen, NotificationState::PresentationMode}) {
    PauseInputs in; in.notification_state = s; CHECK(EvaluatePause(in, {}) == PauseReason::NotificationState); }
  for (auto s : {NotificationState::QuietTime, NotificationState::App, NotificationState::AcceptsNotifications}) {
    PauseInputs in; in.notification_state = s; CHECK(EvaluatePause(in, {}) == PauseReason::None); }
}
TEST(notification_rule_can_be_disabled) { PauseInputs in; in.notification_state = NotificationState::Busy;
  PauseRulesConfig c; c.pause_on_notification_state = false; CHECK(EvaluatePause(in, c) == PauseReason::None); }
TEST(fullscreen_rule_and_option) { PauseInputs in; in.foreground_covers_monitor = true;
  CHECK(EvaluatePause(in, {}) == PauseReason::FullscreenWindow);
  PauseRulesConfig c; c.pause_on_fullscreen_window = false; CHECK(EvaluatePause(in, c) == PauseReason::None); }
TEST(hysteresis_first_update_is_immediate) { PauseHysteresis h(300, 2500);
  CHECK(!h.Update(PauseReason::None, 0)); PauseHysteresis h2(300, 2500); CHECK(h2.Update(PauseReason::FullscreenWindow, 0)); }
TEST(hysteresis_immediate_reasons) { PauseHysteresis h(300, 2500); h.Update(PauseReason::None, 0);
  CHECK(h.Update(PauseReason::SessionLocked, 10)); }
TEST(hysteresis_enter_delay) { PauseHysteresis h(300, 2500); h.Update(PauseReason::None, 0);
  CHECK(!h.Update(PauseReason::FullscreenWindow, 100)); CHECK(!h.Update(PauseReason::FullscreenWindow, 399));
  CHECK(h.Update(PauseReason::FullscreenWindow, 400)); }
TEST(hysteresis_flash_does_not_pause) { PauseHysteresis h(300, 2500); h.Update(PauseReason::None, 0);
  h.Update(PauseReason::FullscreenWindow, 100); CHECK(!h.Update(PauseReason::None, 200)); CHECK(!h.Update(PauseReason::FullscreenWindow, 450)); }
TEST(hysteresis_resume_delay_and_no_flapping) { PauseHysteresis h(300, 2500); h.Update(PauseReason::Manual, 0);
  CHECK(h.Update(PauseReason::None, 1000)); CHECK(h.Update(PauseReason::Manual, 2000));   // il conteggio riparte
  CHECK(h.Update(PauseReason::None, 3000)); CHECK(h.Update(PauseReason::None, 5499)); CHECK(!h.Update(PauseReason::None, 5500)); }
```

- [ ] **Step 2: Esegui** `.\build.ps1` — atteso: errore di compilazione.
- [ ] **Step 3: Implementa** `pause_rules.cpp`.
- [ ] **Step 4: Esegui** `.\build.ps1` — atteso: tutti `[PASS]`.
- [ ] **Step 5: Commit** — `feat(core): regole di pausa con isteresi`

### Task 5: JSON minimale

**Files:**
- Create: `src/core/json.h`, `src/core/json.cpp`, `tests/test_json.cpp`

**Interfaces:**
- Produces:
  - `class JsonValue` con `enum class Type { Null, Bool, Number, String, Array, Object }`; costruttori da `nullptr`, `bool`, `double`, `int`, `std::string`, `const char*`; factory `JsonValue::MakeArray()`, `JsonValue::MakeObject()`; accessori `Type type()`, `bool AsBool()`, `double AsNumber()`, `const std::string& AsString()`, `const std::vector<JsonValue>& AsArray()`, `const std::vector<std::pair<std::string, JsonValue>>& AsObject()` (ordine delle chiavi conservato), `const JsonValue* Find(std::string_view key) const`, `void Set(std::string key, JsonValue v)`, `void Push(JsonValue v)`.
  - `struct JsonParseResult { std::optional<JsonValue> value; std::string error; size_t offset = 0; };`
  - `JsonParseResult ParseJson(std::string_view utf8);` — salta il BOM UTF-8; RFC 8259 senza estensioni; escape `\" \\ \/ \b \f \n \r \t \uXXXX` (coppie surrogate → un codepoint UTF-8); profondità massima 64; errore con messaggio italiano e offset.
  - `std::string SerializeJson(const JsonValue&, int indent = 2);` — numeri interi senza decimali (`2`, non `2.0`); stringhe con escape; output stabile.

- [ ] **Step 1: Scrivi i test**: `json_parses_scalars` (`null`, `true`, `-1.5e2` == -150, `"a"`), `json_parses_nested` (`{"a":[1,{"b":false}]}` → `Find("a")->AsArray()[1].Find("b")->AsBool() == false`), `json_string_escapes` (`"\"\\\nè😀"` → `"\"\\\nè😀"` in UTF-8), `json_skips_utf8_bom` (`"\xEF\xBB\xBF{\"x\":1}"` si analizza, `x == 1`), `json_rejects_garbage` (ognuno di `{`, `{"a":}`, `[1,]`, `{"a":1}x`, `"\x01"`, `tru` → `!value` ed `error` non vuoto), `json_depth_limit` (65 `[` annidate → errore), `json_roundtrip` (`ParseJson(SerializeJson(v))` uguale a `v` per un oggetto con tutti i tipi; `SerializeJson(JsonValue(2))` contiene `2` e non `2.0`).
- [ ] **Step 2: Esegui** `.\build.ps1` — atteso: errore di compilazione.
- [ ] **Step 3: Implementa** il parser a discesa ricorsiva e il serializer; numeri con `std::from_chars`/`std::to_chars`.
- [ ] **Step 4: Esegui** `.\build.ps1` — atteso: tutti `[PASS]`.
- [ ] **Step 5: Commit** — `feat(core): parser e serializer JSON minimali`

### Task 6: Config, validazione, parsing hotkey, selezione monitor

**Files:**
- Create: `src/core/config.h`, `src/core/config.cpp`, `src/core/hotkey_parse.h`, `src/core/hotkey_parse.cpp`, `src/core/monitor_select.h`, `src/core/monitor_select.cpp`, `tests/test_config.cpp`, `tests/test_hotkey.cpp`, `tests/test_monitor_select.cpp`

**Interfaces:**
- Consumes: `JsonValue`, `ParseJson`, `SerializeJson`, `PauseRulesConfig`, `ShiftTiming`
- Produces:
  - `struct Config` con i campi e i default di spec §5.9 (`std::string monitor = "auto"; int amplitude_px = 2; double dwell_min_s = 20, dwell_max_s = 60, transition_min_s = 2, transition_max_s = 5; bool pause_on_notification_state = true, pause_on_fullscreen_window = true; int pause_enter_delay_ms = 300, resume_delay_ms = 2500; std::vector<std::string> denylist, allowlist; std::string hotkey_toggle = "Ctrl+Alt+Shift+P", hotkey_emergency_exit = "Ctrl+Alt+Shift+F12"; bool allow_tearing = false, autostart = false; std::string log_level = "info";`)
  - `struct ConfigLoadResult { Config config; std::vector<std::string> warnings; bool parse_failed = false; };`
  - `ConfigLoadResult ConfigFromJson(std::string_view text);` — campo mancante → default senza avviso; tipo sbagliato o fuori intervallo → default + avviso; interi devono avere valore intero; `min > max` → default per la coppia + avviso; liste → stringhe in minuscolo, elementi non stringa saltati con avviso; hotkey non parsabili o uguali fra loro → default per entrambe + avviso; chiave sconosciuta → avviso. JSON non valido → `parse_failed = true` + avviso con l'errore.
  - `std::string ConfigToJson(const Config&);` — tutte le chiavi nell'ordine di spec §5.9.
  - `PauseRulesConfig ToPauseRules(const Config&); ShiftTiming ToShiftTiming(const Config&);` (secondi → ms)
  - `struct Hotkey { unsigned modifiers = 0; unsigned vk = 0; bool operator==(const Hotkey&) const = default; };` costanti `kModAlt = 0x1, kModControl = 0x2, kModShift = 0x4, kModWin = 0x8` (stessi valori di `MOD_*` Win32).
  - `std::optional<Hotkey> ParseHotkey(std::string_view);` — token separati da `+`, case-insensitive, spazi ignorati; modificatori `Ctrl`/`Control`, `Alt`, `Shift`, `Win`; tasti `A`–`Z` (0x41…), `0`–`9` (0x30…), `F1`–`F24` (0x70…0x87), `Pause` 0x13, `Insert` 0x2D, `Delete` 0x2E, `Home` 0x24, `End` 0x23, `PageUp` 0x21, `PageDown` 0x22, `Space` 0x20; esattamente un tasto e almeno un modificatore.
  - `struct MonitorDesc { std::string device_name; std::string friendly_name; bool primary = false; };`
  - `std::optional<size_t> SelectMonitor(std::span<const MonitorDesc>, std::string_view setting);` — `"auto"`: primo con `friendly_name` che contiene `"LG TV"` (case-insensitive), altrimenti il primario, altrimenti il primo; altrimenti uguaglianza case-insensitive con `friendly_name` o `device_name`; nessuno → `nullopt`.

- [ ] **Step 1: Scrivi i test**:
  - `config_defaults_from_empty_object`: `ConfigFromJson("{}")` → nessun avviso, `amplitude_px == 2`, `resume_delay_ms == 2500`, `hotkey_emergency_exit == "Ctrl+Alt+Shift+F12"`, `allow_tearing == false`.
  - `config_out_of_range_uses_default`: `{"amplitude_px": 7}` → `amplitude_px == 2`, 1 avviso che contiene `"amplitude_px"`.
  - `config_non_integer_uses_default`: `{"amplitude_px": 2.5}` → 2, 1 avviso.
  - `config_wrong_type_uses_default`: `{"allow_tearing": "si"}` → false, 1 avviso.
  - `config_min_greater_than_max`: `{"dwell_min_s": 90, "dwell_max_s": 30}` → 20 e 60, 1 avviso.
  - `config_lists_are_lowercased`: `{"denylist": ["Game.EXE", 3]}` → `{"game.exe"}`, 1 avviso.
  - `config_rejects_equal_hotkeys`: `{"hotkey_toggle": "Ctrl+Alt+X", "hotkey_emergency_exit": "ctrl+alt+x"}` → entrambe ai default, ≥1 avviso.
  - `config_unknown_key_warns`: `{"foo": 1}` → 1 avviso che contiene `"foo"`.
  - `config_malformed_json`: `"{"` → `parse_failed == true`.
  - `config_roundtrip`: un `Config` con valori non di default → `ConfigFromJson(ConfigToJson(c))` uguale, nessun avviso.
  - `hotkey_parses`: `"Ctrl+Alt+Shift+F12"` → `{kModControl|kModAlt|kModShift, 0x7B}`; `"win + p"` → `{kModWin, 0x50}`.
  - `hotkey_rejects`: `"F12"` (nessun modificatore), `"Ctrl+Alt"` (nessun tasto), `"Ctrl+A+B"`, `"Ctrl+F25"`, `""` → `nullopt`.
  - `monitor_auto_prefers_lg`: `[{"\\\\.\\DISPLAY1","DELL U2720Q",true},{"\\\\.\\DISPLAY2","LG TV SSCR2",false}]`, `"auto"` → 1.
  - `monitor_auto_falls_back_to_primary`: senza LG → indice del primario.
  - `monitor_by_name_or_device`: `"lg tv sscr2"` → 1; `"\\\\.\\display1"` → 0; `"Samsung"` → `nullopt`.
- [ ] **Step 2: Esegui** `.\build.ps1` — atteso: errore di compilazione.
- [ ] **Step 3: Implementa** i tre moduli. Intervalli di validazione: spec §5.9.
- [ ] **Step 4: Esegui** `.\build.ps1` — atteso: tutti `[PASS]`.
- [ ] **Step 5: Commit** — `feat(core): config con validazione, hotkey e selezione monitor` — **fine milestone A**: elenco test passati per l'utente.

---

## Milestone B — capture + overlay senza shift (SDR e HDR)

### Task 7: Eseguibile, log, percorsi, thread di emergenza e watchdog

**Files:**
- Create: `src/app/main.cpp`, `src/app/app.h`, `src/app/app.cpp`, `src/app/logger.h`, `src/app/logger.cpp`, `src/app/paths.h`, `src/app/paths.cpp`, `src/app/emergency.h`, `src/app/emergency.cpp`, `src/app/text.h` (UTF-8 ↔ UTF-16), `src/app/oled-guard.manifest`, `src/app/oled-guard.rc`, `src/app/app.ico`
- Modify: `CMakeLists.txt` (target `oled-guard` WIN32, link `d3d11 dxgi dcomp dwmapi wtsapi32 shell32 user32 advapi32 shlwapi`)

**Interfaces:**
- Consumes: `Config`, `ParseHotkey`
- Produces:
  - `enum class LogLevel { Debug, Info, Warn, Error };` `void LogInit(const std::filesystem::path& dir, LogLevel min_level); void LogSetLevel(LogLevel); void Log(LogLevel, std::string_view utf8);` più `template<class... A> void LogInfo(std::format_string<A...>, A&&...)` (e `LogDebug/LogWarn/LogError`). File `oled-guard.log`; a 1 MB rotazione `.log → .1.log → .2.log`, 3 file in tutto; mutex; riga `2026-11-21 14:03:22.123 [INFO] testo`.
  - `std::filesystem::path AppDataDir();` (`%APPDATA%\oled-guard`, creata se manca) e `LogDir()`.
  - `std::wstring Widen(std::string_view); std::string Narrow(std::wstring_view);`
  - `class EmergencyGuard { public: bool Start(Hotkey exit_hotkey); void Stop(); void BeatMain(); void BeatRender(); void SetOverlayActive(bool); };` — thread proprio con message queue; `RegisterHotKey(nullptr, id, mods | MOD_NOREPEAT, vk)`; su `WM_HOTKEY` → `LogError("Uscita di emergenza")` e `TerminateProcess(GetCurrentProcess(), 3)`. Ogni 500 ms controlla: se `overlay_active` e il battito del main o del render è più vecchio di 3000 ms → log e `TerminateProcess(..., 4)`. Battiti come `std::atomic<int64_t>` (ms da `steady_clock`).
  - `class App { public: int Run(HINSTANCE); };` — in questa task: finestra di messaggi nascosta (`HWND_MESSAGE`), `SetUnhandledExceptionFilter` che scrive nel log e termina, timer di 1 s che chiama `BeatMain()`.
  - Manifest: `dpiAwareness` PerMonitorV2, `supportedOS` Windows 10/11, `activeCodePage` UTF-8.

- [ ] **Step 1: Implementa** i file sopra; `main.cpp` → `wWinMain` → `App().Run(hInstance)`.
- [ ] **Step 2: Esegui** `.\build.ps1` — atteso: compila senza warning, test verdi.
- [ ] **Step 3: Prova a mano** l'avvio di `build\oled-guard.exe` e l'uscita di emergenza (`Ctrl+Alt+Shift+F12`): il processo termina e nel log compare `Uscita di emergenza`. Verifica con `Get-Process oled-guard` e leggendo il log.
- [ ] **Step 4: Commit** — `feat(app): eseguibile, log con rotazione, uscita di emergenza e watchdog`

### Task 8: Enumerazione monitor e DesktopCapture

**Files:**
- Create: `src/app/monitors.h`, `src/app/monitors.cpp`, `src/app/capture.h`, `src/app/capture.cpp`

**Interfaces:**
- Consumes: `MonitorDesc`, `SelectMonitor`, `LogInfo/LogWarn`
- Produces:
  - `struct MonitorInfo { std::wstring device_name; std::wstring friendly_name; RECT rect{}; LUID adapter_luid{}; bool primary = false; DXGI_MODE_ROTATION rotation = DXGI_MODE_ROTATION_IDENTITY; };`
  - `std::vector<MonitorInfo> EnumerateMonitors();` — DXGI (`CreateDXGIFactory1`, `EnumAdapters1`, `EnumOutputs`, `DXGI_OUTPUT_DESC`) per device name, rect, rotazione, LUID; nome amichevole con `QueryDisplayConfig` + `DisplayConfigGetDeviceInfo(DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME)` abbinando `GET_SOURCE_NAME.viewGdiDeviceName` al device name.
  - `std::optional<MonitorInfo> FindMonitor(std::string_view setting);` — usa `SelectMonitor`.
  - `HRESULT OpenOutput(const MonitorInfo&, IDXGIAdapter1** adapter, IDXGIOutput5** output);`
  - `class DesktopCapture { public: enum class Status { NewFrame, NoChange, AccessLost, Failed }; HRESULT Start(ID3D11Device*, IDXGIOutput5*); void Stop(); DXGI_FORMAT Format() const; UINT Width() const; UINT Height() const; DXGI_MODE_ROTATION Rotation() const; Status Acquire(UINT timeout_ms, ID3D11Texture2D* cache); };`
  - `Start`: `DuplicateOutput1(device, 0, 2, {DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_B8G8R8A8_UNORM}, &dup)`; formato/dimensioni da `GetDesc`. `Acquire`: `WAIT_TIMEOUT` → `NoChange`; frame con `LastPresentTime.QuadPart == 0` → `ReleaseFrame`, `NoChange`; altrimenti `CopyResource(cache, frame)`, `ReleaseFrame`, `NewFrame`; `DXGI_ERROR_ACCESS_LOST` → `AccessLost`; altro → `Failed` con HRESULT nel log.

- [ ] **Step 1: Implementa** i due moduli.
- [ ] **Step 2: Esegui** `.\build.ps1` — atteso: compila, test verdi.
- [ ] **Step 3: Diagnostica temporanea** — opzione da riga di comando `oled-guard.exe --list-monitors` che scrive nel log l'elenco dei monitor e il formato catturato (avvia `Start` + un `Acquire` e chiude). Esegui e leggi il log: atteso `LG TV SSCR2`, 3840×2160, `R16G16B16A16_FLOAT` con HDR attivo. L'opzione resta (utile per l'utente).
- [ ] **Step 4: Commit** — `feat(app): enumerazione monitor e Desktop Duplication`

### Task 9: Overlay, Presenter, RenderThread (offset fisso 0)

**Files:**
- Create: `src/app/overlay_window.h`, `src/app/overlay_window.cpp`, `src/app/presenter.h`, `src/app/presenter.cpp`, `src/app/render_thread.h`, `src/app/render_thread.cpp`, `src/app/shaders/fullscreen_vs.hlsl`, `src/app/shaders/shift_ps.hlsl`
- Modify: `CMakeLists.txt` (custom command fxc: `/T vs_5_0` e `/T ps_5_0`, `/O3`, `/Fh build/generated/<nome>.h /Vn g_<nome>`; include dir generata), `src/app/app.cpp`

**Interfaces:**
- Consumes: `DesktopCapture`, `MonitorInfo`, `OpenOutput`, `EmergencyGuard`, `Offset`
- Produces:
  - `class OverlayWindow { public: bool Create(HINSTANCE, const RECT& monitor_rect); void Destroy(); HWND Hwnd() const; void ReassertTopmost(); };` — stili di spec §5.4; `WM_NCHITTEST` → `HTTRANSPARENT`; `SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA)`; `SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE)` — se fallisce, `Create` ritorna false. Mostrata con `SW_SHOWNOACTIVATE` subito, ma senza contenuto finché il presenter non attacca la swap chain.
  - `class Presenter { public: HRESULT Init(ID3D11Device*, HWND, UINT width, UINT height, DXGI_FORMAT format, bool allow_tearing); void Shutdown(); HANDLE FrameWaitable() const; HRESULT Draw(ID3D11Texture2D* source, Offset offset); void SetVisible(bool visible); };` — DComp: `DCompositionCreateDevice`, `CreateTargetForHwnd(hwnd, TRUE)`, visual root. Swap chain per composizione: formato = formato catturato, `FLIP_SEQUENTIAL`, 2 buffer, `ALPHA_MODE_IGNORE`, flag `FRAME_LATENCY_WAITABLE_OBJECT` (+ `ALLOW_TEARING` se richiesto e `CheckFeatureSupport` lo conferma); `SetMaximumFrameLatency(1)`; colorspace `RGB_FULL_G10_NONE_P709` per FP16, `RGB_FULL_G22_NONE_P709` per BGRA8. `SetVisible(false)` → `visual->SetContent(nullptr)` + `Commit`; `SetVisible(true)` → `SetContent(swapchain)` + `Commit`. `Draw`: SRV della texture sorgente, constant buffer `int2 offset; uint2 size_minus_one`, triangolo fullscreen, `Present(0, DXGI_PRESENT_ALLOW_TEARING)` o `Present(1, 0)`.
  - Pixel shader: `int2 src = clamp(int2(pos.xy) - offset, int2(0,0), int2(size_minus_one)); return tex.Load(int3(src, 0));`
  - `enum class RenderEvent { FirstFrameShown, CaptureLost, CaptureFailed, DisplayRotated };` notificati con `PostMessage(main_hwnd, WM_APP_RENDER_EVENT, (WPARAM)event, 0)`.
  - `struct RenderParams { HWND overlay; HWND main; MonitorInfo monitor; bool allow_tearing; SharedShift* shift; EmergencyGuard* guard; };` (`shift` nullptr = offset 0)
  - `class RenderThread { public: bool Start(const RenderParams&); void Stop(); };` — `Stop` imposta un `std::atomic<bool>` e fa `join`.
  - Loop: crea device D3D11 sull'adapter dell'output (`D3D_DRIVER_TYPE_UNKNOWN`, feature level 11_0+), avvia la cattura, crea la texture cache (stesso formato/dimensioni, `BIND_SHADER_RESOURCE`) e il presenter. Ciclo: `BeatRender()`; attende `FrameWaitable` (max 100 ms); `Acquire(timeout)` con `timeout` = 500 ms in questa task; se `NewFrame` → `Draw`, e al primo frame `SetVisible(true)` + `FirstFrameShown`. `AccessLost` → `SetVisible(false)`, evento `CaptureLost`, `Stop`/`Start` della cattura con backoff 100 ms → 2 s raddoppiando; se cambiano formato o dimensioni → ricrea presenter e cache. `Failed` o device rimosso → `SetVisible(false)`, ricrea tutto; 5 fallimenti consecutivi → evento `CaptureFailed` e il thread termina. Rotazione ≠ identità → `DisplayRotated` e termina.
  - App (provvisorio per questa milestone): all'avvio trova il monitor (`FindMonitor("auto")`), crea l'overlay, avvia il render thread, `SetOverlayActive(true)`; menu tray minimo con la sola voce **Esci** (icona tray completa in Task 12).

- [ ] **Step 1: Scrivi gli shader** e la regola CMake fxc; verifica che `build/generated/shift_ps.h` venga generato.
- [ ] **Step 2: Implementa** `OverlayWindow`, `Presenter`, `RenderThread`, integrazione in `App`.
- [ ] **Step 3: Esegui** `.\build.ps1` — atteso: compila senza warning, test verdi.
- [ ] **Step 4: Avvia e controlla il log** — atteso: formato della cattura, `FirstFrameShown`, nessun errore; il processo resta in piedi per 30 s (`Get-Process oled-guard`), poi chiusura dalla tray o con l'uscita di emergenza.
- [ ] **Step 5: Commit** — `feat(app): overlay DirectComposition con passthrough SDR/HDR` — **fine milestone B**. Checklist per l'utente:
  1. Desktop identico con e senza app (colori e luminosità in HDR, nessun alone, testo nitido).
  2. Clic, trascinamenti e scroll funzionano ovunque (overlay trasparente ai clic).
  3. Un video su YouTube/Chrome continua ad aggiornarsi sotto l'overlay (rischio occlusione).
  4. Scrolling e mouse: nessun ritardo percepibile, nessun tearing.
  5. Win+L e sblocco, prompt UAC, attivazione/disattivazione HDR nelle impostazioni: l'overlay sparisce e torna da solo, mai un'immagine congelata.
  6. `Ctrl+Alt+Shift+F12` chiude l'app all'istante.
  7. Con G-Sync attivo: nessun flicker sul desktop fermo.

---

## Milestone C — shift

### Task 10: Shift nel render loop

**Files:**
- Modify: `src/app/render_thread.cpp`, `src/app/app.h`, `src/app/app.cpp`

**Interfaces:**
- Consumes: `SharedShift`, `Config`, `ToShiftTiming`
- Produces: `App` possiede `std::unique_ptr<SharedShift> shift_` creato all'avvio con seed da `std::random_device` e `steady_clock` in ms; passato in `RenderParams::shift`.

- [ ] **Step 1: Implementa** nel loop: `now` = ms `steady_clock`; `offset = shift->OffsetAt(now)`; `timeout = clamp(shift->NextChangeAt(now) - now, 0, 500)`; disegna se `NewFrame` **oppure** `offset != last_drawn_offset`; mai `Present` altrimenti. Prima del primo frame non disegna nulla.
- [ ] **Step 2: Opzione di debug** `--fast-shift` da riga di comando: timing 3–5 s di sosta e 1–2 s di transizione, per verificare a occhio senza aspettare.
- [ ] **Step 3: Esegui** `.\build.ps1` — atteso: compila, test verdi. Avvia con `--fast-shift` e controlla nel log (livello debug) gli offset disegnati: cambi di 1 px alla volta, entro ±2.
- [ ] **Step 4: Commit** — `feat(app): pixel shift con redraw solo su frame nuovo o cambio offset` — **fine milestone C**. Checklist per l'utente:
  1. Con `--fast-shift`: guardando la barra delle applicazioni da vicino, lo spostamento si vede a scatti di 1 px, il testo resta nitido.
  2. Ai bordi dello schermo nessuna striscia nera (pixel di bordo ripetuto).
  3. Senza `--fast-shift`, alla distanza abituale: lo spostamento non si nota.
  4. Clic su elementi piccoli (X delle finestre, caselle): sfasamento accettabile.
  5. Desktop fermo: l'uso della GPU in Gestione attività resta vicino a zero fra un passo e l'altro.

---

## Milestone D — pausa automatica

### Task 11: PauseMonitor e macchina a stati dell'overlay

**Files:**
- Create: `src/app/pause_monitor.h`, `src/app/pause_monitor.cpp`
- Modify: `src/app/app.h`, `src/app/app.cpp`

**Interfaces:**
- Consumes: `PauseInputs`, `EvaluatePause`, `PauseHysteresis`, `IsImmediate`, `PauseReasonText`, `FindMonitor`, `RenderEvent`
- Produces:
  - `class PauseMonitor { public: void OnSessionChange(WPARAM wts_event); PauseInputs Collect(const MonitorInfo& monitor, HWND overlay); };` — `SHQueryUserNotificationState` → `NotificationState`; desktop sicuro con `OpenInputDesktop(0, FALSE, GENERIC_READ)` + `GetUserObjectInformationW(UOI_NAME)` ≠ `"Default"` (o `OpenInputDesktop` fallito); finestra in primo piano: esclusi `nullptr`, l'overlay, le classi `Progman`, `WorkerW`, `Shell_TrayWnd`, `Shell_SecondaryTrayWnd`; `DwmGetWindowAttribute(DWMWA_EXTENDED_FRAME_BOUNDS)` contiene interamente `monitor.rect`; nome processo con `QueryFullProcessImageNameW` (nome file, minuscolo, UTF-8). Lock/sessione da `WTS_SESSION_LOCK/UNLOCK`, `WTS_CONSOLE_DISCONNECT/CONNECT`, `WTS_REMOTE_CONNECT/DISCONNECT`.
  - In `App`: `WTSRegisterSessionNotification(main, NOTIFY_FOR_THIS_SESSION)`; timer 250 ms → `Tick()`: monitor = `FindMonitor(config.monitor)` (cache aggiornata su `WM_DISPLAYCHANGE` / `WM_DEVICECHANGE`), `monitor_missing` se assente, `display_rotated` se ruotato; `paused = hysteresis.Update(EvaluatePause(...), now)`; `!paused && !running → StartOverlay()`; `paused && running → StopOverlay()`.
  - `StartOverlay()`: `OverlayWindow::Create` + `RenderThread::Start` + `shift_->Resume(now)` + `guard.SetOverlayActive(true)`. `StopOverlay()`: `guard.SetOverlayActive(false)` + `RenderThread::Stop` + `OverlayWindow::Destroy` (tutto rilasciato: device, duplicazione, swap chain, thread).
  - `OverlayWindow::Create` fallito (es. `WDA_EXCLUDEFROMCAPTURE` rifiutato) o `RenderEvent::CaptureFailed` → `capture_failed = true` per 30 s, poi nuovo tentativo; `DisplayRotated` → `display_rotated = true` fino al prossimo `WM_DISPLAYCHANGE`.
  - `SetWinEventHook(EVENT_SYSTEM_FOREGROUND, …, WINEVENT_OUTOFCONTEXT)` → `ReassertTopmost()` e `Tick()` immediato.
  - Pausa manuale: voce di menu tray **Attivo/In pausa** (toggle di `manual`).
  - Cambio di monitor nella config: `StopOverlay` + `StartOverlay` al tick successivo.

- [ ] **Step 1: Implementa** `PauseMonitor` e la macchina a stati; log a ogni cambio di stato con `PauseReasonText`.
- [ ] **Step 2: Esegui** `.\build.ps1` — atteso: compila, test verdi.
- [ ] **Step 3: Prova automatica**: con l'app avviata, apri da script una finestra Blocco note, portala a coprire l'intero monitor (`SetWindowPos` via P/Invoke in PowerShell, stile popup) e portala in primo piano; dopo 2 s chiudila. Atteso nel log: `In pausa: Finestra a tutto schermo` entro ~0,3 s e `Attivo` ~2,5 s dopo la chiusura. (Lock, UAC e giochi restano nella checklist manuale.)
- [ ] **Step 4: Commit** — `feat(app): pausa automatica con isteresi e overlay a costo zero` — **fine milestone D**. Checklist per l'utente:
  1. Avvio di un gioco DirectX a schermo intero e in borderless: l'overlay sparisce entro ~0,3 s (in Gestione attività il processo scende a ~0% GPU), torna ~2,5 s dopo l'uscita.
  2. Alt-Tab veloce dentro e fuori dal gioco: nessun lampeggio dell'overlay.
  3. Video YouTube con F11 / tasto fullscreen: pausa.
  4. Win+L, schermata Ctrl+Alt+Canc, prompt UAC: nessuna immagine vecchia al ritorno, ripartenza da sola.
  5. Presentazione PowerPoint: pausa.
  6. Pausa manuale dalla tray: sparisce subito, riprende quando la togli.

---

## Milestone E — tray, config, hotkey

### Task 12: Tray completa, config su file con ricarica, hotkey, autostart, istanza singola

**Files:**
- Create: `src/app/tray.h`, `src/app/tray.cpp`, `src/app/config_file.h`, `src/app/config_file.cpp`, `src/app/autostart.h`, `src/app/autostart.cpp`, `src/app/single_instance.h`, `src/app/single_instance.cpp`
- Modify: `src/app/app.h`, `src/app/app.cpp`, `src/app/main.cpp`

**Interfaces:**
- Consumes: `Config`, `ConfigFromJson`, `ConfigToJson`, `ParseHotkey`, `EnumerateMonitors`, `PauseReasonText`, `LogSetLevel`
- Produces:
  - `class Tray { public: bool Create(HWND owner, UINT callback_msg, HICON icon); void Destroy(); void SetTooltip(std::wstring_view); void ShowBalloon(std::wstring_view title, std::wstring_view text); };` — ricrea l'icona su `TaskbarCreated` (Explorer riavviato).
  - Menu (testi esatti): `Attivo` / `In pausa (<motivo>)` (spuntata quando attivo), `Monitor ▸` (una voce per monitor: `<nome amichevole> (<w>×<h>)`, spunta su quello in uso), `Apri configurazione`, `Apri cartella log`, `Avvio automatico` (spunta), separatore, `Esci`. Tooltip: `OLED Guard — Attivo` / `OLED Guard — In pausa: <motivo>`.
  - `class ConfigFile { public: explicit ConfigFile(std::filesystem::path); ConfigLoadResult Load(); bool Save(const Config&); bool StartWatching(HWND notify, UINT msg); void StopWatching(); };` — se il file manca lo crea con i default; `Save` scrive su file temporaneo + `MoveFileExW(REPLACE_EXISTING)`; sorveglianza con `FindFirstChangeNotificationW(dir, FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE)` in un thread che fa `PostMessage(notify, msg)`; `App` ricarica dopo 300 ms di quiete (gli editor scrivono più volte). `parse_failed` → tiene la config precedente. Avvisi → log + balloon `Configurazione: <n> avvisi, vedi il log`.
  - Applicazione della config ricaricata: `shift_->Reconfigure`, `hysteresis.SetDelays`, regole di pausa, livello di log, hotkey (re-registrate), autostart, cambio monitor/tearing → riavvio dell'overlay.
  - Hotkey toggle: `RegisterHotKey(main, id, mods | MOD_NOREPEAT, vk)` → toggle di `manual`; se fallisce → balloon `Hotkey <combinazione> già in uso` + log. Hotkey di emergenza: `EmergencyGuard` riavviato con la nuova combinazione.
  - `bool SetAutostart(bool enable); bool IsAutostartEnabled();` — valore `OledGuard` in `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` = `"<percorso exe>"`; allineato a `config.autostart` all'avvio e quando cambia (menu → aggiorna config e salva).
  - `class SingleInstance { public: bool Acquire(); };` — mutex `Local\OledGuard.SingleInstance`; se esiste già: `FindWindowW` della finestra messaggi (classe `OledGuardMain`) e `PostMessage(WM_APP_SECOND_INSTANCE)` → la prima mostra il balloon `OLED Guard è già in esecuzione`; la seconda esce con codice 0.
  - "Apri configurazione" → `ShellExecuteW(L"open", config_path)`; "Apri cartella log" → `ShellExecuteW(L"open", log_dir)`.

- [ ] **Step 1: Implementa** i moduli e il cablaggio in `App`.
- [ ] **Step 2: Esegui** `.\build.ps1` — atteso: compila senza warning, test verdi.
- [ ] **Step 3: Prove automatiche**: avvio → esiste `%APPDATA%\oled-guard\config.json` con i default; seconda istanza → esce subito (exit code 0) e il log della prima registra la seconda istanza; modifica della config da script (`amplitude_px: 7`) → log con avviso e valore 2 applicato; `autostart: true` → valore nel registro presente, `false` → rimosso.
- [ ] **Step 4: Commit** — `feat(app): tray, config JSON con ricarica, hotkey, autostart, istanza singola` — **fine milestone E**. Checklist per l'utente:
  1. Menu della tray: tutte le voci funzionano, il tooltip mostra lo stato e il motivo della pausa.
  2. `Ctrl+Alt+Shift+P` mette in pausa e riprende (anche con un video Netflix in finestra per il caso DRM).
  3. Modifica e salvataggio di `config.json` in Blocco note: applicato senza riavviare; un valore sbagliato produce il balloon.
  4. "Avvio automatico": dopo logout/login l'app riparte.
  5. Riavvio di Explorer (Gestione attività): l'icona della tray ricompare.
  6. Termina il processo da Gestione attività: il desktop torna normale all'istante.
