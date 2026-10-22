#pragma once

// Нота, которую ядро поставило в долю такта (`timing-display`).

#include "training/core/clock.hpp"
#include "training/core/hit_window.hpp"

namespace training::core::training::state {

/// Нота звука или MIDI, которая легла в долю такта.
struct Note {
  /// Разница: время ноты минус время доли; минус — раньше доли.
  Interval deviation;

  /// Оценка разницы по окну попадания, которое действовало, когда нота
  /// легла (`HitWindow::hit`).
  Hit hit = Hit::Target;

  /// Правда, если у нот совпадают разница и оценка.
  friend bool operator==(const Note &, const Note &) = default;
};

} // namespace training::core::training::state
