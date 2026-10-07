#define REAPERAPI_MINIMAL
#define REAPERAPI_WANT_Master_GetPlayRate

#include "control_surface.hpp"

#include <reaper_plugin_functions.h>

#include "trainer.hpp"

#include <utility>

namespace training::reaper {
namespace {

/// Поверхность без устройства и без окна настроек: только принимает
/// уведомления REAPER и передаёт их тренажёру.
class Surface final : public IReaperControlSurface {
public:
  const char *GetTypeString() override { return "REAPERTRAINING"; }
  const char *GetDescString() override { return "reaper-training"; }
  const char *GetConfigString() override { return ""; }

  void SetPlayState(bool play, bool pause, bool rec) override {
    trainer().onPlayState(play, pause, rec);
  }

  int Extended(int call, void *parm1, void *parm2, void *parm3) override {
    // Скорость — второй параметр; первый, темп, тренажёру не нужен. При
    // открытии проекта и смене вкладки REAPER скорость не передаёт, а
    // Master_GetPlayRate уже отдаёт скорость нового проекта (findings.md, R3).
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
// trainer.cpp). Поверхность уничтожает только unregisterSurface.
Surface *surface = nullptr;

} // namespace

void registerSurface(reaper_plugin_info_t *rec) {
  if (!rec || !rec->Register)
    return;

  hostRegister = rec->Register;
  surface = new Surface();
  hostRegister("csurf_inst", surface);
}

void unregisterSurface() {
  if (hostRegister && surface)
    hostRegister("-csurf_inst", surface);

  delete std::exchange(surface, nullptr);
}

} // namespace training::reaper
