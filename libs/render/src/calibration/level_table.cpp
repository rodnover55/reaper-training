#include "training/render/calibration/level_table.hpp"

#include "training/render/palette.hpp"

#include <algorithm>
#include <cstddef>

namespace training::render {

void LevelTable::setRows(std::span<const TableRow> rows) {
  labels_.clear();
  values_.clear();
  for (const TableRow &row : rows) {
    labels_.emplace_back(
        Label::Style{.color = kNeutral, .bold = false, .align = TextAlign::TopLeft},
        row.label);
    values_.emplace_back(Label::Style{.color = kText, .align = TextAlign::TopLeft}, row.value);
  }
}

void LevelTable::draw(const Frame &frame) const {
  const Box &box = frame.area;
  const double size = box.bottom - box.top;
  double labelWidth = 0.0;
  for (const Label &label : labels_)
    labelWidth = std::max(labelWidth, label.width(frame.canvas, size));
  const double column = box.left + labelWidth + (box.right - box.left) * 0.03;
  for (std::size_t i = 0; i < labels_.size(); ++i) {
    const double y = box.top + static_cast<double>(i) * size * 1.35;
    labels_[i].draw(frame.within(
        Box{.left = box.left, .top = y, .right = box.left + 1.0, .bottom = y + size}));
    values_[i].draw(frame.within(
        Box{.left = column, .top = y, .right = column + 1.0, .bottom = y + size}));
  }
}

} // namespace training::render
