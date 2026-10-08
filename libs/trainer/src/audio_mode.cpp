#include "training/trainer/audio_mode.hpp"

namespace training::trainer {

AudioMode audioModeOf(const Transport &transport, const Calibrator &calibrator) {
  if (transport.running())
    return AudioMode::Onsets;
  if (calibrator.listening())
    return AudioMode::Calibration;
  return AudioMode::Off;
}

} // namespace training::trainer
