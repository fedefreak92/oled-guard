// Stato dell'oscuramento condiviso fra il thread UI (che lo scrive) e il thread di
// render (che lo legge a ogni ciclo); la mappa dei livelli viaggia al contrario.
#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

#include "dim_model.h"
#include "dim_params.h"

namespace oled {

struct DimInputs {
  DimParams params;
  bool suspended = false;
  float sdr_white_scrgb = 2.5f;  // bianco SDR in scRGB (1 = 80 nit); usato solo in HDR
  double highlight_until_ms = 0;
  bool watch = false;    // la scheda vuole la mappa
  std::vector<uint8_t> active_zones;  // finestre attive (sotto il mouse, in primo piano)
  uint64_t version = 0;  // cambia a ogni modifica (esclusa la mappa)
};

struct DimMap {
  std::vector<float> levels;
  int cols = 0;
  int rows = 0;
  double time_ms = 0;
};

// Mappa delle zone (0 desktop/barra, 1 app), calcolata dal thread UI.
struct DimZones {
  std::vector<uint8_t> cells;
  int cols = 0;
  int rows = 0;
  uint64_t version = 0;
};

class SharedDim {
 public:
  // La versione cambia solo se la mappa e' diversa dalla precedente.
  void SetZones(std::vector<uint8_t> cells, int cols, int rows);
  DimZones GetZones();
  uint64_t ZonesVersion();
  // Stato salvato prima di distruggere il dimmer (pausa), ripreso al riavvio.
  void SaveState(DimSavedState state);
  std::optional<DimSavedState> TakeSavedState();

  // Incrementa la versione se qualcosa e' cambiato (la versione passata e' ignorata).
  void Set(const DimInputs& in);
  DimInputs Get();
  void PublishMap(DimMap map);
  // L'ultima mappa pubblicata, una volta sola.
  std::optional<DimMap> TakeMap();

 private:
  std::mutex mutex_;
  DimInputs inputs_;
  std::optional<DimMap> map_;
  DimZones zones_;
  std::optional<DimSavedState> saved_;
};

}  // namespace oled
