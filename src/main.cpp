// Точка входа расширения REAPER.
//
// REAPERAPI_IMPLEMENT создаёт определения указателей на функции API (ровно в
// одной единице трансляции), REAPERAPI_MINIMAL вместе с REAPERAPI_WANT_*
// ограничивает набор загружаемых функций теми, что действительно нужны.
#define REAPERAPI_IMPLEMENT
#define REAPERAPI_MINIMAL
#define REAPERAPI_WANT_ShowConsoleMsg
#define REAPERAPI_WANT_GetResourcePath
#define REAPERAPI_WANT_Audio_RegHardwareHook
#define REAPERAPI_WANT_GetPlayPosition2Ex
#define REAPERAPI_WANT_GetPlayStateEx
#define REAPERAPI_WANT_GetNumAudioInputs
#define REAPERAPI_WANT_GetInputOutputLatency
#define REAPERAPI_WANT_get_config_var
#define REAPERAPI_WANT_projectconfig_var_getoffs
#define REAPERAPI_WANT_projectconfig_var_addr
#define REAPERAPI_WANT_EnumProjects
#define REAPERAPI_WANT_GetSelectedMediaItem
#define REAPERAPI_WANT_GetActiveTake
#define REAPERAPI_WANT_GetMediaItemInfo_Value
#define REAPERAPI_WANT_GetMediaItemTakeInfo_Value
#define REAPERAPI_WANT_SetMediaItemTakeInfo_Value
#define REAPERAPI_WANT_GetMediaItemTake_Source
#define REAPERAPI_WANT_GetMediaSourceNumChannels
#define REAPERAPI_WANT_CreateTakeAudioAccessor
#define REAPERAPI_WANT_DestroyAudioAccessor
#define REAPERAPI_WANT_GetAudioAccessorStartTime
#define REAPERAPI_WANT_GetAudioAccessorEndTime
#define REAPERAPI_WANT_GetAudioAccessorSamples
#define REAPERAPI_WANT_Master_GetPlayRate
#define REAPERAPI_WANT_DockWindowActivate
#define REAPERAPI_WANT_DockWindowRemove
#define REAPERAPI_WANT_DockWindowAddEx
#define REAPERAPI_WANT_GetMainHwnd
#define REAPERAPI_WANT_GetSet_LoopTimeRange2
#define REAPERAPI_WANT_GetSetRepeat
#define REAPERAPI_WANT_GetInputChannelName
#define REAPERAPI_WANT_TimeMap2_beatsToTime
#define REAPERAPI_WANT_TimeMap2_timeToBeats
#define REAPERAPI_WANT_CountTempoTimeSigMarkers
#define REAPERAPI_WANT_GetTempoTimeSigMarker
#define REAPERAPI_WANT_GetProjectTimeSignature2
#define REAPERAPI_WANT_GetPlayPositionEx
#define REAPERAPI_WANT_GetCursorPosition
#define REAPERAPI_WANT_SetExtState
#define REAPERAPI_WANT_GetExtState
#define REAPERAPI_WANT_Audio_IsRunning
#define REAPERAPI_WANT_Audio_Init

// SWELL объявляет max и min макросами, и они ломают стандартную библиотеку.
#define WDL_NO_DEFINE_MINMAX

#include <reaper_plugin.h>
#include <reaper_plugin_functions.h>

#include "actions.hpp"
#include "build_stamp.hpp"
#include "debug_actions.hpp"
#include "journal.hpp"
#include "settings.hpp"
#include "trainer.hpp"
#include "window.hpp"

#include <fmt/format.h>

#include <string>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace {

#ifndef _WIN32
/// Процесс, в который REAPER загрузил расширение; 0 — расширение не загружено.
pid_t loadedProcess = 0;
#endif

/// Проверяет, идёт ли вызов в копии процесса REAPER, сделанной fork, а не в
/// процессе, куда REAPER загрузил расширение. В копии есть только поток,
/// вызвавший fork, а потоков расширения нет.
///
/// На Windows: fork нет, всегда false.
bool inForkedCopy() {
#ifdef _WIN32
  return false;
#else
  return loadedProcess != 0 && getpid() != loadedProcess;
#endif
}

} // namespace

extern "C" REAPER_PLUGIN_DLL_EXPORT int
REAPER_PLUGIN_ENTRYPOINT(REAPER_PLUGIN_HINSTANCE instance, reaper_plugin_info_t *rec) {
  if (!rec) { // rec == nullptr — REAPER выгружает расширение
    // Копия REAPER после fork сейчас завершится. Остановка ждала бы в ней
    // рабочий поток тренажёра, которого там нет, — вечно, — а REAPER в копии
    // — снимок, а не живой процесс. Освобождать в ней нечего.
    if (inForkedCopy())
      return 0;

    training::reaper::unregisterDebugActions();
    training::reaper::unregisterActions();
    training::reaper::closeWindow();
    training::reaper::shutdownTrainer();
    training::reaper::journal("unload");
    training::reaper::closeJournal();
    return 0;
  }

  if (rec->caller_version != REAPER_PLUGIN_VERSION || !rec->GetFunc)
    return 0;

  if (REAPERAPI_LoadAPI(rec->GetFunc) != 0)
    return 0;

#ifndef _WIN32
  loadedProcess = getpid();
#endif

  // Журнал лежит в каталоге ресурсов: у каждого экземпляра REAPER он свой.
  training::reaper::openJournal(std::string(GetResourcePath()) + "/reaper-training.log");
  training::reaper::journal("load (версия {}, сборка {})", training::reaper::kVersion,
                            training::reaper::kBuildStamp);

  training::reaper::setResourceModule(instance);
  training::reaper::initTrainer(rec);
  training::reaper::registerActions(rec);
  training::reaper::registerDebugActions(rec);

  // Версия и время сборки — чтобы было видно, тот ли модуль загружен:
  // забытый `cmake --install` выглядит как «ничего не изменилось». В журнале
  // они есть всегда, в консоли — только по скрытой настройке: обычному
  // пользователю окно консоли ни к чему (`platform-support`).
  if (training::reaper::consoleLogEnabled()) {
    const std::string hello =
        fmt::format("reaper-training {} loaded (сборка {})\n", training::reaper::kVersion,
                    training::reaper::kBuildStamp);
    ShowConsoleMsg(hello.c_str());
  }

  return 1;
}
