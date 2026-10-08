#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace training::grid {

/// Окно попадания: отклонения от клика, мс, при которых значение зелёное
/// (`timing-display`).
struct HitWindow {
  /// Центр окна от клика, мс: минус — до клика, плюс — после.
  double offsetMs = 0.0;

  /// Половина ширины окна, мс; не меньше `kMinToleranceMs`.
  double toleranceMs = 10.0;

  /// Ранний край окна, мс от клика; входит в окно.
  double early() const { return offsetMs - toleranceMs; }

  /// Поздний край окна, мс от клика; входит в окно.
  double late() const { return offsetMs + toleranceMs; }

  bool operator==(const HitWindow &) const = default;
};

/// Граница шкалы окна попадания по умолчанию, мс: шкала от −50 до +50.
constexpr double kDefaultScaleMs = 50.0;

/// Наименьший допуск, мс: края окна не ближе 2 мс друг к другу.
constexpr double kMinToleranceMs = 1.0;

/// Правда, если окно `window` допустимо при шкале от −`scaleMs` до
/// +`scaleMs`: допуск не меньше `kMinToleranceMs`, ранний край не раньше
/// −`scaleMs`, поздний — не позже +`scaleMs`. Кратность 0.5 мс не
/// проверяется.
bool fits(const HitWindow &window, double scaleMs);

/// Окно `window`, ранний край которого перенесён к `earlyMs`, а поздний
/// остался. Край ставится на ближайшее к `earlyMs` место, отстоящее от
/// позднего края на целое число миллисекунд, не ближе 2 мс к нему и не за
/// −`scaleMs`. Для ползунка шкалы.
///
/// @param window окно, которое лежит в шкале `scaleMs` (`fits`).
HitWindow withEarly(HitWindow window, double earlyMs, double scaleMs);

/// Окно `window`, поздний край которого перенесён к `lateMs`, а ранний
/// остался; как `withEarly`, но край не за +`scaleMs`.
HitWindow withLate(HitWindow window, double lateMs, double scaleMs);

/// Окно `window`, сдвинутое на `byMs`, округлённое до целой миллисекунды;
/// сдвиг прижимается, чтобы края легли в шкалу, допуск прежний. Для полосы
/// между ползунками.
///
/// @param window окно, которое лежит в шкале `scaleMs` (`fits`).
HitWindow shifted(HitWindow window, double byMs, double scaleMs);

/// Миллисекунды из текста, округлённые до 0.5 мс: «12», «12.5», «-7.5»,
/// «−7.5» (U+2212), «+3,5». Пробелы по краям не мешают.
///
/// @return пусто, если в тексте не десятичное число: «», «12.», «.5», «1e2».
std::optional<double> halfMsOf(std::string_view text);

/// Как записывать миллисекунды.
enum class MsStyle {
  /// Для файла настроек и поля Tolerance: «12», «12.5», «-7.5».
  Plain,
  /// Для поля Offset: «+12», «-7.5», «0».
  Signed,
  /// Для показа: «+12», «−7.5» (U+2212), «0».
  Shown,
};

/// Записывает миллисекунды `value`, кратные 0.5: целое — без дробной части,
/// с половиной — с одним знаком после точки.
std::string halfMsText(double value, MsStyle style);

} // namespace training::grid
