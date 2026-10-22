#pragma once

// Общие типы ядра тренажёра.

#include <optional>

namespace training::core {

/// Допустимые значения: от `min` до `max` включительно.
template <class T> struct Range {
  T min{};
  T max{};

  /// Правда, если `value` не меньше `min` и не больше `max`.
  constexpr bool contains(T value) const { return value >= min && value <= max; }

  /// Правда, если у диапазонов совпадают обе границы.
  constexpr bool operator==(const Range &) const = default;
};

/// Частота дискретизации, Гц; больше нуля.
class SampleRate {
public:
  /// Частота `hz`, Гц.
  ///
  /// @return пусто, если `hz` — ноль, меньше нуля или NaN.
  static constexpr std::optional<SampleRate> create(double hz) noexcept {
    if (!(hz > 0.0))
      return std::nullopt;
    return SampleRate(hz);
  }

  /// Частота, Гц.
  constexpr double hz() const { return hz_; }

  /// Правда, если частоты равны.
  constexpr bool operator==(const SampleRate &) const = default;

private:
  /// Частота `hz`, Гц; больше нуля.
  constexpr explicit SampleRate(double hz) : hz_(hz) {}

  double hz_;
};

} // namespace training::core
