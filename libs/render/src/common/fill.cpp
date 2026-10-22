#include "training/render/common/fill.hpp"

namespace training::render {

void Fill::draw(const Frame &frame) const { frame.canvas.fillBox(frame.area, color_); }

} // namespace training::render
