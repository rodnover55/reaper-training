#include <doctest/doctest.h>

#include "training/core/settings.hpp"
#include "training/core/types.hpp"

#include <optional>

using training::core::HitWindow;
using training::core::Interval;

namespace core = training::core;

namespace {

/// Окно попадания `offset` ± `tolerance`, мс.
HitWindow windowOf(double offset, double tolerance) {
  return *HitWindow::create(offset * Interval::kMillisecond,
                            tolerance * Interval::kMillisecond);
}

} // namespace

TEST_CASE("Настройки ядра: созданные — с окном попадания и порогом тишины") {
  const core::Settings settings(windowOf(12.0, 8.0), -40.0);
  CHECK(settings.window() == windowOf(12.0, 8.0));
  CHECK(settings.silenceDb() == -40.0);
}

TEST_CASE("Окно попадания: края — смещение минус и плюс допуск") {
  const HitWindow hit = windowOf(12.0, 8.0);
  CHECK(hit.early() == 4.0 * Interval::kMillisecond);
  CHECK(hit.late() == 20.0 * Interval::kMillisecond);
}

TEST_CASE("Окно попадания: допуск хотя бы 1 мс") {
  const std::optional<HitWindow> least =
      HitWindow::create(Interval(), 1.0 * Interval::kMillisecond);
  REQUIRE(least);
  CHECK(least->offset() == Interval());
  CHECK(least->tolerance() == 1.0 * Interval::kMillisecond);
  CHECK_FALSE(HitWindow::create(Interval(), 0.5 * Interval::kMillisecond));
}

TEST_CASE("Окно попадания: допуск точного попадания — заданный, без него 0.5 мс") {
  CHECK(windowOf(0.0, 10.0).exactTolerance() == 500 * Interval::kMicrosecond);
  const std::optional<HitWindow> exact = HitWindow::create(
      Interval(), 10.0 * Interval::kMillisecond, 2.0 * Interval::kMillisecond);
  REQUIRE(exact);
  CHECK(exact->exactTolerance() == 2.0 * Interval::kMillisecond);
}

TEST_CASE("Окно попадания: допуск точного попадания не больше допуска") {
  const std::optional<HitWindow> equal = HitWindow::create(
      Interval(), 2.0 * Interval::kMillisecond, 2.0 * Interval::kMillisecond);
  REQUIRE(equal);
  CHECK(equal->exactTolerance() == equal->tolerance());
  CHECK_FALSE(HitWindow::create(Interval(), 1.0 * Interval::kMillisecond,
                                2.0 * Interval::kMillisecond));
}

TEST_CASE("Настройки ядра: окно попадания ставится как есть") {
  core::Settings settings(windowOf(0.0, 10.0), -50.0);
  settings.setWindow(windowOf(7.5, 12.5));
  CHECK(settings.window() == windowOf(7.5, 12.5));
}

TEST_CASE("Настройки ядра: порог тишины ставится как есть") {
  core::Settings settings(windowOf(0.0, 10.0), -50.0);
  settings.setSilenceDb(-50.5);
  CHECK(settings.silenceDb() == -50.5);
}

TEST_CASE("Частота дискретизации: больше нуля") {
  const std::optional<core::SampleRate> rate = core::SampleRate::create(48000.0);
  REQUIRE(rate);
  CHECK(rate->hz() == 48000.0);
  CHECK_FALSE(core::SampleRate::create(0.0));
  CHECK_FALSE(core::SampleRate::create(-44100.0));
}
