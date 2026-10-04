#pragma once

#include "training/grid/beats.hpp"

#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace training::grid {

/// Цвет числа в окне.
enum class Tone {
  /// Без оценки: пропуск, разброс, подпись.
  Neutral,
  /// Не дальше допуска.
  Good,
  /// Дальше допуска.
  Bad,
};

/// Число или знак в окне вместе с цветом.
struct Cell {
  std::string text;
  Tone tone = Tone::Neutral;
};

/// Строка окна.
struct RowView {
  /// Такт и удар, «12.3».
  std::string label;

  /// Узлы по порядку: «−15», «+5», «0», «.» — пропуск, «+3*» — с лишней
  /// нотой; пустой текст — нота ещё не пришла.
  std::vector<Cell> cells;

  /// Среднее, «+2.0»; пусто — в режиме «четверти» или пока удар не решён.
  std::optional<Cell> mean;

  /// Разброс, «3.7», цвет `Neutral`; есть вместе с `mean`.
  std::optional<Cell> spread;
};

/// Готовит строки окна из строк свода (design.md D6, `timing-display`).
///
/// Отклонение ноты — целые миллисекунды, округлённые до ближайшего, со знаком:
/// «−» (U+2212) — раньше, «+» — позже, «0» — ноль. Цвет — по показанному
/// числу: не больше `toleranceMs` по модулю — `Good`, иначе `Bad`. Среднее и
/// разброс — с одним знаком после запятой; среднее окрашено тем же правилом
/// по показанному числу.
///
/// @param toleranceMs допуск, мс; не меньше нуля.
std::vector<RowView> present(const std::deque<BeatRow> &rows, double toleranceMs);

} // namespace training::grid
