#include "training/reaper/count_in.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>

namespace training::reaper {
namespace {

/// Доля позиции `at` от начала такта `bar`, ударов, по карте `timeline`:
/// позиция лежит в этом такте или в такте прямо перед ним.
double beatFrom(const TempoMap &timeline, const MapBar &bar, Position at) {
  if (at.bar == bar.index)
    return at.beat;
  return at.beat - timeline.barOf(at.bar).meter.beats;
}

} // namespace

CountIn countInOf(const CountInSettings &settings, bool recording, core::Interval barLength,
                  core::Interval sinceBarStart, double sampleRate, int blockLength) {
  const bool enabled =
      settings.metronome && (recording ? settings.beforeRecording : settings.beforePlayback);
  if (!enabled || settings.bars <= 0.0 || sampleRate <= 0.0 || barLength <= core::Interval())
    return {};

  core::Interval length = barLength * settings.bars;
  if (settings.fromBarStart)
    length += sinceBarStart;

  double samples = std::round(length / core::Interval::kSecond * sampleRate);
  if (blockLength > 0) {
    const auto block = static_cast<double>(blockLength);
    samples = std::ceil(samples / block) * block;
  }
  return {.samples = static_cast<std::uint64_t>(samples),
          .barsLength = length,
          .sampleRate = sampleRate};
}

MapBar barAt(const TempoMap &timeline, core::Moment position, double sampleRate) {
  const core::Interval sample =
      sampleRate > 0.0 ? core::Interval::kSecond / sampleRate : core::Interval();
  return timeline.barOf(timeline.positionAt(position + sample).bar);
}

CountIn countInAt(const CountInSettings &settings, bool recording, const TempoMap &timeline,
                  core::Moment position, double rate, double sampleRate, int blockLength) {
  if (rate <= 0.0)
    return {};
  const Position at = timeline.positionAt(position);
  const MapBar bar = barAt(timeline, position, sampleRate);
  const core::Interval beatLength =
      timeline.timeAt({.bar = at.bar, .beat = at.beat + 1.0}) - timeline.timeAt(at);
  // Позиция до сэмпла перед началом своего такта: путь от него — ноль.
  const core::Interval sinceBarStart =
      std::max(core::Interval(), position - timeline.timeAt({.bar = bar.index, .beat = 0.0}));
  return countInOf(settings, recording, beatLength * bar.meter.beats / rate,
                   sinceBarStart / rate, sampleRate, blockLength);
}

CountInTimeline::CountInTimeline(const TempoMap &project, core::Moment start,
                                 double sampleRate)
    : project_(project), start_(start), anchor_(barAt(project, start, sampleRate)) {
  const Position at = project.positionAt(start);
  startBeat_ = beatFrom(project, anchor_, at);
  beatLength_ = project.timeAt({.bar = at.bar, .beat = at.beat + 1.0}) - project.timeAt(at);

  // Темп, который в такте позиции начала действует на ней: значение последней
  // доли темпа не дальше позиции начала, а до доли 0 — значение доли 0.
  const auto after = anchor_.tempo.upper_bound(core::training::state::Beat(startBeat_));
  tempo_ = after == anchor_.tempo.begin() ? after->second : std::prev(after)->second;
}

Position CountInTimeline::positionAt(core::Moment time) const {
  if (time >= start_ || beatLength_ <= core::Interval())
    return project_.positionAt(time);

  // Доли от начала такта позиции начала назад, такты отсчёта — по его размеру.
  const double beats = startBeat_ - (start_ - time) / beatLength_;
  const auto length = static_cast<double>(anchor_.meter.beats);
  const double back = std::floor(beats / length);
  return {.bar = anchor_.index + static_cast<std::int64_t>(back),
          .beat = beats - back * length};
}

core::Moment CountInTimeline::timeAt(Position position) const {
  if (position.bar > anchor_.index)
    return project_.timeAt(position);

  // Доли от начала такта позиции начала: такты отсчёта — по его размеру.
  const auto bars = static_cast<double>(position.bar - anchor_.index);
  const double beats = bars * anchor_.meter.beats + position.beat;
  if (beats >= startBeat_)
    return project_.timeAt({.bar = anchor_.index, .beat = beats});
  return start_ - beatLength_ * (startBeat_ - beats);
}

MapBar CountInTimeline::barOf(std::int64_t index) const {
  if (index >= anchor_.index)
    return project_.barOf(index);
  return MapBar{.index = index,
                .meter = anchor_.meter,
                .tempo = {{core::training::state::Beat(), tempo_}}};
}

} // namespace training::reaper
