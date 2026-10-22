#include "training/core/calibration/silence_step.hpp"

#include "levels.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace training::core::calibration {
namespace {

/// Сколько звука после запуска шаг не слушает: ВЧ-фильтр отзванивает скачок
/// постоянной составляющей.
constexpr Interval kSettle = 0.1 * Interval::kSecond;

/// Окно шага: у окна — наибольшая огибающая.
constexpr Interval kWindow = 0.1 * Interval::kSecond;

/// Звук в тишине: окно громче медианы окон шага больше чем на столько дБ.
/// Окна проверяются, когда их не меньше `kSoundFromWindow`: медиане нужно из
/// чего складываться.
constexpr double kSoundDb = 20.0;
constexpr std::size_t kSoundFromWindow = 5;

/// Медиана значений `values`; при чётном их числе — большая из двух средних.
///
/// @param values непустой набор; функция переставляет свою копию.
double median(std::vector<double> values) {
  const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
  std::ranges::nth_element(values, middle);
  return *middle;
}

} // namespace

SilenceStep::SilenceStep(detector::Detector &detector, SampleRate sampleRate)
    : detector_(detector), rate_(sampleRate), settleLength_(samplesOf(kSettle, sampleRate)),
      windowLength_(samplesOf(kWindow, sampleRate)),
      windowsPerStep_(static_cast<std::size_t>(std::llround(kLength / kWindow))) {
  windows_.reserve(windowsPerStep_);
}

std::expected<void, InvalidStep> SilenceStep::start() {
  const std::scoped_lock lock(mutex_);
  if (step_ != Step::Off)
    return std::unexpected(InvalidStep{});
  filter_.emplace(detector_.filter());
  heardEnd_.reset();
  step_ = Step::Loading;
  position_ = 0;
  windows_.clear();
  windowMax_ = 0.0;
  windowFill_ = 0;
  soundFrom_ = settleLength_;
  noiseDb_ = kFloorDb;
  lastLevel_ = 0.0;
  return {};
}

void SilenceStep::stop() {
  const std::scoped_lock lock(mutex_);
  step_ = Step::Off;
  filter_.reset();
  heardEnd_.reset();
  position_ = 0;
  windows_.clear();
  windowMax_ = 0.0;
  windowFill_ = 0;
  soundFrom_ = 0;
  noiseDb_ = kFloorDb;
  lastLevel_ = 0.0;
}

SilenceStep::State SilenceStep::result() const {
  const std::scoped_lock lock(mutex_);
  return current();
}

SilenceStep::State SilenceStep::current() const {
  State state;
  state.step = step_;
  state.noiseDb = noiseDb_;
  state.levelDb = decibels(lastLevel_);
  if (step_ == Step::Loading || step_ == Step::Working) {
    const double loudest =
        std::max(windowMax_, windows_.empty() ? 0.0 : std::ranges::max(windows_));
    state.noiseDb = decibels(loudest);
    const std::uint64_t heard = windows_.size() * windowLength_ + windowFill_;
    state.silenceLeft = lengthOf(windowsPerStep_ * windowLength_ - heard, rate_);
  } else if (step_ == Step::Done) {
    state.silenceLeft = Interval();
  }
  if (position_ > soundFrom_)
    state.sinceSound = lengthOf(position_ - soundFrom_, rate_);
  return state;
}

void SilenceStep::receive(const input::AudioBlock &block) {
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

void SilenceStep::process(std::span<const float> samples, detector::Serial &filter) {
  if (step_ == Step::Done)
    return;

  const std::span<double> envelopes = copied(samples, buffer_);
  filter.process(envelopes);
  lastLevel_ = 0.0;
  for (const double envelope : envelopes) {
    lastLevel_ = std::max(lastLevel_, envelope);
    if (position_++ < settleLength_)
      continue;
    step_ = Step::Working;

    windowMax_ = std::max(windowMax_, envelope);
    if (++windowFill_ == windowLength_) {
      closeWindow();
      if (step_ == Step::Done)
        return;
    }
  }
  if (position_ >= settleLength_)
    step_ = Step::Working;
}

void SilenceStep::closeWindow() {
  windows_.push_back(windowMax_);
  windowMax_ = 0.0;
  windowFill_ = 0;

  // Окна до последнего громкого и оно само отбрасываются: тишина
  // отсчитывается после звука. Так и затухание струны после удара не
  // начинает шаг заново ещё раз, когда стихнет.
  if (windows_.size() >= kSoundFromWindow) {
    const double loud = median(windows_) * std::pow(10.0, kSoundDb / 20.0);
    const auto last = std::ranges::find_if(windows_.rbegin(), windows_.rend(),
                                           [loud](double peak) { return peak > loud; });
    if (last != windows_.rend()) {
      // Время с последнего звука — от конца громкого окна: окна после него
      // уже засчитаны в тишину.
      windows_.erase(windows_.begin(), last.base());
      soundFrom_ = position_ - windows_.size() * windowLength_;
      return;
    }
  }

  if (windows_.size() == windowsPerStep_) {
    noiseDb_ = decibels(std::ranges::max(windows_));
    step_ = Step::Done;
  }
}

} // namespace training::core::calibration
