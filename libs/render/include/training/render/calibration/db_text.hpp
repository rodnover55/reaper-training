#pragma once

#include <string>

namespace training::render {

/// Уровень в dBFS для показа: `decimals` знаков после запятой, минус — U+2212,
/// как у отклонений; «−0» не бывает.
std::string dbText(double value, int decimals = 0);

} // namespace training::render
