#include <doctest/doctest.h>

#include "wav.hpp"

#include "training/onset/detector.hpp"

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <iterator>
#include <random>
#include <span>
#include <string>
#include <vector>

using training::onset::Detector;
using training::onset::Onset;
using training::onset::Settings;
using training::test::readWav;
using training::test::Recording;

namespace {

/// Записи DI-гитары и разметка настоящих атак — `tests/data/onset/README.md`.
const std::filesystem::path kRecordings =
    std::filesystem::path(TRAINING_TEST_DATA_DIR) / "onset";

/// Порог тишины, при котором записи размечены: DI-вход тихий.
constexpr double kSilenceDb = -60.0;

/// Допуск разметки, с.
constexpr double kTolerance = 0.015;

/// Запись и её размеченные атаки, с.
struct Labelled {
  std::string file;
  std::vector<double> onsets;
};

const std::vector<Labelled> kLabelled{
    {.file = "di_muted_quarters.wav",
     .onsets = {0.1752, 0.6642, 1.1862, 1.6651, 2.1543, 2.6691, 3.1752, 3.6917}},
    {.file = "di_ringing_low_b.wav", .onsets = {0.1249, 0.6382}},
    {.file = "di_ringing_low_b_long.wav", .onsets = {0.1586, 0.6502, 1.1249, 1.6382, 2.1381}},
};

/// Прогоняет запись через новый детектор кусками, длины которых даёт `next`.
template <class Next> std::vector<Onset> detect(const Recording &recording, Next next) {
  Detector detector(Settings{.sampleRate = recording.sampleRate, .silenceDb = kSilenceDb});
  std::vector<Onset> onsets;
  const std::span<const float> all(recording.samples);
  for (std::size_t from = 0; from < all.size();) {
    const std::size_t length = std::min(next(), all.size() - from);
    detector.process(all.subspan(from, length), onsets);
    from += length;
  }
  return onsets;
}

/// Проверяет запись: каждая размеченная атака найдена в пределах допуска, лишних
/// нет.
void checkLabels(const Labelled &labelled) {
  CAPTURE(labelled.file);
  const Recording recording = readWav(kRecordings / labelled.file);
  REQUIRE(recording.sampleRate == 48000.0);

  const std::vector<Onset> onsets = detect(recording, [] { return std::size_t{512}; });
  std::vector<double> found;
  std::ranges::transform(onsets, std::back_inserter(found), [&recording](const Onset &onset) {
    return onset.position / recording.sampleRate;
  });
  INFO("найдено: ", fmt::format("{:.4f}", fmt::join(found, " ")));

  REQUIRE(found.size() == labelled.onsets.size());
  for (std::size_t i = 0; i < found.size(); ++i) {
    CAPTURE(labelled.onsets[i]);
    CHECK(std::abs(found[i] - labelled.onsets[i]) <= kTolerance);
  }
}

} // namespace

TEST_CASE("детектор на записях: глушёные ноты — атака на щелчке, тело ноты не в счёт") {
  checkLabels(kLabelled[0]);
}

TEST_CASE("детектор на записях: звенящая низкая струна атак не даёт") {
  checkLabels(kLabelled[1]);
}

TEST_CASE("детектор на записях: удары по звенящей струне находятся и чуть выше звона") {
  checkLabels(kLabelled[2]);
}

TEST_CASE("детектор на записях: нарезка потока не меняет результат") {
  std::mt19937 random(5);
  std::uniform_int_distribution<std::size_t> anyLength(1, 3000);

  for (const Labelled &labelled : kLabelled) {
    CAPTURE(labelled.file);
    const Recording recording = readWav(kRecordings / labelled.file);
    const std::vector<Onset> reference =
        detect(recording, [&recording] { return recording.samples.size(); });

    for (const std::size_t chunk : {std::size_t{1}, std::size_t{480}, std::size_t{0}}) {
      CAPTURE(chunk);
      const std::vector<Onset> onsets = detect(recording, [chunk, &random, &anyLength] {
        return chunk > 0 ? chunk : anyLength(random);
      });
      REQUIRE(onsets.size() == reference.size());
      for (std::size_t i = 0; i < onsets.size(); ++i) {
        CHECK(onsets[i].position == reference[i].position);
        CHECK(onsets[i].levelDb == reference[i].levelDb);
      }
    }
  }
}
