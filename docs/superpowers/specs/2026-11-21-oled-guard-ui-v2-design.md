# OLED Guard — nuova interfaccia (progetto 1 di 5)

Data: 2026-11-21
Stato: in revisione
Riferimento visivo: le 7 schermate in `C:\Users\feder\Downloads\ui` (Dashboard, Overlay,
Monitors, App Profiles, Settings, Advanced, Statistics).

## 1. Intento

L'utente vuole che OLED Guard abbia **l'aspetto, il comportamento e le funzioni**
dell'interfaccia di riferimento: barra laterale, barra in alto con lo stato, pannelli
scuri con accento verde acqua, striscia fissa in basso con i numeri della protezione.
Il lavoro e' diviso in cinque progetti che si costruiscono uno dopo l'altro:

1. **Nuova interfaccia** (questo documento): tutte le funzioni di oggi nel nuovo
   aspetto, piu' le aggiunte piccole.
2. Segnali dal vivo e statistiche (mappa di usura del pannello, grafico nel tempo, app
   piu' usate, sessioni, usura evitata, avviso nella tray).
3. Livelli 1-5 e preset (sistema + utente, importa/esporta).
4. Profili per app (preset applicato quando un'app e' in primo piano, piu' eseguibili
   per profilo).
5. Autopilota e pagina Avanzate completa.

Ogni progetto aggiunge la sua pagina o sezione **gia' funzionante**: l'app non mostra
mai controlli finti.

## 2. Obiettivi, non-obiettivi, anti-obiettivi

**Obiettivi**
- Nuovo guscio: barra laterale con tre gruppi, barra in alto, striscia in basso,
  dieci pagine (vedi 5.3). Testi in italiano.
- Tutte le funzioni di oggi raggiungibili nel nuovo aspetto, nessuna persa.
- Aggiunte di questo progetto:
  - **Bordi scuriti** (vignettatura) con estensione per lato e intensita'.
  - **Luminosita' via software** del monitor protetto.
  - **Modalita' assente**: lo schermo si scurisce piano dopo X minuti senza input e
    senza movimento, e torna normale subito al primo input.
  - **Pagina Monitor** con elenco degli schermi e scelta di quello protetto.
  - **Attivita' recente** (pause, riprese, cambi importanti) nella Panoramica.
  - **Colore d'accento** e **dimensione del testo** dell'interfaccia.
  - **Apri cartella dei log**, **Sfoglia...** per scegliere un .exe.

**Non-obiettivi (arrivano nei progetti 2-5)**
- Grafici dal vivo di rischio/movimento/luminosita', mappa di usura del pannello,
  grafico nel tempo, app piu' usate, sessioni e serie di giorni, usura evitata,
  avviso nella tray.
- Livelli, preset, profili con preset, autopilota e le sue pillole nella barra in alto.

**Esclusi (decisi con l'utente)**
- Rumore animato (Blue Noise, Ordered Dither, filtro dei buchi, velocita'/grana del
  rumore, tinta colorata): visibile, costoso, beneficio non dimostrato.
- Contrasto e nitidezza per monitor: la TV non risponde ai comandi DDC/CI (verificato:
  errore 203 su luminosita', contrasto e nitidezza).
- "Overlay negli screenshot": impossibile, l'overlay deve restare escluso dalla
  cattura.
- Scelta della scheda video, libreria Steam, Website/Feedback/Early access.
- Protezione di piu' monitor contemporaneamente (rimandata: l'utente ha una sola TV).

**Anti-obiettivi**
- Un controllo che non fa niente, o numeri inventati, nell'app reale.
- Luminosita', bordi scuriti o modalita' assente che alterano l'immagine quando sono
  spenti: con tutto spento il pixel resta identico bit per bit (come oggi).
- La modalita' assente che scurisce un video guardato in finestra.
- Lampo bianco all'apertura, richieste di rete, costi a finestra chiusa, thread UI
  bloccato (tutti gia' anti-obiettivi del progetto precedente, restano).

## 3. Vincoli

- Restano tutti i vincoli esistenti: C++20, CMake, MSVC, git locale, test senza
  framework, `build.ps1`, WebView2 come unica dipendenza esterna, front-end
  HTML/CSS/JS **senza librerie**, contenuti solo locali (`oledguard.local`), CSP
  attuale (`default-src 'self'`).
- Caratteri solo di sistema: **Segoe UI Variable** (testo) con ripiego su Segoe UI,
  **Cascadia Mono** (numeri e didascalie tecniche) con ripiego su Consolas. I caratteri
  del riferimento richiederebbero download.
- La pagina e' contenuto SDR su un desktop HDR; resta l'attenuazione al 30% dopo 60 s
  senza interazione.
- Le funzioni dell'overlay (luminosita', bordi, assente) agiscono solo mentre l'overlay
  e' a schermo: durante una pausa (gioco, video a tutto schermo, sessione bloccata)
  l'overlay e' spento e lo schermo e' quello normale. La pagina lo dice.

## 4. Approcci considerati

- **A. Prima il guscio, poi una funzione alla volta (scelto dall'utente).** Il guscio
  completo arriva subito con le funzioni di oggi; ogni progetto successivo porta la sua
  funzione insieme alla sua pagina. Nuovo aspetto subito, app sempre usabile, niente
  controlli finti; alcune pagine nascono parziali.
- **B. Tutte le pagine con dati finti, poi il motore.** Aspetto approvabile tutto in una
  volta, ma l'app reale avrebbe per molto tempo controlli finti e le pagine finte
  andrebbero riscritte. Scartato.
- **C. Prima il motore, l'aspetto alla fine.** Nessun risultato visibile per molto
  tempo, contrario a cio' che interessa all'utente. Scartato.

Per il front-end: si riscrive `src/ui` mantenendo WebView2 e JavaScript senza librerie
(nessuna alternativa ha senso con i vincoli attuali).

## 5. Design

### 5.1 Aspetto

Gettoni CSS su `:root` (valori indicativi, presi dal riferimento):

| Gettone | Valore | Uso |
|---|---|---|
| `--bg` | `#000000` | fondo della finestra (nero pieno, scelta per l'OLED) |
| `--panel` | `#0c1316` | pannelli |
| `--panel-2` | `#111b1f` | righe evidenziate, campi, segmenti |
| `--line` | `#1b282c` | bordi sottili |
| `--text` | `#e7eef0` | testo |
| `--muted` | `#8b9a9f` | descrizioni |
| `--faint` | `#56666b` | etichette di gruppo in maiuscolo |
| `--accent` | `#1fc7a9` (verde acqua) | interruttori, selezioni, stato attivo |
| `--warn` | `#f2a641` | ambra per avvisi e intensita' |
| `--info` | `#7c86ff` | blu/indaco secondario |
| `--danger` | `#ff5d5d` | elimina, errori |

- Pannelli con angoli arrotondati (~14 px), bordo sottile, intestazione in maiuscolo
  spaziato (`INTENSITA'`, `AZIONI RAPIDE`...), righe "titolo + descrizione a sinistra,
  controllo a destra" come in Settings del riferimento.
- Controlli: interruttori a pillola, segmenti (gruppo di pulsanti con quello scelto
  pieno d'accento), cursori con valore in Cascadia Mono a destra, schede-scelta (card
  con titolo e sottotitolo, bordo d'accento se scelte), etichette a pillola (`PRIMARY`,
  `HDR`, `DP`, `SEMPRE PAUSA`...).
- **Colore d'accento**: sei scelte come nel riferimento (verde acqua, indaco, corallo,
  verde, ambra, viola); sotto ciascuna il rapporto di contrasto **calcolato davvero**
  rispetto a `--panel`. Cambia solo `--accent` (e la sua versione trasparente).
- **Dimensione del testo**: Piccolo / Normale / Grande / XL = 13 / 14 / 15,5 / 17 px
  sulla radice; tutto il resto in `rem`.
- Animazioni brevi (150-200 ms) su hover e cambi di pagina; nessuna animazione continua
  a finestra ferma, salvo i numeri che cambiano davvero (conto alla rovescia).

### 5.2 Guscio

```
┌───────────────┬──────────────────────────────────────────────────────────┐
│ [scudo] OLED  │ [⏸] ● Protetto   Sessione · 47m 32s     [📊] [⚙]  — □ ✕  │
│ Guard  v1.0   ├──────────────────────────────────────────────────────────┤
│ PROTEGGI      │                                                          │
│  Panoramica   │                   pagina scelta                          │
│  Overlay      │              (scorre se non entra)                       │
│  Monitor      │                                                          │
│  Statistiche  │                                                          │
│ CONFIGURA     │                                                          │
│  Profili app  │                                                          │
│  Tasti rapidi │                                                          │
│  Impostazioni ├──────────────────────────────────────────────────────────┤
│  Windows OLED │ ● OGGI                               Apri statistiche >  │
│ AIUTO         │ [Sessione] [Protetto oggi] [Totale] [Uniformita'] [Pause]│
│  Avanzate     │                                                          │
│  Informazioni │                                                          │
│ ───────────── │                                                          │
│ Novita'  v1.0 │                                                          │
└───────────────┴──────────────────────────────────────────────────────────┘
```

- **Barra laterale** (larghezza ~240 px): logo a scudo disegnato in SVG proprio
  (nessun marchio altrui), nome, versione; gruppi PROTEGGI / CONFIGURA / AIUTO con
  icone SVG in linea; voce scelta con fondo `--panel-2` e barretta d'accento a
  sinistra. In fondo: "Novita'" (porta a Informazioni) e la versione.
- **Barra in alto** (trascinabile, `app-region: drag` come oggi): pulsante pausa
  manuale, pallino e testo di stato (`Protetto`, `In pausa · <motivo>`, `Prova in
  corso`, `Assente`), "Sessione · <tempo da quando l'app e' partita>", a destra icone
  Statistiche e Impostazioni, poi riduci/ingrandisci/chiudi. Le pillole Autopilot e
  "profilo attivo" del riferimento arrivano con i progetti 4-5.
- **Striscia in basso** (fissa, ~110 px): cinque riquadri con numeri gia' disponibili:
  Sessione, Protetto oggi, Totale, Uniformita', Pause oggi; link "Apri statistiche".
  Nel progetto 2 i primi tre riquadri diventano i grafici dal vivo del riferimento.
- **Finestra**: dimensione iniziale 1280×800 DIP, minima 1000×640 (oggi 1100×720 e
  900×600). Sotto ~1200 DIP di larghezza le pagine a due colonne vanno su una colonna.
  Posizione e dimensione ricordate come oggi.
- Navigazione da tastiera di base: Tab fra i controlli, Ctrl+1...9 fra le pagine
  (scelta del builder se aggiungerla).

### 5.3 Pagine

Ogni riga sotto indica da dove vengono i dati; "nuovo" = funzione nuova di questo
progetto.

**Panoramica** (Dashboard)
- Pannello **Protezione**: radar delle posizioni dello spostamento (quello di oggi,
  ridisegnato), offset attuale, conto alla rovescia della prossima mossa, app in primo
  piano, latenza aggiunta.
- Pannello **Zone oscurate ora**: miniatura della mappa dell'oscuramento (oggi nella
  scheda Oscuramento), % di zone oscurate, stato (attivo / in attesa / sospeso da
  `app.exe` / spento).
- **Azioni rapide** (riquadri come nel riferimento): Pausa (interruttore), Prova lo
  spostamento (azione), Evidenzia zone oscurate 15 s (azione), Oscuramento
  (interruttore `dim_enabled`), Bordi scuriti (interruttore, nuovo). Il pulsante
  "Personalizza" del riferimento non c'e' in questo progetto.
- Colonna destra **Attivita' recente** (nuovo): ultimi eventi dall'avvio, piu' recente
  in alto, con ora e icona; "Cancella" svuota la lista.

**Overlay**
- Pannello **Spostamento dei pixel**: ampiezza 1/2/3 (segmenti), posizioni usate
  (miniatura), sfasamento medio dei clic, sosta e transizione (cursori a doppia
  maniglia di oggi), "Prova ora".
- Pannello **Oscuramento delle zone ferme**: tutte le regolazioni della scheda di oggi
  (attivo, intensita', inizia dopo, discesa, cosa si scurisce, quando torna normale la
  finestra, finestre ferme) con le stesse regole di visibilita'. L'elenco "Sospendi con
  queste app" si sposta in Profili app; qui resta un link "Gestisci le app escluse".
- Pannello **Bordi scuriti** (nuovo): interruttore, anteprima a 16:9 con quattro
  maniglie trascinabili (estensione per lato 0-40% del lato dello schermo, valore
  mostrato accanto alla maniglia), cursore verticale "Intensita'" 0-80%, "Ripristina
  tutti i lati". Le modifiche si applicano mentre si trascina (con limite di frequenza,
  vedi 5.6).

**Monitor** (nuovo)
- Testata: "Protezione su **<nome>**" e numero di schermi collegati.
- Una card per schermo: nome amichevole, risoluzione, frequenza, etichette `PRIMARY`,
  `HDR`/`SDR`, tipo di collegamento (`HDMI`, `DP`, `Interno`... da
  `DISPLAYCONFIG_VIDEO_OUTPUT_TECHNOLOGY`), miniatura con la risoluzione.
- Scelta dello schermo protetto: un solo interruttore acceso alla volta, piu' la voce
  "Automatico" (valore `auto` di oggi). Cambia `config.monitor` con la logica gia'
  esistente.
- Sulla card dello schermo protetto: cursore **Luminosita'** 20-100% (nuovo) con la
  nota "funziona mentre la protezione e' attiva". Niente contrasto/nitidezza; una riga
  spiega che la TV non accetta quei comandi dal PC.

**Statistiche** (versione del progetto 1)
- Quattro riquadri in alto: Protetto in totale, Protetto oggi, Uniformita', Pause oggi.
- Mappa delle posizioni dello spostamento (7×7, quella di oggi, grande).
- Pause di oggi per motivo (barre orizzontali).
- I pannelli del riferimento (mappa di usura del pannello, grafico nel tempo, app piu'
  usate, usura per contenuto) arrivano nel progetto 2: in questo progetto non compaiono.

**Profili app** (versione del progetto 1)
- Colonna sinistra: elenco di tutte le app note (storico + denylist + allowlist +
  `dim_exclude` + nuovo campo `app_list`), con ricerca. `app_list` (scelta del
  progetto) tiene le app aggiunte a mano: senza di esso un'app aggiunta con regola
  *Automatico* e mai vista nello storico sparirebbe dalla lista, perche' *Automatico*
  significa "in nessuna lista". Un'app resta in elenco finche' l'utente non preme
  "Elimina". Nel progetto 4 `app_list` diventa l'elenco dei profili. Ogni riga: avatar con l'iniziale (colore ricavato dal
  nome), nome dell'eseguibile, "ultima volta" e conteggio dallo storico, etichette
  della regola (`SEMPRE PAUSA` / `AUTOMATICO` / `MAI PAUSA`, `NON OSCURARE`), elimina.
  Niente priorita' e niente interruttore per riga in questo progetto (servono quando
  ci saranno i preset).
- Sotto: le due pause automatiche globali di oggi (giochi DirectX/presentazioni;
  qualsiasi finestra a tutto schermo).
- Colonna destra, **Modifica**: tre card-scelta "Quando questa app e' in primo piano":
  *Metti in pausa* (always), *Automatico* (auto), *Non mettere mai in pausa* (never);
  interruttore "Non oscurare mentre e' in primo piano" (`dim_exclude`); spiegazione
  della scelta in una frase; "Elimina" (toglie la regola e la voce dello storico).
- **Aggiungi app** (l'app entra in `app_list` con regola *Automatico* e si apre nel
  pannello di modifica): campo per il nome, "App aperte" (elenco di oggi), "Sfoglia..."
  (nuovo: finestra di Windows per scegliere un `.exe`, si usa solo il nome del file).

**Tasti rapidi**
- Le tre combinazioni di oggi (pausa/riprendi, spegni schermo, uscita di emergenza)
  con registrazione premendo i tasti e messaggi di errore di oggi.

**Impostazioni**
- **Avvio**: avvio automatico con Windows.
- **Modalita' assente** (nuovo): interruttore, "dopo" 1-60 min (default 10),
  "scurisci fino a" 10-80% di luminosita' (default 30%). Vedi 5.5.
- **Aspetto**: colore d'accento, dimensione del testo.
- **Diagnostica**: livello del log (`info`/`debug`...), "Apri cartella dei log"
  (nuovo), percorsi di `config.json` e dei log mostrati in Cascadia Mono.

**Windows per OLED**
- La pagina di oggi nel nuovo stile: punteggio in testata con "Applica tutti i
  consigli" e "Ripristina come prima", una riga per impostazione (stato attuale,
  consigliato, applica), "Spegni schermo".

**Avanzate**
- I parametri che oggi stanno solo in `config.json`, ognuno con descrizione e nome del
  campo in Cascadia Mono come nel riferimento: soglia di luminosita'
  (`dim_bright_threshold_pct`), quota di movimento (`dim_motion_fraction_pct`), ritorno
  normale (`dim_recover_ms`), ritardo della pausa (`pause_enter_delay_ms`), ritardo
  della ripresa (`resume_delay_ms`), tearing (`allow_tearing`). Pulsante "Ripristina i
  valori predefiniti" per questi soli campi.

**Informazioni**
- Nome, versione, cosa fa l'app in tre righe; **Novita'** (elenco scritto a mano in un
  file della pagina, aggiornato a ogni progetto); licenze (WebView2); percorsi dei file.

### 5.4 Luminosita' e bordi scuriti nello shader

Nuovi campi in `ShiftConstants` (il buffer passa da 32 a 64 byte, allineamento a 16):
`brightness` (moltiplicatore finale 0-1, gia' combinato con la modalita' assente),
`vignette_strength` (0-0,8), `vignette_spread` (sinistra, destra, alto, basso, frazioni
del lato 0-0,4) e un nuovo bit `kShiftFlagShade` acceso quando almeno uno dei due non
e' neutro.

Nel pixel shader, in luce lineare come l'oscuramento:
```
fattore = brightness × (1 − strength × vignetta(pos))
vignetta(pos) = max sui quattro lati di (1 − smoothstep(0, spread_lato, distanza dal lato))
```
(`max` per gli angoli: niente doppio scurimento; spread 0 = lato spento.) Il fattore
si applica insieme a `(1 − livello)` dell'oscuramento. La posizione e' quella dello
schermo (non dell'immagine spostata), cosi' i bordi restano fermi. Con
`kShiftFlagShade` spento e oscuramento spento il pixel esce identico bit per bit,
come oggi.

Il calcolo del fattore vive anche in una funzione pura in `src/core` (nuovo file
`shade_model.*`), usata dai test CPU e confrontata con lo shader nei test GPU (WARP),
come oggi per `dim_model`.

Ridisegni: il fattore e' statico, quindi nessun ridisegno in piu', salvo quando cambia
(nuovi valori dalla config o rampa della modalita' assente): il thread di rendering
ridisegna al cambio, e durante una rampa al massimo a 20 Hz.

### 5.5 Modalita' assente

Logica pura in `src/core/away_model.*` (testata):
- Ingressi a ogni battito del thread UI (1 s): millisecondi dall'ultimo input
  (`GetLastInputInfo`), se lo schermo si sta muovendo, se l'overlay e' a schermo.
- **Assente** quando input fermo da almeno `away_after_min` **e** nessun movimento
  continuo nell'ultimo minuto. "Movimento continuo" = il desktop produce nuovi frame
  in modo regolare (un video in finestra lo fa, un desktop fermo con il cursore che
  lampeggia no). La misura la sceglie il builder sui dati di Desktop Duplication gia'
  disponibili al thread di rendering (frame nuovi al secondo e/o area cambiata); deve
  passare il caso "video in finestra per 30 minuti senza toccare niente = mai
  assente".
- Uscita: rampa di discesa di 10 s dal valore pieno a `away_brightness_pct`; al primo
  input il fattore torna a 1 **subito** (un solo ridisegno).
- La luminosita' finale e' `brightness_pct × fattore assente`.
- Stato "Assente" visibile nella barra in alto e come evento nell'Attivita' recente.
- Spenta di default.

### 5.6 Configurazione

Nuovi campi in `Config` (validati come gli altri in `ConfigFromJson`, con avvisi in
italiano e valore di default se fuori intervallo):

| Campo | Default | Intervallo |
|---|---|---|
| `brightness_pct` | 100 | 20-100 |
| `vignette_enabled` | false | |
| `vignette_strength_pct` | 50 | 0-80 |
| `vignette_left_pct`, `_right_pct`, `_top_pct`, `_bottom_pct` | 8, 8, 0, 12 | 0-40 |
| `away_enabled` | false | |
| `away_after_min` | 10 | 1-60 |
| `away_brightness_pct` | 30 | 10-80 |
| `ui_accent` | `"teal"` | teal, indigo, coral, green, amber, violet |
| `ui_text_size` | `"normal"` | small, normal, large, xl |
| `app_list` | `[]` | nomi .exe in minuscolo, max 200, ognuno max 260 caratteri |

I cursori della pagina (luminosita', bordi) mandano `setConfig` al massimo ogni
100 ms mentre si trascina, e sempre all'ultimo valore; il salvataggio su disco segue
le regole gia' esistenti della config.

### 5.7 Protocollo

App → pagina (nuovi messaggi in `ui_protocol`):
- `monitors`: elenco con `device`, `name`, `width`, `height`, `refresh_hz`, `hdr`,
  `primary`, `connection`, `protected`; inviato a `ready` e quando cambia la
  configurazione degli schermi.
- `activity`: lista completa degli eventi (a `ready`) e poi un evento alla volta
  (`{type:"activity", add:{t, kind, text}}`).
- `state` guadagna `away` (bool) e `session_ms`.
- `about` (a `ready`): `version` e percorsi di config e log.

Pagina → app (nuovi comandi, validati):
- `openLogFolder` — apre la cartella dei log in Esplora file.
- `browseExe` — apre la finestra "Apri" di Windows filtrata su `*.exe`; la risposta e'
  `{type:"browsed", exe:"nome.exe"}` oppure niente se annullata. La finestra e' modale
  sul thread UI: il ciclo modale continua a consegnare `WM_TIMER`, quindi il watchdog
  continua a battere (da verificare in prova).
- `setMonitor {device}` — usa la logica esistente (anche `"auto"`).
- `clearActivity`.
- `setDimExclude {exe, on}` — aggiunge/toglie l'exe da `dim_exclude`.

### 5.8 Attivita' recente

`src/core/activity_log.*` (puro, testato): ultimi 100 eventi in memoria (si perdono
alla chiusura dell'app: scelta voluta, e' "attivita' recente"). Tipi: pausa (con motivo
e app), ripresa, pausa manuale, prova dello spostamento, modalita' assente
entrata/uscita, schermo protetto cambiato, impostazioni di Windows applicate o
ripristinate, config ricaricata con avvisi. Eventi uguali entro 5 s si fondono in uno
(niente raffiche durante un'isteresi).

### 5.9 Versione

Scelta del progetto: un solo valore in `CMakeLists.txt` (`project(... VERSION 1.0.0)`),
passato al codice come definizione di compilazione e inviato alla pagina nel messaggio
`about`. Questo progetto porta la 1.0.0; ogni progetto successivo alza il numero di
mezzo (1.1 segnali e statistiche, 1.2 preset, 1.3 profili, 1.4 autopilota); le
correzioni alzano l'ultimo. "Novita'" elenca le voci per versione.

### 5.10 Modalita' anteprima

Come oggi: senza `window.chrome.webview` la pagina usa dati finti e animati, cosi'
ogni pagina si apre nel browser (`.claude/launch.json`, `ui-anteprima`) per lo sviluppo
e le verifiche visive. I dati finti riguardano solo l'anteprima, mai l'app reale.

## 6. Milestone

- **G1** Logica pura e test: nuovi campi di config (compreso `app_list`), versione da CMake, `shade_model`, `away_model`,
  `activity_log`, messaggi e comandi nuovi in `ui_protocol`.
- **G2** Shader: luminosita' e bordi scuriti, nuovo buffer delle costanti, test GPU
  contro `shade_model` (incluso il caso "tutto spento = identico bit per bit").
- **G3** App: elenco monitor con tipo di collegamento, `setMonitor`, rilevamento
  assente con misura del movimento, eventi di attivita', `openLogFolder`, `browseExe`,
  `setDimExclude`.
- **G4** Front-end: guscio, gettoni, componenti, tutte le pagine, anteprima
  aggiornata.
- **G5** Prova dal vivo sulla TV con checklist breve per l'utente (avvisandolo prima
  di ogni prova che mostra qualcosa a schermo), correzioni, aggiornamento di Novita'.

Ogni milestone: build e test verdi, commit.

## 7. Rischi

1. **Bordi e luminosita' in HDR**: il moltiplicatore in luce lineare scRGB e' corretto,
   ma il risultato percepito dipende dalla luminosita' SDR di Windows; da controllare a
   occhio sulla TV.
2. **Misura del movimento per l'assente**: un desktop con un elemento che si aggiorna
   sempre (orologio con secondi, GIF) potrebbe impedire l'assente; accettabile (meglio
   non scurire che scurire un video).
3. **Finestra "Apri" modale**: se il watchdog non ricevesse battiti durante la
   finestra, va spostata su un thread a parte con il proprio apartment COM.
4. **Pagine dense a 1000 DIP**: alcune pagine del riferimento sono a due colonne;
   sotto ~1200 DIP vanno su una colonna.
5. **Tipo di collegamento**: `QueryDisplayConfig` puo' riportare valori interni o
   sconosciuti con adattatori; in quel caso l'etichetta non compare.

## 8. Lasciato al builder

- Struttura di file del front-end (uno o piu' file JS/CSS), nomi interni, icone SVG.
- Misura precisa del "movimento continuo" per la modalita' assente (vincolata al caso
  di prova in 5.5).
- Testi esatti delle descrizioni (italiano, brevi, nello stile del riferimento).
- Se aggiungere Ctrl+1...9 per le pagine.
