#pragma once

#include "training/core/clock.hpp"

#include <optional>

namespace training::core {

/// Оценка ноты по окну попадания (`HitWindow::hit`).
enum class Hit {
  /// В центре окна: не дальше допуска точного попадания от смещения окна
  /// (`HitWindow::exactTolerance`).
  Target,

  /// Внутри окна, между краями включительно.
  Good,

  /// Вне окна.
  Bad,
};

/// Окно попадания: отклонения от клика, при которых оценка ноты —
/// `Hit::Target` или `Hit::Good` (`HitWindow::hit`).
class HitWindow {
public:
  /// Создаёт окно с центром `offset`, допуском `tolerance` и допуском точного
  /// попадания `exactTolerance` (по умолчанию 0.5 мс).
  ///
  /// @return пусто, если `tolerance` меньше 1 мс или `exactTolerance` больше
  /// `tolerance`.
  static constexpr std::optional<HitWindow>
  create(Interval offset, Interval tolerance,
         Interval exactTolerance = kDefaultExactTolerance) noexcept {
    if (tolerance < kMinTolerance || exactTolerance > tolerance)
      return std::nullopt;
    return HitWindow(offset, tolerance, exactTolerance);
  }

  /// Центр окна от клика: минус — до клика, плюс — после.
  constexpr Interval offset() const { return offset_; }

  /// Половина ширины окна; не меньше 1 мс.
  constexpr Interval tolerance() const { return tolerance_; }

  /// Допуск точного попадания: наибольшее отклонение от смещения окна, при
  /// котором оценка ноты — `Hit::Target`; не больше `tolerance`.
  constexpr Interval exactTolerance() const { return exactTolerance_; }

  /// Ранний край окна от клика; входит в окно.
  constexpr Interval early() const { return offset_ - tolerance_; }

  /// Поздний край окна от клика; входит в окно.
  constexpr Interval late() const { return offset_ + tolerance_; }

  /// Оценка ноты с разницей `deviation`: `Hit::Target`, если разница не
  /// дальше `exactTolerance` от смещения окна включительно, иначе `Hit::Good`
  /// между краями окна включительно, иначе `Hit::Bad`.
  Hit hit(Interval deviation) const;

  /// Правда, если у окон совпадают центр, допуск и допуск точного попадания.
  constexpr bool operator==(const HitWindow &) const = default;

private:
  /// Наименьший допуск окна попадания: края окна — хотя бы в 2 мс друг от
  /// друга.
  static constexpr Interval kMinTolerance = Interval::kMillisecond;

  /// Допуск точного попадания по умолчанию (`create`): 0.5 мс.
  static constexpr Interval kDefaultExactTolerance = 500 * Interval::kMicrosecond;

  /// Окно с центром `offset`, допуском `tolerance` и допуском точного
  /// попадания `exactTolerance`; `tolerance` не меньше `kMinTolerance`,
  /// `exactTolerance` не больше `tolerance`.
  constexpr HitWindow(Interval offset, Interval tolerance, Interval exactTolerance)
      : offset_(offset), tolerance_(tolerance), exactTolerance_(exactTolerance) {}

  Interval offset_;
  Interval tolerance_;
  Interval exactTolerance_;
};

} // namespace training::core
