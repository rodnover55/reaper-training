#pragma once

// Позиция в тактах запуска.

#include "training/core/training/state/beat.hpp"

#include <cstddef>

namespace training::core::training::state {

/// Позиция в списке тактов запуска (`Timeline`): такт и доля в нём.
struct Position {
  /// Индекс такта в `Timeline::bars`.
  std::size_t bar = 0;

  /// Доля такта.
  Beat division;

  /// Правда, если у позиций совпадают такт и доля.
  friend bool operator==(const Position &, const Position &) = default;
};

} // namespace training::core::training::state
