#include <doctest/doctest.h>

#include "training/trainer/journal.hpp"
#include "training/trainer/midi_notes.hpp"

#include <cstdint>
#include <optional>
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

/// Длина блока хоста в тестах: 512 сэмплов при 48 кГц, секунды.
constexpr double kBlock = 512.0 / 48000.0;

/// Чтение истории при идущем запуске с позицией обработки `position`; время
/// запуска `runTime` — пусто, если хост ещё не обработал блоков запуска.
MidiRead reading(double position, std::optional<double> runTime = std::nullopt) {
  return MidiRead{.measuring = true,
                  .device = kDevice,
                  .position = position,
                  .runTime = runTime,
                  .blockLength = kBlock,
                  .hostTime = std::nullopt,
                  .compensation = 0.0};
}

/// Чтение истории на остановке: тренажёр не меряет, часы звука хоста —
/// `hostTime`.
MidiRead stopped(double hostTime) {
  MidiRead read = reading(0.0);
  read.measuring = false;
  read.hostTime = hostTime;
  return read;
}

/// Чтение при идущем запуске: время запуска `runTime`, первый блок запуска
/// начался на `runStart` по часам звука хоста.
MidiRead running(double position, double runTime, double runStart) {
  MidiRead read = reading(position, runTime);
  read.hostTime = runStart + runTime;
  return read;
}

/// Ноты MIDI со строками журнала в `lines`, запуск без отсчёта начат с позиции
/// 1 с.
MidiNotes moving(std::vector<std::string> &lines) {
  MidiNotes notes([&lines](std::string_view line) { lines.emplace_back(line); });
  notes.start(1.0, 0.0);
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

TEST_CASE("Ноты MIDI: нота при стоящем транспорте (pp < 0) без часов хоста не измеряется") {
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

TEST_CASE("Ноты MIDI: нота первого блока с позицией хоста с остановки получает место от "
          "позиции начала") {
  std::vector<std::string> lines;
  MidiNotes notes([&lines](std::string_view line) { lines.emplace_back(line); });
  notes.start(0.0, 0.0);

  // Хост поставил ноте позицию, которую вёл на остановке; пришла она в первом
  // блоке запуска — её возраст равен времени запуска при чтении.
  const double runTime = 5632.0 / 48000.0;
  const std::vector<MidiNote> taken =
      notes.take(std::vector{noteOn(21.941333, runTime)}, reading(runTime, runTime));
  REQUIRE(taken.size() == 1);
  CHECK(taken[0].time == doctest::Approx(0.0));
  CHECK_FALSE(taken[0].beforeStart);
  CHECK(lines.empty());
}

TEST_CASE("Ноты MIDI: нота второго блока и дальше — по позиции события") {
  MidiNotes notes;
  notes.start(0.0, 0.0);
  const double runTime = 0.117333;
  const std::vector<MidiNote> taken = notes.take(
      std::vector{noteOn(0.021333, runTime - 0.021333), noteOn(0.096, runTime - 0.096)},
      reading(runTime, runTime));
  REQUIRE(taken.size() == 2);
  CHECK(taken[0].time == doctest::Approx(0.021333));
  CHECK(taken[1].time == doctest::Approx(0.096));
}

TEST_CASE("Ноты MIDI: без времени запуска место — позиция события") {
  MidiNotes notes;
  notes.start(0.0, 0.0);
  const std::vector<MidiNote> taken = notes.take(std::vector{noteOn(0.5, 0.0)}, reading(0.6));
  REQUIRE(taken.size() == 1);
  CHECK(taken[0].time == doctest::Approx(0.5));
}

TEST_CASE("Ноты MIDI: нота отсчёта получает место сразу, пока позиция ещё стоит") {
  MidiNotes notes;
  notes.start(2.0, 4.0);

  // Отсчёт 4 с; чтение через 3.99 с после начала запуска, позиция обработки
  // стоит на позиции начала; нота пришла 5 мс назад — за 15 мс до конца
  // отсчёта. Хост поставил ей позицию начала.
  const std::vector<MidiNote> taken =
      notes.take(std::vector{noteOn(2.0, 0.005)}, reading(2.0, 3.99));
  REQUIRE(taken.size() == 1);
  CHECK(taken[0].time == doctest::Approx(2.0 - 0.015));
  CHECK(taken[0].beforeStart);
}

TEST_CASE("Ноты MIDI: нота отсчёта, прочитанная после конца отсчёта, — на своём месте") {
  MidiNotes notes;
  notes.start(0.0, 4.0);
  MidiRead read = reading(0.597333, 4.597333);
  read.compensation = 0.032;

  const std::vector<MidiNote> taken = notes.take(std::vector{noteOn(0.0, 0.608)}, read);
  REQUIRE(taken.size() == 1);
  CHECK(taken[0].time == doctest::Approx(-0.010667 - 0.032).epsilon(1e-6));
  CHECK(taken[0].beforeStart);
}

TEST_CASE("Ноты MIDI: после отсчёта и первого блока место — позиция события с компенсацией") {
  MidiNotes notes;
  notes.start(0.0, 4.0);
  MidiRead read = reading(0.6, 4.6);
  read.compensation = 0.006;
  const std::vector<MidiNote> taken = notes.take(std::vector{noteOn(0.5, 0.1)}, read);
  REQUIRE(taken.size() == 1);
  CHECK(taken[0].time == doctest::Approx(0.5 - 0.006));
  CHECK_FALSE(taken[0].beforeStart);
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

  notes.start(2.0, 0.0);
  CHECK(notes.take(std::vector{noteOn(2.020)}, reading(2.6)).size() == 1);
}

TEST_CASE("Ноты MIDI: пока тренажёр не меряет входом MIDI, нот нет") {
  std::vector<std::string> lines;
  MidiNotes notes([&lines](std::string_view line) { lines.emplace_back(line); });
  notes.start(0.0, 0.0);
  MidiRead read = reading(1.0);
  read.measuring = false;
  CHECK(notes.take(std::vector{noteOn(0.5)}, read).empty());
  CHECK(lines.empty());
}

TEST_CASE("Ноты MIDI: нота до Play ждёт запуска и встаёт перед позицией начала") {
  MidiNotes notes;
  // Нота пришла на остановке за 2 мс до чтения: по часам хоста — на 9.998 с.
  CHECK(notes.take(std::vector{noteOn(-1.0, 0.002)}, stopped(10.0)).empty());

  // Запуск с 1 с; первый блок запуска начался на 10.003 с по часам хоста.
  notes.start(1.0, 0.0);
  const std::vector<MidiNote> taken =
      notes.take(std::vector<MidiEvent>{}, running(1.02, 0.021333, 10.003));
  REQUIRE(taken.size() == 1);
  CHECK(taken[0].time == doctest::Approx(1.0 - 0.005));
  CHECK(taken[0].beforeStart);
}

TEST_CASE("Ноты MIDI: нота до Play с отсчётом — перед позицией начала на путь до конца "
          "отсчёта") {
  MidiNotes notes;
  CHECK(notes.take(std::vector{noteOn(-1.0, 0.002)}, stopped(10.0)).empty());
  notes.start(2.0, 4.0);
  const std::vector<MidiNote> taken =
      notes.take(std::vector<MidiEvent>{}, running(2.0, 0.5, 10.003));
  REQUIRE(taken.size() == 1);
  CHECK(taken[0].time == doctest::Approx(2.0 - 4.0 - 0.005));
}

TEST_CASE("Ноты MIDI: нота до Play старше секунды забывается") {
  MidiNotes notes;
  CHECK(notes.take(std::vector{noteOn(-1.0, 0.0)}, stopped(10.0)).empty());
  CHECK(notes.take(std::vector<MidiEvent>{}, stopped(11.5)).empty());
  notes.start(1.0, 0.0);
  CHECK(notes.take(std::vector<MidiEvent>{}, running(1.02, 0.021333, 11.6)).empty());
}

TEST_CASE("Ноты MIDI: нота до Play, прочитанная при запуске, встаёт сразу") {
  MidiNotes notes;
  notes.start(1.0, 0.0);
  // Время запуска 21.333 мс, нота пришла 30 мс назад — за 8.667 мс до первого
  // блока запуска; хост поставил ей −1.
  const std::vector<MidiNote> taken =
      notes.take(std::vector{noteOn(-1.0, 0.030)}, reading(1.02, 0.021333));
  REQUIRE(taken.size() == 1);
  CHECK(taken[0].time == doctest::Approx(1.0 - 0.008667));
  CHECK(taken[0].beforeStart);
}

TEST_CASE("Ноты MIDI: нота до Play без времени запуска ждёт следующего чтения") {
  MidiNotes notes;
  notes.start(1.0, 0.0);
  // Хост ещё не обработал блоков запуска; нота пришла на 9.996 с по его часам.
  MidiRead first = reading(1.0);
  first.hostTime = 10.0;
  CHECK(notes.take(std::vector{noteOn(-1.0, 0.004)}, first).empty());

  const std::vector<MidiNote> taken =
      notes.take(std::vector<MidiEvent>{}, running(1.01, 0.010667, 10.002));
  REQUIRE(taken.size() == 1);
  CHECK(taken[0].time == doctest::Approx(1.0 - 0.006));
}

TEST_CASE("Ноты MIDI: мёртвое время ноты до Play идёт после Play") {
  MidiNotes notes;
  CHECK(notes.take(std::vector{noteOn(-1.0, 0.002)}, stopped(10.0)).empty());
  notes.start(1.0, 0.0);
  // Нота до Play — на 0.995 с, нота запуска — на 1.020 с, через 25 мс: в
  // мёртвом времени первой.
  const std::vector<MidiNote> taken =
      notes.take(std::vector{noteOn(1.020, 0.020)}, running(1.04, 0.040, 10.003));
  REQUIRE(taken.size() == 1);
  CHECK(taken[0].beforeStart);
}

TEST_CASE("Ноты MIDI: смена входа забывает ноты до Play") {
  MidiNotes notes;
  CHECK(notes.take(std::vector{noteOn(-1.0, 0.002)}, stopped(10.0)).empty());
  notes.onInputChange();
  notes.start(1.0, 0.0);
  CHECK(notes.take(std::vector<MidiEvent>{}, running(1.02, 0.021333, 10.003)).empty());
}
