#include "training/reaper/compensation.hpp"

namespace training::reaper {

core::Interval Compensation::audioOf(int inputLatency, int outputLatency, int manualInput,
                                     int manualOutput, bool driverLatency, double sampleRate) {
  const int samples =
      driverLatency ? inputLatency + outputLatency : manualInput + manualOutput;
  return static_cast<double>(samples) / sampleRate * core::Interval::kSecond;
}

core::Interval Compensation::midiOf(int blockSize, int outputLatency, int manualOutput,
                                    bool driverLatency, double sampleRate) {
  const int samples = blockSize + (driverLatency ? outputLatency : manualOutput);
  return static_cast<double>(samples) / sampleRate * core::Interval::kSecond;
}

} // namespace training::reaper
