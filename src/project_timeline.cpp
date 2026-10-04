#define REAPERAPI_MINIMAL
#define REAPERAPI_WANT_TimeMap2_timeToBeats
#define REAPERAPI_WANT_TimeMap2_beatsToTime

#include "project_timeline.hpp"

// SWELL объявляет max и min макросами, и они ломают стандартную библиотеку.
#define WDL_NO_DEFINE_MINMAX

#include <reaper_plugin.h>
#include <reaper_plugin_functions.h>

#include <cmath>

namespace training::reaper {

double ProjectTimeline::beatsAt(double time) const {
  double fullBeats = 0.0;
  (void)TimeMap2_timeToBeats(nullptr, time, nullptr, nullptr, &fullBeats, nullptr);
  return fullBeats;
}

double ProjectTimeline::timeAt(double beats) const {
  return TimeMap2_beatsToTime(nullptr, beats, nullptr);
}

grid::BarBeat ProjectTimeline::barBeat(std::int64_t beat) const {
  int measure = 0;
  const double inMeasure = TimeMap2_timeToBeats(nullptr, timeAt(static_cast<double>(beat)),
                                                &measure, nullptr, nullptr, nullptr);
  // Время удара пересчитывается обратно в доли с погрешностью: «2.9999…» —
  // это третий удар, а не второй.
  return {.bar = measure + 1, .beat = static_cast<int>(std::floor(inMeasure + 1e-6)) + 1};
}

} // namespace training::reaper
