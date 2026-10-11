#include "training/trainer/transport.hpp"

#include <utility>

namespace training::trainer {

Transport::Transport(Journal journal)
    : search_(run_, leadIn_, journal), midiNotes_(std::move(journal)) {}

bool Transport::start(Moment now, double position, const Compensation &compensation,
                      const CountIn &countIn, double rate,
                      std::optional<std::int64_t> leadInEnd) {
  if (running())
    return false;

  compensation_ = compensation;
  countIn_ = countIn;
  runStart_ = position - countIn.seconds() * rate;
  barsStart_ = position - countIn.barsSeconds * rate;
  reachedBarsStart_ = false;
  bars_.clear();
  bars_.setLeadIn(leadInEnd);
  midiNotes_.start(position, countIn.seconds() * rate);
  // Начало и отсчёт — до момента начала: поиск атак читает их после него.
  leadIn_.position.store(position);
  leadIn_.countIn.store(countIn.samples);
  leadIn_.rate.store(rate);
  run_.store(now);
  return true;
}

bool Transport::stop() {
  if (!running())
    return false;

  run_.store(kNoRun);
  bars_.stop();
  return true;
}

bool Transport::play(double beats, double loopBeats, const grid::HitWindow &window,
                     const grid::Timeline &timeline) {
  if (!reachedBarsStart_) {
    const double barsStart = timeline.beatsAt(barsStart_);
    if (beats < barsStart)
      beats = barsStart;
    else
      reachedBarsStart_ = true;
  }
  return bars_.play(beats, loopBeats, window, timeline);
}

bool Transport::takesOnset(Moment onsetRun) const {
  const Moment startedAt = run_.load();
  return startedAt != kNoRun && onsetRun == startedAt && compensation_.audioSampleRate > 0.0;
}

} // namespace training::trainer
