#include "training/render/common/paragraph.hpp"

#include <algorithm>
#include <cstddef>
#include <string_view>

namespace training::render {
namespace {

/// Шаг строк в кеглях.
constexpr double kLineStep = 1.3;

} // namespace

std::vector<std::string> Paragraph::lines(Canvas &canvas, const Box &area) const {
  // Переносит сам: перенос средствами ОС есть не везде.
  canvas.useFont(area.bottom - area.top, false);
  const std::string_view text = text_;
  std::vector<std::string> out;
  std::string line;
  std::size_t from = 0;
  while (from <= text.size()) {
    const std::size_t space = std::min(text.find(' ', from), text.size());
    const std::string_view word = text.substr(from, space - from);
    std::string longer = line;
    if (!longer.empty())
      longer += ' ';
    longer += word;
    if (!line.empty() && canvas.textWidth(longer) > area.right - area.left) {
      out.push_back(line);
      line = word;
    } else {
      line = longer;
    }
    from = space + 1;
  }
  if (!line.empty())
    out.push_back(line);
  return out;
}

double Paragraph::bottom(Canvas &canvas, const Box &area) const {
  const double step = (area.bottom - area.top) * kLineStep;
  return area.top + step * static_cast<double>(lines(canvas, area).size());
}

void Paragraph::draw(const Frame &frame) const {
  const Box &area = frame.area;
  const double size = area.bottom - area.top;
  double y = area.top;
  for (const std::string &line : lines(frame.canvas, area)) {
    frame.canvas.drawText(
        line, Box{.left = area.left, .top = y, .right = area.left + 1.0, .bottom = y + size},
        color_, TextAlign::TopLeft);
    y += size * kLineStep;
  }
}

} // namespace training::render
