#include "training/trainer/calibration.hpp"

#include "training/trainer/settings.hpp"

#include <cstddef>
#include <optional>
#include <utility>

namespace training::trainer {
namespace {

const char *verdictName(onset::Verdict verdict) {
  switch (verdict) {
  case onset::Verdict::Good:
    return "good";
  case onset::Verdict::NoiseTooClose:
    return "noise too close";
  case onset::Verdict::Clipping:
    return "clipping";
  case onset::Verdict::NotEnoughNotes:
    return "not enough notes";
  }
  return "?";
}

/// Порог тишины `silenceDb`, прижатый к границам настройки.
double settable(double silenceDb) {
  return clamped(Settings{.silenceDb = silenceDb}).silenceDb;
}

} // namespace

Calibrator::Calibrator(Journal journal) : journal_(std::move(journal)) {}

CalibrationPhase phaseOf(const CalibrationSession &session) {
  if (session.noSound)
    return CalibrationPhase::NoSound;
  if (!session.calibration)
    return CalibrationPhase::Silence;

  switch (session.calibration->step()) {
  case onset::CalibrationStep::Silence:
    break;
  case onset::CalibrationStep::Notes:
    return CalibrationPhase::Notes;
  case onset::CalibrationStep::Done:
    return CalibrationPhase::Result;
  }
  return CalibrationPhase::Silence;
}

bool Calibrator::start(bool running, const Measurement &measurement, int channel,
                       double silenceDb, double now) {
  if (running || measurement.midi || measurement.measuring == Measuring::NoInput)
    return false;

  end();
  session_.emplace(CalibrationSession{.calibration = std::nullopt,
                                      .previousSilenceDb = silenceDb,
                                      .channelName = measurement.inputName,
                                      .noSound = false,
                                      .finished = false});
  lastSound_ = now;
  heard_ = false;
  cancelledByPlayback_ = false;
  write(journal_, "calibration: started on input {} ({}), silence {} dBFS", channel + 1,
        measurement.inputName, silenceDb);
  return true;
}

void Calibrator::end() {
  if (!session_)
    return;
  session_.reset();
  write(journal_, "calibration: ended");
}

void Calibrator::onInputChange() { end(); }

void Calibrator::onStart() {
  if (!session_)
    return;

  // Запуск транспорта прерывает шаги калибровки, а показанный итог просто
  // закрывает.
  const bool done = finished();
  write(journal_, "calibration: {} by playback", done ? "closed" : "cancelled");
  end();
  cancelledByPlayback_ = !done;
}

void Calibrator::feed(std::span<const float> samples, double sampleRate, bool gap) {
  if (!session_ || session_->noSound)
    return;
  heard_ = true;

  std::optional<onset::Calibration> &stored = session_->calibration;
  if (stored && stored->sampleRate() == sampleRate) {
    if (gap) {
      stored->discontinuity();
      write(journal_, "calibration: gap in the input");
    }
  } else if (session_->finished) {
    // Итог уже подведён: звук другой частоты в нём ничего не меняет.
    return;
  } else {
    // Новая частота — другие уровни фильтра и другое время шагов: заново.
    stored.emplace(sampleRate);
    write(journal_, "calibration: {} Hz, silence step", sampleRate);
  }
  onset::Calibration &calibration = *stored;

  const onset::CalibrationStep step = calibration.step();
  const std::size_t attacks = calibration.attacksDb().size();
  const std::optional<double> sound = calibration.secondsSinceSound();
  calibration.process(samples);

  const std::optional<double> nowSound = calibration.secondsSinceSound();
  if (nowSound && (!sound || *nowSound < *sound))
    write(journal_, "calibration: sound during silence, silence step counts again");
  if (step == onset::CalibrationStep::Silence &&
      calibration.step() != onset::CalibrationStep::Silence)
    write(journal_, "calibration: noise {:.1f} dBFS, notes step", calibration.noiseDb());
  for (std::size_t i = attacks; i < calibration.attacksDb().size(); ++i)
    write(journal_, "calibration: note {} attack {:.1f} dBFS", i + 1,
          calibration.attacksDb()[i]);
}

CalibrationUpdate Calibrator::update(double now) {
  if (!session_)
    return {};
  CalibrationSession &session = *session_;
  const bool heard = std::exchange(heard_, false);

  // После итога звук идёт в калибровку дальше: удар по струнам закрывает
  // панель.
  if (session.finished && session.calibration && session.calibration->soundAfterResult()) {
    write(journal_, "calibration: closed by sound after the result");
    end();
    return {.changed = true, .silenceDb = std::nullopt};
  }
  if (session.finished || session.noSound)
    return {};

  if (heard)
    lastSound_ = now;

  if (session.calibration)
    if (const std::optional<onset::CalibrationResult> &result = session.calibration->result())
      return finish(session, *result);

  if (now - lastSound_ >= kNoSoundSeconds) {
    session.noSound = true;
    write(journal_, "calibration: no sound from the audio device for {} s", kNoSoundSeconds);
    return {.changed = true, .silenceDb = std::nullopt};
  }
  return {.changed = heard, .silenceDb = std::nullopt};
}

CalibrationUpdate Calibrator::finish(CalibrationSession &session,
                                     const onset::CalibrationResult &result) {
  session.finished = true;
  write(journal_,
        "calibration: {}, noise {:.1f}, attacks {} from {:.1f} to {:.1f}, peak {:.1f}, "
        "threshold {} dBFS",
        verdictName(result.verdict), result.noiseDb, result.notes, result.softestDb,
        result.loudestDb, result.peakDb, result.thresholdDb);

  if (result.verdict != onset::Verdict::Good)
    return {.changed = true, .silenceDb = std::nullopt};

  write(journal_, "calibration: silence {} -> {} dBFS", session.previousSilenceDb,
        settable(result.thresholdDb));
  return {.changed = true, .silenceDb = result.thresholdDb};
}

std::optional<double> Calibrator::anywaySilenceDb() const {
  if (!session_ || !session_->calibration)
    return std::nullopt;
  const std::optional<onset::CalibrationResult> &result = session_->calibration->result();
  if (!result || (result->verdict != onset::Verdict::NoiseTooClose &&
                  result->verdict != onset::Verdict::Clipping))
    return std::nullopt;
  return settable(result->thresholdDb);
}

std::optional<double> Calibrator::applyAnyway() {
  if (!session_ || !session_->calibration)
    return std::nullopt;
  const std::optional<onset::CalibrationResult> &result = session_->calibration->result();
  if (!result || result->notes == 0)
    return std::nullopt;

  const double threshold = result->thresholdDb;
  write(journal_, "calibration: applied anyway, silence {} dBFS", settable(threshold));
  end();
  return threshold;
}

} // namespace training::trainer
