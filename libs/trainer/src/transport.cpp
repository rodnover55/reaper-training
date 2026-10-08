#include "training/trainer/transport.hpp"

namespace training::trainer {

bool Transport::start(double now, const Compensation &compensation) {
  if (running_)
    return false;

  running_ = true;
  startedAt_ = now;
  compensation_ = compensation;
  bars_.clear();
  return true;
}

bool Transport::stop() {
  if (!running_)
    return false;

  running_ = false;
  bars_.stop();
  return true;
}

bool Transport::takesOnset(double blockMoment) const {
  return running_ && blockMoment >= startedAt_ && compensation_.audioSampleRate > 0.0;
}

} // namespace training::trainer
