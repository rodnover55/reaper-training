#include "training/render/common/label.hpp"

#include <utility>

namespace training::render {

Label::Label(Style style, std::string text) : style_(style), text_(std::move(text)) {}

double Label::width(Canvas &canvas, double size) const {
  canvas.useFont(size, style_.bold);
  return canvas.textWidth(text_);
}

void Label::draw(const Frame &frame) const {
  if (style_.contextFont)
    frame.canvas.useContextFont();
  else
    frame.canvas.useFont(frame.area.bottom - frame.area.top, style_.bold);
  frame.canvas.drawText(text_, frame.area, style_.color, style_.align);
}

} // namespace training::render
