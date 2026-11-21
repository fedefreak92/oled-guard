#include "logger.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <mutex>
#include <system_error>

#include "text.h"

namespace oled {
namespace {

constexpr uintmax_t kMaxBytes = 1024 * 1024;

std::mutex g_mutex;
std::FILE* g_file = nullptr;
std::filesystem::path g_dir;
uintmax_t g_size = 0;
std::atomic<int> g_level{static_cast<int>(LogLevel::Info)};

std::filesystem::path FileAt(int index) {
  return g_dir / (index == 0 ? L"oled-guard.log" : L"oled-guard." + std::to_wstring(index) + L".log");
}

void OpenFile() {
  // Condiviso: una seconda istanza (che esce subito) e gli editor possono leggerlo.
  g_file = _wfsopen(FileAt(0).c_str(), L"ab", _SH_DENYNO);
  std::error_code ec;
  g_size = std::filesystem::exists(FileAt(0), ec) ? std::filesystem::file_size(FileAt(0), ec) : 0;
}

void Rotate() {
  if (g_file) std::fclose(g_file);
  g_file = nullptr;
  std::error_code ec;
  // .log -> .1.log -> .2.log: tre file in tutto.
  std::filesystem::remove(FileAt(2), ec);
  std::filesystem::rename(FileAt(1), FileAt(2), ec);
  std::filesystem::rename(FileAt(0), FileAt(1), ec);
  if (ec) {
    // Un altro processo tiene il file senza permettere la rinomina: lo si svuota,
    // cosi' il log resta comunque entro il limite e non si ritenta a ogni riga.
    g_file = _wfsopen(FileAt(0).c_str(), L"wb", _SH_DENYNO);
    g_size = 0;
    return;
  }
  OpenFile();
}

const char* LevelName(LogLevel level) {
  switch (level) {
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info: return "INFO";
    case LogLevel::Warn: return "WARN";
    case LogLevel::Error: return "ERROR";
  }
  return "?";
}

}  // namespace

void LogInit(const std::filesystem::path& dir, LogLevel min_level) {
  std::lock_guard lock(g_mutex);
  g_dir = dir;
  g_level = static_cast<int>(min_level);
  OpenFile();
}

void LogSetLevel(LogLevel level) { g_level = static_cast<int>(level); }

bool LogEnabled(LogLevel level) { return static_cast<int>(level) >= g_level.load(); }

void Log(LogLevel level, std::string_view utf8) {
  if (!LogEnabled(level)) return;
  SYSTEMTIME t;
  GetLocalTime(&t);
  const std::string line = std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03} [{}] {}\r\n", t.wYear, t.wMonth,
                                       t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, LevelName(level), utf8);
  OutputDebugStringW(Widen(line).c_str());

  std::lock_guard lock(g_mutex);
  if (!g_file) return;
  if (g_size + line.size() > kMaxBytes) Rotate();
  if (!g_file) return;
  std::fwrite(line.data(), 1, line.size(), g_file);
  std::fflush(g_file);
  g_size += line.size();
}

void LogShutdown() {
  std::lock_guard lock(g_mutex);
  if (g_file) std::fclose(g_file);
  g_file = nullptr;
}

}  // namespace oled
