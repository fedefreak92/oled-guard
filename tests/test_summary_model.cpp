// Riassunto dei segnali (oracolo dei test GPU) e misure per cella del modello.
#include <string>

#include "dim_model.h"
#include "dim_params.h"
#include "dim_scenes.h"
#include "shade_model.h"
#include "summary_model.h"
#include "test_framework.h"

using oled::ComputeSummary;
using oled::DimModel;
using oled::DimParams;
using oled::FrameSummary;
using oled::LumaImage;
using oled::ShadeParams;
using namespace oled::scenes;

namespace {
constexpr int kSw = 64, kSh = 48;  // 4x3 celle
LumaImage WhiteCellImage() {
  LumaImage img = Black(kSw, kSh);
  Fill(img, 0, 0, 16, 16, 1.0f);
  return img;
}
}  // namespace

TEST(model_mean_luma) {
  LumaImage img = Black(20, 16);  // 2 celle, la seconda larga 4 px
  Fill(img, 0, 0, 8, 16, 1.0f);
  Fill(img, 16, 0, 20, 16, 1.0f);
  DimModel m(20, 16, 0);
  m.OnFrame(img, 0);
  CHECK_NEAR(m.MeanLuma(0, 0), 0.5, 1e-6);
  CHECK_NEAR(m.MeanLuma(1, 0), 1.0, 1e-6);
}

TEST(model_content_change_ignores_active_zones) {
  DimModel m(kSw, kSh, 0);
  m.SetZones(Zones(kApp, 4, 3));
  m.SetActiveZones({kApp});
  m.OnFrame(WhiteCellImage(), 0);
  m.Update(30, 30, DimParams{}, false);
  m.OnFrame(WhiteCellImage(), 30);
  CHECK_NEAR(m.ContentChange(0, 0), 0.0, 1e-9);
  LumaImage changed = WhiteCellImage();
  Fill(changed, 20, 0, 24, 4, 0.3f);  // cella (1,0)
  m.OnFrame(changed, 40);
  CHECK_NEAR(m.ContentChange(1, 0), 40.0, 1e-9);
  CHECK_NEAR(m.ContentChange(0, 0), 0.0, 1e-9);
}

TEST(summary_still_bright_is_risk) {
  DimModel m(kSw, kSh, 0);
  m.OnFrame(WhiteCellImage(), 0);
  const FrameSummary at61 = ComputeSummary(m, 61, ShadeParams{});
  CHECK_NEAR(at61.risk, 1.0 / 12, 1e-9);
  CHECK_NEAR(at61.motion, 0.0, 1e-9);
  CHECK_NEAR(ComputeSummary(m, 59, ShadeParams{}).risk, 0.0, 1e-9);
}

TEST(summary_active_window_still_counts_as_still) {
  DimModel m(kSw, kSh, 0);
  m.SetZones(Zones(kApp, 4, 3));
  m.SetActiveZones({kApp});
  m.OnFrame(WhiteCellImage(), 0);
  m.Update(61, 61, DimParams{}, false);
  CHECK_NEAR(ComputeSummary(m, 61, ShadeParams{}).risk, 1.0 / 12, 1e-9);
}

TEST(summary_unprotected_follows_level) {
  DimModel m(kSw, kSh, 0);
  m.SetZones(Zones(kShell, 4, 3));
  DimParams p;
  p.delay_s = 30;
  p.ramp_s = 30;
  m.OnFrame(WhiteCellImage(), 0);
  for (int t = 1; t <= 200; ++t) m.Update(t, 1, p, false);
  CHECK(m.Level(0, 0) > 0.05f);
  const FrameSummary s = ComputeSummary(m, 200, ShadeParams{});
  CHECK_NEAR(s.risk, 1.0 / 12, 1e-9);
  CHECK_NEAR(s.risk_unprotected, 0.0, 1e-9);
  DimModel fresh(kSw, kSh, 0);
  fresh.OnFrame(WhiteCellImage(), 0);
  CHECK_NEAR(ComputeSummary(fresh, 61, ShadeParams{}).risk_unprotected, 1.0 / 12, 1e-9);
}

TEST(summary_motion_and_brightness) {
  DimModel m(kSw, kSh, 0);
  m.OnFrame(Black(kSw, kSh), 0);
  LumaImage half = Black(kSw, kSh);
  Fill(half, 0, 0, 32, kSh, 0.1f);
  m.OnFrame(half, 55);
  CHECK_NEAR(ComputeSummary(m, 60, ShadeParams{}).motion, 0.5, 1e-9);
  CHECK_NEAR(ComputeSummary(m, 66, ShadeParams{}).motion, 0.0, 1e-9);
  DimModel u(kSw, kSh, 0);
  LumaImage grey = Black(kSw, kSh);
  Fill(grey, 0, 0, kSw, kSh, 0.4f);
  u.OnFrame(grey, 0);
  CHECK_NEAR(ComputeSummary(u, 1, ShadeParams{}).brightness, 0.4, 1e-6);
}

TEST(summary_light_removed_and_zones) {
  DimModel m(kSw, kSh, 0);
  m.SetZones(Zones(kShell, 4, 3));
  DimParams p;
  p.delay_s = 30;
  p.ramp_s = 30;
  m.OnFrame(WhiteCellImage(), 0);
  for (int t = 1; t <= 200; ++t) m.Update(t, 1, p, false);
  const double level = m.Level(0, 0);
  ShadeParams shade;
  shade.brightness = 0.5f;
  const FrameSummary s = ComputeSummary(m, 200, shade);
  CHECK_NEAR(s.light_still_before, 1.0 / 12, 1e-6);
  CHECK_NEAR(s.light_still_removed / s.light_still_before, 1 - (1 - level) * 0.5, 1e-6);
  CHECK_EQ(oled::ZoneOf(0, 0, 4, 3), 0);
  CHECK_EQ(oled::ZoneOf(1, 1, 4, 3), 6 * oled::kZoneCols + 8);
  CHECK_NEAR(s.zone_light[0], (1 - level) * 0.5, 1e-6);
  CHECK_NEAR(s.zone_light[oled::ZoneOf(1, 1, 4, 3)], 0.0, 1e-9);
}

TEST(summary_motion_caption) {
  CHECK_EQ(std::string(oled::MotionCaptionKey(0.01)), std::string("signals.caption.still"));
  CHECK_EQ(std::string(oled::MotionCaptionKey(0.1)), std::string("signals.caption.work"));
  CHECK_EQ(std::string(oled::MotionCaptionKey(0.2)), std::string("signals.caption.video"));
}

// Buffer del riassunto scritto da CSSummary: totali in 64 slot, poi due parole per zona.
TEST(summary_decode_layout) {
  std::vector<uint32_t> w(oled::kSummaryWords, 0);
  const auto total = [&](int slot, int field) -> uint32_t& { return w[slot * oled::kSummaryTotals + field]; };
  total(0, oled::kSumRiskPx) = 256;
  total(1, oled::kSumRiskPx) = 256;
  total(1, oled::kSumUnprotectedPx) = 256;
  total(5, oled::kSumMotionPx) = 1024;
  total(2, oled::kSumLuma) = static_cast<uint32_t>(0.5 * 3072 * oled::kSummaryScale);
  total(3, oled::kSumStillBefore) = static_cast<uint32_t>(1.0 * 256 * oled::kSummaryScale);
  total(4, oled::kSumStillRemoved) = static_cast<uint32_t>(0.25 * 256 * oled::kSummaryScale);
  w[oled::kSummaryZoneBase + 3 * 2] = static_cast<uint32_t>(0.75 * 256 * oled::kSummaryScale);
  w[oled::kSummaryZoneBase + 3 * 2 + 1] = 256;
  const double px = 64.0 * 48;
  const FrameSummary s = oled::DecodeSummary(w, px);
  CHECK_NEAR(s.risk, 512 / px, 1e-12);
  CHECK_NEAR(s.risk_unprotected, 256 / px, 1e-12);
  CHECK_NEAR(s.motion, 1024 / px, 1e-12);
  CHECK_NEAR(s.brightness, 0.5 * 3072 / px, 1e-9);
  CHECK_NEAR(s.light_still_before, 256 / px, 1e-9);
  CHECK_NEAR(s.light_still_removed, 0.25 * 256 / px, 1e-9);
  CHECK_NEAR(s.zone_light[3], 0.75, 1e-9);
  CHECK_NEAR(s.zone_light[4], 0.0, 1e-12);
  const FrameSummary bad = oled::DecodeSummary(std::vector<uint32_t>(10, 7), px);
  CHECK_NEAR(bad.risk, 0.0, 1e-12);
}
