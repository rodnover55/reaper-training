#include "training/render/calibration/note_slots.hpp"

#include "training/render/palette.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cstddef>
#include <optional>

namespace training::render {

NoteSlots::NoteSlots()
    : count_(Label::Style{.color = kNeutral, .align = TextAlign::TopLeft}) {}

void NoteSlots::setAttacks(std::span<const double> attacksDb, int count) {
  const double softest = attacksDb.empty() ? 0.0 : std::ranges::min(attacksDb);
  slots_.clear();
  for (int i = 0; i < count; ++i) {
    const auto index = static_cast<std::size_t>(i);
    if (index < attacksDb.size())
      slots_.emplace_back(attacksDb[index], attacksDb[index] == softest);
    else
      slots_.emplace_back(std::nullopt, false);
  }
  count_.setText(fmt::format("{} of {}", attacksDb.size(), count));
}

void NoteSlots::draw(const Frame &frame) const {
  const Box &box = frame.area;
  const double height = box.bottom - box.top;
  const double width = (box.right - box.left) * 0.085;
  const double gap = (box.right - box.left) * 0.012;
  for (std::size_t i = 0; i < slots_.size(); ++i) {
    const double left = box.left + static_cast<double>(i) * (width + gap);
    slots_[i].draw(frame.within(
        Box{.left = left, .top = box.top, .right = left + width, .bottom = box.bottom}));
  }
  const double size = std::max(kMinFont, height * 0.055 / 0.13);
  const double left = box.left + static_cast<double>(slots_.size()) * (width + gap);
  const double top = box.top + height / 2.0 - height * 0.03 / 0.13;
  count_.draw(
      frame.within(Box{.left = left, .top = top, .right = left + 1.0, .bottom = top + size}));
}

} // namespace training::render
