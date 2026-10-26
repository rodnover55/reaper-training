#pragma once

// Тексты полей с числом настройки в окне: подсказка с допустимыми значениями
// и сообщение о вписанном числе вне них.

#include <optional>
#include <string>
#include <string_view>

namespace training::render {

/// Поле окна тренажёра, в которое вписывают число настройки.
enum class Field {
  /// Смещение окна попадания, мс.
  Offset,
  /// Допуск окна попадания, мс.
  Tolerance,
  /// Порог тишины, dBFS.
  Silence,
  /// Сколько последних тактов скрыто, целое без единиц.
  HideBars,
};

/// Допустимые значения поля в его единицах: мс, dBFS или такты.
struct FieldBounds {
  /// Наименьшее и наибольшее допустимое значение.
  double min = 0.0;
  double max = 0.0;

  /// Значение настройки, от которого зависит диапазон: допуск для поля
  /// Offset, смещение для поля Tolerance. Пусто — диапазон постоянный, как у
  /// поля Silence.
  std::optional<double> condition;
};

/// Подсказка к полю `field`: допустимые значения `bounds` — «-40…+40 ms at
/// tolerance 10», «1…50 ms at offset 0», «-90…-10 dBFS», «0…16». Минус — дефис: в
/// растровом шрифте диалогов Windows знака «−» (U+2212) нет.
std::string rangeHint(Field field, const FieldBounds &bounds);

/// Сообщение о том, что число `text`, вписанное в поле `field`, лежит вне
/// допустимых значений `bounds` и не принято: поле, вписанное как есть,
/// допустимые значения, как в `rangeHint`, и действующее значение `current` —
/// «Offset 45 ms is out of range -40…+40 at tolerance 10 — not applied, 0 in
/// use», «Hide bars 20 is out of range 0…16 — not applied, 2 in use».
std::string rejectionText(Field field, std::string_view text, const FieldBounds &bounds,
                          double current);

} // namespace training::render
