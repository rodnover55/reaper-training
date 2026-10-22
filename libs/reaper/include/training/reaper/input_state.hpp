#pragma once

// Выбранный вход, каким его видит REAPER (`timing-display`, `input-calibration`):
// есть ли он и включён ли.

#include "training/reaper/input.hpp"
#include "training/render/main/status_line.hpp"

#include <optional>
#include <string>

namespace training::reaper {

/// Выбранный вход, каким его видит REAPER.
struct InputState {
  /// Имя выбранного входного канала звуковой карты у REAPER; пусто — у
  /// звуковой карты нет такого канала.
  std::string channelName;

  /// Есть ли выбранный канал у звуковой карты по последнему блоку звука, в
  /// котором это проверялось; пусто — после выбора канала блоков не было.
  std::optional<bool> channelPresent;

  /// Имя выбранного входа MIDI у REAPER, с псевдонимом; пусто — вход MIDI не
  /// выбран или устройства с его именем сейчас нет.
  std::optional<std::string> midiName;

  /// Правда, если найденный вход MIDI включён у REAPER для записи на дорожки.
  bool midiEnabled = false;

  /// Правда, если выбранный вход вида `input` есть. Канал звуковой карты
  /// есть, если у него есть имя (`channelName`) и последний блок звука, где
  /// канал проверялся, его нашёл или блоков после выбора канала ещё не было
  /// (`channelPresent`). Вход MIDI есть, если найдено устройство с его именем
  /// (`midiName`).
  bool present(Input input) const;

  /// Выбранный вход вида `input` для строки состояния: входа нет
  /// (`present`) — `render::InputStatus::NoInput`; вход MIDI выключен у
  /// REAPER для записи на дорожки — `render::InputStatus::NotEnabled`; иначе
  /// `render::InputStatus::Present`.
  render::InputStatus status(Input input) const;
};

} // namespace training::reaper
