// Le dieci pagine dell'interfaccia. Ogni pagina costruisce il suo DOM una volta
// (build) e lo aggiorna dallo store (update) a ogni messaggio.
'use strict';

for (const [, items] of NAV) {
  for (const [id, label] of items) {
    PAGES[id] = {
      build(root) {
        const p = panel(label);
        p.body.append(h('div.empty', { text: 'Pagina in arrivo.' }));
        root.append(h('div.page', null, p.el));
      },
    };
  }
}
