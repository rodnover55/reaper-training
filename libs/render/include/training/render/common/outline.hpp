#pragma once

#include "training/render/canvas.hpp"
#include "training/render/element.hpp"

namespace training::render {

/// Рамка толщиной в пиксель по краям места, внутри него: рамки дорожек шкал,
/// ползунков, ячеек нот и полей с числом.
class Outline : public Element {
public:
  /// Создаёт рамку цвета `color`.
  explicit Outline(Color color) : color_(color) {}

  /// Меняет цвет рамки.
  void setColor(Color color) { color_ = color; }

  /// Рисует рамку по краям места кадра.
  void draw(const Frame &frame) const override;

private:
  Color color_;
};

} // namespace training::render
