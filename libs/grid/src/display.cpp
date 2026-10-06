#include "training/grid/display.hpp"

#include "training/grid/hit_window.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string_view>

namespace training::grid {
namespace {

/// Знак «минус» для показа: U+2212, а не дефис.
constexpr std::string_view kMinus = "−";

/// Цвет показанного числа по окну попадания `window`, без `Target`.
Tone windowTone(double shown, const HitWindow &window) {
  return shown >= window.early() && shown <= window.late() ? Tone::Good : Tone::Bad;
}

/// Отклонение ноты для показа: целые миллисекунды со знаком, «*» после числа —
/// у узла была лишняя нота.
Cell noteCell(double seconds, bool extra, const HitWindow &window) {
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
  const auto number = static_cast<double>(shown);
  const Tone tone =
      std::abs(number - window.offsetMs) <= 0.5 ? Tone::Target : windowTone(number, window);
  return {.text = std::move(text), .tone = tone};
}

/// Среднее для показа: миллисекунды с одним знаком после запятой и знаком.
Cell meanCell(double seconds, const HitWindow &window) {
  const double shown = std::round(seconds * 10000.0) / 10.0;
  std::string text;
  if (shown > 0.0)
    text = fmt::format("+{:.1f}", shown);
  else if (shown < 0.0)
    text = fmt::format("{}{:.1f}", kMinus, -shown);
  else
    text = "0.0";
  return {.text = std::move(text), .tone = windowTone(shown, window)};
}

/// Размер для маркера: «7/8».
std::string meterText(const Meter &meter) {
  return fmt::format("{}/{}", meter.beats, meter.unit);
}

/// Темп для маркера: «120», дробный — «120.5», «97.25».
std::string tempoText(double bpm) {
  std::string text = fmt::format("{:.2f}", bpm);
  while (text.back() == '0')
    text.pop_back();
  if (text.back() == '.')
    text.pop_back();
  return text;
}

/// Подпись маркера смены: «4/4 → 7/8», «120 → 150» или обе через «·».
std::string changeText(const Change &change) {
  std::string text;
  if (change.meterFrom && change.meterTo)
    text = fmt::format("{} → {}", meterText(*change.meterFrom), meterText(*change.meterTo));
  if (change.tempoFrom && change.tempoTo) {
    if (!text.empty())
      text += " · ";
    text += fmt::format("{} → {}", tempoText(*change.tempoFrom), tempoText(*change.tempoTo));
  }
  return text;
}

/// Строка такта для окна; `top` — верхняя строка, у неё текущий удар и нет
/// среднего и разброса.
BarView barView(const BarRow &row, bool top, const Bars &bars) {
  BarView view;
  view.label = fmt::format("{}", row.bar.index + 1);
  view.beats = static_cast<int>(row.beats.size());

  for (std::size_t k = 0; k < row.beats.size(); ++k) {
    const RowBeat &beat = row.beats[k];
    if (beat.slots.empty() || !beat.slots.front().deviation)
      view.dots.push_back(static_cast<int>(k));

    if (beat.mode)
      view.division = std::max(view.division, divisions(*beat.mode));
    else if (top)
      view.division = std::max(view.division, divisions(bars.mode()));

    const auto count = static_cast<double>(beat.slots.size());
    for (std::size_t slot = 0; slot < beat.slots.size(); ++slot) {
      const RowSlot &node = beat.slots[slot];
      if (!node.deviation)
        continue;
      view.values.push_back(
          {.beat = static_cast<double>(k) + static_cast<double>(slot) / count,
           .onBeat = slot == 0,
           .cell = noteCell(*node.deviation, node.extra, node.window)});
    }
  }

  for (const Change &change : row.bar.changes)
    view.marks.push_back({.beat = change.beat, .text = changeText(change)});

  if (top) {
    view.currentBeat = bars.currentBeat();
  } else if (row.mean && row.spread) {
    view.mean = meanCell(
        *row.mean, row.leftWindow.value_or(HitWindow{.offsetMs = 0.0, .toleranceMs = 0.0}));
    view.spread = Cell{.text = fmt::format("{:.1f}", *row.spread * 1000.0)};
  }
  return view;
}

} // namespace

std::vector<BarView> present(const Bars &bars) {
  std::vector<BarView> views;
  for (const BarRow &row : bars.rows())
    if (row.entered && !row.folded)
      views.push_back(barView(row, views.empty(), bars));
  return views;
}

} // namespace training::grid
