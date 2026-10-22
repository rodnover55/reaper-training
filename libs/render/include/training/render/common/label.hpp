#pragma once

#include "training/render/canvas.hpp"
#include "training/render/element.hpp"

#include <string>
#include <utility>

namespace training::render {

/// Строка текста: номер такта, значение узла, среднее, подписи шкал, шаги и
/// подсказка панели калибровки, подпись кнопки.
///
/// Кегль — высота отведённого места (Arial); у подписи шрифтом контекста —
/// шрифт, который дал холст.
class Label : public Element {
public:
  /// Как пишется строка.
  struct Style {
    /// Цвет текста.
    Color color;

    /// Жирный шрифт.
    bool bold = true;

    /// Как строка стоит в месте.
    TextAlign align = TextAlign::Center;

    /// Писать шрифтом контекста (`Canvas::useContextFont`), а не Arial кеглем
    /// высоты места.
    bool contextFont = false;
  };

  /// Создаёт строку `text`, которая пишется стилем `style`.
  explicit Label(Style style, std::string text = {});

  /// Меняет текст строки.
  void setText(std::string text) { text_ = std::move(text); }

  /// Меняет цвет текста.
  void setColor(Color color) { style_.color = color; }

  /// Текст строки.
  const std::string &text() const { return text_; }

  /// Цвет текста.
  Color color() const { return style_.color; }

  /// Ширина строки кеглем `size` шрифтом стиля, пикселей.
  double width(Canvas &canvas, double size) const;

  /// Пишет строку в место кадра по выравниванию стиля.
  void draw(const Frame &frame) const override;

private:
  Style style_;
  std::string text_;
};

} // namespace training::render
