#include <string>
#include <vector>

#include "dim_stats.h"
#include "test_framework.h"

TEST(dim_stats_counts_cells) {
  // 4 celle: luminosa ferma piccola e scurita; luminosa ferma in grande area;
  // solo contenuto, ferma; nulla, cambiata da poco.
  const std::vector<float> levels = {0.3f, 0.0f, 0.0f, 0.005f};
  const std::vector<uint32_t> flags = {3u, 3u, 2u, 0u};
  const std::vector<uint32_t> classes = {1u, 2u | 4u, 0u, 0u};
  const std::vector<float> last_change = {0.0f, 0.0f, 0.0f, 95.0f};
  const oled::DimGridStats s = oled::SummarizeDimGrids(levels, flags, classes, last_change, 100.0, 30.0);
  CHECK_EQ(s.cells, 4);
  CHECK_EQ(s.bright, 2);
  CHECK_EQ(s.content, 3);
  CHECK_EQ(s.static_cells, 3);
  CHECK_EQ(s.small_cells, 1);
  CHECK_EQ(s.large_cells, 1);
  CHECK_EQ(s.dimmed, 1);
  CHECK_EQ(s.recent_changes, 0);  // l'ultima e' cambiata 5 s fa
  CHECK_NEAR(s.max_level, 0.3, 1e-6);
  const std::string text = oled::FormatDimGridStats(s);
  CHECK(text.find("piccole 1") != std::string::npos);
  CHECK(text.find("grandi 1") != std::string::npos);
  CHECK(text.find("cambiate da 2 s 0") != std::string::npos);
}

TEST(dim_stats_counts_recent_changes) {
  const oled::DimGridStats s =
      oled::SummarizeDimGrids({0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {99.0f, 98.5f, 90.0f}, 100.0, 30.0);
  CHECK_EQ(s.recent_changes, 2);
}

TEST(dim_stats_rejects_mismatched_sizes) {
  const oled::DimGridStats s = oled::SummarizeDimGrids({0.1f}, {}, {}, {}, 0, 30);
  CHECK_EQ(s.cells, 0);
}
