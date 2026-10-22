#include "training/render/calibration/steps_row.hpp"

#include "training/render/palette.hpp"

#include <cstddef>

namespace training::render {
namespace {

/// Стиль имени шага: жирный текст от левого верхнего угла.
constexpr Label::Style kNameStyle{.color = kDim, .align = TextAlign::TopLeft};

} // namespace

StepsRow::StepsRow()
    : names_{Label(kNameStyle, "1. Silence"), Label(kNameStyle, "2. Notes"),
             Label(kNameStyle, "Result")},
      dash_(kLine) {
  setCurrent(1);
}

void StepsRow::setCurrent(int current) {
  for (std::size_t i = 0; i < names_.size(); ++i) {
    const int index = static_cast<int>(i) + 1;
    if (index < current)
      names_[i].setColor(kGood);
    else
      names_[i].setColor(index == current ? kCurrentBeat : kDim);
  }
}

void StepsRow::draw(const Frame &frame) const {
  const Box &box = frame.area;
  const double size = box.bottom - box.top;
  const double dash = (box.right - box.left) * 0.03;
  double x = box.left;
  for (std::size_t i = 0; i < names_.size(); ++i) {
    const double width = names_[i].width(frame.canvas, size);
    names_[i].draw(frame.within(
        Box{.left = x, .top = box.top, .right = x + width, .bottom = box.top + size}));
    x += width + size * 0.6;
    if (i + 1 < names_.size()) {
      const double y = box.top + size * 0.6;
      dash_.draw(frame.within(Box{.left = x, .top = y, .right = x + dash, .bottom = y + 1.0}));
      x += dash + size * 0.6;
    }
  }
}

} // namespace training::render
