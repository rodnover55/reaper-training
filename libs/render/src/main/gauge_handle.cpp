#include "training/render/main/gauge_handle.hpp"

#include "training/render/palette.hpp"

namespace training::render {

GaugeHandle::GaugeHandle() : body_(kHandle), frame_(kHandleFrame), grip_(kHandleGrip) {}

void GaugeHandle::draw(const Frame &frame) const {
  body_.draw(frame);
  frame_.draw(frame);
  const double center = (frame.area.left + frame.area.right) / 2.0;
  const double middle = (frame.area.top + frame.area.bottom) / 2.0;
  grip_.draw(frame.within(Box{.left = center - 2.0,
                              .top = middle - 2.0,
                              .right = center + 2.0,
                              .bottom = middle - 1.0}));
  grip_.draw(frame.within(Box{.left = center - 2.0,
                              .top = middle + 1.0,
                              .right = center + 2.0,
                              .bottom = middle + 2.0}));
}

} // namespace training::render
