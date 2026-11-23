// Esporta diagnostica: raccoglie GPU, coda del log e build di Windows, scrive il testo in
// Documenti e lo seleziona in Esplora file. Nulla viene inviato.
#pragma once

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "diagnostics_report.h"

namespace oled {

// Build di Windows letta con RtlGetVersion (GetVersionEx dipende dal manifest). 0 se ignota.
uint32_t WindowsBuildNumber();

// Schede video con la versione del driver (dalla UMD, come la mostra Gestione dispositivi).
std::vector<DiagGpu> ListGpus();

// Nome della scheda video con questo LUID, "" se non trovata.
std::string AdapterName(const LUID& luid);

// Ultime righe del log corrente (dal piu' vecchio).
std::vector<std::string> ReadLogTail(size_t max_lines);

// Completa `in` con GPU e log, scrive Documenti\oled-guard-diagnostica-AAAAMMGG-HHMM.txt
// (UTF-8) e restituisce il percorso. Non apre Esplora file: lo fa il chiamante.
std::optional<std::filesystem::path> WriteDiagnostics(DiagnosticsInput in);

// Apre Esplora file con il file selezionato (COM inizializzato nel thread chiamante).
void RevealInExplorer(const std::filesystem::path& file);

}  // namespace oled
