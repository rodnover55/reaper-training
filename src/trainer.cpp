#define REAPERAPI_MINIMAL
#define REAPERAPI_WANT_GetInputChannelName
#define REAPERAPI_WANT_GetInputOutputLatency
#define REAPERAPI_WANT_GetPlayPositionEx
#define REAPERAPI_WANT_GetPlayPosition2Ex
#define REAPERAPI_WANT_GetAudioDeviceInfo
#define REAPERAPI_WANT_MIDI_GetRecentInputEvent
#define REAPERAPI_WANT_Master_GetPlayRate
#define REAPERAPI_WANT_get_config_var
#define REAPERAPI_WANT_Audio_IsRunning
#define REAPERAPI_WANT_Audio_Init
#define REAPERAPI_WANT_GetCursorPosition
#define REAPERAPI_WANT_projectconfig_var_addr
#define REAPERAPI_WANT_projectconfig_var_getoffs
#define REAPERAPI_WANT_GetSet_LoopTimeRange2
#define REAPERAPI_WANT_GetSetRepeat

#include "trainer.hpp"

#include <reaper_plugin_functions.h>

#include "clock.hpp"
#include "journal.hpp"
#include "window.hpp"

#include "training/core/clock.hpp"
#include "training/core/input/midi_receiver.hpp"
#include "training/core/training/state/bar.hpp"
#include "training/core/training/state/note.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
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

/// Таймер главного потока REAPER: опрашивает тренажёр (`Trainer::poll`) и
/// перерисовывает окно, если строки или состояние изменились. Без тренажёра
/// ничего не делает.
void onMainTimer() {
  if (instance && instance->poll())
    refreshWindow();
}

/// Компенсация задержки входов сейчас — по правилу, по которому REAPER
/// сдвигает запись звука (`Compensation::audioOf`) и MIDI
/// (`Compensation::midiOf`).
///
/// @param sampleRate частота звуковой карты по последнему блоку звука, Гц;
///   0 — блоков не было.
Compensation compensationNow(double sampleRate) {
  int inputLatency = 0;
  int outputLatency = 0;
  GetInputOutputLatency(&inputLatency, &outputLatency);
  Compensation compensation{.audio = {},
                            .audioSampleRate = sampleRate,
                            .inputLatency = {},
                            .midi = {},
                            .midiSampleRate = static_cast<double>(deviceInfo("SRATE"))};
  if (sampleRate > 0.0) {
    compensation.inputLatency =
        static_cast<double>(inputLatency) / sampleRate * core::Interval::kSecond;
    compensation.audio = Compensation::audioOf(
        inputLatency, outputLatency, intConfig("adjrecmanlatin", 0),
        intConfig("adjrecmanlat", 0), intConfig("adjreclat", 1) != 0, sampleRate);
  }
  if (compensation.midiSampleRate > 0.0)
    compensation.midi =
        Compensation::midiOf(deviceInfo("BSIZE"), outputLatency, intConfig("adjrecmanlat", 0),
                             intConfig("adjreclat", 1) != 0, compensation.midiSampleRate);
  return compensation;
}

/// Отсчёт перед запуском с позиции `position` по настройкам проекта: метроном
/// и отсчёт — `projmetroen`, тактов — `projmetrocountin`, с начала такта —
/// бит 8 `preroll`. Длина такта — по темпу и размеру в позиции начала.
///
/// @param sampleRate частота звуковой карты, Гц.
CountIn projectCountIn(core::Moment position, bool record, const TempoMap &timeline,
                       double rate, double sampleRate) {
  const int metronome = projectConfig<int>("projmetroen").value_or(0);
  const int preroll = projectConfig<int>("preroll").value_or(intConfig("preroll", 0));
  const CountInSettings settings{.metronome = (metronome & 1) != 0,
                                 .beforePlayback = (metronome & 8) != 0,
                                 .beforeRecording = (metronome & 16) != 0,
                                 .bars =
                                     projectConfig<double>("projmetrocountin").value_or(0.0),
                                 .fromBarStart = (preroll & 8) != 0};

  return countInAt(settings, record, timeline, position, rate, sampleRate,
                   deviceInfo("BSIZE"));
}

/// Такт, которым кончается затакт запуска с позиции `position`: пре-ролл
/// кончается на курсоре редактирования, если для этого запуска он включён
/// (бит 1 `preroll` — перед воспроизведением, бит 2 — перед записью); без
/// пре-ролла отсчёт кончается тактом позиции начала. Такт места —
/// `barAt`. Пусто — затакта нет.
///
/// @param sampleRate частота звуковой карты, Гц.
std::optional<std::int64_t> leadInEndAt(core::Moment position, bool record,
                                        const CountIn &countIn, const TempoMap &timeline,
                                        double sampleRate) {
  const int preroll = projectConfig<int>("preroll").value_or(intConfig("preroll", 0));
  const bool prerollOn = (preroll & (record ? 2 : 1)) != 0;
  const core::Moment cursor = projectTime(GetCursorPosition());
  if (prerollOn && cursor > position)
    return barAt(timeline, cursor, sampleRate).index;
  if (countIn.samples > 0)
    return barAt(timeline, position, sampleRate).index;
  return std::nullopt;
}

/// Петля проекта, если включён повтор: выделение петли REAPER. Пусто —
/// повтора нет или петля пустая.
std::optional<ProjectLoop> projectLoop() {
  if (GetSetRepeat(-1) != 1)
    return std::nullopt;
  double start = 0.0;
  double end = 0.0;
  GetSet_LoopTimeRange2(nullptr, false, true, &start, &end, false);
  if (end <= start)
    return std::nullopt;
  return ProjectLoop{.start = projectTime(start), .end = projectTime(end)};
}

} // namespace

Trainer::Trainer() : Trainer(loadSettings()) {}

Trainer::Trainer(const SettingsRead &read)
    : settings_(read.settings), inputSettings_(read.input), gridMode_(read.mode),
      view_(read.view), rate_(Master_GetPlayRate(nullptr)), input_(inputSettings_),
      runTimeline_(timeline_), fileNotice_(kSettingsFile, read.rejected),
      calibration_(kSilenceRange, &calibrationJournal_) {
  for (const std::string &line : read.rejected)
    journal("{}: {}", kSettingsFile, line);
  refreshInputs();
  skipMidiHistory();
}

void Trainer::setSettings(const core::Settings &settings, const InputSettings &input) {
  apply(settings, input);
  save();
}

void Trainer::setSettings(const core::Settings &settings) {
  setSettings(settings, inputSettings_);
}

void Trainer::previewSettings(const core::Settings &settings) {
  apply(settings, inputSettings_);
}

void Trainer::setView(const render::ViewSettings &view) {
  view_ = view;
  save();
}

void Trainer::save() const { saveSettings(settings_, inputSettings_, gridMode_, view_); }

void Trainer::setGridMode(Mode mode) {
  gridMode_ = mode;
  save();
}

void Trainer::apply(const core::Settings &settings, const InputSettings &input) {
  const bool changed = input != inputSettings_;
  const bool kindChanged = input.input() != inputSettings_.input();
  if (changed) {
    calibration_.onInputChange();
    input_.toTraining();
    midiPlaces_.forget();
  }

  // Память строки выделяется до замка: под ним выбранный вход только меняется
  // местами.
  InputSettings next = input;
  settings_ = settings;
  if (transport_)
    transport_->setSettings(settings_);
  {
    const std::unique_lock lock = input_.lockSettings();
    std::swap(inputSettings_, next);
  }

  if (changed) {
    // Ноты, пришедшие до смены, в новый вход не попадают.
    skipMidiHistory();
    // Наличие канала — сразу, а не на следующем тике: калибровка, начатая до
    // него, проверяет новый вход.
    refreshInputs();
    journal("input: {}", inputText(inputSettings_));
  }
  // Тренировка — одного вида входа: на другом виде запуск начинает её заново.
  if (kindChanged && playing_)
    startTraining();
}

void Trainer::refreshInputs() {
  lookupMidiInput();
  const char *name = GetInputChannelName(inputSettings_.channel());
  inputs_.channelName = name ? name : "";
  inputs_.midiName = midi_ ? std::optional(midi_->name) : std::nullopt;
  inputs_.midiEnabled = midi_ && midiInputEnabled(midi_->device);
  // Строка состояния — по свежим сведениям сразу, а не на следующем тике:
  // окно показывает её вместе со списком входов.
  updateStatus();
}

void Trainer::lookupMidiInput() {
  const bool midi = inputSettings_.input() == Input::Midi;
  std::optional<MidiInput> found;
  if (midi)
    found =
        findMidiInput(midiInputs(), inputSettings_.midiInput(), inputSettings_.midiIndex());

  // По номеру в настройках смена номеров устройств узнаёт выбранный вход
  // (onMidiDeviceRemap). Номер устройства читает только главный поток: замок
  // для него лишний.
  if (found && found->device != inputSettings_.midiIndex()) {
    inputSettings_.setMidiIndex(found->device);
    save();
  }

  const bool same = found.has_value() == midi_.has_value() &&
                    (!found || (found->device == midi_->device && found->name == midi_->name));
  midi_ = std::move(found);
  if (same || !midi)
    return;

  if (midi_)
    journal("midi input «{}»: device {} «{}»", inputSettings_.midiInput(), midi_->device,
            midi_->name);
  else
    journal("midi input «{}»: not found", inputSettings_.midiInput());
}

void Trainer::onMidiDeviceRemap(bool output, int from, int to) {
  if (output)
    return;

  if (midi_)
    midi_->device = remappedDevice(midi_->device, output, from, to);
  const int index = remappedDevice(inputSettings_.midiIndex(), output, from, to);
  if (index != inputSettings_.midiIndex()) {
    journal("midi input «{}»: device {} -> {}", inputSettings_.midiInput(), from, to);
    inputSettings_.setMidiIndex(index);
    save();
  }

  // Со сменой номеров устройство могло появиться или пропасть.
  refreshInputs();
}

void Trainer::updateStatus() {
  inputs_.channelPresent = input_.channelPresent(inputSettings_.channel());
  const Compensation &compensation = running() ? started_ : current_;
  status_ = render::StatusLine{.host = "REAPER",
                               .input = inputs_.status(inputSettings_.input()),
                               .compensation = compensation.of(inputSettings_.input()),
                               .rate = rate_};
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
  const double sampleRate = running() ? started_.midiSampleRate : current_.midiSampleRate;
  std::array<char, 256> bytes{};
  int newest = 0;
  for (int index = 0;; ++index) {
    MidiEvent event;
    int size = static_cast<int>(bytes.size());
    int timestamp = 0;
    double position = 0.0;
    const int sequence = MIDI_GetRecentInputEvent(index, bytes.data(), &size, &timestamp,
                                                  &event.device, &position, &event.loop);
    if (sequence == 0 || sequence <= midiSequence_)
      break;
    if (index == 0)
      newest = sequence;

    // Позиция меньше нуля — транспорт стоял.
    if (position >= 0.0)
      event.position = projectTime(position);

    // Сэмплы от прихода события до чтения — в реальное время: время ядра —
    // часы потока звука.
    if (sampleRate > 0.0)
      event.age = -static_cast<double>(timestamp) / sampleRate * core::Interval::kSecond;

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
    // T0 — время ядра, в которое после отсчёта зазвучит позиция начала: точка
    // отсчёта времени ядра.
    const core::Moment position = projectTime(GetPlayPosition2Ex(nullptr));
    const auto sampleRate = static_cast<double>(deviceInfo("SRATE"));
    const CountIn countIn = projectCountIn(position, record, timeline_, rate_, sampleRate);
    const std::optional<std::int64_t> leadInEnd =
        leadInEndAt(position, record, countIn, timeline_, sampleRate);
    const Compensation compensation = compensationNow(input_.sampleRate());
    const RunStart start{.start = {.position = position, .rate = rate_},
                         .time = core::Moment()};
    if (!playing_) {
      playing_ = true;
      // Калибровка подводит себя по своему состоянию раньше, чем запуск
      // тренировки остановит калибратор.
      calibration_.onStart();
      enteredBar_.reset();
      heard_.reset();
      started_ = compensation;
      input_.setCompensation(compensation.audio);
      runTimeline_.start(start, countIn, leadInEnd, sampleRate);
      // Номер запуска — до тренировки: тренировка звука получает только
      // блоки этого запуска.
      audioRun_ = input_.onStart(start, countIn.samples);
      midiPlaces_.start();
      fileNotice_.onTransportStart();
      startTraining();
      // Первые такты — сразу, а не на тике: ноты звука ставит рабочий поток,
      // и нота у самого начала запуска иначе не нашла бы такта.
      if (running())
        sendFirstBars();
      journal("start: compensation {:.3f} ms, MIDI compensation {:.3f} ms, rate {}, "
              "position {:.6f}, T0 {}, count-in {} samples, lead-in ends at bar {}",
              static_cast<double>(compensation.audio / core::Interval::kMillisecond),
              static_cast<double>(compensation.midi / core::Interval::kMillisecond), rate_,
              projectSeconds(position), momentText(start.time), countIn.samples,
              leadInEnd ? fmt::format("{}", *leadInEnd + 1) : std::string("none"));
      refreshWindow();
    }
  } else if (playing_) {
    playing_ = false;
    trainingPending_ = false;
    if (transport_)
      transport_->stop();
    midiPlaces_.stop();
    calibration_.onStop();
    refreshWindow();
  }
}

void Trainer::onPlayRate(double rate) { rate_ = rate; }

bool Trainer::poll() {
  syncTransport();
  // Транспорт и скорость сообщает REAPER (onPlayState, onPlayRate). Строка
  // состояния без запуска показывает компенсацию, которая будет действовать
  // при запуске; её же связка вычитает из времени блоков звука.
  current_ = compensationNow(input_.sampleRate());
  if (!running())
    input_.setCompensation(current_.audio);
  const render::StatusLine before = status_;
  updateStatus();
  bool changed = status_ != before;

  if (pollCalibration())
    changed = true;

  // Места блоков — до тактов: по ним карта темпа строит такты и переводит
  // слышимую позицию во время ядра.
  takePlaces();
  if (running()) {
    sendBars();
    changed = true;
  }
  if (takeMidiNotes())
    changed = true;
  journalPlaced();
  return changed;
}

void Trainer::takePlaces() {
  // Состояние тренировки копирует её такты: оно берётся один раз на вызов.
  const bool run = running();
  while (const std::optional<BlockPlace> place = input_.popPlace()) {
    // Блок, взятый до того, как хук увидел начало запуска, — со временем
    // прошлой связки: на карту запуска он не ложится.
    if (!run || place->run != audioRun_)
      continue;
    const std::optional<std::int64_t> again =
        runTimeline_.addBlock(place->time, {.position = place->position, .rate = rate_});
    if (!again)
      continue;
    journal("segment: block {} at position {:.6f}, rate {}, bars again from {}",
            momentText(place->time), projectSeconds(place->position), rate_, *again);
    if (nextBar_)
      nextBar_ = std::min(*nextBar_, *again);
  }
}

std::int64_t Trainer::sendFirstBars() {
  // Предтакт — ноты чуть раньше первого такта запуска сравниваются с его
  // долями — и первый такт запуска.
  (void)runTimeline_.setLoop(projectLoop());
  const std::int64_t first = runTimeline_.firstBar();
  runTimeline_.predict(runTimeline_.timeAt({.bar = first + 1, .beat = 0.0}));
  training_->addBar(runTimeline_.barAt(first - 1, gridMode_));
  training_->addBar(runTimeline_.barAt(first, gridMode_));
  nextBar_ = first + 1;
  return first + 1;
}

void Trainer::sendBars() {
  // Строки тактов и текущий удар render берёт по слышимой позиции во времени
  // ядра.
  const core::Moment position = projectTime(GetPlayPositionEx(nullptr));
  const std::optional<HostClock> clock = input_.clock();
  const core::Moment taken = clock ? clock->end : core::Moment();
  const core::Moment heard = runTimeline_.heard({.position = position, .rate = rate_}, taken);
  heard_ = heard;

  const Mode mode = gridMode_;
  std::int64_t next = nextBar_ ? *nextBar_ : sendFirstBars();
  // Смена петли отменяет такты, предсказанные по прежней.
  if (const std::optional<std::int64_t> again = runTimeline_.setLoop(projectLoop()))
    next = std::min(next, *again);
  // Такт уходит, как только начался взятый звук такта перед ним: ноты у
  // его начала находят его доли. Переход петли до конца такта известен
  // заранее: такт после конца петли — такт её начала.
  while (runTimeline_.timeAt({.bar = next - 1, .beat = 0.0}) <= taken) {
    runTimeline_.predict(runTimeline_.timeAt({.bar = next + 1, .beat = 0.0}));
    training_->addBar(runTimeline_.barAt(next, mode));
    ++next;
  }
  nextBar_ = next;

  const std::int64_t bar = runTimeline_.positionAt(heard).bar;
  if (bar != enteredBar_) {
    enteredBar_ = bar;
    journal("bar {} entered at {:.3f}", runTimeline_.barAt(bar, mode).name(),
            projectSeconds(position));
  }
}

bool Trainer::takeMidiNotes() {
  // Возраст событий хост отсчитывает от конца последнего блока звука: часы
  // звука хоста берутся на тот же блок. Если за чтение истории хост начал
  // новый блок, история читается заново.
  std::optional<HostClock> clock;
  for (int attempt = 0; attempt < 3; ++attempt) {
    const std::optional<HostClock> before = input_.clock();
    const int sequence = midiSequence_;
    readMidiHistory();
    clock = input_.clock();
    if (clock == before)
      break;
    midiSequence_ = sequence;
  }

  // Ноты даёт только найденное устройство, включённое у REAPER для записи на
  // дорожки.
  const MidiRead read{.input = inputSettings_.input(),
                      .device = midi_ && inputs_.midiEnabled ? midi_->device : -1};
  std::vector<core::input::MidiReceiver::Note> notes =
      midiPlaces_.place(midiEvents_, read, clock ? std::optional(clock->end) : std::nullopt);
  if (notes.empty())
    return false;

  // Компенсация нот MIDI — сдвиг записи в реальном времени.
  for (core::input::MidiReceiver::Note &note : notes) {
    note.time -= started_.midi;
    journal("midi note t={}", momentText(note.time));
  }
  if (midiTraining_)
    midiTraining_->receive(notes);
  return true;
}

void Trainer::journalPlaced() {
  if (!training_)
    return;
  const std::vector<core::training::state::Bar> bars = training_->result().timeline.bars();
  journaled_.resize(bars.size());
  for (std::size_t index = 0; index < bars.size(); ++index) {
    for (const auto &[beat, notes] : bars[index].notes()) {
      for (std::size_t &done = journaled_[index][beat]; done < notes.size(); ++done)
        journal("note placed, deviation {:+.3f} ms",
                static_cast<double>(notes[done].deviation / core::Interval::kMillisecond));
    }
  }
}

bool Trainer::pollCalibration() {
  bool changed = false;
  while (const auto chunk = input_.popCalibration(chunkSamples_)) {
    lastCalibrationSound_ = momentNow();
    const std::optional<core::SampleRate> rate = core::SampleRate::create(chunk->sampleRate);
    if (!rate)
      continue;
    const bool finished = calibration_.finished();
    calibration_.setSampleRate(*rate);
    calibration_.receive({.samples = chunkSamples_, .start = chunk->start});
    changed = true;
    // Порог удачного итога ставится один раз — на куске, с которым итог
    // появился.
    if (!finished && calibration_.finished())
      if (const std::optional<double> silenceDb = calibration_.silenceDb())
        setSilence(*silenceDb);
  }
  // Удар по струнам после итога кончает сессию: звук больше не ждёт.
  if (!calibration_.listening())
    input_.toTraining();

  // Калибровка не слышит звуковую карту: сессия остаётся, звук в неё больше
  // не идёт.
  if (calibration_.listening() && !calibration_.finished() && !noSound_ &&
      lastCalibrationSound_ && momentNow() - *lastCalibrationSound_ >= kNoSoundTime) {
    noSound_ = true;
    input_.toTraining();
    calibrationJournal_.noSound();
    changed = true;
  }
  return changed;
}

void Trainer::setSilence(double silenceDb) {
  // Калибровка ставит порог из допустимых значений: сеттер его принимает.
  core::Settings settings = settings_;
  settings.setSilenceDb(silenceDb);
  setSettings(settings);
  fileNotice_.onSettingsEdit();
  showSettings();
}

bool Trainer::startCalibration() {
  // Калибруется только канал звуковой карты, который у неё есть, при стоящем
  // транспорте.
  if (playing_ || inputSettings_.input() != Input::Audio || !inputs_.present(Input::Audio))
    return false;
  // Прежняя сессия кончается раньше, чем начинается новая.
  calibration_.start();
  calibrationFromDb_ = settings_.silenceDb();
  calibrationJournal_.started(inputSettings_.channel(), inputs_.channelName,
                              calibrationFromDb_);
  lastCalibrationSound_ = momentNow();
  noSound_ = false;

  // Куски прошлой сессии, которые ещё в очереди, — не этой калибровки.
  while (input_.popCalibration(chunkSamples_)) {
  }
  // Звук идёт калибровке: тренировка звука блоков больше не получает.
  input_.setReceiver(nullptr, 0);
  input_.toCalibration();

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
  calibration_.end();
  input_.toTraining();
}

void Trainer::applyCalibration() {
  const std::optional<double> silenceDb = calibration_.applyAnyway();
  if (!calibration_.listening())
    input_.toTraining();
  if (silenceDb)
    setSilence(*silenceDb);
}

core::training::state::Timeline Trainer::timeline() const {
  if (!training_)
    return {};
  return training_->result().timeline;
}

core::state::WorkMode Trainer::mode() const {
  if (running())
    return core::state::WorkMode::Run;
  if (calibration_.listening() && !noSound_)
    return core::state::WorkMode::Calibration;
  return core::state::WorkMode::Idle;
}

bool Trainer::running() const {
  return training_ && training_->result().step == core::training::Trainer::Step::Working;
}

void Trainer::startTraining() {
  input_.toTraining();
  // Прежняя тренировка звука блоков больше не получает.
  input_.setReceiver(nullptr, 0);
  training_.reset();
  audioTraining_.reset();
  midiTraining_.reset();
  journaled_.clear();
  // Такты запуска новая тренировка получает заново (`sendBars`).
  nextBar_.reset();
  trainingPending_ = !transport_;
  if (!transport_)
    return;

  if (inputSettings_.input() == Input::Audio) {
    auto started =
        transport_->start(core::Transport::Training<core::training::Trainer::Type::Audio>{});
    if (!started) {
      journal("start: no audio training at sample rate {} Hz", transport_->sampleRate().hz());
      return;
    }
    audioTraining_ = std::move(*started);
    training_ = audioTraining_;
    input_.setReceiver(audioTraining_.get(), audioRun_);
    return;
  }
  auto started =
      transport_->start(core::Transport::Training<core::training::Trainer::Type::Midi>{});
  if (!started)
    return;
  midiTraining_ = std::move(*started);
  training_ = midiTraining_;
}

void Trainer::syncTransport() {
  const std::optional<core::SampleRate> rate = core::SampleRate::create(input_.sampleRate());
  if (!rate || (transport_ && transport_->sampleRate() == *rate))
    return;
  // `core::Transport.sampleRate`: на другой частоте тренировка
  // останавливается и запускается заново на новом транспорте.
  // Калибратор прежнего транспорта остаётся у калибровки: показанный итог
  // держится до блока другой частоты в калибровке.
  const bool restart = running() || trainingPending_;
  if (restart && transport_)
    transport_->stop();
  transport_ = std::make_unique<core::Transport>(settings_, *rate);
  calibration_.setTransport(transport_.get());
  journal("transport: sample rate {} Hz", rate->hz());
  if (restart && playing_)
    startTraining();
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
