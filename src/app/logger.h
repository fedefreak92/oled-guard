// Log su file con rotazione semplice (1 MB, 3 file). Thread-safe.
#pragma once

#include <filesystem>
#include <format>
#include <string_view>
#include <utility>

namespace oled {

enum class LogLevel { Debug, Info, Warn, Error };

void LogInit(const std::filesystem::path& dir, LogLevel min_level);
void LogSetLevel(LogLevel level);
bool LogEnabled(LogLevel level);
void Log(LogLevel level, std::string_view utf8);
void LogShutdown();

template <class... A>
void LogDebug(std::format_string<A...> fmt, A&&... args) {
  if (LogEnabled(LogLevel::Debug)) Log(LogLevel::Debug, std::format(fmt, std::forward<A>(args)...));
}
template <class... A>
void LogInfo(std::format_string<A...> fmt, A&&... args) {
  if (LogEnabled(LogLevel::Info)) Log(LogLevel::Info, std::format(fmt, std::forward<A>(args)...));
}
template <class... A>
void LogWarn(std::format_string<A...> fmt, A&&... args) {
  if (LogEnabled(LogLevel::Warn)) Log(LogLevel::Warn, std::format(fmt, std::forward<A>(args)...));
}
template <class... A>
void LogError(std::format_string<A...> fmt, A&&... args) {
  Log(LogLevel::Error, std::format(fmt, std::forward<A>(args)...));
}

// HRESULT in esadecimale per i messaggi di log.
inline std::string Hr(long hr) { return std::format("0x{:08X}", static_cast<unsigned long>(hr)); }

}  // namespace oled
