#pragma once

#include "training/render/common/label.hpp"
#include "training/render/element.hpp"

#include <span>
#include <string>
#include <vector>

namespace training::render {

/// Строка таблицы уровней итога: подпись и значение.
struct TableRow {
  std::string label;
  std::string value;
};

/// Таблица уровней итога калибровки: подписи — слева, значения жирным — в
/// колонке за самой длинной подписью.
class LevelTable : public Element {
public:
  /// Создаёт пустую таблицу.
  LevelTable() = default;

  /// Показывает строки `rows` сверху вниз.
  void setRows(std::span<const TableRow> rows);

  /// Пишет таблицу от левого верхнего угла места: кегль — высота места, шаг
  /// строк — 1.35 кегля, колонка значений — в 3 % ширины места от самой
  /// длинной подписи.
  void draw(const Frame &frame) const override;

private:
  std::vector<Label> labels_;
  std::vector<Label> values_;
};

} // namespace training::render
