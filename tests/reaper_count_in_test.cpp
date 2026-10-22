#include <doctest/doctest.h>

#include "grid_timeline.hpp"

#include "training/core/training/state/timeline.hpp"
#include "training/reaper/count_in.hpp"

#include <cstdint>
#include <map>

using training::core::Interval;
using training::core::training::state::Beat;
using training::core::training::state::Meter;
using training::core::training::state::Timeline;
using training::reaper::barAt;
using training::reaper::CountIn;
using training::reaper::countInAt;
using training::reaper::countInOf;
using training::reaper::CountInSettings;
using training::reaper::CountInTimeline;
using training::reaper::Position;
using training::test::BarPlan;
using training::test::coreBars;
using training::test::MapTimeline;
using training::test::secondsOf;
using training::test::spanOf;
using training::test::SteadyTimeline;
using training::test::timeOf;

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 512;

/// Отсчёт перед записью на `bars` тактов при включённом метрономе.
CountInSettings recording(double bars, bool fromBarStart = false) {
  return {.metronome = true,
          .beforePlayback = false,
          .beforeRecording = true,
          .bars = bars,
          .fromBarStart = fromBarStart};
}

} // namespace

TEST_CASE("Отсчёт: 2 такта 4/4 при 120 BPM — 192000 сэмплов") {
  const CountIn countIn =
      countInOf(recording(2.0), true, spanOf(2.0), spanOf(0.0), kRate, kBlock);
  CHECK(countIn.samples == 192000);
  CHECK(countIn.length() / Interval::kSecond == doctest::Approx(4.0));
}

TEST_CASE("Отсчёт с начала такта: длина округляется вверх до целого блока") {
  // Запуск с третьего удара: путь от начала такта 1 с; 5.0 с = 468.75 блока.
  const CountIn countIn =
      countInOf(recording(2.0, true), true, spanOf(2.0), spanOf(1.0), kRate, kBlock);
  CHECK(countIn.samples == 240128);
  // Длина по тактам — без округления.
  CHECK(countIn.barsLength / Interval::kSecond == doctest::Approx(5.0));
}

TEST_CASE("Отсчёт: 2 такта при 90 BPM — 256000 сэмплов") {
  CHECK(
      countInOf(recording(2.0), true, spanOf(8.0 / 3.0), spanOf(0.0), kRate, kBlock).samples ==
      256000);
}

TEST_CASE("Отсчёт без начала такта путь от начала такта не учитывает") {
  CHECK(countInOf(recording(2.0), true, spanOf(2.0), spanOf(1.0), kRate, kBlock).samples ==
        192000);
}

TEST_CASE("Отсчёта нет без метронома, без отсчёта для запуска и без частоты") {
  CountInSettings off = recording(2.0);
  off.metronome = false;
  CHECK(countInOf(off, true, spanOf(2.0), spanOf(0.0), kRate, kBlock).samples == 0);
  CHECK(countInOf(recording(2.0), false, spanOf(2.0), spanOf(0.0), kRate, kBlock).samples ==
        0);
  CHECK(countInOf(recording(0.0), true, spanOf(2.0), spanOf(0.0), kRate, kBlock).samples == 0);
  CHECK(countInOf(recording(2.0), true, spanOf(2.0), spanOf(0.0), 0.0, kBlock).samples == 0);
}

TEST_CASE("Отсчёт перед воспроизведением") {
  const CountInSettings playback{.metronome = true,
                                 .beforePlayback = true,
                                 .beforeRecording = false,
                                 .bars = 1.0,
                                 .fromBarStart = false};
  // 96000 сэмплов — 187.5 блока: вверх до 188.
  CHECK(countInOf(playback, false, spanOf(2.0), spanOf(0.0), kRate, kBlock).samples == 96256);
  CHECK(countInOf(playback, true, spanOf(2.0), spanOf(0.0), kRate, kBlock).samples == 0);
}

TEST_CASE("Отсчёт без длины блока не округляется") {
  CHECK(countInOf(recording(2.0, true), true, spanOf(2.0), spanOf(1.0), kRate, 0).samples ==
        240000);
}

TEST_CASE("Сетка отсчёта: при одном темпе — карта проекта") {
  const SteadyTimeline project(120.0);
  const CountInTimeline timeline(project, timeOf(2.0), kRate);
  for (const double time : {-1.5, 3.0}) {
    CAPTURE(time);
    const Position counted = timeline.positionAt(timeOf(time));
    const Position projected = project.positionAt(timeOf(time));
    CHECK(counted.bar == projected.bar);
    CHECK(counted.beat == doctest::Approx(projected.beat));
  }
  const Position early{.bar = -2, .beat = 2.0};
  CHECK(secondsOf(timeline.timeAt(early)) ==
        doctest::Approx(secondsOf(project.timeAt(early))));
}

TEST_CASE("Сетка отсчёта: темп точки запуска продолжается назад") {
  // 120 BPM до 4.0 с (такты 1–2), дальше 90 BPM; запуск с 6.0 с — удар 11.
  const MapTimeline project(
      {BarPlan{.meter = Meter{.beats = 4, .unit = 4}, .bpm = 120.0, .inside = {}},
       BarPlan{.meter = Meter{.beats = 4, .unit = 4}, .bpm = 120.0, .inside = {}},
       BarPlan{.meter = Meter{.beats = 4, .unit = 4}, .bpm = 90.0, .inside = {}}});
  const CountInTimeline timeline(project, timeOf(6.0), kRate);
  // Клик отсчёта на 3.333 с — целая доля сетки отсчёта (удар 4 такта 2), у
  // проекта там не удар.
  const Position click = timeline.positionAt(timeOf(10.0 / 3.0));
  CHECK(click.bar == 1);
  CHECK(click.beat == doctest::Approx(3.0));
  CHECK(secondsOf(timeline.timeAt({.bar = 1, .beat = 3.0})) == doctest::Approx(10.0 / 3.0));
  CHECK(project.positionAt(timeOf(10.0 / 3.0)).beat == doctest::Approx(8.0 / 3.0));
  // Такт перед тактом точки запуска — размер и темп точки запуска, 4 удара
  // назад.
  const auto bar = timeline.barOf(1);
  CHECK(bar.meter == Meter{.beats = 4, .unit = 4});
  CHECK(bar.tempo == std::map<Beat, double>{{Beat(0.0), 90.0}});
  CHECK(secondsOf(timeline.timeAt({.bar = 1, .beat = 0.0})) == doctest::Approx(4.0 / 3.0));
  // С позиции начала — карта проекта.
  const Position after = timeline.positionAt(timeOf(7.0));
  CHECK(after.bar == project.positionAt(timeOf(7.0)).bar);
  CHECK(after.beat == doctest::Approx(project.positionAt(timeOf(7.0)).beat));
}

TEST_CASE("Сетка отсчёта: размер точки запуска продолжается назад") {
  // 3/4 до 3.0 с (два такта по 1.5 с), дальше 4/4; запуск с 5.0 с.
  const MapTimeline project(
      {BarPlan{.meter = Meter{.beats = 3, .unit = 4}, .bpm = 120.0, .inside = {}},
       BarPlan{.meter = Meter{.beats = 3, .unit = 4}, .bpm = 120.0, .inside = {}},
       BarPlan{.meter = Meter{.beats = 4, .unit = 4}, .bpm = 120.0, .inside = {}}});
  const CountInTimeline timeline(project, timeOf(5.0), kRate);
  const Position start = timeline.positionAt(timeOf(5.0));
  REQUIRE(start.bar == 3);
  const auto bar = timeline.barOf(start.bar - 2);
  CHECK(bar.meter == Meter{.beats = 4, .unit = 4});
  // Сильные доли отсчёта — на 1.0 и 3.0 с.
  CHECK(secondsOf(timeline.timeAt({.bar = bar.index, .beat = 0.0})) == doctest::Approx(1.0));
}

TEST_CASE("Отсчёт по карте темпа: длина такта и путь от его начала — в позиции начала") {
  const SteadyTimeline steady(120.0);
  CHECK(countInAt(recording(2.0), true, steady, timeOf(0.0), 1.0, kRate, kBlock).samples ==
        192000);
  CHECK(
      countInAt(recording(2.0, true), true, steady, timeOf(1.0), 1.0, kRate, kBlock).samples ==
      240128);

  const MapTimeline tempo(
      {BarPlan{.meter = Meter{.beats = 4, .unit = 4}, .bpm = 120.0, .inside = {}},
       BarPlan{.meter = Meter{.beats = 4, .unit = 4}, .bpm = 120.0, .inside = {}},
       BarPlan{.meter = Meter{.beats = 4, .unit = 4}, .bpm = 90.0, .inside = {}}});
  CHECK(countInAt(recording(2.0), true, tempo, timeOf(6.0), 1.0, kRate, kBlock).samples ==
        256000);

  // 2 такта 4/4 после тактов 3/4.
  const MapTimeline meter(
      {BarPlan{.meter = Meter{.beats = 3, .unit = 4}, .bpm = 120.0, .inside = {}},
       BarPlan{.meter = Meter{.beats = 3, .unit = 4}, .bpm = 120.0, .inside = {}},
       BarPlan{.meter = Meter{.beats = 4, .unit = 4}, .bpm = 120.0, .inside = {}}});
  CHECK(countInAt(recording(2.0), true, meter, timeOf(5.0), 1.0, kRate, kBlock).samples ==
        192000);
}

TEST_CASE("Сетка отсчёта: нота ровно на клик отсчёта при смене темпа — отклонение 0") {
  // 120 BPM до 4.0 с, 90 BPM с 4.0 с, запуск с 6.0 с: клик отсчёта на 3.333 с.
  const MapTimeline project(
      {BarPlan{.meter = Meter{.beats = 4, .unit = 4}, .bpm = 120.0, .inside = {}},
       BarPlan{.meter = Meter{.beats = 4, .unit = 4}, .bpm = 120.0, .inside = {}},
       BarPlan{.meter = Meter{.beats = 4, .unit = 4}, .bpm = 90.0, .inside = {}}});
  const CountInTimeline timeline(project, timeOf(6.0), kRate);
  const std::int64_t bar = timeline.positionAt(timeOf(10.0 / 3.0)).bar;
  const Timeline bars = coreBars(timeline, bar - 1, bar + 1, 1);
  const auto near = bars.nearest(timeOf(10.0 / 3.0));
  REQUIRE(near);
  // Время шкалы — целые наносекунды: отклонение не дальше пары наносекунд от
  // нуля.
  CHECK((timeOf(10.0 / 3.0) - *bars.time(*near)) / Interval::kSecond ==
        doctest::Approx(0.0).epsilon(1e-8));
}

TEST_CASE("Такт позиции: позиция меньше чем на сэмпл раньше начала такта — в нём") {
  // 110 BPM: такт 5 начинается на сэмпле 418909.09, хост отдаёт позицию 418909.
  const SteadyTimeline steady(110.0);
  CHECK(barAt(steady, steady.beatTime(16.0), kRate).index == 4);
  CHECK(barAt(steady, timeOf(418909.0 / kRate), kRate).index == 4);
  CHECK(barAt(steady, timeOf(418907.0 / kRate), kRate).index == 3);
}

TEST_CASE("Отсчёт с начала такта: позиция за долю сэмпла до такта 5 при 110 BPM — 209920 "
          "сэмплов") {
  const SteadyTimeline steady(110.0);
  const CountIn countIn = countInAt(recording(2.0, true), true, steady,
                                    timeOf(418909.0 / kRate), 1.0, kRate, kBlock);
  CHECK(countIn.samples == 209920);
  CHECK(countIn.barsLength / Interval::kSecond == doctest::Approx(8.0 * 60.0 / 110.0));
}

TEST_CASE("Сетка отсчёта: позиция за долю сэмпла до такта — размер этого такта") {
  // Такты 1–4 — 3/4, с такта 5 — 4/4, 110 BPM: такт 5 начинается на сэмпле
  // 314181.82, хост отдаёт позицию 314181.
  const BarPlan waltz{.meter = Meter{.beats = 3, .unit = 4}, .bpm = 110.0, .inside = {}};
  const MapTimeline project(
      {waltz, waltz, waltz, waltz,
       BarPlan{.meter = Meter{.beats = 4, .unit = 4}, .bpm = 110.0, .inside = {}}});
  const CountInTimeline timeline(project, timeOf(314181.0 / kRate), kRate);
  // Такт перед тактом 5 — такт отсчёта 4/4 с удара 8 от начала проекта.
  const auto bar = timeline.barOf(3);
  CHECK(bar.index == 3);
  CHECK(bar.meter == Meter{.beats = 4, .unit = 4});
  CHECK(secondsOf(timeline.timeAt({.bar = 3, .beat = 0.0})) ==
        doctest::Approx(8.0 * 60.0 / 110.0));
}
