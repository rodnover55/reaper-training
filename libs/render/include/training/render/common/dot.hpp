#pragma once

#include "training/render/canvas.hpp"
#include "training/render/element.hpp"

namespace training::render {

/// Круглая точка, вписанная в место: удар без значения в строке такта,
/// пустая ячейка ноты.
class Dot : public Element {
public:
  /// Создаёт точку цвета `color`.
  explicit Dot(Color color) : color_(color) {}

  /// Меняет цвет точки.
  void setColor(Color color) { color_ = color; }

  /// Закрашивает эллипс, вписанный в место кадра: у квадратного места — круг.
  void draw(const Frame &frame) const override;

private:
  Color color_;
};

} // namespace training::render
