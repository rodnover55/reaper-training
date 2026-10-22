#include "training/render/main/bar_rows.hpp"

#include "training/render/palette.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace training::render {
namespace {

/// Удар такта `bar` на слышимой позиции `heard`, с 0: удар последней доли не
/// позже неё; до первой доли — удар перед ней.
int beatAt(const core::training::state::Bar &bar, core::Moment heard) {
  std::optional<double> place;
  for (const auto &[division, at] : bar.divisions()) {
    if (at > heard)
      break;
    place = division.beats();
  }
  if (place)
    return static_cast<int>(std::floor(*place));
  if (bar.divisions().empty())
    return 0;
  return std::max(static_cast<int>(std::ceil(bar.divisions().begin()->first.beats())) - 1, 0);
}

} // namespace

BarRows::BarRows() : background_(kBackground), line_(kLine) {}

void BarRows::setColumns(bool barNumbers, bool barStats) {
  barNumbers_ = barNumbers;
  barStats_ = barStats;
}

void BarRows::setBars(const core::training::state::Timeline &timeline,
                      std::optional<core::Moment> heard, bool playing,
                      const core::HitWindow &window) {
  rows_.clear();
  if (!heard)
    return;

  // Верхний такт — последний пришедший такт со `start` не позже слышимой
  // позиции; строки — от него к первому после предтакта.
  const auto &bars = timeline.bars();
  std::size_t top = bars.size();
  while (top > 0 && bars[top - 1].start() > *heard)
    --top;
  // Такт без нот, ни одна доля которого не действует, целиком заменили
  // пришедшие после него такты — после петли, перемотки или смены скорости:
  // строки у него нет.
  const auto replaced = [&](std::size_t index) {
    const core::training::state::Bar &bar = bars[index];
    return bar.notes().empty() &&
           std::ranges::none_of(bar.divisions(), [&](const auto &division) {
             return timeline.time({.bar = index, .division = division.first}).has_value();
           });
  };
  for (std::size_t index = top; index > 1 && rows_.size() < static_cast<std::size_t>(kRows);
       --index) {
    const core::training::state::Bar &bar = bars[index - 1];
    const core::training::state::Bar &previous = bars[index - 2];
    const bool isTop = index == top;
    if (!isTop && replaced(index - 1))
      continue;
    if (!isTop && bar.notes().empty() && index > 2 && previous.notes().empty())
      continue;

    BarRow &shown = rows_.emplace_back();
    shown.setColumns(barNumbers_, barStats_);
    shown.setRow(bar, previous, isTop,
                 isTop && playing ? std::optional(beatAt(bar, *heard)) : std::nullopt, window);
  }
}

void BarRows::draw(const Frame &frame) const {
  const Box &area = frame.area;
  background_.draw(frame);

  const double rowHeight = (area.bottom - area.top) / kRows;
  for (std::size_t r = 0; r < rows_.size() && r < static_cast<std::size_t>(kRows); ++r) {
    const double top = area.top + static_cast<double>(r) * rowHeight;
    rows_[r].draw(frame.within(
        Box{.left = area.left, .top = top, .right = area.right, .bottom = top + rowHeight}));
  }

  const Layout layout = layoutOf(Box{.left = area.left,
                                     .top = area.top,
                                     .right = area.right,
                                     .bottom = area.top + rowHeight},
                                 barNumbers_, barStats_);
  const double scaleLeft = layout.left + layout.labelWidth;
  if (layout.labelWidth > 0.0)
    line_.draw(frame.within(Box{
        .left = scaleLeft, .top = area.top, .right = scaleLeft + 1.0, .bottom = area.bottom}));
  if (layout.statsWidth > 0.0)
    line_.draw(frame.within(Box{.left = scaleLeft + layout.scaleWidth,
                                .top = area.top,
                                .right = scaleLeft + layout.scaleWidth + 1.0,
                                .bottom = area.bottom}));
}

} // namespace training::render
