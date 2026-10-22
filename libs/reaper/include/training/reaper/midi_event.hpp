#pragma once

// События истории MIDI-входа REAPER и отбор начал нот выбранного устройства
// (`hit-timing`).

#include "training/core/clock.hpp"
#include "training/reaper/input.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace training::reaper {

/// Бит номера устройства события истории MIDI-входа: вход включён только для
/// управления.
inline constexpr int kControlOnly = 0x10000;

/// Событие истории MIDI-входа REAPER.
struct MidiEvent {
  /// Позиция на шкале проекта; пусто — транспорт стоял.
  std::optional<core::Moment> position;

  /// Сколько звука прошло от прихода события до чтения истории — до конца
  /// последнего блока, который обработал REAPER, — в реальном времени, не
  /// меньше нуля; ноль — неизвестно.
  core::Interval age;

  /// Номер устройства в младших 16 битах, бит `kControlOnly` — вход только
  /// для управления.
  int device = 0;

  /// Номер прохода петли.
  int loop = 0;

  /// Длина сообщения, байт.
  int size = 0;

  /// Первые байты сообщения, не больше трёх; дальше `size` — нули.
  std::array<std::uint8_t, 3> bytes{};
};

/// Чтение истории MIDI-входа: какой вход слушается в момент чтения.
struct MidiRead {
  /// Вид входа, которым тренажёр слышит игру: ноты MIDI берутся при
  /// `Input::Midi`.
  Input input = Input::Audio;

  /// Номер выбранного устройства MIDI, которое есть у REAPER и включено у
  /// него для записи на дорожки; −1 — выбранное устройство пропало или
  /// выключено, и события всех устройств пропускаются.
  int device = 0;
};

/// Правда, если MIDI-сообщение `message` — начало ноты: Note On (статус
/// 0x90–0x9F, любой из 16 каналов) с ненулевой громкостью. Note On с
/// громкостью 0 — это конец ноты. Сообщение короче трёх байт — не начало
/// ноты.
bool isNoteStart(std::span<const std::uint8_t> message);

/// Правда, если событие `event` — начало ноты (`isNoteStart`) выбранного
/// устройства `read.device`, включённого не только для управления.
bool isNote(const MidiEvent &event, const MidiRead &read);

} // namespace training::reaper
