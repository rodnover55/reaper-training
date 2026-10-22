#pragma once

#include "training/render/canvas.hpp"
#include "training/render/element.hpp"

namespace training::render {

/// Подложка со скруглёнными углами во всё место: под числом попадания ровно в
/// смещение.
class Plate : public Element {
public:
  /// Создаёт подложку цвета `color`; радиус углов — доля `roundness` высоты
  /// места.
  Plate(Color color, double roundness) : color_(color), roundness_(roundness) {}

  /// Меняет цвет подложки.
  void setColor(Color color) { color_ = color; }

  /// Закрашивает место кадра со скруглёнными углами.
  void draw(const Frame &frame) const override;

private:
  Color color_;
  double roundness_;
};

} // namespace training::render
