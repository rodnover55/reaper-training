#include <doctest/doctest.h>

#include "training/render/main/view_settings.hpp"

using training::core::HitWindow;
using training::core::Interval;
using training::core::Range;
using training::render::Milliseconds;
using training::render::ViewSettings;

namespace {

/// Интервал в `value` мс.
Interval ms(double value) { return value * Interval::kMillisecond; }

/// Окно попадания `offset` ± `tolerance`, мс.
HitWindow windowOf(double offset, double tolerance) {
  return *HitWindow::create(ms(offset), ms(tolerance));
}

} // namespace

TEST_CASE("Настройки отрисовки: по умолчанию") {
  const ViewSettings view;
  CHECK(view.scale() == ms(50.0));
  CHECK(view.showBarNumbers());
  CHECK(view.showBarStats());
  CHECK(view.hiddenBars() == 0);
  CHECK_FALSE(view.panelCollapsed());
}

TEST_CASE("Настройки отрисовки: скрытых тактов от 0 до 16") {
  ViewSettings view;
  CHECK(view.setHiddenBars(16));
  CHECK(view.hiddenBars() == 16);
  CHECK_FALSE(view.setHiddenBars(17));
  CHECK_FALSE(view.setHiddenBars(-1));
  CHECK(view.hiddenBars() == 16);
  CHECK(view.setHiddenBars(0));
  CHECK(view.hiddenBars() == 0);
}

TEST_CASE("Настройки отрисовки: граница шкалы от 10 до 200 мс, окно в шкале") {
  const HitWindow centered = windowOf(0.0, 10.0);
  ViewSettings view;
  CHECK(view.setScale(Milliseconds(10), centered));
  CHECK(view.scale() == ms(10.0));
  CHECK(view.setScale(Milliseconds(200), centered));
  CHECK(view.scale() == ms(200.0));

  const ViewSettings before = view;
  CHECK_FALSE(view.setScale(Milliseconds(9), centered));
  CHECK_FALSE(view.setScale(Milliseconds(201), centered));
  CHECK(view == before);

  SUBCASE("окно за краем новой шкалы") {
    ViewSettings narrow;
    CHECK_FALSE(narrow.setScale(Milliseconds(30), windowOf(40.0, 10.0)));
    CHECK(narrow == ViewSettings{});

    CHECK(narrow.setScale(Milliseconds(30), centered));
    CHECK(narrow.scale() == ms(30.0));
  }
}

TEST_CASE("Настройки отрисовки: окно попадания — края в шкале") {
  const ViewSettings view;
  CHECK(view.fits(windowOf(40.0, 10.0)));
  CHECK(view.fits(windowOf(7.5, 12.5)));
  CHECK_FALSE(view.fits(windowOf(40.5, 10.0)));
  CHECK_FALSE(view.fits(windowOf(0.0, 50.5)));
}

TEST_CASE("Настройки отрисовки: допустимое смещение и допуск") {
  const ViewSettings view;
  const HitWindow centered = windowOf(0.0, 10.0);
  CHECK(view.offsetRange(centered) == Range<Interval>{.min = ms(-40.0), .max = ms(40.0)});
  CHECK(view.toleranceRange(centered) == Range<Interval>{.min = ms(1.0), .max = ms(50.0)});

  const HitWindow shifted = windowOf(40.0, 10.0);
  CHECK(view.toleranceRange(shifted) == Range<Interval>{.min = ms(1.0), .max = ms(10.0)});
  CHECK(view.offsetRange(shifted) == Range<Interval>{.min = ms(-40.0), .max = ms(40.0)});
}

TEST_CASE("Настройки отрисовки: окно допустимо — края в шкале") {
  const ViewSettings view;
  SUBCASE("края на границах шкалы") {
    ViewSettings narrow;
    REQUIRE(narrow.setScale(Milliseconds(10), windowOf(0.0, 10.0)));
    CHECK(narrow.fits(windowOf(0.0, 10.0)));
    CHECK(view.fits(windowOf(40.0, 10.0)));
    CHECK(view.fits(windowOf(-40.0, 10.0)));
  }
  SUBCASE("край за шкалой") {
    ViewSettings narrow;
    REQUIRE(narrow.setScale(Milliseconds(10), windowOf(0.0, 10.0)));
    CHECK_FALSE(view.fits(windowOf(40.5, 10.0)));
    CHECK_FALSE(view.fits(windowOf(-40.5, 10.0)));
    CHECK_FALSE(narrow.fits(windowOf(0.0, 10.5)));
  }
}

TEST_CASE("Настройки отрисовки: ползунок двигает свой край, другой стоит") {
  const ViewSettings view;
  SUBCASE("правый край 0±10 на +20") {
    CHECK(view.withLate(windowOf(0.0, 10.0), ms(20.0)) == windowOf(5.0, 15.0));
  }
  SUBCASE("левый край −20…+20 на −5") {
    CHECK(view.withEarly(windowOf(0.0, 20.0), ms(-5.0)) == windowOf(7.5, 12.5));
  }
  SUBCASE("левый край не ближе 2 мс к правому") {
    CHECK(view.withEarly(windowOf(0.0, 10.0), ms(15.0)) == windowOf(9.0, 1.0));
  }
  SUBCASE("край не за шкалой") {
    CHECK(view.withLate(windowOf(0.0, 10.0), ms(80.0)) == windowOf(20.0, 30.0));
    CHECK(view.withEarly(windowOf(0.0, 10.0), ms(-80.0)) == windowOf(-20.0, 30.0));
  }
  SUBCASE("край округляется до целой миллисекунды") {
    CHECK(view.withLate(windowOf(0.0, 10.0), ms(20.4)) == windowOf(5.0, 15.0));
  }
  SUBCASE("у неподвижного края с половиной двигаемый край тоже с половиной") {
    // +7.5±10: края −2.5 и +17.5.
    const HitWindow moved = view.withEarly(windowOf(7.5, 10.0), ms(0.2));
    CHECK(moved.early() / Interval::kMillisecond == doctest::Approx(0.5));
    CHECK(moved.late() / Interval::kMillisecond == doctest::Approx(17.5));
    CHECK(moved == windowOf(9.0, 8.5));
  }
}

TEST_CASE("Настройки отрисовки: полоса сдвигает окно целиком") {
  const ViewSettings view;
  CHECK(view.shifted(windowOf(0.0, 10.0), ms(12.0)) == windowOf(12.0, 10.0));
  CHECK(view.shifted(windowOf(0.0, 10.0), ms(11.6)) == windowOf(12.0, 10.0));
  CHECK(view.shifted(windowOf(0.0, 10.0), ms(60.0)) == windowOf(40.0, 10.0));
  CHECK(view.shifted(windowOf(7.5, 10.0), ms(-3.0)) == windowOf(4.5, 10.0));
}
