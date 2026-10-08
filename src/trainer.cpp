#define REAPERAPI_MINIMAL
#define REAPERAPI_WANT_GetInputChannelName
#define REAPERAPI_WANT_GetInputOutputLatency
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

#include "clock.hpp"
#include "journal.hpp"
#include "window.hpp"

#include "training/grid/note_time.hpp"
#include "training/trainer/audio_mode.hpp"
#include "training/trainer/clock.hpp"
#include "training/trainer/journal.hpp"
#include "training/trainer/loop.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <utility>

namespace training::reaper {
namespace {

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

void onMainTimer() {
  if (instance && instance->poll())
    refreshWindow();
}

/// Приёмник строк журнала ядра тренажёра: журнал расширения.
trainer::Journal journalSink() {
  return [](std::string_view line) { journalLine(line); };
}

/// Компенсация задержки входов сейчас — по правилу, по которому REAPER
/// сдвигает запись звука (`grid::compensation`) и MIDI
/// (`grid::midiCompensation`).
///
/// @param sampleRate частота звуковой карты по последнему блоку звука, Гц;
///   0 — блоков не было.
trainer::Compensation compensationNow(double sampleRate) {
  int inputLatency = 0;
  int outputLatency = 0;
  GetInputOutputLatency(&inputLatency, &outputLatency);
  trainer::Compensation compensation{.audioSampleRate = sampleRate,
                                     .midiSampleRate =
                                         static_cast<double>(deviceInfo("SRATE"))};
  if (sampleRate > 0.0) {
    compensation.inputLatency = static_cast<double>(inputLatency) / sampleRate;
    compensation.audio = grid::compensation(
        inputLatency, outputLatency, intConfig("adjrecmanlatin", 0),
        intConfig("adjrecmanlat", 0), intConfig("adjreclat", 1) != 0, sampleRate);
  }
  if (compensation.midiSampleRate > 0.0)
    compensation.midi = grid::midiCompensation(
        deviceInfo("BSIZE"), outputLatency, intConfig("adjrecmanlat", 0),
        intConfig("adjreclat", 1) != 0, compensation.midiSampleRate);
  return compensation;
}

/// Петля активного проекта: повтор и границы.
trainer::Loop projectLoop() {
  trainer::Loop loop{.repeat = GetSetRepeat(-1) == 1, .start = 0.0, .end = 0.0};
  GetSet_LoopTimeRange2(nullptr, false, true, &loop.start, &loop.end, false);
  return loop;
}

} // namespace

Trainer::Trainer()
    : transport_(journalSink()), input_(transport_.onsetSearch()),
      rate_(Master_GetPlayRate(nullptr)), settings_(loadSettings()),
      calibrator_(journalSink()) {
  input_.setChannel(trainer::audioChannel(settings_));
  input_.setSilenceDb(settings_.silenceDb);
  transport_.setMode(settings_.mode);
  refreshInputs();
  skipMidiHistory();
}

void Trainer::setSettings(const Settings &settings) {
  apply(settings);
  saveSettings(settings_);
}

void Trainer::previewSettings(const Settings &settings) { apply(settings); }

void Trainer::apply(const Settings &settings) {
  const Settings next = clamped(settings);
  const bool inputChanged = trainer::inputChanged(settings_, next);
  if (inputChanged) {
    calibrator_.onInputChange();
    transport_.midiNotes().onInputChange();
  }

  settings_ = next;
  input_.setChannel(trainer::audioChannel(settings_));
  input_.setSilenceDb(settings_.silenceDb);
  updateAudioMode();
  transport_.setMode(settings_.mode);

  if (inputChanged) {
    // Ноты, пришедшие до смены, в новый вход не попадают.
    skipMidiHistory();
    refreshInputs();
    // Состояние — сразу, а не на следующем тике: калибровка, начатая до него,
    // берёт имя и наличие нового входа.
    updateMeasurement();
    journal("input: {}", inputText(settings_));
  }
}

void Trainer::refreshInputs() {
  lookupMidiInput();
  const char *name = GetInputChannelName(settings_.channel);
  inputs_.channelName = name ? name : "";
  inputs_.midiName = midi_ ? std::optional(midi_->name) : std::nullopt;
  inputs_.midiEnabled = midi_ && midiInputEnabled(midi_->device);
}

void Trainer::lookupMidiInput() {
  std::optional<MidiInput> found;
  if (!settings_.midiInput.empty())
    found = findMidiInput(midiInputs(), settings_.midiInput, settings_.midiIndex);

  // По номеру в настройках смена номеров устройств узнаёт выбранный вход
  // (onMidiDeviceRemap).
  if (found && found->device != settings_.midiIndex) {
    settings_.midiIndex = found->device;
    saveSettings(settings_);
  }

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

void Trainer::onMidiDeviceRemap(bool output, int from, int to) {
  if (output)
    return;

  if (midi_)
    midi_->device = remappedDevice(midi_->device, output, from, to);
  const int index = remappedDevice(settings_.midiIndex, output, from, to);
  if (index != settings_.midiIndex) {
    journal("midi input «{}»: device {} -> {}", settings_.midiInput, from, to);
    settings_.midiIndex = index;
    saveSettings(settings_);
  }

  // Со сменой номеров устройство могло появиться или пропасть.
  refreshInputs();
}

void Trainer::updateMeasurement() {
  inputs_.channelPresent = input_.channelPresent(settings_.channel);
  measurement_ = trainer::measurementOf(settings_, inputs_, transport_.running(),
                                        transport_.compensation(), current_, rate_,
                                        calibrator_.cancelledByPlayback());
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

void Trainer::readMidiHistory() {
  // idx = 0 защёлкивает свежее состояние, дальше — к старым событиям до
  // прочитанного на прошлом тике.
  midiEvents_.clear();
  const double sampleRate = transport_.compensation().midiSampleRate;
  std::array<char, 256> bytes{};
  int newest = 0;
  for (int index = 0;; ++index) {
    trainer::MidiEvent event;
    int size = static_cast<int>(bytes.size());
    int timestamp = 0;
    const int sequence = MIDI_GetRecentInputEvent(index, bytes.data(), &size, &timestamp,
                                                  &event.device, &event.position, &event.loop);
    if (sequence == 0 || sequence <= midiSequence_)
      break;
    if (index == 0)
      newest = sequence;

    // Ядро меряет время в секундах шкалы проекта: сэмплы от прихода события
    // до чтения — в реальные секунды, а их — в секунды проекта.
    if (sampleRate > 0.0)
      event.age = -static_cast<double>(timestamp) / sampleRate * rate_;

    event.size = std::clamp(size, 0, static_cast<int>(bytes.size()));
    for (std::size_t i = 0; i < event.bytes.size() && i < static_cast<std::size_t>(event.size);
         ++i)
      event.bytes[i] = static_cast<std::uint8_t>(bytes[i]);
    midiEvents_.push_back(event);
  }
  if (newest != 0)
    midiSequence_ = newest;
  // История отдаёт события от новых к старым, ядро ждёт от старых к новым.
  std::ranges::reverse(midiEvents_);
}

void Trainer::onPlayState(bool play, bool pause) {
  if (play && !pause) {
    // Компенсация держится до конца запуска — как постоянный сдвиг у записи.
    // Позиция начала берётся в момент запуска, до того как её сдвинул звук:
    // count-in — пока позиция стоит.
    const trainer::Moment now = momentNow();
    const double position = GetPlayPosition2Ex(nullptr);
    if (transport_.start(now, position, compensationNow(input_.sampleRate()))) {
      const trainer::Compensation &compensation = transport_.compensation();
      calibrator_.onStart();
      journal("start: at {}, compensation {:.3f} ms, MIDI compensation {:.3f} ms, rate {}, "
              "position {:.6f}",
              trainer::momentText(now), compensation.audio * 1000.0,
              compensation.midi * 1000.0, rate_, position);
    }
  } else if (transport_.stop()) {
    calibrator_.onStop();
  }
  updateAudioMode();
}

void Trainer::onPlayRate(double rate) { rate_ = rate; }

bool Trainer::poll() {
  // Транспорт и скорость сообщает REAPER (onPlayState, onPlayRate). Строка
  // состояния без запуска показывает компенсацию, которая будет действовать
  // при запуске.
  current_ = compensationNow(input_.sampleRate());
  const Measurement before = measurement_;
  updateMeasurement();
  bool changed = measurement_ != before;

  if (pollCalibration())
    changed = true;

  // Позиция сообщается до нот этого тика: строку ноты свод выбирает по свежей
  // позиции.
  if (transport_.running()) {
    followPlayback();
    changed = true;
  }
  if (takeOnsets())
    changed = true;
  if (takeMidiNotes())
    changed = true;
  return changed;
}

void Trainer::followPlayback() {
  // Верхняя строка и текущий удар — по слышимой позиции.
  const double heard = GetPlayPositionEx(nullptr);
  if (!transport_.play(timeline_.beatsAt(heard),
                       trainer::loopBeats(heard, projectLoop(), timeline_), settings_.window(),
                       timeline_))
    return;

  for (const grid::BarRow &row : transport_.bars().rows()) {
    if (row.entered && !row.folded) {
      journal("bar {} entered at {:.3f}", row.bar.index + 1, heard);
      return;
    }
  }
}

bool Trainer::takeOnsets() {
  bool taken = false;
  const trainer::Compensation &compensation = transport_.compensation();
  while (const auto hit = input_.popOnset()) {
    if (!transport_.takesOnset(hit->monotonic))
      continue;

    const double time =
        trainer::loopWrapped(grid::noteTime(hit->blockPosition, hit->offset, hit->sampleRate,
                                            compensation.audio, rate_),
                             hit->blockPosition, projectLoop(), settings_.mode, timeline_);
    transport_.addNote(time, rate_, settings_.window(), timeline_);
    taken = true;

    // Задержка показа: от блока атаки до этого тика плюс задержка ввода,
    // которую звук прошёл до хука. Окно перерисуется на этом же тике.
    const double ageMs =
        std::chrono::duration<double, std::milli>(momentNow() - hit->monotonic).count() +
        compensation.inputLatency * 1000.0;
    journal("note t={:.6f} level={:.1f} dB, shown after {:.1f} ms", time, hit->levelDb, ageMs);
  }
  return taken;
}

bool Trainer::takeMidiNotes() {
  readMidiHistory();
  // Компенсация — в секундах шкалы проекта, как всё время нот MIDI в ядре.
  const trainer::MidiRead read{.measuring = trainer::measuresMidi(measurement_),
                               .device = midi_ ? midi_->device : -1,
                               .position = GetPlayPosition2Ex(nullptr),
                               .compensation = transport_.compensation().midi * rate_};
  const std::vector<trainer::MidiNote> notes = transport_.midiNotes().take(midiEvents_, read);
  for (const trainer::MidiNote &note : notes) {
    const double time = trainer::loopWrapped(note.time, note.event.position, projectLoop(),
                                             settings_.mode, timeline_);
    transport_.addNote(time, rate_, settings_.window(), timeline_);
    // Возраст ноты в реальном времени — и задержка показа: окно перерисуется
    // на этом же тике.
    journal("midi note t={:.6f} pp={:.6f} loop={} {}, shown after {:.1f} ms", time,
            note.event.position, note.event.loop, trainer::noteText(note.event),
            note.event.age / rate_ * 1000.0);
  }
  return !notes.empty();
}

bool Trainer::pollCalibration() {
  while (const auto chunk = input_.popCalibration(chunkSamples_))
    calibrator_.feed(chunkSamples_, chunk->sampleRate, chunk->gap);

  const trainer::CalibrationUpdate update = calibrator_.update(momentNow());
  updateAudioMode();
  if (update.silenceDb)
    setSilence(*update.silenceDb);
  return update.changed;
}

void Trainer::setSilence(double silenceDb) {
  Settings settings = settings_;
  settings.silenceDb = silenceDb;
  setSettings(settings);
  showSettings();
}

bool Trainer::startCalibration() {
  if (!calibrator_.start(transport_.running(), measurement_, settings_.channel,
                         settings_.silenceDb, momentNow()))
    return false;

  // Куски прошлой сессии, которые ещё в очереди, — не этой калибровки.
  while (input_.popCalibration(chunkSamples_)) {
  }
  updateAudioMode();

  // REAPER закрывает звуковую карту при остановке, если так настроено; без
  // неё хук не вызывается. Калибровка открывает её один раз: если REAPER
  // закроет карту снова, звука не будет, и калибровка оборвётся «нет звука».
  if (Audio_IsRunning() == 0) {
    Audio_Init();
    journal("calibration: audio device was closed, opened: {}", Audio_IsRunning() != 0);
  }
  return true;
}

void Trainer::endCalibration() {
  calibrator_.end();
  updateAudioMode();
}

void Trainer::applyCalibration() {
  const std::optional<double> silenceDb = calibrator_.applyAnyway();
  updateAudioMode();
  if (silenceDb)
    setSilence(*silenceDb);
}

void Trainer::updateAudioMode() {
  input_.setMode(trainer::audioModeOf(transport_, calibrator_));
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
