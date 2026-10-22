#include <doctest/doctest.h>

#include "training/reaper/compensation.hpp"

using training::core::Interval;
using training::reaper::Compensation;

TEST_CASE("компенсация: по правилу записи") {
  // Галка включена: вход и выход из GetInputOutputLatency, поправки уже в них.
  CHECK(Compensation::audioOf(512, 1024, 0, 0, true, 48000.0) / Interval::kSecond ==
        doctest::Approx(0.032));
  CHECK(Compensation::audioOf(992, 1024, 480, 0, true, 48000.0) / Interval::kSecond ==
        doctest::Approx(0.042));

  // Галка выключена: только ручные поправки.
  CHECK(Compensation::audioOf(992, 1024, 480, 0, false, 48000.0) / Interval::kSecond ==
        doctest::Approx(0.010));
  CHECK(Compensation::audioOf(512, 1024, 0, 0, false, 48000.0) / Interval::kSecond == 0.0);
}

TEST_CASE("компенсация MIDI: по правилу записи MIDI") {
  // Галка включена: блок и выход из GetInputOutputLatency, поправка выхода уже
  // в нём.
  CHECK(Compensation::midiOf(512, 1024, 0, true, 48000.0) / Interval::kMillisecond ==
        doctest::Approx(32.0));
  CHECK(Compensation::midiOf(512, 1504, 480, true, 48000.0) / Interval::kMillisecond ==
        doctest::Approx(42.0));
  CHECK(Compensation::midiOf(128, 256, 0, true, 48000.0) / Interval::kMillisecond ==
        doctest::Approx(8.0));
  CHECK(Compensation::midiOf(1024, 2048, 0, true, 48000.0) / Interval::kMillisecond ==
        doctest::Approx(64.0));

  // Галка выключена: блок и ручная поправка выхода.
  CHECK(Compensation::midiOf(512, 1024, 0, false, 48000.0) / Interval::kMillisecond ==
        doctest::Approx(10.667).epsilon(0.0001));
  CHECK(Compensation::midiOf(512, 1504, 480, false, 48000.0) / Interval::kMillisecond ==
        doctest::Approx(20.667).epsilon(0.0001));
  CHECK(Compensation::midiOf(128, 256, 0, false, 48000.0) / Interval::kMillisecond ==
        doctest::Approx(2.667).epsilon(0.0001));
  CHECK(Compensation::midiOf(1024, 2048, 0, false, 48000.0) / Interval::kMillisecond ==
        doctest::Approx(21.333).epsilon(0.0001));
}
