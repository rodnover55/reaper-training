#pragma once

// Состояние калибратора. Ошибка перехода между состояниями —
// `training::core::InvalidStep`.

#include "training/core/errors.hpp"

namespace training::core::calibration {

/// Наименьший уровень, dBFS: тише калибровка не меряет, цифровая тишина — он.
/// Его же показывают уровни, которые ещё не измерены.
inline constexpr double kFloorDb = -120.0;

/// В каком состоянии калибратор. Разрешены переходы: `Off` → `Silence` →
/// `Notes` → `Done`, и из `Silence`, `Notes` и `Done` — в `Off`.
enum class Step {
  /// Калибратор не запущен.
  Off,
  /// Калибратор измеряет порог тишины.
  Silence,
  /// Калибратор измеряет уровень нот.
  Notes,
  /// Калибратор завершил измерения.
  Done,
};

} // namespace training::core::calibration
