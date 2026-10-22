#pragma once

// Выбранный вход тренажёра у REAPER (`timing-display`): вид входа, канал
// звуковой карты и устройство MIDI. Общий для всех проектов.

#include "training/reaper/input.hpp"

#include <string>

namespace training::reaper {

/// Выбранный вход у REAPER: вид входа, канал звуковой карты и устройство
/// MIDI. Какой из них действует, говорит вид входа (`input`). Значение каждой
/// настройки допустимо: сеттер проверяемой настройки отклоняет недопустимое
/// значение, и настройки остаются прежними.
///
/// Созданный объект: вход — канал звуковой карты, первый канал, без входа
/// MIDI.
class InputSettings {
public:
  /// Вид входа: канал звуковой карты или вход MIDI.
  Input input() const { return input_; }

  /// Ставит вид входа.
  void setInput(Input input) { input_ = input; }

  /// Входной канал звуковой карты, 0 — первый. Пока выбран вход MIDI,
  /// остаётся прежним: при возврате к звуку выбран тот же канал.
  int channel() const { return channel_; }

  /// Ставит входной канал звуковой карты.
  ///
  /// @return ложь, если `channel` меньше 0: настройки прежние.
  bool setChannel(int channel);

  /// Выбранный вход MIDI — имя устройства для сохранения: имя без
  /// псевдонима, а если его нет, — с псевдонимом. Действует, когда вид входа —
  /// `Input::Midi`.
  const std::string &midiInput() const { return midiInput_; }

  /// Ставит имя выбранного входа MIDI.
  void setMidiInput(std::string midiInput);

  /// Номер устройства MIDI у REAPER, когда вход выбирали: различает два
  /// устройства с одним именем. По умолчанию −1.
  int midiIndex() const { return midiIndex_; }

  /// Ставит номер устройства MIDI у REAPER для выбранного входа MIDI; номер
  /// принимается любой.
  void setMidiIndex(int midiIndex) { midiIndex_ = midiIndex; }

  /// Правда, если совпадают все настройки.
  bool operator==(const InputSettings &) const = default;

private:
  Input input_ = Input::Audio;
  std::string midiInput_;
  int channel_ = 0;
  int midiIndex_ = -1;
};

} // namespace training::reaper
