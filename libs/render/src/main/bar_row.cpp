#include "training/render/main/bar_row.hpp"

#include "training/core/clock.hpp"
#include "training/core/hit_window.hpp"
#include "training/core/training/state/note.hpp"
#include "training/render/main/deviation_stats.hpp"
#include "training/render/palette.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <string_view>
#include <utility>
#include <vector>

namespace training::render {
namespace {

/// Знак «минус» для показа: U+2212, а не дефис.
constexpr std::string_view kMinus = "−";

/// Разница темпов, четвертей в минуту, до которой темп считается тем же.
constexpr double kSameTempo = 1e-6;

/// Цвет ноты с оценкой `hit`.
Tone hitTone(core::Hit hit) {
  switch (hit) {
  case core::Hit::Target:
    return Tone::Target;
  case core::Hit::Good:
    return Tone::Good;
  case core::Hit::Bad:
    return Tone::Bad;
  }
  return Tone::Bad;
}

/// Нота `note` для показа: целые миллисекунды со знаком, «*» после числа — в
/// доле была лишняя нота; цвет — по оценке ноты.
Cell noteCell(const core::training::state::Note &note, bool extra) {
  const long shown = std::lround(note.deviation / core::Interval::kMillisecond);
  std::string text;
  if (shown > 0)
    text = fmt::format("+{}", shown);
  else if (shown < 0)
    text = fmt::format("{}{}", kMinus, -shown);
  else
    text = "0";

  if (extra)
    text += "*";
  return {.text = std::move(text), .tone = hitTone(note.hit)};
}

/// Размер для маркера: «7/8».
std::string meterText(const core::training::state::Meter &meter) {
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

/// Маркеры смен такта `bar`, который идёт после такта `previous`, по порядку
/// мест: в начале такта — смена размера и темпа против прошлого такта, внутри
/// — смены темпа между соседними долями темпа такта. Подпись — «4/4 → 7/8»,
/// «120 → 150» или обе через «·».
std::vector<MarkView> marksOf(const core::training::state::Bar &bar,
                              const core::training::state::Bar &previous) {
  std::string meterChange;
  if (previous.meter() != bar.meter())
    meterChange = fmt::format("{} → {}", meterText(previous.meter()), meterText(bar.meter()));

  std::vector<MarkView> marks;
  double tempo = previous.tempo().rbegin()->second;
  for (const auto &[beat, next] : bar.tempo()) {
    // Доля 0 — первая в темпе такта: смена размера стоит на ней.
    std::string text = beat == bar.tempo().begin()->first ? meterChange : std::string();
    if (std::abs(next - tempo) > kSameTempo) {
      if (!text.empty())
        text += " · ";
      text += fmt::format("{} → {}", tempoText(tempo), tempoText(next));
    }
    tempo = next;
    if (!text.empty())
      marks.push_back({.beat = beat.beats(), .text = std::move(text)});
  }
  return marks;
}

/// Что показывает строка такта `bar`, который идёт после такта `previous`;
/// `top` — верхняя строка, у неё нет медианы и IQR, `currentBeat` — удар, на
/// котором сейчас воспроизведение, `window` — текущее окно попадания для
/// медианы.
BarView viewOf(const core::training::state::Bar &bar,
               const core::training::state::Bar &previous, bool top,
               std::optional<int> currentBeat, const core::HitWindow &window) {
  BarView view;
  view.label = bar.name();
  view.leadIn = bar.name().starts_with(kMinus);
  view.beats = std::max(bar.meter().beats, 1);
  view.currentBeat = currentBeat;

  for (int beat = 0; beat < view.beats; ++beat)
    if (!bar.notes().contains(core::training::state::Beat(static_cast<double>(beat))))
      view.dots.push_back(beat);

  std::map<int, int> perBeat;
  for (const auto &[division, at] : bar.divisions())
    ++perBeat[static_cast<int>(std::floor(division.beats()))];
  for (const auto &[beat, count] : perBeat)
    view.division = std::max(view.division, count);

  // Медиана и IQR — по одному значению на долю.
  std::vector<core::Interval> values;
  for (const auto &[division, notes] : bar.notes()) {
    if (notes.empty())
      continue;
    const core::training::state::Note &note = DeviationStats::closest(notes);
    values.push_back(note.deviation);
    view.values.push_back({.beat = division.beats(),
                           .onBeat = division.beats() == std::floor(division.beats()),
                           .cell = noteCell(note, notes.size() > 1)});
  }

  view.marks = marksOf(bar, previous);

  if (!top && values.size() > 1) {
    const DeviationStats stats(std::move(values));
    view.median = stats.medianCell(window);
    view.iqr = stats.iqrCell();
  }
  return view;
}

/// Место текста кегля `size` с центром в (`x`, `y`) и полушириной
/// `halfWidth`: вылезать за него по сторонам тексту можно.
Box centered(double x, double y, double halfWidth, double size) {
  return {.left = x - halfWidth,
          .top = y - size / 2.0,
          .right = x + halfWidth,
          .bottom = y + size / 2.0};
}

} // namespace

Layout layoutOf(const Box &row, bool barNumbers, bool barStats) {
  const double width = row.right - row.left;
  Layout layout{.left = row.left, .width = width, .rowHeight = row.bottom - row.top};
  // Колонка номера — под пять цифр, до такта 10000; колонка медианы и IQR —
  // около 9 % ширины.
  layout.sideSize = std::max(kMinFont, layout.rowHeight * 0.34);
  layout.labelWidth = barNumbers ? layout.sideSize * 0.6 * 5.0 + 10.0 : 0.0;
  layout.statsWidth = barStats ? width * 0.09 : 0.0;
  layout.statsSize = std::max(kMinFont, std::min(layout.sideSize, layout.statsWidth / 6.0));
  layout.scaleWidth = width - layout.labelWidth - layout.statsWidth;
  layout.markSize = std::max(kMinFont, layout.rowHeight * 0.2);
  return layout;
}

BarRow::BarRow()
    : leadInRow_(kLeadInRow), leadInLabel_(kLeadInLabel), currentRow_(kCurrentRow),
      currentBeat_(kCurrentBeatBack), number_(Label::Style{.color = kNeutral}),
      median_(Label::Style{.color = kNeutral}), iqr_(Label::Style{.color = kNeutral}) {}

void BarRow::setColumns(bool barNumbers, bool barStats) {
  barNumbers_ = barNumbers;
  barStats_ = barStats;
}

void BarRow::setRow(const core::training::state::Bar &bar,
                    const core::training::state::Bar &previous, bool top,
                    std::optional<int> currentBeat, const core::HitWindow &window) {
  view_ = viewOf(bar, previous, top, currentBeat, window);

  number_.setText(view_.label);
  marks_.clear();
  for (const MarkView &mark : view_.marks)
    marks_.emplace_back(mark.text);
  dots_.clear();
  for (const int beat : view_.dots)
    dots_.emplace_back(view_.currentBeat == beat ? kCurrentBeat : kNeutral);
  values_.clear();
  for (const ValueView &value : view_.values)
    values_.emplace_back(value.cell);
  if (view_.median && view_.iqr) {
    median_.setText(view_.median->text);
    median_.setColor(toneColor(view_.median->tone));
    iqr_.setText(view_.iqr->text);
    iqr_.setColor(toneColor(view_.iqr->tone));
  }
}

void BarRow::draw(const Frame &frame) const {
  const Layout layout = layoutOf(frame.area, barNumbers_, barStats_);
  const double top = frame.area.top;
  const double bottom = frame.area.bottom;
  const double middle = top + layout.rowHeight / 2.0;
  const double beats = std::max(view_.beats, 1);
  const double division = std::max(view_.division, 1);

  // Крупное число — по высоте строки, ширине удара и расстоянию между узлами
  // самого мелкого режима такта; мелкое — 0.6 крупного.
  const auto bigFor = [&](double step) {
    return std::max(kMinFont,
                    std::min({layout.rowHeight * 0.62, step / 2.3, step / division / 1.55}));
  };
  // Перед первым ударом — место под половину крупного числа.
  const double pad = bigFor(layout.scaleWidth / beats) * 0.95 + 6.0;
  const double step = (layout.scaleWidth - pad) / beats;
  const double bigSize = bigFor(step);
  const double smallSize =
      std::max(kMinFont - 1.0, std::min(bigSize * 0.6, step / division / 2.0));
  const double scaleLeft = layout.left + layout.labelWidth;
  const auto x = [&](double beat) { return scaleLeft + pad + step * beat; };
  const Box whole{
      .left = layout.left, .top = top, .right = layout.left + layout.width, .bottom = bottom};

  if (view_.leadIn) {
    leadInRow_.draw(frame.within(whole));
    if (layout.labelWidth > 0.0)
      leadInLabel_.draw(frame.within(Box{.left = layout.left,
                                         .top = top,
                                         .right = layout.left + layout.labelWidth,
                                         .bottom = bottom}));
  }

  if (view_.currentBeat) {
    if (!view_.leadIn)
      currentRow_.draw(frame.within(whole));
    const double beat = *view_.currentBeat;
    currentBeat_.draw(
        frame.within(Box{.left = std::max(scaleLeft + 1.0, x(beat) - step * 0.125),
                         .top = top,
                         .right = x(beat) + step * 0.875,
                         .bottom = bottom}));
  }

  // Маркер смены — флажок над строкой чисел: древко над ударом смены,
  // подпись справа от него.
  for (std::size_t i = 0; i < marks_.size(); ++i) {
    const double at = x(view_.marks[i].beat);
    marks_[i].draw(frame.within(Box{.left = at - 1.0,
                                    .top = top + 2.0,
                                    .right = at + 4.0 + layout.scaleWidth,
                                    .bottom = top + 4.0 + layout.markSize}));
  }

  if (layout.labelWidth > 0.0)
    number_.draw(frame.within(centered(layout.left + layout.labelWidth / 2.0, middle,
                                       layout.labelWidth / 2.0, layout.sideSize)));

  for (std::size_t i = 0; i < dots_.size(); ++i) {
    const int beat = view_.dots[i];
    const bool current = view_.currentBeat == beat;
    const double radius = std::max(4.0, bigSize * (current ? 0.3 : 0.2)) / 2.0;
    dots_[i].draw(frame.within(Box{.left = x(beat) - radius,
                                   .top = middle - radius,
                                   .right = x(beat) + radius,
                                   .bottom = middle + radius}));
  }

  // Число шире места перед первым ударом сдвигается вправо и не заходит на
  // колонку номера.
  for (std::size_t i = 0; i < values_.size(); ++i) {
    const ValueView &value = view_.values[i];
    const double size = value.onBeat ? bigSize : smallSize;
    const double at =
        std::max(x(value.beat), scaleLeft + 3.0 + values_[i].width(frame.canvas, size) / 2.0);
    values_[i].draw(frame.within(centered(at, middle, step, size)));
  }

  if (layout.statsWidth > 0.0 && view_.median && view_.iqr) {
    const double right = layout.left + layout.width;
    median_.draw(frame.within(centered(right - layout.statsWidth * 0.72, middle,
                                       layout.statsWidth * 0.28, layout.statsSize)));
    iqr_.draw(frame.within(centered(right - layout.statsWidth * 0.25, middle,
                                    layout.statsWidth * 0.25, layout.statsSize)));
  }
}

} // namespace training::render
