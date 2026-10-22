#pragma once

// Запись миллисекунд с шагом 0.5 мс для полей, подписей и файла настроек.

#include "training/core/clock.hpp"

#include <string>

namespace training::render {

/// Как записывать миллисекунды.
enum class MsStyle {
  /// Для файла настроек и поля Tolerance: «12», «12.5», «-7.5».
  Plain,
  /// Для поля Offset: «+12», «-7.5», «0».
  Signed,
  /// Для показа: «+12», «−7.5» (U+2212), «0».
  Shown,
};

/// Записывает интервал `value`, кратный 0.5 мс, в миллисекундах: целое — без
/// дробной части, с половиной — с одним знаком после точки.
std::string halfMsText(core::Interval value, MsStyle style);

} // namespace training::render
