#pragma once

#include "training/core/clock.hpp"
#include "training/core/hit_window.hpp"
#include "training/core/training/state/timeline.hpp"
#include "training/render/common/fill.hpp"
#include "training/render/element.hpp"
#include "training/render/main/bar_row.hpp"

#include <optional>
#include <vector>

namespace training::render {

/// Строк тактов в окне.
inline constexpr int kRows = 8;

/// Область строк тактов окна: строки сверху вниз на фоне окна и тонкие
/// линии на краях такта.
class BarRows : public Element {
public:
  /// Создаёт пустую область с обеими колонками.
  BarRows();

  /// Включает и выключает колонку номеров тактов `barNumbers` и колонку
  /// среднего и разброса `barStats`.
  void setColumns(bool barNumbers, bool barStats);

  /// Показывает такты запуска `timeline` (`timing-display`): строки — такты,
  /// в которые вошло воспроизведение, новые сверху, не больше `kRows`.
  ///
  /// - Первый такт списка — предтакт перед началом запуска — строкой не
  ///   показывается.
  /// - Верхняя строка — такт, в котором позиция `heard`.
  /// - Такт без нот, под которым в списке тоже такт без нот, свёрнут: из
  ///   подряд идущих тактов без нот виден самый ранний. Верхняя строка видна
  ///   всегда.
  /// - Такт без нот, ни одна доля которого не действует
  ///   (`core::training::state::Timeline::time`), строкой не показывается:
  ///   его целиком заменили пришедшие после него такты.
  /// - Прошлый такт строки для маркеров смен — такт перед ней в списке.
  ///
  /// @param heard слышимая позиция во времени ядра: в такт вошло
  ///   воспроизведение, если его `start` не позже неё. Пусто — строк нет.
  /// @param playing идёт воспроизведение: у верхней строки выделен удар
  ///   позиции `heard` — удар последней доли не позже неё. У верхней строки нет
  ///   среднего и разброса и без воспроизведения.
  /// @param window текущее окно попадания: им красится среднее
  ///   (`BarRow::setRow`).
  void setBars(const core::training::state::Timeline &timeline,
               std::optional<core::Moment> heard, bool playing, const core::HitWindow &window);

  /// Строки области сверху вниз.
  const std::vector<BarRow> &rows() const { return rows_; }

  /// Рисует в месте кадра первые `kRows` строк — место делится на `kRows`
  /// равных строк — и линии на краях такта: у начала — от номеров тактов, у
  /// конца — от среднего и разброса; у выключенной колонки линии нет.
  void draw(const Frame &frame) const override;

private:
  bool barNumbers_ = true;
  bool barStats_ = true;
  Fill background_;
  Fill line_;
  std::vector<BarRow> rows_;
};

} // namespace training::render
