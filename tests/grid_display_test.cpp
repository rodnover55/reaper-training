#include <doctest/doctest.h>

#include "grid_timeline.hpp"

#include "training/grid/beats.hpp"
#include "training/grid/display.hpp"

#include <array>
#include <cstddef>
#include <deque>
#include <optional>

using training::grid::BeatRow;
using training::grid::Beats;
using training::grid::Mode;
using training::grid::present;
using training::grid::Slot;
using training::grid::Tone;
using training::test::SteadyTimeline;

namespace {

/// Строка удара 12.3 из узлов с заданными отклонениями, мс; пусто — узел ещё
/// без ноты.
BeatRow rowWith(Mode mode, std::initializer_list<std::optional<double>> deviationsMs) {
  BeatRow row;
  row.beat = 46;
  row.label = {.bar = 12, .beat = 3};
  row.mode = mode;
  for (const auto &deviation : deviationsMs) {
    Slot slot;
    if (deviation)
      slot.deviation = *deviation / 1000.0;
    row.slots.push_back(slot);
  }
  return row;
}

} // namespace

TEST_CASE("показ: отклонение — целые миллисекунды со знаком и цвет по допуску") {
  const auto views = present({rowWith(Mode::Sixteenths, {-15.3, 5.0, 10.0, 0.0})}, 10.0);

  REQUIRE(views.size() == 1);
  CHECK(views[0].label == "12.3");
  REQUIRE(views[0].cells.size() == 4);
  CHECK(views[0].cells[0].text == "−15");
  CHECK(views[0].cells[0].tone == Tone::Bad);
  CHECK(views[0].cells[1].text == "+5");
  CHECK(views[0].cells[1].tone == Tone::Good);
  CHECK(views[0].cells[2].text == "+10");
  CHECK(views[0].cells[2].tone == Tone::Good);
  CHECK(views[0].cells[3].text == "0");
  CHECK(views[0].cells[3].tone == Tone::Good);
}

TEST_CASE("показ: цвет — по показанному числу") {
  // 10.4 мс показывается как «+10» и при допуске 10 — зелёное; −0.4 — «0».
  const auto views = present({rowWith(Mode::Eighths, {10.4, -0.4})}, 10.0);
  CHECK(views[0].cells[0].text == "+10");
  CHECK(views[0].cells[0].tone == Tone::Good);
  CHECK(views[0].cells[1].text == "0");
}

TEST_CASE("показ: пропуск — точка, лишняя нота — звёздочка, нота не пришла — пусто") {
  BeatRow row = rowWith(Mode::Sixteenths, {std::nullopt, 3.0, std::nullopt, std::nullopt});
  row.slots[0].missing = true;
  row.slots[1].extra = true;

  const auto views = present({row}, 10.0);
  CHECK(views[0].cells[0].text == ".");
  CHECK(views[0].cells[0].tone == Tone::Neutral);
  CHECK(views[0].cells[1].text == "+3*");
  CHECK(views[0].cells[2].text.empty());
  CHECK(views[0].cells[2].tone == Tone::Neutral);
}

TEST_CASE("показ: среднее и разброс с одним знаком") {
  Beats beats;
  beats.setMode(Mode::Sixteenths);
  const SteadyTimeline timeline(120.0);
  const std::array<double, 4> offsets{-0.004, 0.002, 0.006, 0.004};
  for (std::size_t slot = 0; slot < offsets.size(); ++slot)
    beats.addNote(timeline.timeAt(46.0 + static_cast<double>(slot) / 4.0) + offsets[slot], 1.0,
                  timeline);

  const auto views = present(beats.rows(), 10.0);
  REQUIRE(views[0].mean);
  REQUIRE(views[0].spread);
  CHECK(views[0].mean->text == "+2.0");
  CHECK(views[0].mean->tone == Tone::Good);
  CHECK(views[0].spread->text == "3.7");
  CHECK(views[0].spread->tone == Tone::Neutral);
}

TEST_CASE("показ: отрицательное и нулевое среднее") {
  BeatRow late = rowWith(Mode::Eighths, {-1.0, -2.0});
  late.complete = true;
  late.mean = -0.0015;
  late.spread = 0.0005;
  BeatRow even = rowWith(Mode::Eighths, {-1.0, 1.0});
  even.complete = true;
  even.mean = 0.0;
  even.spread = 0.001;
  BeatRow far = rowWith(Mode::Eighths, {12.0, 14.0});
  far.complete = true;
  far.mean = 0.013;
  far.spread = 0.001;

  const auto views = present({late, even, far}, 10.0);
  CHECK(views[0].mean->text == "−1.5");
  CHECK(views[1].mean->text == "0.0");
  CHECK(views[2].mean->text == "+13.0");
  CHECK(views[2].mean->tone == Tone::Bad);
}

TEST_CASE("показ: в четвертях среднего и разброса нет") {
  Beats beats;
  const SteadyTimeline timeline(120.0);
  beats.addNote(timeline.timeAt(46.0) + 0.002, 1.0, timeline);

  const auto views = present(beats.rows(), 10.0);
  REQUIRE(views[0].cells.size() == 1);
  CHECK(views[0].cells[0].text == "+2");
  CHECK_FALSE(views[0].mean);
  CHECK_FALSE(views[0].spread);
}

TEST_CASE("показ: при скорости 0.8 разница 8 мс на шкале — «+10»") {
  Beats beats;
  const SteadyTimeline timeline(120.0);
  beats.addNote(timeline.timeAt(46.0) + 0.008, 0.8, timeline);

  const auto views = present(beats.rows(), 10.0);
  CHECK(views[0].cells[0].text == "+10");
}
