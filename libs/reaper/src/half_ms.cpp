#include "training/reaper/half_ms.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <string_view>

namespace training::reaper {
namespace {

/// Знак «минус» для показа: U+2212, а не дефис.
constexpr std::string_view kMinus = "−";

/// Ближайшее к `value` кратное 0.5.
double half(double value) { return std::round(value * 2.0) / 2.0; }

/// Правда, если `part` состоит из цифр 0–9 и в нём хотя бы одна цифра.
bool allDigits(std::string_view part) {
  return !part.empty() &&
         std::ranges::all_of(part, [](char c) { return c >= '0' && c <= '9'; });
}

} // namespace

std::optional<core::Interval> halfMsOf(std::string_view text) {
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
  return (negative ? -value : value) * core::Interval::kMillisecond;
}

} // namespace training::reaper
