#pragma once

// Время ядра тренажёра у связки: часы ядра снимаются с монотонных часов ОС и
// отсчитываются от открытия журнала, время шкалы проекта — позиция REAPER.

#include "journal.hpp"

#include "training/core/clock.hpp"

#include <chrono>

namespace training::reaper {

/// Момент сейчас по часам ядра (`core::Moment`): время от открытия журнала
/// по монотонным часам ОС — в той же шкале, что отметки времени строк журнала.
/// Без блокировок — годится и звуковому потоку.
inline core::Moment momentNow() {
  return core::Moment() + std::chrono::duration_cast<std::chrono::nanoseconds>(
                              std::chrono::steady_clock::now() - journalOpenedAt())
                                  .count() *
                              core::Interval::kNanosecond;
}

/// Сколько времени по часам ОС (`momentNow`) без звука от звуковой карты
/// кончает подачу звука в калибровку: калибровка не слышит вход.
inline constexpr core::Interval kNoSoundTime = 2 * core::Interval::kSecond;

/// Время шкалы проекта для ядра по позиции REAPER `seconds` — секундам от
/// начала проекта: позиция 12 с — 12 с от точки отсчёта `core::Moment()`.
/// Округляется до наносекунды.
inline core::Moment projectTime(double seconds) {
  return core::Moment() + seconds * core::Interval::kSecond;
}

/// Позиция REAPER — секунды от начала проекта — по времени шкалы проекта
/// `time` (`projectTime`).
inline double projectSeconds(core::Moment time) {
  return (time - core::Moment()) / core::Interval::kSecond;
}

} // namespace training::reaper
