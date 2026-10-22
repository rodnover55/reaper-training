#pragma once

// Приём данных тренировкой (`training.core.training.Callback`).

#include "training/core/training/state/bar.hpp"

namespace training::core::training {

/// Интерфейс приёма тренировки: сигнал входа через `Receiver` — звук
/// (`input::SoundReceiver`) или ноты MIDI (`input::MidiReceiver`) — и такты.
template <class Receiver> class Callback : public Receiver {
public:
  /// Добавляет новый такт `bar` в тренажёр.
  // Чистая виртуальная функция не инстанцируется ни одним компилятором, а
  // чек считает её такой же, как функцию с телом.
  // NOLINTNEXTLINE(portability-template-virtual-member-function)
  virtual void addBar(const state::Bar &bar) = 0;
};

} // namespace training::core::training
