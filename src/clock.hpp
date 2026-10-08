#pragma once

// Часы ядра тренажёра у связки: моменты снимаются с монотонных часов ОС и
// отсчитываются от открытия журнала.

#include "journal.hpp"

#include "training/trainer/clock.hpp"

#include <chrono>

namespace training::reaper {

/// Момент сейчас по часам ядра (`trainer::Moment`): время от открытия журнала
/// по монотонным часам ОС — в той же шкале, что отметки времени строк журнала.
/// Без блокировок — годится и звуковому потоку.
inline trainer::Moment momentNow() {
  return trainer::Moment(std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - journalOpenedAt()));
}

} // namespace training::reaper
