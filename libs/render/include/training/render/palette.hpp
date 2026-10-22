#pragma once

// Цвета окна на тёмном фоне и наименьший кегль.

#include "training/render/canvas.hpp"

namespace training::render {

/// Цвета строк тактов.
inline constexpr Color kBackground{.red = 24, .green = 24, .blue = 24};
inline constexpr Color kCurrentRow{.red = 36, .green = 36, .blue = 36};
inline constexpr Color kCurrentBeatBack{.red = 69, .green = 59, .blue = 40};
inline constexpr Color kCurrentBeat{.red = 255, .green = 190, .blue = 60};
inline constexpr Color kChange{.red = 110, .green = 170, .blue = 255};
inline constexpr Color kLine{.red = 56, .green = 56, .blue = 56};
inline constexpr Color kNeutral{.red = 170, .green = 170, .blue = 170};
// Затакт — пре-ролл и отсчёт перед запуском: строка на синем фоне, подпись —
// на фоне светлее (`timing-display`).
inline constexpr Color kLeadInRow{.red = 28, .green = 40, .blue = 63};
inline constexpr Color kLeadInLabel{.red = 35, .green = 52, .blue = 84};

/// Цифры числа на подложке попадания ровно в смещение.
inline constexpr Color kPlateText{.red = 20, .green = 20, .blue = 20};

/// Цвета панели калибровки, которых нет у строк.
inline constexpr Color kGood{.red = 90, .green = 210, .blue = 90};
inline constexpr Color kBad{.red = 235, .green = 80, .blue = 70};
inline constexpr Color kDim{.red = 125, .green = 125, .blue = 125};
inline constexpr Color kText{.red = 221, .green = 221, .blue = 221};
inline constexpr Color kBright{.red = 255, .green = 255, .blue = 255};
inline constexpr Color kSlotBorder{.red = 60, .green = 110, .blue = 60};
inline constexpr Color kTrack{.red = 40, .green = 40, .blue = 40};
inline constexpr Color kNoise{.red = 62, .green = 62, .blue = 62};
inline constexpr Color kNoiseStripe{.red = 100, .green = 100, .blue = 100};
inline constexpr Color kLive{.red = 115, .green = 115, .blue = 115};
inline constexpr Color kBand{.red = 45, .green = 95, .blue = 45};

/// Цвета шкалы окна попадания.
inline constexpr Color kOutside{.red = 80, .green = 40, .blue = 37};
inline constexpr Color kGaugeFrame{.red = 60, .green = 60, .blue = 60};
inline constexpr Color kHandle{.red = 228, .green = 228, .blue = 228};
inline constexpr Color kHandleFrame{.red = 34, .green = 34, .blue = 34};
inline constexpr Color kHandleGrip{.red = 119, .green = 119, .blue = 119};

/// Наименьший кегль, пикселей: мельче цифры не читаются.
inline constexpr double kMinFont = 9.0;

/// Системные цвета диалога: ими рисуется то, что стоит среди контролов окна.
/// Их даёт окно.
struct DialogColors {
  /// Фон диалога и кнопок.
  Color face;

  /// Текст кнопок.
  Color text;

  /// Тень объёмной рамки.
  Color shadow;

  /// Блик объёмной рамки.
  Color highlight;
};

} // namespace training::render
