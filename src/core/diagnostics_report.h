// Testo della diagnostica da allegare alle segnalazioni: in inglese, leggibile, nulla
// viene inviato. Logica pura: i dati li raccoglie l'app.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ui_protocol.h"

namespace oled {

struct DiagGpu {
  std::string name;
  std::string driver;
};

struct DiagMonitor {
  MonitorItem item;
  double sdr_white_nits = 0;  // 0 = sconosciuto
  int rotation_deg = 0;
  std::string adapter;
};

struct DiagnosticsInput {
  std::string version, commit;
  uint32_t windows_build = 0;
  std::string language;                       // lingua in uso
  std::vector<std::string> system_languages;  // lingue preferite di Windows
  std::vector<DiagGpu> gpus;
  std::vector<DiagMonitor> monitors;
  std::string config_json;
  std::vector<std::string> render_stats;
  std::vector<std::string> log_lines;  // dal piu' vecchio
};

inline constexpr size_t kDiagnosticsLogLines = 500;

// Contatori del thread di disegno: l'ultima finestra di 10 s e i totali dall'avvio
// dell'app (aggiornati alla fine di ogni finestra).
struct RenderCounters {
  uint64_t last_copied = 0, last_skipped = 0;
  uint64_t total_copied = 0, total_skipped = 0;
  int sessions = 0;
  uint64_t measured_s = 0;  // secondi coperti dai totali
};

// Riga "render: ..." della sezione "## Render".
std::string RenderStatsLine(const RenderCounters& c);

// Sezioni "## System", "## GPUs", "## Displays", "## Config", "## Render",
// "## Log (last N lines)"; del log solo le ultime kDiagnosticsLogLines righe.
std::string BuildDiagnostics(const DiagnosticsInput& in);

}  // namespace oled
