#pragma once

#include <cstdint>

namespace training::core::detector {

/// Найденная атака.
struct Onset {
  /// Начало атаки — номер сэмпла от начала потока, пропуски (`Detector::skip`)
  /// включительно: 0 — первый сэмпл после создания детектора или
  /// `Detector::reset`.
  std::uint64_t position = 0;

  /// Правда, если у атак совпадает начало.
  bool operator==(const Onset &) const = default;
};

} // namespace training::core::detector
