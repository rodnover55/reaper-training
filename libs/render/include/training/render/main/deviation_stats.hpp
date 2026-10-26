#pragma once

#include "training/core/clock.hpp"
#include "training/core/hit_window.hpp"
#include "training/core/training/state/note.hpp"
#include "training/render/main/node_value.hpp"

#include <cstddef>
#include <vector>

namespace training::render {

/// Сводка отклонений нот от клика: медиана, IQR — межквартильный размах — и
/// оценка по окну попадания, как в отчётах rehearsal-timing.
class DeviationStats {
public:
  /// Сводка отклонений `deviations` в любом порядке.
  explicit DeviationStats(std::vector<core::Interval> deviations);

  /// Нота доли `notes`, которая идёт в показ и в сводку: с наименьшей по
  /// модулю разницей; `notes` не пустые.
  static const core::training::state::Note &
  closest(const std::vector<core::training::state::Note> &notes);

  /// Число отклонений.
  std::size_t count() const { return sorted_.size(); }

  /// Медиана — квантиль 0.5 (`quantile`); у пустой сводки — ноль.
  core::Interval median() const;

  /// IQR: квантиль 0.75 минус квантиль 0.25 (`quantile`) — ширина средней
  /// половины отклонений; у пустой сводки — ноль.
  core::Interval iqr() const;

  /// Оценка от 0 до 100 по окну попадания `window`.
  ///
  /// Балл ноты — по расстоянию её отклонения от смещения окна: 1 — не дальше
  /// допуска, линейно до 0.5 на двух допусках, линейно до 0 на трёх допусках,
  /// дальше — 0. Оценка — взвешенное среднее баллов × 100, вес ноты с баллом
  /// `v` — 1 + 2 × (1 − `v`): плохая нота весит втрое больше хорошей. У пустой
  /// сводки — 0.
  double score(const core::HitWindow &window) const;

  /// Медиана для показа: миллисекунды с одним знаком после запятой и знаком —
  /// «+2.0», «−1.5», «0.0»; цвет — `Good` между краями окна `window`
  /// включительно, иначе `Bad`.
  Cell medianCell(const core::HitWindow &window) const;

  /// IQR для показа: миллисекунды с одним знаком после запятой — «3.7»; цвет
  /// `Neutral`.
  Cell iqrCell() const;

  /// Квантиль `q` от 0 до 1 отсортированных отклонений с линейной
  /// интерполяцией между соседними: место — (`count` − 1) × `q`; у пустой
  /// сводки — ноль.
  core::Interval quantile(double q) const;

private:
  std::vector<core::Interval> sorted_;
};

} // namespace training::render
