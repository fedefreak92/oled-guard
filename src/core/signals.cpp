#include "signals.h"

#include <algorithm>

namespace oled {
namespace {

template <class Get>
ChannelStats Stats(const std::deque<SignalSample>& samples, Get get) {
  ChannelStats st;
  double sum = 0;
  for (const auto& s : samples) {
    if (!s.measured) continue;
    const double v = get(s);
    st.peak = st.samples == 0 ? v : std::max(st.peak, v);
    sum += v;
    ++st.samples;
  }
  if (st.samples > 0) st.mean = sum / st.samples;
  return st;
}

}  // namespace

void SignalRing::Push(const SignalSample& s) {
  samples_.push_back(s);
  while (samples_.size() > static_cast<size_t>(kSize)) samples_.pop_front();
}

std::vector<SignalSample> SignalRing::Samples() const { return {samples_.begin(), samples_.end()}; }
ChannelStats SignalRing::Risk() const { return Stats(samples_, [](const SignalSample& s) { return s.risk; }); }
ChannelStats SignalRing::Motion() const { return Stats(samples_, [](const SignalSample& s) { return s.motion; }); }
ChannelStats SignalRing::Brightness() const {
  return Stats(samples_, [](const SignalSample& s) { return s.brightness; });
}
SignalSample SignalRing::Last() const { return samples_.empty() ? SignalSample{} : samples_.back(); }

}  // namespace oled
