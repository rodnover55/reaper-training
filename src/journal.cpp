#include "journal.hpp"

#include "clock.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <system_error>
#include <utility>

namespace training::reaper {
namespace {

std::mutex journalMutex;
std::FILE *journalFile = nullptr;

/// Монотонные часы журнала: по ним строки журнала отсчитывают секунды.
using Clock = std::chrono::steady_clock;
Clock::time_point journalStart = Clock::now();

/// Счётчик номеров потоков: номера раздаются по первому появлению потока в
/// журнале.
std::atomic<int> nextThreadNumber{0};

/// Возвращает короткое имя текущего потока для строки журнала. Поток, который
/// открыл журнал, получает номер 0 и имя `main`; остальные — `T1`, `T2`, ...
std::string threadTag() {
  thread_local const int number = nextThreadNumber.fetch_add(1);
  return number == 0 ? std::string("main") : fmt::format("T{}", number);
}

/// Путь из строки UTF-8: на Windows `std::filesystem::path` из `char`
/// читает строку в кодировке системы, а не в UTF-8.
std::filesystem::path pathOf(const std::string &utf8) {
  return {std::u8string(utf8.begin(), utf8.end())};
}

/// Форматирует строку по правилам {fmt} и отдаёт её приёмнику `sink`.
template <class... Args>
void writeTo(const JournalSink &sink, fmt::format_string<Args...> format, Args &&...args) {
  sink(fmt::format(format, std::forward<Args>(args)...));
}

/// Имя вердикта калибровки для журнала: «good», «noise too close»,
/// «clipping», «not enough notes».
const char *verdictName(core::calibration::NotesStep::Verdict verdict) {
  using Verdict = core::calibration::NotesStep::Verdict;
  switch (verdict) {
  case Verdict::Good:
    return "good";
  case Verdict::NoiseTooClose:
    return "noise too close";
  case Verdict::Clipping:
    return "clipping";
  case Verdict::NotEnoughNotes:
    return "not enough notes";
  }
  return "?";
}

/// Открывает файл `file` на дописывание; файла нет — создаёт его.
///
/// @return открытый файл; nullptr — открыть не удалось.
///
/// На Windows: имя передаётся в UTF-16 и не зависит от кодировки системы.
std::FILE *openAppending(const std::filesystem::path &file) {
#ifdef _WIN32
  return _wfopen(file.c_str(), L"a");
#else
  return std::fopen(file.c_str(), "a");
#endif
}

} // namespace

void openJournal(const std::string &path) {
  const std::scoped_lock lock(journalMutex);

  if (journalFile)
    return;

  // Журнал не растёт без конца: большой уходит в `.1`, и запись идёт заново.
  const std::filesystem::path file = pathOf(path);
  std::error_code error;
  if (const std::uintmax_t size = std::filesystem::file_size(file, error);
      !error && size > kJournalLimit) {
    std::filesystem::path previous = file;
    previous += ".1";
    std::filesystem::remove(previous, error);
    std::filesystem::rename(file, previous, error);
  }

  journalFile = openAppending(file);
  journalStart = Clock::now();

  // Главный поток получает номер первым — до того, как REAPER позовёт
  // расширение из других.
  (void)threadTag();
}

void closeJournal() {
  const std::scoped_lock lock(journalMutex);

  if (journalFile)
    (void)std::fclose(journalFile);

  journalFile = nullptr;
}

double journalSeconds(std::chrono::steady_clock::time_point at) {
  return std::chrono::duration<double>(at - journalStart).count();
}

std::chrono::steady_clock::time_point journalOpenedAt() { return journalStart; }

std::string momentText(core::Moment moment) {
  // Целыми, а не через double: в double наносекунды от точки отсчёта теряют
  // младшие знаки. Доли микросекунды отбрасываются: порядок моментов в
  // записи остаётся тем же, хотя равными могут стать разные моменты.
  const std::chrono::nanoseconds since(
      static_cast<std::int64_t>((moment - core::Moment()) / core::Interval::kNanosecond));
  const bool negative = since < std::chrono::nanoseconds::zero();
  const std::chrono::nanoseconds magnitude = negative ? -since : since;
  const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(magnitude);
  const auto fraction =
      std::chrono::duration_cast<std::chrono::microseconds>(magnitude - seconds);
  return fmt::format("{}{}.{:06}", negative ? "-" : "", seconds.count(), fraction.count());
}

std::string inputText(const InputSettings &settings) {
  if (settings.input() == Input::Audio)
    return fmt::format("channel {}", settings.channel() + 1);
  return fmt::format("MIDI «{}» #{}", settings.midiInput(), settings.midiIndex());
}

std::string noteText(const MidiEvent &event) {
  return fmt::format("note={} channel={} velocity={}", event.bytes[1],
                     (event.bytes[0] & 0x0FU) + 1, event.bytes[2]);
}

void journalLine(std::string_view line) {
  const std::string tag = threadTag();
  const double seconds = std::chrono::duration<double>(Clock::now() - journalStart).count();
  const std::string text = fmt::format("{:12.6f} {:>4} {}\n", seconds, tag, line);

  const std::scoped_lock lock(journalMutex);

  if (!journalFile)
    return;

  (void)std::fputs(text.c_str(), journalFile);
  (void)std::fflush(journalFile);
}

CalibrationJournal::CalibrationJournal(JournalSink sink) : sink_(std::move(sink)) {}

void CalibrationJournal::started(int channel, const std::string &inputName, double silenceDb) {
  fromDb_ = silenceDb;
  writeTo(sink_, "calibration: started on input {} ({}), silence {} dBFS", channel + 1,
          inputName, silenceDb);
}

void CalibrationJournal::ended() { writeTo(sink_, "calibration: ended"); }

void CalibrationJournal::cancelledByPlayback() {
  writeTo(sink_, "calibration: cancelled by playback");
}

void CalibrationJournal::closedByPlayback() {
  writeTo(sink_, "calibration: closed by playback");
}

void CalibrationJournal::silenceStep(double sampleRate) {
  writeTo(sink_, "calibration: {} Hz, silence step", sampleRate);
}

void CalibrationJournal::soundDuringSilence() {
  writeTo(sink_, "calibration: sound during silence, silence step counts again");
}

void CalibrationJournal::notesStep(double noiseDb) {
  writeTo(sink_, "calibration: noise {:.1f} dBFS, notes step", noiseDb);
}

void CalibrationJournal::attack(std::size_t note, double attackDb) {
  writeTo(sink_, "calibration: note {} attack {:.1f} dBFS", note + 1, attackDb);
}

void CalibrationJournal::finished(const core::calibration::Calibrator::State &state) {
  double softestDb = core::calibration::kFloorDb;
  double loudestDb = core::calibration::kFloorDb;
  if (!state.attacksDb.empty()) {
    const auto [softest, loudest] = std::ranges::minmax_element(state.attacksDb);
    softestDb = *softest;
    loudestDb = *loudest;
  }
  writeTo(sink_,
          "calibration: {}, noise {:.1f}, attacks {} from {:.1f} to {:.1f}, peak {:.1f}, "
          "threshold {} dBFS",
          verdictName(state.verdict), state.noiseDb, state.attacksDb.size(), softestDb,
          loudestDb, state.peakDb, state.thresholdDb);
}

void CalibrationJournal::silenceSet(double silenceDb) {
  writeTo(sink_, "calibration: silence {} -> {} dBFS", fromDb_, silenceDb);
}

void CalibrationJournal::appliedAnyway(double silenceDb) {
  writeTo(sink_, "calibration: applied anyway, silence {} dBFS", silenceDb);
}

void CalibrationJournal::closedBySound() {
  writeTo(sink_, "calibration: closed by sound after the result");
}

void CalibrationJournal::noSound() {
  writeTo(sink_, "calibration: no sound from the audio device for {} s",
          static_cast<double>(kNoSoundTime / core::Interval::kSecond));
}

} // namespace training::reaper
