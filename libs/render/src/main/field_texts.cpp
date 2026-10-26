#include "training/render/main/field_texts.hpp"

#include "training/core/clock.hpp"
#include "training/render/main/ms_text.hpp"

#include <fmt/format.h>

#include <cmath>
#include <string>
#include <string_view>

namespace training::render {
namespace {

/// Число поля `field` — миллисекунды `ms`, кратные 0.5, — так, как его пишет
/// поле: у смещения — со знаком.
std::string msText(Field field, double ms) {
  return halfMsText(ms * core::Interval::kMillisecond,
                    field == Field::Offset ? MsStyle::Signed : MsStyle::Plain);
}

/// Значение `value` поля `field` так, как его пишет поле.
std::string valueText(Field field, double value) {
  if (field == Field::Silence || field == Field::HideBars)
    return fmt::format("{}", std::lround(value));
  return msText(field, value);
}

/// Допустимые значения поля `field` без единиц: «-40…+40», «-90…-10».
std::string rangeText(Field field, const FieldBounds &bounds) {
  return fmt::format("{}…{}", valueText(field, bounds.min), valueText(field, bounds.max));
}

/// При каких остальных настройках допустимые значения такие: « at tolerance
/// 10», « at offset +5»; у постоянного диапазона — пусто. Смещение зависит от
/// допуска, допуск — от смещения.
std::string conditionText(Field field, const FieldBounds &bounds) {
  if (!bounds.condition)
    return {};
  if (field == Field::Offset)
    return fmt::format(" at tolerance {}", msText(Field::Tolerance, *bounds.condition));
  return fmt::format(" at offset {}", msText(Field::Offset, *bounds.condition));
}

/// Имя поля `field` в окне.
const char *nameOf(Field field) {
  switch (field) {
  case Field::Offset:
    return "Offset";
  case Field::Tolerance:
    return "Tolerance";
  case Field::Silence:
    return "Silence";
  case Field::HideBars:
    break;
  }
  return "Hide bars";
}

/// Единица числа поля `field` с пробелом перед ней: « ms», « dBFS»; у числа
/// тактов — пусто.
const char *unitOf(Field field) {
  switch (field) {
  case Field::Silence:
    return " dBFS";
  case Field::HideBars:
    return "";
  case Field::Offset:
  case Field::Tolerance:
    break;
  }
  return " ms";
}

} // namespace

std::string rangeHint(Field field, const FieldBounds &bounds) {
  return fmt::format("{}{}{}", rangeText(field, bounds), unitOf(field),
                     conditionText(field, bounds));
}

std::string rejectionText(Field field, std::string_view text, const FieldBounds &bounds,
                          double current) {
  return fmt::format("{} {}{} is out of range {}{} — not applied, {} in use", nameOf(field),
                     text, unitOf(field), rangeText(field, bounds),
                     conditionText(field, bounds), valueText(field, current));
}

} // namespace training::render
