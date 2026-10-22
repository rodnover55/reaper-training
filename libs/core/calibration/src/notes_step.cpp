#include "training/core/calibration/notes_step.hpp"

#include "levels.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace training::core::calibration {
namespace {

/// Сколько звука после запуска шаг не измеряет: фильтр отзванивает скачок
/// постоянной составляющей.
constexpr Interval kSettle = 0.1 * Interval::kSecond;

/// Наименьший зазор между шумом и самой тихой атакой, дБ, при котором итог
/// удачный.
constexpr double kMinGapDb = 12.0;

/// Пик сигнала, dBFS, с которого вход считается перегруженным.
constexpr double kClipDb = -1.0;

/// Порог тишины детектора над шумом, дБ, — половина наименьшего зазора:
/// щелчок тише не даёт удачной калибровки, а шум выше почти не поднимается.
constexpr double kDetectAboveNoiseDb = kMinGapDb / 2.0;

/// Атака — столько от начала ноты: щелчок медиатора.
constexpr Interval kAttack = 5 * Interval::kMillisecond;

/// История огибающей: удар по звучащей струне детектор сообщает через 20–40 мс
/// после его начала, и атака должна найтись в истории.
constexpr Interval kHistory = 0.1 * Interval::kSecond;

/// Сколько шаг идёт после последней необходимой ноты: пик успевает учесть её
/// звук.
constexpr Interval kAfterLast = Interval::kSecond;

} // namespace

NotesStep::NotesStep(const detector::Detector &detector, SampleRate sampleRate, int notes,
                     Interval silenceLength)
    : detectorSettings_(detector.settings()), rate_(sampleRate), notes_(notes),
      silenceLength_(silenceLength), settleLength_(samplesOf(kSettle, sampleRate)),
      attackLength_(samplesOf(kAttack, sampleRate)),
      historyLength_(samplesOf(kHistory, sampleRate)),
      afterLastLength_(samplesOf(kAfterLast, sampleRate)),
      timeoutLength_(samplesOf(silenceLength, sampleRate)),
      history_(static_cast<std::size_t>(historyLength_), 0.0) {
  attacks_.reserve(static_cast<std::size_t>(notes_));
}

double NotesStep::noiseDb() const {
  const std::scoped_lock lock(mutex_);
  return noiseDb_;
}

std::expected<void, InvalidStep> NotesStep::start(double noiseDb) {
  const std::scoped_lock lock(mutex_);
  if (step_ != Step::Off)
    return std::unexpected(InvalidStep{});
  clear();
  // `NotesStep.start#isolated`: у шага свой детектор со своим порогом. Поток
  // детектора начинается с первого сэмпла шага: позиции атак — номера сэмплов
  // шага.
  detector::Settings settings = detectorSettings_;
  settings.silenceDb = noiseDb + kDetectAboveNoiseDb;
  // Настройки отличаются от настроек созданного детектора только порогом
  // тишины, а от него `create` не зависит: детектор создаётся всегда.
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
  detector_.emplace(std::move(*detector::Detector::create(settings)));
  filter_.emplace(detector_->filter());
  noiseDb_ = noiseDb;
  step_ = Step::Loading;
  return {};
}

void NotesStep::stop() {
  const std::scoped_lock lock(mutex_);
  clear();
  step_ = Step::Off;
  noiseDb_ = kFloorDb;
}

void NotesStep::clear() {
  filter_.reset();
  detector_.reset();
  heardEnd_.reset();
  position_ = 0;
  std::ranges::fill(history_, 0.0);
  pending_.clear();
  attacks_.clear();
  peak_ = 0.0;
  lastNoteAt_ = 0;
  finishAt_.reset();
  lastLevel_ = 0.0;
  verdict_ = Verdict::NotEnoughNotes;
  thresholdDb_ = kFloorDb;
}

NotesStep::State NotesStep::result() const {
  const std::scoped_lock lock(mutex_);
  return current();
}

NotesStep::State NotesStep::current() const {
  State state;
  state.step = step_;
  state.attacksDb = attacks_;
  state.levelDb = decibels(lastLevel_);
  state.peakDb = decibels(peak_);
  state.verdict = verdict_;
  state.thresholdDb = thresholdDb_;
  if (step_ == Step::Working)
    state.sinceLastNote = lengthOf(position_ - lastNoteAt_, rate_);
  return state;
}

void NotesStep::receive(const input::AudioBlock &block) {
  const std::scoped_lock lock(mutex_);
  if (block.samples.empty() || !detector_ || !filter_ || step_ == Step::Off ||
      step_ == Step::Done)
    return;

  // Пропущенный между блоками звук — тишина.
  detector::Serial &filter = *filter_;
  detector::Detector &detector = *detector_;
  silence(missedOf(heardEnd_, block, rate_),
          [this, &filter, &detector](std::span<const float> zeros) {
            process(zeros, filter, detector);
          });
  heardEnd_ = endOf(block, rate_);
  process(block.samples, filter, detector);
}

void NotesStep::process(std::span<const float> samples, detector::Serial &filter,
                        detector::Detector &detector) {
  if (step_ == Step::Done)
    return;

  lastLevel_ = 0.0;
  std::size_t from = 0;
  while (from < samples.size() && step_ != Step::Done) {
    // Атаки и конец шага проверяются на границах, кратных длине атаки от
    // запуска шага, и на конце прогрева: так ход не зависит от того, как
    // поток нарезан.
    std::uint64_t boundary = (position_ / attackLength_ + 1) * attackLength_;
    if (position_ < settleLength_)
      boundary = std::min(boundary, settleLength_);
    const auto length = static_cast<std::size_t>(
        std::min<std::uint64_t>(boundary - position_, samples.size() - from));
    const std::span<const float> piece = samples.subspan(from, length);
    from += length;

    const std::span<double> envelopes = copied(piece, buffer_);
    filter.process(envelopes);
    const bool working = step_ == Step::Working;
    for (std::size_t i = 0; i < piece.size(); ++i) {
      lastLevel_ = std::max(lastLevel_, envelopes[i]);
      history_[static_cast<std::size_t>(position_ % historyLength_)] = envelopes[i];
      if (working)
        peak_ = std::max(peak_, std::abs(static_cast<double>(piece[i])));
      ++position_;
    }

    // Позиция атаки — номер сэмпла шага: поток детектора начат запуском шага.
    // В счёт идут только ноты, начавшиеся после прогрева шага.
    detector.process(piece);
    while (const std::optional<detector::Onset> onset = detector.takeOnset())
      if (onset->position >= settleLength_)
        pending_.push_back(onset->position);

    if (step_ == Step::Loading && position_ >= settleLength_) {
      step_ = Step::Working;
      lastNoteAt_ = position_;
    } else if (working && position_ == boundary) {
      checkpoint();
    }
  }
}

void NotesStep::checkpoint() {
  // Атака измеряется, когда прошли все её сэмплы. Начало старше истории
  // обрезается по ней — детектор сообщает атаки раньше, так что это запас.
  const std::uint64_t oldest = position_ > historyLength_ ? position_ - historyLength_ : 0;
  auto measured = pending_.begin();
  for (; measured != pending_.end() && *measured + attackLength_ <= position_; ++measured) {
    if (attacks_.size() >= static_cast<std::size_t>(notes_))
      continue;

    double level = 0.0;
    for (std::uint64_t i = std::max(*measured, oldest); i < *measured + attackLength_; ++i)
      level = std::max(level, history_[static_cast<std::size_t>(i % historyLength_)]);
    attacks_.push_back(decibels(level));
    lastNoteAt_ = *measured;
    if (attacks_.size() == static_cast<std::size_t>(notes_))
      finishAt_ = position_ + afterLastLength_;
  }
  pending_.erase(pending_.begin(), measured);

  if (finishAt_ ? position_ >= *finishAt_ : position_ - lastNoteAt_ >= timeoutLength_)
    finish();
}

void NotesStep::finish() {
  const double peakDb = decibels(peak_);
  double softestDb = kFloorDb;
  thresholdDb_ = kFloorDb;
  if (!attacks_.empty()) {
    softestDb = std::ranges::min(attacks_);
    thresholdDb_ = static_cast<double>(std::lround((noiseDb_ + softestDb) / 2.0));
  }

  // Перегруз — раньше зазора: когда гитарист убавит усиление, все уровни
  // станут другими.
  if (attacks_.size() < static_cast<std::size_t>(notes_))
    verdict_ = Verdict::NotEnoughNotes;
  else if (peakDb >= kClipDb)
    verdict_ = Verdict::Clipping;
  else if (softestDb - noiseDb_ < kMinGapDb)
    verdict_ = Verdict::NoiseTooClose;
  else
    verdict_ = Verdict::Good;
  step_ = Step::Done;
}

} // namespace training::core::calibration
