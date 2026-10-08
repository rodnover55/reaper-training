#include <doctest/doctest.h>

#include "training/trainer/audio_mode.hpp"
#include "training/trainer/calibration.hpp"
#include "training/trainer/measurement.hpp"
#include "training/trainer/transport.hpp"

#include <chrono>

using training::trainer::AudioMode;
using training::trainer::audioModeOf;
using training::trainer::Calibrator;
using training::trainer::Compensation;
using training::trainer::Measurement;
using training::trainer::Measuring;
using training::trainer::Moment;
using training::trainer::Transport;

using namespace std::chrono_literals;

namespace {

/// Тренажёр ждёт запуска на канале «Input 1».
Measurement waiting() {
  return Measurement{.measuring = Measuring::Waiting,
                     .inputName = "Input 1",
                     .midi = false,
                     .compensationMs = 0.0,
                     .rate = 1.0,
                     .calibrationCancelled = false};
}

} // namespace

TEST_CASE("Режим звукового пути: при идущем запуске — атаки") {
  Transport transport;
  const Calibrator calibrator;
  REQUIRE(transport.start(Moment(1s), 0.0, Compensation{}));
  CHECK(audioModeOf(transport, calibrator) == AudioMode::Onsets);

  // Запуск кончился: звук никуда не идёт.
  REQUIRE(transport.stop());
  CHECK(audioModeOf(transport, calibrator) == AudioMode::Off);
}

TEST_CASE("Режим звукового пути: транспорт стоит, калибровка слушает — калибровка") {
  const Transport transport;
  Calibrator calibrator;
  REQUIRE(calibrator.start(false, waiting(), 0, -50.0, Moment(1s)));
  CHECK(audioModeOf(transport, calibrator) == AudioMode::Calibration);

  // Калибровка оборвалась без звука и больше не слушает.
  (void)calibrator.update(Moment(10s));
  REQUIRE_FALSE(calibrator.listening());
  CHECK(audioModeOf(transport, calibrator) == AudioMode::Off);
}

TEST_CASE("Режим звукового пути: транспорт стоит без калибровки — ничего") {
  const Transport transport;
  const Calibrator calibrator;
  CHECK(audioModeOf(transport, calibrator) == AudioMode::Off);
}
