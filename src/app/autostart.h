// Avvio automatico con Windows: valore nella chiave Run dell'utente corrente.
#pragma once

namespace oled {

bool SetAutostart(bool enable);
bool IsAutostartEnabled();

}  // namespace oled
