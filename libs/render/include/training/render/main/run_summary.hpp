#pragma once

#include "training/core/hit_window.hpp"
#include "training/core/training/state/timeline.hpp"
#include "training/render/common/fill.hpp"
#include "training/render/common/label.hpp"
#include "training/render/common/plate.hpp"
#include "training/render/element.hpp"
#include "training/render/main/node_value.hpp"

#include <array>
#include <cstddef>
#include <optional>

namespace training::render {

/// Нот в сводке идущего запуска: столько последних нот берёт `RunSummary`.
inline constexpr std::size_t kSummaryNotes = 128;

/// Наименьшая ширина плитки индикатора, пикселей: уже — числа не читаются.
inline constexpr double kMinTileWidth = 100.0;

/// Что показывают индикаторы запуска.
struct RunSummaryView {
  /// Нот в сводке: в идущем запуске — не больше `kSummaryNotes`.
  std::size_t notes = 0;

  /// Сводка за весь запуск: воспроизведение остановлено.
  bool whole = false;

  /// Прогрев: запуск идёт, и нот меньше `kSummaryNotes`.
  bool warmUp = false;

  /// Медиана отклонений, «+12.3»; цвет — по текущему окну попадания, на
  /// прогреве — `Neutral`.
  Cell median;

  /// IQR — межквартильный размах отклонений, «4.1»; цвет `Neutral`.
  Cell iqr;

  /// Оценка от 0 до 100 по текущему окну попадания (`DeviationStats::score`),
  /// целым числом, «87»; цвет `Neutral`.
  Cell score;
};

/// Индикаторы запуска: три плитки — медиана «MED», IQR и оценка «SCORE» — по
/// последним `kSummaryNotes` нотам идущего запуска, после остановки — по всем
/// нотам запуска.
class RunSummary : public Element {
public:
  /// Создаёт индикаторы без сводки.
  RunSummary();

  /// Показывает сводку тактов запуска `timeline` по окну попадания `window`.
  ///
  /// Ноты — по одной на долю (`DeviationStats::closest`) в тактах после
  /// предтакта до такта номер `end` списка, без него, по порядку тактов и
  /// долей: скрытые такты (`BarRows::shownEnd`) в сводку не идут. Пока
  /// запуск идёт (`playing`), в сводку идут последние `kSummaryNotes` из них;
  /// пока нот меньше, сводка считается по тем, что есть, и это прогрев. После
  /// остановки в сводку идут все эти ноты. Нет нот — сводки нет.
  void setRun(const core::training::state::Timeline &timeline, const core::HitWindow &window,
              bool playing, std::size_t end);

  /// Что показывают индикаторы; пусто — нот нет.
  const std::optional<RunSummaryView> &view() const { return view_; }

  /// Наименьшая ширина места под индикаторы, пикселей: три плитки
  /// `kMinTileWidth` с промежутками и полями.
  static double minWidth();

  /// Рисует на фоне строк тактов три равные плитки во всё место кадра. В
  /// плитке — подпись, под ней крупное число. Кегль числа — половина высоты
  /// плитки, но не шире плитки, от 14 до 64 пикселей. На прогреве подписи и
  /// числа тусклые; без сводки плитки пустые.
  void draw(const Frame &frame) const override;

private:
  std::optional<RunSummaryView> view_;
  Fill background_;
  Plate tile_;
  std::array<Label, 3> captions_;
  std::array<Label, 3> values_;
};

} // namespace training::render
