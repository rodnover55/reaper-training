#pragma once

// Карта темпа текущего проекта REAPER для сетки тренажёра.

#include "training/reaper/tempo_map.hpp"

namespace training::reaper {

/// Карта темпа активного проекта через функции `TimeMap` и метки темпа и
/// размера (`GetTempoTimeSigMarker`). Удар — единица знаменателя размера, как у
/// метронома REAPER; время шкалы — позиция REAPER (`projectTime`).
///
/// Многопоточность: только главный поток — функции `TimeMap` вне него не
/// обещаны.
class ProjectTimeline : public TempoMap {
public:
  /// Позиция по карте темпа активного проекта REAPER (`TimeMap2_timeToBeats`):
  /// номер такта — счёт тактов REAPER от начала проекта, без смещения номеров
  /// из настроек проекта.
  Position positionAt(core::Moment time) const override;

  /// Время позиции по карте темпа активного проекта REAPER
  /// (`TimeMap2_beatsToTime` с номером такта).
  core::Moment timeAt(Position position) const override;

  /// Номер такта — счёт тактов REAPER от начала проекта, без смещения номеров
  /// из настроек проекта. Темп — по меткам темпа проекта: до первой метки —
  /// темп из настроек проекта; метка в начале проекта действует и до него.
  MapBar barOf(std::int64_t index) const override;
};

} // namespace training::reaper
