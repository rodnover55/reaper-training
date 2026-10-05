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

#include "trainer.hpp"

#include <reaper_plugin_functions.h>

#include "journal.hpp"
#include "window.hpp"

#include "training/grid/note_time.hpp"

#include <chrono>
#include <cstring>
#include <memory>

namespace training::reaper {
namespace {

// Биты состояния транспорта (`GetPlayStateEx`).
constexpr int kPlaying = 1;
constexpr int kPaused = 2;
constexpr int kRecording = 4;

std::unique_ptr<Trainer> instance;

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

  rate_ = Master_GetPlayRate(nullptr);

  Status status;
  status.rate = rate_;
  status.compensationMs = compensation_ * 1000.0;
  if (settings_.channel < channels)
    if (const char *name = GetInputChannelName(settings_.channel))
      status.channelName = name;
  if (status.channelName.empty())
    status.measuring = Measuring::NoChannel;
  else
    status.measuring = playing ? Measuring::Running : Measuring::Waiting;

  bool changed =
      status.measuring != status_.measuring || status.channelName != status_.channelName ||
      status.compensationMs != status_.compensationMs || status.rate != status_.rate;
  status_ = status;

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

Trainer &trainer() { return *instance; }

void initTrainer(reaper_plugin_info_t *rec) {
  instance = std::make_unique<Trainer>();

  if (!rec || !rec->Register)
    return;

  hostRegister = rec->Register;
  hostRegister("timer", reinterpret_cast<void *>(onMainTimer));
}

void shutdownTrainer() {
  if (hostRegister)
    hostRegister("-timer", reinterpret_cast<void *>(onMainTimer));

  instance.reset();
}

} // namespace training::reaper
