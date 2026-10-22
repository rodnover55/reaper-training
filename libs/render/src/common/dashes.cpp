#include "training/render/common/dashes.hpp"

#include <algorithm>

namespace training::render {

void Dashes::draw(const Frame &frame) const {
  const Box &box = frame.area;
  for (int dash = 0; box.top + period_ * dash < box.bottom; ++dash) {
    const double top = box.top + period_ * dash;
    frame.canvas.fillBox(Box{.left = box.left,
                             .top = top,
                             .right = box.right,
                             .bottom = std::min(top + dash_, box.bottom)},
                         color_);
  }
}

} // namespace training::render
