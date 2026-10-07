#define REAPERAPI_MINIMAL
#define REAPERAPI_WANT_GetNumAudioInputs
#define REAPERAPI_WANT_GetInputChannelName
#define REAPERAPI_WANT_GetInputOutputLatency
#define REAPERAPI_WANT_GetPlayStateEx
#define REAPERAPI_WANT_GetPlayPositionEx
#define REAPERAPI_WANT_GetPlayPosition2Ex
#define REAPERAPI_WANT_GetAudioDeviceInfo
#define REAPERAPI_WANT_MIDI_GetRecentInputEvent
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
#include <charconv>
#include <chrono>
#include <cstring>
#include <limits>
#include <ranges>
#include <span>
#include <string_view>
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

/// Целое из сведений об открытой звуковой карте (`GetAudioDeviceInfo`);
/// дробная часть отбрасывается. 0 — карта не открыта или в сведениях не
/// число.
int deviceInfo(const char *attribute) {
  std::array<char, 64> text{};
  if (!GetAudioDeviceInfo(attribute, text.data(), static_cast<int>(text.size())))
    return 0;

  const std::string_view view(text.data());
  int value = 0;
  (void)std::from_chars(view.data(), view.data() + view.size(), value);
  return value;
}

/// Через сколько тренажёр снова ищет выбранный вход MIDI по имени.
constexpr auto kMidiLookupInterval = std::chrono::seconds(1);

/// Бит устройства истории MIDI-входа: вход включён только для управления
/// (`MIDI_GetRecentInputEvent`).
constexpr int kControlOnly = 0x10000;

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
  // Пока выбран вход MIDI, хук сэмплов не берёт.
  input_.setChannel(settings_.midiInput.empty() ? settings_.channel : -1);
  input_.setSilenceDb(settings_.silenceDb);
  bars_.setMode(settings_.mode);
  lookupMidiInput();
  skipMidiHistory();
}

void Trainer::setSettings(const Settings &settings) {
  apply(settings);
  saveSettings(settings_);
}

void Trainer::previewSettings(const Settings &settings) { apply(settings); }

void Trainer::apply(const Settings &settings) {
  const Settings next = clamped(settings);
  const bool inputChanged = next.channel != settings_.channel ||
                            next.midiInput != settings_.midiInput ||
                            next.midiIndex != settings_.midiIndex;
  // Смена входа прерывает калибровку: она меряла другой канал.
  if (session_ && inputChanged)
    endCalibration();

  settings_ = next;
  input_.setChannel(settings_.midiInput.empty() ? settings_.channel : -1);
  input_.setSilenceDb(settings_.silenceDb);
  bars_.setMode(settings_.mode);

  if (inputChanged) {
    // Ноты, пришедшие до смены, и мёртвое время прежнего входа в новый вход
    // не попадают.
    deadTime_.reset();
    skipMidiHistory();
    lookupMidiInput();
    // Состояние — сразу, а не на следующем тике: калибровка, начатая до него,
    // берёт имя и наличие нового входа.
    status_ = currentStatus(wasPlaying_, GetNumAudioInputs());
    journal("input: {}", inputText(settings_));
  }
}

void Trainer::lookupMidiInput() {
  lastMidiLookup_ = std::chrono::steady_clock::now();
  std::optional<MidiInput> found;
  if (!settings_.midiInput.empty())
    found = findMidiInput(midiInputs(), settings_.midiInput, settings_.midiIndex);

  const bool same = found.has_value() == midi_.has_value() &&
                    (!found || (found->device == midi_->device && found->name == midi_->name));
  midi_ = std::move(found);
  if (same || settings_.midiInput.empty())
    return;

  if (midi_)
    journal("midi input «{}»: device {} «{}»", settings_.midiInput, midi_->device,
            midi_->name);
  else
    journal("midi input «{}»: not found", settings_.midiInput);
}

void Trainer::skipMidiHistory() {
  // idx = 0 защёлкивает свежую историю; её последнее событие считается
  // прочитанным.
  std::array<char, 256> bytes{};
  int size = static_cast<int>(bytes.size());
  int timestamp = 0;
  int device = 0;
  int loop = 0;
  double position = 0.0;
  midiSequence_ =
      MIDI_GetRecentInputEvent(0, bytes.data(), &size, &timestamp, &device, &position, &loop);
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

  // Для MIDI — по правилу записи MIDI (design.md D2 изменения add-midi-input).
  midiSampleRate_ = deviceInfo("SRATE");
  midiCompensation_ =
      midiSampleRate_ > 0.0
          ? grid::midiCompensation(deviceInfo("BSIZE"), outputLatency,
                                   intConfig("adjrecmanlat", 0),
                                   intConfig("adjreclat", 1) != 0, midiSampleRate_)
          : 0.0;
}

void Trainer::start() {
  // Запуск транспорта: новый список, а компенсация держится до остановки —
  // как постоянный сдвиг у записи.
  bars_.clear();
  lastRun_.clear();
  lastMidiRun_.clear();
  minRun_ = latestRun_ + 1;
  takeCompensation();

  // Ноты MIDI и мёртвое время прошлого запуска в этот не попадают; count-in —
  // пока позиция стоит (design.md D1, D3, D4 изменения add-midi-input).
  deadTime_.reset();
  skipMidiHistory();
  startPosition_ = GetPlayPosition2Ex(nullptr);
  positionMoved_ = false;

  journal("start: compensation {:.3f} ms, MIDI compensation {:.3f} ms, rate {}, position "
          "{:.6f}",
          compensation_ * 1000.0, midiCompensation_ * 1000.0, Master_GetPlayRate(nullptr),
          startPosition_);
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
  if (!bars_.play(timeline_.beatsAt(heard), loopBeats(heard), settings_.window(), timeline_))
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
  bars_.addNote(time, rate_, settings_.window(), timeline_);
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

  const Status status = currentStatus(playing, channels);
  bool changed = status.measuring != status_.measuring ||
                 status.inputName != status_.inputName || status.midi != status_.midi ||
                 status.compensationMs != status_.compensationMs ||
                 status.rate != status_.rate ||
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
    bars_.addNote(time, rate_, settings_.window(), timeline_);
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

  if (pollMidi(playing))
    changed = true;

  return changed;
}

Status Trainer::currentStatus(bool playing, int channels) {
  Status status;
  status.rate = rate_;
  status.calibrationCancelled = calibrationCancelled_;
  status.midi = !settings_.midiInput.empty();
  const Measuring present = playing ? Measuring::Running : Measuring::Waiting;

  if (!status.midi) {
    status.compensationMs = compensation_ * 1000.0;
    if (settings_.channel < channels)
      if (const char *name = GetInputChannelName(settings_.channel))
        status.inputName = name;
    status.measuring = status.inputName.empty() ? Measuring::NoInput : present;
    return status;
  }

  if (std::chrono::steady_clock::now() - lastMidiLookup_ >= kMidiLookupInterval)
    lookupMidiInput();
  status.compensationMs = midiCompensation_ * 1000.0;
  if (!midi_) {
    status.measuring = Measuring::NoInput;
    return status;
  }
  status.inputName = midi_->name;
  status.measuring = midiInputEnabled(midi_->device) ? present : Measuring::NotEnabled;
  return status;
}

void Trainer::readMidiHistory() {
  // idx = 0 защёлкивает свежее состояние, дальше — к старым событиям до
  // прочитанного на прошлом тике (design.md D1 изменения add-midi-input).
  midiEvents_.clear();
  std::array<char, 256> bytes{};
  int newest = 0;
  for (int index = 0;; ++index) {
    MidiEvent event;
    int size = static_cast<int>(bytes.size());
    const int sequence = MIDI_GetRecentInputEvent(index, bytes.data(), &size, &event.timestamp,
                                                  &event.device, &event.position, &event.loop);
    if (sequence == 0 || sequence <= midiSequence_)
      break;
    if (index == 0)
      newest = sequence;

    event.size = std::clamp(size, 0, static_cast<int>(bytes.size()));
    for (std::size_t i = 0; i < event.bytes.size() && i < static_cast<std::size_t>(event.size);
         ++i)
      event.bytes[i] = static_cast<std::uint8_t>(bytes[i]);
    midiEvents_.push_back(event);
  }
  if (newest != 0)
    midiSequence_ = newest;
}

std::optional<double> Trainer::movedAgo() {
  if (positionMoved_)
    return std::numeric_limits<double>::infinity();

  const double position = GetPlayPosition2Ex(nullptr);
  if (position == startPosition_)
    return std::nullopt;

  positionMoved_ = true;
  const double ago = (position - startPosition_) / rate_;
  journal("position moved {:.1f} ms ago", ago * 1000.0);
  return ago;
}

bool Trainer::pollMidi(bool playing) {
  if (settings_.midiInput.empty())
    return false;

  readMidiHistory();
  if (!playing || !midi_ || status_.measuring != Measuring::Running)
    return false;
  const int device = midi_->device;

  // Count-in: позиция стоит у начала записи, и места нот MIDI на шкале нет
  // (design.md D3 изменения add-midi-input). Нота, пришедшая раньше, чем
  // позиция сдвинулась, отбрасывается.
  const std::optional<double> moved = movedAgo();
  bool added = false;
  for (const MidiEvent &event : std::views::reverse(midiEvents_)) {
    const auto message = std::span<const std::uint8_t>(event.bytes)
                             .first(static_cast<std::size_t>(std::min(event.size, 3)));
    if ((event.device & 0xFFFF) != device || (event.device & kControlOnly) != 0 ||
        event.position < 0.0 || !onset::isNoteStart(message))
      continue;

    const int channel = (event.bytes[0] & 0x0F) + 1;
    const int note = event.bytes[1];
    const int velocity = event.bytes[2];
    // Сколько прошло от прихода ноты до опроса истории — это и задержка показа:
    // окно перерисуется на этом же тике.
    const double age =
        midiSampleRate_ > 0.0 ? -static_cast<double>(event.timestamp) / midiSampleRate_ : 0.0;
    if (!moved || age > *moved) {
      journal("midi note dropped by count-in: pp={:.6f} note={} channel={} velocity={}, "
              "arrived {:.1f} ms ago",
              event.position, note, channel, velocity, age * 1000.0);
      continue;
    }

    // Мёртвое время — по времени ноты на шкале проекта и кончается с проходом
    // петли (design.md D4 изменения add-midi-input).
    if (event.loop != midiLoop_)
      deadTime_.reset();
    midiLoop_ = event.loop;
    const double time = event.position - midiCompensation_ * rate_;
    if (!deadTime_.add(time)) {
      journal("midi note skipped in dead time: t={:.6f} pp={:.6f} loop={} note={} channel={} "
              "velocity={}",
              time, event.position, event.loop, note, channel, velocity);
      continue;
    }

    const double wrapped = loopWrapped(time, event.position);
    bars_.addNote(wrapped, rate_, settings_.window(), timeline_);
    lastMidiRun_.push_back(time);
    added = true;
    journal("midi note t={:.6f} pp={:.6f} loop={} note={} channel={} velocity={}, shown "
            "after {:.1f} ms",
            wrapped, event.position, event.loop, note, channel, velocity, age * 1000.0);
  }
  return added;
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
  if (wasPlaying_ || !settings_.midiInput.empty() || status_.measuring == Measuring::NoInput)
    return false;

  endCalibration();
  // Куски прошлой сессии, которые ещё в очереди, — не этой калибровки.
  while (input_.popCalibration(chunkSamples_)) {
  }

  CalibrationSession &session = session_.emplace();
  session.previousSilenceDb = settings_.silenceDb;
  session.channelName = status_.inputName;
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
