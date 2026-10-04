#include <doctest/doctest.h>

#include "synth.hpp"

#include "training/onset/detector.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <random>
#include <span>
#include <vector>

using training::onset::Detector;
using training::onset::Onset;
using training::onset::Settings;
using training::test::evenPlucks;
using training::test::Pluck;
using training::test::render;
using training::test::SynthSettings;

namespace {

constexpr double kSampleRate = 48000.0;

/// 1 мс в сэмплах — допуск на начало ноты (`hit-timing`).
constexpr double kMillisecond = kSampleRate / 1000.0;

/// Прогоняет сигнал через новый детектор кусками, длины которых даёт `next`.
template <class Next> std::vector<Onset> detect(const std::vector<float> &signal, Next next) {
  Detector detector(Settings{.sampleRate = kSampleRate});
  std::vector<Onset> onsets;
  const std::span<const float> all(signal);
  for (std::size_t from = 0; from < all.size();) {
    const std::size_t length = std::min(next(), all.size() - from);
    detector.process(all.subspan(from, length), onsets);
    from += length;
  }
  return onsets;
}

std::vector<Onset> detect(const std::vector<float> &signal) {
  return detect(signal, [] { return std::size_t{512}; });
}

/// Одиночные щипки разных струн и громкостей, по полсекунды на щипок.
std::vector<Pluck> singlePlucks() {
  std::vector<Pluck> plucks;
  std::size_t start = 2400;
  for (const double amplitude : {0.9, 0.3, 0.05}) {
    for (const double frequency : {82.4, 110.0, 146.8, 196.0, 246.9, 329.6, 659.3}) {
      plucks.push_back({.start = start, .frequency = frequency, .amplitude = amplitude});
      start += 24000;
    }
  }
  return plucks;
}

} // namespace

TEST_CASE("детектор на синтезе: одиночные щипки — по атаке, не дальше 1 мс") {
  const std::vector<Pluck> plucks = singlePlucks();
  const std::vector<float> signal =
      render(plucks, plucks.back().start + 24000,
             SynthSettings{.sampleRate = kSampleRate, .noiseDb = -80.0});
  const std::vector<Onset> onsets = detect(signal);

  REQUIRE(onsets.size() == plucks.size());
  for (std::size_t i = 0; i < plucks.size(); ++i) {
    CAPTURE(i);
    CHECK(std::abs(onsets[i].position - static_cast<double>(plucks[i].start)) <= kMillisecond);
  }
}

TEST_CASE("детектор на синтезе: нарезка потока не меняет результат") {
  const std::vector<Pluck> plucks = evenPlucks(2400, 12000.0, 12, {110.0, 146.8, 196.0}, 0.5);
  const std::vector<float> signal =
      render(plucks, plucks.back().start + 24000,
             SynthSettings{.sampleRate = kSampleRate, .noiseDb = -70.0, .humDb = -50.0});
  const std::vector<Onset> reference = detect(signal, [&signal] { return signal.size(); });
  REQUIRE(!reference.empty());

  std::mt19937 random(7);
  std::uniform_int_distribution<std::size_t> anyLength(1, 3000);
  for (const std::size_t chunk : {std::size_t{1}, std::size_t{64}, std::size_t{512},
                                  std::size_t{1024}, std::size_t{0}}) {
    CAPTURE(chunk);
    const std::vector<Onset> onsets = detect(signal, [chunk, &random, &anyLength] {
      return chunk > 0 ? chunk : anyLength(random);
    });
    REQUIRE(onsets.size() == reference.size());
    for (std::size_t i = 0; i < onsets.size(); ++i)
      CHECK(std::abs(onsets[i].position - reference[i].position) < 1e-9);
  }
}

TEST_CASE("детектор на синтезе: шум и гул ниже порога тишины атак не дают") {
  // Шум −60 dBFS и гул −40 dBFS при пороге −50: гул срезает ВЧ-фильтр, шум
  // ниже порога. Щипок на 2 с — единственная атака.
  const std::vector<Pluck> plucks{{.start = 96000, .frequency = 110.0, .amplitude = 0.3}};
  const std::vector<float> signal =
      render(plucks, 144000,
             SynthSettings{.sampleRate = kSampleRate, .noiseDb = -60.0, .humDb = -40.0});
  const std::vector<Onset> onsets = detect(signal);

  REQUIRE(onsets.size() == 1);
  CHECK(std::abs(onsets[0].position - 96000.0) <= kMillisecond);
}
