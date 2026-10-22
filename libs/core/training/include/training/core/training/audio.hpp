#pragma once

// Тренировка по звуку входа (`training.core.training.Audio`).

#include "training/core/clock.hpp"
#include "training/core/detector/detector.hpp"
#include "training/core/errors.hpp"
#include "training/core/hit_window.hpp"
#include "training/core/input/audio_block.hpp"
#include "training/core/input/sound_receiver.hpp"
#include "training/core/settings.hpp"
#include "training/core/training/callback.hpp"
#include "training/core/training/trainer.hpp"
#include "training/core/types.hpp"
#include "training/core/unit.hpp"

#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace training::core::training {

/// Тренировка по звуку входа: ищет атаки в блоках звука детектором и ставит
/// их в доли тактов как ноты (`Trainer::process`).
///
/// - **Время.** Блоки стыкуются по времени (`input::SoundReceiver`): начало
///   блока, которое лежит в уже разобранном звуке, отрезается, а пропавший
///   между блоками звук проходит в детекторе тишиной
///   (`detector::Detector::skip`). Удачный запуск (`start`) начинает время
///   блоков и поток детектора заново (`detector::Detector::reset`): время
///   ядра у каждого запуска своё.
/// - **Детектор** разбирает только блоки запуска: блоки до `start` и после
///   `stop` тренировка пропускает. Детектор тренировка создаёт сама при
///   создании (`create`) на своей частоте с порогом тишины из настроек; новый
///   порог он получает без пересоздания (`setSettings`).
///
/// Многопоточность: все методы можно звать из любых потоков одновременно.
class Audio final : public Trainer,
                    public Unit<Callback<input::SoundReceiver>, Trainer::State> {
private:
  /// Ключ конструктора: создать тренировку может только `create`.
  struct Key {
    /// Создаёт ключ; явный: снаружи ключ не создаётся записью `{}`.
    explicit Key() = default;
  };

public:
  /// Создаёт тренировку блоков частоты `sampleRate` с окном попадания
  /// `Settings::window` и её детектор атак с порогом тишины
  /// `Settings::silenceDb` из настроек `settings`.
  ///
  /// @return `CannotStartUnit`, если детектор атак на частоте `sampleRate`
  ///   не создаётся (`detector::Detector::create`).
  static std::expected<std::unique_ptr<Audio>, CannotStartUnit>
  create(SampleRate sampleRate, const Settings &settings);

  /// Тренировка, которая ищет атаки детектором `detector` с окном попадания
  /// `hitWindow`; частота блоков — частота детектора. Зовёт только `create`.
  Audio(Key /*key*/, detector::Detector detector, const HitWindow &hitWindow);

  /// Запускает тренировку (`Trainer::start`). После удачного запуска время
  /// блоков считается заново, а детектор начинает поток с первого блока
  /// запуска; при `InvalidStep` конец разобранного звука остаётся
  /// (`input::SoundReceiver.receive#order`).
  std::expected<void, InvalidStep> start() override;

  /// Копия состояния тренировки (`Trainer::result`).
  State result() const override { return Trainer::result(); }

  /// Добавляет такт (`Trainer::addBar`).
  void addBar(const state::Bar &bar) override { Trainer::addBar(bar); }

  /// Частота дискретизации блоков тренировки, с которой она создана
  /// (`create`).
  SampleRate sampleRate() const override { return rate_; }

  /// Ставит настройки `settings` (`Trainer::setSettings`), а порог тишины
  /// (`Settings::silenceDb`) — детектору без его пересоздания
  /// (`detector::Detector::setSilenceDb`): запуск, поток детектора и атаки,
  /// которые он ещё определяет, остаются.
  void setSettings(const Settings &settings) override;

  /// Ищет атаки в звуке блока `block` и передаёт найденные ноты тренировке
  /// (`Trainer::process`). Блок вне запуска пропускается.
  void receive(const input::AudioBlock &block) override;

private:
  /// Блок, уже поданный детектору: где он начался в потоке детектора
  /// (`position_`) и время его первого сэмпла.
  struct StreamBlock {
    std::uint64_t streamStart = 0;
    Moment start;
  };

  /// Переводит атаки, которые детектор нашёл в разобранном звуке, во время
  /// ядра и передаёт их тренировке нотами (`Trainer::process`). Зовётся под
  /// `mutex_`.
  void takeOnsets();

  SampleRate rate_;

  // Состояние разбора звука — под этим замком.
  mutable std::mutex mutex_;
  detector::Detector detector_;
  std::vector<StreamBlock> history_;
  // Позиция в потоке детектора: сколько сэмплов он разобрал с начала потока,
  // пропуски включительно (`detector::Onset::position`). Растёт вместе с
  // каждым `process` и `skip` детектора, `reset` детектора её обнуляет.
  std::uint64_t position_ = 0;
  // Конец разобранного звука; пусто — блоков после запуска ещё не было.
  std::optional<Moment> heardEnd_;
};

} // namespace training::core::training
