#include <doctest/doctest.h>

#include "grid_timeline.hpp"

#include "training/grid/bars.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

using training::grid::BarRow;
using training::grid::Bars;
using training::grid::Mode;
using training::test::SteadyTimeline;

namespace {

/// Карта темпа тестов: 120 BPM, 4/4, удар — 0.5 с, такт — 2 с.
const SteadyTimeline &timeline() {
  static const SteadyTimeline steady(120.0);
  return steady;
}

/// Первый удар такта с номером `number` (с 1).
double barStart(int number) { return (number - 1) * 4.0; }

/// Позиция воспроизведения `beats` без петли, допуск `toleranceMs`.
void play(Bars &bars, double beats, double toleranceMs = 10.0) {
  bars.play(beats, 0.0, toleranceMs, timeline());
}

/// Нота через `ms` мс после узла `beats`, при допуске `toleranceMs`.
void note(Bars &bars, double beats, double ms, double toleranceMs = 10.0) {
  bars.addNote(timeline().timeAt(beats) + ms / 1000.0, 1.0, toleranceMs, timeline());
}

/// Номера тактов видимых строк сверху вниз.
std::vector<std::int64_t> shown(const Bars &bars) {
  std::vector<std::int64_t> numbers;
  for (const BarRow &row : bars.rows())
    if (row.entered && !row.folded)
      numbers.push_back(row.bar.index + 1);
  return numbers;
}

/// Видимая строка номер `index` сверху.
const BarRow &visibleRow(const Bars &bars, std::size_t index) {
  std::size_t seen = 0;
  for (const BarRow &row : bars.rows())
    if (row.entered && !row.folded && seen++ == index)
      return row;
  FAIL("нет видимой строки " << index);
  return bars.rows().front();
}

/// Значение узла `slot` удара `beat` строки, мс; пусто — значения нет.
std::optional<double> valueMs(const BarRow &row, std::size_t beat, std::size_t slot) {
  const auto &slots = row.beats.at(beat).slots;
  if (slot >= slots.size() || !slots[slot].deviation)
    return std::nullopt;
  return *slots[slot].deviation * 1000.0;
}

} // namespace

TEST_CASE("такты: вход в каждый такт заводит строку сверху, видно восемь") {
  Bars bars;
  for (int number = 1; number <= 9; ++number) {
    play(bars, barStart(number) + 0.1);
    note(bars, barStart(number), 2.0);
  }

  CHECK(shown(bars) == std::vector<std::int64_t>{9, 8, 7, 6, 5, 4, 3, 2});
  CHECK(bars.currentBeat() == 0);
}

TEST_CASE("такты: позиция на начале такта с погрешностью счёта — уже этот такт") {
  Bars bars;
  play(bars, barStart(60) - 1e-9);

  CHECK(shown(bars) == std::vector<std::int64_t>{60});
  CHECK(bars.currentBeat() == 0);
}

TEST_CASE("такты: второй вход в такт петли 5–6 — новая строка сверху") {
  Bars bars;
  constexpr double kLoop = 8.0;
  bars.play(barStart(5) + 0.1, kLoop, 10.0, timeline());
  note(bars, barStart(5), 3.0);
  bars.play(barStart(6) + 0.1, kLoop, 10.0, timeline());
  note(bars, barStart(6), 2.0);
  bars.play(barStart(5) + 0.1, kLoop, 10.0, timeline());

  CHECK(shown(bars) == std::vector<std::int64_t>{5, 6, 5});
}

TEST_CASE("такты: сброс забывает строки и позицию") {
  Bars bars;
  play(bars, 1.2);
  note(bars, 1.0, 1.0);
  bars.clear();

  CHECK(bars.rows().empty());
  CHECK_FALSE(bars.currentBeat());

  // Без позиции нота никуда не кладётся.
  note(bars, 1.0, 1.0);
  CHECK(bars.rows().empty());
}

TEST_CASE("такты: две ноты у одного узла — остаётся ближайшая, узел с лишней нотой") {
  Bars bars;
  bars.setMode(Mode::Sixteenths);
  play(bars, 0.6);
  note(bars, 0.5, -20.0);
  note(bars, 0.5, 4.0);

  const auto &slot = visibleRow(bars, 0).beats[0].slots[2];
  REQUIRE(slot.deviation);
  CHECK(*slot.deviation == doctest::Approx(0.004));
  CHECK(slot.extra);
}

TEST_CASE("такты: позднее значение последней шестнадцатой — в строке своего такта") {
  Bars bars;
  bars.setMode(Mode::Sixteenths);
  play(bars, barStart(63) + 3.9);
  play(bars, barStart(64) + 0.05);
  note(bars, barStart(63) + 3.75, 8.0);

  CHECK(shown(bars) == std::vector<std::int64_t>{64, 63});
  CHECK_FALSE(visibleRow(bars, 0).hasValues());
  CHECK(valueMs(visibleRow(bars, 1), 3, 3) == doctest::Approx(8.0));
}

TEST_CASE("такты: в петле в один такт позднее значение прошлого прохода — в его строке") {
  Bars bars;
  bars.setMode(Mode::Sixteenths);
  constexpr double kLoop = 4.0;
  bars.play(barStart(5) + 0.05, kLoop, 10.0, timeline());
  bars.play(barStart(5) + 3.9, kLoop, 10.0, timeline());
  bars.play(barStart(5) + 0.02, kLoop, 10.0, timeline());
  REQUIRE(shown(bars) == std::vector<std::int64_t>{5, 5});

  note(bars, barStart(5) + 3.75, 6.0);

  CHECK_FALSE(visibleRow(bars, 0).hasValues());
  CHECK(valueMs(visibleRow(bars, 1), 3, 3) == doctest::Approx(6.0));
}

TEST_CASE("такты: в петле в один такт ранняя нота следующего прохода — в его строке") {
  Bars bars;
  bars.setMode(Mode::Sixteenths);
  constexpr double kLoop = 4.0;
  bars.play(barStart(5) + 0.05, kLoop, 10.0, timeline());
  bars.play(barStart(5) + 3.95, kLoop, 10.0, timeline());

  // Время ноты уже завёрнуто в петлю: за 20 мс до её начала.
  note(bars, barStart(5), -20.0);
  CHECK(shown(bars) == std::vector<std::int64_t>{5});
  CHECK_FALSE(visibleRow(bars, 0).hasValues());

  bars.play(barStart(5) + 0.01, kLoop, 10.0, timeline());
  CHECK(shown(bars) == std::vector<std::int64_t>{5, 5});
  CHECK(valueMs(visibleRow(bars, 0), 0, 0) == doctest::Approx(-20.0));
}

TEST_CASE(
    "такты: ранняя нота к первому удару такта видна, когда воспроизведение входит в такт") {
  Bars bars;
  bars.setMode(Mode::Sixteenths);
  play(bars, barStart(17) + 3.95);
  note(bars, barStart(18), -20.0);

  CHECK(shown(bars) == std::vector<std::int64_t>{17});
  CHECK_FALSE(visibleRow(bars, 0).hasValues());

  play(bars, barStart(18) + 0.05);
  CHECK(shown(bars) == std::vector<std::int64_t>{18, 17});
  CHECK(valueMs(visibleRow(bars, 0), 0, 0) == doctest::Approx(-20.0));
}

TEST_CASE("такты: смена режима посреди удара действует со следующего удара") {
  Bars bars;
  bars.setMode(Mode::Sixteenths);
  play(bars, 1.1);
  bars.setMode(Mode::Eighths);
  for (const double at : {1.0, 1.25, 1.5, 1.75})
    note(bars, at, 1.0);
  play(bars, 2.6);
  for (const double at : {2.0, 2.5})
    note(bars, at, 1.0);

  const BarRow &row = visibleRow(bars, 0);
  CHECK(row.beats[1].mode == Mode::Sixteenths);
  CHECK(row.beats[1].slots.size() == 4);
  CHECK(valueMs(row, 1, 3) == doctest::Approx(1.0));
  CHECK(row.beats[2].mode == Mode::Eighths);
  CHECK(row.beats[2].slots.size() == 2);
  CHECK(valueMs(row, 2, 1) == doctest::Approx(1.0));
}

TEST_CASE("такты: триоли в режиме «восьмые» — две ноты в одном узле") {
  Bars bars;
  bars.setMode(Mode::Eighths);
  play(bars, 1.95);
  note(bars, 1.0 + 1.0 / 3.0, 3.0);
  note(bars, 1.0 + 2.0 / 3.0, -2.0);

  // Обе триоли ближе всего к восьмой 1.5: −83.3 + 3 и +83.3 − 2 мс.
  const auto &slot = visibleRow(bars, 0).beats[1].slots[1];
  REQUIRE(slot.deviation);
  CHECK(*slot.deviation * 1000.0 == doctest::Approx(-80.333).epsilon(0.001));
  CHECK(slot.extra);
}

TEST_CASE("такты: среднее и разброс по такту, в том числе в «четвертях»") {
  Bars bars;
  bars.setMode(Mode::Quarters);
  play(bars, 3.9);
  note(bars, 0.0, -4.0);
  note(bars, 1.0, 2.0);
  note(bars, 2.0, 6.0);
  note(bars, 3.0, 4.0);

  const BarRow &row = visibleRow(bars, 0);
  REQUIRE(row.mean);
  REQUIRE(row.spread);
  CHECK(*row.mean * 1000.0 == doctest::Approx(2.0));
  CHECK(*row.spread * 1000.0 == doctest::Approx(3.742).epsilon(0.001));
}

TEST_CASE("такты: позднее значение пересчитывает среднее своего такта") {
  Bars bars;
  bars.setMode(Mode::Quarters);
  play(bars, 3.9);
  note(bars, 0.0, -4.0);
  note(bars, 1.0, 2.0);
  note(bars, 2.0, 6.0);
  play(bars, 4.05);
  note(bars, 3.0, 4.0);

  const BarRow &row = visibleRow(bars, 1);
  REQUIRE(row.mean);
  CHECK(*row.mean * 1000.0 == doctest::Approx(2.0));
}

TEST_CASE("такты: допуск значения и допуск, когда воспроизведение ушло из такта") {
  Bars bars;
  play(bars, 0.1, 10.0);
  note(bars, 0.0, 13.0, 10.0);
  play(bars, 4.1, 20.0);

  const BarRow &row = visibleRow(bars, 1);
  CHECK(row.beats[0].slots[0].toleranceMs == 10.0);
  CHECK(row.leftToleranceMs == 20.0);
  CHECK_FALSE(visibleRow(bars, 0).leftToleranceMs);
}

TEST_CASE("такты: пауза — строка первого пустого такта, верхняя меняет номер") {
  Bars bars;
  play(bars, barStart(55) + 0.1);
  note(bars, barStart(55), 3.0);

  play(bars, barStart(56) + 0.1);
  CHECK(shown(bars) == std::vector<std::int64_t>{56, 55});
  play(bars, barStart(57) + 0.1);
  CHECK(shown(bars) == std::vector<std::int64_t>{57, 56, 55});
  play(bars, barStart(58) + 0.1);
  CHECK(shown(bars) == std::vector<std::int64_t>{58, 56, 55});
  play(bars, barStart(59) + 0.1);
  CHECK(shown(bars) == std::vector<std::int64_t>{59, 56, 55});

  play(bars, barStart(60) + 0.1);
  note(bars, barStart(60), 5.0);
  CHECK(shown(bars) == std::vector<std::int64_t>{60, 56, 55});
}

TEST_CASE("такты: один пустой такт между сыгранными остаётся") {
  Bars bars;
  play(bars, barStart(19) + 0.1);
  note(bars, barStart(19), 3.0);
  play(bars, barStart(20) + 0.1);
  play(bars, barStart(21) + 0.1);
  note(bars, barStart(21), 1.0);

  CHECK(shown(bars) == std::vector<std::int64_t>{21, 20, 19});
}

TEST_CASE("такты: позднее значение в свёрнутый такт паузы возвращает его строку") {
  Bars bars;
  bars.setMode(Mode::Sixteenths);
  play(bars, barStart(55) + 0.1);
  note(bars, barStart(55), 3.0);
  play(bars, barStart(56) + 0.1);
  play(bars, barStart(57) + 0.1);
  play(bars, barStart(57) + 3.9);
  play(bars, barStart(58) + 0.05);
  REQUIRE(shown(bars) == std::vector<std::int64_t>{58, 56, 55});

  note(bars, barStart(57) + 3.75, 5.0);

  CHECK(shown(bars) == std::vector<std::int64_t>{58, 57, 56, 55});
  CHECK(valueMs(visibleRow(bars, 1), 3, 3) == doctest::Approx(5.0));
}
