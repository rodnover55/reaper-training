#include "training/render/main/hidden_row.hpp"

#include "training/render/main/bar_row.hpp"
#include "training/render/palette.hpp"

#include <fmt/format.h>

#include <algorithm>

namespace training::render {
namespace {

/// Фон строки скрытых тактов и подложка метки.
constexpr Color kHiddenRow{.red = 29, .green = 29, .blue = 29};
constexpr Color kHiddenPlate{.red = 51, .green = 51, .blue = 51};

} // namespace

HiddenRow::HiddenRow()
    : background_(kHiddenRow), plate_(kHiddenPlate, 0.5),
      label_(Label::Style{.color = kDim, .bold = true}),
      text_(Label::Style{.color = kText, .bold = false}) {}

void HiddenRow::setHidden(const std::string &first, const std::string &last, int count) {
  view_.label = first == last ? first : fmt::format("{}–{}", first, last);
  view_.text = fmt::format("{} {} hidden", count, count == 1 ? "bar" : "bars");
  label_.setText(view_.label);
  text_.setText(view_.text);
}

void HiddenRow::draw(const Frame &frame) const {
  background_.draw(frame);
  const Box &area = frame.area;
  const double height = area.bottom - area.top;
  const Layout layout = layoutOf(area, barNumbers_, false);
  const double middle = area.top + height / 2.0;

  if (layout.labelWidth > 0.0) {
    const double size = std::max(kMinFont, height * 0.42);
    label_.draw(frame.within(Box{.left = layout.left,
                                 .top = middle - size / 2.0,
                                 .right = layout.left + layout.labelWidth,
                                 .bottom = middle + size / 2.0}));
  }

  // Метка — на подложке со скруглёнными краями в начале шкалы.
  const double size = std::max(kMinFont, height * 0.32);
  const double width = text_.width(frame.canvas, size);
  const double left = layout.left + layout.labelWidth + 16.0;
  const double padX = size * 0.6;
  const double padY = size * 0.25;
  plate_.draw(frame.within(Box{.left = left,
                               .top = middle - size / 2.0 - padY,
                               .right = left + width + 2.0 * padX,
                               .bottom = middle + size / 2.0 + padY}));
  text_.draw(frame.within(Box{.left = left + padX,
                              .top = middle - size / 2.0,
                              .right = left + padX + width,
                              .bottom = middle + size / 2.0}));
}

} // namespace training::render
