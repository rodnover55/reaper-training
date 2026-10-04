#pragma once

// Тренажёр в главном потоке (design.md D2, D3, D6): атаки со входа → время на
// шкале → строки ударов для окна.

#include "audio_input.hpp"
#include "project_timeline.hpp"
#include "settings.hpp"

#include "training/grid/beats.hpp"

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace training::reaper {

/// Что сейчас делает измерение.
enum class Measuring {
  /// Транспорт остановлен или на паузе.
  Waiting,
  /// Воспроизведение или запись: ноты меряются.
  Running,
  /// Выбранного канала нет у звуковой карты.
  NoChannel,
};

/// Сведения для строки состояния окна.
struct Status {
  Measuring measuring = Measuring::Waiting;

  /// Имя выбранного входного канала (`GetInputChannelName`); пусто — канала
  /// нет.
  std::string channelName;

  /// Компенсация задержки, мс реального времени: при воспроизведении — снятая
  /// при запуске, на остановке — та, что будет действовать при запуске.
  double compensationMs = 0.0;

  /// Скорость воспроизведения проекта.
  double rate = 1.0;
};

/// Тренажёр: вход звуковой карты, настройки и строки ударов.
///
/// Многопоточность: только главный поток.
class Trainer {
public:
  /// Читает сохранённые настройки и отдаёт их входу и своду.
  Trainer();

  const Settings &settings() const { return settings_; }

  /// Меняет и сохраняет настройки (`saveSettings`), прижав их к границам.
  /// Режим и допуск действуют на следующие удары, канал и порог — со
  /// следующего блока.
  void setSettings(const Settings &settings);

  /// Строки ударов, новые вперёд.
  const std::deque<grid::BeatRow> &rows() const { return beats_.rows(); }

  const Status &status() const { return status_; }

  /// Атаки последнего запуска транспорта в порядке нахождения — для сверки с
  /// записанным айтемом.
  const std::vector<HookOnset> &lastRun() const { return lastRun_; }

  AudioInput &input() { return input_; }

  /// Забирает новые атаки, ставит их в строки, отмечает пропуски и обновляет
  /// состояние. Зовётся таймером главного потока.
  ///
  /// @return правда, если строки или состояние изменились и окно пора
  ///   перерисовать.
  bool poll();

private:
  void start();
  void takeCompensation();
  double now() const;
  double loopWrapped(double time, double blockPosition) const;

  AudioInput input_;
  ProjectTimeline timeline_;
  Settings settings_;
  grid::Beats beats_;
  Status status_;
  std::vector<HookOnset> lastRun_;

  bool wasPlaying_ = false;
  double sampleRate_ = 0.0;
  double compensation_ = 0.0;
  double inputLatency_ = 0.0;
  double rate_ = 1.0;
  std::uint32_t minRun_ = 0;
  std::uint32_t latestRun_ = 0;
};

/// Тренажёр расширения; есть между `initTrainer` и `shutdownTrainer`.
Trainer &trainer();

/// Создаёт тренажёр и регистрирует таймер главного потока. Зовётся при
/// загрузке расширения.
void initTrainer(reaper_plugin_info_t *rec);

/// Снимает таймер и уничтожает тренажёр. Зовётся при выгрузке расширения.
void shutdownTrainer();

} // namespace training::reaper
