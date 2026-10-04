#include <doctest/doctest.h>

#include "grid_timeline.hpp"

#include "training/grid/nodes.hpp"

using training::grid::Mode;
using training::grid::nearestNode;
using training::test::RampTimeline;
using training::test::SteadyTimeline;

TEST_CASE("узлы: шестнадцатые при 120 BPM идут через 125 мс") {
  const SteadyTimeline timeline(120.0);

  for (int slot = 0; slot < 4; ++slot) {
    CAPTURE(slot);
    const double time = 1.5 + 0.125 * slot;
    const auto hit = nearestNode(time, Mode::Sixteenths, timeline);
    CHECK(hit.beat == 3);
    CHECK(hit.slot == slot);
    CHECK(hit.time == doctest::Approx(time));
  }
}

TEST_CASE("узлы: нота дальше половины интервала относится к следующему узлу") {
  const SteadyTimeline timeline(120.0);

  // +70 мс от узла 1.5 — ближе к узлу 1.625, отклонение −55 мс.
  const auto hit = nearestNode(1.5 + 0.070, Mode::Sixteenths, timeline);
  CHECK(hit.beat == 3);
  CHECK(hit.slot == 1);
  CHECK(1.570 - hit.time == doctest::Approx(-0.055));
}

TEST_CASE("узлы: ранняя нота достаётся первому узлу следующего удара") {
  const SteadyTimeline timeline(120.0);

  // За 15 мс до удара 4 (2.0 с) — в четвертях и в шестнадцатых.
  for (const Mode mode : {Mode::Quarters, Mode::Sixteenths}) {
    const auto hit = nearestNode(1.985, mode, timeline);
    CHECK(hit.beat == 4);
    CHECK(hit.slot == 0);
    CHECK(1.985 - hit.time == doctest::Approx(-0.015));
  }
}

TEST_CASE("узлы: число узлов на удар по режимам") {
  const SteadyTimeline timeline(60.0); // удар — 1 с

  CHECK(nearestNode(10.5, Mode::Eighths, timeline).slot == 1);
  CHECK(nearestNode(10.0 + 2.0 / 3.0, Mode::EighthTriplets, timeline).slot == 2);
  CHECK(nearestNode(10.75, Mode::Sixteenths, timeline).slot == 3);
  CHECK(nearestNode(10.0 + 5.0 / 6.0, Mode::SixteenthTriplets, timeline).slot == 5);
}

TEST_CASE("узлы: при плавной смене темпа узел делит удар в ударах, а не в секундах") {
  const RampTimeline timeline(100.0, 140.0, 8.0);

  // Восьмая внутри удара 3 разгоняющегося темпа: узел — timeAt(3.5), не середина
  // удара в секундах.
  const double half = timeline.timeAt(3.5);
  const double middle = (timeline.timeAt(3.0) + timeline.timeAt(4.0)) / 2.0;
  REQUIRE(half != doctest::Approx(middle).epsilon(1e-6));

  const auto hit = nearestNode(half, Mode::Eighths, timeline);
  CHECK(hit.beat == 3);
  CHECK(hit.slot == 1);
  CHECK(hit.time == doctest::Approx(half));
}
