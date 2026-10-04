#pragma once

// Карты темпа для тестов сетки: постоянный темп и плавная смена темпа.

#include "training/grid/timeline.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace training::test {

/// Карта темпа с постоянным темпом и числом ударов в такте; удар 0 — на 0 с.
class SteadyTimeline : public grid::Timeline {
public:
  /// @param bpm темп, ударов в минуту; больше нуля.
  /// @param beatsPerBar ударов в такте; больше нуля.
  explicit SteadyTimeline(double bpm, int beatsPerBar = 4)
      : secondsPerBeat_(60.0 / bpm), beatsPerBar_(beatsPerBar) {}

  double beatsAt(double time) const override { return time / secondsPerBeat_; }
  double timeAt(double beats) const override { return beats * secondsPerBeat_; }

  grid::BarBeat barBeat(std::int64_t beat) const override {
    return {.bar = static_cast<int>(beat / beatsPerBar_) + 1,
            .beat = static_cast<int>(beat % beatsPerBar_) + 1};
  }

private:
  double secondsPerBeat_;
  int beatsPerBar_;
};

/// Карта темпа, где темп растёт линейно по ударам от `from` до `to` за
/// `rampBeats` ударов, а дальше держится `to`. Размер — 4/4.
class RampTimeline : public grid::Timeline {
public:
  RampTimeline(double from, double to, double rampBeats)
      : from_(from), to_(to), rampBeats_(rampBeats) {}

  double timeAt(double beats) const override {
    // dt/db = 60 / bpm(b), bpm(b) = from + slope · b: интеграл — логарифм.
    const double slope = (to_ - from_) / rampBeats_;
    const double inside = std::min(beats, rampBeats_);
    double time = 60.0 / slope * std::log((from_ + slope * inside) / from_);
    if (beats > rampBeats_)
      time += (beats - rampBeats_) * 60.0 / to_;
    return time;
  }

  double beatsAt(double time) const override {
    const double slope = (to_ - from_) / rampBeats_;
    const double rampEnd = timeAt(rampBeats_);
    if (time > rampEnd)
      return rampBeats_ + (time - rampEnd) * to_ / 60.0;
    return (from_ * std::exp(time * slope / 60.0) - from_) / slope;
  }

  grid::BarBeat barBeat(std::int64_t beat) const override {
    return {.bar = static_cast<int>(beat / 4) + 1, .beat = static_cast<int>(beat % 4) + 1};
  }

private:
  double from_;
  double to_;
  double rampBeats_;
};

} // namespace training::test
