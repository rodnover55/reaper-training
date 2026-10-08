#pragma once

// Передатчик уведомлений REAPER тренажёру: транспорт, скорость проекта,
// перенумерация входов MIDI.

// SWELL объявляет max и min макросами, и они ломают стандартную библиотеку.
#define WDL_NO_DEFINE_MINMAX

#include <reaper_plugin.h>

namespace training::reaper {

/// Регистрирует передатчик уведомлений REAPER — невидимую поверхность
/// управления (`csurf_inst`). С этого момента перемены транспорта и скорости
/// проекта, в том числе скорость проекта, открытого или выбранного во
/// вкладках, и перенумерация входов MIDI приходят тренажёру
/// (`Trainer::onPlayState`, `Trainer::onPlayRate`, `Trainer::onMidiDeviceRemap`)
/// в главном потоке, прямо в вызове, который их сделал. Зовётся при загрузке
/// расширения, после `initTrainer`.
void registerNotificationRelay(reaper_plugin_info_t *rec);

/// Снимает передатчик, зарегистрированный `registerNotificationRelay`; без
/// него ничего не делает. Зовётся при выгрузке расширения, до
/// `shutdownTrainer`.
void unregisterNotificationRelay();

} // namespace training::reaper
