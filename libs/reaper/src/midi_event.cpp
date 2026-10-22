#include "training/reaper/midi_event.hpp"

#include <algorithm>
#include <cstddef>

namespace training::reaper {

bool isNoteStart(std::span<const std::uint8_t> message) {
  return message.size() >= 3 && (message[0] & 0xF0U) == 0x90U && message[2] != 0;
}

bool isNote(const MidiEvent &event, const MidiRead &read) {
  const auto message = std::span<const std::uint8_t>(event.bytes)
                           .first(static_cast<std::size_t>(std::clamp(event.size, 0, 3)));
  return (event.device & 0xFFFF) == read.device && (event.device & kControlOnly) == 0 &&
         isNoteStart(message);
}

} // namespace training::reaper
