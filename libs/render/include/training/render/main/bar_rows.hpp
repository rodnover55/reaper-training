#pragma once

#include "training/core/clock.hpp"
#include "training/core/hit_window.hpp"
#include "training/core/training/state/timeline.hpp"
#include "training/render/common/fill.hpp"
#include "training/render/element.hpp"
#include "training/render/main/bar_row.hpp"
#include "training/render/main/hidden_row.hpp"

#include <cstddef>
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
  /// медианы и IQR `barStats`.
  void setColumns(bool barNumbers, bool barStats);

  /// Показывает такты запуска `timeline` (`timing-display`): строки — такты,
  /// в которые вошло воспроизведение, новые сверху, не больше `kRows`.
  ///
  /// - Пока идёт воспроизведение (`playing`), последние `hiddenBars` тактов
  ///   до такта позиции `heard` включительно скрыты: над строками — строка
  ///   скрытых тактов (`HiddenRow`), верхняя строка — такт перед скрытыми, и
  ///   она не текущая: у неё есть медиана и IQR и нет текущего удара. Тактов
  ///   после предтакта меньше — скрыты все.
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
  ///   медианы и IQR и без воспроизведения.
  /// @param window текущее окно попадания: им красится медиана
  ///   (`BarRow::setRow`).
  void setBars(const core::training::state::Timeline &timeline,
               std::optional<core::Moment> heard, bool playing, const core::HitWindow &window,
               int hiddenBars = 0);

  /// Строки области сверху вниз.
  const std::vector<BarRow> &rows() const { return rows_; }

  /// Строка скрытых тактов; пусто — скрытых тактов нет.
  const std::optional<HiddenView> &hidden() const { return hidden_; }

  /// Конец показанных тактов: номер в списке такта после последнего
  /// показанного такта; без скрытых тактов — число тактов списка.
  std::size_t shownEnd() const { return shownEnd_; }

  /// Рисует в месте кадра строку скрытых тактов высотой 0.6 строки, если она
  /// есть, под ней первые `kRows` строк — место делится на `kRows` равных
  /// строк и эту строку — и линии на краях такта: у начала — от номеров тактов, у
  /// конца — от медианы и IQR; у выключенной колонки линии нет.
  void draw(const Frame &frame) const override;

private:
  bool barNumbers_ = true;
  bool barStats_ = true;
  Fill background_;
  Fill line_;
  std::vector<BarRow> rows_;
  std::optional<HiddenView> hidden_;
  HiddenRow hiddenRow_;
  std::size_t shownEnd_ = 0;
};

} // namespace training::render
