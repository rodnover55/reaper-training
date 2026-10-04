#pragma once

#include "training/grid/mode.hpp"
#include "training/grid/timeline.hpp"

#include <cstdint>

namespace training::grid {

/// Узел сетки, ближайший к ноте.
struct NodeHit {
  /// Удар узла от начала проекта (0 — первый удар проекта).
  std::int64_t beat = 0;

  /// Номер узла в ударе, с 0; меньше `divisions(mode)`.
  int slot = 0;

  /// Время узла на шкале, с.
  double time = 0.0;
};

/// Ближайший к моменту `time` узел сетки режима `mode`. Узел `slot` удара
/// `beat` стоит на `timeline.timeAt(beat + slot / divisions(mode))`: деление
/// идёт в ударах, поэтому узлы верны и при плавной смене темпа. Нота может
/// достаться и первому узлу следующего удара.
NodeHit nearestNode(double time, Mode mode, const Timeline &timeline);

} // namespace training::grid
