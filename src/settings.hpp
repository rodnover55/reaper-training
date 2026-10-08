#pragma once

// Хранение настроек тренажёра (`timing-display`): в REAPER, глобально, общие
// для всех проектов.

#include "training/trainer/settings.hpp"
#include "training/trainer/settings_file.hpp"

namespace training::reaper {

using trainer::inputText;
using trainer::Settings;

/// Файл, в котором REAPER хранит настройки расширения, — для журнала и
/// строки состояния.
inline constexpr const char *kSettingsFile = "reaper-extstate.ini";

/// Читает настройки, сохранённые REAPER (`GetExtState`), по правилам
/// `trainer::readSettings`: строка, которой нет или которая не принята,
/// оставляет настройке значение по умолчанию, не принятые строки — в
/// `SettingsRead::rejected`. Зовётся из главного потока.
trainer::SettingsRead loadSettings();

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
