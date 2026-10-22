#include "training/render/main/gauge_layout.hpp"

#include "training/render/palette.hpp"

namespace training::render {

GaugeLayout gaugeLayout(const Box &box, core::Interval scale) {
  const double height = box.bottom - box.top;
  GaugeLayout layout{.top = box.top + height * 0.16,
                     .bottom = box.top + height * 0.56,
                     .reach = height * 0.1,
                     .labelSize = std::max(kMinFont, height * 0.28),
                     .scale = std::max(scale, core::Interval::kMillisecond)};
  const double margin = layout.labelSize * 1.4;
  layout.left = box.left + margin;
  layout.right = std::max(layout.left + 1.0, box.right - margin);
  layout.labelMiddle = (layout.bottom + layout.reach + box.bottom) / 2.0;
  return layout;
}

} // namespace training::render
