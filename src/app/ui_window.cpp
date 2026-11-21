#include "ui_window.h"

#include <dwmapi.h>
#include <shellscalingapi.h>
#include <windowsx.h>
#include <wrl/event.h>

#include <filesystem>
#include <system_error>

#include "logger.h"
#include "paths.h"
#include "text.h"

using Microsoft::WRL::Callback;

namespace oled {
namespace {

constexpr wchar_t kUiClass[] = L"OledGuardUi";
constexpr wchar_t kHost[] = L"oledguard.local";
constexpr wchar_t kStartUrl[] = L"https://oledguard.local/index.html";
constexpr wchar_t kAllowedPrefix[] = L"https://oledguard.local/";
constexpr int kWidthDip = 1280;
constexpr int kHeightDip = 800;
constexpr int kMinWidthDip = 1000;
constexpr int kMinHeightDip = 640;
constexpr int kTopBorderDip = 4;  // fascia scoperta in alto per ridimensionare dal bordo superiore

int Scale(int dip, UINT dpi) { return MulDiv(dip, static_cast<int>(dpi), 96); }

}  // namespace

bool UiWindow::Open(HINSTANCE instance, MessageHandler on_message, ClosedHandler on_closed, bool devtools,
                    std::optional<RECT> rect) {
  if (hwnd_) {
    if (IsIconic(hwnd_)) ShowWindow(hwnd_, SW_RESTORE);
    SetForegroundWindow(hwnd_);
    return true;
  }
  on_message_ = std::move(on_message);
  on_closed_ = std::move(on_closed);
  devtools_ = devtools;
  ready_ = false;

  WNDCLASSEXW wc{sizeof(wc)};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = instance;
  wc.lpszClassName = kUiClass;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));  // mai bianco, neanche un istante
  RegisterClassExW(&wc);

  // Dimensione iniziale: posizione salvata se ancora su un monitor, altrimenti centrata
  // sul monitor sotto il cursore.
  RECT r{};
  if (rect && MonitorFromRect(&*rect, MONITOR_DEFAULTTONULL)) {
    r = *rect;
  } else {
    POINT cursor{};
    GetCursorPos(&cursor);
    HMONITOR mon = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(mon, &mi);
    UINT dpi_x = 96, dpi_y = 96;
    GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y);
    const int w = Scale(kWidthDip, dpi_x);
    const int h = Scale(kHeightDip, dpi_x);
    const RECT& wa = mi.rcWork;
    r.left = wa.left + (wa.right - wa.left - w) / 2;
    r.top = wa.top + (wa.bottom - wa.top - h) / 2;
    r.right = r.left + w;
    r.bottom = r.top + h;
  }

  hwnd_ = CreateWindowExW(0, kUiClass, L"OLED Guard", WS_OVERLAPPEDWINDOW, r.left, r.top, r.right - r.left,
                          r.bottom - r.top, nullptr, nullptr, instance, this);
  if (!hwnd_) {
    LogError("Finestra dell'interfaccia non creata: {}", GetLastError());
    return false;
  }
  const BOOL dark = TRUE;
  DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
  const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_DONOTROUND;
  DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
  const COLORREF border = RGB(0x1d, 0x1d, 0x1d);
  DwmSetWindowAttribute(hwnd_, DWMWA_BORDER_COLOR, &border, sizeof(border));
  // Ricalcola la cornice senza barra del titolo prima di mostrare la finestra.
  SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
  ShowWindow(hwnd_, SW_SHOWNORMAL);
  SetForegroundWindow(hwnd_);

  CreateWebView(++generation_);
  return true;
}

void UiWindow::CreateWebView(unsigned generation) {
  const std::wstring data_dir = LocalAppDataDir().wstring() + L"\\webview2";
  const HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
      nullptr, data_dir.c_str(), nullptr,
      Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
          [this, generation](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
            if (generation != generation_ || !hwnd_) return S_OK;  // finestra gia' chiusa
            if (FAILED(result) || !env) {
              Fail("ambiente WebView2 non creato (" + Hr(result) + "): il runtime WebView2 e' installato?");
              return S_OK;
            }
            env_ = env;
            env->CreateCoreWebView2Controller(
                hwnd_, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                           [this, generation](HRESULT res, ICoreWebView2Controller* controller) -> HRESULT {
                             if (generation != generation_ || !hwnd_) {
                               if (controller) controller->Close();
                               return S_OK;
                             }
                             if (FAILED(res) || !controller) {
                               Fail("controller WebView2 non creato (" + Hr(res) + ")");
                               return S_OK;
                             }
                             OnControllerCreated(controller);
                             return S_OK;
                           })
                           .Get());
            return S_OK;
          })
          .Get());
  if (FAILED(hr)) Fail("WebView2 non avviato (" + Hr(hr) + "): il runtime WebView2 e' installato?");
}

void UiWindow::Fail(const std::string& reason) {
  LogError("Interfaccia: {}", reason);
  if (on_failed_) on_failed_(reason);
  // Chiusura rimandata: si puo' essere dentro una callback di WebView2.
  if (hwnd_) PostMessageW(hwnd_, WM_CLOSE, 0, 0);
}

void UiWindow::OnControllerCreated(ICoreWebView2Controller* controller) {
  controller_ = controller;
  // Sfondo nero prima di qualsiasi contenuto: niente lampo bianco.
  ComPtr<ICoreWebView2Controller2> controller2;
  if (SUCCEEDED(controller_.As(&controller2))) controller2->put_DefaultBackgroundColor({255, 0, 0, 0});
  controller_->get_CoreWebView2(&webview_);
  ResizeWebView();

  ComPtr<ICoreWebView2Settings> settings;
  webview_->get_Settings(&settings);
  settings->put_AreDefaultContextMenusEnabled(FALSE);
  settings->put_IsZoomControlEnabled(FALSE);
  settings->put_IsStatusBarEnabled(FALSE);
  settings->put_AreDevToolsEnabled(devtools_ ? TRUE : FALSE);
  settings->put_IsBuiltInErrorPageEnabled(FALSE);
  ComPtr<ICoreWebView2Settings3> settings3;
  if (SUCCEEDED(settings.As(&settings3))) settings3->put_AreBrowserAcceleratorKeysEnabled(devtools_ ? TRUE : FALSE);
  ComPtr<ICoreWebView2Settings9> settings9;
  if (SUCCEEDED(settings.As(&settings9))) settings9->put_IsNonClientRegionSupportEnabled(TRUE);

  // Solo i file dell'interfaccia accanto all'eseguibile.
  const std::wstring ui_dir = ExecutablePath().parent_path().wstring() + L"\\ui";
  std::error_code ec;
  if (!std::filesystem::exists(std::filesystem::path(ui_dir) / L"index.html", ec)) {
    Fail("file dell'interfaccia mancanti in " + Narrow(ui_dir));
    return;
  }
  ComPtr<ICoreWebView2_3> webview3;
  if (SUCCEEDED(webview_.As(&webview3))) {
    webview3->SetVirtualHostNameToFolderMapping(kHost, ui_dir.c_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
  }

  EventRegistrationToken token{};
  webview_->add_NavigationStarting(
      Callback<ICoreWebView2NavigationStartingEventHandler>(
          [](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
            LPWSTR uri = nullptr;
            args->get_Uri(&uri);
            const bool allowed = uri && wcsncmp(uri, kAllowedPrefix, wcslen(kAllowedPrefix)) == 0;
            if (!allowed) {
              LogWarn("Interfaccia: navigazione bloccata verso {}", uri ? Narrow(uri) : std::string("?"));
              args->put_Cancel(TRUE);
            }
            CoTaskMemFree(uri);
            return S_OK;
          })
          .Get(),
      &token);
  webview_->add_NewWindowRequested(
      Callback<ICoreWebView2NewWindowRequestedEventHandler>(
          [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
            args->put_Handled(TRUE);  // nessuna finestra nuova
            return S_OK;
          })
          .Get(),
      &token);
  webview_->add_WebMessageReceived(
      Callback<ICoreWebView2WebMessageReceivedEventHandler>(
          [this](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
            LPWSTR source = nullptr;
            args->get_Source(&source);
            const bool trusted = source && wcsncmp(source, kAllowedPrefix, wcslen(kAllowedPrefix)) == 0;
            CoTaskMemFree(source);
            LPWSTR text = nullptr;
            if (trusted && SUCCEEDED(args->TryGetWebMessageAsString(&text)) && text && on_message_) {
              const std::string message = Narrow(text);
              CoTaskMemFree(text);
              on_message_(message);
            } else if (text) {
              CoTaskMemFree(text);
            }
            return S_OK;
          })
          .Get(),
      &token);
  webview_->add_ProcessFailed(
      Callback<ICoreWebView2ProcessFailedEventHandler>(
          [this](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs* args) -> HRESULT {
            COREWEBVIEW2_PROCESS_FAILED_KIND kind{};
            args->get_ProcessFailedKind(&kind);
            if (kind == COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED) {
              Fail("il processo principale di WebView2 si e' chiuso");
            } else if (kind == COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED ||
                       kind == COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_UNRESPONSIVE) {
              // Cade solo la pagina: la si ricarica, e lei invia di nuovo "ready".
              LogWarn("Interfaccia: pagina caduta (tipo {}), la ricarico", static_cast<int>(kind));
              ready_ = false;
              if (webview_) webview_->Reload();
            } else {
              LogWarn("Interfaccia: processo WebView2 terminato (tipo {})", static_cast<int>(kind));
            }
            return S_OK;
          })
          .Get(),
      &token);

  webview_->Navigate(kStartUrl);
}

void UiWindow::Post(const std::string& utf8_json) {
  if (!webview_ || !ready_) return;
  webview_->PostWebMessageAsJson(Widen(utf8_json).c_str());
}

void UiWindow::Minimize() {
  if (hwnd_) ShowWindow(hwnd_, SW_MINIMIZE);
}

void UiWindow::ToggleMaximize() {
  if (hwnd_) ShowWindow(hwnd_, IsZoomed(hwnd_) ? SW_RESTORE : SW_MAXIMIZE);
}

void UiWindow::Close() {
  if (!hwnd_) return;
  ++generation_;
  RECT last{};
  WINDOWPLACEMENT wp{sizeof(wp)};
  if (GetWindowPlacement(hwnd_, &wp)) last = wp.rcNormalPosition;
  if (controller_) controller_->Close();  // termina i processi del browser
  webview_.Reset();
  controller_.Reset();
  env_.Reset();
  ready_ = false;
  HWND hwnd = hwnd_;
  hwnd_ = nullptr;
  DestroyWindow(hwnd);
  if (on_closed_) on_closed_(last);
}

int UiWindow::TopResizeBorder() const {
  if (!hwnd_ || IsZoomed(hwnd_)) return 0;
  return Scale(kTopBorderDip, GetDpiForWindow(hwnd_));
}

void UiWindow::ResizeWebView() {
  if (!controller_ || !hwnd_) return;
  RECT client{};
  GetClientRect(hwnd_, &client);
  client.top += TopResizeBorder();
  controller_->put_Bounds(client);
}

LRESULT CALLBACK UiWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
  }
  auto* self = reinterpret_cast<UiWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (self && self->hwnd_ == hwnd) return self->HandleMessage(msg, wp, lp);
  return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT UiWindow::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_NCCALCSIZE:
      if (wp) {
        // Si toglie solo la barra del titolo: i bordi di ridimensionamento invisibili
        // di Windows restano a sinistra, a destra e in basso.
        auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
        const LONG top = params->rgrc[0].top;
        const LRESULT result = DefWindowProcW(hwnd_, msg, wp, lp);
        params->rgrc[0].top = top;
        if (IsZoomed(hwnd_)) {
          // Massimizzata, la finestra sborda dallo schermo dello spessore della cornice.
          const UINT dpi = GetDpiForWindow(hwnd_);
          params->rgrc[0].top +=
              GetSystemMetricsForDpi(SM_CYFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
        }
        return result;
      }
      break;
    case WM_NCHITTEST: {
      const LRESULT hit = DefWindowProcW(hwnd_, msg, wp, lp);
      if (hit == HTCLIENT) {
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd_, &pt);
        if (pt.y < TopResizeBorder()) return HTTOP;
      }
      return hit;
    }
    case WM_GETMINMAXINFO: {
      auto* info = reinterpret_cast<MINMAXINFO*>(lp);
      const UINT dpi = GetDpiForWindow(hwnd_);
      info->ptMinTrackSize = {Scale(kMinWidthDip, dpi), Scale(kMinHeightDip, dpi)};
      return 0;
    }
    case WM_SIZE:
      ResizeWebView();
      return 0;
    case WM_MOVE:
      if (controller_) controller_->NotifyParentWindowPositionChanged();
      return 0;
    case WM_DPICHANGED: {
      const auto* suggested = reinterpret_cast<const RECT*>(lp);
      SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                   suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
      return 0;
    }
    case WM_CLOSE:
      Close();
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd_, msg, wp, lp);
}

}  // namespace oled
