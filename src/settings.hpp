#pragma once

// Хранение настроек тренажёра (`timing-display`): в REAPER, глобально, общие
// для всех проектов.

#include "training/trainer/settings.hpp"

namespace training::reaper {

using trainer::clamped;
using trainer::inputText;
using trainer::Settings;

/// Читает настройки, сохранённые REAPER (`GetExtState`). Чего нет или что
/// вне границ, берётся по умолчанию или прижимается к границе. Зовётся из
/// главного потока.
Settings loadSettings();

/// Сохраняет настройки между запусками REAPER (`SetExtState` с `persist`),
/// кроме скрытой границы шкалы `scaleMs`. Зовётся из главного потока.
void saveSettings(const Settings &settings);

/// Включена ли скрытая настройка `console_log` — ненулевое целое в разделе
/// расширения `reaper-extstate.ini`. С ней расширение пишет в консоль REAPER
/// строку загрузки; без неё — только в журнал. В окне настройки нет, по
/// умолчанию она выключена: она для тестовых сборок. Зовётся из главного
/// потока.
bool consoleLogEnabled();

} // namespace training::reaper
