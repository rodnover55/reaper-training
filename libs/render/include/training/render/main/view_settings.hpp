#pragma once

// Настройки отрисовки окна тренажёра (`timing-display`): шкала окна попадания,
// колонки строк тактов и панель настроек. Общие для всех проектов.

#include "training/core/clock.hpp"
#include "training/core/hit_window.hpp"
#include "training/core/types.hpp"
#include "training/render/main/milliseconds.hpp"

namespace training::render {

/// Настройки отрисовки окна. Значение каждой настройки допустимо: сеттер
/// проверяемой настройки отклоняет недопустимое значение, и настройки
/// остаются прежними.
///
/// Созданный объект: шкала от −50 до +50 мс, обе колонки строк показаны,
/// панель настроек развёрнута.
class ViewSettings {
public:
  /// Граница шкалы окна попадания у созданного объекта: шкала от −50 до
  /// +50 мс.
  static constexpr Milliseconds kDefaultScale{50};

  /// Допустимая граница шкалы окна попадания: от 10 до 200 мс.
  static constexpr core::Range<Milliseconds> kScaleRange{.min = Milliseconds(10),
                                                         .max = Milliseconds(200)};

  /// Граница шкалы окна попадания: шкала от −`scale` до +`scale`. Скрытая
  /// настройка: в окне её нет, тренажёр её не сохраняет.
  core::Interval scale() const { return scale_.interval(); }

  /// Ставит границу шкалы окна попадания.
  ///
  /// @param window действующее окно попадания.
  /// @return ложь, если `scale` вне `kScaleRange` или окно `window` за новой
  ///   шкалой (`fits`): настройки прежние.
  bool setScale(Milliseconds scale, const core::HitWindow &window);

  /// Правда, если окно попадания `window` допустимо в шкале: ранний край не
  /// раньше −`scale`, поздний — не позже +`scale`. Кратность 0.5 мс сюда не
  /// входит.
  bool fits(const core::HitWindow &window) const;

  /// Окно попадания `window`, ранний край которого перенесён к `early`, а
  /// поздний остался. Край ставится на ближайшее к `early` место, отстоящее
  /// от позднего края на целое число миллисекунд, хотя бы в 2 мс от него и в
  /// шкале. Для ползунка шкалы.
  ///
  /// @param window окно, которое лежит в шкале (`fits`).
  core::HitWindow withEarly(core::HitWindow window, core::Interval early) const;

  /// Окно попадания `window`, поздний край которого перенесён к `late`, а
  /// ранний остался; как `withEarly`.
  core::HitWindow withLate(core::HitWindow window, core::Interval late) const;

  /// Окно попадания `window`, сдвинутое на `by`, округлённое до целой
  /// миллисекунды; сдвиг прижимается, чтобы края легли в шкалу, допуск
  /// прежний. Для полосы между ползунками.
  ///
  /// @param window окно, которое лежит в шкале (`fits`).
  core::HitWindow shifted(core::HitWindow window, core::Interval by) const;

  /// Допустимое смещение при допуске окна попадания `window` и шкале: от
  /// −(`scale` − допуск) до +(`scale` − допуск).
  core::Range<core::Interval> offsetRange(const core::HitWindow &window) const;

  /// Допустимый допуск при смещении окна попадания `window` и шкале: от
  /// 1 мс до `scale` − |смещение|.
  core::Range<core::Interval> toleranceRange(const core::HitWindow &window) const;

  /// Колонка номеров тактов слева от строк («Bar numbers»).
  bool showBarNumbers() const { return showBarNumbers_; }

  /// Ставит, видна ли колонка номеров тактов.
  void setShowBarNumbers(bool shown) { showBarNumbers_ = shown; }

  /// Колонка среднего и разброса справа от строк («Mean/spread»).
  bool showBarStats() const { return showBarStats_; }

  /// Ставит, видна ли колонка среднего и разброса.
  void setShowBarStats(bool shown) { showBarStats_ = shown; }

  /// Панель настроек свёрнута в одну строку: кнопку «Settings» и строку
  /// состояния.
  bool panelCollapsed() const { return panelCollapsed_; }

  /// Ставит, свёрнута ли панель настроек в одну строку.
  void setPanelCollapsed(bool collapsed) { panelCollapsed_ = collapsed; }

  /// Правда, если совпадают все настройки.
  bool operator==(const ViewSettings &) const = default;

private:
  /// Правда, если окно попадания `window` допустимо при шкале от −`scale` до
  /// +`scale`; как `fits`.
  static bool fits(const core::HitWindow &window, core::Interval scale);

  Milliseconds scale_ = kDefaultScale;
  bool showBarNumbers_ = true;
  bool showBarStats_ = true;
  bool panelCollapsed_ = false;
};

} // namespace training::render
