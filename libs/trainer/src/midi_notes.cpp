#include "training/trainer/midi_notes.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>

namespace training::trainer {

std::string noteText(const MidiEvent &event) {
  return fmt::format("note={} channel={} velocity={}", event.bytes[1],
                     (event.bytes[0] & 0x0FU) + 1, event.bytes[2]);
}

MidiNotes::MidiNotes(Journal journal) : journal_(std::move(journal)) {}

void MidiNotes::start(double position, double countIn) {
  // Мёртвое время прошлого запуска в этот не попадает.
  deadTime_.reset();
  loop_.reset();
  startPosition_ = position;
  countIn_ = countIn;
}

void MidiNotes::onInputChange() {
  deadTime_.reset();
  early_.clear();
}

bool MidiNotes::isNote(const MidiEvent &event, const MidiRead &read) {
  const auto message = std::span<const std::uint8_t>(event.bytes)
                           .first(static_cast<std::size_t>(std::clamp(event.size, 0, 3)));
  return (event.device & 0xFFFF) == read.device && (event.device & kControlOnly) == 0 &&
         onset::isNoteStart(message);
}

double MidiNotes::placeOf(const MidiEvent &event, const MidiRead &read) const {
  // Во время отсчёта хост ставит событию позицию начала, в первом блоке
  // запуска — позицию, которую вёл на остановке, а до Play — −1. Место такой
  // ноты — по её времени от конца отсчёта; позиция после отсчёта идёт с той же
  // скоростью.
  if (read.runTime) {
    const double moment = *read.runTime - event.age;
    if (moment < countIn_ + read.blockLength)
      return startPosition_ + (moment - countIn_);
  }
  return event.position;
}

void MidiNotes::measure(const MidiEvent &event, double place, const MidiRead &read,
                        std::vector<MidiNote> &notes) {
  // Мёртвое время — по времени ноты на шкале проекта и кончается с проходом
  // петли. Нота до Play пришла до первого прохода запуска: проход петли она
  // не меняет.
  if (event.position >= 0.0) {
    if (loop_ && event.loop != *loop_)
      deadTime_.reset();
    loop_ = event.loop;
  }
  const double time = place - read.compensation;
  if (!deadTime_.add(time)) {
    write(journal_, "midi note skipped in dead time: t={:.6f} pp={:.6f} loop={} {}", time,
          event.position, event.loop, noteText(event));
    return;
  }
  notes.push_back({.event = event, .time = time, .beforeStart = place < startPosition_});
}

std::vector<MidiNote> MidiNotes::take(std::span<const MidiEvent> events,
                                      const MidiRead &read) {
  std::vector<MidiNote> notes;

  // Ноты до Play, ждущие запуска, старше kEarlyNoteSeconds забываются.
  if (read.hostTime)
    std::erase_if(early_, [&read](const EarlyNote &early) {
      return *read.hostTime - early.hostTime > kEarlyNoteSeconds;
    });

  // Время запуска известно: ноты до Play — перед позицией начала, раньше нот
  // этого чтения. Начало запуска по часам хоста — их время минус время
  // запуска.
  if (read.measuring && read.runTime && read.hostTime) {
    const double runStart = *read.hostTime - *read.runTime;
    for (const EarlyNote &early : early_)
      measure(early.event, startPosition_ + (early.hostTime - runStart - countIn_), read,
              notes);
    early_.clear();
  }

  for (const MidiEvent &event : events) {
    if (!isNote(event, read))
      continue;

    // Нота пришла при стоящем транспорте: место ей даёт время запуска, а
    // пока его нет, она ждёт запуска по часам хоста.
    const bool early = event.position < 0.0;
    if (early && (!read.measuring || !read.runTime)) {
      if (read.hostTime)
        early_.push_back({.event = event, .hostTime = *read.hostTime - event.age});
      continue;
    }
    if (!read.measuring)
      continue;
    measure(event, placeOf(event, read), read, notes);
  }
  return notes;
}

} // namespace training::trainer
