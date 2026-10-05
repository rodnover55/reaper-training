#include <doctest/doctest.h>

#include "wav.hpp"

#include "training/onset/detector.hpp"

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <iterator>
#include <random>
#include <span>
#include <string_view>
#include <vector>

using training::onset::Detector;
using training::onset::Onset;
using training::onset::Settings;
using training::test::readWav;
using training::test::Recording;

namespace {

/// Записи DI-гитары; что на них и где настоящие атаки —
/// `tests/data/onset/README.md`.
Recording recording(std::string_view file) {
  return readWav(std::filesystem::path(TRAINING_TEST_DATA_DIR) / "onset" / file);
}

/// Порог тишины, при котором записи размечены: DI-вход тихий.
constexpr double kSilenceDb = -60.0;

/// Допуск разметки, с.
constexpr double kTolerance = 0.015;

/// Записи — все, что есть в `tests/data/onset`.
constexpr std::array<std::string_view, 3> kFiles{
    "di_muted_quarters.wav", "di_ringing_low_b.wav", "di_ringing_low_b_long.wav"};

/// Прогоняет запись через новый детектор кусками, длины которых даёт `next`.
template <class Next> std::vector<Onset> detect(const Recording &sound, Next next) {
  Detector detector(Settings{.sampleRate = sound.sampleRate, .silenceDb = kSilenceDb});
  std::vector<Onset> onsets;
  const std::span<const float> all(sound.samples);
  for (std::size_t from = 0; from < all.size();) {
    const std::size_t length = std::min(next(), all.size() - from);
    detector.process(all.subspan(from, length), onsets);
    from += length;
  }
  return onsets;
}

/// Проверяет запись `file`: каждая размеченная атака из `labels`, с, найдена
/// в пределах допуска, лишних нет.
void checkLabels(std::string_view file, const std::vector<double> &labels) {
  CAPTURE(file);
  const Recording sound = recording(file);
  REQUIRE(sound.sampleRate == 48000.0);

  const std::vector<Onset> onsets = detect(sound, [] { return std::size_t{512}; });
  std::vector<double> found;
  std::ranges::transform(onsets, std::back_inserter(found), [&sound](const Onset &onset) {
    return onset.position / sound.sampleRate;
  });
  INFO("найдено: ", fmt::format("{:.4f}", fmt::join(found, " ")));

  REQUIRE(found.size() == labels.size());
  for (std::size_t i = 0; i < found.size(); ++i) {
    CAPTURE(labels[i]);
    CHECK(std::abs(found[i] - labels[i]) <= kTolerance);
  }
}

} // namespace

TEST_CASE("детектор на записях: глушёные ноты — атака на щелчке, тело ноты не в счёт") {
  checkLabels(kFiles[0], {0.1752, 0.6642, 1.1862, 1.6651, 2.1543, 2.6691, 3.1752, 3.6917});
}

TEST_CASE("детектор на записях: звенящая низкая струна атак не даёт") {
  checkLabels(kFiles[1], {0.1249, 0.6382});
}

TEST_CASE("детектор на записях: удары по звенящей струне находятся и чуть выше звона") {
  checkLabels(kFiles[2], {0.1586, 0.6502, 1.1249, 1.6382, 2.1381});
}

TEST_CASE("детектор на записях: нарезка потока не меняет результат") {
  std::mt19937 random(5);
  std::uniform_int_distribution<std::size_t> anyLength(1, 3000);

  for (const std::string_view file : kFiles) {
    CAPTURE(file);
    const Recording sound = recording(file);
    const std::vector<Onset> reference =
        detect(sound, [&sound] { return sound.samples.size(); });

    for (const std::size_t chunk : {std::size_t{1}, std::size_t{480}, std::size_t{0}}) {
      CAPTURE(chunk);
      const std::vector<Onset> onsets = detect(sound, [chunk, &random, &anyLength] {
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
