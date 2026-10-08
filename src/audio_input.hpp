#pragma once

// Звук гитары со входа звуковой карты: аудиохук копирует блоки выбранного
// канала, рабочий поток ищет в них атаки и отдаёт главному.

#include "training/concurrency/spsc_ring.hpp"
#include "training/trainer/onset_search.hpp"

// SWELL объявляет max и min макросами, и они ломают стандартную библиотеку.
#define WDL_NO_DEFINE_MINMAX

#include <reaper_plugin.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

namespace training::reaper {

/// Кусок звука для калибровки входа: подряд идущие сэмплы выбранного канала,
/// снятые при остановленном транспорте.
struct CalibrationChunk {
  /// Частота дискретизации звуковой карты, Гц.
  double sampleRate = 0.0;

  /// Сколько сэмплов в куске; больше нуля.
  int length = 0;

  /// Правда, если кусок не продолжает прошлый: между ними звук пропал, шло
  /// воспроизведение или калибровка выключалась. У первого куска — правда.
  bool gap = false;
};

/// Вход звуковой карты: аудиохук и рабочий поток поиска атак.
///
/// Существует от создания до уничтожения; хук зарегистрирован всё это время.
/// Звук блока идёт в поиск атак, пока транспорт воспроизводит или записывает и
/// не стоит на паузе, и в калибровку, пока транспорт стоит и включён звук
/// калибровки. Поиск атак ядра (`trainer::OnsetSearch`) идёт от `startRun` до
/// `stopRun` и сам решает, какие атаки отдавать.
///
/// Многопоточность: все методы, кроме деструктора, можно звать из любого
/// потока, но `popOnset` — только из одного и `popCalibration` — только из
/// одного.
class AudioInput {
public:
  /// Регистрирует аудиохук и запускает рабочий поток. Выбранный канал — 0.
  AudioInput();

  /// Снимает аудиохук и останавливает рабочий поток. Атаки, не забранные
  /// `popOnset`, пропадают.
  ~AudioInput();

  AudioInput(const AudioInput &) = delete;
  AudioInput &operator=(const AudioInput &) = delete;
  AudioInput(AudioInput &&) = delete;
  AudioInput &operator=(AudioInput &&) = delete;

  /// Ложь, если REAPER не принял аудиохук: тогда атак не будет никогда.
  bool registered() const { return registered_; }

  /// Выбирает входной канал звуковой карты, 0 — первый. Действует со
  /// следующего блока.
  void setChannel(int channel) { channel_.store(channel, std::memory_order_relaxed); }

  /// Выбранный входной канал, 0 — первый.
  int channel() const { return channel_.load(std::memory_order_relaxed); }

  /// Есть ли у звуковой карты входной канал `channel` — по последнему блоку,
  /// в котором хук его проверял. Хук проверяет выбранный канал (`setChannel`)
  /// на каждом блоке, в том числе при остановленном транспорте.
  ///
  /// @return пусто — канал `channel` хук ещё не проверял: после выбора
  ///   канала блоков не было или выбран другой канал.
  std::optional<bool> channelPresent(int channel) const {
    const int checked = checkedChannel_.load(std::memory_order_relaxed);
    if (checked < 0 || checked / 2 != channel)
      return std::nullopt;
    return checked % 2 == 1;
  }

  /// Меняет порог тишины поиска атак, dBFS (`onset::Settings::silenceDb`).
  /// Действует со следующего блока; поиск атак не начинается заново.
  void setSilenceDb(double silenceDb) {
    silenceDb_.store(silenceDb, std::memory_order_relaxed);
  }

  /// Включает и выключает звук для калибровки входа. Пока он включён и
  /// транспорт стоит, сэмплы выбранного канала идут в очередь калибровки
  /// (`popCalibration`); при воспроизведении и записи — как всегда, в поиск
  /// атак. Действует со следующего блока.
  void setCalibrating(bool calibrating) {
    calibrating_.store(calibrating, std::memory_order_relaxed);
  }

  /// Забирает следующий кусок звука калибровки. Если очередь калибровки
  /// переполнена, куски пропадают, и следующий за ними помечен `gap`.
  ///
  /// @param samples сюда пишутся сэмплы куска; прежнее содержимое
  ///   заменяется. Если кусков нет, не меняется.
  /// @return кусок; пусто — новых нет.
  std::optional<CalibrationChunk> popCalibration(std::vector<float> &samples);

  /// Частота дискретизации по последнему блоку, Гц; 0 — блоков ещё не было.
  double sampleRate() const { return sampleRate_.load(std::memory_order_relaxed); }

  /// Забирает следующую найденную атаку; пусто — новых нет.
  std::optional<trainer::AudioOnset> popOnset() { return onsets_.pop(); }

  /// Отмечает начало запуска транспорта (`trainer::Transport::start`): рабочий
  /// поток начнёт поиск атак (`trainer::OnsetSearch::start`) перед следующим
  /// блоком. Зовётся из главного потока.
  void startRun() {
    running_.store(true);
    runs_.fetch_add(1);
  }

  /// Отмечает конец запуска транспорта (`trainer::Transport::stop`): рабочий
  /// поток кончит поиск атак (`trainer::OnsetSearch::stop`) перед следующим
  /// блоком. Зовётся из главного потока.
  void stopRun() { running_.store(false); }

  /// Сколько блоков пропало между хуком и рабочим потоком: рабочий поток не
  /// успевал. Пропавший звук поиск атак отсчитывает как время без звука.
  std::size_t droppedBlocks() const { return headers_.dropped(); }

private:
  /// Заголовок блока аудиохука.
  struct BlockHeader {
    // Номер первого сэмпла блока от начала работы хука.
    std::uint64_t firstSample = 0;
    double position = 0.0;
    double sampleRate = 0.0;
    double monotonic = 0.0;
    int length = 0;
    bool hasSamples = false;
    bool calibration = false;
  };

  static constexpr std::size_t kMaxBlock = 16384;

  static void onAudioBuffer(bool isPost, int length, double sampleRate,
                            audio_hook_register_t *registration);
  void onBlock(int length, double sampleRate, audio_hook_register_t *registration);

  void work();
  void followRun();
  void feed(const BlockHeader &header);
  void forwardCalibration(const BlockHeader &header, bool gap);

  // Поля стоят в порядке, который не оставляет дыр выравнивания; кто их
  // пишет — в комментарии у каждого.

  // Звуковой поток пишет, рабочий читает. Сэмплы блока лежат в очереди
  // сэмплов раньше его заголовка.
  concurrency::SpscRing<BlockHeader, 1024> headers_;

  // Рабочий поток пишет, главный читает.
  concurrency::SpscRing<trainer::AudioOnset, 1024> onsets_;

  // Рабочий поток пишет, главный читает. Сэмплы куска лежат в очереди
  // сэмплов раньше его заголовка.
  concurrency::SpscRing<CalibrationChunk, 1024> calibrationChunks_;

  // Звуковой поток пишет, рабочий читает.
  std::unique_ptr<concurrency::SpscRing<float, (1U << 18)>> samples_;

  // Рабочий поток пишет, главный читает.
  std::unique_ptr<concurrency::SpscRing<float, (1U << 18)>> calibrationSamples_;

  // Звуковой поток пишет, читают все.
  std::atomic<double> sampleRate_{0.0};

  // Главный поток пишет, рабочий читает.
  std::atomic<double> silenceDb_{-50.0};

  // Только звуковой поток.
  std::uint64_t nextSample_ = 0;

  // Только рабочий поток.
  std::uint64_t expectedSample_ = 0;

  std::thread worker_;

  // Только рабочий поток.
  trainer::OnsetSearch search_;
  std::vector<float> blockSamples_;
  std::vector<trainer::AudioOnset> found_;

  // Заполняет конструктор до регистрации хука, дальше его читает REAPER.
  audio_hook_register_t hook_{};

  // Главный поток пишет, звуковой читает.
  std::atomic<int> channel_{0};

  // Звуковой поток пишет, читают все: последний проверенный канал, умноженный
  // на 2, плюс 1, если у карты он есть; −1 — не проверялся. Канал и ответ — в
  // одном числе, чтобы читатель не взял ответ для другого канала.
  std::atomic<int> checkedChannel_{-1};

  // Главный поток пишет, рабочий читает: сколько было запусков.
  std::atomic<std::uint32_t> runs_{0};

  // Только рабочий поток: сколько запусков он уже передал поиску атак.
  std::uint32_t seenRuns_ = 0;

  // Только звуковой поток: блок, переведённый в float перед отправкой.
  std::array<float, kMaxBlock> scratch_{};

  // Только рабочий поток: идёт ли поток кусков калибровки, пропадали ли куски
  // калибровки.
  bool calibrationStreaming_ = false;
  bool calibrationLost_ = false;

  // Главный поток пишет, звуковой читает.
  std::atomic<bool> calibrating_{false};

  // Главный поток пишет, рабочий читает: идёт ли запуск.
  std::atomic<bool> running_{false};

  bool registered_ = false;
  std::atomic<bool> stop_{false};
};

} // namespace training::reaper
