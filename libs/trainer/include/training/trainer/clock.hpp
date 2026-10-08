#pragma once

// Часы ядра тренажёра: моменты, которые ядро получает от хоста и сравнивает.

#include <chrono>
#include <string>

namespace training::trainer {

/// Часы ядра: целые наносекунды от точки отсчёта, которую выбирает хост, —
/// одной на все моменты. Сами часы ядро не читает: моменты приходят
/// параметрами.
struct Clock {
  using duration = std::chrono::nanoseconds;
  using rep = duration::rep;
  using period = duration::period;
  using time_point = std::chrono::time_point<Clock>;
  static constexpr bool is_steady = true;
};

/// Момент по часам ядра. Промежутки между моментами — `std::chrono::nanoseconds`.
using Moment = Clock::time_point;

/// Момент для журнала: секунды от точки отсчёта часов с шестью знаками после
/// запятой — до микросекунд, меньшие доли отбрасываются, — например
/// «12.000345». Строки с моментами сравниваются как числа.
std::string momentText(Moment moment);

} // namespace training::trainer
