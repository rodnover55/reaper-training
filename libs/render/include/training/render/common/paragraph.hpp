#pragma once

#include "training/render/canvas.hpp"
#include "training/render/element.hpp"

#include <string>
#include <utility>
#include <vector>

namespace training::render {

/// Текст с переносом по словам: пояснение на панели калибровки.
///
/// Кегль — высота отведённого места, ширина строки — его ширина. Строки идут
/// вниз от верха места с шагом в 1.3 кегля и выходят за низ места, если их
/// больше одной. Слово шире места стоит в строке одно и вылезает за край.
class Paragraph : public Element {
public:
  /// Создаёт текст `text` цвета `color`; шрифт — не жирный Arial.
  explicit Paragraph(Color color, std::string text = {})
      : color_(color), text_(std::move(text)) {}

  /// Меняет текст.
  void setText(std::string text) { text_ = std::move(text); }

  /// Меняет цвет текста.
  void setColor(Color color) { color_ = color; }

  /// Низ текста, если писать его в место `area`, пикселей.
  double bottom(Canvas &canvas, const Box &area) const;

  /// Пишет текст строками от верха места кадра.
  void draw(const Frame &frame) const override;

private:
  /// Строки текста в место `area` (выбирает шрифт в холст).
  std::vector<std::string> lines(Canvas &canvas, const Box &area) const;

  Color color_;
  std::string text_;
};

} // namespace training::render
