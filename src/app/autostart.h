// Avvio automatico con Windows: valore nella chiave Run dell'utente corrente.
#pragma once

namespace oled {

enum class AutostartState {
  Missing,    // nessun valore
  ThisExe,    // punta a questo eseguibile
  OtherPath,  // punta altrove (exe spostato o copia vecchia)
};

bool SetAutostart(bool enable);
AutostartState GetAutostartState();

}  // namespace oled
