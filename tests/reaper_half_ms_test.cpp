#include <doctest/doctest.h>

#include "training/reaper/half_ms.hpp"

using training::core::Interval;
using training::reaper::halfMsOf;

namespace {

/// Интервал в `value` мс.
Interval ms(double value) { return value * Interval::kMillisecond; }

} // namespace

TEST_CASE("число из текста с шагом 0.5 мс") {
  CHECK(halfMsOf("12") == ms(12.0));
  CHECK(halfMsOf("12.5") == ms(12.5));
  CHECK(halfMsOf("-7.5") == ms(-7.5));
  CHECK(halfMsOf("−7.5") == ms(-7.5));
  CHECK(halfMsOf("+3,5") == ms(3.5));
  CHECK(halfMsOf("-0.5") == ms(-0.5));
  CHECK(halfMsOf(" 15 ") == ms(15.0));
  CHECK(halfMsOf("12.3") == ms(12.5));
  CHECK(halfMsOf("12.2") == ms(12.0));

  CHECK_FALSE(halfMsOf(""));
  CHECK_FALSE(halfMsOf("-"));
  CHECK_FALSE(halfMsOf("12."));
  CHECK_FALSE(halfMsOf(".5"));
  CHECK_FALSE(halfMsOf("1e2"));
  CHECK_FALSE(halfMsOf("12 5"));
  CHECK_FALSE(halfMsOf("99999999999"));
}

TEST_CASE("минус ноль — просто ноль") { CHECK(halfMsOf("-0") == ms(0.0)); }
