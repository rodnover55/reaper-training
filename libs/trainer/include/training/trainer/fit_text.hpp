#pragma once

// Обрезка строки, которая не помещается при показе (`timing-display`).

#include <functional>
#include <string>
#include <string_view>

namespace training::trainer {

/// Ширина строки UTF-8 при показе — в любых единицах, тех же, что у ширины
/// места: пикселях шрифта, знаках. Строка длиннее — не уже.
using TextMeasure = std::function<double(std::string_view text)>;

/// Знак обрезки в конце строки, которая не поместилась: три точки, а не «…»
/// (U+2026), которое стоит в диапазонах вроде «10…200».
inline constexpr std::string_view kCutMark = "...";

/// Строка `text`, которая помещается в ширину `width` по `measure`: `text`
/// как есть, если помещается; иначе — самое длинное начало `text` по целым
/// знакам UTF-8, которое вместе с `kCutMark` в конце помещается. Если не
/// помещается и один `kCutMark`, — `kCutMark`.
std::string fitText(std::string_view text, double width, const TextMeasure &measure);

} // namespace training::trainer
