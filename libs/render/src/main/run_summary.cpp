#include "training/render/main/run_summary.hpp"

#include "training/core/clock.hpp"
#include "training/render/main/deviation_stats.hpp"
#include "training/render/palette.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <ranges>
#include <utility>
#include <vector>

namespace training::render {
namespace {

/// Поля места вокруг плиток и промежуток между плитками, пикселей.
constexpr double kPadLeft = 6.0;
constexpr double kPadRight = 8.0;
constexpr double kPadY = 6.0;
constexpr double kGap = 6.0;

/// Фон плитки и скругление её углов — доля высоты плитки.
constexpr Color kTile{.red = 34, .green = 34, .blue = 34};
constexpr double kTileRoundness = 0.04;

/// Подпись плитки.
constexpr Label::Style kCaptionStyle{.color = kNeutral, .bold = false};

/// Число плитки.
constexpr Label::Style kValueStyle{.color = kNeutral, .bold = true};

} // namespace

RunSummary::RunSummary()
    : background_(kBackground), tile_(kTile, kTileRoundness),
      captions_{Label(kCaptionStyle, "MED"), Label(kCaptionStyle, "IQR"),
                Label(kCaptionStyle, "SCORE")},
      values_{Label(kValueStyle), Label(kValueStyle), Label(kValueStyle)} {}

double RunSummary::minWidth() {
  return kPadLeft + kPadRight + 3.0 * kMinTileWidth + 2.0 * kGap;
}

void RunSummary::setRun(const core::training::state::Timeline &timeline,
                        const core::HitWindow &window, bool playing, std::size_t end) {
  const std::size_t limit = playing ? kSummaryNotes : std::numeric_limits<std::size_t>::max();
  // Последние ноты — с конца: такты от последнего к первому после предтакта,
  // доли в такте — от последней.
  std::vector<core::Interval> deviations;
  const auto &bars = timeline.bars();
  for (std::size_t index = std::min(end, bars.size()); index > 1 && deviations.size() < limit;
       --index) {
    for (const auto &notes :
         bars[index - 1].notes() | std::views::values | std::views::reverse) {
      if (notes.empty())
        continue;
      deviations.push_back(DeviationStats::closest(notes).deviation);
      if (deviations.size() == limit)
        break;
    }
  }

  if (deviations.empty()) {
    view_.reset();
    return;
  }

  const DeviationStats stats(std::move(deviations));
  RunSummaryView view{.notes = stats.count(),
                      .whole = !playing,
                      .warmUp = playing && stats.count() < kSummaryNotes,
                      .median = stats.medianCell(window),
                      .iqr = stats.iqrCell(),
                      .score = {.text = fmt::format("{}", std::lround(stats.score(window)))}};
  if (view.warmUp)
    view.median.tone = Tone::Neutral;
  view_ = std::move(view);

  const Color caption = view_->warmUp ? kDim : kNeutral;
  for (Label &label : captions_)
    label.setColor(caption);
  const std::array<const Cell *, 3> cells{&view_->median, &view_->iqr, &view_->score};
  for (std::size_t i = 0; i < values_.size(); ++i) {
    values_[i].setText(cells[i]->text);
    values_[i].setColor(view_->warmUp ? kDim : toneColor(cells[i]->tone));
  }
}

void RunSummary::draw(const Frame &frame) const {
  background_.draw(frame);
  const Box &area = frame.area;
  const double width = (area.right - area.left - kPadLeft - kPadRight - 2.0 * kGap) / 3.0;
  const double top = area.top + kPadY;
  const double height = area.bottom - area.top - 2.0 * kPadY;
  if (width <= 0.0 || height <= 0.0)
    return;

  const double captionSize = std::clamp(height * 0.15, 11.0, 16.0);
  const double valueSize = std::clamp(std::min(height * 0.52, width * 0.26), 14.0, 64.0);
  const double gap = height * 0.04;

  for (std::size_t i = 0; i < values_.size(); ++i) {
    const double left = area.left + kPadLeft + static_cast<double>(i) * (width + kGap);
    const double right = left + width;
    tile_.draw(
        frame.within(Box{.left = left, .top = top, .right = right, .bottom = top + height}));
    if (!view_)
      continue;

    // Подпись и число — столбиком по центру плитки.
    const double total = captionSize + gap + valueSize;
    double y = top + (height - total) / 2.0;
    captions_[i].draw(
        frame.within(Box{.left = left, .top = y, .right = right, .bottom = y + captionSize}));
    y += captionSize + gap;
    values_[i].draw(
        frame.within(Box{.left = left, .top = y, .right = right, .bottom = y + valueSize}));
  }
}

} // namespace training::render
