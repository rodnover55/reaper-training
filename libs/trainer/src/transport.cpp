#include "training/trainer/transport.hpp"

#include <utility>

namespace training::trainer {

Transport::Transport(Journal journal)
    : search_(run_, journal), midiNotes_(std::move(journal)) {}

bool Transport::start(Moment now, double position, const Compensation &compensation) {
  if (running())
    return false;

  compensation_ = compensation;
  bars_.clear();
  midiNotes_.start(position);
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

bool Transport::takesOnset(Moment blockMoment) const {
  const Moment startedAt = run_.load();
  return startedAt != kNoRun && blockMoment >= startedAt &&
         compensation_.audioSampleRate > 0.0;
}

} // namespace training::trainer
