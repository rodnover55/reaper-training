#include "training/trainer/count_in.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace training::trainer {

CountIn countInOf(const CountInSettings &settings, bool recording, double barSeconds,
                  double sinceBarStart, double sampleRate, int blockLength) {
  const bool enabled =
      settings.metronome && (recording ? settings.beforeRecording : settings.beforePlayback);
  if (!enabled || settings.bars <= 0.0 || sampleRate <= 0.0 || barSeconds <= 0.0)
    return {};

  double seconds = settings.bars * barSeconds;
  if (settings.fromBarStart)
    seconds += sinceBarStart;

  double samples = std::round(seconds * sampleRate);
  if (blockLength > 0) {
    const auto block = static_cast<double>(blockLength);
    samples = std::ceil(samples / block) * block;
  }
  return {.samples = static_cast<std::uint64_t>(samples),
          .barsSeconds = seconds,
          .sampleRate = sampleRate};
}

grid::Bar barAt(const grid::Timeline &timeline, double position, double sampleRate) {
  const double sample = sampleRate > 0.0 ? 1.0 / sampleRate : 0.0;
  return timeline.barOf(
      static_cast<std::int64_t>(std::floor(timeline.beatsAt(position + sample))));
}

CountIn countInAt(const CountInSettings &settings, bool recording,
                  const grid::Timeline &timeline, double position, double rate,
                  double sampleRate, int blockLength) {
  if (rate <= 0.0)
    return {};
  const double startBeats = timeline.beatsAt(position);
  const grid::Bar bar = barAt(timeline, position, sampleRate);
  const double beatSeconds = timeline.timeAt(startBeats + 1.0) - timeline.timeAt(startBeats);
  // Позиция до сэмпла перед началом своего такта: путь от него — ноль.
  const double sinceBarStart =
      std::max(0.0, position - timeline.timeAt(static_cast<double>(bar.firstBeat)));
  return countInOf(settings, recording, bar.meter.beats * beatSeconds / rate,
                   sinceBarStart / rate, sampleRate, blockLength);
}

CountInTimeline::CountInTimeline(const grid::Timeline &project, double start,
                                 double sampleRate)
    : project_(project), start_(start), startBeats_(project.beatsAt(start)),
      beatSeconds_(project.timeAt(startBeats_ + 1.0) - project.timeAt(startBeats_)),
      anchor_(barAt(project, start, sampleRate)) {}

double CountInTimeline::beatsAt(double time) const {
  if (time >= start_ || beatSeconds_ <= 0.0)
    return project_.beatsAt(time);
  return startBeats_ - (start_ - time) / beatSeconds_;
}

double CountInTimeline::timeAt(double beats) const {
  if (beats >= startBeats_)
    return project_.timeAt(beats);
  return start_ - (startBeats_ - beats) * beatSeconds_;
}

grid::Bar CountInTimeline::barOf(std::int64_t beat) const {
  if (beat >= anchor_.firstBeat)
    return project_.barOf(beat);
  const std::int64_t length = anchor_.meter.beats;
  const std::int64_t back = (anchor_.firstBeat - beat + length - 1) / length;
  return grid::Bar{.index = anchor_.index - back,
                   .firstBeat = anchor_.firstBeat - back * length,
                   .meter = anchor_.meter,
                   .changes = {}};
}

} // namespace training::trainer
