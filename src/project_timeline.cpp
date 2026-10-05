#define REAPERAPI_MINIMAL
#define REAPERAPI_WANT_TimeMap2_timeToBeats
#define REAPERAPI_WANT_TimeMap2_beatsToTime
#define REAPERAPI_WANT_CountTempoTimeSigMarkers
#define REAPERAPI_WANT_GetTempoTimeSigMarker
#define REAPERAPI_WANT_GetProjectTimeSignature2

#include "project_timeline.hpp"

// SWELL объявляет max и min макросами, и они ломают стандартную библиотеку.
#define WDL_NO_DEFINE_MINMAX

#include <reaper_plugin.h>
#include <reaper_plugin_functions.h>

#include <algorithm>
#include <cmath>

namespace training::reaper {
namespace {

/// Шаг по шкале, с, чтобы взять размер по разные стороны от метки.
constexpr double kAside = 1e-6;

/// Метка темпа и размера проекта.
struct Marker {
  double time = 0.0;
  double bpm = 0.0;
  bool linear = false;
};

Marker markerAt(int index) {
  Marker marker;
  (void)GetTempoTimeSigMarker(nullptr, index, &marker.time, nullptr, nullptr, &marker.bpm,
                              nullptr, nullptr, &marker.linear);
  return marker;
}

/// Размер такта в момент `time`.
grid::Meter meterAt(double time) {
  int numerator = 0;
  int denominator = 0;
  (void)TimeMap2_timeToBeats(nullptr, time, nullptr, &numerator, nullptr, &denominator);
  return {.beats = numerator, .unit = denominator};
}

} // namespace

double ProjectTimeline::beatsAt(double time) const {
  double fullBeats = 0.0;
  (void)TimeMap2_timeToBeats(nullptr, time, nullptr, nullptr, &fullBeats, nullptr);
  return fullBeats;
}

double ProjectTimeline::timeAt(double beats) const {
  return TimeMap2_beatsToTime(nullptr, beats, nullptr);
}

grid::Bar ProjectTimeline::barOf(std::int64_t beat) const {
  // Такт берётся посередине удара: на самой границе такта обратный пересчёт
  // может дать «3.9999…» прошлого такта.
  int measure = 0;
  int numerator = 0;
  int denominator = 0;
  const double inMeasure =
      TimeMap2_timeToBeats(nullptr, timeAt(static_cast<double>(beat) + 0.5), &measure,
                           &numerator, nullptr, &denominator);

  grid::Bar bar{.index = measure,
                .firstBeat = beat - static_cast<std::int64_t>(std::floor(inMeasure)),
                .meter = {.beats = numerator, .unit = denominator},
                .changes = {}};

  const double start = timeAt(static_cast<double>(bar.firstBeat));
  const double end = timeAt(static_cast<double>(bar.firstBeat + bar.meter.beats));
  const int count = CountTempoTimeSigMarkers(nullptr);
  for (int index = 0; index < count; ++index) {
    const Marker marker = markerAt(index);
    // Метка в начале проекта ничего не меняет: до неё ничего нет.
    if (marker.time < start - kAside || marker.time >= end - kAside || marker.time <= kAside)
      continue;

    grid::Change change;
    change.beat = std::max(0.0, beatsAt(marker.time) - static_cast<double>(bar.firstBeat));

    const grid::Meter before = meterAt(marker.time - kAside);
    const grid::Meter after = meterAt(marker.time + kAside);
    if (before != after) {
      change.meterFrom = before;
      change.meterTo = after;
    }

    // Темп перед меткой: в конце плавного перехода он уже равен темпу метки,
    // перед первой меткой — темп из настроек проекта.
    double tempoBefore = marker.bpm;
    if (index == 0) {
      GetProjectTimeSignature2(nullptr, &tempoBefore, nullptr);
    } else if (const Marker previous = markerAt(index - 1); !previous.linear) {
      tempoBefore = previous.bpm;
    }

    double from = tempoBefore;
    double to = marker.bpm;
    if (marker.linear && index + 1 < count) {
      from = marker.bpm;
      to = markerAt(index + 1).bpm;
    }
    if (std::abs(from - to) > 1e-6) {
      change.tempoFrom = from;
      change.tempoTo = to;
    }

    if (change.meterFrom || change.tempoFrom)
      bar.changes.push_back(change);
  }

  return bar;
}

} // namespace training::reaper
