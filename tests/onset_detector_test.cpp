#include <doctest/doctest.h>

#include "training/onset/detector.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <span>
#include <vector>

using training::onset::Detector;
using training::onset::Onset;
using training::onset::Settings;

namespace {

constexpr double kSampleRate = 44100.0;

/// Сигнал из щелчков: затухающая синусоида 3 кГц с амплитудой 0.5. Синусоида
/// начинается с нуля ровно на заданной позиции — это и есть начало щелчка.
std::vector<float> clicks(const std::vector<std::size_t> &starts, std::size_t length) {
  std::vector<float> signal(length, 0.0F);
  for (const std::size_t start : starts) {
    for (std::size_t i = 0; start + i < length && i < 2000; ++i) {
      const double t = static_cast<double>(i) / kSampleRate;
      const double value =
          0.5 * std::sin(2.0 * std::numbers::pi * 3000.0 * t) * std::exp(-t / 0.01);
      signal[start + i] += static_cast<float>(value);
    }
  }
  return signal;
}

/// Прогоняет сигнал через новый детектор кусками по `chunk` сэмплов.
std::vector<Onset> detect(const std::vector<float> &signal, std::size_t chunk) {
  Detector detector(Settings{.sampleRate = kSampleRate});
  std::vector<Onset> onsets;
  const std::span<const float> all(signal);
  for (std::size_t from = 0; from < all.size(); from += chunk)
    detector.process(all.subspan(from, std::min(chunk, all.size() - from)), onsets);
  return onsets;
}

} // namespace

TEST_CASE("детектор: щелчки находятся у своего начала") {
  const std::vector<std::size_t> starts{4410, 11025, 22050, 30000};
  const std::vector<Onset> onsets = detect(clicks(starts, 44100), 512);

  REQUIRE(onsets.size() == starts.size());
  for (std::size_t i = 0; i < starts.size(); ++i) {
    // Начало найдено в пределах 0.25 мс: 11 сэмплов при 44.1 кГц.
    CHECK(std::abs(onsets[i].position - static_cast<double>(starts[i])) < 11.0);
    CHECK(onsets[i].levelDb > -16.0);
  }
}

TEST_CASE("детектор: нарезка потока не меняет результат") {
  const std::vector<float> signal = clicks({1000, 5000, 9001, 20000, 31234}, 40000);
  const std::vector<Onset> reference = detect(signal, signal.size());
  REQUIRE(reference.size() == 5);

  for (const std::size_t chunk : {64U, 512U, 1000U}) {
    CAPTURE(chunk);
    const std::vector<Onset> onsets = detect(signal, chunk);
    REQUIRE(onsets.size() == reference.size());
    for (std::size_t i = 0; i < onsets.size(); ++i) {
      CHECK(onsets[i].position == reference[i].position);
      CHECK(onsets[i].levelDb == reference[i].levelDb);
    }
  }
}

TEST_CASE("детектор: мёртвое время глотает второй щелчок рядом") {
  // Второй щелчок через 10 мс — внутри мёртвого времени 25 мс; третий — через
  // 100 мс после первого, когда тот уже затих ниже порога.
  const std::vector<Onset> onsets = detect(clicks({4410, 4851, 8820}, 20000), 256);

  REQUIRE(onsets.size() == 2);
  CHECK(std::abs(onsets[0].position - 4410.0) < 11.0);
  CHECK(std::abs(onsets[1].position - 8820.0) < 11.0);
}

TEST_CASE("детектор: reset начинает поток заново") {
  const std::vector<float> signal = clicks({3000}, 10000);
  Detector detector(Settings{.sampleRate = kSampleRate});
  std::vector<Onset> first;
  detector.process(signal, first);
  CHECK(detector.position() == signal.size());

  detector.reset();
  CHECK(detector.position() == 0);

  std::vector<Onset> second;
  detector.process(signal, second);
  REQUIRE(first.size() == 1);
  REQUIRE(second.size() == 1);
  CHECK(second[0].position == first[0].position);
}

TEST_CASE("детектор: порог тишины меняется на ходу") {
  // Тихий щелчок −40 dBFS: при пороге −50 он атака, при пороге −30 — нет.
  std::vector<float> quiet = clicks({2000, 12000}, 20000);
  for (float &sample : quiet)
    sample *= 0.02F;

  Detector detector(Settings{.sampleRate = kSampleRate});
  std::vector<Onset> onsets;
  const std::span<const float> all(quiet);
  detector.process(all.first(10000), onsets);
  CHECK(onsets.size() == 1);

  detector.setSilenceDb(-30.0);
  detector.process(all.subspan(10000), onsets);
  CHECK(onsets.size() == 1);
  CHECK(detector.position() == quiet.size());
}
