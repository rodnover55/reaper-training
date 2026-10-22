#include <doctest/doctest.h>

#include "training/reaper/midi_event.hpp"

#include <array>
#include <cstdint>

using training::reaper::Input;
using training::reaper::isNote;
using training::reaper::isNoteStart;
using training::reaper::kControlOnly;
using training::reaper::MidiEvent;
using training::reaper::MidiRead;

namespace {

constexpr int kDevice = 3;

bool noteStart(std::uint8_t status, std::uint8_t data1, std::uint8_t data2) {
  const std::array<std::uint8_t, 3> message{status, data1, data2};
  return isNoteStart(message);
}

/// Note On устройства `kDevice` на канале 1.
MidiEvent noteOn() {
  return MidiEvent{.position = std::nullopt,
                   .age = {},
                   .device = kDevice,
                   .loop = 0,
                   .size = 3,
                   .bytes = {0x90, 60, 100}};
}

/// Чтение истории со входом MIDI устройства `kDevice`.
MidiRead reading() { return MidiRead{.input = Input::Midi, .device = kDevice}; }

} // namespace

TEST_CASE("начало ноты: Note On с ненулевой громкостью на любом канале") {
  CHECK(noteStart(0x99, 38, 1));
  CHECK(noteStart(0x90, 60, 127));
  CHECK(noteStart(0x9F, 60, 64));

  // Note On с громкостью 0 — конец ноты.
  CHECK_FALSE(noteStart(0x90, 60, 0));
  CHECK_FALSE(noteStart(0x80, 60, 64));
  // Педаль сустейна.
  CHECK_FALSE(noteStart(0xB0, 64, 127));
  // Pitch bend.
  CHECK_FALSE(noteStart(0xE0, 0x00, 0x40));

  const std::array<std::uint8_t, 2> shortMessage{0x90, 60};
  CHECK_FALSE(isNoteStart(shortMessage));
}

TEST_CASE("нота выбранного устройства: начало ноты выбранного устройства") {
  CHECK(isNote(noteOn(), reading()));
}

TEST_CASE("нота выбранного устройства: чужое устройство и вход для управления — не нота") {
  MidiEvent other = noteOn();
  other.device = kDevice + 1;
  CHECK_FALSE(isNote(other, reading()));

  MidiEvent control = noteOn();
  control.device = kDevice | kControlOnly;
  CHECK_FALSE(isNote(control, reading()));

  // Устройство пропало или выключено у REAPER: события всех устройств
  // пропускаются.
  CHECK_FALSE(isNote(noteOn(), MidiRead{.input = Input::Midi, .device = -1}));
}

TEST_CASE("нота выбранного устройства: не начало ноты — не нота") {
  MidiEvent off = noteOn();
  off.bytes = {0x80, 60, 64};
  MidiEvent silentOn = noteOn();
  silentOn.bytes = {0x90, 60, 0};
  MidiEvent controller = noteOn();
  controller.bytes = {0xB0, 7, 100};
  MidiEvent shortOn = noteOn();
  shortOn.size = 2;

  for (const MidiEvent &event : {off, silentOn, controller, shortOn})
    CHECK_FALSE(isNote(event, reading()));
}
