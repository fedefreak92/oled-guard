#include "app.h"

#include "logger.h"
#include "paths.h"

namespace oled {
namespace {

constexpr wchar_t kMainClass[] = L"OledGuardMain";
constexpr UINT_PTR kTimerBeat = 1;

LONG WINAPI CrashFilter(EXCEPTION_POINTERS* info) {
  // Il processo termina: il sistema distrugge l'overlay e il desktop torna normale.
  LogError("Eccezione non gestita 0x{:08X} a {}, chiusura",
           static_cast<unsigned long>(info->ExceptionRecord->ExceptionCode),
           info->ExceptionRecord->ExceptionAddress);
  LogShutdown();
  return EXCEPTION_EXECUTE_HANDLER;
}

}  // namespace

int App::Run(HINSTANCE instance) {
  instance_ = instance;
  SetUnhandledExceptionFilter(CrashFilter);
  LogInit(LogDir(), LogLevel::Info);
  LogInfo("OLED Guard avviato");

  if (const auto exit_key = ParseHotkey(config_.hotkey_emergency_exit)) guard_.Start(*exit_key);

  if (!CreateMainWindow()) {
    LogError("Impossibile creare la finestra principale");
    return 1;
  }
  SetTimer(main_, kTimerBeat, 1000, nullptr);
  guard_.BeatMain();

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  guard_.Stop();
  LogInfo("OLED Guard chiuso");
  LogShutdown();
  return 0;
}

bool App::CreateMainWindow() {
  WNDCLASSEXW wc{sizeof(wc)};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = instance_;
  wc.lpszClassName = kMainClass;
  RegisterClassExW(&wc);
  // Finestra top-level mai mostrata: riceve i messaggi broadcast (TaskbarCreated)
  // che una finestra HWND_MESSAGE non riceverebbe.
  main_ = CreateWindowExW(WS_EX_TOOLWINDOW, kMainClass, L"OLED Guard", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                          instance_, this);
  return main_ != nullptr;
}

LRESULT CALLBACK App::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
  }
  auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (app && app->main_ == hwnd) return app->HandleMessage(msg, wp, lp);
  return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT App::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_TIMER:
      if (wp == kTimerBeat) guard_.BeatMain();
      return 0;
    case WM_CLOSE:
      DestroyWindow(main_);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProcW(main_, msg, wp, lp);
  }
}

}  // namespace oled
