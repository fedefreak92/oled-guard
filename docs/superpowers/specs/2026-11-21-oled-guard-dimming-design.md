# OLED Guard — oscuramento delle aree statiche (progetti 2 e 3)

Data: 2026-11-21
Stato: approvato
Progetti: 2 (oscuramento sulla GPU) e 3 (scheda "Oscuramento" nell'interfaccia).
Documenti di partenza: `2026-11-21-oled-guard-design.md` (base), `2026-11-21-oled-guard-ui-design.md` (interfaccia).

## 1. Intento

Lo spostamento di 1–3 px protegge i bordi degli elementi statici ma non la loro
superficie: una barra delle applicazioni bianca, le icone o un logo fermi per ore
consumano sempre gli stessi subpixel alla stessa luminanza. Le TV LG hanno una
funzione simile ("logo luminance"/ASBL), ma oscura anche documenti e pagine ferme
mentre l'utente li legge, e questo gli da' fastidio.

OLED Guard deve fare la stessa cosa in modo piu' mirato: riconoscere sulla GPU gli
**elementi piccoli** fermi e luminosi da molto tempo e abbassarne gradualmente la
luminanza, lasciando stare le grandi aree ferme (documenti, pagine), e tornare normale
appena il contenuto cambia. Una scheda dell'interfaccia mostra cosa viene oscurato e
permette di regolare tutto.

## 2. Obiettivi, non-obiettivi, anti-obiettivi

**Obiettivi**
- Rilevamento sulla GPU, a celle di 16×16 px del desktop catturato, di zone **ferme**
  (pixel identici) e **luminose**.
- Oscuramento solo degli elementi **piccoli** rispetto a cio' che li circonda (scelta
  dell'utente, opzione 2): barra, icone, loghi su sfondo che cambia o scuro.
- Opzione **grandi aree** (spenta di default): anche le grandi zone ferme si oscurano,
  ma molto piu' tardi e meno.
- Entrata lenta e invisibile, uscita rapida quando il contenuto cambia, bordi sfumati.
- Funziona in HDR e in SDR con la stessa soglia percepita.
- Intensita' massima e tempi regolabili dall'app (richiesta dell'utente).
- Elenco di app che sospendono l'oscuramento quando sono in primo piano.
- Scheda "Oscuramento": miniatura delle zone oscurate, dati, regolazioni, pulsante
  "Evidenzia sullo schermo".

**Non-obiettivi**
- Riconoscere "cosa" e' un elemento (forme, connessione fra pixel): conta solo la
  densita' di zone ferme intorno.
- Ricordare le zone ferme attraverso le pause: quando l'overlay viene distrutto (giochi,
  lock, UAC) la mappa riparte da zero.
- Oscurare cio' che sta sopra l'overlay (menu Start, Alt-Tab, barra quando e' salita
  sopra l'overlay dopo un clic): non passa dall'overlay.
- Ridurre la luminosita' di tutto lo schermo (quello e' compito di Windows/TV).

**Anti-obiettivi** (fallimento anche se "funziona")
- Documenti o pagine ferme oscurati mentre l'utente legge (con l'opzione grandi aree
  spenta).
- Oscuramento percepibile mentre avviene (scalini visibili, pulsazioni).
- Quadretti visibili: bordi netti fra celle oscurate e normali.
- Una zona che resta scura dopo che il contenuto e' cambiato.
- Colori alterati: l'oscuramento cambia solo la luminanza (stesso fattore sui tre
  canali in luce lineare).
- Documenti in tema scuro (testo chiaro su fondo scuro, editor, terminali) oscurati
  mentre l'utente legge.
- Passaggi visibili fra normale e oscurato quando cambia solo la classificazione (es. la
  barra che torna normale di colpo perche' la pagina sopra si e' fermata).
- Qualsiasi readback dei pixel del desktop sulla CPU; costo percepibile su latenza e
  frame del desktop.
- Lavoro della GPU a desktop fermo oltre due ridisegni al secondo (scelta dell'utente:
  accettati anche in modo continuo, vedi §5.3).

## 3. Vincoli

- Tutti i vincoli dei progetti precedenti (C++20, CMake + Ninja, MSVC, solo Windows
  SDK, WebView2 unica dipendenza, test senza framework, `build.ps1`, italiano/inglese,
  git locale, commit a ogni milestone).
- Pixel del desktop sempre sulla GPU. **Unica eccezione** (approvata dall'utente): la
  mappa dei livelli per cella (~32 KB, nessun pixel del desktop) viene letta dalla CPU
  una volta al secondo **solo mentre la scheda "Oscuramento" e' visibile**, per la
  miniatura.
- Shader compilati con fxc in build come quelli esistenti (cs_5_0 / ps_5_0).
- L'app non cambia impostazioni di Windows.

## 4. Approcci considerati

### 4.1 Cosa oscurare — scelta dell'utente: **2 (solo elementi piccoli) + opzione 3**
1. *Tutto cio' che e' fermo (come LG)*: semplice, ma oscura la pagina che si sta
   leggendo. Scartata: e' proprio il comportamento che da' fastidio.
2. **Solo elementi piccoli o isolati (scelta)**: una zona ferma si oscura solo se e'
   piccola rispetto a cio' che la circonda. Limite accettato: una barra bianca sotto una
   pagina bianca anch'essa ferma forma una grande area unica e non viene oscurata.

### 4.1b Cosa conta per misurare una "grande area" — scelta dell'utente: **B**
- *A. Solo celle ferme e luminose*: protegge le pagine bianche ma non quelle scure (un
  editor scuro ha poche celle luminose e verrebbe preso per tanti elementi piccoli, o
  pulserebbe). Scartata.
- **B. Celle ferme con contenuto (luminose oppure con dettagli, es. testo) (scelta)**:
  protegge anche i documenti scuri. Prezzo accettato: barra e icone accanto a una
  finestra ferma con contenuto non si oscurano finche' si legge; si oscurano quando
  intorno c'e' movimento o un fondo uniforme. Il passaggio da "piccolo" a "grande"
  riporta la luminosita' normale lentamente (30 s), cosi' non si nota.

### 4.1c Ridisegni a desktop fermo — scelta dell'utente: **A**
- **A. Regola solo a tempo (scelta)**: l'orologio della barra produce un frame al minuto,
  quindi in pratica i ridisegni a 2 Hz continuano sempre. Costo trascurabile, accettato.
- *B. Indicatore di 4 byte letto dalla GPU* per fermare i ridisegni a oscuramento
  assestato: scartato (seconda eccezione al "niente readback" non necessaria).
3. *Via di mezzo*: anche le grandi aree, ma dopo molto tempo e meno. Tenuta come
   **opzione spenta di default**.

### 4.2 Come riconoscere le zone ferme — scelta dell'utente: **1**
1. **Confronto sulla GPU cella per cella (scelto)**: compute shader che confronta il
   frame nuovo con il precedente. Esatto, costo minimo, nessuna copia in piu'.
2. *Dirty rect di Desktop Duplication*: metadati gia' pronti, ma conservativi: molte app
   ridisegnano zone identiche (la barra intera quando cambia l'orologio, il browser la
   pagina intera), quindi zone ferme mai riconosciute. Scartato.
3. *Ibrido* (dirty rect per sapere dove confrontare): solo ottimizzazione, irrilevante su
   una RTX 5080, piu' codice. Scartato.

### 4.3 Anteprima nella scheda — scelta dell'utente: **entrambe**
- Miniatura nella scheda tramite lettura della sola mappa dei livelli (eccezione al
  "niente readback", §3).
- "Evidenzia sullo schermo": l'overlay colora per 15 s le zone oscurate sul desktop
  vero, senza leggere nulla.

## 5. Design

### 5.1 Comportamento (valori di default, tutti in config; quelli segnati ✎ anche nella scheda)

| Parametro | Default | Intervallo | Note |
|---|---|---|---|
| ✎ Oscuramento attivo | si' | — | interruttore generale |
| ✎ Intensita' massima (elementi piccoli) | 40% | 10–80% | riduzione della luminanza |
| ✎ Inizia dopo (fermo da) | 3 min | 0,5–60 min | |
| ✎ Durata della discesa | 5 min | 0,5–60 min | da 0 all'intensita' massima |
| Ritorno quando il contenuto cambia | 0,5 s | 0,1–5 s | |
| ✎ Grandi aree | spenta | — | opzione 3 |
| ✎ Grandi aree: inizia dopo | 20 min | 5–240 min | discesa: stessa durata |
| ✎ Grandi aree: intensita' massima | 20% | 5–80% | |
| Soglia di luminosita' | 25% | 5–100% | % della luminanza del bianco SDR, in luce lineare (≈ meta' della luminosita' percepita) |
| Soglia "grande area" | 40% | 10–90% | densita' di celle ferme con contenuto oltre la quale una zona e' "grande"; solo in config, per la taratura |
| ✎ App che sospendono l'oscuramento | vuoto | — | nomi .exe |

Campi JSON (nuovi, validati come gli altri; valori fuori intervallo → default + avviso):
`dim_enabled`, `dim_strength_pct`, `dim_delay_s`, `dim_ramp_s`, `dim_recover_ms`,
`dim_large_enabled`, `dim_large_delay_s`, `dim_large_strength_pct`,
`dim_bright_threshold_pct`, `dim_large_fraction_pct`, `dim_exclude` (elenco .exe in minuscolo, come
`denylist`). Vincolo: `dim_large_delay_s` ≥ `dim_delay_s`, altrimenti entrambi ai default.

### 5.2 Algoritmo per cella

Griglia: celle di 16×16 px del desktop catturato (3840×2160 → 240×135 = 32 400 celle;
l'ultima riga/colonna puo' essere parziale). Tutto in coordinate del **desktop
catturato**, non dello schermo: la mappa si sposta insieme all'immagine.

Stato per cella (texture GPU):
- `last_change_s`: istante dell'ultimo cambiamento (secondi dall'inizio della sessione
  di render, float32: precisione < 10 ms anche dopo un giorno).
- `bright`: la cella e' luminosa nell'ultimo frame (vedi sotto).
- `content`: la cella ha contenuto nell'ultimo frame (luminosa, oppure con dettagli).
- `level`: oscuramento attuale, 0 = nessuno, 1 = totale (nella pratica ≤ 0,8).

**1. Rilevamento** (compute shader, solo quando arriva un frame nuovo, prima che la
cache venga sovrascritta): per ogni cella confronta i pixel del frame nuovo con quelli
del precedente (la cache). Se anche un solo pixel differisce → `last_change_s = now`.
Calcola `bright` = almeno 1/8 dei pixel della cella ha luminanza lineare
(Rec.709: 0,2126 R + 0,7152 G + 0,0722 B) ≥ soglia × bianco SDR. In SDR i valori BGRA
vengono prima linearizzati (sRGB → lineare) e il bianco vale 1. In HDR (scRGB) il
bianco vale `SDRWhiteLevel / 1000` (vedi §5.5).
Calcola `content` = `bright` **oppure** (max − min della luminanza lineare nella
cella) ≥ 10% del bianco SDR: il testo chiaro su fondo scuro e i dettagli contano, i
fondi uniformi (sfondo nero, interno vuoto di una finestra) no.

**2. Classificazione** (compute shader, a ogni ridisegno):
- `age = now - last_change_s`.
- Maschera degli elementi `M` = cella `bright` e `age ≥ dim_delay_s`.
- Maschera per la densita' `C` = cella `content` e `age ≥ dim_delay_s` (scelta B,
  §4.1b: conta anche il testo chiaro su fondo scuro).
- Densita' `D` = media di `C` su una finestra di 33×33 celle (~528 px) centrata sulla
  cella, contando solo le celle dentro lo schermo (ai bordi la finestra e' tagliata).
- `Large` = il massimo di `D` su una finestra di 33×33 celle attorno alla cella e' ≥
  `dim_large_fraction_pct` (default 0,4). Il massimo serve perche' gli angoli e i
  bordi di una grande area hanno densita' bassa e sarebbero altrimenti scambiati per
  elementi piccoli. La soglia di default va verificata con i test (una barra di ~5
  celle in fondo allo schermo con sopra movimento deve restare sotto; un editor scuro
  con codice deve restare sopra) e con la prova a schermo.
- Bersaglio:
  - cella `M` e non `Large` → `small_target = strength × clamp((age − delay) / ramp, 0, 1)`;
  - cella `M` e `Large`, opzione grandi aree attiva →
    `large_target = large_strength × clamp((age − large_delay) / ramp, 0, 1)`;
  - altrimenti 0.
  - Sospensione (oscuramento spento, app esclusa) → bersaglio 0 ovunque.
- Le finestre 33×33 si calcolano con filtri separabili (orizzontale + verticale) su
  texture di appoggio della dimensione della griglia: lavoro trascurabile.

**3. Livello** (stesso passaggio o successivo): il livello insegue il bersaglio con
velocita' limitate, usando `dt` dal ridisegno precedente:
- in discesa (verso piu' scuro): al massimo `strength / ramp` al secondo (mai piu'
  veloce della discesa prevista, anche se l'utente alza l'intensita');
- in risalita perche' la cella e' cambiata (`age < delay`): fino a 0 in `dim_recover_ms`;
- in risalita perche' la cella e' diventata parte di una grande area (contenuto
  invariato): lentamente, da `strength` a 0 in **30 s** (passaggio invisibile, §2);
- in risalita per sospensione, oscuramento spento o intensita' abbassata dall'utente:
  in **1 s** (risposta pronta a un comando).
- Se piu' casi valgono insieme vince la risalita piu' veloce.

**4. Applicazione** (pixel shader esistente `shift_ps.hlsl`, esteso): per il pixel
sorgente `src` (gia' spostato) legge il livello con **interpolazione bilineare** della
texture dei livelli (bordi sfumati su una cella), e moltiplica il colore:
- HDR (scRGB lineare): `rgb × (1 − level)`;
- SDR (BGRA sRGB): linearizza, moltiplica, ricodifica (oppure equivalente esatto);
  alfa invariata.
- Con `level` = 0 ovunque il risultato e' identico bit per bit a oggi (anti-obiettivo
  "colori alterati").
- Modalita' "Evidenzia": per 15 s le celle con `level > 0,01` vengono mescolate con
  l'ambra #ffb020 (alla luminanza del bianco SDR) in proporzione al livello.

Ripristino dei dati: alla creazione della sessione di render, a ogni ricreazione della
cache (cambio formato/dimensione) e dopo un `ACCESS_LOST` la mappa riparte da zero
(`last_change_s = now`, `level = 0`).

### 5.3 Quando ridisegnare

Oggi l'overlay ridisegna solo con un frame nuovo o un cambio di offset; un desktop
fermo non produce frame, ma la discesa e il ritorno devono avanzare. Regola (logica pura
in `src/core`, testata):
- Dopo un frame nuovo, per `dim_recover_ms` + 100 ms: un ridisegno per periodo di
  refresh (il ritorno di 0,5 s deve essere fluido).
- Poi, finche' puo' esserci una discesa in corso — cioe' fino a
  `ultimo_frame + max(delay, large_delay se attiva) + ramp + 3 s` — un ridisegno ogni
  **500 ms**. Passo per ridisegno con i default: 40% / 600 = 0,07% → invisibile.
- Dopo un cambio dei parametri, una sospensione/ripresa o l'avvio di "Evidenzia":
  stessa finestra di 500 ms ricalcolata da quel momento (per l'evidenziazione anche
  alla sua fine).
- Altrimenti nessun ridisegno. Nella pratica l'orologio della barra produce un frame
  al minuto e i ridisegni a 2 Hz continuano sempre: accettato dall'utente (§4.1c).
- Il timeout di `AcquireNextFrame` diventa il minimo fra prossimo passo di offset e
  prossimo ridisegno dell'oscuramento.

### 5.4 Integrazione nel codice

**`src/core` (logica pura, TDD)**
- `config.*`: i nuovi campi, validazione, serializzazione.
- `dim_model.*`: **modello di riferimento su CPU** dell'algoritmo §5.2 (rilevamento
  su maschere di input, densita', grandi aree, bersaglio, inseguimento del livello). Non
  usato a runtime: definisce il comportamento, e' coperto da test e fa da oracolo per i
  test degli shader.
- `dim_schedule.*`: la regola dei ridisegni (§5.3), con tempo iniettato.
- `dim_params.*`: conversione Config → parametri per gli shader (secondi, frazioni).
- `ui_protocol.*`: messaggio `dim` e comandi nuovi (§5.6).

**`src/app`**
- `shaders/dim_detect_cs.hlsl`, `dim_classify_cs.hlsl` (piu' passaggi/entry point se
  serve), `shift_ps.hlsl` esteso. Compilati con `add_shader`.
- `dimmer.*` (nuovo): possiede le texture della griglia, gli shader e il constant
  buffer; metodi `Reset`, `Detect(new_frame, previous_cache)`, `Update(now, params)`,
  e la lettura asincrona della mappa (staging, `Map` con `DO_NOT_WAIT` sul frame dopo:
  il thread di render non si ferma mai ad aspettare la GPU).
- `capture.*`: il confronto deve avvenire prima della `CopyResource` nella cache:
  `Acquire` espone il frame acquisito al chiamante (callback o due fasi). Se il frame di
  Desktop Duplication non e' leggibile come shader resource, si copia prima in una
  seconda texture e si alternano le due (costo: una texture in piu').
- `presenter.*`: il pixel shader riceve anche la texture dei livelli (SRV + sampler
  lineare clamp) e i parametri (formato SDR/HDR, bianco SDR, evidenziazione).
- `render_thread.*`: chiama il dimmer, applica la regola dei ridisegni, legge i
  parametri condivisi a ogni ciclo.
- `SharedDim` (accanto a `SharedShift`, protetto da mutex): parametri correnti, stato
  sospeso, fine dell'evidenziazione, richiesta della mappa per la UI, ultima mappa letta
  con i suoi riassunti. Scritto dal thread UI, letto dal render (e viceversa per la mappa).
- `monitors.*`: lettura del bianco SDR (§5.5).
- `app.*`: alimenta `SharedDim` da config, processo in primo piano (esclusioni) e bianco
  SDR; inoltra i comandi della scheda; invia il messaggio `dim` alla UI.

**Test degli shader** (scelta del builder, vedi §8): eseguibile di test separato
`oled-guard-gpu-tests` che crea un device **WARP** (sempre disponibile in Windows,
nessuna GPU necessaria), esegue gli shader su texture sintetiche (barra statica, area
grande, cambio di contenuto, SDR e HDR) e confronta i livelli letti con `dim_model`.
Registrato in CTest; `build.ps1` lo esegue.

### 5.5 HDR e bianco SDR
- Il bianco SDR si legge con `DisplayConfigGetDeviceInfo`
  (`DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL`) per il target del monitor
  scelto: `SDRWhiteLevel` / 1000 = valore scRGB del bianco (1,0 = 80 nit).
- Riletto all'avvio dell'overlay e ogni 5 s dal thread UI (l'utente puo' spostare il
  cursore "luminosita' contenuto SDR"); passato tramite `SharedDim`.
- Se la lettura fallisce: 1,0 in SDR, 2,5 (200 nit) in HDR, con avviso nel log.
- Contenuti HDR piu' luminosi del bianco SDR (immagini/video HDR fermi) contano come
  luminosi e si oscurano come il resto se piccoli.

### 5.6 Scheda "Oscuramento" (progetto 3)

Quinta scheda dopo "Giochi e app", stesso stile "Strumento di precisione" (nero pieno,
Cascadia Mono, accento ambra #ffb020, righe `.row`, `.toggle`, `.seg`, slider `range`
esistenti). Schema:

```
┌ OSCURAMENTO ─────────────────────────────────────────────────────────────┐
│ ┌────────── miniatura 16:9 ──────────┐  Stato          ATTIVO           │
│ │                                    │  Zone oscurate  3,2% dello schermo│
│ │        (celle in ambra,            │  Oscuramento    max 38%           │
│ │         piu' intense = piu' scure) │  Bianco SDR     240 nit (HDR)     │
│ │▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓│  [Evidenzia sullo schermo · 15 s] │
│ └────────────────────────────────────┘                                   │
│ ELEMENTI PICCOLI                        GRANDI AREE                       │
│ Oscuramento attivo            [ON]      Oscura anche le grandi aree [OFF] │
│ Intensita' massima   ──●──── 40%        Inizia dopo  ───●──── 20 min      │
│ Inizia dopo          ─●───── 3 min      Intensita'   ─●───── 20%          │
│ Durata della discesa ──●──── 5 min                                        │
│                                          SOSPENDI CON QUESTE APP          │
│                                          photoshop.exe            [x]     │
│                                          [nome .exe] [App aperte] [Agg.]  │
└──────────────────────────────────────────────────────────────────────────┘
```
- Stato: ATTIVO / SOSPESO · `app.exe` / SPENTO / IN PAUSA (motivo dello shift) /
  "IN ATTESA" nei primi minuti di una sessione.
- Miniatura: canvas con la griglia ridotta (al massimo 120×68 celle, prendendo il
  massimo dei livelli in ogni blocco 2×2), colore ambra con opacita' proporzionale al
  livello; contorno dello schermo. Aggiornata 1 volta al secondo.
- La lettura della mappa e' attiva solo mentre la scheda e' selezionata **e** la
  finestra e' visibile (comando `dimWatch {on}`); spenta alla chiusura della finestra.
- "Evidenzia sullo schermo": comando `dimHighlight`; il pulsante mostra il conto alla
  rovescia dei 15 s. Funziona anche se l'oscuramento e' sospeso? No: in quel caso il
  pulsante e' disattivato con il motivo.
- Regolazioni salvate con `setConfig` come le altre schede (debounce sugli slider).
- Esclusioni: stesso schema di "Giochi e app" (input .exe, elenco delle app aperte con
  `listProcesses`, rimozione), salvate in `dim_exclude`.
- Modalita' anteprima (browser senza WebView2): mappa finta animata (barra in basso e
  qualche icona).

Protocollo (in `ui_protocol`, testato):
- App → pagina `dim`: `{type:"dim", status, suspended_by, cols, rows, cells, dimmed_pct,
  max_level, sdr_white_nits, hdr, highlight_left_ms}`, dove `cells` e' una stringa di
  `cols×rows` caratteri `0`–`9`, `a`–`f` (livello quantizzato a 16 passi). Senza mappa
  disponibile `cells` e' vuota. Inviato 1 volta al secondo mentre `dimWatch` e' attivo.
- Pagina → app: `dimWatch {on}`, `dimHighlight {}`. Le regolazioni e le esclusioni
  passano da `setConfig`.

### 5.7 Sicurezza e robustezza
- Con l'oscuramento spento o sospeso, dopo il ritorno di 1 s, l'immagine e' identica a
  prima del progetto.
- Se la creazione delle risorse dell'oscuramento fallisce (shader, texture), l'overlay
  continua senza oscuramento e lo scrive nel log (lo spostamento resta prioritario).
- Watchdog invariato: nessuna attesa bloccante nuova sul thread di render (readback
  solo con `DO_NOT_WAIT`).
- Pausa, lock, giochi: invariati (l'overlay e la mappa vengono distrutti).

## 6. Milestone
- **G1** Logica pura + test: campi di config, `dim_model`, `dim_schedule`,
  `dim_params`, protocollo `dim`/`dimWatch`/`dimHighlight`.
- **G2** Pipeline GPU: shader, `dimmer`, modifica di cattura/presenter/render thread,
  bianco SDR, `SharedDim`, esclusioni; test WARP degli shader contro `dim_model`.
  Checklist visiva per l'utente (barra bianca che si scurisce, ritorno quando un'icona
  della barra si illumina sotto il mouse — il cursore da solo non conta, non e' nella
  cattura —, documento chiaro e scuro fermi non toccati, HDR/SDR).
- **G3** Evidenziazione sullo schermo e lettura asincrona della mappa.
- **G4** Scheda "Oscuramento" con modalita' anteprima, collegata a stato e config.

Ogni milestone: `.\build.ps1` verde, commit locale, checklist visiva breve. Revisione
finale indipendente alla fine.

## 7. Rischi
1. **Il frame acquisito potrebbe non essere leggibile come shader resource**: ripiego
   con una seconda texture (§5.4), +66 MB di VRAM in HDR 4K.
2. **Pixel che cambiano senza cambiare davvero** (dithering, cursore lampeggiante,
   secondi dell'orologio): la cella interessata non si oscura mai. Accettato: sono
   celle singole; le vicine si oscurano normalmente.
3. **Limite dell'opzione 2 + B**: barra e icone accanto a una finestra ferma con
   contenuto (chiara o scura) = grande area → non oscurate finche' la finestra non
   cambia; quando si ferma di nuovo la barra torna normale in 30 s e poi, quando
   intorno torna il movimento, rientra in discesa lenta. Accettato.
3b. **Il cursore non risveglia nulla**: non e' nella cattura; una zona torna normale
   solo se i suoi pixel cambiano (es. effetto hover dell'app sotto il mouse).
4. **Alone sfumato**: l'interpolazione bilineare porta un leggero oscuramento fino a
   ~8–16 px fuori dall'elemento. Accettato in cambio dei bordi invisibili.
5. **Ridisegni a 2 Hz** a desktop quasi fermo: un'operazione a schermo intero ogni
   500 ms, in pratica continua (orologio); trascurabile, accettato.
6. **Cambio del bianco SDR**: fino a 5 s di ritardo prima che la soglia si adegui.
7. **Celle parziali** ai bordi destro/inferiore: trattate come celle piu' piccole.

## 8. Lasciato al builder
- Struttura esatta degli shader (quanti passaggi, formati delle texture della
  griglia, thread group), purche' il risultato corrisponda a `dim_model`.
- Formula esatta della linearizzazione SDR (sRGB esatta o gamma 2,2) e dell'ambra
  dell'evidenziazione.
- Come `Acquire` espone il frame al confronto.
- Disposizione fine della scheda, animazioni, testi di aiuto.

### Scelte del builder gia' fatte (segnalate all'utente)
- Celle di 16 px, finestra di densita' 33×33 celle, soglia "grande area" 0,4 (in config).
- "Luminosa" = 1/8 dei pixel della cella sopra il 25% della luminanza del bianco SDR;
  "con contenuto" = luminosa oppure escursione di luminanza ≥ 10% del bianco SDR.
- Oscuramento **attivo di default** (e' la funzione richiesta).
- Ritorno di 30 s quando una zona diventa "grande", di 1 s per sospensione/spegnimento/
  intensita' abbassata.
- Ridisegni a 2 Hz durante la discesa.
- Test degli shader con WARP in un eseguibile di test separato.
- Miniatura ridotta al massimo a 120×68 celle.

## 9. Revisione dopo la prova reale (2026-11-21)

### 9.1 Cosa ha mostrato la prova sul desktop dell'utente
Misure con `--dim-diagnostics` (statistiche delle celle, mappa delle classi, istantanee
catturato/uscita confrontate pixel per pixel):
- L'oscuramento arriva a schermo (rapporto mediano 0,83 in sRGB = 40% lineare), ma sulle
  zone sbagliate: le icone del desktop (griglia fitta con etichette) risultavano "grande
  area", mentre le scritte sparse di un'interfaccia scura (la finestra di OLED Guard)
  risultavano "elementi piccoli" e venivano scurite.
- Il mouse da solo non azzera nulla (puntatore mosso 20 s: livelli saliti normalmente).
- Ogni pausa, anche Alt+Tab o la Visualizzazione attivita' (finestre explorer a tutto
  schermo), distruggeva l'overlay e con lui la mappa: tutto ripartiva da zero.

La regola della §5.2 (densita' di celle ferme con contenuto) non distingue un desktop
pieno di icone da una finestra scura con poco testo. Sostituita come segue.

### 9.2 Zone: scelta dell'utente "desktop/barra + loghi su video"
- Il thread UI calcola ogni 250 ms una **mappa delle zone** per cella dalle finestre
  visibili: **shell** (desktop e barra delle applicazioni) oppure **app** (coperta da una
  finestra di un'applicazione, compresa quella di OLED Guard). Escluse dal calcolo:
  finestre nascoste, ridotte a icona, "cloaked", click-through (`WS_EX_TRANSPARENT`) e le
  finestre a tutto schermo del selettore di Windows (Alt+Tab, Visualizzazione attivita').
  La barra delle applicazioni vince sulle finestre che le stanno sotto. Senza mappa (avvio)
  tutto conta come app (prudente).
- **Shell**: ogni cella ferma e luminosa e' un elemento da oscurare (intensita' degli
  elementi piccoli), senza criterio di densita': icone, etichette, sfondo chiaro, barra.
- **App**: una cella ferma e luminosa si oscura solo se **intorno c'e' movimento**: almeno
  il 25% delle celle nella finestra di 33×33 celle e' cambiato negli ultimi 10 s (es.
  logo sopra un video). Senza movimento e' una "grande area" (documenti, pagine,
  interfacce ferme, chiare o scure): non si tocca, salvo l'opzione grandi aree (§5.1).
- Il criterio "contenuto" (escursione di luminanza) e `dim_large_fraction_pct` spariscono;
  nuovo campo solo in config `dim_motion_fraction_pct` (10–90, default 25).
- Le regole dei ritorni (§5.2.3, con l'indicatore persistente "ritorno lento") restano.

### 9.3 Pause
- Le finestre del selettore di Windows (Alt+Tab, Visualizzazione attivita') non contano
  come "finestra a tutto schermo".
- **La mappa sopravvive alle pause** (e ai riavvii della cattura): prima di distruggere il
  dimmer si leggono, una volta, l'eta' di ogni cella e una firma (hash) dei suoi pixel. Al
  ritorno, al primo frame, una cella con la stessa firma riprende la sua eta' (il tempo
  di pausa non conta, il pannello non la mostrava); le altre ripartono da zero. I livelli
  ripartono da 0 e risalgono con la discesa lenta normale (nessuno scatto visibile).
  Vale solo se dimensioni e formato della cattura sono gli stessi.
- Eccezione al "niente readback" per questo salvataggio: due griglie (~260 KB) lette una
  volta per pausa, mai pixel del desktop.

### 9.4 Test
Scene realistiche nel modello e nei test WARP: griglia di icone con etichette sul desktop,
interfaccia scura con testo sparso in una finestra, logo fermo sopra un video, logo in
una finestra ferma (con e senza grandi aree), barra sotto una finestra, ripresa dopo una
pausa con contenuto uguale e diverso. Verifica dal vivo con `--dim-diagnostics`.

### 9.5 Aggiunte dopo la seconda prova reale
- **Deviazioni brevi**: una cella che cambia e torna identica (stessa firma) entro 15 s
  riprende l'eta' che aveva: icona evidenziata dal passaggio del mouse, Visualizzazione
  attivita', menu e tooltip. Mentre e' diversa torna normale come sempre; al ritorno si
  riscurisce con la discesa lenta. Era la causa di "si scurisce solo a mouse fermo".
- **Overlay invisibili**: le finestre layered con opacita' 0 (es. l'overlay di NVIDIA,
  classe `CEF-OSC-WIDGET`, a tutto schermo) non contano come app.
- Verifica dal vivo (desktop mostrato, ritardi 30 s/30 s): 6.386 celle luminose del desktop
  scurite al 40% (rapporto 0,80 in sRGB fra uscita e cattura), icona sotto il mouse tornata
  normale e poi riscurita, eta' conservata attraverso Visualizzazione attivita' e pausa.

### 9.6 Finestre come il desktop, con riattivazione (richiesta dell'utente)
- Nuova scelta `dim_window_mode`: **"desktop"** (default) — nelle finestre si scurisce
  tutto cio' che e' fermo e luminoso come sul desktop; **"video"** — il comportamento della
  §9.2 (solo elementi fermi con movimento intorno, piu' l'opzione "finestre ferme").
- **Riattivazione** `dim_activate`: la finestra "attiva" torna normale in 0,5 s e finche'
  resta attiva non si scurisce; quando smette di esserlo il conteggio riparte da zero
  (scelta A dell'utente). Valori: **"hover"** (finestra sotto il mouse da almeno 0,3 s),
  **"click"** (finestra in primo piano, anche via Alt+Tab), **"both"** (default), **"never"**.
  Desktop e barra non si riattivano mai.
- **Tempi piu' corti** (l'utente: "conteggi troppo lunghi rendono l'app inutile"):
  default `dim_delay_s` 60 (era 180) e `dim_ramp_s` 120 (era 300).
- Realizzazione: il thread UI ricava il numero di zona della finestra sotto il mouse e di
  quella in primo piano dalla stessa enumerazione della mappa delle zone; sulla GPU un
  passaggio "CSActivate" a ogni aggiornamento rimette "cambiate adesso" le celle di quelle
  zone (eta', riferimento e deviazione), quindi il ritorno rapido e il conteggio che
  riparte vengono dalle regole esistenti.
