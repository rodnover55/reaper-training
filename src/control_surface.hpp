#pragma once

// Невидимая поверхность управления тренажёра: уведомления REAPER о транспорте
// и скорости проекта (design.md D2).

// SWELL объявляет max и min макросами, и они ломают стандартную библиотеку.
#define WDL_NO_DEFINE_MINMAX

#include <reaper_plugin.h>

namespace training::reaper {

/// Регистрирует невидимую поверхность управления тренажёра (`csurf_inst`). С
/// этого момента перемены транспорта и скорости проекта, в том числе скорость
/// проекта, открытого или выбранного во вкладках, приходят тренажёру
/// (`Trainer::onPlayState`, `Trainer::onPlayRate`) в главном потоке, прямо в
/// вызове, который их сделал. Зовётся при загрузке расширения, после
/// `initTrainer`.
void registerSurface(reaper_plugin_info_t *rec);

/// Снимает поверхность, зарегистрированную `registerSurface`; без неё ничего
/// не делает. Зовётся при выгрузке расширения, до `shutdownTrainer`.
void unregisterSurface();

} // namespace training::reaper
