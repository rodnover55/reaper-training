#pragma once

#include "training/core/clock.hpp"
#include "training/render/canvas.hpp"
#include "training/render/element.hpp"

namespace training::render {

/// Подписи делений шкалы окна попадания: «−20», «−10», «0», «+10», «+20»;
/// подпись нуля — жирная.
class GaugeTicks : public Element {
public:
  /// Создаёт подписи шкалы с границей `scale`, цвет текста — `color`.
  GaugeTicks(core::Interval scale, Color color) : scale_(scale), color_(color) {}

  /// Меняет границу шкалы.
  void setScale(core::Interval scale) { scale_ = scale; }

  /// Меняет цвет подписей.
  void setColor(Color color) { color_ = color; }

  /// Пишет подписи под дорожкой шкалы, разложенной в месте кадра
  /// (`gaugeLayout`), с самым частым шагом, при котором подписи не налезают
  /// друг на друга.
  void draw(const Frame &frame) const override;

private:
  core::Interval scale_;
  Color color_;
};

} // namespace training::render
