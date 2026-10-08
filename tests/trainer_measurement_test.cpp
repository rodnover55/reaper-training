#include <doctest/doctest.h>

#include "training/trainer/calibration.hpp"
#include "training/trainer/measurement.hpp"
#include "training/trainer/settings.hpp"
#include "training/trainer/transport.hpp"

#include <chrono>

using training::trainer::Calibrator;
using training::trainer::Compensation;
using training::trainer::InputState;
using training::trainer::Measurement;
using training::trainer::measurementOf;
using training::trainer::measuresMidi;
using training::trainer::Measuring;
using training::trainer::Moment;
using training::trainer::Settings;

using namespace std::chrono_literals;

namespace {

constexpr double kSampleRate = 48000.0;

/// Компенсация звука 32 мс и нот MIDI 12 мс.
constexpr Compensation kNow{.audio = 0.032,
                            .audioSampleRate = kSampleRate,
                            .inputLatency = 0.016,
                            .midi = 0.012,
                            .midiSampleRate = kSampleRate};

/// Канал «Input 1», который есть у звуковой карты.
InputState channel() {
  return InputState{
      .channelName = "Input 1", .channelPresent = true, .midiName = {}, .midiEnabled = false};
}

/// Вход MIDI «KeyStep» в настройках и у хоста.
Settings midiSettings() {
  Settings settings;
  settings.setMidiInput("KeyStep");
  settings.setMidiIndex(2);
  return settings;
}

InputState keyStep(bool enabled) {
  return InputState{.channelName = {},
                    .channelPresent = {},
                    .midiName = "KeyStep 37",
                    .midiEnabled = enabled};
}

} // namespace

TEST_CASE("Состояние измерения: ожидание запуска на канале звуковой карты") {
  const Measurement measurement =
      measurementOf(Settings{}, channel(), false, kNow, kNow, 1.0, false);
  CHECK(measurement.measuring == Measuring::Waiting);
  CHECK(measurement.inputName == "Input 1");
  CHECK_FALSE(measurement.midi);
  CHECK(measurement.compensationMs == doctest::Approx(32.0));
  CHECK(measurement.rate == 1.0);
  CHECK_FALSE(measurement.calibrationCancelled);
  CHECK_FALSE(measuresMidi(measurement));
}

TEST_CASE("Состояние измерения: в запуске действует компенсация его начала") {
  const Compensation atStart{.audio = 0.020, .midi = 0.010};
  const Measurement measurement =
      measurementOf(Settings{}, channel(), true, atStart, kNow, 1.0, false);
  CHECK(measurement.measuring == Measuring::Running);
  CHECK(measurement.inputName == "Input 1");
  CHECK(measurement.compensationMs == doctest::Approx(20.0));
  CHECK_FALSE(measuresMidi(measurement));
}

TEST_CASE("Состояние измерения: выбранного канала нет") {
  // У хоста нет имени канала.
  InputState unnamed = channel();
  unnamed.channelName.clear();
  const Measurement noName = measurementOf(Settings{}, unnamed, false, kNow, kNow, 1.0, false);
  CHECK(noName.measuring == Measuring::NoInput);
  CHECK(noName.inputName.empty());

  // Имя есть, но последний блок звука канала не нашёл.
  InputState missing = channel();
  missing.channelPresent = false;
  CHECK(measurementOf(Settings{}, missing, true, kNow, kNow, 1.0, false).measuring ==
        Measuring::NoInput);

  // Блоков после выбора канала ещё не было — верим имени.
  InputState unchecked = channel();
  unchecked.channelPresent.reset();
  CHECK(measurementOf(Settings{}, unchecked, false, kNow, kNow, 1.0, false).measuring ==
        Measuring::Waiting);
}

TEST_CASE("Состояние измерения: вход MIDI") {
  const Measurement waiting =
      measurementOf(midiSettings(), keyStep(true), false, kNow, kNow, 1.0, false);
  CHECK(waiting.measuring == Measuring::Waiting);
  CHECK(waiting.midi);
  CHECK(waiting.inputName == "KeyStep 37");
  CHECK(waiting.compensationMs == doctest::Approx(12.0));
  CHECK_FALSE(measuresMidi(waiting));

  const Measurement running =
      measurementOf(midiSettings(), keyStep(true), true, kNow, kNow, 1.0, false);
  CHECK(running.measuring == Measuring::Running);
  CHECK(measuresMidi(running));
}

TEST_CASE("Состояние измерения: вход MIDI не включён у хоста") {
  const Measurement measurement =
      measurementOf(midiSettings(), keyStep(false), true, kNow, kNow, 1.0, false);
  CHECK(measurement.measuring == Measuring::NotEnabled);
  CHECK(measurement.inputName == "KeyStep 37");
  CHECK(measurement.midi);
  CHECK_FALSE(measuresMidi(measurement));
}

TEST_CASE("Состояние измерения: устройства входа MIDI нет") {
  InputState absent = keyStep(true);
  absent.midiName.reset();
  const Measurement measurement =
      measurementOf(midiSettings(), absent, false, kNow, kNow, 1.0, false);
  CHECK(measurement.measuring == Measuring::NoInput);
  CHECK(measurement.inputName.empty());
  CHECK(measurement.compensationMs == doctest::Approx(12.0));
}

TEST_CASE("Состояние измерения: скорость не 1") {
  const Measurement measurement =
      measurementOf(Settings{}, channel(), true, kNow, kNow, 0.75, false);
  CHECK(measurement.rate == 0.75);
  CHECK(measurement.measuring == Measuring::Running);
}

TEST_CASE("Состояние измерения: калибровку прервал запуск") {
  Calibrator calibrator;
  REQUIRE(calibrator.start(false,
                           measurementOf(Settings{}, channel(), false, kNow, kNow, 1.0, false),
                           0, -50.0, Moment(10s)));
  calibrator.onStart();
  const Measurement running = measurementOf(Settings{}, channel(), true, kNow, kNow, 1.0,
                                            calibrator.cancelledByPlayback());
  CHECK(running.calibrationCancelled);
  CHECK(running.measuring == Measuring::Running);

  calibrator.onStop();
  CHECK_FALSE(measurementOf(Settings{}, channel(), false, kNow, kNow, 1.0,
                            calibrator.cancelledByPlayback())
                  .calibrationCancelled);
}
