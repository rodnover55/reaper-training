#include "training/render/main/ms_text.hpp"

#include <fmt/format.h>

#include <cmath>
#include <string>
#include <string_view>

namespace training::render {
namespace {

/// Знак «минус» для показа: U+2212, а не дефис.
constexpr std::string_view kMinus = "−";

/// Ближайшее к `value` кратное 0.5.
double half(double value) { return std::round(value * 2.0) / 2.0; }

} // namespace

std::string halfMsText(core::Interval value, MsStyle style) {
  const double ms = value / core::Interval::kMillisecond;
  const double magnitude = std::abs(half(ms));
  std::string text = magnitude == std::floor(magnitude)
                         ? fmt::format("{}", std::lround(magnitude))
                         : fmt::format("{:.1f}", magnitude);
  if (magnitude == 0.0)
    return text;
  if (ms < 0.0)
    return std::string(style == MsStyle::Shown ? kMinus : "-") + text;
  if (style != MsStyle::Plain)
    return "+" + text;
  return text;
}

} // namespace training::render
