#pragma once

// Режим сетки, по которому связка строит доли тактов (`timing-display`).

namespace training::reaper {

/// Режим сетки: сколько равных нот приходится на один удар метронома.
enum class Mode {
  Quarters = 1,
  Eighths = 2,
  EighthTriplets = 3,
  Sixteenths = 4,
  SixteenthTriplets = 6,
};

/// Число долей сетки на удар в режиме `mode`: 1, 2, 3, 4 или 6.
constexpr int divisions(Mode mode) { return static_cast<int>(mode); }

} // namespace training::reaper
