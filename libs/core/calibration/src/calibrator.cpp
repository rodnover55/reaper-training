#include "training/core/calibration/calibrator.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>

namespace training::core::calibration {
namespace {

/// Сколько сэмплов тишины идёт в шаги за раз, когда звук между блоками
/// пропущен.
constexpr std::size_t kSilenceChunk = 1024;

/// Сэмплы тишины для пропущенного звука.
constexpr std::array<float, kSilenceChunk> kSilence{};

} // namespace

std::expected<std::unique_ptr<Calibrator>, CannotStartUnit>
Calibrator::create(SampleRate sampleRate) {
  std::optional<detector::Detector> detector =
      detector::Detector::create(detector::Settings{.sampleRate = sampleRate});
  if (!detector)
    return std::unexpected(CannotStartUnit{});
  return std::make_unique<Calibrator>(Key(), std::move(*detector));
}

Calibrator::Calibrator(Key /*key*/, detector::Detector detector)
    : detector_(std::move(detector)), rate_(detector_.settings().sampleRate),
      silenceStep_(detector_, rate_), notesStep_(detector_, rate_),
      resultStep_(detector_, rate_) {}

std::expected<void, InvalidStep> Calibrator::start() {
  const std::scoped_lock lock(mutex_);
  if (state_.step != Step::Off)
    return std::unexpected(InvalidStep{});
  heardEnd_.reset();
  const std::expected<void, InvalidStep> started = silenceStep_.start();
  state_ = State();
  state_.step = Step::Silence;
  (void)syncSilence();
  return started;
}

void Calibrator::stop() {
  const std::scoped_lock lock(mutex_);
  stopSteps();
}

void Calibrator::stopSteps() {
  silenceStep_.stop();
  notesStep_.stop();
  resultStep_.stop();
  state_.step = Step::Off;
  heardEnd_.reset();
}

Calibrator::State Calibrator::result() const {
  const std::scoped_lock lock(mutex_);
  return state_;
}

void Calibrator::receive(const input::AudioBlock &block) {
  const std::scoped_lock lock(mutex_);
  if (state_.step == Step::Off || block.samples.empty())
    return;
  const SampleRate rate = rate_;

  // Стык с прошлым блоком — в целых сэмплах: расхождение меньше половины
  // сэмпла — округление времени. Начало, которое лежит в уже полученном
  // звуке, отбрасывается, а пропущенный звук идёт в шаги тишиной.
  input::AudioBlock heard = block;
  if (heardEnd_) {
    const auto away = std::llround((block.start - *heardEnd_) / Interval::kSecond * rate.hz());
    if (away < 0) {
      const std::size_t cut = std::min(static_cast<std::size_t>(-away), block.samples.size());
      if (cut == block.samples.size())
        return;
      heard.samples = block.samples.subspan(cut);
      heard.start = block.start + static_cast<double>(cut) / rate.hz() * Interval::kSecond;
    } else if (away > 0) {
      dispatchSilence(static_cast<std::uint64_t>(away), *heardEnd_);
    }
  }
  heardEnd_ =
      block.start + static_cast<double>(block.samples.size()) / rate.hz() * Interval::kSecond;
  if (state_.step != Step::Off)
    dispatch(heard);
}

void Calibrator::dispatchSilence(std::uint64_t count, Moment start) {
  while (count > 0 && state_.step != Step::Off) {
    const auto length =
        static_cast<std::size_t>(std::min<std::uint64_t>(count, kSilenceChunk));
    dispatch({.samples = std::span(kSilence).first(length), .start = start});
    start += static_cast<double>(length) / rate_.hz() * Interval::kSecond;
    count -= length;
  }
}

SilenceStep::State Calibrator::syncSilence() {
  SilenceStep::State silence = silenceStep_.result();
  state_.noiseDb = silence.noiseDb;
  state_.levelDb = silence.levelDb;
  state_.sinceSound = silence.sinceSound;
  state_.silenceLeft = silence.silenceLeft;
  return silence;
}

void Calibrator::dispatch(const input::AudioBlock &block) {
  switch (state_.step) {
  case Step::Off:
    break;
  case Step::Silence:
    silenceStep_.receive(block);
    if (syncSilence().step == SilenceStep::Step::Done) {
      silenceStep_.stop();
      (void)notesStep_.start(state_.noiseDb);
      state_.step = Step::Notes;
    }
    break;
  case Step::Notes: {
    notesStep_.receive(block);
    NotesStep::State notes = notesStep_.result();
    state_.levelDb = notes.levelDb;
    state_.attacksDb = std::move(notes.attacksDb);
    state_.sinceLastNote = notes.sinceLastNote;
    state_.peakDb = notes.peakDb;
    if (notes.step == NotesStep::Step::Done) {
      state_.verdict = notes.verdict;
      state_.thresholdDb = notes.thresholdDb;
      notesStep_.stop();
      (void)resultStep_.start(state_.thresholdDb);
      state_.step = Step::Done;
    }
    break;
  }
  case Step::Done:
    resultStep_.receive(block);
    if (resultStep_.step() == ResultStep::Step::Done)
      stopSteps();
    break;
  }
}

} // namespace training::core::calibration
