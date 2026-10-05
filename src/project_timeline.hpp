#pragma once

// Карта темпа текущего проекта REAPER для сетки тренажёра (design.md D6).

#include "training/grid/timeline.hpp"

namespace training::reaper {

/// Карта темпа активного проекта через функции `TimeMap` и метки темпа и
/// размера (`GetTempoTimeSigMarker`). Удар — единица знаменателя размера, как у
/// метронома REAPER (findings.md, R3).
///
/// Многопоточность: только главный поток — функции `TimeMap` вне него не
/// обещаны.
class ProjectTimeline : public grid::Timeline {
public:
  double beatsAt(double time) const override;
  double timeAt(double beats) const override;

  /// Номер такта — счёт тактов REAPER от начала проекта, без смещения номеров
  /// из настроек проекта. Смены — по меткам темпа и размера проекта в такте:
  /// метка, где не меняется ни темп, ни размер, смены не даёт; у метки с
  /// плавным переходом темп «до» и «после» — начало и конец перехода.
  grid::Bar barOf(std::int64_t beat) const override;
};

} // namespace training::reaper
