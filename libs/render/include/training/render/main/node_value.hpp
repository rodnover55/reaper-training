#pragma once

#include "training/render/canvas.hpp"
#include "training/render/common/label.hpp"
#include "training/render/common/plate.hpp"
#include "training/render/element.hpp"

#include <string>

namespace training::render {

/// Цвет числа в окне.
enum class Tone {
  /// Без оценки: пропуск, IQR, подпись.
  Neutral,
  /// В окне попадания.
  Good,
  /// Вне окна попадания.
  Bad,
  /// Ровно в смещение: число не дальше 0.5 мс от смещения окна попадания.
  /// Такое число всегда в окне.
  Target,
};

/// Число или знак в окне вместе с цветом.
struct Cell {
  std::string text;
  Tone tone = Tone::Neutral;
};

/// Цвет числа тона `tone`: попадание и ровно в смещение — зелёный, промах —
/// красный, остальное — серый.
Color toneColor(Tone tone);

/// Значение узла в строке такта: число по центру места; число ровно в
/// смещение — тёмное на зелёной подложке.
class NodeValue : public Element {
public:
  /// Создаёт значение `cell`.
  explicit NodeValue(const Cell &cell);

  /// Меняет число и его цвет.
  void setCell(const Cell &cell);

  /// Число и его цвет.
  const Cell &cell() const { return cell_; }

  /// Ширина значения кеглем `size`, пикселей: числа, а у числа ровно в
  /// смещение — его подложки.
  double width(Canvas &canvas, double size) const;

  /// Пишет число по центру места кеглем высоты места. Подложка числа ровно в
  /// смещение — на 0.2 кегля шире числа с каждой стороны и на 0.08 кегля выше
  /// и ниже ячейки шрифта, скругление — 0.18 кегля.
  void draw(const Frame &frame) const override;

private:
  Cell cell_;
  Plate plate_;
  Label number_;
};

} // namespace training::render
