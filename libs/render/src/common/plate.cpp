#include "training/render/common/plate.hpp"

namespace training::render {

void Plate::draw(const Frame &frame) const {
  frame.canvas.fillRounded(frame.area, (frame.area.bottom - frame.area.top) * roundness_,
                           color_);
}

} // namespace training::render
