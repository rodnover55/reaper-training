#include "training/render/main/node_value.hpp"

#include "training/render/palette.hpp"

namespace training::render {
namespace {

/// Половина высоты подложки в кеглях.
constexpr double kPlateHalfHeight = 0.58;

} // namespace

Color toneColor(Tone tone) {
  switch (tone) {
  case Tone::Good:
  case Tone::Target:
    return kGood;
  case Tone::Bad:
    return kBad;
  case Tone::Neutral:
    break;
  }
  return kNeutral;
}

NodeValue::NodeValue(const Cell &cell)
    : plate_(toneColor(Tone::Target), 0.18 / (2.0 * kPlateHalfHeight)),
      number_(Label::Style{.color = kNeutral}) {
  setCell(cell);
}

void NodeValue::setCell(const Cell &cell) {
  cell_ = cell;
  number_.setText(cell.text);
  number_.setColor(cell.tone == Tone::Target ? kPlateText : toneColor(cell.tone));
}

double NodeValue::width(Canvas &canvas, double size) const {
  const double number = number_.width(canvas, size);
  return cell_.tone == Tone::Target ? number + size * 0.4 : number;
}

void NodeValue::draw(const Frame &frame) const {
  const Box &box = frame.area;
  if (cell_.tone == Tone::Target) {
    const double size = box.bottom - box.top;
    const double center = (box.left + box.right) / 2.0;
    const double middle = (box.top + box.bottom) / 2.0;
    const double halfWidth = width(frame.canvas, size) / 2.0;
    plate_.draw(frame.within(Box{.left = center - halfWidth,
                                 .top = middle - size * kPlateHalfHeight,
                                 .right = center + halfWidth,
                                 .bottom = middle + size * kPlateHalfHeight}));
  }
  number_.draw(frame);
}

} // namespace training::render
