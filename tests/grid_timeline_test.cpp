#include <doctest/doctest.h>

#include "grid_timeline.hpp"

#include "training/grid/timeline.hpp"

#include <cstdint>

using training::grid::Meter;
using training::test::MapTimeline;
using training::test::SteadyTimeline;

namespace {

/// Такты 4/4, 7/8, 6/8, 3/4 при 120; во втором такте 4/4 темп 150 с начала и
/// 100 с третьего удара.
MapTimeline mixed() {
  return MapTimeline({
      {.meter = {.beats = 4, .unit = 4}, .bpm = 120.0, .inside = {}},
      {.meter = {.beats = 4, .unit = 4}, .bpm = 150.0, .inside = {{2.0, 100.0}}},
      {.meter = {.beats = 7, .unit = 8}, .bpm = 100.0, .inside = {}},
      {.meter = {.beats = 6, .unit = 8}, .bpm = 100.0, .inside = {}},
      {.meter = {.beats = 3, .unit = 4}, .bpm = 100.0, .inside = {}},
  });
}

} // namespace

TEST_CASE("карта темпа: число ударов в тактах 4/4, 7/8, 6/8, 3/4") {
  const MapTimeline timeline = mixed();

  const auto third = timeline.barOf(9);
  CHECK(third.index == 2);
  CHECK(third.firstBeat == 8);
  CHECK(third.meter == Meter{.beats = 7, .unit = 8});

  const auto fourth = timeline.barOf(15);
  CHECK(fourth.index == 3);
  CHECK(fourth.firstBeat == 15);
  CHECK(fourth.meter == Meter{.beats = 6, .unit = 8});

  const auto fifth = timeline.barOf(23);
  CHECK(fifth.index == 4);
  CHECK(fifth.firstBeat == 21);
  CHECK(fifth.meter.beats == 3);

  // За последним заданным тактом такты повторяют его размер.
  const auto beyond = timeline.barOf(25);
  CHECK(beyond.index == 5);
  CHECK(beyond.firstBeat == 24);
  CHECK(beyond.meter.beats == 3);
}

TEST_CASE("карта темпа: смена темпа в начале такта и на третьем ударе") {
  const MapTimeline timeline = mixed();

  const auto bar = timeline.barOf(5);
  REQUIRE(bar.changes.size() == 2);

  CHECK(bar.changes[0].beat == 0.0);
  CHECK_FALSE(bar.changes[0].meterFrom);
  CHECK(bar.changes[0].tempoFrom == 120.0);
  CHECK(bar.changes[0].tempoTo == 150.0);

  CHECK(bar.changes[1].beat == 2.0);
  CHECK(bar.changes[1].tempoFrom == 150.0);
  CHECK(bar.changes[1].tempoTo == 100.0);
}

TEST_CASE("карта темпа: смена размера без смены темпа") {
  const MapTimeline timeline = mixed();

  // Такт 7/8 после такта, который кончился темпом 100: меняется только размер.
  const auto bar = timeline.barOf(8);
  REQUIRE(bar.changes.size() == 1);
  CHECK(bar.changes[0].meterFrom == Meter{.beats = 4, .unit = 4});
  CHECK(bar.changes[0].meterTo == Meter{.beats = 7, .unit = 8});
  CHECK_FALSE(bar.changes[0].tempoFrom);

  CHECK(timeline.barOf(0).changes.empty());
}

TEST_CASE("карта темпа: время ударов по темпу и размеру") {
  const MapTimeline timeline = mixed();

  // 4 удара по 0.5 с, 2 по 0.4 с, 2 по 0.6 с, затем восьмые при 100 — по 0.3 с.
  CHECK(timeline.timeAt(4.0) == doctest::Approx(2.0));
  CHECK(timeline.timeAt(6.0) == doctest::Approx(2.8));
  CHECK(timeline.timeAt(8.0) == doctest::Approx(4.0));
  CHECK(timeline.timeAt(9.0) == doctest::Approx(4.3));
  CHECK(timeline.beatsAt(4.3) == doctest::Approx(9.0));
  CHECK(timeline.beatsAt(2.4) == doctest::Approx(5.0));
}

TEST_CASE("карта темпа: постоянная карта считает такты и для отрицательных ударов") {
  const SteadyTimeline timeline(120.0, 4);

  CHECK(timeline.barOf(7).index == 1);
  CHECK(timeline.barOf(7).firstBeat == 4);
  CHECK(timeline.barOf(-1).index == -1);
  CHECK(timeline.barOf(-1).firstBeat == -4);
}
