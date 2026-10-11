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
#define REAPERAPI_WANT_GetCursorPosition
#define REAPERAPI_WANT_projectconfig_var_addr
#define REAPERAPI_WANT_projectconfig_var_getoffs

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
#include <string>
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

/// Настройка активного проекта REAPER по имени (`projectconfig_var_getoffs`)
/// типа `T`; нет или другого размера — пусто.
template <class T> std::optional<T> projectConfig(const char *name) {
  int size = 0;
  const int offset = projectconfig_var_getoffs(name, &size);
  if (size != static_cast<int>(sizeof(T)))
    return std::nullopt;
  const void *address = projectconfig_var_addr(nullptr, offset);
  if (!address)
    return std::nullopt;

  T value{};
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

/// Отсчёт перед запуском с позиции `position` по настройкам проекта: метроном
/// и отсчёт — `projmetroen`, тактов — `projmetrocountin`, с начала такта —
/// бит 8 `preroll`. Длина такта — по темпу и размеру в позиции начала.
///
/// @param sampleRate частота звуковой карты, Гц.
trainer::CountIn projectCountIn(double position, bool record, const grid::Timeline &timeline,
                                double rate, double sampleRate) {
  const int metronome = projectConfig<int>("projmetroen").value_or(0);
  const int preroll = projectConfig<int>("preroll").value_or(intConfig("preroll", 0));
  const trainer::CountInSettings settings{
      .metronome = (metronome & 1) != 0,
      .beforePlayback = (metronome & 8) != 0,
      .beforeRecording = (metronome & 16) != 0,
      .bars = projectConfig<double>("projmetrocountin").value_or(0.0),
      .fromBarStart = (preroll & 8) != 0};

  return trainer::countInAt(settings, record, timeline, position, rate, sampleRate,
                            deviceInfo("BSIZE"));
}

/// Такт, которым кончается затакт запуска с позиции `position`: пре-ролл
/// кончается на курсоре редактирования, если для этого запуска он включён
/// (бит 1 `preroll` — перед воспроизведением, бит 2 — перед записью); без
/// пре-ролла отсчёт кончается тактом позиции начала. Такт места —
/// `trainer::barAt`. Пусто — затакта нет.
///
/// @param sampleRate частота звуковой карты, Гц.
std::optional<std::int64_t> leadInEndAt(double position, bool record,
                                        const trainer::CountIn &countIn,
                                        const grid::Timeline &timeline, double sampleRate) {
  const int preroll = projectConfig<int>("preroll").value_or(intConfig("preroll", 0));
  const bool prerollOn = (preroll & (record ? 2 : 1)) != 0;
  const double cursor = GetCursorPosition();
  if (prerollOn && cursor > position)
    return trainer::barAt(timeline, cursor, sampleRate).index;
  if (countIn.samples > 0)
    return trainer::barAt(timeline, position, sampleRate).index;
  return std::nullopt;
}

/// Петля активного проекта: повтор и границы.
trainer::Loop projectLoop() {
  trainer::Loop loop{.repeat = GetSetRepeat(-1) == 1, .start = 0.0, .end = 0.0};
  GetSet_LoopTimeRange2(nullptr, false, true, &loop.start, &loop.end, false);
  return loop;
}

} // namespace

Trainer::Trainer() : Trainer(loadSettings()) {}

Trainer::Trainer(const trainer::SettingsRead &read)
    : transport_(journalSink()), rate_(Master_GetPlayRate(nullptr)),
      input_(transport_.onsetSearch()), settings_(read.settings),
      fileNotice_(kSettingsFile, read.rejected), calibrator_(journalSink()) {
  for (const std::string &line : read.rejected)
    journal("{}: {}", kSettingsFile, line);
  input_.setChannel(trainer::audioChannel(settings_));
  input_.setSilenceDb(settings_.silenceDb());
  transport_.setMode(settings_.mode());
  refreshInputs();
  skipMidiHistory();
}

void Trainer::setSettings(const Settings &settings) {
  apply(settings);
  saveSettings(settings_);
}

void Trainer::previewSettings(const Settings &settings) { apply(settings); }

void Trainer::apply(const Settings &settings) {
  const bool inputChanged = trainer::inputChanged(settings_, settings);
  if (inputChanged) {
    calibrator_.onInputChange();
    transport_.midiNotes().onInputChange();
  }

  settings_ = settings;
  input_.setChannel(trainer::audioChannel(settings_));
  input_.setSilenceDb(settings_.silenceDb());
  updateAudioMode();
  transport_.setMode(settings_.mode());

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
  const char *name = GetInputChannelName(settings_.channel());
  inputs_.channelName = name ? name : "";
  inputs_.midiName = midi_ ? std::optional(midi_->name) : std::nullopt;
  inputs_.midiEnabled = midi_ && midiInputEnabled(midi_->device);
}

void Trainer::lookupMidiInput() {
  std::optional<MidiInput> found;
  if (!settings_.midiInput().empty())
    found = findMidiInput(midiInputs(), settings_.midiInput(), settings_.midiIndex());

  // По номеру в настройках смена номеров устройств узнаёт выбранный вход
  // (onMidiDeviceRemap).
  if (found && found->device != settings_.midiIndex()) {
    settings_.setMidiIndex(found->device);
    saveSettings(settings_);
  }

  const bool same = found.has_value() == midi_.has_value() &&
                    (!found || (found->device == midi_->device && found->name == midi_->name));
  midi_ = std::move(found);
  if (same || settings_.midiInput().empty())
    return;

  if (midi_)
    journal("midi input «{}»: device {} «{}»", settings_.midiInput(), midi_->device,
            midi_->name);
  else
    journal("midi input «{}»: not found", settings_.midiInput());
}

void Trainer::onMidiDeviceRemap(bool output, int from, int to) {
  if (output)
    return;

  if (midi_)
    midi_->device = remappedDevice(midi_->device, output, from, to);
  const int index = remappedDevice(settings_.midiIndex(), output, from, to);
  if (index != settings_.midiIndex()) {
    journal("midi input «{}»: device {} -> {}", settings_.midiInput(), from, to);
    settings_.setMidiIndex(index);
    saveSettings(settings_);
  }

  // Со сменой номеров устройство могло появиться или пропасть.
  refreshInputs();
}

void Trainer::updateMeasurement() {
  inputs_.channelPresent = input_.channelPresent(settings_.channel());
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
  const double sampleRate = transport_.running() ? transport_.compensation().midiSampleRate
                                                 : current_.midiSampleRate;
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

void Trainer::onPlayState(bool play, bool pause, bool record) {
  if (play && !pause) {
    // Компенсация держится до конца запуска — как постоянный сдвиг у записи.
    // Позиция начала берётся в момент запуска, до того как её сдвинул звук.
    const trainer::Moment now = momentNow();
    const double position = GetPlayPosition2Ex(nullptr);
    const auto sampleRate = static_cast<double>(deviceInfo("SRATE"));
    const trainer::CountIn countIn =
        projectCountIn(position, record, timeline_, rate_, sampleRate);
    const std::optional<std::int64_t> leadInEnd =
        leadInEndAt(position, record, countIn, timeline_, sampleRate);
    if (transport_.start(now, position, compensationNow(input_.sampleRate()), countIn, rate_,
                         leadInEnd)) {
      startPosition_ = position;
      countInPassed_ = countIn.samples == 0;
      countInTimeline_.emplace(timeline_, position, sampleRate);
      const trainer::Compensation &compensation = transport_.compensation();
      calibrator_.onStart();
      fileNotice_.onTransportStart();
      journal("start: at {}, compensation {:.3f} ms, MIDI compensation {:.3f} ms, rate {}, "
              "position {:.6f}, count-in {} samples, lead-in ends at bar {}",
              trainer::momentText(now), compensation.audio * 1000.0,
              compensation.midi * 1000.0, rate_, position, countIn.samples,
              leadInEnd ? fmt::format("{}", *leadInEnd + 1) : std::string("none"));
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
  // Верхняя строка и текущий удар — по слышимой позиции. Во время отсчёта она
  // раньше позиции начала, и такты — по сетке отсчёта.
  const double heard = GetPlayPositionEx(nullptr);
  if (heard >= startPosition_)
    countInPassed_ = true;
  const grid::Timeline &timeline = countInTimeline_ && !countInPassed_
                                       ? static_cast<const grid::Timeline &>(*countInTimeline_)
                                       : timeline_;
  if (!transport_.play(timeline.beatsAt(heard),
                       trainer::loopBeats(heard, projectLoop(), timeline), settings_.window(),
                       timeline))
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
    if (!transport_.takesOnset(hit->run))
      continue;

    const double played = grid::noteTime(hit->blockPosition, hit->offset, hit->sampleRate,
                                         compensation.audio, rate_);
    // Атака отсчёта и атака до Play лежат перед позицией начала по сетке
    // перед ней, петлёй не заворачиваются.
    const bool before = hit->beforeStart && countInTimeline_;
    const double time = before
                            ? played
                            : trainer::loopWrapped(played, hit->blockPosition, projectLoop(),
                                                   settings_.mode(), timeline_);
    const bool placed = transport_.addNote(
        time, rate_, settings_.window(),
        before ? static_cast<const grid::Timeline &>(*countInTimeline_) : timeline_, before);
    taken = taken || placed;

    // Задержка показа: от блока атаки до этого тика плюс задержка ввода,
    // которую звук прошёл до хука. Окно перерисуется на этом же тике.
    const double ageMs =
        std::chrono::duration<double, std::milli>(momentNow() - hit->monotonic).count() +
        compensation.inputLatency * 1000.0;
    if (placed)
      journal("note t={:.6f} level={:.1f} dB, shown after {:.1f} ms", time, hit->levelDb,
              ageMs);
    else
      journal("note t={:.6f} level={:.1f} dB not placed: no row or node of the run takes it",
              time, hit->levelDb);
  }
  return taken;
}

bool Trainer::takeMidiNotes() {
  // Возраст событий хост отсчитывает от конца последнего блока звука: часы
  // звука хоста берутся на тот же блок. Если за чтение истории хост начал
  // новый блок, история читается заново.
  const trainer::Moment run = transport_.started();
  std::optional<HostClock> clock;
  for (int attempt = 0; attempt < 3; ++attempt) {
    const std::optional<HostClock> before = input_.clock(run);
    const int sequence = midiSequence_;
    readMidiHistory();
    clock = input_.clock(run);
    if (clock == before)
      break;
    midiSequence_ = sequence;
  }

  // Компенсация и время — в секундах шкалы проекта, как всё время нот MIDI в
  // ядре.
  trainer::MidiRead read{.measuring = trainer::measuresMidi(measurement_),
                         .device = midi_ ? midi_->device : -1,
                         .position = GetPlayPosition2Ex(nullptr),
                         .runTime = std::nullopt,
                         .blockLength = 0.0,
                         .hostTime = std::nullopt,
                         .compensation = transport_.compensation().midi * rate_};
  if (clock && clock->sampleRate > 0.0) {
    const double scale = rate_ / clock->sampleRate;
    read.hostTime = static_cast<double>(clock->end) * scale;
    if (clock->elapsed)
      read.runTime = static_cast<double>(*clock->elapsed) * scale;
    read.blockLength = static_cast<double>(clock->blockLength) * scale;
  }
  const std::vector<trainer::MidiNote> notes = transport_.midiNotes().take(midiEvents_, read);
  bool taken = false;
  for (const trainer::MidiNote &note : notes) {
    // Нота отсчёта и нота до Play лежат перед позицией начала по сетке перед
    // ней, петлёй не заворачиваются; остальные — по месту ноты.
    const bool before = note.beforeStart && countInTimeline_;
    const double time = before
                            ? note.time
                            : trainer::loopWrapped(note.time, note.time + read.compensation,
                                                   projectLoop(), settings_.mode(), timeline_);
    const bool placed = transport_.addNote(
        time, rate_, settings_.window(),
        before ? static_cast<const grid::Timeline &>(*countInTimeline_) : timeline_, before);
    taken = taken || placed;
    // Возраст ноты в реальном времени — и задержка показа: окно перерисуется
    // на этом же тике.
    if (placed)
      journal("midi note t={:.6f} pp={:.6f} loop={} {}, shown after {:.1f} ms", time,
              note.event.position, note.event.loop, trainer::noteText(note.event),
              note.event.age / rate_ * 1000.0);
    else
      journal("midi note t={:.6f} pp={:.6f} loop={} {} not placed: no row or node of the run "
              "takes it",
              time, note.event.position, note.event.loop, trainer::noteText(note.event));
  }
  return taken;
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
  // Калибровка ставит порог из допустимых значений: сеттер его принимает.
  Settings settings = settings_;
  settings.setSilenceDb(silenceDb);
  setSettings(settings);
  fileNotice_.onSettingsEdit();
  showSettings();
}

bool Trainer::startCalibration() {
  if (!calibrator_.start(transport_.running(), measurement_, settings_.channel(),
                         settings_.silenceDb(), momentNow()))
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
