#pragma once

#include "training/render/canvas.hpp"
#include "training/render/element.hpp"

namespace training::render {

/// Вертикальный пунктир во всю ширину места: смещение окна попадания,
/// прежний порог тишины.
class Dashes : public Element {
public:
  /// Создаёт пунктир цвета `color`: штрихи длиной `dash` пикселей через
  /// каждые `period` пикселей.
  Dashes(Color color, double dash, double period)
      : color_(color), dash_(dash), period_(period) {}

  /// Рисует штрихи от верха места, пока начало штриха выше низа места;
  /// последний штрих обрезается низом места.
  void draw(const Frame &frame) const override;

private:
  Color color_;
  double dash_;
  double period_;
};

} // namespace training::render
