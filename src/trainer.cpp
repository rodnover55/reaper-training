#define REAPERAPI_MINIMAL
#define REAPERAPI_WANT_GetNumAudioInputs
#define REAPERAPI_WANT_GetInputChannelName
#define REAPERAPI_WANT_GetInputOutputLatency
#define REAPERAPI_WANT_GetPlayStateEx
#define REAPERAPI_WANT_GetPlayPositionEx
#define REAPERAPI_WANT_Master_GetPlayRate
#define REAPERAPI_WANT_GetSetRepeat
#define REAPERAPI_WANT_GetSet_LoopTimeRange2
#define REAPERAPI_WANT_get_config_var
#define REAPERAPI_WANT_Audio_IsRunning
#define REAPERAPI_WANT_Audio_Init

#include "trainer.hpp"

#include <reaper_plugin_functions.h>

#include "journal.hpp"
#include "window.hpp"

#include "training/grid/note_time.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <utility>

namespace training::reaper {
namespace {

// Биты состояния транспорта (`GetPlayStateEx`).
constexpr int kPlaying = 1;
constexpr int kPaused = 2;
constexpr int kRecording = 4;

// Обычный указатель, а не unique_ptr: деструктор статического объекта
// вызывает любой exit() в процессе, в том числе в копии REAPER, которую тот
// делает fork, чтобы запустить программу. В копии нет рабочего потока
// AudioInput, и ожидание его конца вешало её навсегда вместе с открытой
// звуковой картой REAPER. Тренажёр уничтожает только shutdownTrainer.
Trainer *instance = nullptr;

int (*hostRegister)(const char *name, void *infostruct) = nullptr;

/// Целая настройка REAPER по имени (`get_config_var`); нет — `fallback`.
int intConfig(const char *name, int fallback) {
  int size = 0;
  const void *address = get_config_var(name, &size);
  if (!address || size != sizeof(int))
    return fallback;

  int value = 0;
  std::memcpy(&value, address, sizeof(value));
  return value;
}

/// Сколько секунд без звука от хука обрывают калибровку.
constexpr double kNoSoundSeconds = 2.0;

/// Не чаще чем через столько калибровка пробует открыть закрытую звуковую
/// карту.
constexpr auto kOpenDeviceInterval = std::chrono::seconds(1);

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

void onMainTimer() {
  if (instance && instance->poll())
    refreshWindow();
}

} // namespace

Trainer::Trainer() : settings_(loadSettings()) {
  input_.setChannel(settings_.channel);
  input_.setSilenceDb(settings_.silenceDb);
  bars_.setMode(settings_.mode);
}

void Trainer::setSettings(const Settings &settings) {
  // Смена входа прерывает калибровку: она меряла другой канал.
  if (session_ && clamped(settings).channel != settings_.channel)
    endCalibration();

  settings_ = clamped(settings);
  saveSettings(settings_);
  input_.setChannel(settings_.channel);
  input_.setSilenceDb(settings_.silenceDb);
  bars_.setMode(settings_.mode);
}

void Trainer::takeCompensation() {
  // Компенсация — по правилу записи REAPER (design.md D2, findings.md R2).
  sampleRate_ = input_.sampleRate();
  int inputLatency = 0;
  int outputLatency = 0;
  GetInputOutputLatency(&inputLatency, &outputLatency);
  inputLatency_ = sampleRate_ > 0.0 ? static_cast<double>(inputLatency) / sampleRate_ : 0.0;
  compensation_ =
      sampleRate_ > 0.0
          ? grid::compensation(inputLatency, outputLatency, intConfig("adjrecmanlatin", 0),
                               intConfig("adjrecmanlat", 0), intConfig("adjreclat", 1) != 0,
                               sampleRate_)
          : 0.0;
}

void Trainer::start() {
  // Запуск транспорта: новый список, а компенсация держится до остановки —
  // как постоянный сдвиг у записи.
  bars_.clear();
  lastRun_.clear();
  minRun_ = latestRun_ + 1;
  takeCompensation();

  journal("start: compensation {:.3f} ms, rate {}", compensation_ * 1000.0,
          Master_GetPlayRate(nullptr));
}

double Trainer::loopWrapped(double time, double blockPosition) const {
  // Петля с повтором: время ноты внутри петли заворачивается в неё (design.md
  // D2, findings.md R4).
  if (GetSetRepeat(-1) != 1)
    return time;

  double loopStart = 0.0;
  double loopEnd = 0.0;
  GetSet_LoopTimeRange2(nullptr, false, true, &loopStart, &loopEnd, false);
  if (loopEnd <= loopStart || blockPosition < loopStart || blockPosition > loopEnd)
    return time;

  // Окно заворота сдвинуто назад на половину интервала между узлами: узел на
  // конце петли — это первый узел следующего прохода. Так ранняя нота к первой
  // доле петли остаётся у её начала, а не уходит к доле после конца петли.
  const double step = 1.0 / static_cast<double>(grid::divisions(settings_.mode));
  const double half =
      (timeline_.timeAt(timeline_.beatsAt(loopStart) + step) - loopStart) / 2.0;
  return grid::wrapIntoLoop(time, loopStart - half, loopEnd - half);
}

double Trainer::loopBeats(double heard) const {
  // Петля с повтором и позиция в ней: свод сравнивает места узлов с позицией по
  // кругу петли (design.md D3).
  if (GetSetRepeat(-1) != 1)
    return 0.0;

  double loopStart = 0.0;
  double loopEnd = 0.0;
  GetSet_LoopTimeRange2(nullptr, false, true, &loopStart, &loopEnd, false);
  if (loopEnd <= loopStart || heard < loopStart || heard > loopEnd)
    return 0.0;
  return timeline_.beatsAt(loopEnd) - timeline_.beatsAt(loopStart);
}

void Trainer::followPlayback() {
  // Верхняя строка и текущий удар — по слышимой позиции (design.md D2).
  const double heard = GetPlayPositionEx(nullptr);
  if (!bars_.play(timeline_.beatsAt(heard), loopBeats(heard), settings_.toleranceMs,
                  timeline_))
    return;

  for (const grid::BarRow &row : bars_.rows()) {
    if (row.entered && !row.folded) {
      journal("bar {} entered at {:.3f}", row.bar.index + 1, heard);
      return;
    }
  }
}

void Trainer::addTestNote(double time) {
  if (!wasPlaying_)
    return;
  bars_.addNote(time, rate_, settings_.toleranceMs, timeline_);
  journal("test note t={:.6f}", time);
  refreshWindow();
}

bool Trainer::poll() {
  const int channels = GetNumAudioInputs();
  input_.setInputChannels(channels);

  const int state = GetPlayStateEx(nullptr);
  const bool playing = (state & kPaused) == 0 && (state & (kPlaying | kRecording)) != 0;
  // Пока транспорт стоит, строка состояния показывает компенсацию, которая
  // будет действовать при запуске.
  if (playing && !wasPlaying_) {
    start();
  } else if (!playing) {
    takeCompensation();
    if (wasPlaying_)
      bars_.stop();
  }
  wasPlaying_ = playing;

  stopCalibrationOnPlayback(playing);

  rate_ = Master_GetPlayRate(nullptr);

  Status status;
  status.rate = rate_;
  status.compensationMs = compensation_ * 1000.0;
  status.calibrationCancelled = calibrationCancelled_;
  if (settings_.channel < channels)
    if (const char *name = GetInputChannelName(settings_.channel))
      status.channelName = name;
  if (status.channelName.empty())
    status.measuring = Measuring::NoChannel;
  else
    status.measuring = playing ? Measuring::Running : Measuring::Waiting;

  bool changed =
      status.measuring != status_.measuring || status.channelName != status_.channelName ||
      status.compensationMs != status_.compensationMs || status.rate != status_.rate ||
      status.calibrationCancelled != status_.calibrationCancelled;
  status_ = status;

  if (session_ && pollCalibration())
    changed = true;

  // Позиция сообщается до нот этого тика: строку ноты свод выбирает по свежей
  // позиции.
  if (playing) {
    followPlayback();
    changed = true;
  }

  while (const auto hit = input_.popOnset()) {
    latestRun_ = std::max(latestRun_, hit->run);
    if (!playing || hit->run < minRun_ || sampleRate_ <= 0.0)
      continue;

    const double time = loopWrapped(
        grid::noteTime(hit->blockPosition, hit->offset, hit->sampleRate, compensation_, rate_),
        hit->blockPosition);
    bars_.addNote(time, rate_, settings_.toleranceMs, timeline_);
    lastRun_.push_back(*hit);
    changed = true;

    // Задержка показа (design.md D10): от блока атаки до этого тика плюс
    // задержка ввода, которую звук прошёл до хука. Окно перерисуется на этом
    // же тике.
    const double age =
        std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
            .count() -
        hit->monotonic + inputLatency_;
    journal("note t={:.6f} level={:.1f} dB, shown after {:.1f} ms", time, hit->levelDb,
            age * 1000.0);
  }

  return changed;
}

void Trainer::stopCalibrationOnPlayback(bool playing) {
  if (!playing) {
    calibrationCancelled_ = false;
    return;
  }
  if (!session_)
    return;

  // Запуск транспорта прерывает шаги калибровки, а показанный итог просто
  // закрывает.
  const bool finished = calibrationFinished();
  journal("calibration: {} by playback", finished ? "closed" : "cancelled");
  endCalibration();
  calibrationCancelled_ = !finished;
}

bool Trainer::startCalibration() {
  if (wasPlaying_ || status_.measuring == Measuring::NoChannel)
    return false;

  endCalibration();
  // Куски прошлой сессии, которые ещё в очереди, — не этой калибровки.
  while (input_.popCalibration(chunkSamples_)) {
  }

  CalibrationSession &session = session_.emplace();
  session.previousSilenceDb = settings_.silenceDb;
  session.channelName = status_.channelName;
  lastSound_ = std::chrono::steady_clock::now();
  lastOpenAttempt_ = {};
  calibrationFinished_ = false;
  calibrationCancelled_ = false;
  input_.setCalibrating(!testSound_);
  journal("calibration: started on input {} ({}), silence {} dBFS", settings_.channel + 1,
          session.channelName, settings_.silenceDb);
  return true;
}

bool Trainer::startCalibration(std::vector<float> samples, double sampleRate) {
  const std::size_t count = samples.size();
  TestSound sound{.samples = std::move(samples),
                  .sampleRate = sampleRate,
                  .startedAt = std::chrono::steady_clock::now()};
  endCalibration();
  testSound_ = std::move(sound);
  if (startCalibration()) {
    journal("calibration: test sound, {} samples at {} Hz", count, sampleRate);
    return true;
  }
  testSound_.reset();
  return false;
}

void Trainer::endCalibration() {
  if (!session_)
    return;

  session_.reset();
  testSound_.reset();
  input_.setCalibrating(false);
  journal("calibration: ended");
}

void Trainer::applyCalibration() {
  if (!session_ || !session_->calibration)
    return;
  const std::optional<onset::CalibrationResult> &result = session_->calibration->result();
  if (!result || result->notes == 0)
    return;

  Settings settings = settings_;
  settings.silenceDb = result->thresholdDb;
  setSettings(settings);
  journal("calibration: applied anyway, silence {} dBFS", settings_.silenceDb);
  endCalibration();
  showSettings();
}

bool Trainer::pollCalibration() {
  if (!session_)
    return false;
  CalibrationSession &session = *session_;

  const auto now = std::chrono::steady_clock::now();
  bool heard = false;

  if (!testSound_ && !session.noSound)
    openAudioDevice(now);

  if (testSound_) {
    TestSound &sound = *testSound_;
    // Звук без гитары идёт вместо входа: куски входа выбрасываются.
    while (input_.popCalibration(chunkSamples_)) {
    }
    const double elapsed = std::chrono::duration<double>(now - sound.startedAt).count();
    const std::size_t due =
        std::min(sound.samples.size(),
                 static_cast<std::size_t>(std::max(0.0, elapsed * sound.sampleRate)));
    if (due > sound.fed) {
      feedCalibration(
          std::span<const float>(sound.samples).subspan(sound.fed, due - sound.fed),
          sound.sampleRate, false);
      sound.fed = due;
      heard = true;
    }
  } else {
    while (const auto chunk = input_.popCalibration(chunkSamples_)) {
      feedCalibration(chunkSamples_, chunk->sampleRate, chunk->gap);
      heard = true;
    }
  }

  // После итога звук идёт в калибровку дальше: удар по струнам закрывает
  // панель.
  if (calibrationFinished_ && session.calibration && session.calibration->soundAfterResult()) {
    journal("calibration: closed by sound after the result");
    endCalibration();
    return true;
  }
  if (calibrationFinished_ || session.noSound)
    return false;

  if (heard)
    lastSound_ = now;

  if (session.calibration && session.calibration->result()) {
    finishCalibration();
    return true;
  }

  if (std::chrono::duration<double>(now - lastSound_).count() >= kNoSoundSeconds) {
    session.noSound = true;
    input_.setCalibrating(false);
    journal("calibration: no sound from the audio device for {} s", kNoSoundSeconds);
    return true;
  }
  return heard;
}

void Trainer::openAudioDevice(std::chrono::steady_clock::time_point now) {
  // REAPER закрывает звуковую карту при остановке, если так настроено; без
  // неё хук не вызывается, и калибровке нечего слушать.
  if (Audio_IsRunning() != 0 || now - lastOpenAttempt_ < kOpenDeviceInterval)
    return;

  lastOpenAttempt_ = now;
  Audio_Init();
  journal("calibration: audio device was closed, opened: {}", Audio_IsRunning() != 0);
}

void Trainer::feedCalibration(std::span<const float> samples, double sampleRate, bool gap) {
  if (!session_ || session_->noSound)
    return;

  std::optional<onset::Calibration> &stored = session_->calibration;
  if (stored && stored->sampleRate() == sampleRate) {
    if (gap) {
      stored->discontinuity();
      journal("calibration: gap in the input");
    }
  } else if (calibrationFinished_) {
    // Итог уже подведён: звук другой частоты в нём ничего не меняет.
    return;
  } else {
    // Новая частота — другие уровни фильтра и другое время шагов: заново.
    stored.emplace(sampleRate);
    journal("calibration: {} Hz, silence step", sampleRate);
  }
  if (!stored)
    return;
  onset::Calibration &calibration = *stored;

  const onset::CalibrationStep step = calibration.step();
  const std::size_t attacks = calibration.attacksDb().size();
  const std::optional<double> sound = calibration.secondsSinceSound();
  calibration.process(samples);

  const std::optional<double> nowSound = calibration.secondsSinceSound();
  if (nowSound && (!sound || *nowSound < *sound))
    journal("calibration: sound during silence, silence step counts again");
  if (step == onset::CalibrationStep::Silence &&
      calibration.step() != onset::CalibrationStep::Silence)
    journal("calibration: noise {:.1f} dBFS, notes step", calibration.noiseDb());
  for (std::size_t i = attacks; i < calibration.attacksDb().size(); ++i)
    journal("calibration: note {} attack {:.1f} dBFS", i + 1, calibration.attacksDb()[i]);
}

void Trainer::finishCalibration() {
  if (!session_ || !session_->calibration)
    return;
  const std::optional<onset::CalibrationResult> &stored = session_->calibration->result();
  if (!stored)
    return;
  const onset::CalibrationResult result = *stored;
  const double previous = session_->previousSilenceDb;

  calibrationFinished_ = true;
  journal("calibration: {}, noise {:.1f}, attacks {} from {:.1f} to {:.1f}, peak {:.1f}, "
          "threshold {} dBFS",
          verdictName(result.verdict), result.noiseDb, result.notes, result.softestDb,
          result.loudestDb, result.peakDb, result.thresholdDb);

  if (result.verdict != onset::Verdict::Good)
    return;

  Settings settings = settings_;
  settings.silenceDb = result.thresholdDb;
  setSettings(settings);
  if (session_)
    session_->applied = true;
  showSettings();
  journal("calibration: silence {} -> {} dBFS", previous, settings_.silenceDb);
}

Trainer &trainer() { return *instance; }

void initTrainer(reaper_plugin_info_t *rec) {
  instance = new Trainer();

  if (!rec || !rec->Register)
    return;

  hostRegister = rec->Register;
  hostRegister("timer", reinterpret_cast<void *>(onMainTimer));
}

void shutdownTrainer() {
  if (hostRegister)
    hostRegister("-timer", reinterpret_cast<void *>(onMainTimer));

  delete std::exchange(instance, nullptr);
}

} // namespace training::reaper
