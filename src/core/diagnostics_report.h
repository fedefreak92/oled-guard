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

// Sezioni "## System", "## GPUs", "## Displays", "## Config", "## Render",
// "## Log (last N lines)"; del log solo le ultime kDiagnosticsLogLines righe.
std::string BuildDiagnostics(const DiagnosticsInput& in);

}  // namespace oled
