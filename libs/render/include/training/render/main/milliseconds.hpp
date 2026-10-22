#pragma once

#include "training/core/clock.hpp"

#include <compare>

namespace training::render {

/// Промежуток из целого числа миллисекунд; бывает отрицательным. Дробное число
/// миллисекунд им не записать.
class Milliseconds {
public:
  /// Создаёт промежуток в `count` миллисекунд.
  explicit constexpr Milliseconds(int count) : count_(count) {}

  /// Число миллисекунд.
  constexpr int count() const { return count_; }

  /// Тот же промежуток интервалом ядра: `count()` миллисекунд.
  constexpr core::Interval interval() const { return count_ * core::Interval::kMillisecond; }

  /// Сравнивает промежутки по числу миллисекунд со знаком.
  friend constexpr auto operator<=>(Milliseconds, Milliseconds) = default;

  /// Правда, если у промежутков одно число миллисекунд.
  friend constexpr bool operator==(Milliseconds, Milliseconds) = default;

private:
  int count_;
};

} // namespace training::render
