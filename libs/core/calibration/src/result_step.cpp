#include "training/core/calibration/result_step.hpp"

#include "levels.hpp"

namespace training::core::calibration {

ResultStep::ResultStep(detector::Detector &detector, SampleRate sampleRate, Interval waitTime)
    : detector_(detector), rate_(sampleRate), waitTime_(waitTime) {}

ResultStep::Step ResultStep::step() const {
  const std::scoped_lock lock(mutex_);
  return step_;
}

double ResultStep::thresholdDb() const {
  const std::scoped_lock lock(mutex_);
  return thresholdDb_;
}

std::expected<void, InvalidStep> ResultStep::start(double thresholdDb) {
  const std::scoped_lock lock(mutex_);
  if (step_ != Step::Off)
    return std::unexpected(InvalidStep{});
  filter_.emplace(detector_.filter());
  heardEnd_.reset();
  thresholdDb_ = thresholdDb;
  threshold_ = amplitudeOf(thresholdDb);
  position_ = 0;
  step_ = Step::Loading;
  return {};
}

void ResultStep::stop() {
  const std::scoped_lock lock(mutex_);
  filter_.reset();
  heardEnd_.reset();
  position_ = 0;
  step_ = Step::Off;
}

void ResultStep::receive(const input::AudioBlock &block) {
  const std::scoped_lock lock(mutex_);
  if (block.samples.empty() || !filter_ || step_ == Step::Off || step_ == Step::Done)
    return;

  // Пропущенный между блоками звук — тишина.
  detector::Serial &filter = *filter_;
  silence(missedOf(heardEnd_, block, rate_),
          [this, &filter](std::span<const float> zeros) { process(zeros, filter); });
  heardEnd_ = endOf(block, rate_);
  process(block.samples, filter);
}

void ResultStep::process(std::span<const float> samples, detector::Serial &filter) {
  if (step_ == Step::Done)
    return;

  // Фильтр идёт и во время ожидания: к началу прослушивания он прогрет.
  const std::span<double> envelopes = copied(samples, buffer_);
  filter.process(envelopes);
  const std::uint64_t wait = samplesOf(waitTime_, rate_);
  for (const double envelope : envelopes) {
    if (position_++ < wait)
      continue;
    step_ = Step::Working;
    if (envelope > threshold_) {
      step_ = Step::Done;
      return;
    }
  }
  if (position_ >= wait)
    step_ = Step::Working;
}

} // namespace training::core::calibration
