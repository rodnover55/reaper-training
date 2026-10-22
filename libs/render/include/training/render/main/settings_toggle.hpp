#pragma once

#include "training/render/common/arrow.hpp"
#include "training/render/common/fill.hpp"
#include "training/render/common/label.hpp"
#include "training/render/element.hpp"
#include "training/render/palette.hpp"

namespace training::render {

/// Кнопка сворачивания панели настроек: объёмная рамка, стрелка — вниз у
/// развёрнутой панели, вправо у свёрнутой — и подпись «Settings» шрифтом
/// кнопки.
class SettingsToggle : public Element {
public:
  /// Создаёт ненажатую кнопку развёрнутой панели.
  SettingsToggle();

  /// Нажата ли кнопка: нажатая утоплена — рамка наоборот, стрелка и подпись
  /// сдвинуты на пиксель вправо и вниз.
  void setPressed(bool pressed);

  /// Свёрнута ли панель настроек.
  void setCollapsed(bool collapsed);

  /// Меняет цвета диалога: фон кнопки, тень и блик рамки, цвет стрелки и
  /// подписи.
  void setColors(const DialogColors &colors);

  /// Рисует кнопку во всё место кадра; подпись — шрифтом контекста холста.
  void draw(const Frame &frame) const override;

private:
  bool pressed_ = false;
  DialogColors colors_{};
  Fill face_;
  Fill light_;
  Fill dark_;
  Arrow arrow_;
  Label caption_;
};

} // namespace training::render
