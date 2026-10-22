#pragma once

#include "training/render/calibration/note_slot.hpp"
#include "training/render/common/label.hpp"
#include "training/render/element.hpp"

#include <span>
#include <vector>

namespace training::render {

/// Ячейки нот шага нот калибровки в ряд и за ними — сколько нот из скольких.
class NoteSlots : public Element {
public:
  /// Создаёт ряд без ячеек.
  NoteSlots();

  /// Показывает уровни атак найденных нот `attacksDb`, dBFS, в `count`
  /// ячейках; самая тихая — в яркой рамке.
  void setAttacks(std::span<const double> attacksDb, int count);

  /// Рисует ячейки от левого края места: ячейка — 8.5 % ширины места,
  /// просвет — 1.2 %, высота — высота места. Подпись «3 of 8» — после ячеек,
  /// кегль — 0.055 высоты панели (0.055 / 0.13 высоты места).
  void draw(const Frame &frame) const override;

private:
  std::vector<NoteSlot> slots_;
  Label count_;
};

} // namespace training::render
