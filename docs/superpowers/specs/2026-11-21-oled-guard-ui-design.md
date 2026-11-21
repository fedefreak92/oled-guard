# OLED Guard — interfaccia (progetto 1 di 2)

Data: 2026-11-21
Stato: in revisione
Progetto successivo (separato): oscuramento automatico delle aree statiche, con la sua scheda.

## 1. Intento

Oggi OLED Guard lavora in silenzio dietro un'icona nella tray e si configura modificando
`config.json` a mano. L'utente vuole un'app "definitiva": una finestra curata che
**mostri che la protezione sta funzionando** (dove si trova l'immagine, quanto e'
uniforme l'usura, perche' e' in pausa), che permetta di regolare tutto senza JSON e che
raccolga in un posto solo anche le impostazioni di Windows utili a un pannello OLED.

## 2. Obiettivi, non-obiettivi, anti-obiettivi

**Obiettivi**
- Finestra con quattro schede, stile "Strumento di precisione" (mockup approvati):
  **Stato**, **Spostamento**, **Giochi e app**, **Windows per OLED**.
- Si apre dalla tray (nuova prima voce "Apri OLED Guard", doppio clic sull'icona); si
  chiude liberando tutte le risorse. L'app all'avvio resta solo nella tray.
- Statistiche persistenti: tempo per posizione (mappa di calore), uniformita', tempo
  protetto oggi/totale, pause per motivo.
- Storico delle app che hanno messo in pausa con regole AUTO / SEMPRE PAUSA / MAI PAUSA.
- Nove impostazioni di Windows per OLED con stato, valore consigliato, applica,
  "Applica tutti i consigli", "Ripristina come prima"; pulsante e hotkey "Spegni
  schermo ora".
- Interfaccia adatta a un OLED: nero pieno, si attenua al 30% dopo 60 s senza
  interazione.

**Non-obiettivi**
- Oscuramento delle aree statiche (progetto 2).
- Luminosita' SDR in HDR e luce notturna (Windows non offre API documentate).
- Piu' lingue, installer, accessibilita' oltre la navigazione da tastiera di base.

**Anti-obiettivi**
- Lampo bianco all'apertura della finestra.
- Qualsiasi costo (CPU, GPU, RAM di WebView2) a finestra chiusa.
- L'interfaccia che blocca il thread UI (il watchdog chiuderebbe l'app).
- Impostazioni di Windows cambiate senza un clic esplicito dell'utente, o non
  ripristinabili.
- Contenuti o richieste di rete: l'interfaccia e' solo locale.

## 3. Vincoli

- Tutti i vincoli del progetto base restano (C++20, CMake, MSVC, italiano/inglese,
  git locale, test senza framework, `build.ps1`).
- **Unica dipendenza esterna ammessa** (scelta dell'utente): WebView2 SDK, pacchetto
  NuGet `Microsoft.Web.WebView2` 1.0.4314.35 (9,4 MB da nuget.org), di cui si tengono in
  `third_party/webview2/` solo gli header, `WebView2LoaderStatic.lib` x64 e la licenza.
  Runtime WebView2 Evergreen gia' installato (157.0.4341.60).
- Front-end in HTML/CSS/JavaScript **senza librerie** (niente framework, niente CDN).
- Font: Cascadia Mono con ripiego su Consolas.

## 4. Approcci considerati
- **Direct2D/DirectWrite disegnata in proprio**: nessuna dipendenza, ma ogni controllo
  va costruito a mano e il risultato visivo e' piu' lento da raggiungere.
- **WebView2 + HTML/CSS (scelto dall'utente)**: risultato visivo migliore e piu'
  rapido; costo: una dipendenza esterna e ~100 MB di RAM solo a finestra aperta.
- **XAML Islands**: scartato, tecnologia in manutenzione e poco adatta a grafica su misura.

## 5. Design

### 5.1 Struttura

```
third_party/webview2/          header + WebView2LoaderStatic.lib + LICENSE
src/core/                      (logica pura, testata)
  wear_stats.*                 tempo per posizione, uniformita', tempo protetto, pause
  app_history.*                app che hanno messo in pausa, regole
  ui_protocol.*                stato -> JSON, JSON -> comandi validati
  windows_score.*              punteggio e consigli a partire dagli stati letti
src/app/
  ui_window.*                  finestra con WebView2, ciclo di vita, ponte messaggi
  windows_settings.*           lettura/applicazione/ripristino delle 9 impostazioni
  stats_file.*                 persistenza di statistiche e storico
src/ui/                        index.html, app.css, app.js (copiati accanto all'exe in ui/)
```

### 5.2 Finestra (`UiWindow`, thread UI)
- Finestra top-level senza cornice di sistema (barra del titolo disegnata in HTML,
  trascinabile con `app-region: drag` tramite il supporto alle regioni non-client di
  WebView2; pulsanti riduci/ingrandisci/chiudi in HTML). Bordi ridimensionabili.
  Dimensione iniziale 1100×720 DIP, minima 900×600, posizione e dimensione ricordate.
- `CoInitializeEx(COINIT_APARTMENTTHREADED)` sul thread UI all'avvio dell'app.
- Ambiente WebView2 con cartella dati in `%LOCALAPPDATA%\oled-guard\webview2`.
- `DefaultBackgroundColor` nero **prima** della navigazione (niente lampo bianco).
- Impostazioni: niente menu contestuale, zoom, barra di stato, scorciatoie del browser;
  DevTools solo con `--debug`.
- Contenuti serviti da `SetVirtualHostNameToFolderMapping("oledguard.local", <exe>\ui)`.
  Navigazioni verso altri host annullate, nuove finestre negate.
- Chiusura: distrugge controller e ambiente (i processi di WebView2 terminano).
- Il thread UI non attende mai WebView2 in modo sincrono: tutto e' a callback.

### 5.3 Ponte messaggi (JSON)
Definito in `ui_protocol` (puro, testato). App → interfaccia (`PostWebMessageAsJson`):
- `state` (4 volte al secondo a finestra visibile, e subito a ogni cambiamento):
  attivo/pausa e motivo, processo in primo piano, offset corrente, ultime 4 posizioni
  (per la scia), prossima mossa (ms), ampiezza, monitor e formato (HDR/SDR,
  risoluzione), latenza aggiunta stimata = un periodo di refresh del monitor
  (`DwmGetCompositionTimingInfo`, es. 6,9 ms a 144 Hz).
- `stats`: celle della mappa di calore, uniformita', protetto oggi/totale, pause per
  motivo (ogni 5 s e all'apertura).
- `config`: la configurazione corrente.
- `apps`: storico delle app e regole.
- `windows`: elenco delle 9 impostazioni con stato attuale, consigliato, se
  ripristinabile, punteggio.
- `processes`: app aperte con finestra visibile (su richiesta).

Interfaccia → app (`WebMessageReceived`), ciascun comando validato:
`setConfig {patch}` (fusione con la config corrente, validata da `ConfigFromJson`,
poi applicata e salvata con le regole gia' esistenti), `setManualPause {on}`,
`testShift` (30 s di spostamento accelerato, poi tempi normali), `setAppRule {exe,
rule}`, `addApp {exe}`, `removeApp {exe}`, `listProcesses`, `windowsApply {id}`,
`windowsApplyAll`, `windowsRestoreAll`, `displayOff`, `hotkeyCapture {on}`
(sospende le hotkey globali mentre l'utente registra una combinazione),
`window {minimize|maximize|close}`, `ready` (l'interfaccia e' carica: l'app invia
tutto lo stato).

### 5.4 Statistiche (`WearStats`, `AppHistory`)
- Tempo (ms) per cella su griglia 7×7 (ampiezza massima 3), accumulato solo quando
  l'overlay e' a schermo, dall'offset corrente.
- Uniformita' = `100 × (1 − coefficiente di variazione)` sulle celle dentro l'ampiezza
  corrente, limitata a 0–100; "n/d" con meno di 10 minuti di dati.
- Protetto oggi / totale; pause di oggi per motivo; azzeramento di "oggi" a mezzanotte
  locale.
- `AppHistory`: per ogni .exe che ha causato una pausa automatica o era in primo piano
  durante una pausa manuale: motivo, ultima volta, conteggio; massimo 50 voci (le piu'
  vecchie escono). Regola AUTO/SEMPRE/MAI ricavata da denylist/allowlist della config
  (unica fonte di verita'); cambiarla modifica la config.
- Persistenza in `%APPDATA%\oled-guard\stats.json` ogni 60 s e all'uscita (scrittura
  atomica come la config).

### 5.5 Windows per OLED (`windows_settings`)
Ogni voce ha: lettura stato, valore consigliato, applica, ripristina. Al **primo**
applica di una voce si salva il valore originale in
`%APPDATA%\oled-guard\windows-backup.json`; "Ripristina come prima" rimette solo le voci
salvate e poi cancella i loro backup. Nessuna richiede diritti di amministratore.

| Voce | Come si legge/applica | Consigliato |
|---|---|---|
| Tema scuro sistema e app | `HKCU\…\Themes\Personalize` `AppsUseLightTheme`, `SystemUsesLightTheme` + `WM_SETTINGCHANGE "ImmersiveColorSet"` | 0 / 0 |
| Barra a scomparsa | `SHAppBarMessage(ABM_GETSTATE/ABM_SETSTATE)` | autohide |
| Spegni schermo dopo | piano energetico attivo, `GUID_VIDEO_POWERDOWN_TIMEOUT` (AC), `PowerWriteACValueIndex` + `PowerSetActiveScheme` | 5 min |
| Salvaschermo nero | `SPI_SETSCREENSAVEACTIVE`/`TIMEOUT` + `SCRNSAVE.EXE`=`scrnsave.scr` | 10 min |
| Icone del desktop nascoste | `…\Explorer\Advanced` `HideIcons` + aggiornamento della vista desktop | nascoste |
| Trasparenza disattivata | `…\Personalize` `EnableTransparency` + `WM_SETTINGCHANGE` | 0 |
| Sfondo nero | `SPI_SETDESKWALLPAPER` vuoto + colore desktop nero (`SetSysColors` e registro) | nero |
| Nascondi puntatore mentre scrivi | `SPI_SETMOUSEVANISH` | attivo |
| Accento sulle barre del titolo | `HKCU\Software\Microsoft\Windows\DWM` `ColorPrevalence` | 0 |

Punteggio = voci gia' come consigliato / 9 (calcolo puro in `windows_score`).
**Spegni schermo ora**: `WM_SYSCOMMAND SC_MONITORPOWER 2` sulla finestra principale;
nuova hotkey in config `hotkey_display_off`, default `Ctrl+Alt+Shift+O`, validata come
le altre (tutte e tre diverse fra loro).

### 5.6 Front-end (`src/ui`)
- Una pagina con quattro schede, stile dei mockup approvati
  (`.superpowers/brainstorm/…/stile-visivo.html` opzione B e `schede-stile-b.html`).
- Stato: radar 5×5 (o 3×3/7×7 secondo l'ampiezza) con mappa di calore, scia delle
  ultime 4 mosse, conto alla rovescia della prossima mossa, righe di dati.
- Spostamento: ampiezza segmentata, slider a doppia maniglia per sosta e transizione,
  "Prova ora", registrazione delle hotkey premendo i tasti, avanzate.
- Giochi e app: tabella dello storico con le tre regole, aggiunta di un .exe scritto o
  scelto fra le app aperte, i due interruttori di pausa automatica.
- Windows per OLED: punteggio, nove righe, applica singolo/tutti, ripristina, spegni.
- Attenuazione: dopo 60 s senza mouse/tastiera nella finestra, luminosita' al 30% con
  transizione di 1 s; torna piena al primo movimento.
- **Modalita' anteprima**: se `window.chrome.webview` manca, la pagina usa dati finti
  e animati, cosi' si puo' aprire in un browser per sviluppo e verifica visiva.

## 6. Milestone
- **F1** Logica pura + test: `wear_stats`, `app_history`, `ui_protocol`,
  `windows_score`, nuovo campo `hotkey_display_off`.
- **F2** WebView2: SDK in `third_party`, CMake, `UiWindow` con barra del titolo
  propria, sfondo nero, mapping locale, ponte con `ready`/`state`; voce di tray e
  doppio clic.
- **F3** Front-end completo con modalita' anteprima; collegamento a stato, statistiche
  e config.
- **F4** Windows per OLED: lettura, applica, ripristina, spegni schermo + hotkey.
- **F5** Giochi e app, registrazione hotkey, "Prova ora", persistenza delle statistiche.

Ogni milestone: build e test verdi, commit, checklist visiva breve. Le prove che
cambierebbero impostazioni di Windows sul PC dell'utente le esegue l'utente.

## 7. Rischi
1. **RAM**: ~100–150 MB a finestra aperta (processi WebView2); zero a finestra chiusa.
2. **Regioni non-client** per trascinare la finestra richiedono SDK/runtime recenti
   (presenti); in caso di problemi, ripiego su `WM_NCHITTEST` con area di trascinamento
   comunicata dalla pagina.
3. **Desktop e icone**: nascondere le icone richiede di aggiornare la vista di
   Explorer tramite un messaggio non documentato; se non funziona, si chiede di
   riavviare Explorer.
4. **Piano energetico**: alcune versioni/OEM bloccano la modifica via criteri di
   gruppo; in quel caso la voce mostra "gestito dal sistema".
5. **HDR**: la pagina e' contenuto SDR su desktop HDR; l'aspetto dipende dalla
   luminosita' SDR impostata in Windows (accettabile).
6. **Sicurezza**: solo contenuti locali, navigazioni esterne bloccate, comandi validati.

## 8. Lasciato al builder
- Nomi interni delle funzioni JS, struttura CSS, animazioni.
- Formato interno di `stats.json` e `windows-backup.json`.
- Dove salvare posizione/dimensione della finestra (stats.json o file a parte).
