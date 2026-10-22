#include "training/render/calibration/level_scale.hpp"

#include "training/render/calibration/db_text.hpp"
#include "training/render/palette.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

namespace training::render {
namespace {

/// Стиль подписи шкалы цвета `color`: не жирный текст от левого верхнего угла.
constexpr Label::Style labelStyle(Color color) {
  return {.color = color, .bold = false, .align = TextAlign::TopLeft};
}

/// Подпись отметки в ряду подписей шкалы.
struct RowLabel {
  /// Подпись.
  const Label *text = nullptr;

  /// Где середина подписи стояла бы над своей отметкой, пикселей.
  double at = 0.0;

  /// Ширина подписи, пикселей.
  double width = 0.0;

  /// Левый край подписи, который ей дал ряд, пикселей.
  double left = 0.0;
};

/// Ставит подписи одного ряда от `from` до `to` по середине над отметкой так,
/// чтобы соседние не налезали друг на друга: между ними не меньше `gap`.
/// Подпись, которой не хватило места у отметки, сдвигается от соседней; ряд,
/// вылезший за `to`, сдвигается влево. Если ряду не хватает всей ширины, левые
/// подписи выходят за `from`.
void placeRow(std::vector<RowLabel> &row, double from, double to, double gap) {
  std::ranges::sort(row, {}, &RowLabel::at);
  for (std::size_t i = 0; i < row.size(); ++i) {
    row[i].left = std::max(from, std::min(row[i].at - row[i].width / 2.0, to - row[i].width));
    if (i > 0)
      row[i].left = std::max(row[i].left, row[i - 1].left + row[i - 1].width + gap);
  }
  for (std::size_t i = row.size(); i-- > 0;) {
    const double limit = i + 1 < row.size() ? row[i + 1].left - gap : to;
    row[i].left = std::min(row[i].left, limit - row[i].width);
  }
}

} // namespace

LevelScale::LevelScale()
    : track_(kTrack), noise_(kNoise), stripe_(kNoiseStripe), level_(kLive), band_(kBand),
      frame_(kLine), attack_(kGood), oldThreshold_(kDim, 4.0, 8.0),
      newThreshold_(kCurrentBeat), peak_(kBright), oldLabel_(labelStyle(kDim)),
      newLabel_(labelStyle(kCurrentBeat)), noiseLabel_(labelStyle(kNeutral)),
      pickLabel_(labelStyle(kGood)), peakLabel_(labelStyle(kBright)) {
  for (int db = -100; db <= 0; db += 10)
    divisions_.emplace_back(labelStyle(kDim), dbText(db));
}

void LevelScale::setMarks(ScaleMarks marks) {
  marks_ = std::move(marks);
  if (marks_.oldThreshold)
    oldLabel_.setText(fmt::format("{} {}", marks_.newThreshold ? "was" : "Silence",
                                  dbText(*marks_.oldThreshold)));
  if (marks_.newThreshold)
    newLabel_.setText(fmt::format("Silence {}", dbText(*marks_.newThreshold)));
  if (marks_.noise)
    noiseLabel_.setText(fmt::format("noise {}", dbText(*marks_.noise)));
  if (!marks_.attacks.empty()) {
    const auto [softest, loudest] = std::ranges::minmax(marks_.attacks);
    pickLabel_.setText(fmt::format("pick {}…{}", dbText(softest), dbText(loudest)));
  }
  if (marks_.peak)
    peakLabel_.setText(fmt::format("peak {}", dbText(*marks_.peak, 1)));
}

void LevelScale::draw(const Frame &frame) const {
  const double left = frame.area.left;
  const double right = frame.area.right;
  const double top = frame.area.top;
  const double height = frame.area.bottom - frame.area.top;
  const auto x = [&](double db) {
    return left + (std::clamp(db, -100.0, 0.0) + 100.0) / 100.0 * (right - left);
  };
  const double trackTop = top + height * 0.42;
  const double trackBottom = top + height * 0.60;
  const double tickTop = top + height * 0.34;
  const double tickBottom = top + height * 0.68;
  const auto span = [&](double from, double to, double spanTop, double spanBottom) {
    return frame.within(Box{.left = from, .top = spanTop, .right = to, .bottom = spanBottom});
  };

  track_.draw(span(left, right, trackTop, trackBottom));
  if (marks_.noise) {
    const double edge = x(*marks_.noise + 3.0);
    noise_.draw(span(left, edge, trackTop, trackBottom));
    // Штриховка: полоска в 2 пикселя через каждые 6.
    for (int stripe = 0; left + 6.0 * stripe + 5.0 < edge; ++stripe)
      stripe_.draw(
          span(left + 6.0 * stripe + 3.0, left + 6.0 * stripe + 5.0, trackTop, trackBottom));
  }
  if (marks_.level)
    level_.draw(span(left, x(*marks_.level), trackTop + 2.0, trackBottom - 2.0));
  if (!marks_.attacks.empty() && marks_.peak) {
    const auto [softest, loudest] = std::ranges::minmax(marks_.attacks);
    band_.draw(span(x(softest), x(loudest), trackTop, trackBottom));
  }
  frame_.draw(span(left, right, trackTop, trackBottom));

  for (const double attack : marks_.attacks)
    attack_.draw(span(x(attack) - 1.0, x(attack) + 1.0, tickTop, tickBottom));

  const double labelSize = std::max(kMinFont, height * 0.15);
  // Подпись по центру `at`, не вылезая за края шкалы.
  const auto label = [&](const Label &text, double at, double y, double size) {
    const double width = text.width(frame.canvas, size);
    const double from = std::clamp(at - width / 2.0, left, right - width);
    text.draw(
        frame.within(Box{.left = from, .top = y, .right = from + width, .bottom = y + size}));
  };
  // Подписи ряда над отметками и ряда под ними не налезают друг на друга.
  const auto row = [&](std::vector<RowLabel> labels, double y) {
    placeRow(labels, left, right, labelSize * 0.5);
    for (const RowLabel &placed : labels)
      placed.text->draw(frame.within(Box{.left = placed.left,
                                         .top = y,
                                         .right = placed.left + placed.width,
                                         .bottom = y + labelSize}));
  };
  const auto rowLabel = [&](const Label &text, double middle) {
    return RowLabel{.text = &text, .at = middle, .width = text.width(frame.canvas, labelSize)};
  };
  std::vector<RowLabel> above;
  std::vector<RowLabel> below;

  if (marks_.oldThreshold) {
    const double mark = x(*marks_.oldThreshold);
    // Пунктир: штрих в 4 пикселя через каждые 8.
    oldThreshold_.draw(
        span(mark - 1.0, mark + 1.0, tickTop - height * 0.04, tickBottom + height * 0.04));
    above.push_back(rowLabel(oldLabel_, mark));
  }
  if (marks_.newThreshold) {
    const double mark = x(*marks_.newThreshold);
    newThreshold_.draw(
        span(mark - 1.0, mark + 1.0, tickTop - height * 0.04, tickBottom + height * 0.04));
    above.push_back(rowLabel(newLabel_, mark));
  }
  if (marks_.noise)
    below.push_back(rowLabel(noiseLabel_, (left + x(*marks_.noise)) / 2.0));
  if (!marks_.attacks.empty() && marks_.peak) {
    const auto [softest, loudest] = std::ranges::minmax(marks_.attacks);
    below.push_back(rowLabel(pickLabel_, (x(softest) + x(loudest)) / 2.0));
  }
  if (marks_.peak) {
    peak_.draw(span(x(*marks_.peak) - 1.0, x(*marks_.peak) + 1.0, tickTop, tickBottom));
    below.push_back(rowLabel(peakLabel_, x(*marks_.peak)));
  }
  row(std::move(above), top);
  row(std::move(below), tickBottom + 2.0);

  const double divisionSize = std::max(kMinFont, height * 0.125);
  for (std::size_t i = 0; i < divisions_.size(); ++i)
    label(divisions_[i], x(-100.0 + 10.0 * static_cast<double>(i)),
          top + height - divisionSize * 1.2, divisionSize);
}

} // namespace training::render
