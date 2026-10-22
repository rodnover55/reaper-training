#include <doctest/doctest.h>

#include "grid_timeline.hpp"

#include "training/core/errors.hpp"
#include "training/core/hit_window.hpp"
#include "training/core/input/midi_receiver.hpp"
#include "training/core/settings.hpp"
#include "training/core/training/midi.hpp"
#include "training/core/training/state/note.hpp"
#include "training/core/training/state/timeline.hpp"
#include "training/core/training/trainer.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <vector>

using training::core::Hit;
using training::core::HitWindow;
using training::core::Interval;
using training::core::InvalidStep;
using training::core::Settings;
using training::core::input::MidiReceiver;
using training::core::training::Midi;
using training::core::training::Trainer;
using training::core::training::state::Bar;
using training::core::training::state::Beat;
using training::core::training::state::Note;
using training::test::coreBar;
using training::test::SteadyTimeline;
using training::test::timeOf;

namespace {

/// Карта темпа тестов: 120 BPM, 4/4, удар — 0.5 с, такт — 2 с.
const SteadyTimeline &timeline() {
  static const SteadyTimeline steady(120.0);
  return steady;
}

/// Окно попадания тестов: 0 ± 10 мс.
constexpr HitWindow kWindow = *HitWindow::create(Interval(), 10 * Interval::kMillisecond);

/// Тренировка по нотам MIDI с окном тестов (`kWindow`): ноты `Trainer`
/// получает через неё.
struct Bench {
  Midi trainer{kWindow};

  /// Передаёт тренировке такт `bar` карты тестов с `perBeat` долями на удар.
  void bar(std::int64_t bar, int perBeat = 1) {
    trainer.addBar(coreBar(timeline(), bar, perBeat));
  }

  /// Подаёт ноты MIDI на секундах `seconds`.
  void notes(const std::vector<double> &seconds) {
    std::vector<MidiReceiver::Note> notes;
    notes.reserve(seconds.size());
    for (const double at : seconds)
      notes.push_back({.time = timeOf(at)});
    trainer.receive(notes);
  }

  /// Ноты доли `place` такта номер `index` в списке тактов.
  std::vector<Note> notesAt(std::size_t index, double place) const {
    return trainer.result().timeline.bars().at(index).notes().at(Beat(place));
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

/// Разница ноты `note` в миллисекундах.
double msIn(const Note &note) { return note.deviation / Interval::kMillisecond; }

} // namespace

TEST_CASE("Тренировка: запуск переводит Off → Working, остановка — обратно") {
  Bench bench;
  CHECK(bench.trainer.result().step == Trainer::Step::Off);
  REQUIRE(bench.trainer.start().has_value());
  CHECK(bench.trainer.result().step == Trainer::Step::Working);
  bench.trainer.stop();
  CHECK(bench.trainer.result().step == Trainer::Step::Off);
}

TEST_CASE("Тренировка: запуск идущей тренировки — InvalidStep, такты остаются") {
  Bench bench;
  REQUIRE(bench.trainer.start().has_value());
  bench.bar(0);
  CHECK(bench.trainer.start() == std::unexpected(InvalidStep{}));
  CHECK(bench.trainer.result().step == Trainer::Step::Working);
  CHECK(bench.trainer.result().timeline.bars().size() == 1);
}

TEST_CASE("Тренировка: такты остаются после остановки, новый запуск их очищает") {
  Bench bench;
  REQUIRE(bench.trainer.start().has_value());
  bench.bar(0);
  bench.bar(1);
  bench.notes({0.0});
  REQUIRE(bench.trainer.result().timeline.bars().size() == 2);

  bench.trainer.stop();
  CHECK(bench.trainer.result().timeline.bars().size() == 2);
  CHECK(bench.notesAt(0, 0.0).size() == 1);

  REQUIRE(bench.trainer.start().has_value());
  CHECK(bench.trainer.result().timeline.bars().empty());
}

TEST_CASE("Тренировка: нота ложится в ближайшую долю с разницей") {
  Bench bench;
  REQUIRE(bench.trainer.start().has_value());
  bench.bar(0);
  bench.bar(1);
  bench.notes({0.512});

  REQUIRE(bench.notesAt(0, 1.0).size() == 1);
  CHECK(msIn(bench.notesAt(0, 1.0).front()) == doctest::Approx(12.0));
}

TEST_CASE("Тренировка: нота ждёт такта, который начинается позже её времени") {
  // Последняя доля такта 0 — 1.5 с; нота на 1.99 с ближе к началу такта 1.
  Bench bench;
  REQUIRE(bench.trainer.start().has_value());
  bench.notes({1.99});
  CHECK(bench.placed() == 0);

  bench.bar(0);
  CHECK(bench.placed() == 0);

  bench.bar(1);
  REQUIRE(bench.notesAt(1, 0.0).size() == 1);
  CHECK(msIn(bench.notesAt(1, 0.0).front()) == doctest::Approx(-10.0));
}

TEST_CASE("Тренировка: нота у конца такта ложится в первую долю пришедшего следующего такта") {
  Bench bench;
  REQUIRE(bench.trainer.start().has_value());
  bench.bar(0);
  bench.bar(1);
  bench.notes({1.99});
  REQUIRE(bench.notesAt(1, 0.0).size() == 1);
  CHECK(msIn(bench.notesAt(1, 0.0).front()) == doctest::Approx(-10.0));
}

TEST_CASE("Тренировка: без запуска ноты отбрасываются") {
  Bench bench;
  bench.bar(0);
  bench.bar(1);
  bench.notes({0.5});
  CHECK(bench.placed() == 0);
}

TEST_CASE("Тренировка: остановка удаляет ноты, ждущие такта") {
  Bench bench;
  REQUIRE(bench.trainer.start().has_value());
  bench.bar(0);
  bench.notes({1.99});
  bench.trainer.stop();
  REQUIRE(bench.trainer.start().has_value());
  bench.bar(0);
  bench.bar(1);
  CHECK(bench.placed() == 0);
}

TEST_CASE("Тренировка: нота раньше первого такта ложится в его первую долю") {
  // Предтакт — такт 0, первый такт запуска — такт 1 (2.0 с).
  Bench bench;
  REQUIRE(bench.trainer.start().has_value());
  bench.bar(0);
  bench.bar(1);
  bench.notes({1.9});
  REQUIRE(bench.notesAt(1, 0.0).size() == 1);
  CHECK(msIn(bench.notesAt(1, 0.0).front()) == doctest::Approx(-100.0));
}

TEST_CASE(
    "Тренировка: оценка ноты — по окну попадания, которое действовало, когда нота легла") {
  Bench bench;
  REQUIRE(bench.trainer.start().has_value());
  bench.bar(0);
  bench.bar(1);
  bench.notes({0.015});
  bench.trainer.setSettings(
      Settings(*HitWindow::create(Interval(), 20 * Interval::kMillisecond), -50.0));
  bench.notes({0.515, 1.0});

  CHECK(bench.notesAt(0, 0.0).front().hit == Hit::Bad);
  CHECK(bench.notesAt(0, 1.0).front().hit == Hit::Good);
  CHECK(bench.notesAt(0, 2.0).front().hit == Hit::Target);
}

TEST_CASE("Тренировка: две ноты у одной доли — обе в её нотах по порядку постановки") {
  Bench bench;
  REQUIRE(bench.trainer.start().has_value());
  bench.bar(0);
  bench.bar(1);
  bench.notes({0.45});
  bench.notes({0.512});
  const std::vector<Note> notes = bench.notesAt(0, 1.0);
  REQUIRE(notes.size() == 2);
  CHECK(msIn(notes[0]) == doctest::Approx(-50.0));
  CHECK(msIn(notes[1]) == doctest::Approx(12.0));
}

TEST_CASE(
    "Тренировка: перекрытие такта — поставленные ноты остаются, новые — в доли нового такта") {
  // Такты 1 и 2 пришли заранее; петля на 3.0 с вернула воспроизведение к
  // такту 0: такт со start 3.0 с перекрывает такт 1 с этого времени.
  Bench bench;
  REQUIRE(bench.trainer.start().has_value());
  bench.bar(0);
  bench.bar(1);
  bench.bar(2);
  bench.notes({3.01});
  REQUIRE(bench.notesAt(1, 2.0).size() == 1);

  Bar again("1", timeOf(3.0), timeOf(5.0), {}, 120.0);
  for (int beat = 0; beat < 4; ++beat)
    again.addDivision(Beat(beat), timeOf(3.0 + 0.5 * beat));
  bench.trainer.addBar(again);

  // Нота, уже поставленная в такт 1, остаётся в нём.
  REQUIRE(bench.trainer.result().timeline.bars().size() == 4);
  CHECK(bench.trainer.result().timeline.bars()[1].notes().contains(Beat(2.0)));
  CHECK(bench.placed() == 1);

  // Следующая нота — в долю нового такта: доля 3.0 такта 1 в то же время
  // перекрыта.
  bench.notes({3.52});
  REQUIRE(bench.notesAt(3, 1.0).size() == 1);
  CHECK(msIn(bench.notesAt(3, 1.0).front()) == doctest::Approx(20.0));
  CHECK_FALSE(bench.trainer.result().timeline.bars()[1].notes().contains(Beat(3.0)));
}

TEST_CASE("Тренировка: нота ложится в долю сетки такта, а не только в удар") {
  // Шестнадцатые: доля 0.25 — на 0.125 с.
  Bench bench;
  REQUIRE(bench.trainer.start().has_value());
  bench.bar(0, 4);
  bench.bar(1, 4);
  bench.notes({0.128});
  REQUIRE(bench.notesAt(0, 0.25).size() == 1);
  CHECK(msIn(bench.notesAt(0, 0.25).front()) == doctest::Approx(3.0));
}
