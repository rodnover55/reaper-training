#pragma once

// Окно тренажёра (design.md D8, `timing-display`).

// SWELL объявляет max и min макросами, и они ломают стандартную библиотеку.
#define WDL_NO_DEFINE_MINMAX

#include <reaper_plugin.h>

namespace training::reaper {

/// Модуль, в ресурсах которого лежат формы, — сам модуль расширения. Зовётся
/// при загрузке расширения до первого показа окна.
void setResourceModule(REAPER_PLUGIN_HINSTANCE module);

/// Открыто ли окно тренажёра.
bool windowShown();

/// Показывает окно тренажёра в доке REAPER, если оно закрыто, и закрывает,
/// если открыто. Где и закреплено ли окно, REAPER помнит сам по его
/// идентификатору. Зовётся из главного потока, после `initTrainer`.
void toggleWindow();

/// Перерисовывает строки и строку состояния открытого окна; закрытое не
/// трогает. Зовётся из главного потока.
void refreshWindow();

/// Показывает в контролах окна текущие настройки тренажёра — после того как
/// их поменяли не из окна, например действием режима.
void showSettings();

/// Закрывает окно, если оно открыто. Зовётся при выгрузке расширения, до
/// `shutdownTrainer`.
void closeWindow();

} // namespace training::reaper
