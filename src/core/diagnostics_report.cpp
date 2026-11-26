#include "diagnostics_report.h"

#include <algorithm>
#include <sstream>

namespace oled {
namespace {

std::string Join(const std::vector<std::string>& items, const char* sep) {
  std::string out;
  for (const auto& s : items) {
    if (!out.empty()) out += sep;
    out += s;
  }
  return out;
}

}  // namespace

std::string RenderStatsLine(const RenderCounters& c) {
  return "render: last 10 s: " + std::to_string(c.last_copied) + " frames copied, " + std::to_string(c.last_skipped) +
         " skipped; since start (" + std::to_string(c.measured_s) + " s): " + std::to_string(c.total_copied) +
         " frames copied, " + std::to_string(c.total_skipped) + " skipped; sessions started " +
         std::to_string(c.sessions);
}

std::string BuildDiagnostics(const DiagnosticsInput& in) {
  std::ostringstream o;
  o << "OLED Guard diagnostics\n"
    << "Read this file before sharing it: the configuration lists the app names in your rules.\n\n";

  o << "## System\n"
    << "Version: " << (in.version.empty() ? "unknown" : in.version) << " (commit "
    << (in.commit.empty() ? "unknown" : in.commit) << ")\n"
    << "Windows build: " << in.windows_build << "\n"
    << "Language: " << (in.language.empty() ? "none" : in.language) << " (Windows: "
    << (in.system_languages.empty() ? "none" : Join(in.system_languages, ", ")) << ")\n\n";

  o << "## GPUs\n";
  if (in.gpus.empty()) o << "none\n";
  for (const auto& g : in.gpus) o << "- " << g.name << " (driver " << (g.driver.empty() ? "unknown" : g.driver) << ")\n";
  o << "\n";

  o << "## Displays\n";
  if (in.monitors.empty()) o << "none\n";
  for (const auto& m : in.monitors) {
    const MonitorItem& it = m.item;
    o << "- " << (it.is_protected ? "[protected] " : "") << it.name << " " << it.device << ": " << it.width << "x"
      << it.height;
    if (it.refresh_hz > 0) o << " " << it.refresh_hz << " Hz";
    o << " " << (it.hdr ? "HDR" : "SDR");
    if (!it.connection.empty()) o << " " << it.connection;
    if (it.primary) o << " main";
    if (m.sdr_white_nits > 0) o << ", SDR white " << static_cast<int>(m.sdr_white_nits + 0.5) << " nits";
    o << ", rotation " << m.rotation_deg;
    if (!m.adapter.empty()) o << ", adapter " << m.adapter;
    o << "\n";
  }
  o << "\n";

  o << "## Config\n" << (in.config_json.empty() ? "none" : in.config_json) << "\n\n";

  o << "## Render\n";
  if (in.render_stats.empty()) o << "none\n";
  for (const auto& line : in.render_stats) o << line << "\n";
  o << "\n";

  const size_t count = std::min(in.log_lines.size(), kDiagnosticsLogLines);
  o << "## Log (last " << count << " lines)\n";
  for (size_t i = in.log_lines.size() - count; i < in.log_lines.size(); ++i) o << in.log_lines[i] << "\n";
  return o.str();
}

}  // namespace oled
