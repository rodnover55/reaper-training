#include "training/core/detector/detector.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>
#include <vector>

namespace training::core::detector {
namespace {

/// С чем сравнивается огибающая: с её максимумом от `kBackOldest` до
/// `kBackNewest` назад. Окно длиннее периода самой низкой ноты гитары — си
/// 62 Гц, 16 мс.
constexpr core::Interval kBackOldest = 25 * core::Interval::kMillisecond;
constexpr core::Interval kBackNewest = 5 * core::Interval::kMillisecond;

/// Окно энергии: тоже хотя бы с период самой низкой ноты, чтобы звон струны
/// давал в окне почти одну и ту же энергию.
constexpr core::Interval kEnergy = 20 * core::Interval::kMillisecond;

/// Длина `length` в сэмплах при частоте `sampleRate`, без округления.
double samplesOf(core::Interval length, core::SampleRate sampleRate) {
  return length / core::Interval::kSecond * sampleRate.hz();
}

/// Длина в сэмплах, хотя бы один сэмпл.
std::uint64_t lengthOf(core::Interval length, core::SampleRate sampleRate) {
  return static_cast<std::uint64_t>(
      std::max(1LL, std::llround(samplesOf(length, sampleRate))));
}

/// Окно максимума огибающей в сэмплах: от `kBackOldest` до `kBackNewest`
/// назад включительно, хотя бы один сэмпл.
std::size_t envelopeWindowOf(core::SampleRate sampleRate) {
  const std::uint64_t newest = lengthOf(kBackNewest, sampleRate);
  return static_cast<std::size_t>(std::max(lengthOf(kBackOldest, sampleRate), newest) -
                                  newest + 1);
}

/// Окно максимума энергии в сэмплах: окно энергии и ещё один сэмпл.
std::size_t energyWindowOf(core::SampleRate sampleRate) {
  return static_cast<std::size_t>(lengthOf(kEnergy, sampleRate) + 1);
}

/// Точка, где огибающая пересекла уровень, линейно между прошлым сэмплом и
/// сэмплом `index`.
double crossing(std::uint64_t index, double level, double previous, double envelope) {
  return static_cast<double>(index) - 1.0 + (level - previous) / (envelope - previous);
}

} // namespace

std::optional<Detector> Detector::create(const Settings &settings) {
  std::optional<HighPass> highPass =
      HighPass::create(settings.highPassHz, settings.sampleRate);
  // Окна — хотя бы на один сэмпл и создаются всегда.
  std::optional<SlidingMax<Magnitude>> envelopeMax =
      SlidingMax<Magnitude>::create(envelopeWindowOf(settings.sampleRate));
  std::optional<SlidingMax<Magnitude>> energyMax =
      SlidingMax<Magnitude>::create(energyWindowOf(settings.sampleRate));
  if (!highPass || !envelopeMax || !energyMax)
    return std::nullopt;
  return Detector(settings, std::move(*highPass), std::move(*envelopeMax),
                  std::move(*energyMax));
}

Detector::Detector(const Settings &settings, HighPass hpf, SlidingMax<Magnitude> envelopeMax,
                   SlidingMax<Magnitude> energyMax)
    : onsets_(kOnsetCapacity), settings_(settings), hpf_(std::move(hpf)),
      envelope_(settings.sampleRate), threshold_(std::pow(10.0, settings.silenceDb / 20.0)),
      ratio_(settings.ratio), energyRatio_(settings.energyRatio),
      deadTime_(static_cast<std::uint64_t>(
          std::llround(samplesOf(settings.deadTime, settings.sampleRate)))),
      backOldest_(std::max(lengthOf(kBackOldest, settings.sampleRate),
                           lengthOf(kBackNewest, settings.sampleRate))),
      backNewest_(lengthOf(kBackNewest, settings.sampleRate)),
      energyLength_(lengthOf(kEnergy, settings.sampleRate)), filtered_(kBlock),
      envelopes_(kBlock), energies_(kBlock), envelopeMaxima_(kBlock), energyMaxima_(kBlock),
      recentEnvelope_(static_cast<std::size_t>(backNewest_), 0.0),
      envelopeMax_(std::move(envelopeMax)),
      squares_(static_cast<std::size_t>(energyLength_), 0.0),
      recentEnergy_(static_cast<std::size_t>(energyLength_), 0.0),
      energyMax_(std::move(energyMax)), rises_(static_cast<std::size_t>(2 * energyLength_)),
      energyArmedFrom_(3 * energyLength_) {}

Detector::Detector(Detector &&other) noexcept
    : Detector(std::move(other), std::scoped_lock(other.mutex_)) {}

Detector::Detector(Detector &&other, const std::scoped_lock<std::mutex> & /*lock*/)
    : onsets_(std::move(other.onsets_)), settings_(other.settings_),
      hpf_(std::move(other.hpf_)), envelope_(std::move(other.envelope_)),
      threshold_(other.threshold_), ratio_(other.ratio_), energyRatio_(other.energyRatio_),
      deadTime_(other.deadTime_), backOldest_(other.backOldest_),
      backNewest_(other.backNewest_), energyLength_(other.energyLength_),
      filtered_(std::move(other.filtered_)), envelopes_(std::move(other.envelopes_)),
      energies_(std::move(other.energies_)), envelopeMaxima_(std::move(other.envelopeMaxima_)),
      energyMaxima_(std::move(other.energyMaxima_)), recentEnvelope_(other.recentEnvelope_),
      envelopeMax_(std::move(other.envelopeMax_)), squares_(other.squares_),
      squaresSum_(other.squaresSum_), recentEnergy_(other.recentEnergy_),
      energyMax_(std::move(other.energyMax_)), rises_(std::move(other.rises_)),
      rise_(other.rise_), rising_(other.rising_), energyPending_(other.energyPending_),
      energyDecision_(other.energyDecision_), lastFiltered_(other.lastFiltered_),
      lastEnvelope_(other.lastEnvelope_), position_(other.position_),
      armedFrom_(other.armedFrom_), energyArmedFrom_(other.energyArmedFrom_) {}

Settings Detector::settings() const {
  const std::scoped_lock lock(mutex_);
  return settings_;
}

void Detector::setSilenceDb(double silenceDb) {
  const std::scoped_lock lock(mutex_);
  settings_.silenceDb = silenceDb;
  threshold_ = std::pow(10.0, silenceDb / 20.0);
}

void Detector::reset() {
  const std::scoped_lock lock(mutex_);
  restart();
  onsets_.clear();
}

void Detector::restart() {
  hpf_.reset();
  envelope_.reset();
  lastFiltered_ = 0.0;
  lastEnvelope_ = 0.0;
  position_ = 0;
  armedFrom_ = 0;
  energyArmedFrom_ = 3 * energyLength_;
  std::ranges::fill(recentEnvelope_, 0.0);
  envelopeMax_.reset();
  std::ranges::fill(squares_, 0.0);
  squaresSum_ = 0.0;
  std::ranges::fill(recentEnergy_, 0.0);
  energyMax_.reset();
  rises_.clear();
  rising_ = false;
  energyPending_ = false;
}

void Detector::skip(std::uint64_t count) {
  const std::scoped_lock lock(mutex_);
  // Пропуск — тишина: его сэмплы разбираются как нули.
  while (count > 0) {
    const auto length = static_cast<std::size_t>(std::min<std::uint64_t>(count, kBlock));
    std::fill_n(filtered_.begin(), length, 0.0);
    processBlock(length);
    count -= length;
  }
}

void Detector::process(std::span<const float> samples) {
  const std::scoped_lock lock(mutex_);
  while (!samples.empty()) {
    const std::size_t length = std::min(samples.size(), kBlock);
    std::ranges::copy(samples.first(length), filtered_.begin());
    processBlock(length);
    samples = samples.subspan(length);
  }
}

std::optional<Onset> Detector::takeOnset() { return onsets_.pop(); }

Serial Detector::filter() const {
  // Копии фильтров детектора в сброшенном состоянии. Оба — на частоте
  // детектора: цепочка собирается в обход проверки частот `Serial::create`.
  HighPass hpf(hpf_);
  hpf.reset();
  Envelope envelope(envelope_);
  envelope.reset();
  std::vector<std::unique_ptr<Filter>> filters;
  filters.push_back(std::make_unique<HighPass>(hpf));
  filters.push_back(std::make_unique<Envelope>(envelope));
  return {std::move(filters), settings().sampleRate};
}

void Detector::processBlock(std::size_t length) {
  const std::span<double> filtered(filtered_.data(), length);
  hpf_.process(filtered);
  const std::span<double> envelopes(envelopes_.data(), length);
  std::ranges::copy(filtered, envelopes.begin());
  envelope_.process(envelopes);

  const std::uint64_t first = position_;
  position_ += length;
  for (std::size_t i = 0; i < length; ++i) {
    const std::uint64_t index = first + i;
    const double slope = filtered[i] - lastFiltered_;
    lastFiltered_ = filtered[i];

    // Энергия верха спектра — сумма квадратов разности соседних сэмплов за
    // последние 20 мс: разность усиливает звук пропорционально частоте.
    // Квадрат этого сэмпла входит в окно, квадрат сэмпла 20 мс назад из него
    // выходит. Сравниваются только отношения энергий, поэтому делить на длину
    // окна незачем.
    const auto energySlot = static_cast<std::size_t>(index % energyLength_);
    const double square = slope * slope;
    squaresSum_ += square - squares_[energySlot];
    squares_[energySlot] = square;
    energies_[i] = std::max(squaresSum_, 0.0);

    // Энергия окна, кончившегося 20 мс назад, становится опорой: максимум по
    // окнам, кончающимся от 40 до 20 мс назад, — окна, не задевающие текущее.
    energyMaxima_[i] = {.index = index - energyLength_, .value = {recentEnergy_[energySlot]}};
    recentEnergy_[energySlot] = energies_[i];

    // В ячейке этого сэмпла лежит огибающая сэмпла, отстоящего на длину
    // кольца, — она переходит в окно максимума. После записи кольцо держит
    // огибающую последних 5 мс.
    const auto backSlot = static_cast<std::size_t>(index % backNewest_);
    envelopeMaxima_[i] = {.index = index - backNewest_, .value = {recentEnvelope_[backSlot]}};
    recentEnvelope_[backSlot] = envelopes[i];
  }

  // Пока окну нечего сравнивать, в кольце лежат не сэмплы потока, а нули:
  // в окна идут только сэмплы начиная с длины кольца.
  const auto from = [first, length](std::uint64_t start) {
    return static_cast<std::size_t>(
        std::min<std::uint64_t>(length, start > first ? start - first : 0));
  };
  const std::size_t energyFrom = from(energyLength_);
  energyMax_.process(std::span(energyMaxima_).subspan(energyFrom, length - energyFrom));
  const std::size_t envelopeFrom = from(backNewest_);
  envelopeMax_.process(
      std::span(envelopeMaxima_).subspan(envelopeFrom, length - envelopeFrom));

  for (std::size_t i = envelopeFrom; i < length; ++i)
    detect(first + i, i > 0 ? envelopes[i - 1] : lastEnvelope_, envelopes[i],
           envelopeMaxima_[i].value.value(), energies_[i],
           i >= energyFrom ? energyMaxima_[i].value.value() : 0.0);
  lastEnvelope_ = envelopes[length - 1];
}

void Detector::detect(std::uint64_t index, double previous, double envelope, double reference,
                      double energy, double energyReference) {
  // Подъём огибающей над максимумом за 25 мс — кандидат в начало атаки.
  // Начинается пересечением уровня снизу, сила — наибольшее отношение
  // огибающей к уровню за подъём. Законченный подъём после мёртвого времени
  // уходит в кандидаты.
  const double riseLevel = std::max(threshold_, reference);
  if (envelope < riseLevel) {
    if (rising_ && rise_.index >= armedFrom_)
      rises_.push(rise_);
    rising_ = false;
  } else if (!rising_ && previous < riseLevel) {
    rising_ = true;
    rise_ = {.index = index,
             .strength = envelope / riseLevel,
             .position = crossing(index, riseLevel, previous, envelope)};
  } else if (rising_ && envelope / riseLevel > rise_.strength) {
    rise_.strength = envelope / riseLevel;
  }

  if (index < armedFrom_)
    return;

  // Резкая атака: огибающая пересекла уровень снизу. Начало — там, где начался
  // этот подъём над максимумом: у шумной атаки первый сэмпл выше уровня
  // приходит позже начала. Рост сравнивается за последние 5 мс, и подъём,
  // начавшийся раньше, — медленное разгорание, а не начало этой атаки.
  const double level = std::max(threshold_, ratio_ * reference);
  if (envelope >= level && previous < level) {
    if (rising_ && rise_.index >= armedFrom_ && rise_.index + backNewest_ >= index)
      report(rise_.index, rise_.position);
    else
      report(index, crossing(index, level, previous, envelope));
    return;
  }

  // Удар по звучащей струне: энергия выросла в `energyRatio` раз. Верх спектра от
  // медиатора приходит раньше щелчка, поэтому решение ждёт ещё одно окно, и
  // начало — самый сильный подъём огибающей за 20 мс до и после роста.
  // Энергия без подъёма огибающей — не удар.
  if (!energyPending_ && index >= energyArmedFrom_ &&
      energy > energyRatio_ * energyReference) {
    energyPending_ = true;
    energyDecision_ = index + energyLength_;
  }

  if (energyPending_ && index >= energyDecision_) {
    energyPending_ = false;
    if (const std::optional<Rise> strongest = strongestRise(index))
      report(strongest->index, strongest->position);
  }
}

std::optional<Detector::Rise> Detector::strongestRise(std::uint64_t index) {
  // Подъёмы в буфере идут в порядке начала: строгое сравнение оставляет из
  // равных самый ранний. Текущий подъём начался позже всех законченных.
  const auto recent = [this, index](const Rise &rise) {
    return rise.index >= armedFrom_ && rise.index + 2 * energyLength_ > index;
  };
  std::optional<Rise> strongest;
  while (const std::optional<Rise> rise = rises_.pop())
    if (recent(*rise) && (!strongest || rise->strength > strongest->strength))
      strongest = rise;
  if (rising_ && recent(rise_) && (!strongest || rise_.strength > strongest->strength))
    strongest = rise_;
  return strongest;
}

void Detector::report(std::uint64_t index, double position) {
  onsets_.push(
      {.position = static_cast<std::uint64_t>(std::llround(std::max(position, 0.0)))});

  // Окна энергии второго пути — текущее и два опорных — должны лечь целиком
  // после атаки.
  armedFrom_ = index + deadTime_;
  energyArmedFrom_ = std::max(armedFrom_, index + 3 * energyLength_);
  rises_.clear();
  energyPending_ = false;
}

} // namespace training::core::detector
