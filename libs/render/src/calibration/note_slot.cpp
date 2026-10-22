#include "training/render/calibration/note_slot.hpp"

#include "training/render/calibration/db_text.hpp"
#include "training/render/palette.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace training::render {
namespace {

/// Цвет рамки ячейки: у пустой — цвет линий, у самого тихого щелчка —
/// зелёный, у остальных — рамка ячейки.
Color frameColor(bool empty, bool softest) {
  if (empty)
    return kLine;
  return softest ? kGood : kSlotBorder;
}

} // namespace

NoteSlot::NoteSlot(std::optional<double> attackDb, bool softest)
    : empty_(!attackDb), frame_(frameColor(!attackDb, softest)),
      level_(Label::Style{.color = kGood}, attackDb ? dbText(*attackDb) : std::string()),
      dot_(kNeutral) {}

void NoteSlot::draw(const Frame &frame) const {
  const Box &box = frame.area;
  const double height = box.bottom - box.top;
  frame_.draw(frame);
  if (empty_) {
    // Центр — в целом пикселе: иначе точки разных ячеек выходят разными.
    const double x = std::round((box.left + box.right) / 2.0);
    const double y = std::round((box.top + box.bottom) / 2.0);
    const double radius = std::round(std::max(4.0, height * 0.07)) / 2.0;
    dot_.draw(frame.within(Box{
        .left = x - radius, .top = y - radius, .right = x + radius, .bottom = y + radius}));
    return;
  }
  const double size = std::max(kMinFont, height * 0.075 / 0.13);
  const double middle = (box.top + box.bottom) / 2.0;
  level_.draw(frame.within(Box{.left = box.left,
                               .top = middle - size / 2.0,
                               .right = box.right,
                               .bottom = middle + size / 2.0}));
}

} // namespace training::render
