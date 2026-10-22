#pragma once

// Хранение настроек тренажёра (`timing-display`): в REAPER, глобально, общие
// для всех проектов.

#include "input_settings.hpp"
#include "settings_read.hpp"

#include "training/core/settings.hpp"
#include "training/reaper/mode.hpp"
#include "training/render/main/view_settings.hpp"

namespace training::reaper {

/// Файл, в котором REAPER хранит настройки расширения, — для журнала и
/// строки состояния.
inline constexpr const char *kSettingsFile = "reaper-extstate.ini";

/// Читает настройки, сохранённые REAPER (`GetExtState`), по правилам
/// `readSettings`: строка, которой нет или которая не принята, оставляет
/// настройке значение по умолчанию, не принятые строки — в
/// `SettingsRead::rejected`. Зовётся из главного потока.
SettingsRead loadSettings();

/// Сохраняет настройки ядра `settings`, выбранный вход `input`, режим сетки
/// `mode` и настройки отрисовки `view` между запусками REAPER (`SetExtState`
/// с `persist`), кроме скрытой границы шкалы `render::ViewSettings::scale`.
/// Режим пишется ключом `mode` — числом долей на удар. Зовётся из главного
/// потока.
void saveSettings(const core::Settings &settings, const InputSettings &input, Mode mode,
                  const render::ViewSettings &view);

/// Включена ли скрытая настройка `console_log` — ненулевое целое в разделе
/// расширения `reaper-extstate.ini`. С ней расширение пишет в консоль REAPER
/// строку загрузки; без неё — только в журнал. В окне настройки нет, по
/// умолчанию она выключена: она для тестовых сборок. Зовётся из главного
/// потока.
bool consoleLogEnabled();

} // namespace training::reaper
