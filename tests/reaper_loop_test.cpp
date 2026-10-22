#include <doctest/doctest.h>

#include "grid_timeline.hpp"

#include "training/reaper/loop.hpp"

using training::core::Interval;
using training::core::Mode;
using training::reaper::Loop;
using training::reaper::loopLength;
using training::reaper::loopWrapped;
using training::test::spanOf;
using training::test::SteadyTimeline;
using training::test::timeOf;

namespace {

/// Петля со 2 по 4 с — четыре удара при 120 ударах в минуту.
const Loop kLoop{.repeat = true, .start = timeOf(2.0), .end = timeOf(4.0)};

/// Время ноты `time` секунд, завёрнутое в петлю `loop` по позиции блока
/// `position` секунд (`loopWrapped`).
training::core::Moment wrapped(double time, double position, const Loop &loop, Mode mode,
                               const SteadyTimeline &timeline) {
  return loopWrapped(timeOf(time), timeOf(position), loop, mode, timeline);
}

} // namespace

TEST_CASE("Петля: время ноты в петле с повтором заворачивается") {
  // Удар — 0.5 с.
  const SteadyTimeline timeline(120.0);
  // Окно заворота в четвертях сдвинуто назад на полудара: [1.75, 3.75).
  CHECK(wrapped(4.1, 3.9, kLoop, Mode::Quarters, timeline) == timeOf(2.1));
  CHECK(wrapped(3.8, 3.9, kLoop, Mode::Quarters, timeline) == timeOf(1.8));
  // Ранняя нота к первой доле петли остаётся у её начала.
  CHECK(wrapped(1.8, 2.0, kLoop, Mode::Quarters, timeline) == timeOf(1.8));
  // В шестнадцатых сдвиг — половина шестнадцатой: [1.9375, 3.9375).
  CHECK(wrapped(3.95, 3.9, kLoop, Mode::Sixteenths, timeline) == timeOf(1.95));
  CHECK(wrapped(3.9, 3.9, kLoop, Mode::Sixteenths, timeline) == timeOf(3.9));
}

TEST_CASE("Петля: без повтора, без длины и вне петли время не меняется") {
  // Удар — 0.5 с.
  const SteadyTimeline timeline(120.0);
  Loop off = kLoop;
  off.repeat = false;
  CHECK(wrapped(4.1, 3.9, off, Mode::Quarters, timeline) == timeOf(4.1));

  const Loop empty{.repeat = true, .start = timeOf(2.0), .end = timeOf(2.0)};
  CHECK(wrapped(4.1, 2.0, empty, Mode::Quarters, timeline) == timeOf(4.1));

  // Позиция блока за петлёй: нота не из петли.
  CHECK(wrapped(4.1, 4.05, kLoop, Mode::Quarters, timeline) == timeOf(4.1));
  CHECK(wrapped(1.5, 1.9, kLoop, Mode::Quarters, timeline) == timeOf(1.5));
  // Границы петли — в петле.
  CHECK(wrapped(4.1, 4.0, kLoop, Mode::Quarters, timeline) == timeOf(2.1));
}

TEST_CASE("Петля: длина — только когда позиция в петле с повтором") {
  CHECK(loopLength(timeOf(3.0), kLoop) == spanOf(2.0));
  CHECK(loopLength(timeOf(2.0), kLoop) == spanOf(2.0));
  CHECK(loopLength(timeOf(4.5), kLoop) == spanOf(0.0));
  Loop off = kLoop;
  off.repeat = false;
  CHECK(loopLength(timeOf(3.0), off) == spanOf(0.0));
}
