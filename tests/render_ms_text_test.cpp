#include <doctest/doctest.h>

#include "training/render/main/ms_text.hpp"

using training::core::Interval;
using training::render::halfMsText;
using training::render::MsStyle;

namespace {

/// Интервал в `value` мс.
Interval ms(double value) { return value * Interval::kMillisecond; }

} // namespace

TEST_CASE("запись числа с шагом 0.5 мс") {
  CHECK(halfMsText(ms(7.5), MsStyle::Shown) == "+7.5");
  CHECK(halfMsText(ms(-5.0), MsStyle::Shown) == "−5");
  CHECK(halfMsText(ms(0.0), MsStyle::Shown) == "0");
  CHECK(halfMsText(ms(12.0), MsStyle::Signed) == "+12");
  CHECK(halfMsText(ms(-7.5), MsStyle::Signed) == "-7.5");
  CHECK(halfMsText(ms(12.5), MsStyle::Plain) == "12.5");
  CHECK(halfMsText(ms(-7.5), MsStyle::Plain) == "-7.5");
  CHECK(halfMsText(ms(10.0), MsStyle::Plain) == "10");
}

TEST_CASE("минус ноль записывается просто нулём") {
  CHECK(halfMsText(ms(-0.0), MsStyle::Shown) == "0");
  CHECK(halfMsText(ms(-0.2), MsStyle::Signed) == "0");
}
