#pragma once

#include "training/render/common/outline.hpp"
#include "training/render/element.hpp"

namespace training::render {

/// Ширина рамки вокруг поля с числом, пикселей: рамка — снаружи поля.
inline constexpr int kRejectionFrame = 2;

/// Рамка вокруг поля с числом шириной `kRejectionFrame`: у отклонённого поля
/// — красная, у остальных — цветом фона диалога, стирая прежнюю.
class FieldFrame : public Element {
public:
  /// Создаёт рамку цвета `color`.
  explicit FieldFrame(Color color);

  /// Меняет цвет рамки.
  void setColor(Color color);

  /// Рисует рамку по краям места кадра — поля, расширенного на
  /// `kRejectionFrame` с каждой стороны.
  void draw(const Frame &frame) const override;

private:
  Outline ring_;
};

} // namespace training::render
