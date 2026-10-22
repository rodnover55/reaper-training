#define REAPERAPI_MINIMAL
#define REAPERAPI_WANT_TimeMap2_timeToBeats
#define REAPERAPI_WANT_TimeMap2_beatsToTime
#define REAPERAPI_WANT_CountTempoTimeSigMarkers
#define REAPERAPI_WANT_GetTempoTimeSigMarker
#define REAPERAPI_WANT_GetProjectTimeSignature2

#include "project_timeline.hpp"

#include "clock.hpp"

// SWELL объявляет max и min макросами, и они ломают стандартную библиотеку.
#define WDL_NO_DEFINE_MINMAX

#include <reaper_plugin.h>
#include <reaper_plugin_functions.h>

#include <algorithm>
#include <cmath>

namespace training::reaper {
namespace {

/// Шаг по шкале, с, чтобы метка на границе такта считалась в начале такта.
constexpr double kAside = 1e-6;

/// Метка темпа и размера проекта.
struct Marker {
  double time = 0.0;
  double bpm = 0.0;
  bool linear = false;
};

/// Метка темпа и размера активного проекта с номером `index`, с 0; метки с
/// таким номером нет — метка с нулями.
Marker markerAt(int index) {
  Marker marker;
  (void)GetTempoTimeSigMarker(nullptr, index, &marker.time, nullptr, nullptr, &marker.bpm,
                              nullptr, nullptr, &marker.linear);
  return marker;
}

/// Темп, которым становится темп на метке `index` из `count` меток, четвертей
/// в минуту: у метки с плавным переходом — темп в конце перехода, то есть темп
/// следующей метки.
double tempoAfter(int index, int count) {
  const Marker marker = markerAt(index);
  if (marker.linear && index + 1 < count)
    return markerAt(index + 1).bpm;
  return marker.bpm;
}

/// Позиция REAPER, с, доли `beat` такта `measure`.
double secondsAt(std::int64_t measure, double beat) {
  const auto index = static_cast<int>(measure);
  return TimeMap2_beatsToTime(nullptr, beat, &index);
}

} // namespace

Position ProjectTimeline::positionAt(core::Moment time) const {
  int measure = 0;
  int numerator = 0;
  double beat = TimeMap2_timeToBeats(nullptr, projectSeconds(time), &measure, &numerator,
                                     nullptr, nullptr);

  // До начала проекта REAPER отдаёт удар в такте отрицательным — от начала
  // ближайшего следующего такта: время −0.25 с при 120 ударах в минуту — такт
  // 0, удар −0.5. Такт с таким ударом начинается на целое число тактов
  // раньше.
  std::int64_t bar = measure;
  if (beat < 0.0 && numerator > 0) {
    const double before = std::ceil(-beat / static_cast<double>(numerator));
    beat += before * static_cast<double>(numerator);
    bar -= static_cast<std::int64_t>(before);
  }
  return {.bar = bar, .beat = beat};
}

core::Moment ProjectTimeline::timeAt(Position position) const {
  return projectTime(secondsAt(position.bar, position.beat));
}

MapBar ProjectTimeline::barOf(std::int64_t index) const {
  // Размер берётся посередине первого удара: на самой границе такта обратный
  // пересчёт может дать конец прошлого такта.
  int numerator = 0;
  int denominator = 0;
  (void)TimeMap2_timeToBeats(nullptr, secondsAt(index, 0.5), nullptr, &numerator, nullptr,
                             &denominator);
  MapBar bar{.index = index, .meter = {.beats = numerator, .unit = denominator}, .tempo = {}};

  // Темп в начале такта — по последней метке не позже начала такта, до первой
  // метки — темп из настроек проекта. Метка в начале проекта действует и до
  // него: до неё ничего нет.
  double tempo = 0.0;
  GetProjectTimeSignature2(nullptr, &tempo, nullptr);
  const double start = secondsAt(index, 0.0);
  const double end = secondsAt(index, static_cast<double>(numerator));
  const int count = CountTempoTimeSigMarkers(nullptr);
  for (int marker = 0; marker < count; ++marker) {
    const double time = markerAt(marker).time;
    if (time <= std::max(start, 0.0) + kAside)
      tempo = tempoAfter(marker, count);
    else if (time < end - kAside)
      bar.tempo.emplace(core::training::state::Beat(positionAt(projectTime(time)).beat),
                        tempoAfter(marker, count));
  }
  bar.tempo.emplace(core::training::state::Beat(), tempo);
  return bar;
}

} // namespace training::reaper
