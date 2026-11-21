#include "activity_log.h"

namespace oled {

const ActivityEvent& ActivityLog::Add(int64_t t_ms, std::string_view kind, std::string_view text) {
  if (!events_.empty()) {
    ActivityEvent& last = events_.front();
    if (last.kind == kind && last.text == text && t_ms - last.t_ms <= kMergeMs) {
      last.t_ms = t_ms;
      ++last.count;
      return last;
    }
  }
  events_.push_front(ActivityEvent{t_ms, std::string(kind), std::string(text), 1});
  if (events_.size() > kMax) events_.pop_back();
  return events_.front();
}

}  // namespace oled
