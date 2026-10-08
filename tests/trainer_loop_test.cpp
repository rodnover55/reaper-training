#include <doctest/doctest.h>

#include "grid_timeline.hpp"

#include "training/trainer/loop.hpp"

using training::grid::Mode;
using training::test::SteadyTimeline;
using training::trainer::Loop;
using training::trainer::loopBeats;
using training::trainer::loopWrapped;

namespace {

/// Петля со 2 по 4 с — четыре удара при 120 ударах в минуту.
constexpr Loop kLoop{.repeat = true, .start = 2.0, .end = 4.0};

} // namespace

TEST_CASE("Петля: время ноты в петле с повтором заворачивается") {
  // Удар — 0.5 с.
  const SteadyTimeline timeline(120.0);
  // Окно заворота в четвертях сдвинуто назад на полудара: [1.75, 3.75).
  CHECK(loopWrapped(4.1, 3.9, kLoop, Mode::Quarters, timeline) == doctest::Approx(2.1));
  CHECK(loopWrapped(3.8, 3.9, kLoop, Mode::Quarters, timeline) == doctest::Approx(1.8));
  // Ранняя нота к первой доле петли остаётся у её начала.
  CHECK(loopWrapped(1.8, 2.0, kLoop, Mode::Quarters, timeline) == doctest::Approx(1.8));
  // В шестнадцатых сдвиг — половина шестнадцатой: [1.9375, 3.9375).
  CHECK(loopWrapped(3.95, 3.9, kLoop, Mode::Sixteenths, timeline) == doctest::Approx(1.95));
  CHECK(loopWrapped(3.9, 3.9, kLoop, Mode::Sixteenths, timeline) == doctest::Approx(3.9));
}

TEST_CASE("Петля: без повтора, без длины и вне петли время не меняется") {
  // Удар — 0.5 с.
  const SteadyTimeline timeline(120.0);
  Loop off = kLoop;
  off.repeat = false;
  CHECK(loopWrapped(4.1, 3.9, off, Mode::Quarters, timeline) == 4.1);

  constexpr Loop empty{.repeat = true, .start = 2.0, .end = 2.0};
  CHECK(loopWrapped(4.1, 2.0, empty, Mode::Quarters, timeline) == 4.1);

  // Позиция блока за петлёй: нота не из петли.
  CHECK(loopWrapped(4.1, 4.05, kLoop, Mode::Quarters, timeline) == 4.1);
  CHECK(loopWrapped(1.5, 1.9, kLoop, Mode::Quarters, timeline) == 1.5);
  // Границы петли — в петле.
  CHECK(loopWrapped(4.1, 4.0, kLoop, Mode::Quarters, timeline) == doctest::Approx(2.1));
}

TEST_CASE("Петля: длина в ударах — только когда позиция в петле с повтором") {
  // Удар — 0.5 с.
  const SteadyTimeline timeline(120.0);
  CHECK(loopBeats(3.0, kLoop, timeline) == doctest::Approx(4.0));
  CHECK(loopBeats(2.0, kLoop, timeline) == doctest::Approx(4.0));
  CHECK(loopBeats(4.5, kLoop, timeline) == 0.0);
  Loop off = kLoop;
  off.repeat = false;
  CHECK(loopBeats(3.0, off, timeline) == 0.0);
}
