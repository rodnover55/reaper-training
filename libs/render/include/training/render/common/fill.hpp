#pragma once

#include "training/render/canvas.hpp"
#include "training/render/element.hpp"

namespace training::render {

/// Заливка всего места одним цветом: фон области, строки, полосы шкалы,
/// древко маркера, ход шага.
class Fill : public Element {
public:
  /// Создаёт заливку цвета `color`.
  explicit Fill(Color color) : color_(color) {}

  /// Меняет цвет заливки.
  void setColor(Color color) { color_ = color; }

  /// Закрашивает место кадра цветом заливки.
  void draw(const Frame &frame) const override;

private:
  Color color_;
};

} // namespace training::render
