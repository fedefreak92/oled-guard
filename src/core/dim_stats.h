// Riassunto dello stato delle celle dell'oscuramento, per la diagnostica nel log.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace oled {

struct DimGridStats {
  int cells = 0;
  int bright = 0;        // luminose nell'ultimo frame
  int content = 0;       // con contenuto (luminose o con dettagli)
  int static_cells = 0;  // ferme da almeno delay_s
  int small_cells = 0;         // elementi piccoli fermi e luminosi (oscurabili)
  int large_cells = 0;         // fermi e luminosi ma dentro una grande area
  int dimmed = 0;        // livello > 0,01
  int recent_changes = 0;  // cambiate negli ultimi 2 s
  double max_level = 0;
};

// flags: bit0 luminosa, bit1 contenuto; classes: bit0-1 classe (1 piccola, 2 grande).
DimGridStats SummarizeDimGrids(const std::vector<float>& levels, const std::vector<uint32_t>& flags,
                               const std::vector<uint32_t>& classes, const std::vector<float>& last_change,
                               double now_s, double delay_s);
std::string FormatDimGridStats(const DimGridStats& s);

}  // namespace oled
