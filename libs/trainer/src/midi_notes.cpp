#include "training/trainer/midi_notes.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>

namespace training::trainer {

std::string noteText(const MidiEvent &event) {
  return fmt::format("note={} channel={} velocity={}", event.bytes[1],
                     (event.bytes[0] & 0x0FU) + 1, event.bytes[2]);
}

MidiNotes::MidiNotes(Journal journal) : journal_(std::move(journal)) {}

void MidiNotes::start(double position) {
  // Мёртвое время прошлого запуска в этот не попадает.
  deadTime_.reset();
  startPosition_ = position;
  moved_ = false;
}

void MidiNotes::onInputChange() { deadTime_.reset(); }

std::optional<double> MidiNotes::movedBy(const MidiRead &read) {
  if (moved_)
    return std::numeric_limits<double>::infinity();
  if (read.position == startPosition_)
    return std::nullopt;

  moved_ = true;
  const double by = read.position - startPosition_;
  write(journal_, "position moved by {:.1f} ms", by * 1000.0);
  return by;
}

std::vector<MidiNote> MidiNotes::take(std::span<const MidiEvent> events,
                                      const MidiRead &read) {
  std::vector<MidiNote> notes;
  if (!read.measuring)
    return notes;

  // Count-in: позиция стоит у начала записи, и места нот MIDI на шкале нет.
  // Нота, пришедшая не позже, чем позиция сдвинулась, отбрасывается: и при
  // count-in, и в первом блоке после запуска, где хост ставит ноте позицию
  // остановленного транспорта.
  const std::optional<double> moved = movedBy(read);
  for (const MidiEvent &event : events) {
    const auto message = std::span<const std::uint8_t>(event.bytes)
                             .first(static_cast<std::size_t>(std::clamp(event.size, 0, 3)));
    if ((event.device & 0xFFFF) != read.device || (event.device & kControlOnly) != 0 ||
        event.position < 0.0 || !onset::isNoteStart(message))
      continue;

    if (!moved || event.age >= *moved) {
      write(
          journal_,
          "midi note dropped by count-in: pp={:.6f} {}, arrived {:.1f} ms of project time ago",
          event.position, noteText(event), event.age * 1000.0);
      continue;
    }

    // Мёртвое время — по времени ноты на шкале проекта и кончается с проходом
    // петли.
    if (event.loop != loop_)
      deadTime_.reset();
    loop_ = event.loop;
    const double time = event.position - read.compensation;
    if (!deadTime_.add(time)) {
      write(journal_, "midi note skipped in dead time: t={:.6f} pp={:.6f} loop={} {}", time,
            event.position, event.loop, noteText(event));
      continue;
    }

    notes.push_back({.event = event, .time = time});
  }
  return notes;
}

} // namespace training::trainer
