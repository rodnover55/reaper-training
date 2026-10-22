#include "training/reaper/midi_places.hpp"

#include <utility>
#include <vector>

namespace training::reaper {

std::vector<core::input::MidiReceiver::Note>
MidiPlaces::place(std::span<const MidiEvent> events, const MidiRead &read,
                  std::optional<core::Moment> end) {
  std::vector<core::input::MidiReceiver::Note> notes;
  // Ноты отдаются при запуске со входом MIDI.
  const bool measuring = running_ && read.input == Input::Midi;
  if (!end)
    return notes;

  // Ноты до Play, ждущие запуска, старше kEarlyNote забываются; при запуске
  // они идут раньше нот этого чтения.
  std::erase_if(early_, [&end](const core::input::MidiReceiver::Note &early) {
    return *end - early.time > kEarlyNote;
  });
  if (measuring) {
    notes = std::move(early_);
    early_.clear();
  }

  for (const MidiEvent &event : events) {
    if (!isNote(event, read))
      continue;
    const core::input::MidiReceiver::Note note{.time = *end - event.age};
    if (measuring)
      notes.push_back(note);
    else if (!event.position)
      // Нота пришла при стоящем транспорте: ждёт запуска.
      early_.push_back(note);
  }
  return notes;
}

} // namespace training::reaper
