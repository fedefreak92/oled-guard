# Come è stata sviluppata OLED Guard

[English](DEVELOPMENT.md) · **Italiano**

Ho progettato e pubblicato OLED Guard in quattro giorni (21-24 novembre 2026),
guidando il progetto dall'inizio alla fine e usando
[Claude Code](https://claude.com/claude-code), l'agente di programmazione di Anthropic,
per lo sviluppo. Ho definito il prodotto e i suoi vincoli, approvato ogni progetto e
ogni piano prima che esistesse una riga di codice, preso le decisioni dove c'era una
vera scelta, accettato ogni tappa su hardware vero e deciso cosa rilasciare. Claude Code
ha scritto il codice seguendo quelle spec e quei piani.

Questa pagina documenta come ha funzionato, con i link alle prove: spec, piani, commit e
test.

## Chi ha fatto cosa

| Io | Claude Code |
| --- | --- |
| Il problema: la mia TV LG OLED, usata come monitor del PC, non ha una protezione dal burn-in pensata per il desktop | Ha posto le domande di design e proposto le alternative con pro e contro |
| Obiettivi e priorità di ogni versione: prima le TV, cinque lingue, Windows 10 e 11, cosa togliere | Ha scritto le spec di design e i piani di implementazione |
| Ho approvato ogni spec e ogni piano prima dell'implementazione | Ha scritto codice, test e documentazione, partendo dai test |
| Le decisioni di prodotto quando il design aveva una vera scelta (esempi sotto) | Ha fatto revisionare ogni attività da agenti separati e ha annotato ogni scostamento dal piano |
| Le prove di accettazione su hardware vero alla fine di ogni tappa (RTX 5080, TV LG OLED, 4K 143 Hz, HDR attivo) | Ha preparato le checklist delle prove, letto i log e corretto quello che le prove trovavano |
| Le decisioni di rilascio: unione, licenza GPL-3.0, pubblicazione | Ha preparato il rilascio: CI, pacchetto, README in cinque lingue |

## Il metodo

Ogni tappa ha seguito gli stessi passi, con il flusso di lavoro
[Superpowers](https://github.com/obra/superpowers) per Claude Code:

1. **Design.** L'agente fa una domanda alla volta, poi scrive una spec
   (`docs/superpowers/specs`). Non si costruisce nulla finché non la approvo.
2. **Piano.** La spec diventa piccole attività, ognuna con i test che la dimostrano
   (`docs/superpowers/plans`). Approvo anche il piano.
3. **Implementazione partendo dai test.** Test che fallisce, codice, test che passa,
   commit. Poi agenti separati controllano ogni attività rispetto alla spec e per la
   qualità del codice.
4. **Revisione finale** di tutto il ramo prima dell'unione.
5. **Prova dal vivo sulla TV**, con le mie impostazioni salvate prima e ripristinate
   dopo. La prova è il log dell'app, non l'impressione che funzioni.
6. **Unione e rilascio** solo con il mio ok.

## Tappe

| Data | Tappa | Versione | Spec | Piano |
| --- | --- | --- | --- | --- |
| 21 nov | Overlay con pixel shift, cattura sulla GPU, uscita di emergenza | | [spec](superpowers/specs/2026-11-21-oled-guard-design.md) | [piano](superpowers/plans/2026-11-21-oled-guard.md) |
| 21 nov | Oscuramento delle aree statiche luminose (compute shader) | | [spec](superpowers/specs/2026-11-21-oled-guard-dimming-design.md) | [piano](superpowers/plans/2026-11-21-oled-guard-dimming.md) |
| 21 nov | Prima interfaccia (WebView2) | | [spec](superpowers/specs/2026-11-21-oled-guard-ui-design.md) | [piano](superpowers/plans/2026-11-21-oled-guard-ui.md) |
| 22 nov | Nuova interfaccia: barra laterale, 10 pagine, luminosità, bordi, modalità assente | 1.0.0 | [spec](superpowers/specs/2026-11-21-oled-guard-ui-v2-design.md) | [piano](superpowers/plans/2026-11-21-oled-guard-ui-v2.md) |
| 23 nov | Segnali dal vivo, mappa di usura e statistiche | 1.1.0 | [spec](superpowers/specs/2026-11-22-oled-guard-signals-stats-design.md) | [piano](superpowers/plans/2026-11-22-oled-guard-signals-stats.md) |
| 24 nov | Qualsiasi schermo, cinque lingue, Windows 10, primo avvio guidato | 1.2.0 | [spec](superpowers/specs/2026-11-23-oled-guard-multi-setup-design.md) | [piano](superpowers/plans/2026-11-23-oled-guard-multi-setup.md) |

## Controlli per il codice scritto da un agente

Un agente scrive codice in fretta, quindi anche i controlli devono essere rapidi e
automatici.

- **Logica separata da Windows.** Tutto ciò che decide qualcosa (quando spostare,
  quando mettere in pausa, quanto scurire, statistiche, traduzioni, avvio) sta in
  `src/core` senza header di Windows, così 306 test unitari possono coprirlo.
- **Shader provati senza scheda video.** 33 test eseguono gli shader HLSL veri su
  WARP, il rasterizzatore software di Direct3D, e li confrontano con modelli di
  riferimento in C++.
- **CI su due generazioni di Windows.** Ogni push compila, esegue tutti i test e avvia
  l'eseguibile vero su Windows 11 e su Windows Server 2022 (generazione di Windows 10).
- **Traduzioni controllate dai test.** Ogni lingua deve avere le stesse chiavi e gli
  stessi segnaposto dell'inglese, e ogni testo usato dalla pagina deve esistere.
- **Sicurezza prima di tutto.** Un errore nell'overlay potrebbe lasciare lo schermo
  nero o bloccato, quindi un watchdog e una scorciatoia di emergenza su un thread
  proprio tolgono l'overlay se qualcosa smette di rispondere.
- **Decisioni scritte.** Ogni volta che l'agente si è scostato dal piano ha scritto
  cosa ha deciso, perché e quanto costerebbe se fosse sbagliato. Ho rivisto quelle
  scelte invece di riscoprirle nel codice.

## Problemi riscontrati e come sono stati gestiti

- **Errore di compilazione rilevato prima del rilascio.** La CI ha rilevato che MSVC
  14.52, l'ultima versione del compilatore Microsoft, generava codice sbagliato per un
  ciclo sulla mappa delle posizioni: un test delle statistiche restituiva 18,35 invece
  di 100, mentre lo stesso codice era corretto con MSVC 14.51 in locale e 14.44 in CI.
  Impatto: la pagina Statistiche avrebbe mostrato una mappa delle posizioni errata. Una
  prima correzione, basata su un'ipotesi, non ha superato la CI; l'analisi è quindi
  passata ai dati, con i valori delle celle e l'assembly generato estratti dalla CI.
  Causa: l'ottimizzatore univa un contatore di ciclo negativo a 64 bit a una struttura
  passata come parametro senza azzerarne la metà alta. Soluzione: i cicli interessati
  non costruiscono più quella struttura, test di regressione controllano ogni cella e le
  release si compilano con MSVC 14.44 ([correzione](https://github.com/fedefreak92/oled-guard/commit/f1cd25a)).
- **Oscuramento validato solo su dati sintetici.** La prima implementazione
  dell'oscuramento superava tutti i test sintetici ma su un desktop reale scuriva le
  zone sbagliate. Soluzione: una modalità di diagnostica (statistiche, griglie,
  istantanee dei fotogrammi), e le funzioni visive si accettano solo dopo la verifica
  sullo schermo vero ([diagnostica](https://github.com/fedefreak92/oled-guard/commit/3fe5160)).
- **Pausa per le finestre protette che non terminava.** La prova di accettazione dal
  vivo della 1.2.0 ha mostrato che la pausa per le finestre protette (gestori di
  password, video protetti) non terminava mentre la finestra di OLED Guard le copriva:
  le finestre dell'app erano escluse del tutto dal controllo delle coperture. Individuato
  dal log durante la prova e corretto lo stesso giorno ([correzione](https://github.com/fedefreak92/oled-guard/commit/167b396)).
- **Funzione tolta dopo la prova di accettazione.** "Spegni schermo" funzionava come
  previsto ma non portava benefici sull'hardware di riferimento, quindi è stata tolta
  invece di essere rilasciata ([rimozione](https://github.com/fedefreak92/oled-guard/commit/981befa)).
- **Notifiche riviste sull'uso reale.** La revisione finale ha rilevato che l'avviso
  "schermo non trovato" sarebbe comparso ogni sera a chi spegne la TV e lascia il PC
  acceso, cioè il pubblico principale. Ora compare solo se lo schermo non è mai stato
  rilevato da quando l'app è partita ([correzione](https://github.com/fedefreak92/oled-guard/commit/017986b)).
- **Copertura dei test dichiarata.** Windows 10 è coperto dalla CI (compilazione, test e
  avvio sulla generazione di Windows 10) invece che da una macchina dedicata. L'overlay
  non è ancora girato su un PC con Windows 10 vero, e il README lo dice.

## Numeri

- 4 giorni, 113 commit, 6 spec e 6 piani
- Circa 20.000 righe di C++20, HLSL e JavaScript, di cui circa 4.900 di test
- 339 test automatici, CI su due versioni di Windows
- 5 lingue, niente da installare oltre a Windows e WebView2

## Commit

Ogni commit ha una riga `Co-Authored-By: Claude`. Le ho tenute di proposito:
descrivono esattamente come è stato fatto il progetto.
