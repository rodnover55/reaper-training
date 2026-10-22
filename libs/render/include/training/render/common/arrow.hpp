#pragma once

#include "training/render/canvas.hpp"
#include "training/render/element.hpp"

namespace training::render {

/// Треугольная стрелка в квадратном месте: у кнопки сворачивания настроек.
class Arrow : public Element {
public:
  /// Куда смотрит стрелка.
  enum class Direction {
    /// Вниз: треугольник во всю ширину места, по середине высоты.
    Down,
    /// Вправо: треугольник во всю высоту места, от 0.2 до 0.8 ширины.
    Right,
  };

  /// Создаёт стрелку цвета `color`, смотрящую в сторону `direction`.
  Arrow(Direction direction, Color color) : direction_(direction), color_(color) {}

  /// Меняет сторону, куда смотрит стрелка.
  void setDirection(Direction direction) { direction_ = direction; }

  /// Меняет цвет стрелки.
  void setColor(Color color) { color_ = color; }

  /// Закрашивает треугольник стрелки в месте кадра; размер стрелки — ширина
  /// места.
  void draw(const Frame &frame) const override;

private:
  Direction direction_;
  Color color_;
};

} // namespace training::render
