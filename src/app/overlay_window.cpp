#include "overlay_window.h"

#include "logger.h"

namespace oled {
namespace {

constexpr wchar_t kOverlayClass[] = L"OledGuardOverlay";

LRESULT CALLBACK OverlayProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_NCHITTEST:
      return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:
      return MA_NOACTIVATE;
    case WM_ERASEBKGND:
      return 1;
    default:
      return DefWindowProcW(hwnd, msg, wp, lp);
  }
}

}  // namespace

bool OverlayWindow::Create(HINSTANCE instance, const RECT& r) {
  Destroy();
  WNDCLASSEXW wc{sizeof(wc)};
  wc.lpfnWndProc = OverlayProc;
  wc.hInstance = instance;
  wc.lpszClassName = kOverlayClass;
  wc.hCursor = nullptr;  // il cursore resta quello delle finestre sotto
  RegisterClassExW(&wc);

  // LAYERED + TRANSPARENT: i clic passano alle finestre sotto, anche di altri processi.
  // NOREDIRECTIONBITMAP: il contenuto arriva solo da DirectComposition; senza
  // contenuto la finestra e' invisibile e si vede il desktop reale.
  const DWORD ex = WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW |
                   WS_EX_NOREDIRECTIONBITMAP;
  hwnd_ = CreateWindowExW(ex, kOverlayClass, L"OLED Guard overlay", WS_POPUP, r.left, r.top, r.right - r.left,
                          r.bottom - r.top, nullptr, nullptr, instance, nullptr);
  if (!hwnd_) {
    LogError("CreateWindowEx overlay fallito: {}", GetLastError());
    return false;
  }
  SetLayeredWindowAttributes(hwnd_, 0, 255, LWA_ALPHA);
  if (!SetWindowDisplayAffinity(hwnd_, WDA_EXCLUDEFROMCAPTURE)) {
    LogError("SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE) fallito: {}", GetLastError());
    Destroy();
    return false;
  }
  ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
  ReassertTopmost();
  return true;
}

void OverlayWindow::Destroy() {
  if (hwnd_) DestroyWindow(hwnd_);
  hwnd_ = nullptr;
}

void OverlayWindow::ReassertTopmost() {
  if (!hwnd_) return;
  SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

}  // namespace oled
