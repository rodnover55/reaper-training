#include <doctest/doctest.h>

#include "grid_timeline.hpp"

#include "training/grid/bars.hpp"
#include "training/grid/display.hpp"

#include <cstddef>
#include <string>
#include <vector>

using training::grid::Bars;
using training::grid::BarView;
using training::grid::Mode;
using training::grid::present;
using training::grid::Timeline;
using training::grid::Tone;
using training::test::MapTimeline;
using training::test::SteadyTimeline;

namespace {

/// Карта темпа тестов: 120 BPM, 4/4, удар — 0.5 с.
const SteadyTimeline &steady() {
  static const SteadyTimeline timeline(120.0);
  return timeline;
}

/// Нота через `ms` мс после узла `beats`.
void note(Bars &bars, double beats, double ms, double toleranceMs = 10.0,
          const Timeline &timeline = steady()) {
  bars.addNote(timeline.timeAt(beats) + ms / 1000.0, 1.0, toleranceMs, timeline);
}

/// Тексты значений строки по порядку.
std::vector<std::string> texts(const BarView &view) {
  std::vector<std::string> result;
  result.reserve(view.values.size());
  for (const auto &value : view.values)
    result.push_back(value.cell.text);
  return result;
}

} // namespace

TEST_CASE("показ тактов: шестнадцатые в 4/4 — четыре значения на ударах и двенадцать между") {
  Bars bars;
  bars.setMode(Mode::Sixteenths);
  bars.play(3.9, 0.0, 10.0, steady());
  for (int node = 0; node < 16; ++node)
    note(bars, node / 4.0, 1.0);

  const auto views = present(bars);
  REQUIRE(views.size() == 1);
  const BarView &view = views.front();
  CHECK(view.beats == 4);
  CHECK(view.values.size() == 16);
  std::size_t onBeat = 0;
  for (const auto &value : view.values)
    onBeat += value.onBeat ? 1 : 0;
  CHECK(onBeat == 4);
  CHECK(view.values[5].beat == doctest::Approx(1.25));
  CHECK(view.dots.empty());
  CHECK(view.division == 4);
}

TEST_CASE("показ тактов: восьмые в режиме «шестнадцатые» — узлы между ними пустые") {
  Bars bars;
  bars.setMode(Mode::Sixteenths);
  bars.play(3.9, 0.0, 10.0, steady());
  for (int node = 0; node < 8; ++node)
    note(bars, node / 2.0, 2.0);

  const auto views = present(bars);
  REQUIRE(views.size() == 1);
  REQUIRE(views.front().values.size() == 8);
  for (std::size_t i = 0; i < 8; ++i)
    CHECK(views.front().values[i].beat == doctest::Approx(static_cast<double>(i) / 2.0));
}

TEST_CASE("показ тактов: пропуск на ударе — точка, лишняя нота — «+3*»") {
  Bars bars;
  bars.setMode(Mode::Eighths);
  bars.play(1.9, 0.0, 10.0, steady());
  note(bars, 0.5, 3.0);
  note(bars, 0.5, -20.0);
  note(bars, 1.0, 1.0);

  const BarView view = present(bars).front();
  CHECK(view.dots == std::vector<int>{0, 2, 3});
  CHECK(texts(view) == std::vector<std::string>{"+3*", "+1"});
  CHECK_FALSE(view.values[0].onBeat);
}

TEST_CASE("показ тактов: смена допуска не перекрашивает показанные значения") {
  Bars bars;
  bars.setMode(Mode::Eighths);
  bars.play(1.6, 0.0, 10.0, steady());
  note(bars, 0.0, 13.0, 10.0);
  note(bars, 0.5, 13.0, 10.0);
  note(bars, 1.0, 13.0, 20.0);
  note(bars, 1.5, 13.0, 20.0);

  const BarView view = present(bars).front();
  REQUIRE(view.values.size() == 4);
  CHECK(view.values[0].cell.tone == Tone::Bad);
  CHECK(view.values[1].cell.tone == Tone::Bad);
  CHECK(view.values[2].cell.tone == Tone::Good);
  CHECK(view.values[3].cell.tone == Tone::Good);
}

TEST_CASE("показ тактов: у верхней строки текущий удар и нет среднего") {
  Bars bars;
  bars.setMode(Mode::Quarters);
  bars.play(3.9, 0.0, 10.0, steady());
  note(bars, 0.0, -4.0);
  note(bars, 1.0, 2.0);
  note(bars, 2.0, 6.0);
  note(bars, 3.0, 4.0);
  bars.play(5.2, 0.0, 10.0, steady());
  note(bars, 4.0, 1.0);
  note(bars, 5.0, 2.0);

  const auto views = present(bars);
  REQUIRE(views.size() == 2);
  CHECK(views[0].currentBeat == 1);
  CHECK_FALSE(views[0].mean);
  CHECK_FALSE(views[1].currentBeat);
  REQUIRE(views[1].mean);
  REQUIRE(views[1].spread);
  CHECK(views[1].mean->text == "+2.0");
  CHECK(views[1].mean->tone == Tone::Good);
  CHECK(views[1].spread->text == "3.7");
  CHECK(views[1].spread->tone == Tone::Neutral);

  // После остановки текущего удара нет.
  bars.stop();
  CHECK_FALSE(present(bars).front().currentBeat);
}

TEST_CASE("показ тактов: номер такта «10000»") {
  Bars bars;
  bars.play((10000 - 1) * 4.0 + 0.1, 0.0, 10.0, steady());

  const auto views = present(bars);
  REQUIRE(views.size() == 1);
  CHECK(views.front().label == "10000");
  CHECK(views.front().dots == std::vector<int>{0, 1, 2, 3});
}

TEST_CASE("показ тактов: самый мелкий режим такта и режим для ещё не начатых ударов") {
  Bars bars;
  bars.setMode(Mode::Eighths);
  bars.play(0.1, 0.0, 10.0, steady());
  CHECK(present(bars).front().division == 2);

  bars.setMode(Mode::SixteenthTriplets);
  CHECK(present(bars).front().division == 6);
}

TEST_CASE("показ тактов: маркеры смен темпа и размера") {
  const MapTimeline timeline({
      {.meter = {.beats = 4, .unit = 4}, .bpm = 120.0, .inside = {}},
      {.meter = {.beats = 4, .unit = 4}, .bpm = 150.0, .inside = {{2.0, 100.0}}},
      {.meter = {.beats = 7, .unit = 8}, .bpm = 100.0, .inside = {}},
      {.meter = {.beats = 3, .unit = 4}, .bpm = 120.0, .inside = {}},
      {.meter = {.beats = 4, .unit = 4}, .bpm = 150.0, .inside = {}},
      {.meter = {.beats = 4, .unit = 4}, .bpm = 120.5, .inside = {}},
  });

  Bars bars;
  for (const double beat : {0.1, 4.1, 8.1, 15.1, 18.1, 22.1}) {
    bars.play(beat, 0.0, 10.0, timeline);
    note(bars, beat - 0.1, 1.0, 10.0, timeline);
  }

  const auto views = present(bars);
  REQUIRE(views.size() == 6);

  // Строки сверху вниз: последний такт первым.
  REQUIRE(views[4].marks.size() == 2);
  CHECK(views[4].marks[0].beat == 0.0);
  CHECK(views[4].marks[0].text == "120 → 150");
  CHECK(views[4].marks[1].beat == 2.0);
  CHECK(views[4].marks[1].text == "150 → 100");

  REQUIRE(views[3].marks.size() == 1);
  CHECK(views[3].marks[0].text == "4/4 → 7/8");
  CHECK(views[3].beats == 7);

  REQUIRE(views[1].marks.size() == 1);
  CHECK(views[1].marks[0].text == "3/4 → 4/4 · 120 → 150");

  REQUIRE(views[0].marks.size() == 1);
  CHECK(views[0].marks[0].text == "150 → 120.5");

  CHECK(views[5].marks.empty());
}

TEST_CASE("показ тактов: отклонение — целые миллисекунды со знаком и цвет по допуску") {
  Bars bars;
  bars.setMode(Mode::Quarters);
  bars.play(3.9, 0.0, 10.0, steady());
  note(bars, 0.0, -15.3);
  note(bars, 1.0, 5.0);
  note(bars, 2.0, 10.0);
  note(bars, 3.0, 0.0);

  const BarView view = present(bars).front();
  CHECK(texts(view) == std::vector<std::string>{"−15", "+5", "+10", "0"});
  CHECK(view.values[0].cell.tone == Tone::Bad);
  CHECK(view.values[1].cell.tone == Tone::Good);
  CHECK(view.values[2].cell.tone == Tone::Good);
  CHECK(view.values[3].cell.tone == Tone::Good);
}

TEST_CASE("показ тактов: цвет — по показанному числу") {
  // 10.4 мс показывается как «+10» и при допуске 10 — зелёное; −0.4 — «0».
  Bars bars;
  bars.setMode(Mode::Quarters);
  bars.play(1.9, 0.0, 10.0, steady());
  note(bars, 0.0, 10.4);
  note(bars, 1.0, -0.4);

  const BarView view = present(bars).front();
  CHECK(texts(view) == std::vector<std::string>{"+10", "0"});
  CHECK(view.values[0].cell.tone == Tone::Good);
}

TEST_CASE("показ тактов: отрицательное, нулевое и далёкое среднее") {
  Bars bars;
  bars.setMode(Mode::Quarters);
  const std::vector<std::vector<double>> bars3{{-1.0, -2.0}, {-1.0, 1.0}, {12.0, 14.0}};
  for (std::size_t i = 0; i < bars3.size(); ++i) {
    const double start = static_cast<double>(i) * 4.0;
    bars.play(start + 1.9, 0.0, 10.0, steady());
    note(bars, start, bars3[i][0]);
    note(bars, start + 1.0, bars3[i][1]);
  }
  bars.play(12.1, 0.0, 10.0, steady());

  const auto views = present(bars);
  REQUIRE(views.size() == 4);
  CHECK(views[3].mean->text == "−1.5");
  CHECK(views[2].mean->text == "0.0");
  CHECK(views[1].mean->text == "+13.0");
  CHECK(views[1].mean->tone == Tone::Bad);
}

TEST_CASE("показ тактов: при скорости 0.8 разница 8 мс на шкале — «+10»") {
  Bars bars;
  bars.play(0.2, 0.0, 10.0, steady());
  bars.addNote(steady().timeAt(0.0) + 0.008, 0.8, 10.0, steady());

  CHECK(texts(present(bars).front()) == std::vector<std::string>{"+10"});
}
