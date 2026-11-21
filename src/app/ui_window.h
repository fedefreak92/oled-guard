// Finestra dell'interfaccia: WebView2 creato all'apertura e distrutto alla chiusura.
// Tutto avviene sul thread UI con callback asincrone: il thread non attende mai WebView2.
#pragma once

#include <windows.h>
#include <wrl/client.h>

#include <WebView2.h>

#include <functional>
#include <optional>
#include <string>

namespace oled {

class UiWindow {
 public:
  using MessageHandler = std::function<void(const std::string& utf8_json)>;
  using ClosedHandler = std::function<void(const RECT& last_rect)>;

  ~UiWindow() { Close(); }

  // Se e' gia' aperta la porta in primo piano. `rect`: posizione salvata (opzionale).
  bool Open(HINSTANCE instance, MessageHandler on_message, ClosedHandler on_closed, bool devtools,
            std::optional<RECT> rect);
  void Close();
  bool IsOpen() const { return hwnd_ != nullptr; }
  bool IsReady() const { return ready_; }
  // La pagina ha inviato "ready": da qui in poi i messaggi arrivano a destinazione.
  void MarkReady() { ready_ = true; }
  void Post(const std::string& utf8_json);
  void Minimize();
  void ToggleMaximize();
  HWND Hwnd() const { return hwnd_; }

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
  LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);
  void CreateWebView(unsigned generation);
  void OnControllerCreated(ICoreWebView2Controller* controller);
  void ResizeWebView();
  int TopResizeBorder() const;

  template <class T>
  using ComPtr = Microsoft::WRL::ComPtr<T>;

  HWND hwnd_ = nullptr;
  ComPtr<ICoreWebView2Environment> env_;
  ComPtr<ICoreWebView2Controller> controller_;
  ComPtr<ICoreWebView2> webview_;
  MessageHandler on_message_;
  ClosedHandler on_closed_;
  bool devtools_ = false;
  bool ready_ = false;
  unsigned generation_ = 0;  // scarta le callback di una finestra gia' chiusa
};

}  // namespace oled
