#define REAPERAPI_MINIMAL
#define REAPERAPI_WANT_Master_GetPlayRate

#include "reaper_notification_relay.hpp"

#include <reaper_plugin_functions.h>

#include "trainer.hpp"

#include <utility>

namespace training::reaper {
namespace {

/// Передатчик уведомлений REAPER: только принимает их и передаёт тренажёру.
/// Это невидимая поверхность управления REAPER (`csurf_inst`) без устройства и
/// без окна настроек: так REAPER доставляет уведомления.
class ReaperNotificationRelay final : public IReaperControlSurface {
public:
  /// Тип поверхности для REAPER — «REAPERTRAINING».
  const char *GetTypeString() override { return "REAPERTRAINING"; }

  /// Описание поверхности для REAPER — «reaper-training».
  const char *GetDescString() override { return "reaper-training"; }

  /// Пустая строка настроек: у поверхности нет настроек.
  const char *GetConfigString() override { return ""; }

  // Запись REAPER всегда ведёт с воспроизведением: флаг записи запуску ничего
  // не добавляет.
  void SetPlayState(bool play, bool pause, bool rec) override {
    trainer().onPlayState(play, pause, rec);
  }

  /// Передаёт тренажёру скорость воспроизведения
  /// (`CSURF_EXT_SETBPMANDPLAYRATE`) и смену номеров устройств MIDI
  /// (`CSURF_EXT_MIDI_DEVICE_REMAP`); остальные вызовы пропускает.
  ///
  /// @return всегда 0.
  int Extended(int call, void *parm1, void *parm2, void *parm3) override {
    // Скорость — второй параметр; первый, темп, тренажёру не нужен. При
    // открытии проекта и смене вкладки REAPER скорость не передаёт, а
    // Master_GetPlayRate уже отдаёт скорость нового проекта.
    if (call == CSURF_EXT_SETBPMANDPLAYRATE)
      trainer().onPlayRate(parm2 ? *static_cast<const double *>(parm2)
                                 : Master_GetPlayRate(nullptr));

    // Числа переданы в самих указателях, как в DEFAULT_DEVICE_REMAP примеров
    // поверхностей SDK (csurf.h).
    if (call == CSURF_EXT_MIDI_DEVICE_REMAP)
      trainer().onMidiDeviceRemap(reinterpret_cast<INT_PTR>(parm1) != 0,
                                  static_cast<int>(reinterpret_cast<INT_PTR>(parm2)),
                                  static_cast<int>(reinterpret_cast<INT_PTR>(parm3)));
    return 0;
  }
};

int (*hostRegister)(const char *name, void *infostruct) = nullptr;

// Обычный указатель, а не unique_ptr: деструктор статического объекта зовёт
// любой exit() в процессе, в том числе в копии REAPER после fork (см.
// trainer.cpp). Передатчик уничтожает только unregisterNotificationRelay.
ReaperNotificationRelay *relay = nullptr;

} // namespace

void registerNotificationRelay(reaper_plugin_info_t *rec) {
  if (!rec || !rec->Register)
    return;

  hostRegister = rec->Register;
  relay = new ReaperNotificationRelay();
  hostRegister("csurf_inst", relay);
}

void unregisterNotificationRelay() {
  if (hostRegister && relay)
    hostRegister("-csurf_inst", relay);

  delete std::exchange(relay, nullptr);
}

} // namespace training::reaper
