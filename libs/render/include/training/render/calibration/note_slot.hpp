#pragma once

#include "training/render/common/dot.hpp"
#include "training/render/common/label.hpp"
#include "training/render/common/outline.hpp"
#include "training/render/element.hpp"

#include <optional>

namespace training::render {

/// Ячейка ноты на шаге нот калибровки: уровень атаки найденной ноты в
/// зелёной рамке, самая тихая — в яркой; пустая ячейка — точка в серой рамке.
class NoteSlot : public Element {
public:
  /// Создаёт ячейку: `attackDb` — уровень атаки ноты, dBFS, пусто — ячейка
  /// пустая; `softest` — нота самая тихая из найденных.
  NoteSlot(std::optional<double> attackDb, bool softest);

  /// Рисует ячейку во всё место кадра. Кегль уровня — 0.075 высоты панели,
  /// то есть 0.075 / 0.13 высоты ячейки; точка — 0.07 высоты ячейки, не меньше
  /// 4 пикселей, по центру в целом пикселе.
  void draw(const Frame &frame) const override;

private:
  bool empty_;
  Outline frame_;
  Label level_;
  Dot dot_;
};

} // namespace training::render
