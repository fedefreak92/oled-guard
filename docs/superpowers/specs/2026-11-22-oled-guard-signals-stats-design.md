# OLED Guard — segnali dal vivo e statistiche (progetto 2 di 5)

Data: 2026-11-22
Stato: in revisione
Progetto precedente: `2026-11-21-oled-guard-ui-v2-design.md` (nuova interfaccia, v1.0.0).
Riferimento visivo: le schermate di riferimento, non incluse nel repository (striscia "Live signals",
pagina Statistics).

## 1. Intento

L'utente vuole "capire di tutto e di più" guardando OLED Guard: se la protezione
funziona, quali zone della TV rischiano di piu', come usa il PC. Questo progetto porta i
segnali dal vivo nella striscia in basso, la pagina Statistiche completa del riferimento
e l'avviso nella barra delle applicazioni, con **definizioni oneste**: ogni numero ha un
significato dichiarato e misurato davvero.

## 2. Obiettivi, non-obiettivi, anti-obiettivi

**Obiettivi**
- Striscia in basso con tre grafici degli ultimi 60 s (Rischio, Movimento,
  Luminosita') e un riquadro riassuntivo, come nel riferimento.
- Pagina Statistiche: quattro riquadri (Protetto in totale, Sessioni, Usura evitata,
  Giorni di fila), mappa di usura del pannello, grafico del rischio 6 h / 24 h / 7 g,
  app piu' usate (7 g), usura per contenuto (7 g); restano sotto le statistiche del
  progetto 1 (posizioni dello spostamento, pause di oggi).
- Avviso nella barra delle applicazioni quando il rischio resta alto troppo a lungo.
- Impostazioni: pannelli "Notifiche" e "Privacy" (statistiche per app, cancella lo
  storico).

**Non-obiettivi**
- Misure durante le pause (giochi, video a tutto schermo, schermo bloccato): l'overlay
  e la cattura sono spenti. La pagina lo dice ("tempo in pausa non misurato").
- Piu' monitor protetti insieme (rimandato dal progetto 1).
- Livelli, preset, profili, autopilota (progetti 3-5).

**Anti-obiettivi**
- Leggere pixel del desktop dalla CPU: si leggono solo riassunti per zona (pochi KB).
- Attese bloccanti sulla GPU (watchdog): letture solo asincrone (`DO_NOT_WAIT`).
- Numeri inventati o senza definizione.
- Dati che lasciano il PC: tutto resta in `%APPDATA%\oled-guard`.
- Costo a overlay spento: senza cattura non si calcola nulla.

## 3. Vincoli

- Tutti i vincoli del progetto 1 (C++20, MSVC, CMake, test senza framework, WebView2,
  JS senza librerie, CSP `default-src 'self'`, testi in italiano).
- Shader `cs_5_0`: al massimo 8 UAV per passaggio (D3D11.0).
- I test GPU girano su WARP contro modelli puri in `src/core`, come per l'oscuramento.
- Versione 1.1.0 (regola del progetto 1: ogni progetto alza il numero di mezzo).

## 4. Approcci considerati

- **A. Riassunto sulla GPU (scelto, l'utente ha lasciato la scelta).** Il passaggio di
  rilevamento gia' esistente calcola anche la luminanza media per cella; un passaggio
  nuovo riduce le celle a 32×18 zone piu' pochi totali in un buffer di interi; l'app lo
  legge una volta al secondo in modo asincrono. Costo trascurabile, dati precisi; lavoro
  sugli shader coperto da test WARP.
- **B. Leggere tutte le celle ogni secondo** (~32 400 celle, ~0,5 MB/s) e calcolare
  sulla CPU: shader piu' semplici ma cento volte piu' dati e CPU sempre al lavoro.
- **C. Solo segnali di Windows** (frame nuovi, aree cambiate): niente luminosita' ne'
  mappa. Non soddisfa la richiesta.

## 5. Definizioni (approvate dall'utente)

Celle: le celle 16×16 px dell'oscuramento (240×135 su 3840×2160). "Bianco" = bianco SDR
(in HDR il valore di Windows, come per l'oscuramento). Luminanza in luce lineare, gia'
calcolata da `Luma()` nel rilevamento.

| Numero | Definizione |
|---|---|
| **Rischio** | % dell'area dello schermo con celle *luminose* (flag dell'oscuramento, soglia `dim_bright_threshold_pct`) e ferme da almeno 60 s, misurata **prima** dell'oscuramento. |
| **Movimento** | % dell'area con celle cambiate negli ultimi 10 s. Didascalia: < 2% "interfaccia ferma", 2-15% "lavoro", > 15% "video o animazioni". |
| **Luminosita'** | Luminanza media dell'immagine (APL) in % del bianco, prima dell'oscuramento (puo' superare 100% in HDR). |
| **Mappa di usura** | 32×18 zone; per zona si somma `luminanza arrivata al pannello × tempo`, cioe' dopo oscuramento, luminosita' software e bordi. Unita': ore a bianco pieno. Mostrata relativa (meno → piu'). |
| **Zona peggiore** | La zona con il valore piu' alto, descritta a parole (griglia 3×3: "in alto a sinistra"... "al centro"). |
| **Concentrazione** | valore della zona peggiore / media delle zone. |
| **Zone oltre il doppio** | numero di zone con valore > 2 × media, e % del pannello. |
| **Usura evitata** | `luce tolta / luce prima` sulle celle ferme da almeno 60 s, in %, oggi e in totale. Luce tolta = luminanza × (1 − (1 − livello) × fattore luminosita'/bordi). |
| **Rischio non protetto** | Come il Rischio, ma solo le celle il cui livello di oscuramento e' ancora ≤ 0,05 (non scurite). Serve all'avviso (decisione dell'utente: avvisare solo per le zone che la protezione non sta gia' scurendo); non compare come grafico. |
| **Grafico del rischio** | Media del Rischio ogni 5 minuti; 7 giorni conservati. Intervalli senza misure (pausa, app chiusa) = vuoti, non zero. |
| **App piu' usate (7 g)** | Secondi in primo piano per eseguibile, divisi in "protetto" (overlay a schermo) e "in pausa". |
| **Usura per contenuto (7 g)** | Secondi per categoria: Interfaccia ferma e lavoro (Movimento ≤ 15%), Video e animazioni (> 15%), Giochi e schermo intero (pausa per `NotificationState`, `FullscreenWindow`, `Denylisted`), Assente (modalita' assente), Pausa manuale. Le altre pause (blocco, sessione inattiva, errori) non contano. |
| **Sessioni** | Ogni avvio dell'app; totale e "questa settimana" (lunedi'-domenica). |
| **Giorni di fila** | Giorni consecutivi (fino a oggi o ieri) con almeno 30 minuti di overlay a schermo; piu' il record. |

## 6. Design

### 6.1 GPU (render thread)

**Rilevamento (`CSDetect`)**: nuova uscita `luma_out` (`RWTexture2D<float>`, u6) con la
luminanza media della cella (somma in groupshared a virgola fissa, `min(luma, 64) × 1024`
per pixel, divisa per il numero di pixel). Le celle parziali ai bordi usano i pixel
reali.

Seconda uscita nuova `content_change_out` (`RWTexture2D<float>`, u7, l'ultimo UAV
disponibile): l'istante dell'ultimo cambio **reale** dei pixel della cella (senza la regola delle
deviazioni brevi di `last_change`: servirebbero altre due griglie e gli UAV sono finiti),
che `CSActivate` non tocca.
Scelta del progetto: `last_change` viene riportato a "adesso" su tutte le celle della
finestra sotto il mouse o in primo piano (`dim_activate`), quindi usarlo renderebbe
falsi Rischio, Movimento, Usura per contenuto e Usura evitata (una pagina bianca ferma
che stai leggendo conterebbe come "in movimento"). Tutte le definizioni di "ferma" e
"cambiata" del §5 usano `content_change_out`.

**Riassunto (`CSSummary`, nuovo)**: un thread per cella, `numthreads(8,8,1)`. Ingressi:
flags, last_change, luma, livelli, costanti (now_s, colonne, righe, parametri di
luminosita'/bordi come `ShadeParams`). Uscita: `RWByteAddressBuffer` di interi a 32 bit,
azzerato prima di ogni passaggio (`ClearUnorderedAccessViewUint`), sommato con
`InterlockedAdd` a virgola fissa:
- per ognuna delle 576 zone: somma della luminanza arrivata al pannello (× 256) e
  numero di celle;
- totali: celle (area in pixel), area a rischio, area a rischio non protetta (livello
  ≤ 0,05), area in movimento, somma luminanza prima, luce prima e luce tolta sulle
  celle ferme.
Zona di una cella: `floor(cx × 32 / colonne)`, `floor(cy × 18 / righe)`. Il fattore
luminosita'/bordi si calcola al centro della cella con la formula di `shade_model`
(include il fattore della modalita' assente in corso). Le somme sono pesate per area in
pixel (celle parziali).

**Cadenza**: una volta al secondo, solo con overlay a schermo e almeno un frame: dispatch
di `CSSummary`, copia in un buffer staging (due staging alternati), lettura del
precedente con `D3D11_MAP_FLAG_DO_NOT_WAIT`; se non e' pronto si riprova al giro dopo. Il
risultato decodificato va in un nuovo `SharedSignals` (mutex) con l'istante di misura.
Nessun ridisegno in piu': il riassunto non tocca il disegno.

Il riassunto gira anche con l'oscuramento spento (il rilevamento gira comunque, il
livello e' 0).

### 6.2 Logica pura (`src/core`)

- `summary_model.*`: `ComputeSummary(celle..., parametri) -> FrameSummary` (oracolo dei
  test GPU, stessa virgola fissa) e `DecodeSummary(buffer) -> FrameSummary` con i valori
  in unita' reali (frazioni 0-1, luminanze).
- `signals.*`: anello di 60 campioni `{rischio, movimento, luminosita'}` al secondo,
  picco e media, didascalia del movimento.
- `history.*` (salvato in `history.json`):
  - `Timeline`: 2016 secchi da 5 minuti (7 giorni) con somma e numero di campioni del
    rischio; il secchio si sceglie dall'ora locale; buchi = vuoti.
  - `WearMap`: per ogni monitor (nome amichevole) 576 valori in ore a bianco pieno e data
    di inizio.
  - `DailyUsage`: per giorno (ultimi 8 giorni): secondi protetti, secondi per categoria
    di contenuto, per app (max 50 per giorno, le piu' usate) secondi protetti/in pausa,
    luce prima/tolta sulle celle ferme.
  - `Totals`: luce prima/tolta totale, sessioni totali, date delle sessioni dell'ultima
    settimana, record dei giorni di fila, giorni con >= 30 min (ultimi 400).
  - Interrogazioni: app piu' usate (7 g), quote per contenuto (7 g), giorni di fila e
    record, usura evitata oggi/totale, zona peggiore/concentrazione/zone oltre il doppio.
- `alert_model.*`: soglie Bassa 10% / Media 20% / Alta 35%; il conteggio parte quando il
  **Rischio non protetto** supera la soglia e si azzera dopo 2 minuti consecutivi sotto; avviso quando il
  conteggio raggiunge `alert_after_min`; al massimo un avviso all'ora.

### 6.3 App (thread UI)

- Ogni secondo (`OnSecond`): prende l'ultimo `FrameSummary` (se piu' vecchio di 3 s non
  conta: segnali vuoti), aggiorna `signals`, `history` (secondi per categoria/app con lo
  stato corrente, mappa di usura con `luminanza × 1 s`), `alert_model`.
- All'avvio: sessione +1. Salvataggio di `history.json` ogni 60 s e all'uscita
  (scrittura atomica come `stats.json`); file mancante o rovinato = storico vuoto con un
  avviso nel log, mai un errore all'avvio.
- Avviso: `tray_.ShowBalloon("OLED Guard", "Da 60 minuti il 24% dello schermo e' fermo,
  luminoso e non protetto. Una pausa o \"Spegni schermo\" aiutano il pannello.")` e un evento
  "Avviso: schermo fermo da 60 min" nell'Attivita' recente.
- Statistiche per app spente (Privacy): i secondi per app non si registrano; quelli gia'
  salvati restano finche' non si cancella lo storico.
- "Cancella lo storico": azzera `history.json` (non `stats.json` del progetto 1), con
  conferma nella pagina.

### 6.4 Protocollo

App → pagina:
- `signals` (ogni secondo a pagina aperta): `{risk, motion, brightness, caption, measured}`
  (measured falso se non ci sono misure, es. in pausa).
- `signals` a `ready`: `{history: [[r,m,b] x 60], ...}` per riempire subito i grafici.
- `history` (a `ready`, ogni 60 s e quando si apre Statistiche): timeline (2016 valori o
  null), mappa del monitor protetto (576 valori normalizzati 0-1 + ore della zona
  peggiore, zona peggiore a parole, concentrazione, zone oltre il doppio, data di inizio,
  ore misurate), app piu' usate (prime 8: exe, secondi protetti, in pausa, regola),
  quote per contenuto, sessioni totali/settimana, giorni di fila/record, usura evitata
  oggi/totale, protetto questa settimana.
Pagina → app: `clearHistory`, `historyWatch {on}` (opzionale, se il builder vuole
inviare la storia solo a pagina Statistiche aperta).

### 6.5 Configurazione

| Campo | Default | Intervallo |
|---|---|---|
| `alert_enabled` | true | |
| `alert_threshold` | `"medium"` | low, medium, high |
| `alert_after_min` | 60 | 5-240 |
| `app_stats_enabled` | true | |

### 6.6 Interfaccia

**Striscia in basso** (sostituisce i cinque riquadri del progetto 1):
```
● SEGNALI DAL VIVO · ULTIMI 60 S                              Apri statistiche ›
[RISCHIO      24%] [MOVIMENTO     11%] [LUMINOSITA'    41%] [47m 32s  sessione  142 h totale ]
[~~~ ambra ~~~~~~] [~~~ indaco ~~~~~~] [~~~ accento ~~~~~~] [24%  picco 24h  31%  evitata oggi]
 picco 30 · media 22  media 9 · lavoro    media 42 (APL)
```
Grafici SVG a linea con area sfumata, colori come nel riferimento (ambra, indaco,
accento). Senza misure: linea interrotta e "in pausa".

**Pagina Statistiche**:
```
[Protetto in totale 142 h] [Sessioni 47] [Usura evitata 31% oggi] [Giorni di fila 12 · record 19]
[ Mappa di usura (32×18)            ] [ Grafico del rischio  6h | 24h | 7g ]
[ zona peggiore · concentrazione ... ] [ picco · media · attuale           ]
[ App piu' usate · 7 giorni          ] [ Usura per contenuto · 7 giorni    ]
[ Posizioni dello spostamento        ] [ Pause di oggi                     ]
```
Mappa: celle colorate con la scala del riferimento (blu scuro → verde acqua → verde →
ambra → rosso), legenda "meno ... piu'", sotto i quattro numeri (misurato da, zona
peggiore, concentrazione, zone oltre il doppio) e la nota "tempo in pausa non misurato".
Grafico: area ambra con griglia 25/50/75%, orari sull'asse, vuoti dove non ci sono
misure. App: avatar, nome, etichetta della regola, barra protetto/in pausa, tempo. Contenuto:
barra unica a colori con la legenda e le percentuali.

**Impostazioni**: pannello "Notifiche" (attive, soglia Bassa/Media/Alta, dopo 5-240 min)
e pannello "Privacy" (statistiche per app, cancella lo storico con conferma).

**Modalita' anteprima**: dati finti per tutti i messaggi nuovi.

## 7. Milestone

- **H1** Logica pura e test: `summary_model`, `signals`, `history`, `alert_model`,
  config, protocollo, versione 1.1.0.
- **H2** GPU: `luma_out`, `CSSummary`, lettura asincrona; test WARP contro
  `summary_model` (celle parziali, HDR oltre il bianco, celle oscurate, bordi).
- **H3** App: `SharedSignals`, `OnSecond`, `history.json`, sessioni, avviso, comandi.
- **H4** Interfaccia: striscia in basso, pagina Statistiche, Notifiche e Privacy,
  anteprima.
- **H5** Prova sulla TV con checklist (avvisando prima), revisione finale.

## 8. Rischi

1. **HDR**: la luminanza puo' superare molto il bianco SDR; la virgola fissa limita a 64×
   il bianco per pixel (oltre: saturato, raro e innocuo per la mappa relativa).
2. **Overflow delle somme**: 576 zone × fino a ~60 celle × 256 × 64 × 1024 per cella non
   entra in 32 bit; la somma per zona usa la luminanza media di cella (non per pixel) ×
   256 e l'area in celle: 60 × 64 × 256 < 2^20. I totali usano l'area in celle, non in
   pixel, salvo le celle parziali pesate (dettaglio al builder, vincolato dai test).
3. **Fuso orario e ora legale**: i secchi del grafico usano l'ora locale; al cambio
   d'ora un secchio puo' raddoppiare o mancare (accettabile).
4. **Dimensione di `history.json`**: ~2016 + 576 numeri + 8 giorni × 50 app: < 200 KB.
5. **Movimento falso**: un orologio con i secondi o un cursore che lampeggia muovono
   poche celle: restano sotto il 2%.

## 9. Lasciato al builder

- Layout interno del buffer del riassunto e virgola fissa esatta (vincolati dai test).
- Nomi delle zone a parole e colori esatti della scala.
- Se inviare `history` solo a pagina Statistiche aperta.
- Struttura di `history.json`.
