#pragma once

// Размер такта.

namespace training::core::training::state {

/// Размер такта.
struct Meter {
  /// Ударов в такте — числитель размера; больше нуля.
  int beats = 4;

  /// Длительность удара — знаменатель размера: 4 — четверть, 8 — восьмая.
  int unit = 4;

  /// Правда, если у размеров совпадают число ударов и их длительность.
  friend bool operator==(const Meter &, const Meter &) = default;
};

} // namespace training::core::training::state
