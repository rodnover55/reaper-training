#include <doctest/doctest.h>

#include "grid_timeline.hpp"

#include "training/core/hit_window.hpp"
#include "training/core/training/state/timeline.hpp"

#include <map>
#include <optional>
#include <utility>
#include <vector>

using training::core::Hit;
using training::core::HitWindow;
using training::core::Interval;
using training::core::Moment;
using training::core::training::state::Bar;
using training::core::training::state::Beat;
using training::core::training::state::Note;
using training::core::training::state::Position;
using training::core::training::state::Timeline;
using training::test::coreBar;
using training::test::coreBars;
using training::test::msOf;
using training::test::SteadyTimeline;
using training::test::timeOf;

namespace {

/// Карта темпа тестов: 120 BPM, 4/4, удар — 0.5 с, такт — 2 с.
const SteadyTimeline &steady() {
  static const SteadyTimeline timeline(120.0);
  return timeline;
}

/// Такт с долями на секундах `seconds` и концом на секунде `end`: доля `k` —
/// место `k`, начало — первая доля.
Bar barAt(const std::vector<double> &seconds, double end) {
  Bar bar("1", timeOf(seconds.front()), timeOf(end), {}, 120.0);
  for (std::size_t k = 0; k < seconds.size(); ++k)
    bar.addDivision(Beat(static_cast<double>(k)), timeOf(seconds[k]));
  return bar;
}

} // namespace

TEST_CASE("Доля такта: порядок — по месту в такте") {
  CHECK(Beat(0.5) < Beat(1.0));
  CHECK(Beat(1.0 / 3.0) == Beat(1.0 / 3.0));
  CHECK(Beat().beats() == 0.0);
  CHECK(Beat(2.5).beats() == 2.5);
}

TEST_CASE("Такт: темп на доле 0 есть всегда, смена темпа на доле заменяет прошлую") {
  Bar bar("1", timeOf(0.0), timeOf(2.0), {}, 120.0);
  CHECK(bar.tempo() == std::map<Beat, double>{{Beat(), 120.0}});
  bar.changeTempo(Beat(2.0), 90.0);
  bar.changeTempo(Beat(), 100.0);
  CHECK(bar.tempo() == std::map<Beat, double>{{Beat(), 100.0}, {Beat(2.0), 90.0}});
}

TEST_CASE("Такт: доля не раньше начала такта, время долей растёт вместе с местом") {
  Bar bar("1", timeOf(1.0), timeOf(3.0), {}, 120.0);
  CHECK_FALSE(bar.addDivision(Beat(), timeOf(0.5)));
  CHECK(bar.addDivision(Beat(1.0), timeOf(1.5)));
  CHECK_FALSE(bar.addDivision(Beat(1.0), timeOf(1.6)));
  CHECK_FALSE(bar.addDivision(Beat(2.0), timeOf(1.5)));
  CHECK_FALSE(bar.addDivision(Beat(0.5), timeOf(1.7)));
  CHECK(bar.addDivision(Beat(), timeOf(1.0)));
  CHECK(bar.addDivision(Beat(2.0), timeOf(2.0)));
  CHECK(bar.divisions() == std::map<Beat, Moment>{{Beat(), timeOf(1.0)},
                                                  {Beat(1.0), timeOf(1.5)},
                                                  {Beat(2.0), timeOf(2.0)}});
}

TEST_CASE("Такт: нота ложится только в долю такта") {
  Bar bar = barAt({0.0, 1.0}, 2.0);
  const Note note{.deviation = msOf(3.0), .hit = Hit::Good};
  CHECK_FALSE(bar.addNote(Beat(0.5), note));
  CHECK(bar.notes().empty());
  CHECK(bar.addNote(Beat(1.0), note));
  CHECK(bar.addNote(Beat(1.0), note));
  CHECK(bar.notes() == std::map<Beat, std::vector<Note>>{{Beat(1.0), {note, note}}});
}

TEST_CASE("Такт: нота в долю вне такта не ложится") {
  Bar bar = barAt({0.0, 1.0}, 2.0);
  REQUIRE(bar.addDivision(Beat(2.0), timeOf(2.0)));
  CHECK_FALSE(bar.addNote(Beat(2.0), {.deviation = msOf(1.0), .hit = Hit::Good}));
  CHECK(bar.notes().empty());
}

TEST_CASE("Такты: ближайшая доля среди всех тактов, у конца такта — первая доля следующего") {
  const Timeline timeline = coreBars(steady(), 0, 1, 1);
  const auto near = timeline.nearest(timeOf(1.99));
  REQUIRE(near);
  CHECK(*near == Position{.bar = 1, .division = Beat(0.0)});
  CHECK(timeline.time(*near) == timeOf(2.0));

  const auto inside = timeline.nearest(timeOf(0.6));
  REQUIRE(inside);
  CHECK(*inside == Position{.bar = 0, .division = Beat(1.0)});
}

TEST_CASE("Такты: после времени нет ни одной доли — ближайшая доля раньше времени") {
  const Timeline timeline = coreBars(steady(), 0, 0, 1);
  const auto last = timeline.nearest(timeOf(1.6));
  REQUIRE(last);
  CHECK(*last == Position{.bar = 0, .division = Beat(3.0)});
  CHECK_FALSE(Timeline{}.nearest(timeOf(0.0)));
  // Доля ровно на времени — ближайшая.
  const auto exact = timeline.nearest(timeOf(1.5));
  REQUIRE(exact);
  CHECK(exact->division == Beat(3.0));
}

TEST_CASE("Такты: время вне тактов — пусто, даже рядом с первой долей") {
  const Timeline timeline = coreBars(steady(), 1, 1, 1);
  CHECK_FALSE(timeline.nearest(timeOf(1.99)));

  // Доля после конца такта — вне такта: время рядом с ней тоже вне тактов.
  Timeline past;
  Bar bar = barAt({0.0, 1.0}, 2.0);
  REQUIRE(bar.addDivision(Beat(2.0), timeOf(2.5)));
  past.add(bar);
  CHECK_FALSE(past.nearest(timeOf(2.4)));
}

TEST_CASE("Такты: время в пересечении тактов — ближайшая действующая доля всех тактов") {
  Timeline timeline;
  timeline.add(barAt({0.0, 0.5, 1.0, 1.55}, 2.0));
  Bar newer("2", timeOf(1.6), timeOf(3.6), {}, 120.0);
  REQUIRE(newer.addDivision(Beat(), timeOf(1.7)));
  timeline.add(newer);

  // Время лежит в обоих тактах; доля 1.55 с первого такта — до начала
  // нового такта, действует и ближе доли 1.7 с.
  CHECK(timeline.nearest(timeOf(1.6)) == Position{.bar = 0, .division = Beat(3.0)});
  CHECK(timeline.nearest(timeOf(1.66)) == Position{.bar = 1, .division = Beat(0.0)});

  // У конца нового такта ближе доля перекрытого такта после его конца.
  Timeline tail;
  tail.add(barAt({0.0, 2.0, 4.0, 6.0}, 8.0));
  tail.add(barAt({2.5, 5.5}, 6.0));
  CHECK(tail.nearest(timeOf(5.95)) == Position{.bar = 0, .division = Beat(3.0)});
}

TEST_CASE("Такты: из двух равно близких долей разных тактов — ранняя по времени") {
  Timeline timeline;
  timeline.add(barAt({0.0, 1.0}, 2.0));
  timeline.add(barAt({2.0, 3.0}, 4.0));
  const auto tie = timeline.nearest(timeOf(1.5));
  REQUIRE(tie);
  CHECK(*tie == Position{.bar = 0, .division = Beat(1.0)});
}

TEST_CASE("Такты: новый такт добавляется, прошлые такты с долями и нотами остаются") {
  Timeline timeline;
  timeline.add(barAt({0.0, 1.0, 2.0, 3.0}, 4.0));
  REQUIRE(timeline.addNote({.bar = 0, .division = Beat(3.0)},
                           {.deviation = msOf(-4.0), .hit = Hit::Good}));
  timeline.add(barAt({4.0, 5.0}, 6.0));

  // Петля на 2.5 с: такт со start 2.5 с перекрывает хвост первого такта и
  // второй такт целиком.
  timeline.add(barAt({2.5, 3.5}, 6.0));
  REQUIRE(timeline.bars().size() == 3);
  CHECK(timeline.bars()[0].divisions().size() == 4);
  CHECK(timeline.bars()[0].notes().contains(Beat(3.0)));
  CHECK(timeline.bars()[1].divisions().size() == 2);
  CHECK(timeline.bars()[2].start() == timeOf(2.5));
}

TEST_CASE("Такты: нота ложится только в долю пришедшего такта") {
  Timeline timeline;
  timeline.add(barAt({0.0, 1.0}, 2.0));
  const Note note{.deviation = msOf(2.0), .hit = Hit::Good};
  CHECK_FALSE(timeline.addNote({.bar = 1, .division = Beat(0.0)}, note));
  CHECK_FALSE(timeline.addNote({.bar = 0, .division = Beat(0.5)}, note));
  CHECK(timeline.bars()[0].notes().empty());
  CHECK(timeline.addNote({.bar = 0, .division = Beat(1.0)}, note));
  CHECK(timeline.bars()[0].notes() == std::map<Beat, std::vector<Note>>{{Beat(1.0), {note}}});
}

TEST_CASE("Такты: нота в долю вне её такта не ложится") {
  Timeline timeline;
  Bar bar = barAt({0.0, 1.0}, 2.0);
  REQUIRE(bar.addDivision(Beat(4.0), timeOf(2.5)));
  timeline.add(bar);
  const Note note{.deviation = msOf(2.0), .hit = Hit::Good};
  CHECK_FALSE(timeline.addNote({.bar = 0, .division = Beat(4.0)}, note));
  CHECK(timeline.bars()[0].notes().empty());
}

TEST_CASE("Такты: доли, перекрытые тактом, пришедшим позже, не ближайшие") {
  Timeline timeline;
  timeline.add(barAt({0.0, 1.0, 2.0, 3.0}, 4.0));
  timeline.add(barAt({4.0, 5.0}, 6.0));
  timeline.add(barAt({2.5, 3.5}, 6.0));

  // Доля 2.0 с первого такта раньше 2.5 с — действует.
  const auto before = timeline.nearest(timeOf(2.1));
  REQUIRE(before);
  CHECK(*before == Position{.bar = 0, .division = Beat(2.0)});

  // Доля 3.0 с первого такта перекрыта: ближайшая — доля 3.5 с нового такта.
  const auto overlapped = timeline.nearest(timeOf(3.1));
  REQUIRE(overlapped);
  CHECK(*overlapped == Position{.bar = 2, .division = Beat(1.0)});

  // Доли второго такта перекрыты целиком: ближайшая — доля 3.5 с нового
  // такта.
  CHECK(timeline.nearest(timeOf(4.0)) == Position{.bar = 2, .division = Beat(1.0)});
}

TEST_CASE("Такты: такт, пришедший позже, перекрывает доли только от своего начала до конца") {
  Timeline timeline;
  timeline.add(barAt({0.0, 1.0, 2.0, 3.0}, 4.0));
  timeline.add(barAt({1.0, 1.5}, 2.0));

  // Доля 1.0 с первого такта перекрыта, доли 2.0 и 3.0 с — после конца
  // нового такта — действуют.
  CHECK(timeline.nearest(timeOf(1.1)) == Position{.bar = 1, .division = Beat(0.0)});
  CHECK(timeline.nearest(timeOf(2.9)) == Position{.bar = 0, .division = Beat(3.0)});
}

TEST_CASE("Такты: такт ровно в конце прошлого ничего не перекрывает") {
  Timeline timeline = coreBars(steady(), 0, 0, 4);
  timeline.add(coreBar(steady(), 1, 4));
  const auto last = timeline.nearest(timeOf(1.9));
  REQUIRE(last);
  CHECK(*last == Position{.bar = 0, .division = Beat(3.75)});
}

TEST_CASE("Такты: время позиции — время доли, без такта или доли — пусто") {
  const Timeline timeline = coreBars(steady(), 0, 1, 1);
  CHECK(timeline.time({.bar = 1, .division = Beat(2.0)}) == timeOf(3.0));
  CHECK_FALSE(timeline.time({.bar = 1, .division = Beat(0.5)}));
  CHECK_FALSE(timeline.time({.bar = 2, .division = Beat(0.0)}));
}

TEST_CASE("Такты: доля, перекрытая тактом, пришедшим позже, не действует для времени и нот") {
  Timeline timeline;
  timeline.add(barAt({0.0, 1.0, 2.0, 3.0}, 4.0));
  timeline.add(barAt({2.5, 3.5}, 4.0));
  const Note note{.deviation = msOf(2.0), .hit = Hit::Good};

  CHECK(timeline.time({.bar = 0, .division = Beat(2.0)}) == timeOf(2.0));
  CHECK_FALSE(timeline.time({.bar = 0, .division = Beat(3.0)}));
  CHECK_FALSE(timeline.addNote({.bar = 0, .division = Beat(3.0)}, note));
  CHECK(timeline.bars()[0].notes().empty());
  CHECK(timeline.addNote({.bar = 1, .division = Beat(0.0)}, note));
}

TEST_CASE(
    "Окно попадания: Target — не дальше допуска точного попадания от смещения, край входит") {
  const HitWindow window = *HitWindow::create(Interval(), msOf(10.0));
  CHECK(window.hit(msOf(0.4)) == Hit::Target);
  CHECK(window.hit(msOf(-0.4)) == Hit::Target);
  CHECK(window.hit(msOf(0.5)) == Hit::Target);
  CHECK(window.hit(msOf(-0.5)) == Hit::Target);
  CHECK(window.hit(msOf(0.6)) == Hit::Good);

  const HitWindow half = *HitWindow::create(msOf(7.5), msOf(12.5));
  CHECK(half.hit(msOf(7.0)) == Hit::Target);
  CHECK(half.hit(msOf(8.0)) == Hit::Target);
  CHECK(half.hit(msOf(9.0)) == Hit::Good);

  // Разница −0.4 мс — 0.7 мс от смещения 0.3 мс.
  const HitWindow shifted = *HitWindow::create(msOf(0.3), msOf(10.0));
  CHECK(shifted.hit(msOf(0.0)) == Hit::Target);
  CHECK(shifted.hit(msOf(-0.4)) == Hit::Good);

  const HitWindow exact = *HitWindow::create(Interval(), msOf(10.0), msOf(2.0));
  CHECK(exact.hit(msOf(-2.0)) == Hit::Target);
  CHECK(exact.hit(msOf(1.5)) == Hit::Target);
  CHECK(exact.hit(msOf(2.5)) == Hit::Good);
}

TEST_CASE("Окно попадания: Good и Bad — по точной разнице, края входят в окно") {
  const HitWindow window = *HitWindow::create(Interval(), msOf(10.0));
  CHECK(window.hit(msOf(10.0)) == Hit::Good);
  CHECK(window.hit(msOf(-10.0)) == Hit::Good);
  CHECK(window.hit(msOf(10.4)) == Hit::Bad);
  CHECK(window.hit(msOf(-10.4)) == Hit::Bad);

  const HitWindow wide = *HitWindow::create(Interval(), msOf(10.7));
  CHECK(wide.hit(msOf(-10.6)) == Hit::Good);
  CHECK(wide.hit(msOf(10.6)) == Hit::Good);
}
