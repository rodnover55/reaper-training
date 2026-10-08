#include <doctest/doctest.h>

#include "training/trainer/journal.hpp"
#include "training/trainer/midi_notes.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

using training::trainer::kControlOnly;
using training::trainer::MidiEvent;
using training::trainer::MidiNote;
using training::trainer::MidiNotes;
using training::trainer::MidiRead;

namespace {

constexpr int kDevice = 3;

/// Note On устройства `kDevice` на канале 1 в позиции `position`, пришедший
/// `age` секунд шкалы проекта назад.
MidiEvent noteOn(double position, double age = 0.0, int loop = 0) {
  return MidiEvent{.position = position,
                   .age = age,
                   .device = kDevice,
                   .loop = loop,
                   .size = 3,
                   .bytes = {0x90, 60, 100}};
}

/// Чтение истории при идущем запуске с позицией обработки `position`.
MidiRead reading(double position) {
  return MidiRead{
      .measuring = true, .device = kDevice, .position = position, .compensation = 0.0};
}

/// Ноты MIDI со строками журнала в `lines`, запуск начат с позиции 1 с и
/// позиция уже сдвинулась.
MidiNotes moving(std::vector<std::string> &lines) {
  MidiNotes notes([&lines](std::string_view line) { lines.emplace_back(line); });
  notes.start(1.0);
  notes.take({}, reading(1.5));
  lines.clear();
  return notes;
}

} // namespace

TEST_CASE("Ноты MIDI: ноты чужого устройства не измеряются") {
  std::vector<std::string> lines;
  MidiNotes notes = moving(lines);
  MidiEvent other = noteOn(2.0);
  other.device = kDevice + 1;
  CHECK(notes.take(std::vector{other}, reading(2.1)).empty());
  CHECK(lines.empty());
}

TEST_CASE("Ноты MIDI: вход только для управления не измеряется") {
  std::vector<std::string> lines;
  MidiNotes notes = moving(lines);
  MidiEvent control = noteOn(2.0);
  control.device = kDevice | kControlOnly;
  CHECK(notes.take(std::vector{control}, reading(2.1)).empty());
  CHECK(lines.empty());
}

TEST_CASE("Ноты MIDI: нота при стоящем транспорте (pp < 0) не измеряется") {
  std::vector<std::string> lines;
  MidiNotes notes = moving(lines);
  CHECK(notes.take(std::vector{noteOn(-1.0)}, reading(2.1)).empty());
  CHECK(lines.empty());
}

TEST_CASE("Ноты MIDI: не начало ноты не измеряется") {
  std::vector<std::string> lines;
  MidiNotes notes = moving(lines);

  MidiEvent off = noteOn(2.0);
  off.bytes = {0x80, 60, 64};
  MidiEvent silentOn = noteOn(2.0);
  silentOn.bytes = {0x90, 60, 0};
  MidiEvent controller = noteOn(2.0);
  controller.bytes = {0xB0, 7, 100};
  MidiEvent shortOn = noteOn(2.0);
  shortOn.size = 2;

  CHECK(notes.take(std::vector{off, silentOn, controller, shortOn}, reading(2.1)).empty());
  CHECK(lines.empty());
}

TEST_CASE("Ноты MIDI: при count-in позиция стоит, и ноты отбрасываются") {
  std::vector<std::string> lines;
  MidiNotes notes([&lines](std::string_view line) { lines.emplace_back(line); });
  notes.start(4.0);

  // Позиция у начала записи и не сдвигается, пока идёт count-in.
  CHECK(notes.take(std::vector{noteOn(4.0, 0.010)}, reading(4.0)).empty());
  CHECK(notes.take(std::vector{noteOn(4.0, 0.0)}, reading(4.0)).empty());
  REQUIRE(lines.size() == 2);
  CHECK(lines[0] == "midi note dropped by count-in: pp=4.000000 note=60 channel=1 "
                    "velocity=100, arrived 10.0 ms of project time ago");
}

TEST_CASE("Ноты MIDI: в первом блоке после запуска отбрасывается нота, пришедшая "
          "до сдвига позиции") {
  std::vector<std::string> lines;
  MidiNotes notes([&lines](std::string_view line) { lines.emplace_back(line); });
  notes.start(0.0);

  // Позиция сдвинулась на 10 мс: нота 15 мс назад пришла раньше сдвига, нота
  // 5 мс назад — после. Первой хост ставит позицию остановленного транспорта.
  const std::vector<MidiNote> taken =
      notes.take(std::vector{noteOn(22.0, 0.015), noteOn(0.005, 0.005)}, reading(0.010));
  REQUIRE(taken.size() == 1);
  CHECK(taken[0].event.position == doctest::Approx(0.005));
  CHECK(taken[0].event.age == doctest::Approx(0.005));
  REQUIRE(lines.size() == 2);
  CHECK(lines[0] == "position moved by 10.0 ms");
  CHECK(lines[1].starts_with("midi note dropped by count-in: pp=22.000000"));
}

TEST_CASE("Ноты MIDI: возраст ноты и сдвиг позиции сравниваются на шкале проекта") {
  MidiNotes earlier;
  earlier.start(0.0);
  // Позиция ушла на 20 мс шкалы, нота пришла 30 мс шкалы назад — до сдвига.
  CHECK(earlier.take(std::vector{noteOn(0.010, 0.030)}, reading(0.020)).empty());

  MidiNotes later;
  later.start(0.0);
  CHECK(later.take(std::vector{noteOn(0.010, 0.010)}, reading(0.020)).size() == 1);
}

TEST_CASE("Ноты MIDI: после сдвига позиции нота измеряется с компенсацией") {
  std::vector<std::string> lines;
  MidiNotes notes = moving(lines);
  MidiRead read = reading(5.0);
  read.compensation = 0.006;

  // Сдвиг был на прошлом чтении: и давняя нота — не count-in.
  const std::vector<MidiNote> taken = notes.take(std::vector{noteOn(4.0, 1.0)}, read);
  REQUIRE(taken.size() == 1);
  CHECK(taken[0].time == doctest::Approx(4.0 - 0.006));
  CHECK(lines.empty());
}

TEST_CASE("Ноты MIDI: нота в мёртвом времени пропускается, новый проход петли его "
          "кончает") {
  std::vector<std::string> lines;
  MidiNotes notes = moving(lines);

  // Вторая нота — через 20 мс после первой, в мёртвом времени.
  CHECK(notes.take(std::vector{noteOn(2.000), noteOn(2.020)}, reading(2.1)).size() == 1);
  REQUIRE(lines.size() == 1);
  CHECK(lines[0] == "midi note skipped in dead time: t=2.020000 pp=2.020000 loop=0 "
                    "note=60 channel=1 velocity=100");

  // Петля завернула позицию к 2.01 с: по времени нота в мёртвом времени, но
  // это новый проход.
  const std::vector<MidiNote> taken =
      notes.take(std::vector{noteOn(2.010, 0, 1)}, reading(2.1));
  REQUIRE(taken.size() == 1);
  CHECK(taken[0].event.loop == 1);
}

TEST_CASE("Ноты MIDI: нота после мёртвого времени измеряется") {
  std::vector<std::string> lines;
  MidiNotes notes = moving(lines);
  CHECK(notes.take(std::vector{noteOn(2.000), noteOn(2.041)}, reading(2.1)).size() == 2);
}

TEST_CASE("Ноты MIDI: новый запуск и смена входа кончают мёртвое время") {
  std::vector<std::string> lines;
  MidiNotes notes = moving(lines);
  REQUIRE(notes.take(std::vector{noteOn(2.000)}, reading(2.1)).size() == 1);
  notes.onInputChange();
  CHECK(notes.take(std::vector{noteOn(2.010)}, reading(2.1)).size() == 1);

  notes.start(2.0);
  REQUIRE(notes.take({}, reading(2.5)).empty());
  CHECK(notes.take(std::vector{noteOn(2.020)}, reading(2.6)).size() == 1);
}

TEST_CASE("Ноты MIDI: пока тренажёр не меряет входом MIDI, нот нет и сдвиг не "
          "отмечается") {
  std::vector<std::string> lines;
  MidiNotes notes([&lines](std::string_view line) { lines.emplace_back(line); });
  notes.start(0.0);
  MidiRead read = reading(1.0);
  read.measuring = false;
  CHECK(notes.take(std::vector{noteOn(0.5)}, read).empty());
  CHECK(lines.empty());

  // Сдвиг отмечается на первом чтении, когда тренажёр меряет.
  CHECK(notes.take({}, reading(1.0)).empty());
  CHECK(lines == std::vector<std::string>{"position moved by 1000.0 ms"});
}
