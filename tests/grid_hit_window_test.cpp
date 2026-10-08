#include <doctest/doctest.h>

#include "training/grid/hit_window.hpp"

#include <optional>

using training::grid::fits;
using training::grid::halfMsOf;
using training::grid::halfMsText;
using training::grid::HitWindow;
using training::grid::MsStyle;
using training::grid::shifted;
using training::grid::withEarly;
using training::grid::withLate;

namespace {

/// Граница шкалы по умолчанию, мс.
constexpr double kScale = 50.0;

/// Окно смещением `offsetMs` и допуском `toleranceMs`.
HitWindow window(double offsetMs, double toleranceMs) {
  return {.offsetMs = offsetMs, .toleranceMs = toleranceMs};
}

} // namespace

TEST_CASE("края окна — смещение минус и плюс допуск") {
  const HitWindow hit = window(12.0, 8.0);
  CHECK(hit.early() == 4.0);
  CHECK(hit.late() == 20.0);
}

TEST_CASE("окно допустимо: допуск не меньше 1 мс, края в шкале") {
  SUBCASE("края на границах шкалы") {
    CHECK(fits(window(0.0, 10.0), 10.0));
    CHECK(fits(window(40.0, 10.0), kScale));
    CHECK(fits(window(-40.0, 10.0), kScale));
  }
  SUBCASE("допуск 1 мс, но не меньше") {
    CHECK(fits(window(0.0, 1.0), kScale));
    CHECK_FALSE(fits(window(0.0, 0.5), kScale));
  }
  SUBCASE("край за шкалой") {
    CHECK_FALSE(fits(window(40.5, 10.0), kScale));
    CHECK_FALSE(fits(window(-40.5, 10.0), kScale));
    CHECK_FALSE(fits(window(0.0, 10.5), 10.0));
  }
}

TEST_CASE("ползунок двигает свой край, другой стоит") {
  SUBCASE("правый край 0±10 на +20") {
    CHECK(withLate(window(0.0, 10.0), 20.0, kScale) == window(5.0, 15.0));
  }
  SUBCASE("левый край −20…+20 на −5") {
    CHECK(withEarly(window(0.0, 20.0), -5.0, kScale) == window(7.5, 12.5));
  }
  SUBCASE("левый край не ближе 2 мс к правому") {
    CHECK(withEarly(window(0.0, 10.0), 15.0, kScale) == window(9.0, 1.0));
  }
  SUBCASE("край не за шкалой") {
    CHECK(withLate(window(0.0, 10.0), 80.0, kScale) == window(20.0, 30.0));
    CHECK(withEarly(window(0.0, 10.0), -80.0, kScale) == window(-20.0, 30.0));
  }
  SUBCASE("край округляется до целой миллисекунды") {
    CHECK(withLate(window(0.0, 10.0), 20.4, kScale) == window(5.0, 15.0));
  }
  SUBCASE("у неподвижного края с половиной двигаемый край тоже с половиной") {
    // +7.5±10: края −2.5 и +17.5.
    const HitWindow moved = withEarly(window(7.5, 10.0), 0.2, kScale);
    CHECK(moved.early() == doctest::Approx(0.5));
    CHECK(moved.late() == doctest::Approx(17.5));
    CHECK(moved == window(9.0, 8.5));
  }
}

TEST_CASE("полоса сдвигает окно целиком") {
  CHECK(shifted(window(0.0, 10.0), 12.0, kScale) == window(12.0, 10.0));
  CHECK(shifted(window(0.0, 10.0), 11.6, kScale) == window(12.0, 10.0));
  CHECK(shifted(window(0.0, 10.0), 60.0, kScale) == window(40.0, 10.0));
  CHECK(shifted(window(7.5, 10.0), -3.0, kScale) == window(4.5, 10.0));
}

TEST_CASE("число из текста с шагом 0.5 мс") {
  CHECK(halfMsOf("12") == 12.0);
  CHECK(halfMsOf("12.5") == 12.5);
  CHECK(halfMsOf("-7.5") == -7.5);
  CHECK(halfMsOf("−7.5") == -7.5);
  CHECK(halfMsOf("+3,5") == 3.5);
  CHECK(halfMsOf("-0.5") == -0.5);
  CHECK(halfMsOf(" 15 ") == 15.0);
  CHECK(halfMsOf("12.3") == 12.5);
  CHECK(halfMsOf("12.2") == 12.0);

  CHECK_FALSE(halfMsOf(""));
  CHECK_FALSE(halfMsOf("-"));
  CHECK_FALSE(halfMsOf("12."));
  CHECK_FALSE(halfMsOf(".5"));
  CHECK_FALSE(halfMsOf("1e2"));
  CHECK_FALSE(halfMsOf("12 5"));
  CHECK_FALSE(halfMsOf("99999999999"));
}

TEST_CASE("минус ноль — просто ноль") {
  const std::optional<double> zero = halfMsOf("-0");
  REQUIRE(zero);
  CHECK(halfMsText(*zero, MsStyle::Shown) == "0");
}

TEST_CASE("запись числа с шагом 0.5 мс") {
  CHECK(halfMsText(7.5, MsStyle::Shown) == "+7.5");
  CHECK(halfMsText(-5.0, MsStyle::Shown) == "−5");
  CHECK(halfMsText(0.0, MsStyle::Shown) == "0");
  CHECK(halfMsText(12.0, MsStyle::Signed) == "+12");
  CHECK(halfMsText(-7.5, MsStyle::Signed) == "-7.5");
  CHECK(halfMsText(12.5, MsStyle::Plain) == "12.5");
  CHECK(halfMsText(-7.5, MsStyle::Plain) == "-7.5");
  CHECK(halfMsText(10.0, MsStyle::Plain) == "10");
}
