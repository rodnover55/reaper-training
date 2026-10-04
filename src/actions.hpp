#pragma once

// Действия REAPER тренажёра (design.md D7, `timing-display`).

// SWELL объявляет max и min макросами, и они ломают стандартную библиотеку.
#define WDL_NO_DEFINE_MINMAX

#include <reaper_plugin.h>

namespace training::reaper {

/// Регистрирует действия: показать или скрыть окно тренажёра и по действию на
/// каждый режим сетки. Все — с состоянием-переключателем: в меню и на панелях
/// видно, открыто ли окно и какой режим включён. Зовётся при загрузке
/// расширения, после `initTrainer`.
void registerActions(reaper_plugin_info_t *rec);

/// Снимает действия, зарегистрированные `registerActions`. Зовётся при
/// выгрузке расширения.
void unregisterActions();

} // namespace training::reaper
