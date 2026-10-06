#include "training/onset/calibration.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace training::onset {
namespace {

/// Сколько секунд после начала потока и после разрыва шаг тишины не слушает:
/// ВЧ-фильтр отзванивает скачок постоянной составляющей.
constexpr double kSettleSeconds = 0.1;

/// Окно шага тишины, с: у окна — наибольшая огибающая.
constexpr double kWindowSeconds = 0.1;

/// Звук в тишине: окно громче медианы окон шага больше чем на столько дБ.
/// Окна проверяются, когда их не меньше `kSoundFromWindow`: медиане нужно из
/// чего складываться.
constexpr double kSoundDb = 20.0;
constexpr std::size_t kSoundFromWindow = 5;

/// Порог тишины детектора шага нот над шумом, дБ, — половина наименьшего
/// зазора: щелчок тише не даёт удачной калибровки, а шум выше почти не
/// поднимается.
constexpr double kDetectAboveNoiseDb = kCalibrationMinGapDb / 2.0;

/// Атака — столько секунд от начала ноты: щелчок медиатора.
constexpr double kAttackSeconds = 0.005;

/// История огибающей, с: удар по звучащей струне детектор сообщает через
/// 20–40 мс после его начала, и атака должна найтись в истории.
constexpr double kHistorySeconds = 0.1;

/// Сколько секунд шаг нот идёт после последней ноты: пик успевает учесть её
/// звук.
constexpr double kAfterLastSeconds = 1.0;

std::uint64_t samplesOf(double seconds, double sampleRate) {
  return static_cast<std::uint64_t>(std::max(1LL, std::llround(seconds * sampleRate)));
}

double decibels(double amplitude) {
  return amplitude > 0.0 ? std::max(kCalibrationFloorDb, 20.0 * std::log10(amplitude))
                         : kCalibrationFloorDb;
}

double median(std::vector<double> values) {
  const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
  std::ranges::nth_element(values, middle);
  return *middle;
}

} // namespace

CalibrationResult judgeCalibration(double noiseDb, std::span<const double> attacksDb,
                                   double peakDb) {
  CalibrationResult result{
      .noiseDb = noiseDb, .notes = static_cast<int>(attacksDb.size()), .peakDb = peakDb};
  if (!attacksDb.empty()) {
    const auto [softest, loudest] = std::ranges::minmax_element(attacksDb);
    result.softestDb = *softest;
    result.loudestDb = *loudest;
    result.thresholdDb = static_cast<double>(std::lround((noiseDb + *softest) / 2.0));
  }

  // Перегруз — раньше зазора: когда гитарист убавит усиление, все уровни
  // станут другими.
  if (result.notes < kCalibrationNotes)
    result.verdict = Verdict::NotEnoughNotes;
  else if (peakDb >= kCalibrationClipDb)
    result.verdict = Verdict::Clipping;
  else if (result.softestDb - noiseDb < kCalibrationMinGapDb)
    result.verdict = Verdict::NoiseTooClose;
  else
    result.verdict = Verdict::Good;
  return result;
}

Calibration::Calibration(double sampleRate)
    : sampleRate_(sampleRate), highPass_(Settings{}.highPassHz, sampleRate),
      envelope_(sampleRate), settleLength_(samplesOf(kSettleSeconds, sampleRate)),
      windowLength_(samplesOf(kWindowSeconds, sampleRate)),
      windowsPerStep_(
          static_cast<std::size_t>(std::llround(kCalibrationSilenceSeconds / kWindowSeconds))),
      attackLength_(samplesOf(kAttackSeconds, sampleRate)),
      historyLength_(samplesOf(kHistorySeconds, sampleRate)),
      afterLastLength_(samplesOf(kAfterLastSeconds, sampleRate)),
      timeoutLength_(samplesOf(kCalibrationNotesTimeoutSeconds, sampleRate)),
      closeAfterLength_(samplesOf(kCalibrationCloseAfterSeconds, sampleRate)),
      settleUntil_(settleLength_), history_(static_cast<std::size_t>(historyLength_), 0.0) {
  windows_.reserve(windowsPerStep_);
  attacks_.reserve(kCalibrationNotes);
}

void Calibration::process(std::span<const float> samples) {
  if (samples.empty())
    return;

  lastLevel_ = 0.0;
  std::size_t from = 0;
  while (from < samples.size() && step_ == CalibrationStep::Silence)
    processSilence(samples[from++]);
  if (step_ == CalibrationStep::Notes)
    from += processNotes(samples.subspan(from));
  if (step_ == CalibrationStep::Done)
    processDone(samples.subspan(from));
}

void Calibration::discontinuity() {
  highPass_.reset();
  envelope_.reset();

  switch (step_) {
  case CalibrationStep::Silence:
    settleUntil_ = position_ + settleLength_;
    restartSilence();
    break;
  case CalibrationStep::Notes:
    if (detector_)
      detector_->reset();
    detectorStart_ = position_;
    pending_.clear();
    break;
  case CalibrationStep::Done:
    settleUntil_ = position_ + settleLength_;
    break;
  }
}

double Calibration::silenceLeft() const {
  if (step_ != CalibrationStep::Silence)
    return 0.0;
  const std::uint64_t heard = windows_.size() * windowLength_ + windowFill_;
  return seconds(windowsPerStep_ * windowLength_ - heard);
}

std::optional<double> Calibration::secondsSinceSound() const {
  if (!soundAt_)
    return std::nullopt;
  return seconds(position_ - *soundAt_);
}

double Calibration::levelDb() const { return decibels(lastLevel_); }

double Calibration::secondsWithoutNotes() const {
  return step_ == CalibrationStep::Notes ? seconds(position_ - lastNoteAt_) : 0.0;
}

void Calibration::processSilence(float sample) {
  const double envelope = envelope_.process(highPass_.process(static_cast<double>(sample)));
  lastLevel_ = std::max(lastLevel_, envelope);
  if (position_++ < settleUntil_)
    return;

  windowMax_ = std::max(windowMax_, envelope);
  if (++windowFill_ == windowLength_)
    closeWindow();
}

void Calibration::closeWindow() {
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
      windows_.erase(windows_.begin(), last.base());
      soundAt_ = position_;
      return;
    }
  }

  if (windows_.size() == windowsPerStep_) {
    noiseDb_ = decibels(std::ranges::max(windows_));
    startNotes();
  }
}

void Calibration::restartSilence() {
  windows_.clear();
  windowMax_ = 0.0;
  windowFill_ = 0;
}

void Calibration::startNotes() {
  step_ = CalibrationStep::Notes;
  detector_.emplace(
      Settings{.sampleRate = sampleRate_, .silenceDb = noiseDb_ + kDetectAboveNoiseDb});
  detectorStart_ = position_;
  lastNoteAt_ = position_;
}

std::size_t Calibration::processNotes(std::span<const float> samples) {
  if (!detector_)
    return 0;
  Detector &detector = *detector_;

  std::size_t from = 0;
  while (from < samples.size() && step_ == CalibrationStep::Notes) {
    // Атаки и конец шага проверяются на границах, кратных длине атаки от
    // начала потока: так ход не зависит от того, как поток нарезан.
    const std::uint64_t boundary = (position_ / attackLength_ + 1) * attackLength_;
    const auto length = static_cast<std::size_t>(
        std::min<std::uint64_t>(boundary - position_, samples.size() - from));
    const std::span<const float> piece = samples.subspan(from, length);
    from += length;

    for (const float sample : piece) {
      const auto value = static_cast<double>(sample);
      const double envelope = envelope_.process(highPass_.process(value));
      lastLevel_ = std::max(lastLevel_, envelope);
      history_[static_cast<std::size_t>(position_ % historyLength_)] = envelope;
      peak_ = std::max(peak_, std::abs(value));
      ++position_;
    }

    found_.clear();
    detector.process(piece, found_);
    for (const Onset &onset : found_)
      pending_.push_back(detectorStart_ + static_cast<std::uint64_t>(onset.position));

    if (position_ == boundary)
      checkpoint();
  }
  return from;
}

void Calibration::checkpoint() {
  // Атака измеряется, когда прошли все её сэмплы. Начало старше истории
  // обрезается по ней — детектор сообщает атаки раньше, так что это запас.
  const std::uint64_t oldest = position_ > historyLength_ ? position_ - historyLength_ : 0;
  auto measured = pending_.begin();
  for (; measured != pending_.end() && *measured + attackLength_ <= position_; ++measured) {
    if (attacks_.size() >= static_cast<std::size_t>(kCalibrationNotes))
      continue;

    double level = 0.0;
    for (std::uint64_t i = std::max(*measured, oldest); i < *measured + attackLength_; ++i)
      level = std::max(level, history_[static_cast<std::size_t>(i % historyLength_)]);
    attacks_.push_back(decibels(level));
    lastNoteAt_ = position_;
    if (attacks_.size() == static_cast<std::size_t>(kCalibrationNotes))
      finishAt_ = position_ + afterLastLength_;
  }
  pending_.erase(pending_.begin(), measured);

  if (finishAt_ ? position_ >= *finishAt_ : position_ - lastNoteAt_ >= timeoutLength_)
    finish();
}

void Calibration::finish() {
  result_ = judgeCalibration(noiseDb_, attacks_, decibels(peak_));
  step_ = CalibrationStep::Done;
  doneAt_ = position_;
}

void Calibration::processDone(std::span<const float> samples) {
  // Звук после итога — то же правило, что у звука в тишине: громче шума на
  // `kSoundDb`.
  const double loud = std::pow(10.0, (noiseDb_ + kSoundDb) / 20.0);
  const std::uint64_t from = std::max(doneAt_ + closeAfterLength_, settleUntil_);
  for (const float sample : samples) {
    const double envelope = envelope_.process(highPass_.process(static_cast<double>(sample)));
    lastLevel_ = std::max(lastLevel_, envelope);
    if (position_++ >= from && envelope > loud)
      soundAfterResult_ = true;
  }
}

double Calibration::seconds(std::uint64_t samples) const {
  return static_cast<double>(samples) / sampleRate_;
}

} // namespace training::onset
