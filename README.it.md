# OLED Guard

[![CI](https://github.com/fedefreak92/oled-guard/actions/workflows/ci.yml/badge.svg)](https://github.com/fedefreak92/oled-guard/actions/workflows/ci.yml)
[![Licenza: GPL-3.0](https://img.shields.io/badge/licenza-GPL--3.0-blue.svg)](LICENSE)

[English](README.md) · **Italiano** · [Español](README.es.md) · [Deutsch](README.de.md) · [Français](README.fr.md)

> **Un progetto di ingegneria del software con l'IA.** Ho progettato, diretto e
> provato OLED Guard; il codice l'ha scritto [Claude Code](https://claude.com/claude-code)
> a partire da spec e piani approvati da me. [Come è stata sviluppata →](docs/DEVELOPMENT.it.md)

Un'app per Windows che protegge dal burn-in un monitor o una TV OLED usati col PC.
Molte TV OLED non hanno una protezione pensata per l'uso col PC, dove barra delle
applicazioni, icone e bordi delle finestre restano per ore sugli stessi pixel. OLED
Guard cattura il desktop sulla GPU e lo ridisegna, spostato di 1-3 pixel, in un
overlay trasparente ai clic. Inoltre scurisce gli elementi piccoli fermi e luminosi
da molto tempo (barra, icone, loghi) e mostra segnali dal vivo e statistiche d'uso.

![Pagina Panoramica](docs/screenshots/overview.png)

| Statistiche | Impostazioni dell'overlay |
| --- | --- |
| ![Pagina Statistiche con la mappa di usura e il grafico del rischio](docs/screenshots/statistics.png) | ![Spostamento, oscuramento e bordi scuriti](docs/screenshots/overlay.png) |

*Schermate dell'interfaccia in modalità anteprima, con dati dimostrativi (in inglese).*

## Punti forti

- **Tutta l'elaborazione dell'immagine avviene sulla GPU.** La cattura (Desktop
  Duplication) passa a compute shader HLSL e a un overlay DirectComposition che lascia
  passare i clic ed è escluso dalla cattura dello schermo. La CPU non legge mai un
  pixel del desktop: torna indietro solo un piccolo riepilogo, in modo asincrono.
- **Sicura per progetto.** Un watchdog e una scorciatoia di emergenza su un thread
  proprio tolgono l'overlay se qualcosa smette di rispondere, così lo schermo non resta
  mai nero o bloccato.
- **339 test automatici.** 306 test unitari coprono la logica, tenuta separata dagli
  header di Windows. 33 test sulla GPU eseguono gli shader veri su WARP e li
  confrontano con modelli di riferimento in C++. La CI compila, prova e avvia l'app su
  due generazioni di Windows.
- **Un errore del compilatore trovato prima del rilascio.** La CI ha scoperto che MSVC
  14.52 compila male un ciclo sulla mappa delle posizioni. La causa è stata trovata
  nell'assembly generato e corretta con test di regressione
  ([la storia](docs/DEVELOPMENT.it.md#cosa-mi-hanno-insegnato-i-problemi-veri),
  [la correzione](https://github.com/fedefreak92/oled-guard/commit/f1cd25a)).
- **Costruita in quattro giorni con Claude Code**, partendo da spec e test, con ogni
  tappa accettata su hardware vero ([come è stata sviluppata](docs/DEVELOPMENT.it.md)).

## Funzioni

- **Spostamento dell'immagine**: tutto il desktop si sposta di 1-3 px ogni 20-60 s,
  in modo graduale su 2-5 s, così il testo non si sfoca e non salta. Funziona in HDR
  e in SDR con i colori fedeli.
- **Oscuramento delle aree ferme**: un compute shader trova le celle di 16x16 px
  luminose e invariate da minuti e abbassa piano piano solo quelle piccole (barra,
  icone, loghi), lasciando stare i documenti che stai leggendo. La finestra sotto il
  mouse torna subito normale.
- **Luminosità software, bordi scuriti e modalità assente**, che abbassa l'immagine
  quando ti allontani dal PC.
- **Pausa automatica** in giochi, app a schermo intero, presentazioni, schermo
  bloccato, UAC, cambio di sessione e con le **finestre protette** (vedi sotto), più
  regole per singola app.
- **Segnali dal vivo e statistiche**: rischio, movimento e luminosità degli ultimi
  60 s, mappa di usura del pannello che segue le sue proporzioni (16:9, 16:10,
  ultrawide, super-ultrawide), grafico del rischio 6 h / 24 h / 7 g, app più usate e
  un avviso nella barra delle applicazioni quando il rischio non protetto resta alto.
  Le statistiche non lasciano mai il PC.
- **Primo avvio guidato**: scegli la lingua e lo schermo da proteggere; **Identifica**
  mostra un grande numero su ogni schermo, comodo con TV e monitor insieme. La
  protezione parte solo quando confermi.
- **Cinque lingue**: inglese, italiano, spagnolo, tedesco e francese, secondo la lingua
  di Windows o scelta nelle Impostazioni.
- **Impostazioni di Windows adatte all'OLED** (solo su richiesta, con copia e
  ripristino): tema scuro, barra che si nasconde, spegnimento dello schermo,
  salvaschermo e sfondo neri, icone del desktop nascoste e altre.
- **Esporta diagnostica**: un file di testo leggibile da allegare alle segnalazioni.
- Icona nella tray, tasti rapidi globali, avvio automatico, istanza singola, log e
  uscita di emergenza.

## Download e primo avvio

1. Scarica l'ultimo `oled-guard-<versione>-windows-x64.zip` dalle
   [Release](https://github.com/fedefreak92/oled-guard/releases) e, se vuoi,
   controllalo con il file `.sha256` accanto.
2. Estrailo dove preferisci e avvia `oled-guard.exe`. Non serve installare altro: il
   runtime C++ è incluso, e il runtime WebView2 per la finestra c'è già in Windows 11
   (su Windows 10 l'app ti dice dove scaricarlo, e la protezione funziona dalla tray
   anche senza).
3. La release non è firmata, quindi al primo avvio Windows SmartScreen mostra un
   avviso: scegli *Ulteriori informazioni → Esegui comunque*.
4. Segui i tre passi della procedura: lingua, schermo OLED, un breve riepilogo.

## Finestre e video protetti

Alcune app nascondono le loro finestre alla cattura dello schermo: video protetti
(per esempio nelle app di streaming), gestori di password come KeePassXC, alcune app
bancarie. Siccome l'overlay ridisegna un'immagine catturata, le coprirebbe di nero o
con il desktop dietro. OLED Guard va in pausa da solo mentre una finestra così è
visibile sullo schermo protetto (si può disattivare in *Profili app*).

I video protetti **dentro un browser** Windows non li riconosce. A schermo intero la
protezione va comunque in pausa da sola; in finestra usa la pausa manuale o aggiungi
una regola per il browser in *Profili app*.

## Come funziona

```
Desktop Duplication (DXGI)  ──►  texture D3D11 (resta sulla GPU)
                                      │
                 ┌────────────────────┼─────────────────────┐
                 ▼                    ▼                     ▼
        CSDetect / CSActivate     CSSummary            shift_ps.hlsl
        celle ferme e luminose,   zone della mappa,    disegna l'immagine spostata,
        livelli di oscuramento    letto 1 volta al s   oscuramento, luminosità, bordi
                                  (asincrono, piccolo)       │
                                                             ▼
                              overlay DirectComposition, trasparente ai clic,
                              escluso dalle catture dello schermo
```

- **La CPU non legge nessun pixel del desktop.** Torna indietro solo un piccolo buffer
  di riassunto, in modo asincrono con `D3D11_MAP_FLAG_DO_NOT_WAIT`, così il thread di
  render non aspetta mai la GPU.
- **Il cursore vero non viene toccato**: lo sfasamento del clic è pari allo
  spostamento del momento (circa 2 px). È una scelta voluta, contro un cursore
  software che aggiungerebbe ritardo e potrebbe sparire dopo un crash.
- **Overlay a prova di guasto**: un thread di emergenza con un tasto rapido tutto suo
  e un watchdog tolgono l'overlay se il thread dell'interfaccia o quello di render
  smettono di rispondere, così lo schermo non resta mai congelato o nero.
- **Interfaccia**: WebView2 con HTML/CSS/JS semplici, CSP rigida
  (`default-src 'self'`), nessuna libreria e nessun accesso alla rete. I testi stanno
  in `src/ui/i18n/<lingua>.json`, condivisi da finestra, tray e notifiche.

## Struttura del codice

| Cartella | Contenuto |
| --- | --- |
| `src/core` | Logica pura senza header di Windows: pianificazione dello spostamento, regole di pausa, modello dell'oscuramento, statistiche, avviso, traduzioni, decisioni d'avvio, testo della diagnostica, JSON, config, protocollo dell'interfaccia |
| `src/app` | Win32, DXGI e D3D11: cattura, thread di render, overlay, tray, tasti rapidi, file, finestra WebView2, identificazione degli schermi, esportazione della diagnostica |
| `src/app/shaders` | Shader HLSL, compilati in build con `fxc` e inclusi come header |
| `src/ui` | L'interfaccia; `i18n/` contiene le traduzioni, `mock.js` fornisce dati finti quando la pagina gira in un browser normale |
| `tests` | Test unitari di `src/core`, con un piccolo framework senza dipendenze |
| `tests_gpu` | Test degli shader sul dispositivo software WARP |
| `tools` | Confezione della release (controllo delle dipendenze, zip, SHA-256) |
| `docs/superpowers` | Spec di design e piani di implementazione di ogni milestone |

## Test

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1
```

configura, compila ed esegue le due serie di test con CTest.

- **Test unitari**: coprono la logica pura di `src/core`, compresi i controlli che ogni
  file di lingua abbia le stesse chiavi e gli stessi segnaposto dell'inglese e che ogni
  testo usato dalla pagina esista.
- **Test GPU**: eseguono gli shader veri su **WARP**, il rasterizzatore software di
  Direct3D, quindi non serve una scheda video e girano anche nella CI. Ogni shader
  viene confrontato con un modello di riferimento in C++ (`src/core/dim_model.*`,
  `summary_model.*`, `shade_model.*`) su scene sintetiche: valori HDR molto sopra il
  bianco SDR, risoluzioni dispari, formati 16:10, ultrawide e super-ultrawide,
  finestre attive e pause.

## Compilazione

Requisiti:

- Visual Studio 2022 o successivo, o i Build Tools, con il carico di lavoro C++ per
  desktop e un Windows 10/11 SDK (CMake e Ninja sono inclusi)
- Una GPU Direct3D 11 per usare l'app; i test girano anche senza

`build.ps1` trova Visual Studio da solo, compila in `build\` ed esegue i test.
`-Config Debug` fa una build di debug e `-Clean` riparte da zero. L'app è
`build\oled-guard.exe` e ha bisogno della cartella `ui` che la build copia accanto.
`tools\package-release.ps1` controlla l'eseguibile e prepara lo zip della release.

Per lavorare solo sull'interfaccia basta servire `src/ui` con un server statico
qualsiasi (per esempio `python -m http.server --directory src/ui`) e aprirla nel
browser: i dati arrivano da `mock.js`. Parametri utili: `?lang=de` (lingua),
`?lang=xx` (lingua di prova più lunga del 40%, per controllare l'impaginazione),
`?setup=1` (procedura di primo avvio), `?grid=21x9` (mappa di usura ultrawide).

## Uso

OLED Guard resta nella tray e protegge lo schermo scelto al primo avvio (si cambia
dal menu della tray o dalla pagina *Schermo*). Tasti rapidi predefiniti
(modificabili):

| Tasti | Azione |
| --- | --- |
| `Ctrl+Alt+Shift+P` | Pausa / ripresa |
| `Ctrl+Alt+Shift+F12` | Uscita di emergenza |

Opzioni da riga di comando: `--open-ui`, `--list-monitors`, `--debug`, `--fast-shift`
(una nuova posizione ogni 3-5 s, per le prove) e `--dim-diagnostics` (statistiche e
istantanee dell'oscuramento nella cartella dei log).

File:

- Impostazioni, statistiche, storico, copia delle impostazioni di Windows e log in
  `%APPDATA%\oled-guard` (`config.json`, `stats.json`, `history.json`,
  `windows-backup.json`, `logs\`)
- Dati di WebView2 in `%LOCALAPPDATA%\oled-guard\webview2`
- Avvio automatico in `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` (valore
  `OledGuard`), solo se lo attivi
- Diagnostica esportata nella cartella *Documenti*

Non servono diritti di amministratore. Le impostazioni di Windows cambiano solo
quando le applichi dalla pagina *Windows per OLED*, e *Ripristina come prima* rimette
i valori salvati.

## Sistemi supportati

- **Windows 11**: provato.
- **Windows 10 versione 2004 o successiva**: supportato, provato meno. La CI compila,
  prova e avvia l'app su Windows Server 2022 (la generazione di Windows 10, build
  20348), ma overlay e finestra non sono ancora stati provati su Windows 10 vero. Le
  versioni più vecchie mostrano un messaggio e non partono, perché manca l'esclusione
  dalla cattura che serve all'overlay.
- Monitor e TV, SDR e HDR, qualsiasi frequenza e risoluzione. Provato a fondo su una
  configurazione (RTX 5080, TV LG OLED, HDR attivo): le segnalazioni da altri schermi
  e GPU sono benvenute.

## Segnalare un problema

Apri una issue con il modulo *Bug report*: chiede lo schermo, la GPU, la versione di
Windows e il file di diagnostica (*Avanzate → Esporta diagnostica*, o il menu della
tray). Leggi il file prima di allegarlo: la configurazione contiene i nomi delle app
nelle tue regole. Non viene mai inviato nulla in automatico.

Spagnolo, tedesco e francese sono traduzioni automatiche in attesa della rilettura di
un madrelingua: le correzioni sono benvenute con il modulo *Translation fix* o con una
pull request su `src/ui/i18n/`.

## Limiti

- Uno schermo alla volta. Gli schermi in verticale non sono supportati: l'app resta in
  pausa.
- I video protetti dentro un browser non vengono riconosciuti (vedi sopra).
- Portatili: ancora nessuna gestione specifica della batteria.
- Commenti del codice e log sono in italiano; gli identificatori in inglese.
- La build di release non è firmata.

## Avvertenza

OLED Guard riduce il tempo in cui gli stessi pixel restano accesi, ma non può
garantire che il pannello non subisca burn-in. Continua a usare la cura del pannello
integrata nello schermo (pixel refresh e, se c'è, il suo spostamento dei pixel). Non
comunica con lo schermo e non ne modifica il firmware.

## Come è stata sviluppata

Ho sviluppato OLED Guard in quattro giorni con Claude Code come agente di
programmazione, lavorando da product owner e responsabile tecnico: ho fissato
obiettivi e vincoli, approvato ogni spec di design e ogni piano prima
dell'implementazione, provato ogni tappa su hardware vero e deciso cosa rilasciare.
Claude Code ha scritto codice, test e documentazione, partendo dai test, con ogni
attività revisionata da agenti separati.

[docs/DEVELOPMENT.it.md](docs/DEVELOPMENT.it.md) racconta tutto: chi ha fatto cosa,
il metodo, le tappe e i problemi veri trovati lungo la strada. Spec e piani sono in
[`docs/superpowers`](docs/superpowers), e ogni commit ha una riga `Co-Authored-By`.

## Licenza

Copyright (C) 2026 fedefreak92. OLED Guard è software libero con licenza
[GNU General Public License v3.0](LICENSE) (GPL-3.0-only): puoi usarlo, studiarlo,
modificarlo e condividerlo, e chi lo distribuisce, modificato o no, deve farlo con la
stessa licenza e insieme al codice sorgente. I componenti di terze parti sono elencati in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
