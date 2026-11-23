#include "diagnostics_export.h"

#include <dxgi.h>
#include <shlobj.h>
#include <wrl/client.h>

#include <deque>
#include <fstream>

#include "logger.h"
#include "paths.h"
#include "text.h"

using Microsoft::WRL::ComPtr;

namespace oled {

uint32_t WindowsBuildNumber() {
  using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
  const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  const auto get = ntdll ? reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion")) : nullptr;
  RTL_OSVERSIONINFOW info{};
  info.dwOSVersionInfoSize = sizeof(info);
  if (!get || get(&info) != 0) return 0;
  return info.dwBuildNumber;
}

namespace {

template <typename Fn>
void ForEachAdapter(Fn fn) {
  ComPtr<IDXGIFactory1> factory;
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return;
  ComPtr<IDXGIAdapter1> adapter;
  for (UINT a = 0; factory->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND; ++a) {
    DXGI_ADAPTER_DESC1 desc{};
    if (SUCCEEDED(adapter->GetDesc1(&desc)) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) fn(adapter.Get(), desc);
    adapter.Reset();
  }
}

}  // namespace

std::vector<DiagGpu> ListGpus() {
  std::vector<DiagGpu> out;
  ForEachAdapter([&](IDXGIAdapter1* adapter, const DXGI_ADAPTER_DESC1& desc) {
    DiagGpu g;
    g.name = Narrow(desc.Description);
    LARGE_INTEGER umd{};
    if (SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umd))) {
      const auto hi = static_cast<uint32_t>(umd.HighPart), lo = static_cast<uint32_t>(umd.LowPart);
      g.driver = std::to_string(hi >> 16) + "." + std::to_string(hi & 0xFFFF) + "." + std::to_string(lo >> 16) + "." +
                 std::to_string(lo & 0xFFFF);
    }
    out.push_back(std::move(g));
  });
  return out;
}

std::string AdapterName(const LUID& luid) {
  std::string name;
  ForEachAdapter([&](IDXGIAdapter1*, const DXGI_ADAPTER_DESC1& desc) {
    if (desc.AdapterLuid.LowPart == luid.LowPart && desc.AdapterLuid.HighPart == luid.HighPart) {
      name = Narrow(desc.Description);
    }
  });
  return name;
}

std::vector<std::string> ReadLogTail(size_t max_lines) {
  std::ifstream in(LogDir() / L"oled-guard.log", std::ios::binary);
  std::deque<std::string> lines;
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    lines.push_back(std::move(line));
    if (lines.size() > max_lines) lines.pop_front();
  }
  return {lines.begin(), lines.end()};
}

std::optional<std::filesystem::path> WriteDiagnostics(DiagnosticsInput in) {
  in.gpus = ListGpus();
  in.log_lines = ReadLogTail(kDiagnosticsLogLines);
  PWSTR raw = nullptr;
  std::filesystem::path dir;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_CREATE, nullptr, &raw))) dir = raw;
  CoTaskMemFree(raw);
  if (dir.empty()) dir = AppDataDir();
  SYSTEMTIME t{};
  GetLocalTime(&t);
  wchar_t name[64];
  swprintf_s(name, L"oled-guard-diagnostica-%04u%02u%02u-%02u%02u.txt", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute);
  const std::filesystem::path file = dir / name;
  std::ofstream out(file, std::ios::binary | std::ios::trunc);
  if (!out) {
    LogWarn("Diagnostica non scritta in {}", Narrow(file.wstring()));
    return std::nullopt;
  }
  out << BuildDiagnostics(in);
  if (!out) return std::nullopt;
  LogInfo("Diagnostica esportata in {}", Narrow(file.wstring()));
  return file;
}

void RevealInExplorer(const std::filesystem::path& file) {
  PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(file.c_str());
  if (!pidl) return;
  SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
  ILFree(pidl);
}

}  // namespace oled
