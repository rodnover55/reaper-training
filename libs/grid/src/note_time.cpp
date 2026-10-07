#include "training/grid/note_time.hpp"

#include <cmath>

namespace training::grid {

double compensation(int inputLatency, int outputLatency, int manualInput, int manualOutput,
                    bool driverLatency, double sampleRate) {
  const int samples =
      driverLatency ? inputLatency + outputLatency : manualInput + manualOutput;
  return static_cast<double>(samples) / sampleRate;
}

double midiCompensation(int blockSize, int outputLatency, int manualOutput, bool driverLatency,
                        double sampleRate) {
  const int samples = blockSize + (driverLatency ? outputLatency : manualOutput);
  return static_cast<double>(samples) / sampleRate;
}

double noteTime(double blockPosition, double offset, double sampleRate, double compensation,
                double rate) {
  return blockPosition + (offset / sampleRate - compensation) * rate;
}

double wrapIntoLoop(double time, double loopStart, double loopEnd) {
  const double length = loopEnd - loopStart;
  if (length <= 0.0)
    return time;

  double inside = std::fmod(time - loopStart, length);
  if (inside < 0.0)
    inside += length;
  return loopStart + inside;
}

} // namespace training::grid
