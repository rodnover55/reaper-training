#pragma once

// Доля такта.

#include <compare>

namespace training::core::training::state {

/// Доля такта: место в такте в ударах от его начала, дробное. Удар —
/// длительность, которую задаёт знаменатель размера (`Meter::unit`).
class Beat {
public:
  /// Создаёт долю на первом ударе такта.
  constexpr Beat() = default;

  /// Создаёт долю на месте `beats` ударов от начала такта: 0 — первый удар,
  /// 1.5 — середина второго.
  constexpr explicit Beat(double beats) : beats_(beats) {}

  /// Место доли в ударах от начала такта.
  constexpr double beats() const { return beats_; }

  /// Сравнивает доли по месту в такте: доля ближе к началу такта — меньше.
  /// Равны доли с одним местом.
  constexpr auto operator<=>(const Beat &) const = default;

private:
  double beats_ = 0.0;
};

} // namespace training::core::training::state
