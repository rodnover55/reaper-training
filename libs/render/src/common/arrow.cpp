#include "training/render/common/arrow.hpp"

#include <array>

namespace training::render {

void Arrow::draw(const Frame &frame) const {
  const Box &box = frame.area;
  const double size = box.right - box.left;
  const double middle = (box.top + box.bottom) / 2.0;
  std::array<Point, 3> points{};
  if (direction_ == Direction::Right)
    points = {Point{.x = box.left + size * 0.2, .y = middle - size / 2.0},
              Point{.x = box.left + size * 0.2, .y = middle + size / 2.0},
              Point{.x = box.left + size * 0.8, .y = middle}};
  else
    points = {Point{.x = box.left, .y = middle - size * 0.3},
              Point{.x = box.left + size, .y = middle - size * 0.3},
              Point{.x = box.left + size / 2.0, .y = middle + size * 0.3}};
  frame.canvas.fillPolygon(points, color_);
}

} // namespace training::render
