#include <doctest/doctest.h>

#include "training/render/main/status_line.hpp"

using training::core::Interval;
using training::core::state::WorkMode;
using training::render::InputStatus;
using training::render::StatusLine;

namespace {

/// Вход есть, компенсация 32 мс, обычная скорость.
constexpr StatusLine kPresent{.host = "REAPER",
                              .input = InputStatus::Present,
                              .compensation = 32 * Interval::kMillisecond,
                              .rate = 1.0};

} // namespace

TEST_CASE("Строка состояния: режим работы решает, ждёт тренажёр или меряет") {
  CHECK(kPresent.text("Quarters", "-10…+10 ms", WorkMode::Idle, false) ==
        "Quarters, window -10…+10 ms, latency 32.0 ms — waiting for playback");
  CHECK(kPresent.text("Eighths", "+4…+20 ms", WorkMode::Run, false) ==
        "Eighths, window +4…+20 ms, latency 32.0 ms — measuring");
  CHECK(kPresent.text("Quarters", "-10…+10 ms", WorkMode::Calibration, false) ==
        "Quarters, window -10…+10 ms, latency 32.0 ms — waiting for playback");
}

TEST_CASE("Строка состояния: вход, которого нет или который выключен, — при любом режиме") {
  StatusLine absent = kPresent;
  absent.input = InputStatus::NoInput;
  CHECK(absent.text("Quarters", "-10…+10 ms", WorkMode::Run, false) ==
        "Quarters, window -10…+10 ms, latency 32.0 ms — selected input is not available");

  StatusLine disabled = kPresent;
  disabled.input = InputStatus::NotEnabled;
  CHECK(disabled.text("Quarters", "-10…+10 ms", WorkMode::Idle, false) ==
        "Quarters, window -10…+10 ms, latency 32.0 ms — MIDI input is not enabled in REAPER "
        "preferences");
}

TEST_CASE("Строка состояния: скорость не 1 и прерванная запуском калибровка") {
  StatusLine slow = kPresent;
  slow.compensation = 12 * Interval::kMillisecond;
  slow.rate = 0.75;
  CHECK(slow.text("Quarters", "-10…+10 ms", WorkMode::Run, true) ==
        "Quarters, window -10…+10 ms, latency 12.0 ms, rate 0.75 — measuring · calibration "
        "cancelled: playback started");
}
