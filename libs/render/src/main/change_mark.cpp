#include "training/render/main/change_mark.hpp"

#include "training/render/palette.hpp"

#include <utility>

namespace training::render {

ChangeMark::ChangeMark(std::string text)
    : pole_(kChange),
      caption_(Label::Style{.color = kChange, .align = TextAlign::TopLeft}, std::move(text)) {}

void ChangeMark::draw(const Frame &frame) const {
  const Box &box = frame.area;
  pole_.draw(frame.within(
      Box{.left = box.left, .top = box.top, .right = box.left + 2.0, .bottom = box.bottom}));
  caption_.draw(frame.within(Box{.left = box.left + 5.0,
                                 .top = box.top,
                                 .right = box.right,
                                 .bottom = box.bottom - 2.0}));
}

} // namespace training::render
