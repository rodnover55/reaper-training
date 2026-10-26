#include <doctest/doctest.h>

#include "grid_timeline.hpp"

#include "training/core/hit_window.hpp"
#include "training/core/training/state/timeline.hpp"
#include "training/render/main/run_summary.hpp"

#include <cstddef>

using training::core::HitWindow;
using training::core::Interval;
using training::core::training::state::Beat;
using training::core::training::state::Timeline;
using training::render::kSummaryNotes;
using training::render::RunSummary;
using training::render::Tone;
using training::test::coreBars;
using training::test::msOf;
using training::test::SteadyTimeline;

namespace {

/// Окно попадания тестов: 0 ± 10 мс.
constexpr HitWindow kWindow = *HitWindow::create(Interval(), 10 * Interval::kMillisecond);

/// Такты запуска в 4/4 при 120 BPM: предтакт и `bars` тактов, доля — удар.
Timeline runOf(int bars) {
  static const SteadyTimeline steady(120.0);
  return coreBars(steady, -1, bars - 1, 1);
}

/// Кладёт в удар `beat` такта номер `index` списка ноту с разницей `ms` мс.
void note(Timeline &timeline, std::size_t index, double beat, double ms) {
  const Interval deviation = msOf(ms);
  REQUIRE(timeline.addNote({.bar = index, .division = Beat(beat)},
                           {.deviation = deviation, .hit = kWindow.hit(deviation)}));
}

} // namespace

TEST_CASE("сводка запуска: без нот сводки нет") {
  RunSummary summary;
  summary.setRun(runOf(2), kWindow, true, 3);
  CHECK_FALSE(summary.view());
}

TEST_CASE("сводка запуска: меньше 128 нот — прогрев по тем, что есть") {
  Timeline run = runOf(1);
  note(run, 1, 0.0, -4.0);
  note(run, 1, 1.0, 2.0);
  note(run, 1, 2.0, 6.0);
  note(run, 1, 3.0, 4.0);

  RunSummary summary;
  summary.setRun(run, kWindow, true, run.bars().size());
  REQUIRE(summary.view());
  const auto &view = *summary.view();
  CHECK(view.notes == 4);
  CHECK(view.warmUp);
  CHECK(view.median.text == "+3.0");
  CHECK(view.median.tone == Tone::Neutral);
  CHECK(view.iqr.text == "4.0");
  CHECK(view.score.text == "100");
}

TEST_CASE("сводка запуска: последние 128 нот, по одной на долю") {
  // 36 тактов по 4 ноты: первые 4 такта — +50, остальные 32 такта — 128 нот
  // +2; в последней доле лишняя нота +30 не идёт в сводку.
  const int bars = 36;
  Timeline run = runOf(bars);
  for (int bar = 1; bar <= bars; ++bar)
    for (int beat = 0; beat < 4; ++beat)
      note(run, static_cast<std::size_t>(bar), beat, bar <= 4 ? 50.0 : 2.0);
  note(run, static_cast<std::size_t>(bars), 3.0, 30.0);

  RunSummary summary;
  summary.setRun(run, kWindow, true, run.bars().size());
  REQUIRE(summary.view());
  const auto &view = *summary.view();
  CHECK(view.notes == kSummaryNotes);
  CHECK_FALSE(view.warmUp);
  CHECK(view.median.text == "+2.0");
  CHECK(view.median.tone == Tone::Good);
  CHECK(view.iqr.text == "0.0");
  CHECK(view.score.text == "100");
}

TEST_CASE("сводка запуска: после остановки — все ноты запуска без прогрева") {
  // 36 тактов по 4 ноты: первые 4 такта — +50, остальные — +2; медиана и
  // IQR — по всем 144 нотам.
  const int bars = 36;
  Timeline run = runOf(bars);
  for (int bar = 1; bar <= bars; ++bar)
    for (int beat = 0; beat < 4; ++beat)
      note(run, static_cast<std::size_t>(bar), beat, bar <= 4 ? 50.0 : 2.0);

  RunSummary summary;
  summary.setRun(run, kWindow, false, run.bars().size());
  REQUIRE(summary.view());
  const auto &view = *summary.view();
  CHECK(view.notes == 144);
  CHECK(view.whole);
  CHECK_FALSE(view.warmUp);
  CHECK(view.median.text == "+2.0");
  CHECK(view.median.tone == Tone::Good);
  // 16 нот по 0 баллов с весом 3 и 128 по 1 с весом 1: 128 / 176.
  CHECK(view.score.text == "73");

  // Меньше 128 нот после остановки — тоже не прогрев.
  Timeline shortRun = runOf(1);
  note(shortRun, 1, 0.0, 1.0);
  summary.setRun(shortRun, kWindow, false, shortRun.bars().size());
  REQUIRE(summary.view());
  CHECK(summary.view()->notes == 1);
  CHECK_FALSE(summary.view()->warmUp);
  CHECK(summary.view()->median.tone == Tone::Good);
}

TEST_CASE("сводка запуска: такты от конца списка не идут в сводку") {
  Timeline run = runOf(2);
  note(run, 1, 0.0, 1.0);
  note(run, 1, 1.0, 3.0);
  note(run, 2, 0.0, 40.0);

  RunSummary summary;
  summary.setRun(run, kWindow, true, 2);
  REQUIRE(summary.view());
  CHECK(summary.view()->notes == 2);
  CHECK(summary.view()->median.text == "+2.0");

  summary.setRun(run, kWindow, true, 1);
  CHECK_FALSE(summary.view());
}
