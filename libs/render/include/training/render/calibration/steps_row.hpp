#pragma once

#include "training/render/common/fill.hpp"
#include "training/render/common/label.hpp"
#include "training/render/element.hpp"

#include <array>

namespace training::render {

/// Строка шагов калибровки «1. Silence — 2. Notes — Result»: пройденные шаги
/// — зелёные, текущий — жёлтый, будущие — тусклые.
class StepsRow : public Element {
public:
  /// Создаёт строку, в которой идёт первый шаг.
  StepsRow();

  /// Ставит текущий шаг `current`: 1 — тишина, 2 — ноты, 3 — итог.
  void setCurrent(int current);

  /// Пишет шаги слева направо от левого верхнего угла места кегля высоты
  /// места; между шагами — черта длиной 3 % ширины места.
  void draw(const Frame &frame) const override;

private:
  std::array<Label, 3> names_;
  Fill dash_;
};

} // namespace training::render
