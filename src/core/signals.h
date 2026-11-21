// Segnali dal vivo: un campione al secondo (rischio, movimento, luminosita'), ultimi 60.
#pragma once

#include <deque>
#include <vector>

namespace oled {

struct SignalSample {
  bool measured = false;  // falso: nessuna misura (pausa, overlay spento)
  double risk = 0;
  double motion = 0;
  double brightness = 0;
};

struct ChannelStats {
  double peak = 0;
  double mean = 0;
  int samples = 0;  // solo i campioni misurati
};

class SignalRing {
 public:
  static constexpr int kSize = 60;
  void Push(const SignalSample& s);
  std::vector<SignalSample> Samples() const;  // dal piu' vecchio, al massimo 60
  ChannelStats Risk() const;
  ChannelStats Motion() const;
  ChannelStats Brightness() const;
  SignalSample Last() const;

 private:
  std::deque<SignalSample> samples_;
};

}  // namespace oled
