#include "training/grid/hit_window.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <string>
#include <string_view>

namespace training::grid {
namespace {

/// Знак «минус» для показа: U+2212, а не дефис.
constexpr std::string_view kMinus = "−";

/// Ближайшее к `value` кратное 0.5.
double half(double value) { return std::round(value * 2.0) / 2.0; }

/// Наибольшее по модулю смещение, при котором окно с допуском `toleranceMs`
/// лежит в шкале.
double offsetLimit(double toleranceMs, double scaleMs) {
  return std::max(scaleMs - toleranceMs, 0.0);
}

/// Окно с краями `early` и `late`.
HitWindow ofEdges(double early, double late) {
  return {.offsetMs = (early + late) / 2.0, .toleranceMs = (late - early) / 2.0};
}

/// Сколько целых миллисекунд между краями, если подвижный край стоит как
/// можно ближе к `wanted` от неподвижного: не меньше двух и не больше `most`.
double edgeSteps(double wanted, double most) {
  // Небольшой запас: `most` складывается из чисел, кратных 0.5, и floor не
  // должен терять целое из-за округления.
  return std::clamp(std::round(wanted), 2.0 * kMinToleranceMs, std::floor(most + 1e-9));
}

/// Правда, если `part` не пуст и состоит из цифр 0–9.
bool allDigits(std::string_view part) {
  return !part.empty() &&
         std::ranges::all_of(part, [](char c) { return c >= '0' && c <= '9'; });
}

} // namespace

HitWindow fitted(HitWindow window, double scaleMs) {
  const double tolerance = std::clamp(half(window.toleranceMs), kMinToleranceMs,
                                      std::max(scaleMs, kMinToleranceMs));
  const double limit = offsetLimit(tolerance, scaleMs);
  return {.offsetMs = std::clamp(half(window.offsetMs), -limit, limit),
          .toleranceMs = tolerance};
}

HitWindow withOffset(HitWindow window, double offsetMs, double scaleMs) {
  const double limit = offsetLimit(window.toleranceMs, scaleMs);
  window.offsetMs = std::clamp(half(offsetMs), -limit, limit);
  return window;
}

HitWindow withTolerance(HitWindow window, double toleranceMs, double scaleMs) {
  const double most = std::max(scaleMs - std::abs(window.offsetMs), kMinToleranceMs);
  window.toleranceMs = std::clamp(half(toleranceMs), kMinToleranceMs, most);
  return window;
}

HitWindow withEarly(HitWindow window, double earlyMs, double scaleMs) {
  const double late = window.late();
  return ofEdges(late - edgeSteps(late - earlyMs, late + scaleMs), late);
}

HitWindow withLate(HitWindow window, double lateMs, double scaleMs) {
  const double early = window.early();
  return ofEdges(early, early + edgeSteps(lateMs - early, scaleMs - early));
}

HitWindow shifted(HitWindow window, double byMs, double scaleMs) {
  const double limit = offsetLimit(window.toleranceMs, scaleMs);
  window.offsetMs = std::clamp(window.offsetMs + std::round(byMs), -limit, limit);
  return window;
}

std::optional<double> halfMsOf(std::string_view text) {
  while (!text.empty() && text.front() == ' ')
    text.remove_prefix(1);
  while (!text.empty() && text.back() == ' ')
    text.remove_suffix(1);

  bool negative = false;
  if (text.starts_with(kMinus)) {
    negative = true;
    text.remove_prefix(kMinus.size());
  } else if (text.starts_with('-') || text.starts_with('+')) {
    negative = text.front() == '-';
    text.remove_prefix(1);
  }

  const std::size_t point = text.find_first_of(".,");
  const std::string_view whole = text.substr(0, point);
  const std::string_view fraction =
      point == std::string_view::npos ? std::string_view() : text.substr(point + 1);
  if (!allDigits(whole) || (point != std::string_view::npos && !allDigits(fraction)))
    return std::nullopt;

  // Дробная часть — вручную: дробного from_chars нет в системной библиотеке
  // C++ macOS 12.
  int units = 0;
  const auto [end, error] = std::from_chars(whole.data(), whole.data() + whole.size(), units);
  if (error != std::errc())
    return std::nullopt;
  double value = units;
  double weight = 0.1;
  for (const char digit : fraction.substr(0, 6)) {
    value += (digit - '0') * weight;
    weight /= 10.0;
  }

  value = half(value);
  if (value == 0.0)
    return 0.0;
  return negative ? -value : value;
}

std::string halfMsText(double value, MsStyle style) {
  const double magnitude = std::abs(half(value));
  std::string text = magnitude == std::floor(magnitude)
                         ? fmt::format("{}", std::lround(magnitude))
                         : fmt::format("{:.1f}", magnitude);
  if (magnitude == 0.0)
    return text;
  if (value < 0.0)
    return std::string(style == MsStyle::Shown ? kMinus : "-") + text;
  if (style != MsStyle::Plain)
    return "+" + text;
  return text;
}

} // namespace training::grid
