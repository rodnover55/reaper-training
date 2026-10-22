#include "training/reaper/calibration.hpp"

#include <algorithm>
#include <expected>
#include <memory>
#include <utility>

namespace training::reaper {

using core::calibration::Calibrator;
using core::calibration::NotesStep;
using core::calibration::Step;

Calibration::Calibration(core::Range<double> silenceRange, CalibrationEvents *events)
    : silenceRange_(silenceRange), events_(events) {}

void Calibration::setTransport(core::Transport *transport) {
  const std::scoped_lock lock(mutex_);
  transport_ = transport;
}

double Calibration::silenceOf(double thresholdDb) const {
  return std::clamp(thresholdDb, silenceRange_.min, silenceRange_.max);
}

void Calibration::start() {
  const std::scoped_lock lock(mutex_);
  endSession();
  session_ = true;
}

void Calibration::end() {
  const std::scoped_lock lock(mutex_);
  endSession();
}

void Calibration::endSession() {
  if (!session_)
    return;
  session_ = false;
  if (calibrator_)
    calibrator_->stop();
  calibrator_.reset();
  heardSound_ = false;
  if (events_)
    events_->ended();
}

void Calibration::onInputChange() { end(); }

void Calibration::onStart() {
  const std::scoped_lock lock(mutex_);
  if (!session_)
    return;

  // Запуск транспорта прерывает шаги калибровки, а показанный итог просто
  // закрывает.
  const bool finished = done();
  if (events_) {
    if (finished)
      events_->closedByPlayback();
    else
      events_->cancelledByPlayback();
  }
  endSession();
  cancelledByPlayback_ = !finished;
}

void Calibration::onStop() {
  const std::scoped_lock lock(mutex_);
  cancelledByPlayback_ = false;
}

void Calibration::setSampleRate(core::SampleRate rate) {
  const std::scoped_lock lock(mutex_);
  rate_ = rate;
}

bool Calibration::restart() {
  heardSound_ = false;
  std::expected<std::shared_ptr<Calibrator>, core::CannotStartUnit> started =
      transport_->start(core::Transport::Calibration{});
  if (!started) {
    calibrator_.reset();
    return false;
  }
  calibrator_ = std::move(*started);
  if (events_)
    events_->silenceStep(calibrator_->sampleRate().hz());
  return true;
}

void Calibration::receive(const core::input::AudioBlock &block) {
  const std::scoped_lock lock(mutex_);
  if (!session_ || !rate_ || block.samples.empty())
    return;

  if (!calibrator_ || calibrator_->sampleRate() != *rate_) {
    // Итог уже подведён: звук другой частоты в нём ничего не меняет.
    if (done())
      return;
    // Новая частота — другие уровни фильтра и другое время шагов: заново.
    // Частота калибратора — частота транспорта
    // (`core::input::SoundReceiver.sampleRate`): блоки другой частоты ждут
    // транспорта под неё.
    if (!transport_ || transport_->sampleRate() != *rate_)
      return;
    if (!restart()) {
      endSession();
      return;
    }
  }

  const Calibrator::State before = calibrator_->result();
  calibrator_->receive(block);
  report(before, calibrator_->result());
}

void Calibration::report(const Calibrator::State &before, const Calibrator::State &after) {
  // Без звука время с последнего звука только растёт: уменьшилось — шаг
  // тишины начат заново после звука.
  const bool sound = before.step == Step::Silence && after.sinceSound < before.sinceSound;
  if (sound)
    heardSound_ = true;
  if (events_) {
    if (sound)
      events_->soundDuringSilence();
    if (before.step == Step::Silence && after.step != Step::Silence)
      events_->notesStep(after.noiseDb);
    for (std::size_t i = before.attacksDb.size(); i < after.attacksDb.size(); ++i)
      events_->attack(i, after.attacksDb[i]);
    if (before.step != Step::Done && after.step == Step::Done) {
      events_->finished(after);
      if (after.verdict == NotesStep::Verdict::Good)
        events_->silenceSet(silenceOf(after.thresholdDb));
    }
  }

  // Удар по струнам после итога останавливает калибратор: сессия кончается.
  if (before.step == Step::Done && after.step == Step::Off) {
    if (events_)
      events_->closedBySound();
    endSession();
  }
}

std::optional<Calibrator::State> Calibration::current() const {
  if (!calibrator_)
    return std::nullopt;
  return calibrator_->result();
}

bool Calibration::done() const {
  const std::optional<Calibrator::State> state = current();
  return state && state->step == Step::Done;
}

std::optional<double> Calibration::applyAnyway() {
  const std::scoped_lock lock(mutex_);
  if (!session_)
    return std::nullopt;
  const std::optional<Calibrator::State> state = current();
  if (!state || state->step != Step::Done || state->attacksDb.empty())
    return std::nullopt;

  const double silenceDb = silenceOf(state->thresholdDb);
  if (events_)
    events_->appliedAnyway(silenceDb);
  endSession();
  return silenceDb;
}

bool Calibration::listening() const {
  const std::scoped_lock lock(mutex_);
  return session_;
}

std::optional<Calibrator::State> Calibration::state() const {
  const std::scoped_lock lock(mutex_);
  if (!session_)
    return std::nullopt;
  return current();
}

bool Calibration::finished() const {
  const std::scoped_lock lock(mutex_);
  return session_ && done();
}

bool Calibration::heardSound() const {
  const std::scoped_lock lock(mutex_);
  return heardSound_;
}

std::optional<double> Calibration::silenceDb() const {
  const std::scoped_lock lock(mutex_);
  const std::optional<Calibrator::State> state = current();
  if (!session_ || !state || state->step != Step::Done ||
      state->verdict != NotesStep::Verdict::Good)
    return std::nullopt;
  return silenceOf(state->thresholdDb);
}

std::optional<double> Calibration::anywaySilenceDb() const {
  const std::scoped_lock lock(mutex_);
  const std::optional<Calibrator::State> state = current();
  if (!session_ || !state || state->step != Step::Done ||
      (state->verdict != NotesStep::Verdict::NoiseTooClose &&
       state->verdict != NotesStep::Verdict::Clipping))
    return std::nullopt;
  return silenceOf(state->thresholdDb);
}

bool Calibration::cancelledByPlayback() const {
  const std::scoped_lock lock(mutex_);
  return cancelledByPlayback_;
}

} // namespace training::reaper
