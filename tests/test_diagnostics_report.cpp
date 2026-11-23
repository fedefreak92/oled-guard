#include <string>
#include <vector>

#include "diagnostics_report.h"
#include "test_framework.h"

using oled::BuildDiagnostics;
using oled::DiagnosticsInput;

namespace {

DiagnosticsInput Sample() {
  DiagnosticsInput in;
  in.version = "1.2.0";
  in.commit = "abc1234";
  in.windows_build = 26300;
  in.language = "it";
  in.system_languages = {"it-IT", "en-US"};
  in.gpus = {{"Example GPU", "32.0.15.1234"}};
  oled::DiagMonitor tv;
  tv.item.device = "\\\\.\\DISPLAY1";
  tv.item.name = "OLED TV";
  tv.item.width = 3840;
  tv.item.height = 2160;
  tv.item.refresh_hz = 120;
  tv.item.hdr = true;
  tv.item.connection = "HDMI";
  tv.item.is_protected = true;
  tv.sdr_white_nits = 240;
  tv.adapter = "Example GPU";
  oled::DiagMonitor desk;
  desk.item.device = "\\\\.\\DISPLAY2";
  desk.item.name = "Monitor 27";
  desk.item.width = 2560;
  desk.item.height = 1440;
  in.monitors = {tv, desk};
  in.config_json = "{\n  \"monitor\": \"auto\"\n}";
  in.render_stats = {"frames copied 377, skipped 0"};
  in.log_lines = {"line 1", "line 2"};
  return in;
}

size_t Pos(const std::string& text, const std::string& what) { return text.find(what); }

}  // namespace

TEST(diagnostics_sections_in_order) {
  const std::string text = BuildDiagnostics(Sample());
  const char* sections[] = {"## System", "## GPUs", "## Displays", "## Config", "## Render", "## Log (last 2 lines)"};
  size_t last = 0;
  for (const char* s : sections) {
    const size_t p = Pos(text, s);
    CHECK(p != std::string::npos);
    if (p == std::string::npos) return;
    CHECK(p >= last);
    last = p;
  }
  CHECK(Pos(text, "1.2.0") != std::string::npos);
  CHECK(Pos(text, "abc1234") != std::string::npos);
  CHECK(Pos(text, "26300") != std::string::npos);
  CHECK(Pos(text, "it-IT, en-US") != std::string::npos);
  CHECK(Pos(text, "32.0.15.1234") != std::string::npos);
  CHECK(Pos(text, "\"monitor\": \"auto\"") != std::string::npos);
  CHECK(Pos(text, "frames copied 377") != std::string::npos);
}

TEST(diagnostics_keeps_last_500_log_lines) {
  DiagnosticsInput in = Sample();
  in.log_lines.clear();
  for (int i = 1; i <= 800; ++i) in.log_lines.push_back("log " + std::to_string(i));
  const std::string text = BuildDiagnostics(in);
  CHECK(Pos(text, "## Log (last 500 lines)") != std::string::npos);
  CHECK(Pos(text, "log 300\n") == std::string::npos);
  CHECK(Pos(text, "log 301\n") != std::string::npos);
  CHECK(Pos(text, "log 800") != std::string::npos);
}

TEST(diagnostics_marks_protected_display) {
  const std::string text = BuildDiagnostics(Sample());
  const size_t tv = Pos(text, "OLED TV"), desk = Pos(text, "Monitor 27");
  CHECK(tv != std::string::npos && desk != std::string::npos);
  const size_t mark = Pos(text, "[protected]");
  CHECK(mark != std::string::npos && mark < desk);
  CHECK(Pos(text, "3840x2160") != std::string::npos);
  CHECK(Pos(text, "HDR") != std::string::npos);
  CHECK(Pos(text, "SDR white 240 nits") != std::string::npos);
}

TEST(diagnostics_empty_input_is_valid) {
  const std::string text = BuildDiagnostics(DiagnosticsInput{});
  for (const char* s : {"## System", "## GPUs", "## Displays", "## Config", "## Render", "## Log (last 0 lines)"}) {
    CHECK(Pos(text, s) != std::string::npos);
  }
  CHECK(Pos(text, "none") != std::string::npos);
}
