// Riassunto dei segnali di un istante: rischio, movimento, luminosita', luce sulle zone
// ferme e luce arrivata al pannello per zona 32x18. Riferimento su CPU dello shader
// CSSummary (dim_cs.hlsl), usato come oracolo nei test GPU.
#pragma once

#include <array>
#include <cstdint>
#include <vector>
#include <string_view>

#include "dim_model.h"
#include "shade_model.h"

namespace oled {

inline constexpr int kZoneCols = 32;
inline constexpr int kZoneRows = 18;
inline constexpr int kZones = kZoneCols * kZoneRows;
inline constexpr double kStillS = 60;           // ferma: contenuto invariato da almeno 60 s
inline constexpr double kMovingS = 10;          // in movimento: cambiata negli ultimi 10 s
inline constexpr double kProtectedLevel = 0.05; // sopra: la cella e' gia' scurita

struct FrameSummary {
  double risk = 0;                // frazione dell'area: luminosa e ferma (prima dell'oscuramento)
  double risk_unprotected = 0;    // come risk, con livello <= kProtectedLevel
  double motion = 0;              // frazione dell'area cambiata negli ultimi 10 s
  double brightness = 0;          // luminanza media (1 = bianco SDR)
  double light_still_before = 0;  // luce sulle celle ferme, pesata per area totale
  double light_still_removed = 0; // luce tolta da oscuramento, luminosita' e bordi
  std::array<double, kZones> zone_light{};  // luce media arrivata al pannello per zona
};

// Disposizione del buffer scritto dallo shader CSSummary (dim_cs.hlsl, stesse costanti):
// kSummarySlots gruppi di kSummaryTotals totali (le celle si distribuiscono per indice,
// cosi' le somme a 32 bit non traboccano), poi per ogni zona luce arrivata e pixel.
// Le luci sono luminanza x pixel x kSummaryScale, arrotondate cella per cella.
inline constexpr int kSummarySlots = 64;
inline constexpr int kSummaryTotals = 6;
inline constexpr int kSumRiskPx = 0, kSumUnprotectedPx = 1, kSumMotionPx = 2, kSumLuma = 3, kSumStillBefore = 4,
                     kSumStillRemoved = 5;
inline constexpr double kSummaryScale = 64;
inline constexpr int kSummaryZoneBase = kSummarySlots * kSummaryTotals;
inline constexpr int kSummaryWords = kSummaryZoneBase + kZones * 2;

// Zona 32x18 di una cella della griglia cols x rows.
int ZoneOf(int cx, int cy, int cols, int rows);
FrameSummary ComputeSummary(const DimModel& m, double now_s, const ShadeParams& shade);
std::string_view MotionCaption(double motion);
// Dal buffer dello shader; dimensione sbagliata o total_px <= 0: riassunto vuoto.
FrameSummary DecodeSummary(const std::vector<uint32_t>& words, double total_px);

}  // namespace oled
