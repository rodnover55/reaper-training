#include "training/render/main/view_settings.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace training::render {
namespace {

/// Наименьший допуск окна попадания в поле Tolerance и у ползунков шкалы:
/// края окна — хотя бы в 2 мс друг от друга.
constexpr core::Interval kMinTolerance = core::Interval::kMillisecond;

/// Наибольшее по модулю смещение, при котором окно с допуском `tolerance`
/// лежит в шкале.
core::Interval offsetLimit(core::Interval tolerance, core::Interval scale) {
  return std::max(scale - tolerance, core::Interval());
}

/// Окно с краями `early` и `late`.
///
/// @return пусто, если `late` − `early` меньше 2 * `kMinTolerance`.
std::optional<core::HitWindow> ofEdges(core::Interval early, core::Interval late) {
  return core::HitWindow::create((early + late) / 2.0, (late - early) / 2.0);
}

/// Сколько целых миллисекунд между краями, если подвижный край стоит как
/// можно ближе к `wanted` от неподвижного: хотя бы два и самое большее
/// `most`.
core::Interval edgeSteps(core::Interval wanted, core::Interval most) {
  // Небольшой запас: `most` складывается из чисел, кратных 0.5, и floor
  // должен сохранить целое при округлении.
  const double steps = std::clamp(std::round(wanted / core::Interval::kMillisecond),
                                  2.0 * (kMinTolerance / core::Interval::kMillisecond),
                                  std::floor(most / core::Interval::kMillisecond + 1e-9));
  return core::Interval::kMillisecond * steps;
}

} // namespace

bool ViewSettings::setScale(Milliseconds scale, const core::HitWindow &window) {
  if (!kScaleRange.contains(scale) || !fits(window, scale.interval()))
    return false;
  scale_ = scale;
  return true;
}

bool ViewSettings::fits(const core::HitWindow &window) const { return fits(window, scale()); }

core::HitWindow ViewSettings::withEarly(core::HitWindow window, core::Interval early) const {
  const core::Interval late = window.late();
  // Края хотя бы в 2 мс друг от друга (`edgeSteps`): окно есть всегда.
  return ofEdges(late - edgeSteps(late - early, late + scale()), late).value_or(window);
}

core::HitWindow ViewSettings::withLate(core::HitWindow window, core::Interval late) const {
  const core::Interval early = window.early();
  return ofEdges(early, early + edgeSteps(late - early, scale() - early)).value_or(window);
}

core::HitWindow ViewSettings::shifted(core::HitWindow window, core::Interval by) const {
  const core::Interval limit = offsetLimit(window.tolerance(), scale());
  const core::Interval offset =
      std::clamp(window.offset() + core::Interval::kMillisecond *
                                       std::round(by / core::Interval::kMillisecond),
                 -limit, limit);
  // Допуск прежний: окно со сдвинутым центром есть всегда.
  return core::HitWindow::create(offset, window.tolerance()).value_or(window);
}

core::Range<core::Interval> ViewSettings::offsetRange(const core::HitWindow &window) const {
  const core::Interval limit = scale() - window.tolerance();
  return {.min = -limit, .max = limit};
}

core::Range<core::Interval> ViewSettings::toleranceRange(const core::HitWindow &window) const {
  return {.min = kMinTolerance, .max = scale() - window.offset().abs()};
}

bool ViewSettings::fits(const core::HitWindow &window, core::Interval scale) {
  return window.early() >= -scale && window.late() <= scale;
}

} // namespace training::render
