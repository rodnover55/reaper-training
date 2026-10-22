#include <doctest/doctest.h>

#include "training/reaper/input_state.hpp"

using training::reaper::Input;
using training::reaper::InputState;
using training::render::InputStatus;

namespace {

/// Канал «Input 1», который есть у звуковой карты.
InputState channel() {
  return InputState{
      .channelName = "Input 1", .channelPresent = true, .midiName = {}, .midiEnabled = false};
}

/// Найденный вход MIDI «KeyStep 37»; `enabled` — включён ли он у REAPER для
/// записи на дорожки.
InputState keyStep(bool enabled) {
  return InputState{.channelName = {},
                    .channelPresent = {},
                    .midiName = "KeyStep 37",
                    .midiEnabled = enabled};
}

} // namespace

TEST_CASE("Выбранный вход: канал звуковой карты есть") {
  CHECK(channel().present(Input::Audio));
  CHECK(channel().status(Input::Audio) == InputStatus::Present);
}

TEST_CASE("Выбранный вход: выбранного канала нет") {
  // У REAPER нет имени канала.
  InputState unnamed = channel();
  unnamed.channelName.clear();
  CHECK_FALSE(unnamed.present(Input::Audio));
  CHECK(unnamed.status(Input::Audio) == InputStatus::NoInput);

  // Имя есть, но последний блок звука канала не нашёл.
  InputState missing = channel();
  missing.channelPresent = false;
  CHECK_FALSE(missing.present(Input::Audio));
  CHECK(missing.status(Input::Audio) == InputStatus::NoInput);

  // Блоков после выбора канала ещё не было — верим имени.
  InputState unchecked = channel();
  unchecked.channelPresent.reset();
  CHECK(unchecked.present(Input::Audio));
  CHECK(unchecked.status(Input::Audio) == InputStatus::Present);
}

TEST_CASE("Выбранный вход: вход MIDI") {
  CHECK(keyStep(true).present(Input::Midi));
  CHECK(keyStep(true).status(Input::Midi) == InputStatus::Present);
}

TEST_CASE("Выбранный вход: вход MIDI выключен у REAPER") {
  CHECK(keyStep(false).present(Input::Midi));
  CHECK(keyStep(false).status(Input::Midi) == InputStatus::NotEnabled);
}

TEST_CASE("Выбранный вход: устройства входа MIDI нет") {
  InputState absent = keyStep(true);
  absent.midiName.reset();
  CHECK_FALSE(absent.present(Input::Midi));
  CHECK(absent.status(Input::Midi) == InputStatus::NoInput);
}
