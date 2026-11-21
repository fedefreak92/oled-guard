#include "tray.h"

#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <vector>

namespace oled {
namespace {

void CopyText(wchar_t* dst, size_t capacity, std::wstring_view text) {
  const size_t n = std::min(text.size(), capacity - 1);
  std::wmemcpy(dst, text.data(), n);
  dst[n] = L'\0';
}

// Copertura (0..1) di un disco pieno, con bordo sfumato di 1 px.
double Disc(double dx, double dy, double r) { return std::clamp(r - std::sqrt(dx * dx + dy * dy) + 0.5, 0.0, 1.0); }

}  // namespace

HICON CreateAppIcon(int size) {
  std::vector<uint32_t> pixels(static_cast<size_t>(size) * size);
  const double c = size / 2.0;
  const double outer = size * 0.44;
  const double inner = size * 0.30;
  const double dot = size * 0.12;
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      const double px = x + 0.5 - c;
      const double py = y + 0.5 - c;
      const double ring = std::max(0.0, Disc(px, py, outer) - Disc(px, py, inner));
      const double centre = Disc(px - size * 0.06, py - size * 0.06, dot);
      const double a = std::max(ring, centre);
      // Colore premoltiplicato: verde acqua per l'anello, bianco per il punto.
      const double r = ring * 0x20 + centre * 0xFF;
      const double g = ring * 0xC8 + centre * 0xFF;
      const double b = ring * 0xB0 + centre * 0xFF;
      const auto ch = [](double v) { return static_cast<uint32_t>(std::clamp(v, 0.0, 255.0)); };
      pixels[static_cast<size_t>(y) * size + x] = (ch(a * 255) << 24) | (ch(std::min(r, a * 255)) << 16) |
                                                  (ch(std::min(g, a * 255)) << 8) | ch(std::min(b, a * 255));
    }
  }
  BITMAPV5HEADER bi{};
  bi.bV5Size = sizeof(bi);
  bi.bV5Width = size;
  bi.bV5Height = -size;
  bi.bV5Planes = 1;
  bi.bV5BitCount = 32;
  bi.bV5Compression = BI_BITFIELDS;
  bi.bV5RedMask = 0x00FF0000;
  bi.bV5GreenMask = 0x0000FF00;
  bi.bV5BlueMask = 0x000000FF;
  bi.bV5AlphaMask = 0xFF000000;
  void* bits = nullptr;
  HDC dc = GetDC(nullptr);
  HBITMAP color = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS, &bits, nullptr, 0);
  ReleaseDC(nullptr, dc);
  if (!color) return LoadIconW(nullptr, IDI_APPLICATION);
  std::memcpy(bits, pixels.data(), pixels.size() * sizeof(uint32_t));
  HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
  ICONINFO ii{TRUE, 0, 0, mask, color};
  HICON icon = CreateIconIndirect(&ii);
  DeleteObject(color);
  DeleteObject(mask);
  return icon ? icon : LoadIconW(nullptr, IDI_APPLICATION);
}

bool Tray::Create(HWND owner, UINT callback_msg, HICON icon) {
  Destroy();
  data_ = {};
  data_.cbSize = sizeof(data_);
  data_.hWnd = owner;
  data_.uID = 1;
  data_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
  data_.uCallbackMessage = callback_msg;
  data_.hIcon = icon;
  CopyText(data_.szTip, ARRAYSIZE(data_.szTip), L"OLED Guard");
  return Recreate();
}

bool Tray::Recreate() {
  if (!data_.hWnd) return false;
  Shell_NotifyIconW(NIM_DELETE, &data_);
  added_ = Shell_NotifyIconW(NIM_ADD, &data_) != FALSE;
  if (added_) {
    data_.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &data_);
  }
  return added_;
}

void Tray::Destroy() {
  if (added_) Shell_NotifyIconW(NIM_DELETE, &data_);
  added_ = false;
}

void Tray::SetTooltip(std::wstring_view text) {
  CopyText(data_.szTip, ARRAYSIZE(data_.szTip), text);
  data_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
  if (added_) Shell_NotifyIconW(NIM_MODIFY, &data_);
}

void Tray::ShowBalloon(std::wstring_view title, std::wstring_view text) {
  NOTIFYICONDATAW n = data_;
  n.uFlags = NIF_INFO;
  n.dwInfoFlags = NIIF_INFO;
  CopyText(n.szInfoTitle, ARRAYSIZE(n.szInfoTitle), title);
  CopyText(n.szInfo, ARRAYSIZE(n.szInfo), text);
  if (added_) Shell_NotifyIconW(NIM_MODIFY, &n);
}

}  // namespace oled
