#include "training/render/common/outline.hpp"

namespace training::render {

void Outline::draw(const Frame &frame) const {
  const Box &box = frame.area;
  frame.canvas.fillBox(
      Box{.left = box.left, .top = box.top, .right = box.right, .bottom = box.top + 1.0},
      color_);
  frame.canvas.fillBox(
      Box{.left = box.left, .top = box.bottom - 1.0, .right = box.right, .bottom = box.bottom},
      color_);
  frame.canvas.fillBox(
      Box{.left = box.left, .top = box.top, .right = box.left + 1.0, .bottom = box.bottom},
      color_);
  frame.canvas.fillBox(
      Box{.left = box.right - 1.0, .top = box.top, .right = box.right, .bottom = box.bottom},
      color_);
}

} // namespace training::render
