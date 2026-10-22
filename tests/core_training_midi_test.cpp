#include <doctest/doctest.h>

#include "grid_timeline.hpp"

#include "training/core/hit_window.hpp"
#include "training/core/input/midi_receiver.hpp"
#include "training/core/training/midi.hpp"
#include "training/core/training/state/note.hpp"

#include <cstddef>
#include <vector>

using training::core::HitWindow;
using training::core::Interval;
using training::core::input::MidiReceiver;
using training::core::training::Midi;
using training::core::training::state::Bar;
using training::core::training::state::Beat;
using training::test::coreBar;
using training::test::SteadyTimeline;
using training::test::timeOf;

namespace {

/// Тренировка MIDI с окном 0 ± 10 мс, запущенная, с тактами 0 и 1 при
/// 120 BPM: удар — 0.5 с.
struct Bench {
  Midi trainer{*HitWindow::create(Interval(), 10 * Interval::kMillisecond)};

  Bench() {
    static const SteadyTimeline steady(120.0);
    REQUIRE(trainer.start().has_value());
    trainer.addBar(coreBar(steady, 0, 1));
    trainer.addBar(coreBar(steady, 1, 1));
  }

  /// Подаёт ноты MIDI на секундах `seconds` одним блоком.
  void notes(const std::vector<double> &seconds) {
    std::vector<MidiReceiver::Note> notes;
    notes.reserve(seconds.size());
    for (const double at : seconds)
      notes.push_back({.time = timeOf(at)});
    trainer.receive(notes);
  }

  /// Сколько нот легло во все такты.
  std::size_t placed() const {
    std::size_t count = 0;
    for (const Bar &bar : trainer.result().timeline.bars())
      for (const auto &[beat, notes] : bar.notes())
        count += notes.size();
    return count;
  }
};

} // namespace

TEST_CASE("Тренировка MIDI: каждая нота блока ложится со своим временем") {
  Bench bench;
  bench.notes({0.505, 1.003});
  CHECK(bench.placed() == 2);
  CHECK(bench.trainer.result().timeline.bars()[0].notes().at(Beat(1.0)).front().deviation /
            Interval::kMillisecond ==
        doctest::Approx(5.0));
}

TEST_CASE("Тренировка MIDI: нота старше последней обработанной отбрасывается") {
  Bench bench;
  bench.notes({1.0});
  bench.notes({0.5});
  CHECK(bench.placed() == 1);
  bench.notes({0.6, 0.4});
  CHECK(bench.placed() == 1);
}

TEST_CASE("Тренировка MIDI: нота в то же время, что и последняя, обрабатывается") {
  Bench bench;
  bench.notes({1.0, 1.0});
  CHECK(bench.placed() == 2);
}

TEST_CASE("Тренировка MIDI: новый запуск начинает время нот заново") {
  Bench bench;
  bench.notes({1.0});
  bench.trainer.stop();
  REQUIRE(bench.trainer.start().has_value());
  static const SteadyTimeline steady(120.0);
  bench.trainer.addBar(coreBar(steady, 0, 1));
  bench.trainer.addBar(coreBar(steady, 1, 1));
  bench.notes({0.5});
  CHECK(bench.placed() == 1);
}

TEST_CASE("Тренировка MIDI: отказ запуска оставляет последнюю обработанную ноту") {
  Bench bench;
  bench.notes({1.0});
  CHECK_FALSE(bench.trainer.start().has_value());
  bench.notes({0.5});
  CHECK(bench.placed() == 1);
}
