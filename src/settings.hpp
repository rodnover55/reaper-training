#pragma once

// Настройки тренажёра (design.md D7, `timing-display`): хранятся в REAPER
// глобально, общие для всех проектов.

#include "training/grid/mode.hpp"

namespace training::reaper {

/// Настройки тренажёра.
struct Settings {
  /// Режим сетки.
  grid::Mode mode = grid::Mode::Quarters;

  /// Допуск, мс: целое от 1 до 50.
  double toleranceMs = 10.0;

  /// Входной канал звуковой карты, 0 — первый.
  int channel = 0;

  /// Порог тишины, dBFS: целое от −90 до −10.
  double silenceDb = -50.0;

  /// Колонка номеров тактов слева от строк («Bar numbers»).
  bool showBarNumbers = true;

  /// Колонка среднего и разброса справа от строк («Mean/spread»).
  bool showBarStats = true;
};

/// Читает настройки, сохранённые REAPER (`GetExtState`). Чего нет или что
/// вне границ, берётся по умолчанию или прижимается к границе. Зовётся из
/// главного потока.
Settings loadSettings();

/// Сохраняет настройки между запусками REAPER (`SetExtState` с `persist`).
/// Зовётся из главного потока.
void saveSettings(const Settings &settings);

/// Прижимает допуск к границам 1–50 мс, а порог тишины — к −90…−10 dBFS;
/// неизвестный режим заменяет на «четверти», отрицательный канал — на 0.
Settings clamped(Settings settings);

} // namespace training::reaper
