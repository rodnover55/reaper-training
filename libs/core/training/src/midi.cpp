#include "training/core/training/midi.hpp"

namespace training::core::training {

std::expected<void, InvalidStep> Midi::start() {
  const std::scoped_lock lock(mutex_);
  std::expected<void, InvalidStep> started = Trainer::start();
  // Время ядра у запуска своё: с нотами до запуска ноты не сравнивают.
  if (started)
    last_.reset();
  return started;
}

void Midi::receive(std::span<const input::MidiReceiver::Note> notes) {
  // Ноты ставятся под замком приёма, который берёт и `start`: каждая нота
  // ложится в тот запуск, в котором её приняли (`Midi.receive`).
  const std::scoped_lock lock(mutex_);
  // `MidiReceiver::receive#order`: нота старше последней обработанной
  // отбрасывается.
  for (const input::MidiReceiver::Note &note : notes) {
    if (last_ && note.time < *last_)
      continue;
    last_ = note.time;
    process({.time = note.time});
  }
}

} // namespace training::core::training
