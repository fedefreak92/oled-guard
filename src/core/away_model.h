// Modalita' assente: lo schermo si scurisce dopo un po' senza input e senza movimento
// continuo sul desktop (un video in finestra non deve farla scattare).
#pragma once

#include <array>

namespace oled {

inline constexpr int kMotionWindowS = 60;
inline constexpr int kMovingSecondFrames = 8;  // >= 8 frame nuovi in un secondo = secondo "in movimento"
inline constexpr int kContinuousSeconds = 20;  // >= 20 secondi in movimento negli ultimi 60
inline constexpr double kAwayRampMs = 10000;

// Secondi recenti in cui il desktop ha prodotto frame nuovi.
class MotionWindow {
 public:
  void AddSecond(int new_frames);  // un campione al secondo
  bool Continuous() const;
  void Reset();

 private:
  std::array<int, kMotionWindowS> frames_{};
  int next_ = 0;
};

struct AwayInputs {
  double now_ms = 0;
  double idle_ms = 0;  // da GetLastInputInfo
  bool continuous_motion = false;
  bool overlay_shown = false;
  bool enabled = false;
  int after_min = 10;
};

class AwayModel {
 public:
  // Aggiorna lo stato; restituisce true se e' cambiato.
  bool Update(const AwayInputs& in);
  bool Away() const { return away_; }
  double SinceMs() const { return since_ms_; }  // inizio dell'assenza (valido se Away())

 private:
  bool away_ = false;
  double since_ms_ = -1;
};

// Fattore 1 -> target lineare in kAwayRampMs da since_ms; 1 se since_ms < 0.
double AwayFactorAt(double now_ms, double since_ms, double target);

}  // namespace oled
