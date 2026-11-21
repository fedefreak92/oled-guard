// Avvio dell'interfaccia: guscio, prima pagina, poi "ready" (l'app invia tutto lo stato).
'use strict';

buildShell();
wake();
showPage('panoramica');
if (preview) window.Mock.start();
send({ cmd: 'ready' });
