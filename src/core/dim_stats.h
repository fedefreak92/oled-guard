// Riassunto dello stato delle celle dell'oscuramento, per la diagnostica nel log.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace oled {

struct DimGridStats {
  int cells = 0;
  int bright = 0;        // luminose nell'ultimo frame
  int moving = 0;        // cambiate da meno di 10 s (movimento)
  int static_cells = 0;  // ferme da almeno delay_s
  int small_cells = 0;   // elementi fermi e luminosi da scurire
  int large_cells = 0;   // fermi e luminosi ma da non toccare (app senza movimento)
  int dimmed = 0;        // livello > 0,01
  int recent_changes = 0;  // cambiate negli ultimi 2 s
  double max_level = 0;
};

// flags: bit0 luminosa; classes: bit0-1 classe (1 elemento, 2 da non toccare).
DimGridStats SummarizeDimGrids(const std::vector<float>& levels, const std::vector<uint32_t>& flags,
                               const std::vector<uint32_t>& classes, const std::vector<float>& last_change,
                               double now_s, double delay_s);
std::string FormatDimGridStats(const DimGridStats& s);

}  // namespace oled
