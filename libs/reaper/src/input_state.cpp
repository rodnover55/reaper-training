#include "training/reaper/input_state.hpp"

namespace training::reaper {

bool InputState::present(Input input) const {
  // Имя REAPER даёт только каналу, который есть у звуковой карты; хук
  // подтверждает канал буфером блока.
  if (input == Input::Audio)
    return !channelName.empty() && channelPresent.value_or(true);
  return midiName.has_value();
}

render::InputStatus InputState::status(Input input) const {
  if (!present(input))
    return render::InputStatus::NoInput;
  if (input == Input::Midi && !midiEnabled)
    return render::InputStatus::NotEnabled;
  return render::InputStatus::Present;
}

} // namespace training::reaper
