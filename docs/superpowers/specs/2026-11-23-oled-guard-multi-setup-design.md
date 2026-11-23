# OLED Guard — pronta per altri schermi, lingue e PC (versione 1.2.0)

Data: 2026-11-23
Stato: in revisione
Progetto precedente: `2026-11-22-oled-guard-signals-stats-design.md` (v1.1.0, unito in main).

## 1. Intento

Finora OLED Guard e' stata costruita e provata su un solo PC: TV LG OLED 4K a 143 Hz,
HDR sempre attivo, RTX 5080, Windows 11, interfaccia in italiano. L'utente vuole
pubblicarla su GitHub (open source, portfolio) e prima vuole che funzioni per **chi usa
come schermo del PC un monitor o una TV OLED**, soprattutto TV, perche' molte TV non
hanno una protezione pensata per l'uso col PC. Su **Windows 10 e 11**, in **cinque
lingue**.

Lo studio del codice (2026-11-23) ha trovato cio' che oggi da' per scontato il setup
dell'utente:

- La scelta automatica dello schermo cerca "LG TV" nel nome, poi il principale; al primo
  avvio l'app parte in silenzio nella tray.
- Le finestre che si escludono dalla cattura (video con DRM, KeePassXC, alcune app
  bancarie) non compaiono nell'immagine catturata: l'overlay ci disegna sopra e
  l'utente vede nero o lo sfondo al posto della finestra.
- L'exe usa il runtime di Visual C++ come DLL: senza il Redistributable non parte.
- Testi solo in italiano, in C++ (notifiche, tray, motivi di pausa, attivita') e in JS.
- Testi che parlano di "la TV"; mappa di usura fissa 32×18 (16:9).
- Windows 10 mai provato; nessun modo di diagnosticare un problema su un PC altrui.

Gia' a posto: device D3D creato sull'adapter che possiede l'output (GPU ibride),
HDR/SDR decisi dal formato catturato, DPI per monitor, risoluzioni non multiple di 16,
WebView2 assente che non blocca la protezione.

## 2. Obiettivi, non-obiettivi, anti-obiettivi

**Obiettivi**
- Parte su qualunque PC Windows 10 (2004, build 19041) o Windows 11 x64 senza installare
  nulla oltre a WebView2 (gia' presente su Windows 11).
- Primo avvio guidato: lingua, scelta dello schermo da proteggere con identificazione a
  schermo, spiegazione breve; la protezione parte solo dopo la scelta.
- Pausa automatica quando sullo schermo protetto e' visibile una finestra esclusa dalla
  cattura; app di streaming note nell'elenco predefinito.
- Interfaccia, tray, notifiche e messaggi in inglese, italiano, spagnolo, tedesco e
  francese; lingua di Windows in automatico, cambiabile nelle Impostazioni.
- Testi neutri rispetto al tipo di schermo; mappa di usura con le proporzioni vere dello
  schermo (16:9, 16:10, 21:9, 32:9, 4:3).
- "Esporta diagnostica": un file di testo leggibile da allegare alle segnalazioni.
- README nelle cinque lingue, modulo di segnalazione su GitHub, release 1.2.0 come zip.

**Non-obiettivi** (scelte del progetto, rimandabili)
- Portatili: gestione della batteria, GPU integrate deboli.
- Piu' schermi protetti insieme; schermi in verticale (resta la pausa attuale).
- Lingue con caratteri CJK (giapponese, cinese, coreano).
- Installer, firma del codice, aggiornamenti automatici: si pubblica uno zip e il README
  spiega l'avviso di SmartScreen.
- "Buco" nell'overlay sopra le finestre protette (versione successiva).
- Riconoscere i video protetti dentro un browser: Windows non lo espone.
- Telemetria di qualunque tipo.

**Anti-obiettivi** (fallimento anche se "funziona")
- Una finestra che l'utente non vede piu' perche' l'overlay ci disegna sopra.
- Al primo avvio lo schermo sbagliato si mette a spostarsi senza che l'utente sappia
  perche'.
- Una traduzione che rompe l'impaginazione (testo tagliato, scorrimento orizzontale).
- Su un Windows troppo vecchio: crash o schermo nero invece di un messaggio chiaro.
- Un file di diagnostica che parte verso la rete o che l'utente non puo' leggere prima
  di condividerlo.
- Peggiorare l'uso sul PC dell'utente (TV LG, HDR): tutte le prove di regressione
  restano verdi.

## 3. Vincoli

- Gli stessi dei progetti precedenti: C++20, MSVC, CMake + Ninja, solo Windows SDK e
  WebView2; logica pura in `src/core` senza header di Windows; UI HTML/CSS/JS senza
  librerie, CSP `default-src 'self'`, niente rete; commenti in italiano, identificatori
  in inglese; `build.ps1` verde e commit a ogni task.
- Script di modifica con Python: `newline=''`.
- Il log resta in italiano (e' per chi sviluppa); i testi per l'utente vanno nei file di
  traduzione.
- Prove a runtime: i comandi della sessione girano nel pacchetto MSIX dell'app Claude e
  vedono un `%APPDATA%` virtualizzato. Copie, ripristini e avvii dell'app si fanno con
  processi lanciati fuori dal pacchetto (`Win32_Process.Create`), come gia' fatto per la
  prova della v1.1.0 (`build-check\prova-tv-*.ps1`). Avvisare l'utente prima di ogni
  prova che mostra qualcosa a schermo.
- Il PC dell'utente e' Windows 11 **Home**: niente Hyper-V ne' Windows Sandbox. Windows
  10 si prova in una macchina virtuale VirtualBox o VMware Workstation (gratuite).

## 4. Approcci considerati

### 4.1 Ordine del lavoro — scelta dell'utente: **tutto prima**
- **Tutto prima (scelto)**: tutte le parti di questo documento, poi la 1.2.0 su GitHub.
  Debutto piu' rifinito.
- *Beta presto*: punti bloccanti, beta 0.9 in inglese e italiano, il resto dopo le
  segnalazioni. Scartato dall'utente.

### 4.2 Finestre protette — scelta dell'utente: **pausa**
- **Pausa (scelta)**: se una finestra esclusa dalla cattura e' visibile sullo schermo
  protetto, l'overlay va in pausa come per i giochi. Semplice e sicuro; mentre la
  finestra e' aperta lo schermo non e' protetto.
- *Buco nell'overlay*: solo la zona della finestra resta senza spostamento. Migliore per
  chi la usa ma delicato (finestre che si muovono o si sovrappongono): rimandato.

### 4.3 Origine delle traduzioni — proposta accettata
File JSON per lingua, inglese e italiano curati, spagnolo, tedesco e francese tradotti
durante lo sviluppo e segnati "da rileggere" per chi e' madrelingua. Alternativa
scartata: solo inglese e italiano (l'utente vuole 4-5 lingue principali).

## 5. Decisioni dell'utente

- Pubblico: monitor e soprattutto TV OLED usati col PC, su Windows.
- Lingue: le 4-5 principali → **en, it, es, de, fr**.
- Windows 10 (2004+) e 11, Windows 10 dichiarato "provato meno".
- Tutto prima della pubblicazione; finestre protette con la pausa.

## 6. Design

### 6.1 Compatibilita' e distribuzione

- **Runtime statico**: `CMAKE_MSVC_RUNTIME_LIBRARY` = `MultiThreaded$<$<CONFIG:Debug>:Debug>`
  per tutti i target. Verificare che `WebView2LoaderStatic.lib` si colleghi senza
  conflitti; `dumpbin /dependents` dell'exe non deve elencare `MSVCP*`/`VCRUNTIME*`.
- **Controllo della versione di Windows** all'avvio, prima di creare overlay o hotkey:
  build letta con `RtlGetVersion` (non `GetVersionEx`, che mente senza manifest
  adeguato). Sotto 19041: messaggio tradotto ("serve Windows 10 versione 2004 o
  successiva") e uscita pulita. Il manifest dichiara gia' Windows 10/11.
- **WebView2 mancante** (Windows 10 senza runtime): la notifica gia' esistente diventa
  tradotta e dice dove scaricarlo; la protezione continua dalla tray. In quel caso il
  primo avvio usa la regola automatica (§6.2) e lo comunica con una notifica.
- **Release**: la CI produce `oled-guard-<versione>-windows-x64.zip` (exe, cartella `ui`,
  LICENSE, THIRD_PARTY_NOTICES, README) con il suo SHA-256; la release 1.2.0 si crea su
  GitHub da quell'artefatto, a mano, quando l'utente lo decide.

### 6.2 Primo avvio e scelta dello schermo

- **Primo avvio** = `config.json` assente. Un config esistente senza il nuovo campo
  conta come gia' configurato (gli utenti attuali non vedono la procedura).
- L'app apre l'interfaccia su una **procedura in tre passi** al posto delle pagine
  normali; l'overlay non parte finche' non e' finita:
  1. **Lingua**: preselezionata da Windows (§6.4), con i nomi nella propria lingua.
  2. **Schermo**: elenco con nome, risoluzione, frequenza, HDR, collegamento, "principale".
     Pulsante **Identifica**: per 3 s compare su ogni schermo un grande numero con il
     nome, in una finestra a schermo intero semitrasparente e trasparente ai clic, cosi'
     chi ha TV e monitor capisce quale e' quale. Preselezione: la regola automatica.
  3. **Cosa succede ora**: tre righe (spostamento di pochi pixel; zone ferme scurite;
     pausa automatica nei giochi e nelle finestre protette), il tasto di uscita di
     emergenza, la casella "Avvia con Windows" (spenta). Pulsante "Proteggi questo
     schermo": salva il config e avvia la protezione.
- Chiudere la procedura senza finirla: nessuna protezione, una notifica dice di riaprirla
  dalla tray; al prossimo avvio riparte la procedura.
- **Regola automatica** (`monitor: "auto"`): non cerca piu' "LG TV". Ordine: uno schermo
  HDR attivo collegato via HDMI (tipico delle TV), poi il principale, poi il primo.
  L'etichetta nell'interfaccia diventa "Automatico".
- **Schermo scelto non trovato** per piu' di 30 s di fila (non durante l'avvio di
  Windows: dopo i primi 60 s di vita dell'app): notifica "Lo schermo scelto non c'e'"
  con l'invito ad aprire la pagina Monitor. Resta la pausa `MonitorMissing` attuale.

### 6.3 Finestre protette

- Nuovo motivo di pausa `ProtectedWindow`, immediato (nessun ritardo di entrata), non
  "causato da un'app" ai fini dello storico ma registrato nell'attivita' con il nome del
  processo.
- **Rilevamento** nel `PauseMonitor`, a ogni raccolta (4 volte al secondo): `EnumWindows`
  sulle finestre di primo livello visibili, non ridotte a icona, non "cloaked"
  (`DWMWA_CLOAKED`), non dell'app stessa, il cui rettangolo (`DWMWA_EXTENDED_FRAME_BOUNDS`)
  interseca lo schermo protetto per almeno 64×64 px, con `GetWindowDisplayAffinity`
  diverso da `WDA_NONE`. Il risultato (presente o no, primo processo trovato) entra in
  `PauseInputs`; la decisione resta in `EvaluatePause` (core, testata).
- **Prima verifica (task 0, prova usa-e-getta)**: `GetWindowDisplayAffinity` su finestre
  di altri processi restituisce il valore giusto? Prova con KeePassXC (protezione dalla
  cattura attiva di serie) e con una piccola finestra di prova che chiama
  `SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE)`. Se non funziona: rilevamento solo
  per nome del processo (elenco predefinito) e si torna dall'utente prima di proseguire.
- **App UWP/pacchettizzate**: oggi il processo in primo piano di un'app UWP risulta
  `ApplicationFrameHost.exe`. Si risolve il processo vero dalla finestra figlia
  `Windows.UI.Core.CoreWindow`, sia per la pausa sia per le regole per app e le
  statistiche.
- **Elenco predefinito** delle app di streaming (in `denylist` solo nei config nuovi): i
  nomi dei processi di Netflix, Disney+, Prime Video e Apple TV per Windows, da
  verificare nel task 0 (documentazione, Microsoft Store o, solo con l'ok dell'utente,
  installandole sul suo PC); si mettono solo i nomi verificati.
- Config: `pause_on_protected_window` (bool, default `true`). Interfaccia: un
  interruttore nella pagina Profili app accanto alle altre pause automatiche.
- **Esito del task 1 (2026-11-23)**: `GetWindowDisplayAffinity` legge correttamente le
  finestre di altri processi (finestre di prova con `0x11` e `0x01` riportate esatte da un
  processo diverso). Anche l'overlay di OLED Guard risulta `0x11`: va escluso per
  processo. Nomi delle app di streaming: le fonti trovate non bastano a verificarli
  (Netflix: probabilmente `Netflix.exe`; Prime Video forse un involucro di Edge; Disney+
  e Apple TV senza nome documentato) e non sono installate sul PC dell'utente. Per la
  regola "solo nomi verificati" l'**elenco predefinito resta vuoto**: a schermo intero
  basta la pausa automatica, in finestra l'utente aggiunge una regola (le app UWP ora
  hanno il nome vero). Il README lo spiega.
- Video protetti dentro un browser: non rilevabili. Il README e la pagina Informazioni
  dicono che a schermo intero la pausa scatta da sola e che in finestra si usa la pausa
  manuale o una regola per il browser.

### 6.4 Traduzioni

- **File**: `src/ui/i18n/{en,it,es,de,fr}.json`, oggetto piatto di chiavi con punti
  (`"overlay.shift.title": "Pixel shift"`), segnaposto `{name}`, plurali con suffissi
  `_one`/`_other` (regola: `fr` → one per 0 e 1, le altre → one per 1). Campo `_meta` con
  `name` (nome nativo della lingua) e `review` (`"native"` per en e it, `"machine"` per
  es, de, fr).
- **Unica fonte**: il C++ legge i file con il parser JSON gia' esistente (logica in
  `src/core`, file in `src/app`), sceglie la lingua e manda il dizionario attivo alla
  pagina in un messaggio `strings` prima dello stato iniziale; cambiando lingua lo
  rimanda e la pagina ridisegna tutto. In anteprima (`mock.js`) la pagina legge i file
  da sola.
- **Lingua**: config `language` = `"auto"` | `en` | `it` | `es` | `de` | `fr`. `auto` usa
  `GetUserPreferredUILanguages`: la prima lingua dell'elenco che corrisponde (per
  prefisso, `de-AT` → `de`), altrimenti `en`. Chiave mancante nella lingua scelta →
  testo inglese; mancante anche in inglese → la chiave stessa (e un avviso nel log).
- **Testi prodotti dal C++** da spostare sulle chiavi: notifiche (`ShowNotice`), menu della
  tray, motivi di pausa mostrati all'utente, etichette delle regole per app, valori della
  pagina Windows per OLED, finestra "Scegli l'app", avviso del rischio, messaggi di
  config non valida. `PauseReasonText` resta per il log; per l'utente una funzione che
  restituisce la chiave.
- **Attivita' recente**: `ActivityEvent` memorizza chiave e parametri, non il testo; la
  pagina lo compone nella lingua attiva (cambiando lingua cambia anche lo storico
  visibile).
- **Formati**: numeri, date, orari e durate nella pagina con `Intl` e la lingua attiva;
  le poche durate nelle notifiche C++ con un helper che usa le chiavi `unit.*`.
- **Controlli**:
  - test unitario: ogni file ha le stesse chiavi e gli stessi segnaposto di `en.json`;
  - test unitario: nessun testo italiano rimasto nei punti elencati sopra (le funzioni
    restituiscono chiavi esistenti in `en.json`);
  - in anteprima una pseudo-lingua `xx` (solo `mock.js`) allunga ogni testo del 40% con
    accenti: a 1 280×800 e 1 000×640 nessun testo tagliato ne' scorrimento orizzontale,
    poi stessa verifica con `de`.
- **README** nelle cinque lingue (`README.md` in inglese, `README.<lingua>.md` per le
  altre), con i collegamenti in cima; nei tre tradotti una riga dice che la traduzione
  aspetta una rilettura.

### 6.5 Testi neutri

- "Lo schermo" al posto di "la TV" in tutti i testi; dove la differenza conta (standby
  della TV, DDC/CI non disponibile sulle TV) il testo copre entrambi i casi in una frase.
- Nessun nome di marca o modello nei testi e nelle regole; `mock.js` usa nomi generici.
- La pagina Informazioni ricorda di usare anche la cura del pannello integrata nello
  schermo (pixel refresh, spostamento integrato se c'e').

### 6.6 Mappa di usura con le proporzioni dello schermo

- La griglia delle zone dipende dalle proporzioni `a = larghezza / altezza` dell'output
  catturato: `rows = round(sqrt(576 / a))`, `cols = round(rows * a)`, con il prodotto al
  massimo 640. Esempi: 16:9 → 32×18; 16:10 → 30×19; 21:9 (3440×1440) → 38×16;
  32:9 → 46×13; 4:3 → 28×21.
- `CSSummary` e `ComputeSummary` ricevono `cols`/`rows` come parametri (costanti dello
  shader) e il buffer del riassunto e' dimensionato per 640 zone. I test GPU coprono
  16:9, 21:9 e 32:9 con dimensioni dispari.
- `history.json` versione 2: salva `cols`/`rows` della mappa. Un file v1 si legge come
  32×18. Se cambiano le dimensioni (altro schermo, altra risoluzione), la mappa riparte
  da zero e il resto dello storico resta.
- La pagina Statistiche disegna la griglia con `cols`×`rows` e le proporzioni giuste.

### 6.7 Diagnostica e segnalazioni

- **Esporta diagnostica** (pagina Avanzate e menu della tray): scrive
  `oled-guard-diagnostica-AAAAMMGG-HHMM.txt` nella cartella Documenti e la apre in
  Esplora file con il file selezionato. Contenuto, in inglese per chi legge le
  segnalazioni: versione e commit (hash breve passato da CMake al momento della build,
  "unknown" fuori da git), build di Windows, lingua scelta e di sistema, GPU con
  versione del driver, schermi (nome, risoluzione, frequenza, HDR, bianco SDR,
  collegamento, rotazione, adapter, scelto o no), `config.json` completo, statistiche di
  render degli ultimi minuti (frame copiati/ignorati, errori), ultime 500 righe del log.
  Prima dell'esportazione la pagina avvisa che il file contiene i nomi delle app nelle
  regole e che va letto prima di condividerlo. Nulla viene inviato.
- **Modulo di segnalazione** `.github/ISSUE_TEMPLATE/bug_report.yml` (in inglese): schermo
  (marca e modello), TV o monitor, GPU, versione di Windows, HDR acceso, cosa succede,
  file di diagnostica allegato. Piu' un modulo per le correzioni alle traduzioni.

### 6.8 Windows 10

- Le API usate esistono dalla 2004 (`DuplicateOutput1` 1803, `WDA_EXCLUDEFROMCAPTURE`
  2004, `DISPLAYCONFIG_SDR_WHITE_LEVEL` 1709). Il builder controlla le altre chiamate
  (WebView2, menu, notifiche, `SetThreadDescription`) e le rende facoltative dove non ci
  sono.
- Prova in macchina virtuale Windows 10 22H2 (VirtualBox o VMware), da installare solo
  dopo l'ok dell'utente (programma e immagine di Windows 10 di alcuni GB, licenza di
  prova senza attivazione): avvio, procedura di
  primo avvio, protezione con il dispositivo video della VM, pausa per finestra protetta,
  lingua, diagnostica. HDR e TV non si provano in VM: lo dice il README.

## 7. Prove

- Unitari e GPU (WARP) come sempre, piu' i nuovi (§6.3, §6.4, §6.6), anche in CI.
- **Sulla TV dell'utente**, con copia e ripristino dei file fatti fuori dal pacchetto:
  HDR acceso/spento; 60 e 120/144 Hz; 4K, 1440p e 1080p; scala 100%, 150% e 200%;
  finestra protetta aperta (la finestra di prova del task 0, o KeePassXC se l'utente lo
  usa) con pausa e ripresa; Netflix in Edge a schermo intero; primo avvio con
  config assente (e Identifica); cambio di lingua dal vivo; esportazione della
  diagnostica. La checklist si da' all'utente un pezzo per volta e il log conferma ogni
  punto (durata della sessione, eventi attesi) prima di considerarlo fatto.
- **Windows 10** in macchina virtuale (§6.8).
- Se l'utente ha un secondo schermo, prova di scelta e Identifica con due schermi.

## 8. Milestone

1. **Compatibilita'**: runtime statico, controllo della versione di Windows, testi del
   WebView2 mancante, zip di release con SHA-256 nella CI.
2. **Finestre protette**: task 0 (prova di `GetWindowDisplayAffinity` e nomi delle app di
   streaming), poi rilevamento, UWP, pausa, interruttore.
3. **Traduzioni, infrastruttura**: file, caricamento e scelta della lingua, messaggio
   `strings`, chiavi al posto dei testi in C++ e JS, attivita' con chiavi, controlli;
   `en` e `it` completi.
4. **Primo avvio e testi neutri**: procedura in tre passi, Identifica, regola automatica,
   avviso schermo mancante, revisione dei testi.
5. **Mappa per proporzioni**: shader, modello, storico v2, pagina Statistiche.
6. **Diagnostica e GitHub**: esportazione, moduli di segnalazione.
7. **Lingue es, de, fr e README** nelle cinque lingue, pseudo-lingua e verifica
   dell'impaginazione.
8. **Prove** (§7), correzioni, versione 1.2.0, release.

## 9. Rischi

- `GetWindowDisplayAffinity` potrebbe non funzionare su finestre di altri processi: il
  task 0 lo chiarisce prima di costruire sopra.
- Nomi dei processi delle app di streaming che cambiano: elenco solo con nomi verificati,
  modificabile dall'utente.
- Rilevamento delle finestre protette troppo largo (finestre invisibili ma "visibili"
  per Windows): filtri su cloaked, dimensione minima e intersezione; la pausa si vede
  nell'attivita' col nome del processo, quindi un falso positivo si riconosce.
- Testi tedeschi lunghi: pseudo-lingua e verifica a due dimensioni della finestra.
- VirtualBox/VMware potrebbero non offrire la duplicazione del desktop come una GPU
  vera: si annota cosa si e' potuto provare.
- Traduzioni automatiche goffe: segnate "da rileggere", modulo per le correzioni.

## 10. Scelte fatte nella stesura (da confermare leggendo)

- La protezione non parte finche' la procedura di primo avvio non e' finita.
- "Avvia con Windows" spento di default nella procedura.
- Regola automatica: HDR via HDMI, poi principale, poi il primo.
- Diagnostica nella cartella Documenti, in inglese, con avviso sulla privacy.
- Elenco delle app di streaming solo nei config nuovi.
- Log in italiano; versione 1.2.0; zip senza installer ne' firma.

## 11. Lasciato al builder

- Nomi esatti delle chiavi di traduzione e organizzazione dei file.
- Aspetto della procedura di primo avvio e della finestra Identifica, nello stile
  dell'interfaccia attuale.
- Come passare `cols`/`rows` allo shader (costanti o buffer) e la forma esatta di
  `history.json` v2.
- Dove mettere l'interruttore delle finestre protette se la pagina Profili app risulta
  affollata.
