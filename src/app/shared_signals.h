// Ultimo riassunto dei segnali, dal thread di render (che lo pubblica) al thread UI.
#pragma once

#include <mutex>
#include <optional>

#include "summary_model.h"

namespace oled {

class SharedSignals {
 public:
  void Publish(const FrameSummary& s, double measured_ms);
  // L'ultimo riassunto se misurato da al massimo max_age_ms.
  std::optional<FrameSummary> Latest(double now_ms, double max_age_ms);

 private:
  std::mutex mutex_;
  std::optional<FrameSummary> latest_;
  double measured_ms_ = 0;
};

}  // namespace oled
