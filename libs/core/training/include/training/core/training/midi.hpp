#pragma once

// Тренировка по нотам MIDI (`training.core.training.Midi`).

#include "training/core/clock.hpp"
#include "training/core/hit_window.hpp"
#include "training/core/input/midi_receiver.hpp"
#include "training/core/training/callback.hpp"
#include "training/core/training/trainer.hpp"
#include "training/core/unit.hpp"

#include <expected>
#include <mutex>
#include <optional>
#include <span>
#include <utility>

namespace training::core::training {

/// Тренировка по нотам MIDI: каждую полученную ноту ставит в доли тактов
/// (`Trainer::process`).
///
/// Ноты идут по времени (`input::MidiReceiver`): нота старше последней
/// обработанной отбрасывается. Удачный запуск (`start`) начинает время нот
/// заново: время ядра у каждого запуска своё.
///
/// Многопоточность: все методы можно звать из любых потоков одновременно.
class Midi final : public Trainer, public Unit<Callback<input::MidiReceiver>, Trainer::State> {
public:
  /// Тренировка с окном попадания `hitWindow`.
  explicit Midi(const HitWindow &hitWindow) : Trainer(hitWindow) {}

  /// Запускает тренировку (`Trainer::start`). После удачного запуска время
  /// нот считается заново; при `InvalidStep` последняя обработанная нота
  /// остаётся (`input::MidiReceiver.receive#order`).
  std::expected<void, InvalidStep> start() override;

  /// Копия состояния тренировки (`Trainer::result`).
  Trainer::State result() const override { return Trainer::result(); }

  /// Добавляет такт (`Trainer::addBar`).
  void addBar(const state::Bar &bar) override { Trainer::addBar(bar); }

  /// Передаёт тренировке каждую ноту `notes` по порядку (`Trainer::process`);
  /// нота старше последней обработанной отбрасывается.
  void receive(std::span<const input::MidiReceiver::Note> notes) override;

private:
  // Время последней обработанной ноты — под этим замком; пусто — нот после
  // запуска ещё не было.
  mutable std::mutex mutex_;
  std::optional<Moment> last_;
};

} // namespace training::core::training
