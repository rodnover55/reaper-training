#include <doctest/doctest.h>

#include "grid_timeline.hpp"

#include "training/core/calibration/calibrator.hpp"
#include "training/core/hit_window.hpp"
#include "training/core/input/midi_receiver.hpp"
#include "training/core/settings.hpp"
#include "training/core/training/audio.hpp"
#include "training/core/training/midi.hpp"
#include "training/core/training/trainer.hpp"
#include "training/core/transport.hpp"
#include "training/core/types.hpp"

#include <memory>
#include <vector>

using training::core::CannotStartUnit;
using training::core::Hit;
using training::core::HitWindow;
using training::core::Interval;
using training::core::SampleRate;
using training::core::Settings;
using training::core::Transport;
using training::core::input::MidiReceiver;
using training::core::training::Audio;
using training::core::training::Midi;
using training::core::training::Trainer;
using training::core::training::state::Beat;
using training::test::coreBar;
using training::test::SteadyTimeline;
using training::test::timeOf;

namespace {

/// Окно попадания тестов: 0 ± 10 мс.
constexpr HitWindow kWindow = *HitWindow::create(Interval(), 10 * Interval::kMillisecond);

/// Настройки ядра тестов: окно попадания `kWindow`, порог тишины −50 dBFS.
const Settings kSettings{kWindow, -50.0};

using AudioTraining = Transport::Training<Trainer::Type::Audio>;
using MidiTraining = Transport::Training<Trainer::Type::Midi>;
using training::core::calibration::Step;

/// Частота звука тестов.
const SampleRate kRate = *SampleRate::create(48000.0);

} // namespace

TEST_CASE("Транспорт: частота — та, с которой он создан") {
  const Transport transport(kSettings, kRate);
  CHECK(transport.sampleRate() == kRate);
}

TEST_CASE("Транспорт: калибровка запускается калибратором частоты транспорта с шага тишины") {
  Transport transport(kSettings, kRate);
  const auto calibrator = transport.start(Transport::Calibration{});
  REQUIRE(calibrator);
  CHECK((*calibrator)->sampleRate() == kRate);
  CHECK((*calibrator)->result().step == Step::Silence);
}

TEST_CASE("Транспорт: тренировка по звуку — с частотой транспорта") {
  Transport transport(kSettings, kRate);
  const auto audio = transport.start(AudioTraining{});
  REQUIRE(audio);
  CHECK((*audio)->sampleRate() == kRate);
  CHECK((*audio)->result().step == Trainer::Step::Working);
}

TEST_CASE("Транспорт: частота, на которой детектор не создаётся, шаг не запускает") {
  // Частота среза детектора, 1 кГц, — не ниже половины частоты 1600 Гц.
  Transport transport(kSettings, *SampleRate::create(1600.0));
  const auto midi = transport.start(MidiTraining{});
  REQUIRE(midi);
  CHECK(transport.start(Transport::Calibration{}) == std::unexpected(CannotStartUnit{}));
  CHECK((*midi)->result().step == Trainer::Step::Off);
  CHECK(transport.start(AudioTraining{}) == std::unexpected(CannotStartUnit{}));
}

TEST_CASE("Транспорт: запуск тренировки останавливает калибровку") {
  Transport transport(kSettings, kRate);
  const auto calibrator = transport.start(Transport::Calibration{});
  REQUIRE(calibrator);
  const auto audio = transport.start(AudioTraining{});
  REQUIRE(audio);
  CHECK((*calibrator)->result().step == Step::Off);
  CHECK((*audio)->result().step == Trainer::Step::Working);
}

TEST_CASE("Транспорт: запуск калибровки останавливает тренировку") {
  Transport transport(kSettings, kRate);
  const auto midi = transport.start(MidiTraining{});
  REQUIRE(midi);
  (void)transport.start(Transport::Calibration{});
  CHECK((*midi)->result().step == Trainer::Step::Off);
}

TEST_CASE("Транспорт: прежний шаг живёт у того, кто его держит") {
  // Новый запуск создаёт новый калибратор; прежний остаётся у вызывающего
  // остановленным, и его методы можно звать.
  Transport transport(kSettings, kRate);
  const auto first = transport.start(Transport::Calibration{});
  REQUIRE(first);
  const auto again = transport.start(Transport::Calibration{});
  REQUIRE(again);
  CHECK(first->get() != again->get());
  CHECK((*again)->result().step == Step::Silence);
  CHECK((*first)->result().step == Step::Off);
  const std::vector<float> silence(480, 0.0F);
  (*first)->receive({.samples = silence, .start = timeOf(0.0)});
  CHECK((*first)->result().step == Step::Off);
}

TEST_CASE("Транспорт: тренировка звука живёт у того, кто её держит, после транспорта") {
  std::shared_ptr<Audio> audio;
  {
    Transport transport(kSettings, kRate);
    auto started = transport.start(AudioTraining{});
    REQUIRE(started);
    audio = *started;
  }
  CHECK(audio->result().step == Trainer::Step::Working);
}

TEST_CASE("Транспорт: запуск тренировки MIDI — та же тренировка") {
  Transport transport(kSettings, kRate);
  const auto first = transport.start(MidiTraining{});
  transport.stop();
  const auto again = transport.start(MidiTraining{});
  REQUIRE(first);
  REQUIRE(again);
  CHECK(first->get() == again->get());
  CHECK((*again)->result().step == Trainer::Step::Working);
}

TEST_CASE("Транспорт: остановка останавливает запущенный шаг") {
  Transport transport(kSettings, kRate);
  const auto audio = transport.start(AudioTraining{});
  REQUIRE(audio);
  transport.stop();
  CHECK((*audio)->result().step == Trainer::Step::Off);

  const auto calibrator = transport.start(Transport::Calibration{});
  REQUIRE(calibrator);
  transport.stop();
  CHECK((*calibrator)->result().step == Step::Off);
}

TEST_CASE("Транспорт: новые настройки действуют на идущую тренировку без перезапуска") {
  static const SteadyTimeline steady(120.0);
  Transport transport(kSettings, kRate);
  const auto started = transport.start(MidiTraining{});
  REQUIRE(started);
  Midi &midi = **started;
  midi.addBar(coreBar(steady, 0, 1));
  midi.addBar(coreBar(steady, 1, 1));

  Settings wide = kSettings;
  wide.setWindow(*HitWindow::create(Interval(), 20 * Interval::kMillisecond));
  transport.setSettings(wide);
  const std::vector<MidiReceiver::Note> notes{{.time = timeOf(0.515)}};
  midi.receive(notes);

  CHECK(midi.result().step == Trainer::Step::Working);
  CHECK(midi.result().timeline.bars()[0].notes().at(Beat(1.0)).front().hit == Hit::Good);
}

TEST_CASE("Транспорт: новый порог тишины тренировка звука получает без перезапуска") {
  static const SteadyTimeline steady(120.0);
  Transport transport(kSettings, kRate);
  const auto audio = transport.start(AudioTraining{});
  REQUIRE(audio);
  (*audio)->addBar(coreBar(steady, 0, 1));

  Settings quieter = kSettings;
  quieter.setSilenceDb(-60.0);
  transport.setSettings(quieter);
  CHECK((*audio)->result().step == Trainer::Step::Working);
  CHECK((*audio)->result().timeline.bars().size() == 1);
}
