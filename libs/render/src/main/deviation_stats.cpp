#include "training/render/main/deviation_stats.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <utility>

namespace training::render {
namespace {

/// Знак «минус» для показа: U+2212, а не дефис.
constexpr std::string_view kMinus = "−";

/// Балл ноты на расстоянии `away` от смещения окна при допуске `tolerance`:
/// 1 — не дальше допуска, 0.5 — на двух допусках, 0 — на трёх и дальше.
double pointsOf(core::Interval away, core::Interval tolerance) {
  const double steps = away / tolerance;
  if (steps <= 1.0)
    return 1.0;
  if (steps <= 2.0)
    return 1.0 - 0.5 * (steps - 1.0);
  if (steps <= 3.0)
    return 0.5 - 0.5 * (steps - 2.0);
  return 0.0;
}

} // namespace

DeviationStats::DeviationStats(std::vector<core::Interval> deviations)
    : sorted_(std::move(deviations)) {
  std::ranges::sort(sorted_);
}

const core::training::state::Note &
DeviationStats::closest(const std::vector<core::training::state::Note> &notes) {
  return *std::ranges::min_element(
      notes, {}, [](const core::training::state::Note &note) { return note.deviation.abs(); });
}

core::Interval DeviationStats::quantile(double q) const {
  if (sorted_.empty())
    return {};
  const double place = static_cast<double>(sorted_.size() - 1) * q;
  const auto below = static_cast<std::size_t>(std::floor(place));
  if (below + 1 >= sorted_.size())
    return sorted_.back();
  const double part = place - static_cast<double>(below);
  return sorted_[below] + (sorted_[below + 1] - sorted_[below]) * part;
}

core::Interval DeviationStats::median() const { return quantile(0.5); }

core::Interval DeviationStats::iqr() const { return quantile(0.75) - quantile(0.25); }

Cell DeviationStats::medianCell(const core::HitWindow &window) const {
  const double shown = std::round(median() / core::Interval::kMillisecond * 10.0) / 10.0;
  std::string text;
  if (shown > 0.0)
    text = fmt::format("+{:.1f}", shown);
  else if (shown < 0.0)
    text = fmt::format("{}{:.1f}", kMinus, -shown);
  else
    text = "0.0";
  const bool inside = shown >= window.early() / core::Interval::kMillisecond &&
                      shown <= window.late() / core::Interval::kMillisecond;
  return {.text = std::move(text), .tone = inside ? Tone::Good : Tone::Bad};
}

Cell DeviationStats::iqrCell() const {
  return {.text = fmt::format("{:.1f}",
                              static_cast<double>(iqr() / core::Interval::kMillisecond))};
}

double DeviationStats::score(const core::HitWindow &window) const {
  double weights = 0.0;
  double sum = 0.0;
  for (const core::Interval deviation : sorted_) {
    const double points = pointsOf((deviation - window.offset()).abs(), window.tolerance());
    const double weight = 1.0 + 2.0 * (1.0 - points);
    weights += weight;
    sum += weight * points;
  }
  return weights > 0.0 ? sum / weights * 100.0 : 0.0;
}

} // namespace training::render
