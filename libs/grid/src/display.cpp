#include "training/grid/display.hpp"

#include <fmt/format.h>

#include <cmath>
#include <optional>
#include <string_view>

namespace training::grid {
namespace {

/// Знак «минус» для показа: U+2212, а не дефис.
constexpr std::string_view kMinus = "−";

/// Цвет показанного значения по допуску.
Tone toneOf(double shown, double toleranceMs) {
  return std::abs(shown) <= toleranceMs ? Tone::Good : Tone::Bad;
}

/// Отклонение ноты для показа: целые миллисекунды со знаком, «*» после числа —
/// у узла была лишняя нота.
Cell noteCell(double seconds, bool extra, double toleranceMs) {
  const long shown = std::lround(seconds * 1000.0);
  std::string text;
  if (shown > 0)
    text = fmt::format("+{}", shown);
  else if (shown < 0)
    text = fmt::format("{}{}", kMinus, -shown);
  else
    text = "0";

  if (extra)
    text += "*";
  return {.text = std::move(text), .tone = toneOf(static_cast<double>(shown), toleranceMs)};
}

/// Среднее для показа: миллисекунды с одним знаком после запятой и знаком.
Cell meanCell(double seconds, double toleranceMs) {
  const double shown = std::round(seconds * 10000.0) / 10.0;
  std::string text;
  if (shown > 0.0)
    text = fmt::format("+{:.1f}", shown);
  else if (shown < 0.0)
    text = fmt::format("{}{:.1f}", kMinus, -shown);
  else
    text = "0.0";
  return {.text = std::move(text), .tone = toneOf(shown, toleranceMs)};
}

} // namespace

std::vector<RowView> present(const std::deque<BeatRow> &rows, double toleranceMs) {
  std::vector<RowView> views;
  views.reserve(rows.size());

  for (const BeatRow &row : rows) {
    RowView view;
    view.label = fmt::format("{}.{}", row.label.bar, row.label.beat);
    view.cells.reserve(row.slots.size());

    for (const Slot &slot : row.slots) {
      if (slot.deviation)
        view.cells.push_back(noteCell(*slot.deviation, slot.extra, toleranceMs));
      else if (slot.missing)
        view.cells.push_back({.text = ".", .tone = Tone::Neutral});
      else
        view.cells.push_back({});
    }

    const std::optional<double> mean = row.mean;
    const std::optional<double> spread = row.spread;
    if (mean && spread) {
      view.mean = meanCell(*mean, toleranceMs);
      view.spread = Cell{.text = fmt::format("{:.1f}", *spread * 1000.0)};
    }

    views.push_back(std::move(view));
  }

  return views;
}

} // namespace training::grid
