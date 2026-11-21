// Modello di riferimento su CPU dell'oscuramento delle aree statiche.
// Non viene usato a runtime (lo fanno gli shader sulla GPU): definisce il
// comportamento, e' coperto dai test e fa da oracolo per i test degli shader.
#pragma once

#include <cstdint>
#include <vector>

#include "dim_params.h"

namespace oled {

// Luminanza lineare di ogni pixel divisa per il bianco SDR (1 = bianco di Windows).
struct LumaImage {
  int width = 0;
  int height = 0;
  std::vector<float> luma;
};

// Zone per cella: 0 desktop e barra delle applicazioni, altrimenti il numero della
// finestra dell'app (1-255). Il movimento intorno conta solo dentro la stessa zona.
inline constexpr uint8_t kDimZoneShell = 0;
inline constexpr uint8_t kDimZoneApp = 1;  // finestra generica (default prima della mappa)

// Stato salvato prima di distruggere l'overlay (pausa), per riprendere dopo.
struct DimSavedState {
  int width = 0;
  int height = 0;
  std::vector<float> ages;       // secondi da cui ogni cella e' ferma
  std::vector<uint32_t> hashes;  // firma dei pixel della cella nell'ultimo frame
};

class DimModel {
 public:
  // Tutte le celle partono "cambiate adesso" e con livello 0. Con `restore` (stesse
  // dimensioni), al primo frame le celle con la stessa firma riprendono la loro eta'.
  DimModel(int width, int height, double now_s, const DimSavedState* restore = nullptr);

  // Zone per cella (Cols()*Rows()); di dimensione sbagliata vengono ignorate.
  // Senza zone tutto conta come finestra di un'app.
  void SetZones(const std::vector<uint8_t>& zones);
  // Zone delle finestre "attive" (sotto il mouse, in primo piano): tornano normali e il
  // loro conteggio riparte da zero finche' restano attive. Lo 0 (desktop) e' ignorato.
  void SetActiveZones(const std::vector<uint8_t>& active) { active_ = active; }
  // Frame nuovo: le celle con almeno un pixel diverso dal frame precedente
  // risultano cambiate adesso. Il primo frame cambia tutte le celle (salvo ripresa).
  void OnFrame(const LumaImage& frame, double now_s);
  // Classificazione e avanzamento del livello verso il bersaglio.
  void Update(double now_s, double dt_s, const DimParams& p, bool suspended);
  DimSavedState Save(double now_s) const;

  int Cols() const { return cols_; }
  int Rows() const { return rows_; }
  float Level(int cx, int cy) const { return levels_[Index(cx, cy)]; }
  bool Bright(int cx, int cy) const { return bright_[Index(cx, cy)] != 0; }
  // Dentro un'app senza movimento intorno: non e' un elemento da scurire.
  bool Large(int cx, int cy) const { return large_[Index(cx, cy)] != 0; }
  double Target(int cx, int cy) const { return targets_[Index(cx, cy)]; }
  const std::vector<float>& Levels() const { return levels_; }

 private:
  size_t Index(int cx, int cy) const { return static_cast<size_t>(cy) * cols_ + cx; }

  int width_;
  int height_;
  int cols_;
  int rows_;
  double bright_threshold_ = 0.25;
  std::vector<float> previous_;
  std::vector<double> last_change_;
  std::vector<uint32_t> hashes_;
  // Contenuto "di riferimento" di ogni cella e da quando c'e'; inizio della deviazione
  // in corso (-1 nessuna): se la cella torna al riferimento entro kDimBlipS, riprende l'eta'.
  std::vector<uint32_t> anchor_hash_;
  std::vector<double> anchor_time_;
  std::vector<double> excursion_;
  std::vector<uint8_t> zones_;
  std::vector<uint8_t> active_;
  std::vector<uint8_t> bright_;
  std::vector<uint8_t> large_;
  // Bit 0-1: ultima classe (0 nessuna, 1 elemento, 2 da non toccare);
  // bit 2: ritorno lento in corso (la classe e' cambiata con il livello sopra il bersaglio).
  std::vector<uint8_t> class_;
  std::vector<double> targets_;
  std::vector<float> levels_;
  DimSavedState restore_;
};

// Media e massimo su finestre (2r+1)x(2r+1) tagliate ai bordi della griglia, con filtri
// separabili.
std::vector<double> WindowMean(const std::vector<double>& grid, int cols, int rows, int radius);
std::vector<double> WindowMax(const std::vector<double>& grid, int cols, int rows, int radius);
// Come WindowMean, ma conta solo le celle della stessa zona della cella centrale (passo
// orizzontale e poi verticale, ciascuno filtrato sulla zona: esatto per finestre rettangolari).
std::vector<double> ZoneWindowMean(const std::vector<double>& grid, const std::vector<uint8_t>& zones, int cols,
                                   int rows, int radius);

}  // namespace oled
