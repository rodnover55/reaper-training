#pragma once

// Карта темпа текущего проекта REAPER для сетки тренажёра (design.md D5).

#include "training/grid/timeline.hpp"

namespace training::reaper {

/// Карта темпа активного проекта через `TimeMap2_timeToBeats` и
/// `TimeMap2_beatsToTime`. Удар — единица знаменателя размера, как у
/// метронома REAPER (findings.md, R3).
///
/// Многопоточность: только главный поток — функции `TimeMap` вне него не
/// обещаны.
class ProjectTimeline : public grid::Timeline {
public:
  double beatsAt(double time) const override;
  double timeAt(double beats) const override;
  grid::BarBeat barBeat(std::int64_t beat) const override;
};

} // namespace training::reaper
