#include <doctest/doctest.h>

#include "grid_timeline.hpp"

#include "training/grid/bars.hpp"
#include "training/trainer/count_in.hpp"

#include <cmath>
#include <cstdint>

using training::grid::Bars;
using training::grid::HitWindow;
using training::grid::Meter;
using training::test::BarPlan;
using training::test::MapTimeline;
using training::test::SteadyTimeline;
using training::trainer::barAt;
using training::trainer::CountIn;
using training::trainer::countInAt;
using training::trainer::countInOf;
using training::trainer::CountInSettings;
using training::trainer::CountInTimeline;

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
  const CountIn countIn = countInOf(recording(2.0), true, 2.0, 0.0, kRate, kBlock);
  CHECK(countIn.samples == 192000);
  CHECK(countIn.seconds() == doctest::Approx(4.0));
}

TEST_CASE("Отсчёт с начала такта: длина округляется вверх до целого блока") {
  // Запуск с третьего удара: путь от начала такта 1 с; 5.0 с = 468.75 блока.
  const CountIn countIn = countInOf(recording(2.0, true), true, 2.0, 1.0, kRate, kBlock);
  CHECK(countIn.samples == 240128);
  // Длина по тактам — без округления.
  CHECK(countIn.barsSeconds == doctest::Approx(5.0));
}

TEST_CASE("Отсчёт: 2 такта при 90 BPM — 256000 сэмплов") {
  CHECK(countInOf(recording(2.0), true, 8.0 / 3.0, 0.0, kRate, kBlock).samples == 256000);
}

TEST_CASE("Отсчёт без начала такта путь от начала такта не учитывает") {
  CHECK(countInOf(recording(2.0), true, 2.0, 1.0, kRate, kBlock).samples == 192000);
}

TEST_CASE("Отсчёта нет без метронома, без отсчёта для запуска и без частоты") {
  CountInSettings off = recording(2.0);
  off.metronome = false;
  CHECK(countInOf(off, true, 2.0, 0.0, kRate, kBlock).samples == 0);
  CHECK(countInOf(recording(2.0), false, 2.0, 0.0, kRate, kBlock).samples == 0);
  CHECK(countInOf(recording(0.0), true, 2.0, 0.0, kRate, kBlock).samples == 0);
  CHECK(countInOf(recording(2.0), true, 2.0, 0.0, 0.0, kBlock).samples == 0);
}

TEST_CASE("Отсчёт перед воспроизведением") {
  const CountInSettings playback{.metronome = true,
                                 .beforePlayback = true,
                                 .beforeRecording = false,
                                 .bars = 1.0,
                                 .fromBarStart = false};
  // 96000 сэмплов — 187.5 блока: вверх до 188.
  CHECK(countInOf(playback, false, 2.0, 0.0, kRate, kBlock).samples == 96256);
  CHECK(countInOf(playback, true, 2.0, 0.0, kRate, kBlock).samples == 0);
}

TEST_CASE("Отсчёт без длины блока не округляется") {
  CHECK(countInOf(recording(2.0, true), true, 2.0, 1.0, kRate, 0).samples == 240000);
}

TEST_CASE("Сетка отсчёта: при одном темпе — карта проекта") {
  const SteadyTimeline project(120.0);
  const CountInTimeline timeline(project, 2.0, kRate);
  CHECK(timeline.beatsAt(-1.5) == doctest::Approx(project.beatsAt(-1.5)));
  CHECK(timeline.timeAt(-3.0) == doctest::Approx(project.timeAt(-3.0)));
  CHECK(timeline.beatsAt(3.0) == doctest::Approx(project.beatsAt(3.0)));
}

TEST_CASE("Сетка отсчёта: темп точки запуска продолжается назад") {
  // 120 BPM до 4.0 с (такты 1–2), дальше 90 BPM; запуск с 6.0 с — удар 11.
  const MapTimeline project(
      {BarPlan{.meter = Meter{.beats = 4, .unit = 4}, .bpm = 120.0, .inside = {}},
       BarPlan{.meter = Meter{.beats = 4, .unit = 4}, .bpm = 120.0, .inside = {}},
       BarPlan{.meter = Meter{.beats = 4, .unit = 4}, .bpm = 90.0, .inside = {}}});
  const CountInTimeline timeline(project, 6.0, kRate);
  // Клик отсчёта на 3.333 с — целый удар сетки отсчёта, у проекта там не удар.
  CHECK(timeline.beatsAt(10.0 / 3.0) == doctest::Approx(7.0));
  CHECK(timeline.timeAt(7.0) == doctest::Approx(10.0 / 3.0));
  CHECK(project.beatsAt(10.0 / 3.0) == doctest::Approx(20.0 / 3.0));
  // Такт перед тактом точки запуска — размер точки запуска, 4 удара назад.
  const auto bar = timeline.barOf(7);
  CHECK(bar.index == 1);
  CHECK(bar.firstBeat == 4);
  CHECK(timeline.timeAt(static_cast<double>(bar.firstBeat)) == doctest::Approx(4.0 / 3.0));
  // С позиции начала — карта проекта.
  CHECK(timeline.beatsAt(7.0) == doctest::Approx(project.beatsAt(7.0)));
}

TEST_CASE("Сетка отсчёта: размер точки запуска продолжается назад") {
  // 3/4 до 3.0 с (два такта по 1.5 с), дальше 4/4; запуск с 5.0 с.
  const MapTimeline project(
      {BarPlan{.meter = Meter{.beats = 3, .unit = 4}, .bpm = 120.0, .inside = {}},
       BarPlan{.meter = Meter{.beats = 3, .unit = 4}, .bpm = 120.0, .inside = {}},
       BarPlan{.meter = Meter{.beats = 4, .unit = 4}, .bpm = 120.0, .inside = {}}});
  const CountInTimeline timeline(project, 5.0, kRate);
  const auto start = static_cast<std::int64_t>(std::llround(timeline.beatsAt(5.0)));
  const auto bar = timeline.barOf(start - 5);
  CHECK(bar.meter == Meter{.beats = 4, .unit = 4});
  // Сильные доли отсчёта — на 1.0 и 3.0 с.
  CHECK(timeline.timeAt(static_cast<double>(bar.firstBeat)) == doctest::Approx(1.0));
}

TEST_CASE("Отсчёт по карте темпа: длина такта и путь от его начала — в позиции начала") {
  const SteadyTimeline steady(120.0);
  CHECK(countInAt(recording(2.0), true, steady, 0.0, 1.0, kRate, kBlock).samples == 192000);
  CHECK(countInAt(recording(2.0, true), true, steady, 1.0, 1.0, kRate, kBlock).samples ==
        240128);

  const MapTimeline tempo(
      {BarPlan{.meter = Meter{.beats = 4, .unit = 4}, .bpm = 120.0, .inside = {}},
       BarPlan{.meter = Meter{.beats = 4, .unit = 4}, .bpm = 120.0, .inside = {}},
       BarPlan{.meter = Meter{.beats = 4, .unit = 4}, .bpm = 90.0, .inside = {}}});
  CHECK(countInAt(recording(2.0), true, tempo, 6.0, 1.0, kRate, kBlock).samples == 256000);

  // 2 такта 4/4 после тактов 3/4.
  const MapTimeline meter(
      {BarPlan{.meter = Meter{.beats = 3, .unit = 4}, .bpm = 120.0, .inside = {}},
       BarPlan{.meter = Meter{.beats = 3, .unit = 4}, .bpm = 120.0, .inside = {}},
       BarPlan{.meter = Meter{.beats = 4, .unit = 4}, .bpm = 120.0, .inside = {}}});
  CHECK(countInAt(recording(2.0), true, meter, 5.0, 1.0, kRate, kBlock).samples == 192000);
}

TEST_CASE("Сетка отсчёта: нота ровно на клик отсчёта при смене темпа — отклонение 0") {
  // 120 BPM до 4.0 с, 90 BPM с 4.0 с, запуск с 6.0 с: клик отсчёта на 3.333 с.
  const MapTimeline project(
      {BarPlan{.meter = Meter{.beats = 4, .unit = 4}, .bpm = 120.0, .inside = {}},
       BarPlan{.meter = Meter{.beats = 4, .unit = 4}, .bpm = 120.0, .inside = {}},
       BarPlan{.meter = Meter{.beats = 4, .unit = 4}, .bpm = 90.0, .inside = {}}});
  const CountInTimeline timeline(project, 6.0, kRate);
  const HitWindow window{.offsetMs = 0.0, .toleranceMs = 10.0};
  Bars bars;
  bars.play(timeline.beatsAt(10.0 / 3.0), 0.0, window, timeline);
  bars.addNote(10.0 / 3.0, 1.0, window, timeline);
  const auto &row = bars.rows().front();
  REQUIRE(row.mean.has_value() == false);
  bool found = false;
  for (const auto &beat : row.beats)
    for (const auto &slot : beat.slots)
      if (slot.deviation) {
        CHECK(*slot.deviation == doctest::Approx(0.0).epsilon(1e-9));
        found = true;
      }
  CHECK(found);
}

TEST_CASE("Такт позиции: позиция меньше чем на сэмпл раньше начала такта — в нём") {
  // 110 BPM: такт 5 начинается на сэмпле 418909.09, хост отдаёт позицию 418909.
  const SteadyTimeline steady(110.0);
  CHECK(barAt(steady, steady.timeAt(16.0), kRate).index == 4);
  CHECK(barAt(steady, 418909.0 / kRate, kRate).index == 4);
  CHECK(barAt(steady, 418907.0 / kRate, kRate).index == 3);
}

TEST_CASE("Отсчёт с начала такта: позиция за долю сэмпла до такта 5 при 110 BPM — 209920 "
          "сэмплов") {
  const SteadyTimeline steady(110.0);
  const CountIn countIn =
      countInAt(recording(2.0, true), true, steady, 418909.0 / kRate, 1.0, kRate, kBlock);
  CHECK(countIn.samples == 209920);
  CHECK(countIn.barsSeconds == doctest::Approx(8.0 * 60.0 / 110.0));
}

TEST_CASE("Сетка отсчёта: позиция за долю сэмпла до такта — размер этого такта") {
  // Такты 1–4 — 3/4, с такта 5 — 4/4, 110 BPM: такт 5 начинается на сэмпле
  // 314181.82, хост отдаёт позицию 314181.
  const BarPlan waltz{.meter = Meter{.beats = 3, .unit = 4}, .bpm = 110.0, .inside = {}};
  const MapTimeline project(
      {waltz, waltz, waltz, waltz,
       BarPlan{.meter = Meter{.beats = 4, .unit = 4}, .bpm = 110.0, .inside = {}}});
  const CountInTimeline timeline(project, 314181.0 / kRate, kRate);
  const auto bar = timeline.barOf(11);
  CHECK(bar.index == 3);
  CHECK(bar.firstBeat == 8);
  CHECK(bar.meter == Meter{.beats = 4, .unit = 4});
}
