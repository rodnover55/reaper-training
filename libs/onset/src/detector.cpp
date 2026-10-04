#include "training/onset/detector.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace training::onset {
namespace {

/// С чем сравнивается огибающая: с ней самой столько миллисекунд назад.
constexpr double kBackMs = 5.0;

/// За сколько миллисекунд огибающая спадает в e раз, когда звук стихает.
constexpr double kReleaseMs = 10.0;

double samplesOf(double milliseconds, double sampleRate) {
  return milliseconds * sampleRate / 1000.0;
}

} // namespace

Detector::Detector(const Settings &settings)
    : release_(std::exp(-1.0 / samplesOf(kReleaseMs, settings.sampleRate))),
      threshold_(std::pow(10.0, settings.silenceDb / 20.0)), ratio_(settings.ratio),
      deadTime_(static_cast<std::uint64_t>(
          std::llround(samplesOf(settings.deadTimeMs, settings.sampleRate)))),
      history_(static_cast<std::size_t>(
                   std::max(1LL, std::llround(samplesOf(kBackMs, settings.sampleRate)))) +
                   1,
               0.0) {
  // ВЧ-фильтр второго порядка по Баттерворту, формулы RBJ: alpha = sin(w0) / 2Q,
  // а при Q = 1/√2 знаменатель 2Q равен √2.
  const double w0 = 2.0 * std::numbers::pi * settings.highPassHz / settings.sampleRate;
  const double cosW0 = std::cos(w0);
  const double alpha = std::sin(w0) / std::numbers::sqrt2;
  const double a0 = 1.0 + alpha;

  b0_ = (1.0 + cosW0) / 2.0 / a0;
  b1_ = -(1.0 + cosW0) / a0;
  b2_ = b0_;
  a1_ = -2.0 * cosW0 / a0;
  a2_ = (1.0 - alpha) / a0;
}

void Detector::reset() {
  x1_ = x2_ = y1_ = y2_ = 0.0;
  envelope_ = 0.0;
  position_ = 0;
  armedFrom_ = 0;
  std::ranges::fill(history_, 0.0);
}

void Detector::setSilenceDb(double silenceDb) {
  threshold_ = std::pow(10.0, silenceDb / 20.0);
}

void Detector::process(std::span<const float> samples, std::vector<Onset> &onsets) {
  for (const float sample : samples)
    processSample(static_cast<double>(sample), onsets);
}

void Detector::processSample(double sample, std::vector<Onset> &onsets) {
  const double filtered = b0_ * sample + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_;
  x2_ = x1_;
  x1_ = sample;
  y2_ = y1_;
  y1_ = filtered;

  // Огибающая поднимается сразу, спадает плавно.
  const double previous = envelope_;
  envelope_ = std::max(std::abs(filtered), envelope_ * release_);

  // В ячейке этого сэмпла лежит огибающая сэмпла, отстоящего на длину
  // кольца, — она и есть «5 мс назад». После записи кольцо держит огибающую
  // последних 5 мс.
  const std::uint64_t index = position_++;
  const std::size_t size = history_.size();
  const double back = history_[static_cast<std::size_t>(index % size)];
  history_[static_cast<std::size_t>(index % size)] = envelope_;

  const double level = std::max(threshold_, ratio_ * back);
  if (index < size || index < armedFrom_ || envelope_ < level || previous >= level)
    return;

  // Звук нарос: огибающая пересекла уровень атаки. Начало — точка
  // пересечения, линейно между прошлым сэмплом и этим.
  const double start =
      static_cast<double>(index) - 1.0 + (level - previous) / (envelope_ - previous);

  onsets.push_back({.position = start, .levelDb = 20.0 * std::log10(envelope_)});
  armedFrom_ = index + deadTime_;
}

} // namespace training::onset
