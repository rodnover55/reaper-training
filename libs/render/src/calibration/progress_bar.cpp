#include "training/render/calibration/progress_bar.hpp"

#include "training/render/palette.hpp"

namespace training::render {

ProgressBar::ProgressBar() : track_(kLine), passed_(kCurrentBeat) {}

void ProgressBar::draw(const Frame &frame) const {
  const Box &box = frame.area;
  track_.draw(frame);
  passed_.draw(frame.within(Box{.left = box.left,
                                .top = box.top,
                                .right = box.left + (box.right - box.left) * done_,
                                .bottom = box.bottom}));
}

} // namespace training::render
