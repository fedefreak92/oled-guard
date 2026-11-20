# OLED Guard — design

Data: 2026-11-21
Stato: in revisione

## 1. Intento

Il PC usa come monitor una TV LG OLED (LG TV SSCR2, 3840×2160 a 143 Hz, HDR attivo
sul desktop, collegata a una RTX 5080; la GPU integrata è disattivata). Gli elementi
statici di Windows (barra delle applicazioni, icone, bordi di finestre, UI fisse)
occupano sempre gli stessi pixel e rischiano il burn-in.

OLED Guard sposta impercettibilmente l'intera immagine del desktop di pochi pixel,
lentamente, così che i bordi degli elementi statici non insistano sempre sugli stessi
subpixel. Lo fa catturando il desktop e ridisegnandolo, spostato, in un overlay a
schermo intero trasparente ai clic. Lo spostamento integrato della TV non è
un'opzione per l'utente.

## 2. Obiettivi, non-obiettivi, anti-obiettivi

**Obiettivi**
- Spostamento di 1–3 px (ampiezza configurabile), cambio di posizione ogni 20–60 s,
  transizione distribuita su 2–5 s.
- Funziona con desktop in HDR **e** in SDR (passthrough fedele dei colori).
- Pausa automatica in giochi, fullscreen, presentazioni, schermo bloccato, UAC,
  cambio sessione; ripartenza automatica.
- Funzioni di contorno: tray, config JSON, hotkey, avvio automatico, log, istanza
  singola, uscita di emergenza.

**Non-obiettivi**
- Spostare il cursore: resta quello reale del sistema (decisione: "strada A",
  vedi §4.2).
- Rilevare automaticamente contenuti protetti da DRM: l'utente mette in pausa a mano
  da tray o hotkey.
- Più monitor contemporaneamente: si protegge un solo output, scelto in config.
- Installer, firma del codice, aggiornamenti automatici.
- Rotazione del display (portrait): se l'output è ruotato, l'app resta in pausa e lo
  scrive nel log.

**Anti-obiettivi** (sarebbe un fallimento anche se "funziona")
- Testo sfocato o tremolio visibile durante lo spostamento.
- Latenza percepibile, stutter, scatti nello scrolling.
- Overlay che resta a schermo con un'immagine congelata o nera dopo un crash, un blocco
  o la perdita della cattura.
- Overlay che interferisce con i giochi (deve sparire, a costo zero).
- Colori o luminosità diversi da quelli del desktop reale (in HDR come in SDR).

## 3. Vincoli

- C++20, CMake, MSVC, solo Windows SDK (DXGI, D3D11, DirectComposition, Win32).
  Nessuna libreria esterna senza chiedere.
- Nessun readback su CPU: i pixel restano sulla GPU (CopyResource, shader).
- Logica pura separata dal codice Win32/D3D e coperta da test unitari con un
  eseguibile di test senza framework.
- `build.ps1`: un solo comando che configura, compila ed esegue i test.
- Commenti e testi UI in italiano, identificatori in inglese.
- Solo repo git locale, commit a ogni milestone. Nessun codice copiato da progetti
  con licenze incompatibili.
- Toolchain presente: VS Build Tools 2026 (MSVC 14.51) e 2022 (14.44), CMake 4.3 e
  Ninja inclusi nei Build Tools (non nel PATH), Windows SDK 10.0.26100. Sistema:
  Windows 11 build 26300 (`WDA_EXCLUDEFROMCAPTURE` disponibile).

## 4. Approcci considerati

### 4.1 Architettura di base
Fissata dalla richiesta: Desktop Duplication → overlay DirectComposition
click-through → disegno con offset. Alternative scartate:
- *Magnification API* (fullscreen magnifier con `MagSetFullscreenTransform`): sposta
  anche l'input ma lavora con scala ≥ 1 e trasformazioni pensate per lo zoom; non
  permette il riempimento dei bordi né il controllo di pacing e HDR.
- *Spostamento lato TV*: escluso dall'utente.

### 4.2 Cursore e clic sfasati — scelta dell'utente: **A**
- **A (scelta)**: cursore reale, nessuna compensazione; lo sfasamento tra immagine e
  punto di clic è uguale all'offset corrente. Ampiezza di default 2 px.
  Sfasamento medio con distribuzione uniforme: ~1,9 px con ampiezza 2, ~1,1 px con
  ampiezza 1. Latenza del cursore invariata, nessun rischio.
- B (scartata): nascondere il cursore di sistema e disegnarne uno spostato
  nell'overlay. Clic esatti, ma cursore software con almeno un frame di ritardo e
  rischio di cursore invisibile dopo un crash.

### 4.3 HDR — scelto: passthrough in formato nativo
- **Scelto**: `DuplicateOutput1` con formati supportati
  `{R16G16B16A16_FLOAT, B8G8R8A8_UNORM}`. Il formato della swap chain segue quello
  catturato: FP16 + `DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709` (scRGB) in HDR,
  B8G8R8A8 + `RGB_FULL_G22_NONE_P709` in SDR. Lo shader copia i valori senza
  conversioni: colori identici.
- Scartato: solo SDR con pausa in HDR (la richiesta originale). L'utente tiene il
  desktop in HDR, quindi l'app non farebbe mai nulla.

### 4.4 Spostamento sub-pixel vs pixel interi — scelto: **pixel interi**
Un offset frazionario richiede interpolazione e sfoca il testo. L'offset applicato è
sempre intero; la morbidezza viene dal distribuire i singoli passi da 1 px nel tempo
della transizione con una curva di accelerazione dolce.

## 5. Design

### 5.1 Struttura del codice

```
CMakeLists.txt
build.ps1
src/core/        logica pura, nessun include di <windows.h>  → libreria statica oled_core
  rng.*            PRNG deterministico (xoshiro256** o splitmix64) con seed
  offset.*         OffsetGenerator, OffsetAnimator, ShiftScheduler
  pause_rules.*    PauseInputs → PauseDecision, PauseHysteresis
  json.*           parser/serializer JSON minimale
  config.*         struct Config, default, validazione, (de)serializzazione
  hotkey_parse.*   "Ctrl+Alt+Shift+P" → modificatori + virtual key
src/app/         codice Win32/D3D → oled-guard.exe (sottosistema WINDOWS)
  main.cpp, app.*, tray.*, capture.*, overlay.*, renderer.*, pause_monitor.*,
  monitors.*, logger.*, autostart.*, single_instance.*, watchdog.*, config_file.*
src/app/shaders/ shift_ps.hlsl, fullscreen_vs.hlsl (compilati con fxc in build,
                 bytecode incluso come header)
tests/           oled-guard-tests.exe: mini-framework di assert, registrato in CTest
docs/
```

Manifest dell'eseguibile: DPI awareness PerMonitorV2 (indispensabile: senza, le
coordinate della finestra vengono virtualizzate).

### 5.2 Thread

1. **Thread principale (UI)**: message loop, icona tray, hotkey di pausa, notifiche
   di sessione (`WTSRegisterSessionNotification`), `WM_DISPLAYCHANGE`, timer di
   polling della pausa (ogni 250 ms), creazione/distruzione della finestra overlay,
   sorveglianza del file di config.
2. **Thread di render**: esiste solo quando l'overlay è attivo. Possiede device D3D11,
   duplicazione, swap chain e device DirectComposition. Termina (rilasciando tutto)
   quando si entra in pausa.
3. **Thread di emergenza**: registra con `RegisterHotKey` l'hotkey di uscita di
   emergenza sulla propria coda di messaggi e fa anche da watchdog. Funziona anche se
   gli altri thread sono bloccati.

### 5.3 Capture
- Enumera gli adapter DXGI e trova quello che possiede l'output scelto; crea il device
  D3D11 su quell'adapter (`D3D_DRIVER_TYPE_UNKNOWN`), senza flag BGRA-only.
- `IDXGIOutput5::DuplicateOutput1` con i due formati di §4.3.
- Loop: `AcquireNextFrame(timeout)`. Il timeout è il tempo fino al prossimo passo di
  offset, con un massimo di 500 ms.
  - Frame con `LastPresentTime == 0` (solo aggiornamento del puntatore): ignorato,
    nessun redraw.
  - Frame nuovo: `CopyResource` nella texture cache (stesso formato), poi
    `ReleaseFrame` subito.
  - `DXGI_ERROR_WAIT_TIMEOUT`: normale; ridisegna solo se l'offset è cambiato.
  - `DXGI_ERROR_ACCESS_LOST` (cambio modalità, UAC, lock, toggle HDR): **nasconde
    subito l'overlay** (stacca la swap chain dal visual DComp e fa Commit, quindi
    nessuna immagine vecchia resta a schermo), ricrea la duplicazione con backoff
    (100 ms → 2 s). Se cambia formato o dimensione ricrea anche la swap chain.
    Riattacca l'overlay solo dopo il primo frame nuovo disegnato.
  - Altri errori (device removed, ecc.): l'overlay viene nascosto, il thread di render
    ricrea tutto da zero con backoff; dopo 5 fallimenti consecutivi l'app va in pausa
    con motivo "errore di cattura" visibile nella tray.
- Output ruotato: niente overlay, pausa con motivo nel log.

### 5.4 Overlay
- Finestra `WS_POPUP` grande quanto il monitor, stili estesi
  `WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_NOACTIVATE |
  WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP`; `WM_NCHITTEST` → `HTTRANSPARENT`.
  Mostrata con `SW_SHOWNOACTIVATE`.
- `SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE)`: se fallisce, l'overlay non
  parte (evita il feedback loop) e l'errore va nel log e nella tray.
- Swap chain `CreateSwapChainForComposition`, flip model (`FLIP_SEQUENTIAL`, 2
  buffer), `FRAME_LATENCY_WAITABLE_OBJECT`, `SetMaximumFrameLatency(1)`, alpha
  ignorata (overlay opaco). Flag `ALLOW_TEARING` se supportato **e** abilitato in
  config.
- Il TOPMOST viene riaffermato (`SetWindowPos(HWND_TOPMOST, … SWP_NOACTIVATE)`) quando
  cambia la finestra in primo piano (`SetWinEventHook(EVENT_SYSTEM_FOREGROUND)`),
  perché la barra delle applicazioni e altre finestre topmost possono salirgli sopra.

### 5.5 Render
- Triangolo a schermo intero senza vertex buffer. Pixel shader:
  `src = clamp(int2(SV_Position.xy) - offset, 0, size - 1); return tex.Load(src);`.
  Copia esatta pixel per pixel, nessun filtro; i bordi scoperti ripetono il pixel di
  bordo.
- Offset passato in un constant buffer.
- Si presenta **solo** se è arrivato un frame nuovo o se l'offset intero è cambiato.
  Prima di disegnare si attende l'oggetto waitable della swap chain.
- Present: `Present(0, DXGI_PRESENT_ALLOW_TEARING)` se il tearing è abilitato e
  supportato, altrimenti `Present(1, 0)`.

### 5.6 Offset (logica pura)
- **OffsetGenerator(seed, amplitude)**: posizione corrente su griglia intera con
  |x| ≤ A e |y| ≤ A. Il prossimo target è scelto uniformemente fra tutti i punti della
  griglia **diversi** dalla posizione corrente (distribuzione uniforme → usura
  uniforme nel lungo periodo). Si parte da (0,0).
- **OffsetAnimator**: da `from` a `to` con durata D. Il percorso è una linea discreta
  (stile Bresenham, passi diagonali ammessi) di n = distanza di Chebyshev passi.
  All'istante t la posizione è il punto di indice `round(smoothstep(t/D) * n)` sul
  percorso. Proprietà: parte da `from`, finisce in `to`, monotona, ogni passo cambia
  ciascun asse di al massimo 1 px, nessun avanti-indietro. Espone anche l'istante del
  prossimo passo (per il timeout di cattura).
- **ShiftScheduler**: alterna sosta (durata uniforme in [dwell_min, dwell_max]) e
  transizione (durata uniforme in [transition_min, transition_max]). Il tempo è
  sempre passato come argomento (ms), mai letto dall'orologio: tutto testabile.
- Lo stato dello scheduler appartiene all'oggetto App (non al thread di render) e
  sopravvive alle pause: alla ripresa l'offset riparte da dove era. Il thread di render
  lo interroga sotto mutex (`OffsetAt(now_ms)`, `NextChangeAt(now_ms)`) usando
  `steady_clock` come sorgente del tempo.
- Se `amplitude_px` cambia con la ricarica della config, la posizione corrente viene
  riportata entro i nuovi limiti con una transizione normale.

### 5.7 Pausa automatica (logica pura + raccolta segnali)

**Segnali** (raccolti da `PauseMonitor` sul thread UI, ogni 250 ms ed evento):
- `manual`: pausa manuale da tray/hotkey.
- `session_locked`, `session_inactive` (WTS lock/unlock, console connect/disconnect,
  remote).
- `secure_desktop`: il desktop di input non è "Default" (`OpenInputDesktop` +
  nome), per UAC e schermata Ctrl+Alt+Canc.
- `notification_state`: da `SHQueryUserNotificationState`.
- `foreground_covers_monitor`: finestra in primo piano (esclusi desktop, shell,
  barra delle applicazioni e l'overlay stesso), confronto fra
  `DWMWA_EXTENDED_FRAME_BOUNDS` e il rettangolo **intero** del monitor (una finestra
  massimizzata non copre la barra, quindi non conta).
- `foreground_process`: nome .exe del processo in primo piano (minuscolo).
- `capture_failed`, `display_rotated`: dal thread di render.
- `monitor_missing`: il monitor in config non è collegato (TV spenta, cavo staccato).
  Rientra nella regola 3; al ritorno del monitor (`WM_DISPLAYCHANGE`,
  `WM_DEVICECHANGE`) si riparte da soli. Un cambio di monitor dalla tray o dalla
  config ricrea l'overlay.

**Regole** (`EvaluatePause(inputs, config) → {pause, reason}`), in ordine:
1. `manual` → pausa ("pausa manuale").
2. `session_locked`, `session_inactive`, `secure_desktop` → pausa.
3. `capture_failed`, `display_rotated`, `monitor_missing` → pausa.
4. Processo in allowlist → **attivo** (salta le regole 5–7).
5. Processo in denylist → pausa.
6. `notification_state` ∈ {RUNNING_D3D_FULL_SCREEN, BUSY, PRESENTATION_MODE,
   NOT_PRESENT} → pausa (disattivabile con `pause_on_notification_state`).
7. `foreground_covers_monitor` → pausa (disattivabile con
   `pause_on_fullscreen_window`).
8. Altrimenti attivo.

**Isteresi** (`PauseHysteresis`, tempo iniettato):
- Entrata in pausa: immediata per le regole 1–3; dopo `pause_enter_delay_ms`
  (default 300 ms) di condizione continua per le regole 5–7, così un lampo di
  fullscreen non distrugge l'overlay.
- Uscita dalla pausa: solo dopo `resume_delay_ms` (default 2500 ms) di "attivo"
  continuo.

**In pausa** l'overlay è distrutto: finestra, swap chain, duplicazione, device D3D e
thread di render. Costo zero per i giochi. Alla fine della pausa tutto viene ricreato.

### 5.8 Sicurezza
- La finestra overlay appartiene al processo: se il processo muore (crash, kill), il
  sistema la distrugge e il desktop torna normale. Non si toccano impostazioni di
  sistema (niente cursori modificati, niente modalità video cambiate), quindi non c'è
  nulla da ripristinare.
- Filtro delle eccezioni non gestite: scrive nel log e termina.
- **Uscita di emergenza**: hotkey globale (default `Ctrl+Alt+Shift+F12`) gestita dal
  thread di emergenza → `TerminateProcess` immediato.
- **Watchdog**: il thread di render aggiorna un heartbeat a ogni ciclo; il thread
  principale risponde a un ping ogni secondo. Se uno dei due è bloccato da più di
  3 s mentre l'overlay è visibile, il watchdog termina il processo.
- Overlay mostrato solo dopo il primo frame valido; nascosto subito alla perdita
  della cattura.

### 5.9 Funzioni di contorno
- **Tray** (icona + tooltip con stato e motivo della pausa). Menu:
  Attivo/In pausa (toggle manuale) · Monitor ▸ elenco (nome amichevole + risoluzione)
  · Apri configurazione · Apri cartella log · Avvio automatico (spunta) · Esci.
- **Config**: `%APPDATA%\oled-guard\config.json`. Creato con i default se manca. Il
  file è sorvegliato e ricaricato automaticamente quando cambia. Valori non validi →
  default per quel campo + avviso nel log (e un balloon nella tray). JSON malformato →
  si tengono i valori precedenti + avviso. Chiavi sconosciute ignorate con avviso.

  ```json
  {
    "monitor": "auto",
    "amplitude_px": 2,
    "dwell_min_s": 20,
    "dwell_max_s": 60,
    "transition_min_s": 2,
    "transition_max_s": 5,
    "pause_on_notification_state": true,
    "pause_on_fullscreen_window": true,
    "pause_enter_delay_ms": 300,
    "resume_delay_ms": 2500,
    "denylist": [],
    "allowlist": [],
    "hotkey_toggle": "Ctrl+Alt+Shift+P",
    "hotkey_emergency_exit": "Ctrl+Alt+Shift+F12",
    "allow_tearing": false,
    "autostart": false,
    "log_level": "info"
  }
  ```
  Validazione: `amplitude_px` 1–3; dwell 5–600 s con min ≤ max; transition 0,5–30 s con
  min ≤ max; ritardi 0–10000 ms; hotkey parsabili e diverse fra loro; `log_level` ∈
  {debug, info, warn, error}. `monitor`: `"auto"` sceglie l'output il cui nome
  amichevole contiene "LG TV", altrimenti il primario; oppure un nome amichevole
  esatto o un device name tipo `\\.\DISPLAY1`.
  Il JSON viene riscritto dall'app quando si cambia qualcosa dalla tray (monitor,
  autostart).
- **Hotkey**: `RegisterHotKey`; se la combinazione è già occupata, avviso in tray e
  log.
- **Avvio automatico**: valore `OledGuard` in
  `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` con il percorso dell'exe tra
  virgolette; allineato al campo `autostart`.
- **Log**: `%APPDATA%\oled-guard\logs\oled-guard.log`, rotazione a 1 MB, 3 file
  conservati, thread-safe, timestamp locale.
- **Istanza singola**: mutex con nome nella sessione; la seconda istanza mostra un
  avviso tramite la prima e termina.

### 5.10 Build e test
- `build.ps1 [-Config Release|Debug] [-Clean]`: trova Visual Studio con vswhere (il
  più recente con gli strumenti VC), entra nella Developer Shell x64, configura con
  CMake + Ninja in `build/`, compila, esegue `ctest --output-on-failure`. Exit code
  ≠ 0 se qualcosa fallisce.
- Test (`oled-guard-tests.exe`): generatore (mai stesso punto di fila, sempre entro
  l'ampiezza, determinismo con seed, copertura uniforme su molti campioni), animator
  (estremi, monotonia, passi ≤ 1 px, nessun ritorno, istante del prossimo passo),
  scheduler (durate entro i limiti), regole di pausa (ogni regola e priorità),
  isteresi (entrata immediata/ritardata, uscita ritardata, niente flapping), JSON
  (valori, escape, `\uXXXX`, errori), config (default, validazione, round-trip),
  parsing hotkey.

## 6. Milestone
- **A** Logica pura + test + `build.ps1` + git init.
- **B** Capture + overlay senza shift, SDR **e HDR**, gestione ACCESS_LOST/TIMEOUT,
  uscita di emergenza, watchdog. Avvio da riga di comando, tray minima (Esci).
- **C** Shift con scheduler/animator, redraw solo su frame nuovo o cambio offset.
- **D** Pausa automatica completa con isteresi e distruzione/ricreazione overlay.
- **E** Tray completa, config JSON con ricarica, hotkey pausa, autostart, log con
  rotazione, istanza singola.

Ogni milestone: build e test verdi, commit locale, checklist di verifica manuale.

## 7. Rischi tecnici

1. **Latenza aggiunta**: almeno +1 refresh (~7 ms a 143 Hz) su tutto quello che sta
   sotto l'overlay: DWM compone il frame, la duplicazione lo consegna, noi
   presentiamo, DWM ricompone. Accettato dall'utente se impercettibile; mitigato da
   frame latency 1 e waitable swap chain.
2. **`WDA_EXCLUDEFROMCAPTURE`**: richiede Windows 10 2004+ (qui c'è 11). Costringe
   DWM a una composizione separata per la cattura: più lavoro GPU. Da misurare.
3. **VRR / G-Sync**: l'overlay presenta a ritmo irregolare (solo quando il desktop
   cambia). Se G-Sync "finestra e schermo intero" è attivo nel pannello NVIDIA e il
   driver lo applica all'overlay, la TV potrebbe scendere a refresh bassi con il noto
   flicker/gamma shift degli OLED LG in VRR. L'overlay non prende mai il focus, quindi
   probabilmente il driver non lo considera; da verificare a occhio.
4. **Tearing**: la richiesta chiedeva tearing consentito. Sul desktop senza VRR il
   tearing produce linee visibili durante lo scrolling (anti-obiettivo), quindi è
   supportato ma **disattivato di default** (`allow_tearing: false`).
5. **Occlusione**: un overlay opaco a schermo intero copre tutte le finestre. Windows
   e alcune app (Chrome/Edge hanno un tracker di occlusione nativo) potrebbero
   considerarle coperte e smettere di disegnare → la cattura vedrebbe un'immagine
   ferma. `WS_EX_LAYERED | WS_EX_TRANSPARENT` dovrebbe escludere l'overlay dal
   calcolo; da verificare in milestone B con un video in riproduzione nel browser.
6. **Finestre sopra l'overlay**: menu Start, Alt-Tab, centro notifiche, alcune UI di
   sistema stanno in bande z-order superiori e appaiono sopra l'overlay, non spostate;
   ai loro bordi resta visibile una striscia di 1–3 px della copia spostata. La barra
   delle applicazioni può salire sopra l'overlay quando viene cliccata (mitigato
   riaffermando il TOPMOST).
7. **DRM**: i contenuti protetti risultano neri nella cattura → rettangolo nero.
   Gestione manuale (pausa da tray/hotkey) per decisione dell'utente.
8. **Clic sfasati**: fino a `amplitude_px` px (media ~1,9 px con ampiezza 2).
   Accettato (strada A).
9. **Banda GPU**: a 4K FP16 un frame è ~66 MB; copia + disegno ≈ 200 MB per frame,
   ~28 GB/s a 143 Hz con il desktop in movimento continuo. Irrilevante per una RTX
   5080; ottimizzazione con i dirty rect lasciata a dopo.
10. **Multi-GPU**: la GPU integrata è disattivata; il device viene comunque creato
    sull'adapter che possiede l'output, così funziona anche su sistemi ibridi.
11. **Cambio HDR/SDR a caldo**: arriva come ACCESS_LOST; si ricrea duplicazione e
    swap chain col nuovo formato e colorspace.

## 8. Lasciato al builder
- Scelta esatta del PRNG, struttura interna delle classi, nomi dei file oltre a
  quelli indicati.
- Icona della tray (risorsa .ico semplice disegnata a mano o icona di sistema).
- Dettagli del mini-framework di test e dell'output di `build.ps1`.
- Come compilare gli shader (preferito fxc in build tramite CMake).
