#pragma once

// SWELL объявляет max и min макросами, и они ломают стандартную библиотеку.
#define WDL_NO_DEFINE_MINMAX

#include <reaper_plugin.h>

namespace training::reaper {

/// Регистрирует действия отладочной сборки для живых проверок (design.md D10,
/// D11). Их зовут из списка действий или скриптом по имени, например
/// `Main_OnCommand(NamedCommandLookup("_REAPER_TRAINING_DEBUG_INPUTS"), 0)`.
/// В обычной сборке не регистрирует ничего. Зовётся при загрузке расширения,
/// после `initTrainer`.
void registerDebugActions(reaper_plugin_info_t *rec);

/// Снимает действия, зарегистрированные `registerDebugActions`. Зовётся при
/// выгрузке расширения, до `shutdownTrainer`.
void unregisterDebugActions();

} // namespace training::reaper
