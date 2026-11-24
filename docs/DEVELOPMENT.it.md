# Come è stata sviluppata OLED Guard

[English](DEVELOPMENT.md) · **Italiano**

Ho sviluppato OLED Guard in quattro giorni (21-24 novembre 2026) con
[Claude Code](https://claude.com/claude-code), l'agente di programmazione di
Anthropic. Il codice non l'ho scritto a mano. Il mio lavoro è stato quello di un
product owner e responsabile tecnico: decidere cosa costruire, fissare i vincoli,
approvare ogni progetto prima che si scrivesse codice, provare il risultato su
hardware vero e decidere cosa rilasciare.

Questa pagina spiega chi ha fatto cosa, il metodo, le tappe e i punti in cui i test
automatici non sono bastati.

## Chi ha fatto cosa

| Io | Claude Code |
| --- | --- |
| Il problema: la mia TV LG OLED, usata come monitor del PC, non ha una protezione dal burn-in pensata per il desktop | Ha posto le domande di design e proposto le alternative con pro e contro |
| Obiettivi e priorità di ogni versione: prima le TV, cinque lingue, Windows 10 e 11, cosa togliere | Ha scritto le spec di design e i piani di implementazione |
| Ho approvato ogni spec e ogni piano prima dell'implementazione | Ha scritto codice, test e documentazione, partendo dai test |
| Le decisioni di prodotto quando il design aveva una vera scelta (esempi sotto) | Ha revisionato ogni attività con agenti revisori separati e annotato ogni scostamento dal piano come decisione scritta |
| Le prove di accettazione su hardware vero alla fine di ogni tappa (RTX 5080, TV LG OLED, 4K 143 Hz, HDR attivo) | Ha preparato le checklist delle prove, letto i log e corretto quello che le prove trovavano |
| Le decisioni di rilascio: unione, licenza GPL-3.0, pubblicazione | Ha preparato il rilascio: CI, pacchetto, README in cinque lingue |

## Il metodo

Ogni tappa ha seguito gli stessi passi, con il flusso di lavoro
[Superpowers](https://github.com/obra/superpowers) per Claude Code:

1. **Design.** Una conversazione in cui l'agente fa una domanda alla volta, poi una
   spec scritta (`docs/superpowers/specs`). Non si costruisce nulla finché non la
   approvo.
2. **Piano.** La spec diventa piccole attività, ognuna con i test che la dimostrano
   (`docs/superpowers/plans`). Approvo anche il piano.
3. **Implementazione partendo dai test.** Ogni attività segue test che fallisce,
   codice, test che passa, commit, e poi passa per agenti revisori separati che la
   confrontano con la spec e ne controllano la qualità.
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
  `src/core` senza header di Windows, ed è coperto da 303 test unitari.
- **Shader provati senza scheda video.** 33 test eseguono gli shader HLSL veri su
  WARP, il rasterizzatore software di Direct3D, e li confrontano con modelli di
  riferimento in C++.
- **CI su due generazioni di Windows.** Ogni push compila, esegue tutti i test e
  avvia l'eseguibile vero su Windows 11 e su Windows Server 2022 (generazione di
  Windows 10).
- **Traduzioni controllate dai test.** Ogni lingua deve avere le stesse chiavi e gli
  stessi segnaposto dell'inglese, e ogni testo usato dalla pagina deve esistere.
- **Sicurezza prima di tutto.** Un errore nell'overlay potrebbe lasciare lo schermo
  nero o bloccato, quindi un watchdog e una scorciatoia di emergenza su un thread
  proprio tolgono l'overlay se qualcosa smette di rispondere.
- **Decisioni scritte.** Ogni volta che l'agente si è scostato dal piano ha scritto
  cosa ha deciso, perché e quanto costa se è sbagliato, così potevo rivedere le
  scelte invece di riscoprirle.

## Dove i test non sono bastati

Sono i momenti da cui ho imparato di più.

- **Scurire le zone sbagliate.** Il primo oscuramento passava tutti i test sintetici,
  ma sul mio desktop vero scuriva le zone sbagliate. Abbiamo aggiunto una modalità
  di diagnostica (statistiche, griglie e istantanee confrontate fotogramma per
  fotogramma) e da lì in poi una funzione visiva era finita solo quando si vedeva
  giusta sullo schermo vero.
- **L'app che copre se stessa.** Nella prova dal vivo della 1.2.0 la pausa per le
  finestre protette (gestori di password, video protetti) non finiva mai quando la
  finestra di OLED Guard stava sopra di loro: il codice ignorava le finestre
  dell'app del tutto, anche come copertura. Il log l'ha mostrato in pochi minuti ed
  è stato corretto lo stesso giorno.
- **Togliere una funzione.** "Spegni schermo" era stata costruita e provata, ma sulla
  mia TV non serviva a niente e ho capito che non mi era utile. L'ho tolta invece di
  rilasciare qualcosa che non funziona sullo schermo principale.
- **Pensare alla sera di chi la usa.** La revisione finale si è accorta che l'avviso
  "schermo non trovato" sarebbe comparso ogni sera a chi spegne la TV e lascia il PC
  acceso, cioè il pubblico principale. Ora compare solo se lo schermo non si è mai
  visto da quando l'app è partita.
- **Dire cosa non è provato.** Non ho voluto installare una macchina virtuale con
  Windows 10, quindi la CI controlla compilazione, test e avvio sulla generazione di
  Windows 10. L'overlay non è mai girato su un Windows 10 vero, e il README lo dice.

## Numeri

- 4 giorni, 106 commit, 6 spec e 6 piani
- Circa 20.000 righe di C++20, HLSL e JavaScript, di cui circa 4.900 di test
- 336 test automatici, CI su due versioni di Windows
- 5 lingue, nessuna dipendenza oltre a Windows e WebView2

## Commit

Ogni commit ha una riga `Co-Authored-By: Claude`. Le ho tenute di proposito:
descrivono esattamente come è stato fatto il progetto.
