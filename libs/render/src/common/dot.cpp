#include "training/render/common/dot.hpp"

namespace training::render {

void Dot::draw(const Frame &frame) const { frame.canvas.fillEllipse(frame.area, color_); }

} // namespace training::render
