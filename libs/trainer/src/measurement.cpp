#include "training/trainer/measurement.hpp"

namespace training::trainer {

Measurement measurementOf(const Settings &settings, const InputState &input, bool running,
                          const Compensation &atStart, const Compensation &current,
                          double rate, bool calibrationCancelled) {
  const Compensation &compensation = running ? atStart : current;
  Measurement measurement{.measuring = Measuring::Waiting,
                          .inputName = {},
                          .midi = !settings.midiInput().empty(),
                          .compensationMs = 0.0,
                          .rate = rate,
                          .calibrationCancelled = calibrationCancelled};
  const Measuring present = running ? Measuring::Running : Measuring::Waiting;

  if (!measurement.midi) {
    measurement.compensationMs = compensation.audio * 1000.0;
    // Канала нет, если хук не получил его буфер или у хоста не было его имени:
    // имени нет у канала, которого нет у звуковой карты.
    if (!input.channelPresent || *input.channelPresent)
      measurement.inputName = input.channelName;
    measurement.measuring = measurement.inputName.empty() ? Measuring::NoInput : present;
    return measurement;
  }

  measurement.compensationMs = compensation.midi * 1000.0;
  if (!input.midiName) {
    measurement.measuring = Measuring::NoInput;
    return measurement;
  }
  measurement.inputName = *input.midiName;
  measurement.measuring = input.midiEnabled ? present : Measuring::NotEnabled;
  return measurement;
}

} // namespace training::trainer
