#pragma once

// Настройки тренажёра (`timing-display`): общие для всех проектов.

#include "training/grid/hit_window.hpp"
#include "training/grid/mode.hpp"

#include <string>

namespace training::trainer {

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

  /// Входной канал звуковой карты, 0 — первый. Пока выбран вход MIDI, не
  /// меняется: при возврате к звуку выбран тот же канал.
  int channel = 0;

  /// Порог тишины, dBFS: целое от −90 до −10.
  double silenceDb = -50.0;

  /// Выбранный вход MIDI — имя устройства для сохранения: имя без
  /// псевдонима, а если его нет, — с псевдонимом. Пусто — выбран канал
  /// звуковой карты `channel`.
  std::string midiInput{};

  /// Номер устройства MIDI у хоста, когда вход выбирали: различает два
  /// устройства с одним именем. Без входа MIDI не используется.
  int midiIndex = -1;

  /// Колонка номеров тактов слева от строк («Bar numbers»).
  bool showBarNumbers = true;

  /// Колонка среднего и разброса справа от строк («Mean/spread»).
  bool showBarStats = true;

  /// Панель настроек свёрнута в одну строку: кнопку «Settings» и строку
  /// состояния.
  bool panelCollapsed = false;

  /// Граница шкалы окна попадания, мс: целое от 10 до 200. Скрытая
  /// настройка: в окне её нет, тренажёр её не сохраняет.
  double scaleMs = grid::kDefaultScaleMs;

  /// Окно попадания из смещения и допуска.
  grid::HitWindow window() const { return {.offsetMs = offsetMs, .toleranceMs = toleranceMs}; }
};

/// Прижимает границу шкалы к 10–200 мс, укладывает окно попадания в шкалу
/// (`grid::fitted`), порог тишины прижимает к −90…−10 dBFS; неизвестный режим
/// заменяет на «четверти», отрицательный канал — на 0.
Settings clamped(Settings settings);

/// Входной канал звуковой карты, звук которого нужен тренажёру при настройках
/// `settings`: `Settings::channel`; −1 — выбран вход MIDI, и звук не нужен.
int audioChannel(const Settings &settings);

/// Правда, если у настроек `before` и `after` разный выбранный вход: канал
/// звуковой карты, вход MIDI или номер его устройства.
bool inputChanged(const Settings &before, const Settings &after);

/// Выбранный вход настроек `settings` для журнала: «channel 1» или «MIDI
/// «KeyStep 37» #3».
std::string inputText(const Settings &settings);

} // namespace training::trainer
