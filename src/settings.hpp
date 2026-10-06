#pragma once

// Настройки тренажёра (design.md D7, `timing-display`): хранятся в REAPER
// глобально, общие для всех проектов.

#include "training/grid/hit_window.hpp"
#include "training/grid/mode.hpp"

namespace training::reaper {

/// Настройки тренажёра.
struct Settings {
  /// Режим сетки.
  grid::Mode mode = grid::Mode::Quarters;

  /// Допуск, мс: половина ширины окна попадания, кратная 0.5, от 1 до
  /// `scaleMs`.
  double toleranceMs = 10.0;

  /// Смещение, мс: центр окна попадания от клика, кратный 0.5; минус — до
  /// клика. Окно попадания лежит в шкале от −`scaleMs` до +`scaleMs`.
  double offsetMs = 0.0;

  /// Входной канал звуковой карты, 0 — первый.
  int channel = 0;

  /// Порог тишины, dBFS: целое от −90 до −10.
  double silenceDb = -50.0;

  /// Колонка номеров тактов слева от строк («Bar numbers»).
  bool showBarNumbers = true;

  /// Колонка среднего и разброса справа от строк («Mean/spread»).
  bool showBarStats = true;

  /// Панель настроек свёрнута в одну строку: кнопку «Settings» и строку
  /// состояния.
  bool panelCollapsed = false;

  /// Граница шкалы окна попадания, мс: целое от 10 до 200. Скрытая настройка
  /// `window_scale_ms`: `loadSettings` её читает, `saveSettings` не пишет, в
  /// окне её нет.
  double scaleMs = grid::kDefaultScaleMs;

  /// Окно попадания из смещения и допуска.
  grid::HitWindow window() const { return {.offsetMs = offsetMs, .toleranceMs = toleranceMs}; }
};

/// Читает настройки, сохранённые REAPER (`GetExtState`). Чего нет или что
/// вне границ, берётся по умолчанию или прижимается к границе. Зовётся из
/// главного потока.
Settings loadSettings();

/// Сохраняет настройки между запусками REAPER (`SetExtState` с `persist`),
/// кроме скрытой границы шкалы `scaleMs`. Зовётся из главного потока.
void saveSettings(const Settings &settings);

/// Включена ли скрытая настройка `console_log` — ненулевое целое в разделе
/// расширения `reaper-extstate.ini`. С ней расширение пишет в консоль REAPER
/// строку загрузки и вывод отладочных действий; без неё — только в журнал. В
/// окне настройки нет, по умолчанию она выключена: она для тестовых сборок.
/// Зовётся из главного потока.
bool consoleLogEnabled();

/// Прижимает границу шкалы к 10–200 мс, укладывает окно попадания в шкалу
/// (`grid::fitted`), порог тишины прижимает к −90…−10 dBFS; неизвестный режим
/// заменяет на «четверти», отрицательный канал — на 0.
Settings clamped(Settings settings);

} // namespace training::reaper
