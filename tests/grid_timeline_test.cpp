#include <doctest/doctest.h>

#include "grid_timeline.hpp"

#include "training/core/training/state/timeline.hpp"

#include <cstdint>
#include <map>

using training::core::training::state::Beat;
using training::core::training::state::Meter;
using training::reaper::Position;
using training::test::MapTimeline;
using training::test::secondsOf;
using training::test::SteadyTimeline;
using training::test::timeOf;

namespace {

/// Такты 4/4, 4/4, 7/8, 6/8, 3/4 при 120; во втором такте 4/4 темп 150 с
/// начала и 100 с третьего удара.
MapTimeline mixed() {
  return MapTimeline({
      {.meter = {.beats = 4, .unit = 4}, .bpm = 120.0, .inside = {}},
      {.meter = {.beats = 4, .unit = 4}, .bpm = 150.0, .inside = {{2.0, 100.0}}},
      {.meter = {.beats = 7, .unit = 8}, .bpm = 100.0, .inside = {}},
      {.meter = {.beats = 6, .unit = 8}, .bpm = 100.0, .inside = {}},
      {.meter = {.beats = 3, .unit = 4}, .bpm = 100.0, .inside = {}},
  });
}

/// Проверяет, что позиция `actual` — такт `bar` и доля `beat`.
void checkPosition(Position actual, std::int64_t bar, double beat) {
  CHECK(actual.bar == bar);
  CHECK(actual.beat == doctest::Approx(beat));
}

} // namespace

TEST_CASE("карта темпа: позиции по числу ударов в тактах 4/4, 4/4, 7/8, 6/8, 3/4") {
  const MapTimeline timeline = mixed();

  // Такт 7/8 начинается с удара 8 от начала проекта.
  checkPosition(timeline.positionAt(timeline.beatTime(9.5)), 2, 1.5);
  CHECK(timeline.barOf(2).meter == Meter{.beats = 7, .unit = 8});

  checkPosition(timeline.positionAt(timeline.beatTime(15.5)), 3, 0.5);
  CHECK(timeline.barOf(3).meter == Meter{.beats = 6, .unit = 8});

  checkPosition(timeline.positionAt(timeline.beatTime(23.5)), 4, 2.5);
  CHECK(timeline.barOf(4).meter.beats == 3);

  // За последним заданным тактом такты повторяют его размер.
  checkPosition(timeline.positionAt(timeline.beatTime(25.5)), 5, 1.5);
  CHECK(timeline.barOf(5).meter.beats == 3);
}

TEST_CASE("карта темпа: темп такта — с начала такта и с третьего удара") {
  const MapTimeline timeline = mixed();

  CHECK(timeline.barOf(1).tempo ==
        std::map<Beat, double>{{Beat(0.0), 150.0}, {Beat(2.0), 100.0}});
  CHECK(timeline.barOf(0).tempo == std::map<Beat, double>{{Beat(0.0), 120.0}});
}

TEST_CASE("карта темпа: у такта со сменой размера без смены темпа — темп начала") {
  const MapTimeline timeline = mixed();

  // Такт 7/8 после такта, который кончился темпом 100.
  const auto bar = timeline.barOf(2);
  CHECK(bar.meter == Meter{.beats = 7, .unit = 8});
  CHECK(bar.tempo == std::map<Beat, double>{{Beat(0.0), 100.0}});
}

TEST_CASE("карта темпа: время долей по темпу и размеру") {
  const MapTimeline timeline = mixed();

  // 4 удара по 0.5 с, 2 по 0.4 с, 2 по 0.6 с, затем восьмые при 100 — по 0.3 с.
  CHECK(secondsOf(timeline.timeAt({.bar = 1, .beat = 0.0})) == doctest::Approx(2.0));
  CHECK(secondsOf(timeline.timeAt({.bar = 1, .beat = 2.0})) == doctest::Approx(2.8));
  CHECK(secondsOf(timeline.timeAt({.bar = 2, .beat = 0.0})) == doctest::Approx(4.0));
  CHECK(secondsOf(timeline.timeAt({.bar = 2, .beat = 1.0})) == doctest::Approx(4.3));
  checkPosition(timeline.positionAt(timeOf(4.3)), 2, 1.0);
  checkPosition(timeline.positionAt(timeOf(2.4)), 1, 1.0);
}

TEST_CASE("карта темпа: постоянная карта считает такты и до начала проекта") {
  const SteadyTimeline timeline(120.0, 4);

  checkPosition(timeline.positionAt(timeline.beatTime(7.5)), 1, 3.5);
  checkPosition(timeline.positionAt(timeline.beatTime(-0.5)), -1, 3.5);
  CHECK(secondsOf(timeline.timeAt({.bar = -1, .beat = 0.0})) == doctest::Approx(-2.0));
}
