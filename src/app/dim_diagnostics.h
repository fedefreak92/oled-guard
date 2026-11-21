// Diagnostica dell'oscuramento (--dim-diagnostics): istantanee dell'immagine catturata
// e di quella mandata a schermo dall'overlay, salvate come BMP nella cartella dei log.
// Solo per verifiche: legge pixel del desktop, quindi mai attiva di default.
#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <filesystem>

namespace oled {

class FrameSnapshot {
 public:
  // Copia `source` in una texture di appoggio (asincrona).
  void Request(ID3D11Device* device, ID3D11Texture2D* source);
  bool Pending() const { return pending_; }
  // Se la copia e' pronta la converte in BMP 24 bit su un thread a parte e ritorna true.
  // white: valore del bianco SDR (scRGB) per convertire l'HDR; ignorato in SDR.
  bool TryWrite(const std::filesystem::path& file, float white);

 private:
  Microsoft::WRL::ComPtr<ID3D11Device> device_;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> staging_;
  bool pending_ = false;
};

}  // namespace oled
