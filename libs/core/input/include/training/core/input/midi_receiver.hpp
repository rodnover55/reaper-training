#pragma once

// Поставка нот MIDI в ядро (`training.core.input.MidiReceiver`).

#include "training/core/clock.hpp"

#include <span>

namespace training::core::input {

/// Приёмник нот MIDI блоками.
///
/// Условия вызова: ноты идут по времени, каждая следующая — не раньше
/// предыдущей. Нота старше последней обработанной приёмником отбрасывается.
///
/// Многопоточность: методы реализации можно звать из любых потоков
/// одновременно.
class MidiReceiver {
public:
  /// Нота MIDI: начало ноты выбранного устройства.
  struct Note {
    /// Время, когда возникла нота.
    Moment time;

    /// Правда, если у нот одно время.
    friend bool operator==(const Note &, const Note &) = default;
  };

  /// Позволяет удалять реализацию через указатель на `MidiReceiver`.
  virtual ~MidiReceiver() = default;

  /// Создаёт приёмник; своих данных у `MidiReceiver` нет, их хранит
  /// реализация.
  MidiReceiver() = default;

  /// Копирование запрещено: приёмник передаётся только по ссылке или указателю.
  MidiReceiver(const MidiReceiver &) = delete;

  /// Присваивание копией запрещено: приёмник передаётся
  /// только по ссылке или указателю.
  MidiReceiver &operator=(const MidiReceiver &) = delete;

  /// Перемещение запрещено: приёмник передаётся только по ссылке или указателю.
  MidiReceiver(MidiReceiver &&) = delete;

  /// Присваивание перемещением запрещено: приёмник передаётся
  /// только по ссылке или указателю.
  MidiReceiver &operator=(MidiReceiver &&) = delete;

  /// Принимает блок нот MIDI для обработки, по порядку.
  virtual void receive(std::span<const Note> notes) = 0;
};

} // namespace training::core::input
