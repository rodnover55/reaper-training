#pragma once

// Карты темпа для тестов: постоянный темп, плавная смена темпа и такты с
// заданными размерами и сменами темпа; такты ядра по этим картам.

#include "training/core/clock.hpp"
#include "training/core/training/state/timeline.hpp"
#include "training/reaper/tempo_map.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace training::test {

/// Время шкалы через `seconds` секунд от начала проекта, с округлением до
/// наносекунды.
inline core::Moment timeOf(double seconds) {
  return core::Moment() + seconds * core::Interval::kSecond;
}

/// Секунды от начала проекта до времени шкалы `time`.
inline double secondsOf(core::Moment time) {
  return (time - core::Moment()) / core::Interval::kSecond;
}

/// Интервал в `seconds` секунд, с округлением до наносекунды.
inline core::Interval spanOf(double seconds) { return seconds * core::Interval::kSecond; }

/// Интервал в `ms` миллисекунд, с округлением до наносекунды.
inline core::Interval msOf(double ms) { return ms * core::Interval::kMillisecond; }

/// Карта темпа тестов, заданная ударами от начала проекта: удар 0 — начало
/// проекта, такты идут подряд.
///
/// Реализация задаёт перевод между секундами и ударами от начала проекта и
/// начала тактов в этих ударах.
class BeatTimeline : public reaper::TempoMap {
public:
  reaper::Position positionAt(core::Moment time) const override {
    const double beats = beatsAt(secondsOf(time));
    const std::int64_t bar = barAt(beats);
    return {.bar = bar, .beat = beats - static_cast<double>(firstBeat(bar))};
  }

  core::Moment timeAt(reaper::Position position) const override {
    return timeOf(secondsAt(static_cast<double>(firstBeat(position.bar)) + position.beat));
  }

  /// Время удара `beats` от начала проекта, дробного: так тесты задают места
  /// нот и позиций.
  core::Moment beatTime(double beats) const { return timeOf(secondsAt(beats)); }

protected:
  /// Удары от начала проекта, дробные, через `seconds` секунд от него.
  virtual double beatsAt(double seconds) const = 0;

  /// Секунды от начала проекта до удара `beats` от него.
  virtual double secondsAt(double beats) const = 0;

  /// Такт, в котором лежит удар `beats` от начала проекта.
  virtual std::int64_t barAt(double beats) const = 0;

  /// Первый удар такта `bar` от начала проекта.
  virtual std::int64_t firstBeat(std::int64_t bar) const = 0;
};

/// Карта темпа с постоянным темпом и числом ударов в такте; удар 0 — на 0 с.
class SteadyTimeline : public BeatTimeline {
public:
  /// @param bpm темп, ударов в минуту; больше нуля.
  /// @param beatsPerBar ударов в такте; больше нуля.
  explicit SteadyTimeline(double bpm, int beatsPerBar = 4)
      : bpm_(bpm), secondsPerBeat_(60.0 / bpm), beatsPerBar_(beatsPerBar) {}

  reaper::MapBar barOf(std::int64_t index) const override {
    return {.index = index,
            .meter = {.beats = beatsPerBar_, .unit = 4},
            .tempo = {{core::training::state::Beat(), bpm_}}};
  }

protected:
  double beatsAt(double seconds) const override { return seconds / secondsPerBeat_; }
  double secondsAt(double beats) const override { return beats * secondsPerBeat_; }

  std::int64_t barAt(double beats) const override {
    return static_cast<std::int64_t>(std::floor(beats / beatsPerBar_));
  }

  std::int64_t firstBeat(std::int64_t bar) const override { return bar * beatsPerBar_; }

private:
  double bpm_;
  double secondsPerBeat_;
  int beatsPerBar_;
};

/// Карта темпа, где темп растёт линейно по ударам от `from` до `to` за
/// `rampBeats` ударов, а дальше держится `to`. Размер — 4/4.
class RampTimeline : public BeatTimeline {
public:
  RampTimeline(double from, double to, double rampBeats)
      : from_(from), to_(to), rampBeats_(rampBeats) {}

  /// Темп такта — `to`: у плавного перехода с начала проекта темп становится
  /// темпом конца перехода.
  reaper::MapBar barOf(std::int64_t index) const override {
    return {.index = index, .meter = {}, .tempo = {{core::training::state::Beat(), to_}}};
  }

protected:
  double secondsAt(double beats) const override {
    // dt/db = 60 / bpm(b), bpm(b) = from + slope · b: интеграл — логарифм.
    const double slope = (to_ - from_) / rampBeats_;
    const double inside = std::min(beats, rampBeats_);
    double time = 60.0 / slope * std::log((from_ + slope * inside) / from_);
    if (beats > rampBeats_)
      time += (beats - rampBeats_) * 60.0 / to_;
    return time;
  }

  double beatsAt(double seconds) const override {
    const double slope = (to_ - from_) / rampBeats_;
    const double rampEnd = secondsAt(rampBeats_);
    if (seconds > rampEnd)
      return rampBeats_ + (seconds - rampEnd) * to_ / 60.0;
    return (from_ * std::exp(seconds * slope / 60.0) - from_) / slope;
  }

  std::int64_t barAt(double beats) const override {
    return static_cast<std::int64_t>(std::floor(beats / 4.0));
  }

  std::int64_t firstBeat(std::int64_t bar) const override { return bar * 4; }

private:
  double from_;
  double to_;
  double rampBeats_;
};

/// Такт карты `MapTimeline`.
struct BarPlan {
  /// Размер такта.
  core::training::state::Meter meter;

  /// Темп в начале такта, четвертей в минуту, как в REAPER.
  double bpm = 120.0;

  /// Смены темпа внутри такта: место в ударах от начала такта (больше нуля)
  /// и новый темп; по возрастанию места.
  std::vector<std::pair<double, double>> inside;
};

/// Карта темпа из тактов с заданными размерами и ступенчатыми сменами темпа;
/// удар 0 — на 0 с. После последнего заданного такта такты повторяют его
/// размер и темп, на котором он кончился; до начала проекта — размер и темп
/// первого такта.
class MapTimeline : public BeatTimeline {
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

  reaper::MapBar barOf(std::int64_t index) const override {
    if (index < 0)
      return {.index = index,
              .meter = plan_.front().meter,
              .tempo = {{core::training::state::Beat(), plan_.front().bpm}}};
    if (index >= static_cast<std::int64_t>(plan_.size())) {
      const BarPlan &last = plan_.back();
      const double end = last.inside.empty() ? last.bpm : last.inside.back().second;
      return {.index = index,
              .meter = last.meter,
              .tempo = {{core::training::state::Beat(), end}}};
    }

    const BarPlan &bar = plan_[static_cast<std::size_t>(index)];
    reaper::MapBar result{.index = index,
                          .meter = bar.meter,
                          .tempo = {{core::training::state::Beat(), bar.bpm}}};
    for (const auto &[at, tempo] : bar.inside)
      result.tempo.emplace(core::training::state::Beat(at), tempo);
    return result;
  }

protected:
  double secondsAt(double beats) const override {
    const Piece &piece = pieceBy([beats](const Piece &p) { return p.beat <= beats; });
    return piece.time + (beats - piece.beat) * piece.secondsPerBeat;
  }

  double beatsAt(double seconds) const override {
    const Piece &piece = pieceBy([seconds](const Piece &p) { return p.time <= seconds; });
    return piece.beat + (seconds - piece.time) / piece.secondsPerBeat;
  }

  std::int64_t barAt(double beats) const override {
    if (beats < 0.0)
      return static_cast<std::int64_t>(std::floor(beats / plan_.front().meter.beats));
    const auto end = static_cast<double>(endBeat_);
    if (beats >= end)
      return static_cast<std::int64_t>(plan_.size()) +
             static_cast<std::int64_t>(std::floor((beats - end) / plan_.back().meter.beats));

    std::size_t index = 0;
    while (index + 1 < plan_.size() && static_cast<double>(firstBeats_[index + 1]) <= beats)
      ++index;
    return static_cast<std::int64_t>(index);
  }

  std::int64_t firstBeat(std::int64_t bar) const override {
    if (bar < 0)
      return bar * plan_.front().meter.beats;
    const auto count = static_cast<std::int64_t>(plan_.size());
    if (bar >= count)
      return endBeat_ + (bar - count) * plan_.back().meter.beats;
    return firstBeats_[static_cast<std::size_t>(bar)];
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

/// Такт ядра — такт `bar` карты `timeline` с `perBeat` долями на удар: имя —
/// номер такта с 1, начало — первый удар такта, конец — первый удар
/// следующего.
inline core::training::state::Bar coreBar(const reaper::TempoMap &timeline, std::int64_t bar,
                                          int perBeat) {
  const reaper::MapBar map = timeline.barOf(bar);
  core::training::state::Bar result(std::to_string(bar + 1),
                                    timeline.timeAt({.bar = bar, .beat = 0.0}),
                                    timeline.timeAt({.bar = bar + 1, .beat = 0.0}), map.meter,
                                    map.tempo.at(core::training::state::Beat()));
  for (const auto &[beat, tempo] : map.tempo)
    result.changeTempo(beat, tempo);
  for (int node = 0; node < map.meter.beats * perBeat; ++node) {
    const double place = static_cast<double>(node) / perBeat;
    result.addDivision(core::training::state::Beat(place),
                       timeline.timeAt({.bar = bar, .beat = place}));
  }
  return result;
}

/// Такты ядра `from`…`to` включительно карты `timeline` с `perBeat` долями на
/// удар (`coreBar`), по порядку.
inline core::training::state::Timeline
coreBars(const reaper::TempoMap &timeline, std::int64_t from, std::int64_t to, int perBeat) {
  core::training::state::Timeline result;
  for (std::int64_t bar = from; bar <= to; ++bar)
    result.add(coreBar(timeline, bar, perBeat));
  return result;
}

} // namespace training::test
