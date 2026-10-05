#pragma once

// Карты темпа для тестов сетки: постоянный темп, плавная смена темпа и такты
// с заданными размерами и сменами темпа.

#include "training/grid/timeline.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace training::test {

/// Номер такта, в котором удар `beat`, при одинаковых тактах по `beatsPerBar`
/// ударов; для отрицательных ударов такты тоже отрицательные.
inline std::int64_t barIndexOf(std::int64_t beat, int beatsPerBar) {
  const std::int64_t length = beatsPerBar;
  return beat >= 0 ? beat / length : -((-beat + length - 1) / length);
}

/// Карта темпа с постоянным темпом и числом ударов в такте; удар 0 — на 0 с.
class SteadyTimeline : public grid::Timeline {
public:
  /// @param bpm темп, ударов в минуту; больше нуля.
  /// @param beatsPerBar ударов в такте; больше нуля.
  explicit SteadyTimeline(double bpm, int beatsPerBar = 4)
      : secondsPerBeat_(60.0 / bpm), beatsPerBar_(beatsPerBar) {}

  double beatsAt(double time) const override { return time / secondsPerBeat_; }
  double timeAt(double beats) const override { return beats * secondsPerBeat_; }

  grid::Bar barOf(std::int64_t beat) const override {
    const std::int64_t index = barIndexOf(beat, beatsPerBar_);
    return {.index = index,
            .firstBeat = index * beatsPerBar_,
            .meter = {.beats = beatsPerBar_, .unit = 4},
            .changes = {}};
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

  grid::Bar barOf(std::int64_t beat) const override {
    const std::int64_t index = barIndexOf(beat, 4);
    return {.index = index, .firstBeat = index * 4, .meter = {}, .changes = {}};
  }

private:
  double from_;
  double to_;
  double rampBeats_;
};

/// Такт карты `MapTimeline`.
struct BarPlan {
  /// Размер такта.
  grid::Meter meter;

  /// Темп в начале такта, четвертей в минуту, как в REAPER.
  double bpm = 120.0;

  /// Смены темпа внутри такта: место в ударах от начала такта (больше нуля)
  /// и новый темп; по возрастанию места.
  std::vector<std::pair<double, double>> inside;
};

/// Карта темпа из тактов с заданными размерами и ступенчатыми сменами темпа;
/// удар 0 — на 0 с. После последнего заданного такта такты повторяют его
/// размер и темп, на котором он кончился.
class MapTimeline : public grid::Timeline {
public:
  /// @param plan такты по порядку с начала проекта; не пустой.
  explicit MapTimeline(std::vector<BarPlan> plan) : plan_(std::move(plan)) {
    double beat = 0.0;
    double time = 0.0;
    for (const BarPlan &bar : plan_) {
      firstBeats_.push_back(static_cast<std::int64_t>(beat));
      double bpm = bar.bpm;
      double from = 0.0;
      // Куски постоянного темпа: от начала такта до смены, между сменами и
      // от последней смены до конца такта.
      auto add = [&](double to) {
        const double secondsPerBeat = 60.0 / bpm * 4.0 / bar.meter.unit;
        pieces_.push_back(
            {.beat = beat + from, .time = time, .secondsPerBeat = secondsPerBeat});
        time += (to - from) * secondsPerBeat;
        from = to;
      };
      for (const auto &[at, tempo] : bar.inside) {
        add(at);
        bpm = tempo;
      }
      add(bar.meter.beats);
      beat += bar.meter.beats;
    }
    endBeat_ = static_cast<std::int64_t>(beat);
  }

  double timeAt(double beats) const override {
    const Piece &piece = pieceBy([beats](const Piece &p) { return p.beat <= beats; });
    return piece.time + (beats - piece.beat) * piece.secondsPerBeat;
  }

  double beatsAt(double time) const override {
    const Piece &piece = pieceBy([time](const Piece &p) { return p.time <= time; });
    return piece.beat + (time - piece.time) / piece.secondsPerBeat;
  }

  grid::Bar barOf(std::int64_t beat) const override {
    const BarPlan &last = plan_.back();
    if (beat >= endBeat_) {
      // За последним заданным тактом — его повторения без смен.
      const std::int64_t extra = (beat - endBeat_) / last.meter.beats;
      return {.index = static_cast<std::int64_t>(plan_.size()) + extra,
              .firstBeat = endBeat_ + extra * last.meter.beats,
              .meter = last.meter,
              .changes = {}};
    }

    std::size_t index = 0;
    while (index + 1 < plan_.size() && firstBeats_[index + 1] <= beat)
      ++index;
    const BarPlan &bar = plan_[index];

    grid::Bar result{.index = static_cast<std::int64_t>(index),
                     .firstBeat = firstBeats_[index],
                     .meter = bar.meter,
                     .changes = {}};
    if (index > 0) {
      const BarPlan &previous = plan_[index - 1];
      const double previousEnd =
          previous.inside.empty() ? previous.bpm : previous.inside.back().second;
      grid::Change change;
      if (previous.meter != bar.meter) {
        change.meterFrom = previous.meter;
        change.meterTo = bar.meter;
      }
      if (previousEnd != bar.bpm) {
        change.tempoFrom = previousEnd;
        change.tempoTo = bar.bpm;
      }
      if (change.meterFrom || change.tempoFrom)
        result.changes.push_back(change);
    }

    double tempo = bar.bpm;
    for (const auto &[at, next] : bar.inside) {
      grid::Change change;
      change.beat = at;
      change.tempoFrom = tempo;
      change.tempoTo = next;
      result.changes.push_back(change);
      tempo = next;
    }
    return result;
  }

private:
  /// Кусок постоянного темпа.
  struct Piece {
    double beat;
    double time;
    double secondsPerBeat;
  };

  /// Последний кусок, для которого `starts` правда, или первый, если такого
  /// нет: до начала и после конца карты темп продолжается.
  template <class Starts> const Piece &pieceBy(Starts starts) const {
    const Piece *found = &pieces_.front();
    for (const Piece &piece : pieces_)
      if (starts(piece))
        found = &piece;
    return *found;
  }

  std::vector<BarPlan> plan_;
  std::vector<std::int64_t> firstBeats_;
  std::vector<Piece> pieces_;
  std::int64_t endBeat_ = 0;
};

} // namespace training::test
