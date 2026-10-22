#pragma once

#include "training/render/common/fill.hpp"
#include "training/render/common/label.hpp"
#include "training/render/element.hpp"

#include <string>

namespace training::render {

/// Маркер смены темпа или размера в строке такта — флажок: древко у левого
/// края места и подпись «было → стало» справа от него.
class ChangeMark : public Element {
public:
  /// Создаёт маркер с подписью `text`: «4/4 → 7/8», «120 → 150».
  explicit ChangeMark(std::string text);

  /// Древко во всю высоту места шириной 2 пикселя у его левого края; подпись
  /// — в 5 пикселях от левого края, кегль — высота места без 2 пикселей.
  void draw(const Frame &frame) const override;

private:
  Fill pole_;
  Label caption_;
};

} // namespace training::render
