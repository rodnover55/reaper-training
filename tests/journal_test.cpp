#include <doctest/doctest.h>

#include "journal.hpp"

#include <fmt/format.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using training::reaper::closeJournal;
using training::reaper::journalLine;
using training::reaper::kJournalLimit;
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
