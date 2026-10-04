#pragma once

// Звук гитары со входа звуковой карты (design.md D1, D3): аудиохук копирует
// блоки выбранного канала, рабочий поток ищет в них атаки и отдаёт главному.

#include "training/concurrency/spsc_ring.hpp"
#include "training/onset/detector.hpp"

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

/// Атака, найденная во входном звуке, вместе с блоком, в котором она началась.
struct HookOnset {
  /// Позиция обработки блока на шкале проекта, с (`GetPlayPosition2Ex`).
  double blockPosition = 0.0;

  /// Начало атаки — дробный номер сэмпла от начала блока; не меньше 0 и
  /// меньше длины блока.
  double offset = 0.0;

  /// Частота дискретизации звуковой карты, Гц.
  double sampleRate = 0.0;

  /// Уровень атаки, dBFS (`onset::Onset::levelDb`).
  double levelDb = 0.0;

  /// Момент, когда хук получил блок, — секунды монотонных часов
  /// (`std::chrono::steady_clock`). Только для замера задержки показа.
  double monotonic = 0.0;

  /// Номер запуска транспорта: растёт на 1 с каждым запуском воспроизведения
  /// или записи и с каждым продолжением после паузы. Атаки одного запуска
  /// идут подряд.
  std::uint32_t run = 0;
};

/// Вход звуковой карты: аудиохук и рабочий поток поиска атак.
///
/// Существует от создания до уничтожения; хук зарегистрирован всё это время.
/// Сэмплы берутся только при воспроизведении и записи. Атаки из блоков, где
/// позиция транспорта не сдвинулась с прошлого блока (запуск, count-in), не
/// отдаются: на шкале им места нет (findings.md, R4).
///
/// Многопоточность: все методы, кроме деструктора, можно звать из любого
/// потока, но `popOnset` — только из одного.
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

  /// Сообщает хуку число входных каналов звуковой карты (`GetNumAudioInputs`):
  /// сам хук его не узнаёт (findings.md, R1). Каналы с номером не меньше этого
  /// числа сэмплов не дают. Действует со следующего блока.
  void setInputChannels(int channels) {
    inputChannels_.store(channels, std::memory_order_relaxed);
  }

  /// Число входных каналов, сообщённое `setInputChannels`; 0 — ещё не
  /// сообщалось.
  int inputChannels() const { return inputChannels_.load(std::memory_order_relaxed); }

  /// Меняет порог тишины поиска атак, dBFS (`onset::Settings::silenceDb`).
  /// Действует со следующего блока; поиск атак не начинается заново.
  void setSilenceDb(double silenceDb) {
    silenceDb_.store(silenceDb, std::memory_order_relaxed);
  }

  /// Порог тишины поиска атак, dBFS; по умолчанию −50.
  double silenceDb() const { return silenceDb_.load(std::memory_order_relaxed); }

  /// Частота дискретизации по последнему блоку, Гц; 0 — блоков ещё не было.
  double sampleRate() const { return sampleRate_.load(std::memory_order_relaxed); }

  /// Забирает следующую найденную атаку; пусто — новых нет.
  std::optional<HookOnset> popOnset() { return onsets_.pop(); }

  /// Сколько блоков пропало между хуком и рабочим потоком: рабочий поток не
  /// успевал. На пропуске поиск атак начинается заново.
  std::size_t droppedBlocks() const { return headers_.dropped(); }

private:
  /// Заголовок блока аудиохука.
  struct BlockHeader {
    std::uint64_t sequence = 0;
    double position = 0.0;
    double sampleRate = 0.0;
    double monotonic = 0.0;
    int length = 0;
    bool hasSamples = false;
  };

  /// Блок, уже поданный детектору: где он начался в потоке сэмплов и сдвинулась
  /// ли позиция транспорта с прошлого блока.
  struct StreamBlock {
    std::uint64_t streamStart = 0;
    BlockHeader header;
    bool moving = false;
  };

  static constexpr std::size_t kMaxBlock = 16384;

  static void onAudioBuffer(bool isPost, int length, double sampleRate,
                            audio_hook_register_t *registration);
  void onBlock(int length, double sampleRate, audio_hook_register_t *registration);

  void work();
  void feed(const BlockHeader &header);

  // Поля стоят в порядке, который не оставляет дыр выравнивания; кто их
  // пишет — в комментарии у каждого.

  // Звуковой поток пишет, рабочий читает. Сэмплы блока лежат в очереди
  // сэмплов раньше его заголовка.
  concurrency::SpscRing<BlockHeader, 1024> headers_;

  // Рабочий поток пишет, главный читает.
  concurrency::SpscRing<HookOnset, 1024> onsets_;

  // Звуковой поток пишет, рабочий читает.
  std::unique_ptr<concurrency::SpscRing<float, (1U << 18)>> samples_;

  // Звуковой поток пишет, читают все.
  std::atomic<double> sampleRate_{0.0};

  // Главный поток пишет, рабочий читает.
  std::atomic<double> silenceDb_{-50.0};

  // Только звуковой поток.
  std::uint64_t nextSequence_ = 0;

  // Только рабочий поток.
  double detectorRate_ = 0.0;
  double appliedSilenceDb_ = 0.0;
  std::uint64_t expectedSequence_ = 0;

  std::thread worker_;

  // Только рабочий поток.
  std::optional<double> previousPosition_;
  std::vector<float> blockSamples_;
  std::vector<StreamBlock> history_;
  std::vector<onset::Onset> found_;

  // Заполняет конструктор до регистрации хука, дальше его читает REAPER.
  audio_hook_register_t hook_{};

  // Только рабочий поток.
  std::optional<onset::Detector> detector_;

  // Главный поток пишет, звуковой читает.
  std::atomic<int> channel_{0};
  std::atomic<int> inputChannels_{0};

  // Только рабочий поток.
  std::uint32_t run_ = 0;

  // Только звуковой поток: блок, переведённый в float перед отправкой.
  std::array<float, kMaxBlock> scratch_{};

  // Только рабочий поток.
  bool streaming_ = false;

  bool registered_ = false;
  std::atomic<bool> stop_{false};
};

} // namespace training::reaper
