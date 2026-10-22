#pragma once

// Время нот истории MIDI-входа REAPER во времени ядра (`hit-timing`).

#include "training/core/clock.hpp"
#include "training/core/input/midi_receiver.hpp"
#include "training/reaper/midi_event.hpp"

#include <optional>
#include <span>
#include <vector>

namespace training::reaper {

/// Сколько нота, пришедшая при стоящем транспорте, ждёт запуска как нота до
/// Play.
inline constexpr core::Interval kEarlyNote = core::Interval::kSecond;

/// Ноты MIDI у запусков транспорта REAPER: какие ноты истории MIDI-входа
/// отдавать ядру и их время. Нота — начало ноты выбранного устройства
/// (`isNote`); ноты отдаются, пока идёт запуск (от `start` до `stop`) и вид
/// входа — MIDI (`MidiRead::input`).
///
/// - **Время ноты** — время ядра: конец взятого звука на момент чтения
///   истории минус возраст события (`MidiEvent::age`). Без конца взятого звука
///   у ноты времени нет, и она пропускается.
/// - **Нота до Play** — нота, пришедшая при стоящем транспорте (позиции у
///   события нет), ждёт запуска `kEarlyNote` и отдаётся первым чтением
///   запуска, раньше нот этого чтения.
///
/// Созданный объект: запуска нет, нот до Play нет.
///
/// Многопоточность: один поток вызовов.
class MidiPlaces {
public:
  /// Начинает запуск транспорта.
  void start() { running_ = true; }

  /// Кончает запуск транспорта: ноты больше не отдаются, а ноты, пришедшие при
  /// стоящем транспорте, ждут следующего запуска как ноты до Play.
  void stop() { running_ = false; }

  /// Забывает ноты до Play: их сыграли на прежнем входе.
  void forget() { early_.clear(); }

  /// Отбирает ноты из событий, прочитанных из истории MIDI-входа одним
  /// чтением.
  ///
  /// @param events события от старых к новым.
  /// @param read какой вход слушается в момент чтения.
  /// @param end конец взятого звука во времени ядра на момент чтения — конец
  ///   последнего блока, который обработал REAPER; пусто — блоков ещё не было.
  /// @return ноты по порядку: сначала ноты до Play, ждавшие запуска, потом
  ///   ноты событий; пусто без запуска и при виде входа `Input::Audio`.
  std::vector<core::input::MidiReceiver::Note> place(std::span<const MidiEvent> events,
                                                     const MidiRead &read,
                                                     std::optional<core::Moment> end);

private:
  std::vector<core::input::MidiReceiver::Note> early_;
  // Идёт запуск: от `start` до `stop`.
  bool running_ = false;
};

} // namespace training::reaper
