#include "identify_window.h"

#include <string>

namespace oled {
namespace {

constexpr wchar_t kIdentifyClass[] = L"OledGuardIdentify";
constexpr UINT_PTR kCloseTimer = 1;
constexpr UINT kShowMs = 3000;

// Testo da disegnare: numero e nome, salvati nella finestra.
struct IdentifyText {
  std::wstring number;
  std::wstring name;
};

void Paint(HWND hwnd) {
  PAINTSTRUCT ps;
  HDC dc = BeginPaint(hwnd, &ps);
  RECT r{};
  GetClientRect(hwnd, &r);
  FillRect(dc, &r, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
  const auto* text = reinterpret_cast<const IdentifyText*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (text) {
    const int height = r.bottom - r.top;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    HFONT number_font = CreateFontW(-height / 3, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HFONT label_font = CreateFontW(-height / 18, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HGDIOBJ old = SelectObject(dc, number_font);
    RECT top = r;
    top.bottom = r.top + height * 2 / 3;
    DrawTextW(dc, text->number.c_str(), -1, &top, DT_CENTER | DT_BOTTOM | DT_SINGLELINE);
    SelectObject(dc, label_font);
    RECT bottom = r;
    bottom.top = top.bottom + height / 30;
    DrawTextW(dc, text->name.c_str(), -1, &bottom, DT_CENTER | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(dc, old);
    DeleteObject(number_font);
    DeleteObject(label_font);
  }
  EndPaint(hwnd, &ps);
}

LRESULT CALLBACK IdentifyProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_PAINT:
      Paint(hwnd);
      return 0;
    case WM_TIMER:
      if (wp == kCloseTimer) DestroyWindow(hwnd);
      return 0;
    case WM_NCDESTROY:
      delete reinterpret_cast<IdentifyText*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
      return 0;
    default:
      return DefWindowProcW(hwnd, msg, wp, lp);
  }
}

}  // namespace

void ShowMonitorIdentifiers(HINSTANCE instance, const std::vector<MonitorInfo>& monitors) {
  static bool registered = false;
  if (!registered) {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = IdentifyProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kIdentifyClass;
    registered = RegisterClassExW(&wc) != 0;
  }
  for (size_t i = 0; i < monitors.size(); ++i) {
    const RECT& r = monitors[i].rect;
    const DWORD ex = WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
    HWND hwnd = CreateWindowExW(ex, kIdentifyClass, L"OLED Guard", WS_POPUP, r.left, r.top, r.right - r.left,
                                r.bottom - r.top, nullptr, nullptr, instance, nullptr);
    if (!hwnd) continue;
    auto* text = new IdentifyText{std::to_wstring(i + 1), monitors[i].friendly_name};
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(text));
    SetLayeredWindowAttributes(hwnd, 0, 200, LWA_ALPHA);
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    SetTimer(hwnd, kCloseTimer, kShowMs, nullptr);
  }
}

}  // namespace oled
