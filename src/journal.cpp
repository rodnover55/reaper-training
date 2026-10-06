#include "journal.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <system_error>

namespace training::reaper {
namespace {

std::mutex journalMutex;
std::FILE *journalFile = nullptr;

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

} // namespace training::reaper
