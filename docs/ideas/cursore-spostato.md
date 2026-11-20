# Idea futura: cursore spostato insieme all'immagine (opzione B, sperimentale)

Stato: da valutare dopo la v1. Nella v1 il cursore resta quello reale del sistema
(opzione A: ampiezza di default ±2 px, sfasamento medio ~1 px).

## Problema
L'overlay sposta l'immagine di 1-3 px, ma il cursore reale no: i clic cadono fino a
qualche pixel di distanza da cio' che si vede.

## Idea
Nascondere il cursore di sistema e disegnarne una copia nell'overlay, spostata dello
stesso offset dell'immagine. Clic e immagine coincidono esattamente.

## Costi e rischi
- Cursore software: almeno un frame di ritardo sul movimento del mouse (a 143 Hz si sente).
- Se l'app crasha, il cursore puo' restare invisibile: serve ripristino garantito
  (es. SystemParametersInfo(SPI_SETCURSORS) all'uscita + processo watchdog).
- Va disattivata insieme all'overlay quando l'app e' in pausa (giochi, fullscreen).

## Come attivarla
Opzione di config spenta di default, etichettata "sperimentale".
