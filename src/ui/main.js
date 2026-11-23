// Avvio dell'interfaccia: guscio, prima pagina, poi "ready" (l'app invia tutto lo stato).
'use strict';

buildShell();
wake();
showPage('panoramica');
if (preview) window.Mock.start();
send({ cmd: 'ready' });
// Se i testi non arrivano (file mancanti) la pagina si mostra comunque, con le chiavi.
setTimeout(() => document.body.classList.remove('loading'), 1500);
