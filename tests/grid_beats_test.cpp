#include <doctest/doctest.h>

#include "grid_timeline.hpp"

#include "training/grid/beats.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

using training::grid::Beats;
using training::grid::Mode;
using training::test::SteadyTimeline;

namespace {

/// Карта темпа тестов: 120 BPM, 4/4, удар — 0.5 с.
const SteadyTimeline &timeline() {
  static const SteadyTimeline steady(120.0);
  return steady;
}

/// Время узла `slot` удара `beat` в режиме с `count` узлами на удар.
double node(std::int64_t beat, int slot, int count) {
  return timeline().timeAt(static_cast<double>(beat) + static_cast<double>(slot) / count);
}

} // namespace

TEST_CASE("свод: галоп в шестнадцатых — второй узел пропущен, среднее по трём нотам") {
  Beats beats;
  beats.setMode(Mode::Sixteenths);
  beats.addNote(node(3, 0, 4) + 0.002, 1.0, timeline());
  beats.addNote(node(3, 2, 4) - 0.003, 1.0, timeline());
  beats.addNote(node(3, 3, 4) + 0.004, 1.0, timeline());
  beats.advance(2.2, timeline());

  REQUIRE(beats.rows().size() == 1);
  const auto &row = beats.rows().front();
  CHECK(row.complete);
  CHECK(row.slots[1].missing);
  CHECK_FALSE(row.slots[1].deviation);
  REQUIRE(row.mean);
  CHECK(*row.mean == doctest::Approx(0.001));
}

TEST_CASE("свод: две ноты у одного узла — остаётся ближайшая, узел с лишней нотой") {
  Beats beats;
  beats.addNote(node(3, 0, 1) - 0.020, 1.0, timeline());
  beats.addNote(node(3, 0, 1) + 0.004, 1.0, timeline());

  REQUIRE(beats.rows().size() == 1);
  const auto &slot = beats.rows().front().slots[0];
  CHECK(slot.extra);
  REQUIRE(slot.deviation);
  CHECK(*slot.deviation == doctest::Approx(0.004));
}

TEST_CASE("свод: среднее и разброс четырёх нот") {
  Beats beats;
  beats.setMode(Mode::Sixteenths);
  const std::array<double, 4> offsets{-0.004, 0.002, 0.006, 0.004};
  for (int slot = 0; slot < 4; ++slot)
    beats.addNote(node(3, slot, 4) + offsets[static_cast<std::size_t>(slot)], 1.0, timeline());

  const auto &row = beats.rows().front();
  CHECK(row.complete);
  REQUIRE(row.mean);
  REQUIRE(row.spread);
  CHECK(*row.mean == doctest::Approx(0.002));
  CHECK(*row.spread == doctest::Approx(0.0037417).epsilon(1e-4));
}

TEST_CASE("свод: в четвертях среднего и разброса нет") {
  Beats beats;
  beats.addNote(node(3, 0, 1) + 0.005, 1.0, timeline());

  const auto &row = beats.rows().front();
  CHECK(row.complete);
  CHECK_FALSE(row.mean);
  CHECK_FALSE(row.spread);
}

TEST_CASE("свод: пока узлы не решены, среднего нет") {
  Beats beats;
  beats.setMode(Mode::Sixteenths);
  beats.addNote(node(3, 0, 4) + 0.003, 1.0, timeline());

  // Середина между узлами 0 и 1 — 1.5625 с: до неё узел 1 ещё ждёт ноту.
  beats.advance(1.55, timeline());
  CHECK_FALSE(beats.rows().front().complete);
  CHECK_FALSE(beats.rows().front().mean);

  beats.advance(2.0, timeline());
  const auto &row = beats.rows().front();
  CHECK(row.complete);
  CHECK(row.slots[3].missing);
  REQUIRE(row.mean);
  CHECK(*row.mean == doctest::Approx(0.003));
}

TEST_CASE("свод: пауза в игре не добавляет строк") {
  Beats beats;
  for (std::int64_t beat = 0; beat < 4; ++beat)
    beats.addNote(node(beat, 0, 1), 1.0, timeline());

  // Транспорт идёт ещё два такта, нот нет.
  beats.advance(6.0, timeline());
  CHECK(beats.rows().size() == 4);
}

TEST_CASE("свод: девятый удар вытесняет самый старый, новые сверху") {
  Beats beats;
  for (std::int64_t beat = 0; beat < 9; ++beat)
    beats.addNote(node(beat, 0, 1) + 0.001, 1.0, timeline());

  REQUIRE(beats.rows().size() == 8);
  CHECK(beats.rows().front().beat == 8);
  CHECK(beats.rows().back().beat == 1);
}

TEST_CASE("свод: смена режима действует со следующего удара") {
  Beats beats;
  beats.setMode(Mode::Sixteenths);
  beats.addNote(node(3, 0, 4), 1.0, timeline());

  beats.setMode(Mode::Eighths);
  // Начатый удар доводится в шестнадцатых.
  beats.addNote(node(3, 1, 4) + 0.002, 1.0, timeline());
  // Следующий удар — уже в восьмых.
  beats.addNote(node(4, 1, 2) - 0.001, 1.0, timeline());

  REQUIRE(beats.rows().size() == 2);
  const auto &newer = beats.rows()[0];
  const auto &older = beats.rows()[1];
  CHECK(older.beat == 3);
  CHECK(older.mode == Mode::Sixteenths);
  REQUIRE(older.slots[1].deviation);
  CHECK(*older.slots[1].deviation == doctest::Approx(0.002));
  CHECK(newer.beat == 4);
  CHECK(newer.mode == Mode::Eighths);
  REQUIRE(newer.slots.size() == 2);
  REQUIRE(newer.slots[1].deviation);
  CHECK(*newer.slots[1].deviation == doctest::Approx(-0.001));
}

TEST_CASE("свод: при скорости 0.8 отклонение хранится в реальном времени") {
  Beats beats;
  beats.addNote(node(3, 0, 1) + 0.008, 0.8, timeline());

  REQUIRE(beats.rows().front().slots[0].deviation);
  CHECK(*beats.rows().front().slots[0].deviation == doctest::Approx(0.010));
}

TEST_CASE("свод: подпись строки — такт и удар") {
  Beats beats;
  beats.addNote(node(46, 0, 1), 1.0, timeline());

  CHECK(beats.rows().front().label.bar == 12);
  CHECK(beats.rows().front().label.beat == 3);
}

TEST_CASE("свод: нота прошедшего удара — новый проход петли, новая строка сверху") {
  Beats beats;
  beats.setMode(Mode::Eighths);
  beats.addNote(node(3, 0, 2) + 0.002, 1.0, timeline());
  beats.addNote(node(3, 1, 2) - 0.001, 1.0, timeline());
  beats.advance(node(4, 0, 2) + 0.2, timeline()); // время удара 3 прошло

  REQUIRE(beats.rows().size() == 1);
  CHECK(beats.rows().front().closed);

  // Петля вернулась к удару 3.
  beats.addNote(node(3, 0, 2) + 0.005, 1.0, timeline());

  REQUIRE(beats.rows().size() == 2);
  const auto &again = beats.rows()[0];
  const auto &first = beats.rows()[1];
  CHECK(again.beat == 3);
  CHECK_FALSE(again.closed);
  REQUIRE(again.slots[0].deviation);
  CHECK(*again.slots[0].deviation == doctest::Approx(0.005));
  CHECK_FALSE(again.slots[0].extra);
  REQUIRE(first.slots[0].deviation);
  CHECK(*first.slots[0].deviation == doctest::Approx(0.002));
  CHECK_FALSE(first.slots[0].extra);
}

TEST_CASE("свод: в четвертях вторая нота у узла до конца удара — лишняя, не новая строка") {
  Beats beats;
  beats.addNote(node(3, 0, 1) + 0.010, 1.0, timeline());
  beats.advance(node(3, 0, 1) + 0.050, timeline()); // строка решена, удар не кончился
  beats.addNote(node(3, 0, 1) - 0.002, 1.0, timeline());

  REQUIRE(beats.rows().size() == 1);
  CHECK(beats.rows().front().slots[0].extra);
  CHECK(*beats.rows().front().slots[0].deviation == doctest::Approx(-0.002));
}
