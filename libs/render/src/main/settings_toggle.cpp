#include "training/render/main/settings_toggle.hpp"

#include <algorithm>

namespace training::render {

SettingsToggle::SettingsToggle()
    : face_(Color{}), light_(Color{}), dark_(Color{}), arrow_(Arrow::Direction::Down, Color{}),
      caption_(
          Label::Style{.color = Color{}, .align = TextAlign::MiddleLeft, .contextFont = true},
          "Settings") {}

void SettingsToggle::setPressed(bool pressed) {
  pressed_ = pressed;
  setColors(colors_);
}

void SettingsToggle::setCollapsed(bool collapsed) {
  arrow_.setDirection(collapsed ? Arrow::Direction::Right : Arrow::Direction::Down);
}

void SettingsToggle::setColors(const DialogColors &colors) {
  colors_ = colors;
  face_.setColor(colors.face);
  light_.setColor(pressed_ ? colors.shadow : colors.highlight);
  dark_.setColor(pressed_ ? colors.highlight : colors.shadow);
  arrow_.setColor(colors.text);
  caption_.setColor(colors.text);
}

void SettingsToggle::draw(const Frame &frame) const {
  const Box &box = frame.area;
  face_.draw(frame);
  light_.draw(frame.within(
      Box{.left = box.left, .top = box.top, .right = box.right, .bottom = box.top + 1.0}));
  light_.draw(frame.within(
      Box{.left = box.left, .top = box.top, .right = box.left + 1.0, .bottom = box.bottom}));
  dark_.draw(frame.within(Box{
      .left = box.left, .top = box.bottom - 1.0, .right = box.right, .bottom = box.bottom}));
  dark_.draw(frame.within(
      Box{.left = box.right - 1.0, .top = box.top, .right = box.right, .bottom = box.bottom}));

  const double shift = pressed_ ? 1.0 : 0.0;
  const double height = box.bottom - box.top;
  const double size = std::max(5.0, height * 0.36);
  const double x = box.left + height * 0.35 + shift;
  const double y = (box.top + box.bottom) / 2.0 + shift;
  arrow_.draw(frame.within(
      Box{.left = x, .top = y - size / 2.0, .right = x + size, .bottom = y + size / 2.0}));
  caption_.draw(frame.within(Box{.left = x + size + height * 0.25,
                                 .top = box.top + shift,
                                 .right = box.right,
                                 .bottom = box.bottom + shift}));
}

} // namespace training::render
