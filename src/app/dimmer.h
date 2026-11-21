// Oscuramento delle aree statiche sulla GPU: possiede la griglia di celle 16x16 px
// (texture), i compute shader e il loro constant buffer. Vive sul thread di render.
// Non scrive nel log: viene usato anche dai test con device WARP.
#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <vector>

#include "dim_params.h"

namespace oled {

// Copia delle griglie per la diagnostica (--dim-diagnostics), niente pixel del desktop.
struct DimDebugGrids {
  std::vector<float> levels;
  std::vector<float> last_change;  // secondi dall'inizio della sessione
  std::vector<uint32_t> flags;
  std::vector<uint32_t> classes;
  double now_s = 0;  // istante della richiesta, stessa scala di last_change
};

class Dimmer {
 public:
  ~Dimmer() { Shutdown(); }

  // Griglia per una cattura width x height nel formato dato. Tutte le celle partono
  // "cambiate adesso" e con livello 0.
  HRESULT Init(ID3D11Device* device, UINT width, UINT height, DXGI_FORMAT format, double now_ms);
  void Shutdown();
  bool Ready() const { return levels_ != nullptr; }
  UINT Cols() const { return cols_; }
  UINT Rows() const { return rows_; }

  // Frame nuovo, da chiamare prima che la cache (frame precedente) venga sovrascritta.
  // sdr_white: valore del bianco nel formato della cattura (1 in SDR).
  HRESULT Detect(ID3D11Texture2D* frame, ID3D11Texture2D* previous_cache, double now_ms, const DimParams& p,
                 float sdr_white);
  // Classificazione e avanzamento dei livelli; dt dal precedente Update (il primo usa 0).
  void Update(double now_ms, const DimParams& p, bool suspended);
  ID3D11ShaderResourceView* LevelsSrv() const { return levels_srv_.Get(); }

  // Lettura asincrona della mappa dei livelli (solo per la miniatura della scheda).
  void RequestReadback();
  // Vero se la copia richiesta e' pronta: riempie `levels` (Cols()*Rows(), riga per riga).
  bool TryReadback(std::vector<float>& levels);
  bool ReadbackPending() const { return readback_pending_; }

  // Diagnostica: copia asincrona di livelli, flag, classi e ultimo cambiamento.
  void RequestDebugReadback(double now_ms);
  bool TryDebugReadback(DimDebugGrids& out);

 private:
  template <class T>
  using ComPtr = Microsoft::WRL::ComPtr<T>;

  struct Grid {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11UnorderedAccessView> uav;
  };

  HRESULT CreateGrid(DXGI_FORMAT format, const void* initial, UINT pixel_bytes, Grid& grid);
  void SetConstants(double now_s, double dt_s, const DimParams& p, float sdr_white, bool suspended, bool first);
  void Run(ID3D11ComputeShader* shader, std::initializer_list<ID3D11ShaderResourceView*> srvs,
           ID3D11UnorderedAccessView* uav0, ID3D11UnorderedAccessView* uav1, UINT groups_x, UINT groups_y);
  double Seconds(double now_ms) const { return (now_ms - epoch_ms_) / 1000.0; }

  ComPtr<ID3D11Device> device_;
  ComPtr<ID3D11DeviceContext> context_;
  ComPtr<ID3D11Buffer> constants_;
  ComPtr<ID3D11ComputeShader> detect_, density_h_, density_v_, max_h_, max_v_, level_;
  Grid last_change_, flags_, sums_, density_, max_tmp_, large_, levels_grid_, class_;
  ID3D11Texture2D* levels_ = nullptr;  // = levels_grid_.texture
  ComPtr<ID3D11ShaderResourceView> levels_srv_;
  ComPtr<ID3D11Texture2D> frame_copy_;  // ripiego se il frame non e' leggibile come SRV
  ComPtr<ID3D11Texture2D> previous_srv_source_;
  ComPtr<ID3D11ShaderResourceView> previous_srv_;
  ComPtr<ID3D11Texture2D> staging_;
  ComPtr<ID3D11Texture2D> debug_staging_[4];  // levels, last_change, flags, classes
  bool debug_pending_ = false;
  double debug_now_s_ = 0;
  bool readback_pending_ = false;
  UINT width_ = 0, height_ = 0, cols_ = 0, rows_ = 0;
  bool sdr_ = false;
  bool first_frame_ = true;
  double epoch_ms_ = 0;
  double last_update_s_ = -1;
  float last_white_ = 1;
};

}  // namespace oled
