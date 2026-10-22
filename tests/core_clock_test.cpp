#include <doctest/doctest.h>

#include "training/core/clock.hpp"

#include <cstdint>
#include <limits>

using training::core::Interval;
using training::core::Moment;

TEST_CASE("Интервал: единицы и деление на единицу") {
  CHECK(1'000 * Interval::kNanosecond == Interval::kMicrosecond);
  CHECK(1'000 * Interval::kMicrosecond == Interval::kMillisecond);
  CHECK(1'000 * Interval::kMillisecond == Interval::kSecond);
  CHECK(5 * Interval::kSecond + 500 * Interval::kMillisecond == 5.5 * Interval::kSecond);

  CHECK(Interval() / Interval::kNanosecond == 0.0);
  CHECK(1'234 * Interval::kNanosecond / Interval::kNanosecond == 1'234.0);
  CHECK(1.5 * Interval::kSecond / Interval::kNanosecond == 1'500'000'000.0);
  CHECK(-7.5 * Interval::kMillisecond / Interval::kNanosecond == -7'500'000.0);
  CHECK(2 * Interval::kSecond / Interval::kSecond == 2.0);
  CHECK(12.5 * Interval::kMillisecond / Interval::kMillisecond == 12.5);

  // Умножение на дробное число округляет до наносекунды; середина — от нуля.
  CHECK(0.5 * Interval::kNanosecond == Interval::kNanosecond);
  CHECK(-0.5 * Interval::kNanosecond == -Interval::kNanosecond);
  CHECK(1.0 / 3.0 * Interval::kSecond == 333'333'333 * Interval::kNanosecond);
}

TEST_CASE("Интервал: целая часть отношения") {
  const Interval quarter = 250 * Interval::kMillisecond;

  CHECK(static_cast<std::int64_t>(1100 * Interval::kMillisecond / quarter) == 4);
  CHECK(static_cast<std::int64_t>(-1100 * Interval::kMillisecond / quarter) == -4);

  // Целые наносекунды точны и там, где их не держит double.
  const std::int64_t most = std::numeric_limits<std::int64_t>::max();
  CHECK(static_cast<std::int64_t>(most * Interval::kNanosecond / Interval::kNanosecond) ==
        most);
}

TEST_CASE("Интервал: действия") {
  const Interval second = Interval::kSecond;
  const Interval quarter = 250 * Interval::kMillisecond;

  CHECK(second + quarter == 1250 * Interval::kMillisecond);
  CHECK(second - quarter == 750 * Interval::kMillisecond);
  CHECK(-quarter == -250 * Interval::kMillisecond);
  CHECK(quarter * 2 == 500 * Interval::kMillisecond);
  CHECK(2 * quarter == 500 * Interval::kMillisecond);
  CHECK(quarter * 2.0 == 500 * Interval::kMillisecond);
  CHECK(2.0 * quarter == 500 * Interval::kMillisecond);
  CHECK(second / 4.0 == quarter);
  CHECK(second / quarter == 4.0);
  CHECK((-quarter).abs() == quarter);
  CHECK(quarter.abs() == quarter);
  CHECK(quarter < second);

  // Умножение и деление на дробное число округляют до наносекунды.
  CHECK(10 * Interval::kNanosecond / 3.0 == 3 * Interval::kNanosecond);
  CHECK(10 * Interval::kNanosecond * 0.25 == 3 * Interval::kNanosecond);

  Interval sum = second;
  sum += quarter;
  sum -= second;
  CHECK(sum == quarter);
}

TEST_CASE("Время: сдвиг на интервал и разность") {
  const Moment origin;
  const Moment later = origin + 12 * Interval::kSecond;

  CHECK(later - origin == 12 * Interval::kSecond);
  CHECK(origin - later == -12 * Interval::kSecond);
  CHECK(later - 2 * Interval::kSecond == origin + 10 * Interval::kSecond);
  CHECK(Interval::kSecond + origin == origin + Interval::kSecond);
  CHECK(origin < later);

  Moment moved = origin;
  moved += 3 * Interval::kSecond;
  moved -= Interval::kSecond;
  CHECK(moved - origin == 2 * Interval::kSecond);
}
