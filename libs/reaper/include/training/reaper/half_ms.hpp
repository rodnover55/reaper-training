#pragma once

// Разбор миллисекунд с шагом 0.5 мс из поля окна и строки файла настроек.

#include "training/core/clock.hpp"

#include <optional>
#include <string_view>

namespace training::reaper {

/// Миллисекунды из текста, округлённые до 0.5 мс: «12», «12.5», «-7.5»,
/// «−7.5» (U+2212), «+3,5». Пробелы по краям допустимы.
///
/// @return интервал; пусто, если в тексте — что-то кроме десятичного числа:
///   «», «12.», «.5», «1e2».
std::optional<core::Interval> halfMsOf(std::string_view text);

} // namespace training::reaper
