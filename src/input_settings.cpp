#include "input_settings.hpp"

#include <string>
#include <utility>

namespace training::reaper {

bool InputSettings::setChannel(int channel) {
  if (channel < 0)
    return false;
  channel_ = channel;
  return true;
}

void InputSettings::setMidiInput(std::string midiInput) { midiInput_ = std::move(midiInput); }

} // namespace training::reaper
