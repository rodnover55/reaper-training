#include "training/onset/detector.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace training::onset {
namespace {

/// С чем сравнивается огибающая: с её максимумом от `kBackOldestMs` до
/// `kBackNewestMs` назад. Окно длиннее периода самой низкой ноты гитары — си
/// 62 Гц, 16 мс.
constexpr double kBackOldestMs = 25.0;
constexpr double kBackNewestMs = 5.0;

/// Окно энергии, мс: тоже не короче периода самой низкой ноты, чтобы звон
/// струны давал в окне почти одну и ту же энергию.
constexpr double kEnergyMs = 20.0;

/// За сколько миллисекунд огибающая спадает в e раз, когда звук стихает.
constexpr double kReleaseMs = 10.0;

double samplesOf(double milliseconds, double sampleRate) {
  return milliseconds * sampleRate / 1000.0;
}

/// Длина в сэмплах, не меньше одного.
std::uint64_t lengthOf(double milliseconds, double sampleRate) {
  return static_cast<std::uint64_t>(
      std::max(1LL, std::llround(samplesOf(milliseconds, sampleRate))));
}

/// Номер сэмпла `length` назад от `index`; до начала потока — 0.
std::uint64_t before(std::uint64_t index, std::uint64_t length) {
  return index > length ? index - length : 0;
}

/// Точка, где огибающая пересекла уровень, линейно между прошлым сэмплом и
/// сэмплом `index`.
double crossing(std::uint64_t index, double level, double previous, double envelope) {
  return static_cast<double>(index) - 1.0 + (level - previous) / (envelope - previous);
}

} // namespace

Detector::Detector(const Settings &settings)
    : release_(std::exp(-1.0 / samplesOf(kReleaseMs, settings.sampleRate))),
      threshold_(std::pow(10.0, settings.silenceDb / 20.0)), ratio_(settings.ratio),
      energyRatio_(settings.energyRatio),
      deadTime_(static_cast<std::uint64_t>(
          std::llround(samplesOf(settings.deadTimeMs, settings.sampleRate)))),
      backOldest_(std::max(lengthOf(kBackOldestMs, settings.sampleRate),
                           lengthOf(kBackNewestMs, settings.sampleRate))),
      backNewest_(lengthOf(kBackNewestMs, settings.sampleRate)),
      energyLength_(lengthOf(kEnergyMs, settings.sampleRate)),
      recentEnvelope_(static_cast<std::size_t>(backNewest_), 0.0),
      envelopeMax_(static_cast<std::size_t>(backOldest_ - backNewest_ + 1)),
      squares_(static_cast<std::size_t>(energyLength_), 0.0),
      recentEnergy_(static_cast<std::size_t>(energyLength_), 0.0),
      energyMax_(static_cast<std::size_t>(energyLength_ + 1)),
      rises_(static_cast<std::size_t>(2 * energyLength_)),
      energyArmedFrom_(3 * energyLength_) {
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
  energyArmedFrom_ = 3 * energyLength_;
  std::ranges::fill(recentEnvelope_, 0.0);
  envelopeMax_.clear();
  std::ranges::fill(squares_, 0.0);
  squaresSum_ = 0.0;
  std::ranges::fill(recentEnergy_, 0.0);
  energyMax_.clear();
  rises_.clear();
  rising_ = false;
  energyPending_ = false;
}

void Detector::setSilenceDb(double silenceDb) {
  threshold_ = std::pow(10.0, silenceDb / 20.0);
}

void Detector::setEnergyRatio(double energyRatio) { energyRatio_ = energyRatio; }

void Detector::process(std::span<const float> samples, std::vector<Onset> &onsets) {
  for (const float sample : samples)
    processSample(static_cast<double>(sample), onsets);
}

void Detector::processSample(double sample, std::vector<Onset> &onsets) {
  const double filtered = b0_ * sample + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_;
  const double slope = filtered - y1_;
  x2_ = x1_;
  x1_ = sample;
  y2_ = y1_;
  y1_ = filtered;

  // Огибающая поднимается сразу, спадает плавно.
  const double previous = envelope_;
  envelope_ = std::max(std::abs(filtered), envelope_ * release_);

  const std::uint64_t index = position_++;

  // Энергия верха спектра — сумма квадратов разности соседних сэмплов за
  // последние 20 мс: разность усиливает звук пропорционально частоте.
  // Квадрат этого сэмпла входит в окно, квадрат сэмпла 20 мс назад из него
  // выходит. Сравниваются только отношения энергий, поэтому делить на длину
  // окна незачем.
  const auto energySlot = static_cast<std::size_t>(index % energyLength_);
  const double square = slope * slope;
  squaresSum_ += square - squares_[energySlot];
  squares_[energySlot] = square;
  const double energy = std::max(squaresSum_, 0.0);

  // Энергия окна, кончившегося 20 мс назад, становится опорой: максимум по
  // окнам, кончающимся от 40 до 20 мс назад, — окна, не задевающие текущее.
  const double agedEnergy = recentEnergy_[energySlot];
  recentEnergy_[energySlot] = energy;
  if (index >= energyLength_) {
    energyMax_.dropBefore(before(index, 2 * energyLength_));
    energyMax_.push({.index = index - energyLength_, .value = agedEnergy});
  }

  // В ячейке этого сэмпла лежит огибающая сэмпла, отстоящего на длину
  // кольца, — она переходит в окно максимума. После записи кольцо держит
  // огибающую последних 5 мс.
  const auto backSlot = static_cast<std::size_t>(index % backNewest_);
  const double agedEnvelope = recentEnvelope_[backSlot];
  recentEnvelope_[backSlot] = envelope_;

  // Пока окну нечего сравнивать, в кольце лежат не сэмплы потока, а нули.
  if (index < backNewest_)
    return;

  envelopeMax_.dropBefore(before(index, backOldest_));
  envelopeMax_.push({.index = index - backNewest_, .value = agedEnvelope});
  const double reference = envelopeMax_.max().value;

  // Подъём огибающей над максимумом за 25 мс — кандидат в начало атаки.
  // Начинается пересечением уровня снизу, сила — наибольшее отношение
  // огибающей к уровню за подъём. Подъёмы, начавшиеся в мёртвое время, не в
  // счёт.
  const double riseLevel = std::max(threshold_, reference);
  rises_.dropBefore(before(index + 1, 2 * energyLength_));
  if (envelope_ < riseLevel) {
    rising_ = false;
  } else if (!rising_ && previous < riseLevel) {
    rising_ = true;
    rise_ = {.index = index,
             .value = envelope_ / riseLevel,
             .position = crossing(index, riseLevel, previous, envelope_),
             .envelope = envelope_};
    if (rise_.index >= armedFrom_)
      rises_.push(rise_);
  } else if (rising_ && envelope_ / riseLevel > rise_.value) {
    rise_.value = envelope_ / riseLevel;
    rise_.envelope = envelope_;
    if (rise_.index >= armedFrom_ && rise_.index + 2 * energyLength_ > index)
      rises_.push(rise_);
  }

  if (index < armedFrom_)
    return;

  // Резкая атака: огибающая пересекла уровень снизу. Начало — там, где начался
  // этот подъём над максимумом: у шумной атаки первый сэмпл выше уровня
  // приходит позже начала. Рост сравнивается за последние 5 мс, и подъём,
  // начавшийся раньше, — медленное разгорание, а не начало этой атаки.
  const double level = std::max(threshold_, ratio_ * reference);
  if (envelope_ >= level && previous < level) {
    if (rising_ && rise_.index >= armedFrom_ && rise_.index + backNewest_ >= index)
      report(rise_.index, rise_.position, envelope_, onsets);
    else
      report(index, crossing(index, level, previous, envelope_), envelope_, onsets);
    return;
  }

  // Удар по звучащей струне: энергия выросла в `energyRatio` раз. Верх спектра от
  // медиатора приходит раньше щелчка, поэтому решение ждёт ещё одно окно, и
  // начало — самый сильный подъём огибающей за 20 мс до и после роста.
  // Энергия без подъёма огибающей — не удар.
  if (!energyPending_ && index >= energyArmedFrom_ &&
      energy > energyRatio_ * energyMax_.max().value) {
    energyPending_ = true;
    energyDecision_ = index + energyLength_;
  }

  if (energyPending_ && index >= energyDecision_) {
    energyPending_ = false;
    if (!rises_.empty()) {
      const Rise strongest = rises_.max();
      report(strongest.index, strongest.position, strongest.envelope, onsets);
    }
  }
}

void Detector::report(std::uint64_t index, double position, double envelope,
                      std::vector<Onset> &onsets) {
  onsets.push_back({.position = position, .levelDb = 20.0 * std::log10(envelope)});

  // Окна энергии второго пути — текущее и два опорных — должны лечь целиком
  // после атаки.
  armedFrom_ = index + deadTime_;
  energyArmedFrom_ = std::max(armedFrom_, index + 3 * energyLength_);
  rises_.clear();
  energyPending_ = false;
}

} // namespace training::onset
