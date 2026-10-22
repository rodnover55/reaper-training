#include <doctest/doctest.h>

#include "journal.hpp"

#include <fmt/format.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using training::core::Interval;
using training::core::Moment;
using training::core::calibration::Calibrator;
using training::core::calibration::NotesStep;
using training::reaper::CalibrationJournal;
using training::reaper::closeJournal;
using training::reaper::Input;
using training::reaper::InputSettings;
using training::reaper::inputText;
using training::reaper::journalLine;
using training::reaper::JournalSink;
using training::reaper::kJournalLimit;
using training::reaper::MidiEvent;
using training::reaper::momentText;
using training::reaper::noteText;
using training::reaper::openJournal;

namespace {

/// Пустой каталог для одного теста; удаляется вместе с содержимым.
class Scratch {
public:
  Scratch()
      : dir_(std::filesystem::temp_directory_path() /
             fmt::format("reaper-training-journal-{}",
                         std::chrono::steady_clock::now().time_since_epoch().count())) {
    std::filesystem::create_directories(dir_);
  }

  Scratch(const Scratch &) = delete;
  Scratch &operator=(const Scratch &) = delete;
  Scratch(Scratch &&) = delete;
  Scratch &operator=(Scratch &&) = delete;

  ~Scratch() {
    std::error_code error;
    std::filesystem::remove_all(dir_, error);
  }

  std::filesystem::path operator/(const std::string &name) const { return dir_ / name; }

private:
  std::filesystem::path dir_;
};

std::string contents(const std::filesystem::path &file) {
  std::ifstream in(file);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void write(const std::filesystem::path &file, const std::string &text) {
  std::ofstream(file) << text;
}

} // namespace

TEST_CASE("Журнал: большой файл уходит в .1, запись идёт заново") {
  const Scratch scratch;
  const auto log = scratch / "reaper-training.log";
  write(log, std::string(kJournalLimit + 1, 'x'));
  write(scratch / "reaper-training.log.1", "old");

  openJournal(log.string());
  journalLine("load");
  closeJournal();

  CHECK(std::filesystem::file_size(scratch / "reaper-training.log.1") == kJournalLimit + 1);
  const std::string text = contents(log);
  CHECK(text.find("load") != std::string::npos);
  CHECK(text.size() < 100);
}

TEST_CASE("Журнал: файл не больше предела дописывается") {
  const Scratch scratch;
  const auto log = scratch / "reaper-training.log";
  write(log, "earlier\n");

  openJournal(log.string());
  journalLine("load");
  closeJournal();

  const std::string text = contents(log);
  CHECK(text.starts_with("earlier\n"));
  CHECK(text.find("load") != std::string::npos);
  CHECK_FALSE(std::filesystem::exists(scratch / "reaper-training.log.1"));
}

TEST_CASE("Журнал: момент — секунды с шестью знаками") {
  // Доли микросекунды отбрасываются.
  CHECK(momentText(Moment() + 12'000'345'678 * Interval::kNanosecond) == "12.000345");
  CHECK(momentText(Moment{}) == "0.000000");
  CHECK(momentText(Moment() - 1500 * Interval::kMillisecond) == "-1.500000");
  // Микросекунды не теряются и через сутки работы часов.
  CHECK(momentText(Moment() + 108'000'000'001'000 * Interval::kNanosecond) == "108000.000001");
}

namespace {

/// Выбранный вход по умолчанию с каналом `channel`.
InputSettings onChannel(int channel) {
  InputSettings settings;
  REQUIRE(settings.setChannel(channel));
  return settings;
}

/// Выбранный вход MIDI `name` устройства `index` при канале `channel`.
InputSettings onMidi(int channel, const std::string &name, int index) {
  InputSettings settings = onChannel(channel);
  settings.setMidiInput(name);
  settings.setMidiIndex(index);
  settings.setInput(Input::Midi);
  return settings;
}

} // namespace

TEST_CASE("Журнал: выбранный вход") {
  // Канал — с 1, как в списке Input.
  CHECK(inputText(onChannel(0)) == "channel 1");
  CHECK(inputText(onChannel(3)) == "channel 4");
  // Вход MIDI — имя и номер устройства; канал звука при нём не пишется.
  CHECK(inputText(onMidi(3, "KeyStep 37", 2)) == "MIDI «KeyStep 37» #2");
}

namespace {

/// Приёмник, который складывает строки журнала в `lines`.
JournalSink sinkTo(std::vector<std::string> &lines) {
  return [&lines](std::string_view line) { lines.emplace_back(line); };
}

/// Note On канала 3 в позиции `position`, с; пусто — транспорт стоял.
MidiEvent noteOn(std::optional<double> position) {
  return MidiEvent{.position = position
                                   ? std::optional(Moment() + *position * Interval::kSecond)
                                   : std::nullopt,
                   .age = {},
                   .device = 0,
                   .loop = 2,
                   .size = 3,
                   .bytes = {0x92, 60, 100}};
}

} // namespace

TEST_CASE("Журнал: ход калибровки") {
  Calibrator::State result;
  result.step = training::core::calibration::Step::Done;
  result.noiseDb = -61.27;
  result.attacksDb = {-30.0, -58.04, -40.0, -20.06, -35.0, -45.0, -50.0, -25.0};
  result.peakDb = -3.5;
  result.verdict = NotesStep::Verdict::NoiseTooClose;
  result.thresholdDb = -60.0;
  std::vector<std::string> lines;
  CalibrationJournal calibration(sinkTo(lines));
  calibration.started(1, "Input 1", -42.0);
  calibration.silenceStep(48000.0);
  calibration.soundDuringSilence();
  calibration.notesStep(-61.27);
  calibration.attack(7, -20.06);
  calibration.finished(result);
  calibration.silenceSet(-60.0);
  calibration.appliedAnyway(-60.0);
  calibration.closedBySound();
  calibration.noSound();
  calibration.cancelledByPlayback();
  calibration.closedByPlayback();
  calibration.ended();
  // Строка итога длиннее строки кода: её литерал склеен из двух.
  const std::string finished =
      "calibration: noise too close, noise -61.3, attacks 8 from -58.0 to -20.1, peak "
      "-3.5, threshold -60 dBFS";
  CHECK(lines == std::vector<std::string>{
                     "calibration: started on input 2 (Input 1), silence -42 dBFS",
                     "calibration: 48000 Hz, silence step",
                     "calibration: sound during silence, silence step counts again",
                     "calibration: noise -61.3 dBFS, notes step",
                     "calibration: note 8 attack -20.1 dBFS", finished,
                     "calibration: silence -42 -> -60 dBFS",
                     "calibration: applied anyway, silence -60 dBFS",
                     "calibration: closed by sound after the result",
                     "calibration: no sound from the audio device for 2 s",
                     "calibration: cancelled by playback", "calibration: closed by playback",
                     "calibration: ended"});
}

TEST_CASE("Журнал: нота MIDI") {
  // Канал — с 1.
  CHECK(noteText(noteOn(1.0)) == "note=60 channel=3 velocity=100");
}
