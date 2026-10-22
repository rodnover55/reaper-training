#include "training/render/main/field_frame.hpp"

namespace training::render {

FieldFrame::FieldFrame(Color color) : ring_(color) {}

void FieldFrame::setColor(Color color) { ring_.setColor(color); }

void FieldFrame::draw(const Frame &frame) const {
  const Box &box = frame.area;
  for (int ring = 0; ring < kRejectionFrame; ++ring)
    ring_.draw(frame.within(Box{.left = box.left + ring,
                                .top = box.top + ring,
                                .right = box.right - ring,
                                .bottom = box.bottom - ring}));
}

} // namespace training::render
