#include <doctest/doctest.h>

#include "training/core/clock.hpp"
#include "training/core/hit_window.hpp"
#include "training/render/main/deviation_stats.hpp"

#include <vector>

using training::core::HitWindow;
using training::core::Interval;
using training::render::DeviationStats;

namespace {

/// Отклонения в миллисекундах `values`.
std::vector<Interval> msList(const std::vector<double> &values) {
  std::vector<Interval> list;
  list.reserve(values.size());
  for (const double value : values)
    list.push_back(Interval::kMillisecond * value);
  return list;
}

/// Сводка отклонений в миллисекундах `values`.
DeviationStats statsOf(const std::vector<double> &values) {
  return DeviationStats(msList(values));
}

/// Миллисекунды интервала `interval`.
double msIn(Interval interval) { return interval / Interval::kMillisecond; }

/// Окно попадания `offsetMs` ± `toleranceMs`.
HitWindow windowOf(double offsetMs, double toleranceMs) {
  return *HitWindow::create(Interval::kMillisecond * offsetMs,
                            Interval::kMillisecond * toleranceMs);
}

} // namespace

TEST_CASE("сводка отклонений: медиана и IQR с интерполяцией между соседними") {
  const DeviationStats even = statsOf({6.0, -4.0, 4.0, 2.0});
  CHECK(even.count() == 4);
  CHECK(msIn(even.median()) == doctest::Approx(3.0));
  CHECK(msIn(even.quantile(0.25)) == doctest::Approx(0.5));
  CHECK(msIn(even.quantile(0.75)) == doctest::Approx(4.5));
  CHECK(msIn(even.iqr()) == doctest::Approx(4.0));

  const DeviationStats odd = statsOf({5.0, 1.0, 3.0, 2.0, 4.0});
  CHECK(msIn(odd.median()) == doctest::Approx(3.0));
  CHECK(msIn(odd.iqr()) == doctest::Approx(2.0));
}

TEST_CASE("сводка отклонений: пустая — нули") {
  const DeviationStats empty(std::vector<Interval>{});
  CHECK(empty.count() == 0);
  CHECK(empty.median() == Interval());
  CHECK(empty.iqr() == Interval());
  CHECK(empty.score(windowOf(0.0, 10.0)) == doctest::Approx(0.0));
}

TEST_CASE("сводка отклонений: оценка — баллы по допускам, плохие весят больше") {
  CHECK(statsOf({0.0, 10.0, -10.0}).score(windowOf(0.0, 10.0)) == doctest::Approx(100.0));
  // Баллы 1, 0.75, 0.25, 0 с весами 1, 1.5, 2.5, 3: 2.75 / 8.
  CHECK(statsOf({5.0, -15.0, 25.0, 40.0}).score(windowOf(0.0, 10.0)) ==
        doctest::Approx(34.375));
}

TEST_CASE("сводка отклонений: оценка считается от смещения окна") {
  // Окно +12 ± 8: +20 — на допуске, 0 — в полутора допусках.
  CHECK(statsOf({20.0, 0.0}).score(windowOf(12.0, 8.0)) == doctest::Approx(85.0));
}
