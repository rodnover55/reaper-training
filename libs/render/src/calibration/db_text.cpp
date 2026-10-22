#include "training/render/calibration/db_text.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <string_view>

namespace training::render {
namespace {

/// Знак «минус» для показа: U+2212, а не дефис.
constexpr std::string_view kMinus = "−";

} // namespace

std::string dbText(double value, int decimals) {
  std::string text = fmt::format("{:.{}f}", std::abs(value), decimals);
  const bool zero = std::ranges::all_of(text, [](char c) { return c == '0' || c == '.'; });
  if (value < 0.0 && !zero)
    text.insert(0, kMinus);
  return text;
}

} // namespace training::render
