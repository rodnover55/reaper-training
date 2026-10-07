#pragma once

// Входы MIDI, которые знает REAPER (design.md D5 изменения add-midi-input).

#include <optional>
#include <string>
#include <vector>

namespace training::reaper {

/// Номер виртуальной MIDI-клавиатуры REAPER среди входов MIDI.
inline constexpr int kVirtualKeyboard = 62;

/// Вход MIDI, который знает REAPER.
struct MidiInput {
  /// Номер устройства в REAPER. Устройства нумеруются по порядку появления:
  /// при другом наборе устройств номер у того же устройства другой.
  int device = 0;

  /// Имя, под которым устройство видно в REAPER, с псевдонимом из его
  /// настроек (`GetMIDIInputName`).
  std::string name;

  /// Имя для сохранения в настройках: без псевдонима
  /// (`GetMIDIInputNameNoAlias`), а если оно пустое, как у виртуальной
  /// клавиатуры, — с псевдонимом.
  std::string key;

  /// Включён ли вход в настройках REAPER для записи на дорожки
  /// (`midiInputEnabled`).
  bool enabled = false;
};

/// Входы MIDI, которые есть сейчас, по возрастанию номера: устройства, для
/// которых `GetMIDIInputName` отвечает, что они есть. Петлевых входов REAPER
/// («REAPER Loopback N») в списке нет. Зовётся из главного потока.
std::vector<MidiInput> midiInputs();

/// Правда, если вход MIDI с номером `device` включён в настройках REAPER для
/// записи на дорожки: бит устройства в маске `midiins`. Вход, включённый
/// только для управления, не включён. Виртуальная клавиатура включена
/// всегда. Маска читается из памяти REAPER при каждом вызове: перемена в окне
/// настроек REAPER видна сразу. Зовётся из главного потока.
bool midiInputEnabled(int device);

/// Находит в `inputs` вход по имени для сохранения `key`
/// (`MidiInput::key`).
///
/// @param device сохранённый номер устройства: из нескольких входов с этим
///   именем выбирается вход с этим номером, а если такого нет, — первый.
/// @return вход; пусто — входа с таким именем нет.
std::optional<MidiInput> findMidiInput(const std::vector<MidiInput> &inputs,
                                       const std::string &key, int device);

} // namespace training::reaper
